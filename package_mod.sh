#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build"
DIST_DIR="${SCRIPT_DIR}/dist/AnimatedMojang"
ARCHIVE_ZIP="${SCRIPT_DIR}/dist/AnimatedMojang-1.26.52.3-android-arm64.zip"
LEVIPACK_OUT="${SCRIPT_DIR}/dist/animated_mojang.levipack"

export MALLOC_TAGGING_LEVEL=0

CXX_BIN="/data/data/com.termux/files/usr/bin/clang++"
if [ ! -x "${CXX_BIN}" ]; then
    CXX_BIN="/usr/bin/clang++"
fi
if [ ! -x "${CXX_BIN}" ]; then
    CXX_BIN="aarch64-linux-gnu-g++"
fi

echo "=== 1. Building Animated Mojang for Android ARM64 using ${CXX_BIN} ==="
rm -rf "${BUILD_DIR}"
cmake -B "${BUILD_DIR}" -S "${SCRIPT_DIR}" -DCMAKE_CXX_COMPILER="${CXX_BIN}"
cmake --build "${BUILD_DIR}" --config Release -- -j1

echo "=== 2. Verifying Native Binary ABI Exports ==="
if command -v nm >/dev/null 2>&1; then
    nm -D "${BUILD_DIR}/libAnimatedMojang.so" | grep -E "(PLGetModRegistration|_ZNSt6__ndk113__hash_memory|_ZNSt6__ndk112__next_prime)"
fi

echo "=== 3. Packaging LeviLaunchroid Distribution ==="
rm -rf "${SCRIPT_DIR}/dist"
mkdir -p "${DIST_DIR}"

cp -f "${BUILD_DIR}/libAnimatedMojang.so" "${DIST_DIR}/"
cp -f "${SCRIPT_DIR}/manifest.json" "${DIST_DIR}/"
cp -f "${SCRIPT_DIR}/icon.png" "${DIST_DIR}/"
cp -rf "${SCRIPT_DIR}/config" "${DIST_DIR}/"
mkdir -p "${DIST_DIR}/resources"
cp -rf "${SCRIPT_DIR}/resources/data" "${DIST_DIR}/resources/"
cp -rf "${SCRIPT_DIR}/resources/textures" "${DIST_DIR}/resources/"

# Build Standalone Zip
(
    cd "${SCRIPT_DIR}/dist"
    python3 -c "
import zipfile, os
with zipfile.ZipFile('${ARCHIVE_ZIP}', 'w', zipfile.ZIP_DEFLATED) as zf:
    for root, dirs, files in os.walk('AnimatedMojang'):
        for file in files:
            p = os.path.join(root, file)
            zf.write(p, p)
"
)

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

# Copy Archive and levipack to Download and MG
cp -f "${LEVIPACK_OUT}" "/sdcard/MG/animated_mojang.levipack" 2>/dev/null || true
cp -f "${LEVIPACK_OUT}" "/sdcard/Download/animated_mojang.levipack" 2>/dev/null || true
cp -f "${ARCHIVE_ZIP}" "/sdcard/MG/AnimatedMojang-1.0.8-android-arm64.zip" 2>/dev/null || true
cp -f "${ARCHIVE_ZIP}" "/sdcard/Download/AnimatedMojang-1.0.8-android-arm64.zip" 2>/dev/null || true

# Auto-deploy to all active LeviLauncher profiles
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
"
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
"

# Build Resource Pack .mcpack
MCPACK_OUT="${SCRIPT_DIR}/dist/AnimatedMojang_RP.mcpack"
RP_SRC="${SCRIPT_DIR}/resources/minecraft_resource_packs/AnimatedMojang_RP"
if [ -d "${RP_SRC}" ]; then
    echo "=== Packaging AnimatedMojang_RP.mcpack ==="
    (
        cd "${RP_SRC}"
        python3 -c "
import zipfile, os
with zipfile.ZipFile('${MCPACK_OUT}', 'w', zipfile.ZIP_DEFLATED) as zf:
    for root, dirs, files in os.walk('.'):
        for file in files:
            p = os.path.join(root, file)
            arc = os.path.relpath(p, '.')
            zf.write(p, arc)
"
    )
    cp -f "${MCPACK_OUT}" "/sdcard/Download/AnimatedMojang_RP.mcpack"
    cp -f "${MCPACK_OUT}" "/sdcard/MG/AnimatedMojang_RP.mcpack"

    # Synchronize loose AnimatedMojang_RP directly to all resource_packs folders
    for profile in /sdcard/Android/media/org.levimc.launcher/minecraft/*; do
        if [ -d "${profile}" ]; then
            for loc in "internal" "external"; do
                if [ -d "${profile}/${loc}" ]; then
                    for sub in "resource_packs" "development_resource_packs"; do
                        rp_dir="${profile}/${loc}/games/com.mojang/${sub}/AnimatedMojang_RP"
                        mkdir -p "${rp_dir}"
                        cp -rf "${RP_SRC}/"* "${rp_dir}/"
                    done
                fi
            done
        fi
    done
    mkdir -p "/sdcard/games/com.mojang/resource_packs/AnimatedMojang_RP"
    cp -rf "${RP_SRC}/"* "/sdcard/games/com.mojang/resource_packs/AnimatedMojang_RP/"

    # Register AnimatedMojang_RP globally in all global_resource_packs.json files
    python3 -c "
import json, glob, os
pack_entry = { 'pack_id': '8e4d2a10-4820-4b21-81d3-359f4d1e00a1', 'version': [1, 0, 8] }
targets = glob.glob('/sdcard/Android/media/org.levimc.launcher/minecraft/*/internal/games/com.mojang/minecraftpe/global_resource_packs.json') + \
          glob.glob('/sdcard/Android/media/org.levimc.launcher/minecraft/*/external/games/com.mojang/minecraftpe/global_resource_packs.json') + \
          ['/sdcard/games/com.mojang/minecraftpe/global_resource_packs.json']
for gp in targets:
    try:
        os.makedirs(os.path.dirname(gp), exist_ok=True)
        if os.path.isfile(gp):
            with open(gp, 'r') as f:
                data = json.load(f)
        else:
            data = []
        if not isinstance(data, list):
            data = []
        # Filter out existing entries with this UUID and prepend
        data = [p for p in data if p.get('pack_id') != '8e4d2a10-4820-4b21-81d3-359f4d1e00a1']
        data.insert(0, pack_entry)
        with open(gp, 'w') as f:
            json.dump(data, f, indent=2)
        print(f'Registered AnimatedMojang_RP globally in {gp}')
    except Exception as e:
        print(f'Error registering {gp}: {e}')
"
fi

# Package all-in-one distribution suite zip
SUITE_ZIP="${SCRIPT_DIR}/dist/AnimatedMojang_Suite_Android.zip"
python3 -c "
import zipfile, os
suite_files = [
    ('${MCPACK_OUT}', 'AnimatedMojang_RP.mcpack'),
    ('${LEVIPACK_OUT}', 'animated_mojang.levipack'),
    ('${ARCHIVE_ZIP}', 'AnimatedMojang-1.0.8-android-arm64.zip'),
    ('${SCRIPT_DIR}/README.md', 'README.md')
]
with zipfile.ZipFile('${SUITE_ZIP}', 'w', zipfile.ZIP_DEFLATED) as zf:
    for src, arc in suite_files:
        if os.path.isfile(src):
            zf.write(src, arc)
"
cp -f "${SUITE_ZIP}" "/sdcard/Download/AnimatedMojang_Suite_Android.zip"
cp -f "${SUITE_ZIP}" "/sdcard/MG/AnimatedMojang_Suite_Android.zip"
echo "=== Created distribution suite: ${SUITE_ZIP} ==="

echo "=== Successfully built, packaged, and deployed Animated Mojang v1.0.2 (Bedrock Multi-Version) ==="


