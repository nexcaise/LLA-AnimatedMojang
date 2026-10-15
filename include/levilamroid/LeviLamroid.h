#pragma once
#include "ll/api/Macros.h"
#include "ll/api/ui/UI.h"
#include "ll/api/render/Render.h"
#include <string>
#include <dlfcn.h>

extern "C" {
LL_API bool ll_is_in_world();
LL_API bool ll_is_main_menu();
}

namespace ll {

inline bool isInWorld() {
    using Fn = bool (*)();
    static auto fn = reinterpret_cast<Fn>(dlsym(ui::getLoaderHandle(), "ll_is_in_world"));
    if (fn) return fn();
    return false;
}

inline bool isMainMenu() {
    using Fn = bool (*)();
    static auto fn = reinterpret_cast<Fn>(dlsym(ui::getLoaderHandle(), "ll_is_main_menu"));
    if (fn) return fn();
    return false;
}

} // namespace ll
