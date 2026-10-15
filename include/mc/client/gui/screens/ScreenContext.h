#pragma once

#include <memory>
#include <optional>
#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>
#include "mc/deps/core/math/Color.h"

struct ShaderColor {
    struct ColorWrapper {
        mce::Color color{255, 255, 255};
        mce::Color get() const { return color; }
        ColorWrapper& operator=(const mce::Color& c) {
            color = c;
            return *this;
        }
    } color;
    bool dirty{false};
};

struct MatrixStack {
    struct MatrixStackRef {
        int dummy{0};
    };

    struct TopMatrix {
        glm::mat4 _m{1.0f};
    };

    MatrixStackRef push(bool = false) { return {}; }
    TopMatrix& getTop() {
        static TopMatrix sTop;
        return sTop;
    }
};

struct Camera {
    std::shared_ptr<MatrixStack> worldMatrixStack = std::make_shared<MatrixStack>();
};

class ScreenContext {
public:
    ShaderColor currentShaderColor;
    std::optional<glm::vec4> normalizedClipRegion;
    Camera camera;
};

struct FrameRenderObject {};
