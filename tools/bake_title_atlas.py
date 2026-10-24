#!/usr/bin/env python3
"""
Bake the 9 giant title strips (2403x5992 RGBA, ~55 MB each once decoded) into a
compact set of 2048x2048 atlas pages.

What it does
  * splits every strip into its 8 frames (69 frames total: 0..68)
  * crops each frame to the bounding box of its visible (alpha > 0) pixels
  * downscales the crop (default 0.5x, premultiplied alpha, so no dark fringes)
  * de-duplicates identical frames (the resting frames 66-68 are the same image)
  * packs everything, in playback order, into 2048x2048 pages (the GLES 3.0
    guaranteed minimum texture size, so it works on every device)

Output (next to the source strips):  title/baked/title_atlas.json
                                      title/baked/title_atlas_<n>.png
Pixels are stored PREMULTIPLIED. The runtime blends with (ONE, ONE_MINUS_SRC_ALPHA).

Usage:  python3 tools/bake_title_atlas.py [--scale 0.5] [--src DIR] [--out DIR]
Requires: Pillow, numpy
"""
import argparse, hashlib, json, math, os, sys
import numpy as np
from PIL import Image

FRAME_W, FRAME_H = 2403, 749
FRAMES_PER_STRIP = 8
TOTAL_FRAMES = 69
PAGE = 2048
GUTTER = 2  # transparent pixels around each frame so bilinear filtering never bleeds


def load_frames(src):
    frames = []
    for s in range(9):
        path = os.path.join(src, f"minecraft_{s}.png")
        im = Image.open(path).convert("RGBA")
        if im.size != (FRAME_W, FRAME_H * FRAMES_PER_STRIP):
            sys.exit(f"{path}: unexpected size {im.size}")
        for f in range(FRAMES_PER_STRIP):
            if len(frames) >= TOTAL_FRAMES:
                break
            frames.append(im.crop((0, f * FRAME_H, FRAME_W, (f + 1) * FRAME_H)))
        im.close()
    return frames


def bake(frames, scale):
    entries, unique = [], {}  # unique: digest -> (image bytes premultiplied, w, h)
    cooked = []
    for idx, fr in enumerate(frames):
        a = np.array(fr)[:, :, 3]
        ys, xs = np.nonzero(a)
        if len(xs) == 0:
            cooked.append(None)
            continue
        x0, y0, x1, y1 = int(xs.min()), int(ys.min()), int(xs.max()) + 1, int(ys.max()) + 1
        crop = fr.crop((x0, y0, x1, y1))
        w = max(1, math.ceil((x1 - x0) * scale))
        h = max(1, math.ceil((y1 - y0) * scale))
        # 'RGBa' = premultiplied; resizing in that space is the correct way to filter alpha art
        small = crop.convert("RGBa").resize((w, h), Image.LANCZOS)
        arr = np.array(small)  # premultiplied uint8
        digest = hashlib.sha1(arr.tobytes() + bytes([x0 & 255, y0 & 255])).hexdigest()
        cooked.append(dict(digest=digest, arr=arr, w=w, h=h, crop=(x0, y0, x1 - x0, y1 - y0)))

    # shelf packing in playback order
    pages = [np.zeros((PAGE, PAGE, 4), np.uint8)]
    cursor_x = cursor_y = shelf_h = 0
    placed = {}  # digest -> (page, x, y)
    for idx, c in enumerate(cooked):
        if c is None:
            entries.append({"e": 1})
            continue
        if c["digest"] in placed:
            p, x, y = placed[c["digest"]]
        else:
            w, h = c["w"], c["h"]
            if w + 2 * GUTTER > PAGE or h + 2 * GUTTER > PAGE:
                sys.exit("frame larger than a page; lower --scale")
            if cursor_x + w + 2 * GUTTER > PAGE:
                cursor_x, cursor_y, shelf_h = 0, cursor_y + shelf_h, 0
            if cursor_y + h + 2 * GUTTER > PAGE:
                pages.append(np.zeros((PAGE, PAGE, 4), np.uint8))
                cursor_x = cursor_y = shelf_h = 0
            p = len(pages) - 1
            x, y = cursor_x + GUTTER, cursor_y + GUTTER
            pages[p][y:y + h, x:x + w] = c["arr"]
            cursor_x += w + 2 * GUTTER
            shelf_h = max(shelf_h, h + 2 * GUTTER)
            placed[c["digest"]] = (p, x, y)
        cx, cy, cw, ch = c["crop"]
        entries.append({"p": p, "x": x, "y": y, "w": c["w"], "h": c["h"], "cx": cx, "cy": cy, "cw": cw, "ch": ch})

    # trim the last page's empty bottom (keeps VRAM tight); height rounded to 8 rows
    used_h = []
    for i, pg in enumerate(pages):
        rows = np.nonzero(pg[:, :, 3].any(axis=1))[0]
        hh = int(rows.max()) + 1 + GUTTER if len(rows) else 8
        used_h.append(min(PAGE, (hh + 7) // 8 * 8))
    return pages, used_h, entries


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    root = os.path.dirname(here)
    default_src = os.path.join(root, "resources", "textures", "animated_mojang", "title", "minecraft_animation")
    default_out = os.path.join(root, "resources", "textures", "animated_mojang", "title", "baked")
    ap = argparse.ArgumentParser()
    ap.add_argument("--scale", type=float, default=0.5, help="downscale factor vs. the 2403px source (default 0.5)")
    ap.add_argument("--src", default=default_src)
    ap.add_argument("--out", default=default_out)
    args = ap.parse_args()

    frames = load_frames(args.src)
    pages, heights, entries = bake(frames, args.scale)
    os.makedirs(args.out, exist_ok=True)
    for f in os.listdir(args.out):
        if f.startswith("title_atlas"):
            os.remove(os.path.join(args.out, f))
    names, total_px = [], 0
    for i, pg in enumerate(pages):
        name = f"title_atlas_{i}.png"
        Image.fromarray(pg[:heights[i]], "RGBA").save(os.path.join(args.out, name), optimize=True)
        names.append({"file": name, "w": PAGE, "h": heights[i]})
        total_px += PAGE * heights[i]
    meta = {"version": 1, "srcFrame": [FRAME_W, FRAME_H], "scale": args.scale,
            "premultiplied": True, "pages": names, "frames": entries}
    with open(os.path.join(args.out, "title_atlas.json"), "w") as fh:
        json.dump(meta, fh, separators=(",", ":"))
    print(f"frames={len(entries)} pages={len(pages)} vram={total_px*4/1048576:.1f} MB "
          f"(was {9*FRAME_W*FRAME_H*FRAMES_PER_STRIP*4/1048576:.0f} MB)")


if __name__ == "__main__":
    main()
