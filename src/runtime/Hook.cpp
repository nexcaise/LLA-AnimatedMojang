#include "ll/api/memory/Hook.h"
#include "ll/api/Logger.h"
#include <cstdio>
#include <cstring>
#include <unistd.h>
#include <sys/mman.h>
#include <dlfcn.h>
#include <elf.h>
#include <link.h>
#include <vector>

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

bool hookGotSlot(uintptr_t gotRva, void* detour, void** original) {
    uintptr_t base = getMinecraftBase();
    if (!base) return false;

    uintptr_t target = base + gotRva;
    long pageSize = sysconf(_SC_PAGESIZE);
    if (pageSize <= 0) pageSize = 4096;
    uintptr_t pageStart = target & ~(pageSize - 1);

    if (mprotect(reinterpret_cast<void*>(pageStart), pageSize, PROT_READ | PROT_WRITE) != 0) {
        return false;
    }

    auto* slot = reinterpret_cast<void**>(target);
    if (original) {
        *original = *slot;
    }
    *slot = detour;
    return true;
}

// ----------------------------------------------------------------------------
// Multi-Version Runtime Pattern Scanner (IDA-style hex mask)
// ----------------------------------------------------------------------------
struct PatternByte {
    uint8_t value{0};
    bool isWildcard{true};
};

static std::vector<PatternByte> parseIdaPattern(std::string_view pattern) {
    std::vector<PatternByte> bytes;
    const char* p = pattern.data();
    const char* end = p + pattern.size();

    while (p < end) {
        if (*p == ' ') {
            p++;
            continue;
        }
        if (*p == '?') {
            bytes.push_back({0, true});
            p++;
            if (p < end && *p == '?') p++;
        } else {
            char hex[3] = {p[0], (p + 1 < end && *(p + 1) != ' ') ? *(p + 1) : '\0', '\0'};
            bytes.push_back({static_cast<uint8_t>(std::strtoul(hex, nullptr, 16)), false});
            p += (hex[1] != '\0') ? 2 : 1;
        }
    }
    return bytes;
}

struct MemorySegment {
    uintptr_t start;
    size_t size;
};

static std::vector<MemorySegment> sSegments;

static void discoverSegments() {
    if (!sSegments.empty()) return;

    FILE* fp = fopen("/proc/self/maps", "r");
    if (fp) {
        char line[512];
        while (fgets(line, sizeof(line), fp)) {
            if (strstr(line, "libminecraftpe.so") && (strstr(line, "r-xp") || strstr(line, "r--p"))) {
                uintptr_t start = 0, end = 0;
                if (sscanf(line, "%lx-%lx", &start, &end) == 2 && end > start) {
                    sSegments.push_back({start, static_cast<size_t>(end - start)});
                }
            }
        }
        fclose(fp);
    }

    if (sSegments.empty()) {
        uintptr_t base = getMinecraftBase();
        if (base) {
            sSegments.push_back({base, 0x15000000}); // 336MB fallback
        }
    }
}

uintptr_t findSignature(const std::string& patternStr) {
    discoverSegments();
    if (sSegments.empty()) return 0;

    auto pattern = parseIdaPattern(patternStr);
    if (pattern.empty()) return 0;

    size_t anchorIdx = 0;
    while (anchorIdx < pattern.size() && pattern[anchorIdx].isWildcard) {
        anchorIdx++;
    }
    if (anchorIdx >= pattern.size()) return 0;

    uint8_t anchorVal = pattern[anchorIdx].value;

    for (const auto& seg : sSegments) {
        if (seg.size < pattern.size()) continue;

        const uint8_t* startPtr = reinterpret_cast<const uint8_t*>(seg.start);
        const size_t maxScan = seg.size - pattern.size();
        const uint8_t* scanPtr = startPtr + anchorIdx;
        const uint8_t* endPtr = startPtr + seg.size;

        while (scanPtr < endPtr) {
            const uint8_t* match = static_cast<const uint8_t*>(std::memchr(scanPtr, anchorVal, endPtr - scanPtr));
            if (!match) break;

            const uint8_t* candidate = match - anchorIdx;
            if (candidate >= startPtr && candidate <= startPtr + maxScan) {
                bool fullMatch = true;
                for (size_t j = 0; j < pattern.size(); ++j) {
                    if (!pattern[j].isWildcard && candidate[j] != pattern[j].value) {
                        fullMatch = false;
                        break;
                    }
                }
                if (fullMatch) {
                    return reinterpret_cast<uintptr_t>(candidate);
                }
            }
            scanPtr = match + 1;
        }
    }
    return 0;
}

bool hookSignature(const std::string& pattern, void* detour, void** original, uintptr_t fallbackRva) {
    uintptr_t addr = findSignature(pattern);
    if (addr != 0) {
        uintptr_t base = getMinecraftBase();
        uintptr_t rva = base ? (addr - base) : addr;
        char buf[96];
        snprintf(buf, sizeof(buf), "[AnimatedMojang] Hooked signature at %p (RVA 0x%lx)", reinterpret_cast<void*>(addr), rva);
        Logger::getInstance().info(buf);
        return hook(reinterpret_cast<void*>(addr), detour, original);
    }

    if (fallbackRva != 0) {
        char buf[96];
        snprintf(buf, sizeof(buf), "[AnimatedMojang] Pattern not matched, using version RVA fallback 0x%lx", fallbackRva);
        Logger::getInstance().warn(buf);
        return hookRva(fallbackRva, detour, original);
    }

    Logger::getInstance().error("[AnimatedMojang] hookSignature failed: pattern not matched and no fallback RVA");
    return false;
}

// ----------------------------------------------------------------------------
// Multi-Version ELF Dynamic GOT Relocation Resolver
// ----------------------------------------------------------------------------
static uintptr_t findGotSlotRvaFromElf(const char* symbolName) {
    struct SearchContext {
        const char* symName;
        uintptr_t foundRva;
    } ctx = { symbolName, 0 };

    dl_iterate_phdr([](struct dl_phdr_info* info, size_t, void* data) -> int {
        auto* sc = static_cast<SearchContext*>(data);
        if (!info->dlpi_name || !strstr(info->dlpi_name, "libminecraftpe.so")) {
            return 0;
        }

        const Elf64_Phdr* dynamicPhdr = nullptr;
        for (int i = 0; i < info->dlpi_phnum; ++i) {
            if (info->dlpi_phdr[i].p_type == PT_DYNAMIC) {
                dynamicPhdr = &info->dlpi_phdr[i];
                break;
            }
        }
        if (!dynamicPhdr) return 0;

        uintptr_t dynamicAddr = info->dlpi_addr + dynamicPhdr->p_vaddr;
        const auto* dyn = reinterpret_cast<const Elf64_Dyn*>(dynamicAddr);

        uintptr_t jmprel = 0;
        size_t pltrelsz = 0;
        uintptr_t symtab = 0;
        uintptr_t strtab = 0;

        for (; dyn->d_tag != DT_NULL; ++dyn) {
            if (dyn->d_tag == DT_JMPREL) jmprel = dyn->d_un.d_ptr;
            else if (dyn->d_tag == DT_PLTRELSZ) pltrelsz = dyn->d_un.d_val;
            else if (dyn->d_tag == DT_SYMTAB) symtab = dyn->d_un.d_ptr;
            else if (dyn->d_tag == DT_STRTAB) strtab = dyn->d_un.d_ptr;
        }

        if (!jmprel || !pltrelsz || !symtab || !strtab) return 0;

        if (jmprel < info->dlpi_addr) jmprel += info->dlpi_addr;
        if (symtab < info->dlpi_addr) symtab += info->dlpi_addr;
        if (strtab < info->dlpi_addr) strtab += info->dlpi_addr;

        const auto* rela = reinterpret_cast<const Elf64_Rela*>(jmprel);
        const auto* syms = reinterpret_cast<const Elf64_Sym*>(symtab);
        const char* strings = reinterpret_cast<const char*>(strtab);

        size_t numRelas = pltrelsz / sizeof(Elf64_Rela);
        for (size_t i = 0; i < numRelas; ++i) {
            uint32_t symIdx = ELF64_R_SYM(rela[i].r_info);
            const char* name = strings + syms[symIdx].st_name;
            if (strcmp(name, sc->symName) == 0) {
                sc->foundRva = rela[i].r_offset;
                return 1;
            }
        }
        return 0;
    }, &ctx);

    return ctx.foundRva;
}

bool hookGotSymbol(const char* symbolName, void* detour, void** original, uintptr_t fallbackGotRva) {
    uintptr_t rva = findGotSlotRvaFromElf(symbolName);
    if (rva != 0) {
        char buf[96];
        snprintf(buf, sizeof(buf), "[AnimatedMojang] Dynamically resolved GOT slot for %s at RVA 0x%lx", symbolName, rva);
        Logger::getInstance().info(buf);
        return hookGotSlot(rva, detour, original);
    }
    if (fallbackGotRva != 0) {
        char buf[96];
        snprintf(buf, sizeof(buf), "[AnimatedMojang] Falling back to version GOT RVA 0x%lx for %s", fallbackGotRva, symbolName);
        Logger::getInstance().warn(buf);
        return hookGotSlot(fallbackGotRva, detour, original);
    }
    Logger::getInstance().error(std::string("[AnimatedMojang] hookGotSymbol failed for: ") + symbolName);
    return false;
}

} // namespace ll::memory
