#pragma once

#include <cstddef>
#include <cstdint>

class Tessellator {
public:
    static Tessellator& getInstance() {
        static Tessellator sInstance;
        return sInstance;
    }

    void begin(int /*mode*/, size_t /*maxVertices*/ = 512) {}
    void vertex(float /*x*/, float /*y*/, float /*z*/) {}
    void color(float /*r*/, float /*g*/, float /*b*/, float /*a*/) {}
    void end() {}
};
