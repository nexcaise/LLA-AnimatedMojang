#include "ll/api/memory/Hook.h"
#include "ll/api/Logger.h"
#include <cstdio>
#include <cstring>
#include <unistd.h>
#include <sys/mman.h>
#include <dlfcn.h>

namespace ll::memory {

static uintptr_t sMinecraftBase = 0;

uintptr_t getMinecraftBase() {
    if (sMinecraftBase != 0) return sMinecraftBase;

    FILE* fp = fopen("/proc/self/maps", "r");
    if (!fp) return 0;

    char line[512];
    while (fgets(line, sizeof(line), fp)) {
        if (strstr(line, "libminecraftpe.so") && (strstr(line, "r-xp") || strstr(line, "r--p"))) {
            uintptr_t start = 0;
            if (sscanf(line, "%lx-", &start) == 1) {
                sMinecraftBase = start;
                break;
            }
        }
    }
    fclose(fp);

    if (sMinecraftBase == 0) {
        // Fallback to dlopen
        void* handle = dlopen("libminecraftpe.so", RTLD_NOLOAD);
        if (handle) {
            Dl_info info;
            if (dladdr(handle, &info) && info.dli_fbase) {
                sMinecraftBase = reinterpret_cast<uintptr_t>(info.dli_fbase);
            }
        }
    }

    if (sMinecraftBase != 0) {
        Logger::getInstance().info("Discovered libminecraftpe.so base: 0x" + std::to_string(sMinecraftBase));
    }
    return sMinecraftBase;
}

// Fallback AArch64 inline hook implementation if external Dobby library is not linked
static bool inlineHookAArch64(void* target, void* detour, void** original) {
    if (!target || !detour) return false;

    uintptr_t targetAddr = reinterpret_cast<uintptr_t>(target);
    long pageSize = sysconf(_SC_PAGESIZE);
    if (pageSize <= 0) pageSize = 4096;

    uintptr_t pageStart = targetAddr & ~(pageSize - 1);
    uintptr_t pageEnd = (targetAddr + 16 + pageSize - 1) & ~(pageSize - 1);
    size_t totalLen = pageEnd - pageStart;

    if (mprotect(reinterpret_cast<void*>(pageStart), totalLen, PROT_READ | PROT_WRITE | PROT_EXEC) != 0) {
        return false;
    }

    // Allocate 32-byte trampoline for original function bytes
    static uint8_t sTrampolinePool[4096] __attribute__((aligned(4096)));
    static size_t sPoolOffset = 0;

    if (sPoolOffset + 32 > sizeof(sTrampolinePool)) {
        return false;
    }

    uint8_t* trampoline = &sTrampolinePool[sPoolOffset];
    sPoolOffset += 32;

    mprotect(sTrampolinePool, sizeof(sTrampolinePool), PROT_READ | PROT_WRITE | PROT_EXEC);

    // Copy original 16 bytes
    std::memcpy(trampoline, target, 16);

    // Append jump back to target + 16 in trampoline
    // LDR X16, #8; BR X16; <targetAddr + 16>
    uint32_t jumpBack[4] = {
        0x58000050, // ldr x16, #8
        0xd61f0200, // br x16
        static_cast<uint32_t>((targetAddr + 16) & 0xFFFFFFFF),
        static_cast<uint32_t>((targetAddr + 16) >> 32)
    };
    std::memcpy(trampoline + 16, jumpBack, 16);

    if (original) {
        *original = trampoline;
    }

    // Write detour jump to target: LDR X16, #8; BR X16; <detourAddr>
    uintptr_t detourAddr = reinterpret_cast<uintptr_t>(detour);
    uint32_t hookCode[4] = {
        0x58000050, // ldr x16, #8
        0xd61f0200, // br x16
        static_cast<uint32_t>(detourAddr & 0xFFFFFFFF),
        static_cast<uint32_t>(detourAddr >> 32)
    };
    std::memcpy(target, hookCode, 16);

    __builtin___clear_cache(reinterpret_cast<char*>(pageStart), reinterpret_cast<char*>(pageEnd));
    __builtin___clear_cache(reinterpret_cast<char*>(trampoline), reinterpret_cast<char*>(trampoline) + 32);

    return true;
}

bool hook(void* target, void* detour, void** original) {
    if (!target || !detour) return false;

    // 1. Attempt LeviLauncher PreLoader hook API
    using PlMemoryHookFn = int (*)(void*, void*, void**, int);
    static auto fnPlMemHook = reinterpret_cast<PlMemoryHookFn>(dlsym(RTLD_DEFAULT, "_ZN2pl6memory4hookEPvS1_PS1_NS0_12HookPriorityE"));
    if (fnPlMemHook) {
        int res = fnPlMemHook(target, detour, original, 0);
        return res == 0;
    }

    using PlHookFn = int (*)(void*, void*, void**);
    static auto fnPlHook = reinterpret_cast<PlHookFn>(dlsym(RTLD_DEFAULT, "pl_hook"));
    if (fnPlHook) {
        int res = fnPlHook(target, detour, original);
        return res == 0;
    }

    // 2. Attempt GlossHook from libpreloader
    using GlossHookFn = int (*)(void*, void*, void**);
    static auto fnGlossHook = reinterpret_cast<GlossHookFn>(dlsym(RTLD_DEFAULT, "GlossHook"));
    if (fnGlossHook) {
        int res = fnGlossHook(target, detour, original);
        return res == 0;
    }

    // 3. Attempt Dobby dynamic hook
    void* dobbyHandle = dlopen("libdobby.so", RTLD_NOLOAD);
    if (dobbyHandle) {
        using DobbyHookFn = int (*)(void*, void*, void**);
        auto fn = reinterpret_cast<DobbyHookFn>(dlsym(dobbyHandle, "DobbyHook"));
        if (fn) {
            return fn(target, detour, original) == 0;
        }
    }
    return inlineHookAArch64(target, detour, original);
}

bool hookRva(uintptr_t rva, void* detour, void** original) {
    uintptr_t base = getMinecraftBase();
    if (!base) return false;
    return hook(reinterpret_cast<void*>(base + rva), detour, original);
}

} // namespace ll::memory
