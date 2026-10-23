#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build"
DIST_DIR="${SCRIPT_DIR}/dist/AnimatedMojang"
LEVIPACK_OUT="${SCRIPT_DIR}/dist/animated_mojang.levipack"

export MALLOC_TAGGING_LEVEL=0

CXX_BIN="/data/data/com.termux/files/usr/bin/clang++"
if [ ! -x "${CXX_BIN}" ]; then
    CXX_BIN="/usr/bin/clang++"
fi
if [ ! -x "${CXX_BIN}" ]; then
    CXX_BIN="aarch64-linux-gnu-g++"
fi

echo "=== 1. Building Animated Mojang v1.0.0 for Android ARM64 using ${CXX_BIN} ==="
rm -rf "${BUILD_DIR}"
cmake -B "${BUILD_DIR}" -S "${SCRIPT_DIR}" -DCMAKE_CXX_COMPILER="${CXX_BIN}"
cmake --build "${BUILD_DIR}" --config Release -- -j1

echo "=== 2. Verifying Native Binary ABI Exports ==="
if command -v nm >/dev/null 2>&1; then
    nm -D "${BUILD_DIR}/libAnimatedMojang.so" | grep -E "(PLGetModRegistration|_ZNSt6__ndk113__hash_memory|_ZNSt6__ndk112__next_prime)"
fi

echo "=== 3. Packaging Native LeviPack Distribution (Zero RP, Direct Native Hooks) ==="
rm -rf "${SCRIPT_DIR}/dist"
mkdir -p "${DIST_DIR}"

cp -f "${BUILD_DIR}/libAnimatedMojang.so" "${DIST_DIR}/"
cp -f "${SCRIPT_DIR}/manifest.json" "${DIST_DIR}/"
cp -f "${SCRIPT_DIR}/icon.png" "${DIST_DIR}/"
cp -rf "${SCRIPT_DIR}/config" "${DIST_DIR}/"
mkdir -p "${DIST_DIR}/resources"
cp -rf "${SCRIPT_DIR}/resources/data" "${DIST_DIR}/resources/"
cp -rf "${SCRIPT_DIR}/resources/textures" "${DIST_DIR}/resources/"

# Build Native .levipack
(
    cd "${SCRIPT_DIR}/dist/AnimatedMojang"
    python3 -c "
import zipfile, os
with zipfile.ZipFile('${LEVIPACK_OUT}', 'w', zipfile.ZIP_DEFLATED) as zf:
    for root, dirs, files in os.walk('.'):
        for file in files:
            p = os.path.join(root, file)
            arc = os.path.relpath(p, '.')
            zf.write(p, arc)
"
)

# Copy levipack to Download and MG
cp -f "${LEVIPACK_OUT}" "/sdcard/MG/animated_mojang.levipack" 2>/dev/null || true
cp -f "${LEVIPACK_OUT}" "/sdcard/Download/animated_mojang.levipack" 2>/dev/null || true

# Auto-deploy to all active launcher profiles
for profile_dir in /sdcard/Android/media/org.levimc.launcher/minecraft/*/mods; do
    if [ -d "${profile_dir}" ]; then
        mod_dir="${profile_dir}/AnimatedMojang"
        mkdir -p "${mod_dir}"
        cp -f "${DIST_DIR}/libAnimatedMojang.so" "${mod_dir}/"
        cp -f "${DIST_DIR}/manifest.json" "${mod_dir}/"
        cp -f "${DIST_DIR}/icon.png" "${mod_dir}/"
        cp -rf "${DIST_DIR}/config" "${mod_dir}/"
        rm -rf "${mod_dir}/resources"
        cp -rf "${DIST_DIR}/resources" "${mod_dir}/"

        # Ensure AnimatedMojang is enabled in mods_config.json
        cfg_file="${profile_dir}/mods_config.json"
        python3 -c "
import json, os
p = '${cfg_file}'
if os.path.isfile(p):
    try:
        with open(p, 'r') as f: data = json.load(f)
        found = False
        for m in data:
            if m.get('name') == 'AnimatedMojang':
                m['enabled'] = True
                found = True
        if not found:
            data.append({'name': 'AnimatedMojang', 'enabled': True, 'order': len(data)})
        with open(p, 'w') as f: json.dump(data, f, indent=2)
    except Exception as e: print(f'mods_config error: {e}')
" 2>/dev/null || true
    fi
done

# Ensure dev_disableRenderMainMenuCubeMap:1 in all options.txt files
python3 -c "
import os, glob
targets = glob.glob('/sdcard/Android/media/org.levimc.launcher/minecraft/*/internal/games/com.mojang/minecraftpe/options.txt') + \
          glob.glob('/sdcard/Android/media/org.levimc.launcher/minecraft/*/external/games/com.mojang/minecraftpe/options.txt') + \
          ['/sdcard/games/com.mojang/minecraftpe/options.txt']
for p in targets:
    if os.path.isfile(p):
        with open(p, 'r', encoding='utf-8', errors='ignore') as f:
            lines = f.readlines()
        found = False
        new_lines = []
        for line in lines:
            if line.startswith('dev_disableRenderMainMenuCubeMap:'):
                new_lines.append('dev_disableRenderMainMenuCubeMap:1\n')
                found = True
            else:
                new_lines.append(line)
        if not found:
            new_lines.append('dev_disableRenderMainMenuCubeMap:1\n')
        with open(p, 'w', encoding='utf-8') as f:
            f.writelines(new_lines)
" 2>/dev/null || true

echo "=== Created LeviPack package ==="
echo "LeviPack: ${LEVIPACK_OUT}"
echo "=== Successfully built and packaged Animated Mojang v1.0.0 (Zero RP, Direct Native Hooks) ==="
