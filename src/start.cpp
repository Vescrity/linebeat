// 开始界面：背景跑一个演示场景，前景是 logo + 居中的大按钮（像游戏那样）。
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>

#include "app.h"
#include "dialog.h"
#include "i18n.h"
#include "imgui.h"
#include "ui.h"

namespace fs = std::filesystem;

namespace {

std::string fileName(const std::string& path) {
    return fs::path(path).filename().string();
}

std::string parentDir(const std::string& path) {
    const fs::path p(path);
    std::string parent = p.parent_path().string();
    if (const char* home = std::getenv("HOME")) {
        const std::string h(home);
        if (parent.rfind(h, 0) == 0) parent = "~" + parent.substr(h.size());
    }
    return parent;
}

// 背景里那个世界：淡一点、只画线和球。
// 相机在进场时算一次（App::bgBounds_），这里只做投影，不跟内容跑，画面就不会跳。
void drawBgWorld(ImDrawList* dl, const Project& p, const World& w, ImVec2 origin, ImVec2 size,
                 ImVec4 bounds) {
    const double w0 = double(bounds.z) - double(bounds.x);
    const double h0 = double(bounds.w) - double(bounds.y);
    if (p.lines.empty() || w0 < 0.5 || h0 < 0.5) return;

    const double scale = std::min(size.x / w0, size.y / h0);
    const double cx = 0.5 * (bounds.x + bounds.z);
    const double cy = 0.5 * (bounds.y + bounds.w);

    auto toScreen = [&](Vec2 v) {
        return ImVec2(float(origin.x + size.x * 0.5 + (v.x - cx) * scale),
                      float(origin.y + size.y * 0.5 - (v.y - cy) * scale));
    };

    for (size_t i = 0; i < w.lineA().size() && i < p.lines.size(); ++i) {
        if (!p.lines[i].collide) continue;
        dl->AddLine(toScreen(w.lineA()[i]), toScreen(w.lineB()[i]), IM_COL32(96, 186, 220, 120), 1.8f);
    }
    for (const auto& b : w.balls())
        dl->AddCircleFilled(toScreen(b.pos), 3.4f, IM_COL32(160, 226, 255, 150));
}

} // namespace

void App::drawStartScreen(ImVec2 origin, ImVec2 size) {
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    dl->AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + size.y), IM_COL32(10, 12, 16, 255));

    drawBgWorld(dl, bgProject_, bgWorld_, origin, size, bgBounds_);
    // 压暗 + 顶部到底部的渐变，保证字能看清
    dl->AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + size.y), IM_COL32(8, 10, 14, 140));
    dl->AddRectFilledMultiColor(origin, ImVec2(origin.x + size.x, origin.y + size.y),
                                IM_COL32(8, 10, 14, 210), IM_COL32(8, 10, 14, 210),
                                IM_COL32(8, 10, 14, 120), IM_COL32(8, 10, 14, 120));

    lb::ui::Overlay overlay("##start", origin, size, false);   // 不留焦点，后面的弹窗才能盖上来

    const float us = uiScale();
    const float midX = size.x * 0.5f;
    const float btnW = std::min(360.0f * us, size.x * 0.6f);
    const float btnH = 52.0f * us;
    const float gap = 14.0f * us;

    // ---- logo
    ImGui::SetCursorPos(ImVec2(0, size.y * 0.16f));
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 3.4f);
    const char* title = lb::tr(lb::S::AppTitle);
    const float tw = ImGui::CalcTextSize(title).x;
    ImGui::SetCursorPosX(midX - tw * 0.5f);
    ImGui::PushStyleColor(ImGuiCol_Text, ImColor(236, 248, 255, 255).Value);
    ImGui::TextUnformatted(title);
    ImGui::PopStyleColor();
    ImGui::PopFont();

    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.05f);
    const char* sub = lb::tr(lb::S::AppTagline);
    const float sw = ImGui::CalcTextSize(sub).x;
    ImGui::SetCursorPos(ImVec2(midX - sw * 0.5f, size.y * 0.16f + 74.0f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImColor(120, 150, 175, 255).Value);
    ImGui::TextUnformatted(sub);
    ImGui::PopStyleColor();
    ImGui::PopFont();

    dl->AddLine(ImVec2(midX - 70, origin.y + size.y * 0.16f + 112),
                ImVec2(midX + 70, origin.y + size.y * 0.16f + 112),
                IM_COL32(126, 226, 255, 90), 1.0f);

    // ---- 大按钮
    float y = size.y * 0.40f;
    auto bigButton = [&](const char* label) {
        ImGui::SetCursorPos(ImVec2(midX - btnW * 0.5f, y));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 12.0f);
        ImGui::PushStyleColor(ImGuiCol_Button, ImColor(28, 38, 50, 235).Value);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImColor(48, 96, 120, 245).Value);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImColor(60, 128, 158, 255).Value);
        ImGui::PushStyleColor(ImGuiCol_Text, ImColor(226, 238, 248, 255).Value);
        ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.25f);
        const bool clicked = ImGui::Button(label, ImVec2(btnW, btnH));
        ImGui::PopFont();
        ImGui::PopStyleColor(4);
        ImGui::PopStyleVar();
        y += btnH + gap;
        return clicked;
    };

    {
        bool hasRecent = false;
        std::error_code ec;
        if (!config_.lastProject.empty() && fs::exists(config_.lastProject, ec)) hasRecent = true;

        if (hasRecent) {
            const std::string label = std::string("继续  ") + fileName(config_.lastProject);
            if (bigButton(label.c_str())) openProject(config_.lastProject);
        }

        if (bigButton(lb::tr(lb::S::NewDefault))) newProject(false);
        if (bigButton(lb::tr(lb::S::OpenScene))) {
            openSceneBrowser();
        }
    }

    // ---- 演示（列表在进界面时算一次，不要每帧扫目录）
    const auto& demos = demoCache_;
    if (!demos.empty()) {
        y += 6.0f;
        ImGui::SetCursorPos(ImVec2(midX - btnW * 0.5f, y));
        ImGui::PushStyleColor(ImGuiCol_Text, ImColor(110, 130, 150, 255).Value);
        ImGui::TextUnformatted(lb::tr(lb::S::Demo));
        ImGui::PopStyleColor();
        y += 26.0f;

        const float chipW = std::max(60.0f, std::min(170.0f * us, (btnW - 12.0f) / 2.0f));
        float x = midX - btnW * 0.5f;
        for (const auto& d : demos) {
            ImGui::SetCursorPos(ImVec2(x, y));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 9.0f);
            ImGui::PushStyleColor(ImGuiCol_Button, ImColor(22, 30, 40, 220).Value);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImColor(40, 78, 96, 240).Value);
            ImGui::PushStyleColor(ImGuiCol_Text, ImColor(200, 214, 228, 255).Value);
            const std::string name = fileName(d);
            if (ImGui::Button(name.c_str(), ImVec2(chipW, 38.0f * us))) openProject(d);
            ImGui::PopStyleColor(3);
            ImGui::PopStyleVar();
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", parentDir(d).c_str());
            x += chipW + 12.0f;
            if (x + chipW > midX + btnW * 0.5f) { x = midX - btnW * 0.5f; y += 46.0f * us; }
        }
    }

    // ---- 底部
    ImGui::PushStyleColor(ImGuiCol_Text, ImColor(80, 92, 108, 255).Value);
#include "lb_build_stamp.h"
    const char* foot = "v0.2   ·   空格 暂停   ·   Esc 菜单   ·   Ctrl+S 保存   ·   F 适配";
    const float fw = ImGui::CalcTextSize(foot).x;
    ImGui::SetCursorPos(ImVec2(midX - fw * 0.5f, size.y - 40.0f));
    ImGui::TextUnformatted(foot);
    ImGui::PopStyleColor();

    ImGui::PushStyleColor(ImGuiCol_Text, ImColor(62, 72, 86, 255).Value);
    const std::string stamp = std::string("build ") + LB_GIT_HASH + "  " + LB_BUILD_TIME;
    const float sw2 = ImGui::CalcTextSize(stamp.c_str()).x;
    ImGui::SetCursorPos(ImVec2(midX - sw2 * 0.5f, size.y - 20.0f));
    ImGui::TextUnformatted(stamp.c_str());
    ImGui::PopStyleColor();
}
