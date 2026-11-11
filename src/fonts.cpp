#include <cstdio>
#include <cstring>

#include "app.h"
#include "imgui.h"

namespace {

struct FontCandidate { const char* path; int ttcIndex; };

const FontCandidate kCandidates[] = {
    {"/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc", 2},
    {"/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc", 0},
    {"/home/vescrity/.local/share/fonts/lxgw-wenkai/LXGWWenKai-Regular.ttf", 0},
    {"/home/vescrity/.local/share/fonts/lxgw-wenkai/LXGWWenKai-Medium.ttf", 0},
    {"/home/vescrity/.local/share/fonts/lxgw-wenkai/LXGWWenKaiMono-Light.ttf", 0},
    {"/usr/share/fonts/TTF/DejaVuSans.ttf", 0},
};

} // namespace

namespace lb {

ImFont* loadFont(ImGuiIO& io, float size, const ImWchar* ranges, float* outScale) {
    for (const auto& c : kCandidates) {
        ImFontConfig cfg;
        cfg.FontNo = c.ttcIndex;
        cfg.OversampleH = 2;
        cfg.OversampleV = 2;
        cfg.PixelSnapH = true;
        if (ImFont* f = io.Fonts->AddFontFromFileTTF(c.path, size, &cfg, ranges)) {
            if (outScale) *outScale = 1.0f;
            std::fprintf(stderr, "[font] %s (ttc %d)\n", c.path, c.ttcIndex);
            return f;
        }
    }
    std::fprintf(stderr, "[font] 没找到中文字体，退回默认字体\n");
    return io.Fonts->AddFontDefault();
}

void loadAppFonts(App& app, ImFont** ui, ImFont** small) {
    (void)app;
    ImGuiIO& io = ImGui::GetIO();
    const ImWchar* ranges = io.Fonts->GetGlyphRangesChineseSimplifiedCommon();
    *ui = loadFont(io, 16.0f, ranges, nullptr);
    *small = loadFont(io, 13.0f, ranges, nullptr);
    if (*ui) io.FontDefault = *ui;
}

} // namespace lb
