#pragma once

#include "mc/deps/core/math/Color.h"

class ScreenContext;

class ScreenRenderer {
public:
    static ScreenRenderer& singleton() {
        static ScreenRenderer sInstance;
        return sInstance;
    }

    void fill(ScreenContext& /*ctx*/, float /*x*/, float /*y*/, float /*w*/, float /*h*/, mce::Color /*col*/) {}
};
