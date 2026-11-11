// 共用的 UI 小工具：工具条几何、全屏覆盖层、压暗面板、大按钮。
#pragma once

#include "imgui.h"

namespace lb::ui {

// ---------------------------------------------------------------- 工具条几何
constexpr int   kToolCount    = 5;
constexpr float kToolbarW     = 52.0f;
constexpr float kToolbarItemH = 42.0f;
constexpr float kToolbarGap   = 6.0f;
constexpr float kToolbarH =
    kToolbarItemH * kToolCount + kToolbarGap * (kToolCount + 1) + 12.0f;

inline ImVec2 toolbarPos(ImVec2 origin, ImVec2 size, float reveal) {
    return ImVec2(origin.x + 8.0f - (1.0f - reveal) * (kToolbarW + 16.0f),
                  origin.y + size.y * 0.5f - kToolbarH * 0.5f);
}

// ---------------------------------------------------------------- 全屏覆盖层
// 自动完成 SetNextWindowPos/Size、Push/Pop、Begin/End 以及窗口 flag。
// focus=true 用于模态框（压到最上层）；false 用于开始界面这种"背景窗口"。
class Overlay {
public:
    Overlay(const char* id, ImVec2 origin, ImVec2 size, bool focus);
    ~Overlay();
    Overlay(const Overlay&) = delete;
    Overlay& operator=(const Overlay&) = delete;
    bool visible() const { return visible_; }

private:
    bool visible_ = false;
};

// 整屏压暗（alpha 0-255）
void dim(ImDrawList* dl, ImVec2 origin, ImVec2 size, int alpha);

// 居中对话框底板：p 是内容左上角，w/h 是内容尺寸，us 是界面缩放
void panel(ImDrawList* dl, ImVec2 p, float w, float h, float us, int fillAlpha = 246, float radius = 18.0f);

void centeredText(ImVec2 origin, float width, float y, const char* text, ImU32 col, float scale);

// 覆盖层里的大按钮（自带配色与字号）
bool bigButton(ImVec2 pos, ImVec2 size, const char* label, float fontScale);

} // namespace lb::ui
