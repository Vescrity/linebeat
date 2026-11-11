#include "ui.h"

namespace lb::ui {

Overlay::Overlay(const char* id, ImVec2 origin, ImVec2 size, bool focus) {
    ImGui::SetNextWindowPos(origin);
    ImGui::SetNextWindowSize(size);
    if (focus) ImGui::SetNextWindowFocus();

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground |
                             ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
    if (!focus) flags |= ImGuiWindowFlags_NoBringToFrontOnFocus;
    visible_ = ImGui::Begin(id, nullptr, flags);
}

Overlay::~Overlay() {
    ImGui::End();
    ImGui::PopStyleVar();
}

void dim(ImDrawList* dl, ImVec2 origin, ImVec2 size, int alpha) {
    dl->AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + size.y),
                      IM_COL32(6, 8, 11, alpha));
}

void panel(ImDrawList* dl, ImVec2 p, float w, float h, float us, int fillAlpha, float radius) {
    const ImVec2 a(p.x - 24.0f * us, p.y - 20.0f * us);
    const ImVec2 b(p.x + w + 24.0f * us, p.y + h + 20.0f * us);
    dl->AddRectFilled(a, b, IM_COL32(14, 18, 24, fillAlpha), radius * us);
    dl->AddRect(a, b, IM_COL32(70, 96, 118, 130), radius * us, 0, 1.0f);
}

void centeredText(ImVec2 origin, float width, float y, const char* text, ImU32 col, float scale) {
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * scale);
    const ImVec2 ts = ImGui::CalcTextSize(text);
    ImGui::SetCursorScreenPos(ImVec2(origin.x + (width - ts.x) * 0.5f, y));
    ImGui::PushStyleColor(ImGuiCol_Text, col);
    ImGui::TextUnformatted(text);
    ImGui::PopStyleColor();
    ImGui::PopFont();
}

bool bigButton(ImVec2 pos, ImVec2 size, const char* label, float fontScale) {
    ImGui::SetCursorScreenPos(pos);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 12.0f);
    ImGui::PushStyleColor(ImGuiCol_Button, ImColor(34, 44, 58, 255).Value);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImColor(48, 96, 120, 250).Value);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImColor(60, 128, 158, 255).Value);
    ImGui::PushStyleColor(ImGuiCol_Text, ImColor(228, 240, 250, 255).Value);
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * fontScale);
    const bool clicked = ImGui::Button(label, size);
    ImGui::PopFont();
    ImGui::PopStyleColor(4);
    ImGui::PopStyleVar();
    return clicked;
}

} // namespace lb::ui
