// Esc 菜单：半透明盖住整个画面，三个居中的按钮（返回 / 返回至标题 / 退出）。
// 打开时游戏不暂停；有未保存改动时，离开前会先问一句。
#include <algorithm>

#include "app.h"
#include "i18n.h"
#include "imgui.h"
#include "ui.h"

namespace {

constexpr float kBtnW = 320.0f;   // 基准尺寸，实际会乘以界面缩放
constexpr float kBtnH = 66.0f;
constexpr float kGap = 14.0f;

// 顺带把实际尺寸写进 g_btnW/g_btnH/g_gap，后面画按钮和底板都靠它们
float g_btnW = kBtnW, g_btnH = kBtnH, g_gap = kGap;

ImVec2 layout(ImVec2 origin, ImVec2 size, int count, float extraTop, float us,
              float& outX, float& outY) {
    const float bw = kBtnW * us, bh = kBtnH * us, gap = kGap * us;
    const float total = count * bh + (count - 1) * gap + extraTop;
    outX = origin.x + (size.x - bw) * 0.5f;
    outY = origin.y + (size.y - total) * 0.5f;
    g_btnW = bw;
    g_btnH = bh;
    g_gap = gap;
    return ImVec2(outX, outY);
}

} // namespace

void App::drawMenu(ImVec2 origin, ImVec2 size) {
    lb::ui::Overlay overlay("##menu", origin, size, true);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float us = uiScale();

    float x = 0, y = 0;
    if (!confirmOpen_) {
        lb::ui::dim(dl, origin, size, 168);
        lb::ui::panel(dl, layout(origin, size, 3, 0.0f, us, x, y), g_btnW, 3 * g_btnH + 2 * g_gap, us);

        if (lb::ui::bigButton(ImVec2(x, y), ImVec2(g_btnW, g_btnH), lb::tr(lb::S::MenuResume), 1.55f))
            menuOpen_ = false;
        y += g_btnH + g_gap;
        if (lb::ui::bigButton(ImVec2(x, y), ImVec2(g_btnW, g_btnH), lb::tr(lb::S::MenuToTitle), 1.55f))
            doMenuAction(1);
        y += g_btnH + g_gap;
        if (lb::ui::bigButton(ImVec2(x, y), ImVec2(g_btnW, g_btnH), lb::tr(lb::S::MenuQuit), 1.55f))
            doMenuAction(2);
    } else {
        const float textH = ImGui::GetStyle().FontSizeBase * 1.5f;
        const float headH = textH + 40.0f * us;   // 标题两行 + 间距
        lb::ui::dim(dl, origin, size, 168);
        const ImVec2 p = layout(origin, size, 3, headH, us, x, y);
        lb::ui::panel(dl, p, g_btnW, headH + 3 * g_btnH + 2 * g_gap, us);   // 组件高度要含标题，否则底板短一截

        lb::ui::centeredText(origin, size.x, y, lb::tr(lb::S::MenuUnsavedTitle),
                             IM_COL32(240, 246, 252, 255), 1.5f);
        lb::ui::centeredText(origin, size.x, y + textH + 6.0f * us, lb::tr(lb::S::MenuUnsavedBody),
                             IM_COL32(150, 168, 186, 255), 1.15f);
        y += textH + 40.0f * us;

        if (lb::ui::bigButton(ImVec2(x, y), ImVec2(g_btnW, g_btnH), lb::tr(lb::S::MenuSaveAndGo), 1.55f)) {
            saveState();
            const int a = confirmAction_;
            confirmOpen_ = false;
            performMenuAction(a);
        }
        y += g_btnH + g_gap;
        if (lb::ui::bigButton(ImVec2(x, y), ImVec2(g_btnW, g_btnH), lb::tr(lb::S::MenuDiscard), 1.55f)) {
            const int a = confirmAction_;
            confirmOpen_ = false;
            performMenuAction(a);
        }
        y += g_btnH + g_gap;
        if (lb::ui::bigButton(ImVec2(x, y), ImVec2(g_btnW, g_btnH), lb::tr(lb::S::MenuCancel), 1.55f))
            confirmOpen_ = false;
    }
}

void App::doMenuAction(int action) {
    if (dirty_) {
        confirmOpen_ = true;
        confirmAction_ = action;
        return;
    }
    performMenuAction(action);
}

bool App::performMenuAction(int action) {
    if (action == 0) {                 // 返回
        menuOpen_ = false;
        return false;
    }
    if (action == 1) {                 // 返回至标题
        menuOpen_ = false;
        confirmOpen_ = false;
        if (audioReady_) audio_.allNotesOff();
        showStartScreen();
        return false;
    }
    quitRequested_ = true;
    return true;
}

bool App::requestQuit() {
    if (startScreen_) return true;
    if (dirty_) {
        menuOpen_ = true;
        confirmOpen_ = true;
        confirmAction_ = 2;
        return false;
    }
    return true;
}
