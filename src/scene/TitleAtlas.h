#pragma once
// Metadata of the baked title atlas produced by tools/bake_title_atlas.py.
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace animated_mojang::scene {

struct TitlePageInfo {
    std::string file;
    int w = 0, h = 0;
};

struct TitleFrame {
    bool empty = true;
    int page = 0;
    int x = 0, y = 0, w = 0, h = 0;     // rectangle inside the atlas page (pixels)
    int cx = 0, cy = 0, cw = 0, ch = 0; // where that rectangle sits inside the 2403x749 source frame
};

struct TitleAtlasInfo {
    std::filesystem::path directory;
    std::vector<TitlePageInfo> pages;
    std::vector<TitleFrame> frames;

    int restingFrame() const { return frames.empty() ? 0 : static_cast<int>(frames.size()) - 1; }
    int restingPage() const { return frames.empty() ? 0 : frames.back().page; }
};

inline bool parseTitleAtlas(std::filesystem::path const& jsonPath, TitleAtlasInfo& out, std::string& error) {
    try {
        std::ifstream in(jsonPath);
        if (!in) { error = "cannot open " + jsonPath.string(); return false; }
        nlohmann::json j = nlohmann::json::parse(in);
        if (j.value("version", 0) != 1) { error = "unsupported title atlas version"; return false; }

        TitleAtlasInfo info;
        info.directory = jsonPath.parent_path();
        for (auto const& p : j.at("pages")) {
            TitlePageInfo page{p.at("file").get<std::string>(), p.at("w").get<int>(), p.at("h").get<int>()};
            if (page.w <= 0 || page.h <= 0 || page.w > 4096 || page.h > 4096 || page.file.empty()
                || page.file.find("..") != std::string::npos || page.file.find('/') != std::string::npos) {
                error = "invalid page entry";
                return false;
            }
            info.pages.push_back(std::move(page));
        }
        for (auto const& f : j.at("frames")) {
            TitleFrame fr;
            if (f.contains("e")) {
                info.frames.push_back(fr); // empty frame
                continue;
            }
            fr.empty = false;
            fr.page = f.at("p").get<int>();
            fr.x = f.at("x"); fr.y = f.at("y"); fr.w = f.at("w"); fr.h = f.at("h");
            fr.cx = f.at("cx"); fr.cy = f.at("cy"); fr.cw = f.at("cw"); fr.ch = f.at("ch");
            if (fr.page < 0 || fr.page >= static_cast<int>(info.pages.size()) || fr.w <= 0 || fr.h <= 0
                || fr.x < 0 || fr.y < 0 || fr.x + fr.w > info.pages[fr.page].w || fr.y + fr.h > info.pages[fr.page].h
                || fr.cw <= 0 || fr.ch <= 0) {
                error = "frame " + std::to_string(info.frames.size()) + " is outside its page";
                return false;
            }
            info.frames.push_back(fr);
        }
        if (info.frames.empty() || info.pages.empty()) { error = "empty atlas"; return false; }
        out = std::move(info);
        return true;
    } catch (std::exception const& e) {
        error = e.what();
        return false;
    }
}

} // namespace animated_mojang::scene
