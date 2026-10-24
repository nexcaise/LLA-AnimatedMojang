#!/usr/bin/env bash
# Assemble dist/AnimatedMojang + a .levipack from an already-built libAnimatedMojang.so.
# Used by CI; package_mod.sh remains the on-device (Termux) flow with sdcard deployment.
#   tools/make_levipack.sh <path/to/libAnimatedMojang.so> [out.levipack]
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SO="${1:?usage: make_levipack.sh <libAnimatedMojang.so> [out.levipack]}"
OUT="${2:-$ROOT/dist/animated_mojang.levipack}"
STAGE="$ROOT/dist/AnimatedMojang"

[ -f "$SO" ] || { echo "missing $SO" >&2; exit 1; }
BAKED="$ROOT/resources/textures/animated_mojang/title/baked/title_atlas.json"
[ -f "$BAKED" ] || { echo "baked title atlas missing: run tools/bake_title_atlas.py" >&2; exit 1; }

rm -rf "$ROOT/dist"; mkdir -p "$STAGE" "$(dirname "$OUT")"
cp -f "$SO" "$STAGE/libAnimatedMojang.so"
cp -f "$ROOT/manifest.json" "$ROOT/icon.png" "$STAGE/"
cp -rf "$ROOT/config" "$STAGE/"
cp -rf "$ROOT/resources" "$STAGE/"
# raw strips are only the source for the baker; not used at runtime
rm -rf "$STAGE/resources/textures/animated_mojang/title/minecraft_animation"

python3 - "$STAGE" "$OUT" <<'PY'
import os, sys, zipfile
stage, out = sys.argv[1:3]
with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as zf:
    for root, _, files in os.walk(stage):
        for f in sorted(files):
            p = os.path.join(root, f)
            zf.write(p, os.path.relpath(p, stage))
print(f"wrote {out} ({os.path.getsize(out)/1048576:.1f} MB)")
PY
