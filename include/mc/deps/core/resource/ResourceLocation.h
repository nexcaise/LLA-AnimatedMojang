#pragma once

#include <string>
#include <string_view>

namespace Core {

struct PathView {
    std::string path;

    PathView() = default;
    explicit PathView(std::string_view p) : path(p) {}
    explicit PathView(const std::string& p) : path(p) {}
};

} // namespace Core

enum class ResourceFileSystem : int {
    Raw = 0,
    App = 1,
    User = 2
};

struct ResourceLocation {
    std::string path;
    ResourceFileSystem fs{ResourceFileSystem::Raw};

    ResourceLocation() = default;
    ResourceLocation(Core::PathView pv, ResourceFileSystem f) : path(pv.path), fs(f) {}
    ResourceLocation(std::string_view p, ResourceFileSystem f) : path(p), fs(f) {}
};
