// 自带的存档浏览界面：从固定目录读 .lbproj，列出名字 + 修改时间。
// 不用系统文件对话框（Wayland 上 portal 有时候根本弹不出来）。
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>

#include "app.h"
#include "config.h"
#include "i18n.h"
#include "imgui.h"
#include "ui.h"

namespace fs = std::filesystem;

namespace {

std::string formatTime(fs::file_time_type ft) {
    using namespace std::chrono;
    // file_clock -> system_clock（C++20）
    const auto sys = time_point_cast<system_clock::duration>(ft - fs::file_time_type::clock::now() +
                                                             system_clock::now());
    const std::time_t t = system_clock::to_time_t(sys);
    std::tm tm{};
    localtime_r(&t, &tm);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", &tm);
    return buf;
}

void shortDir(const std::string& dir, char* out, size_t n) {
    std::string s = dir;
    if (const char* home = std::getenv("HOME")) {
        const std::string h(home);
        if (s.rfind(h, 0) == 0) s = "~" + s.substr(h.size());
    }
    std::snprintf(out, n, "%s", s.c_str());
}

} // namespace

void App::refreshSceneEntries() {
    browserEntries_.clear();
    std::vector<std::string> dirs = {lb::demoScenesDir(), lb::userScenesDir()};
    for (const auto& dir : dirs) {
        if (dir.empty()) continue;
        std::error_code ec;
        for (const auto& e : fs::directory_iterator(dir, ec)) {
            if (!e.is_regular_file(ec)) continue;
            if (e.path().extension() != ".lbproj") continue;
            SceneEntry s;
            s.path = e.path().string();
            s.name = e.path().filename().string();
            s.dir = dir;
            const auto ft = fs::last_write_time(e.path(), ec);
            s.when = ec ? std::string("-") : formatTime(ft);
            browserEntries_.push_back(std::move(s));
        }
    }
    std::sort(browserEntries_.begin(), browserEntries_.end(),
              [](const SceneEntry& a, const SceneEntry& b) { return a.when > b.when; });
}

void App::openSceneBrowser() {
    refreshSceneEntries();
    browserSel_ = browserEntries_.empty() ? -1 : 0;
    browserOpen_ = true;
}

void App::drawSceneBrowser(ImVec2 origin, ImVec2 size) {
    if (!browserOpen_) return;

    const float us = uiScale();
    const float w = std::min(680.0f * us, size.x - 80.0f * us);
    const float h = std::min(520.0f * us, size.y - 100.0f * us);
    const ImVec2 p(origin.x + (size.x - w) * 0.5f, origin.y + (size.y - h) * 0.5f);

    lb::ui::Overlay overlay("##scenebrowser", origin, size, true);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    lb::ui::dim(dl, origin, size, 186);
    lb::ui::panel(dl, p, w, h, us);

    ImGui::SetCursorScreenPos(p);
    ImGui::BeginGroup();
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.5f);
    ImGui::TextUnformatted("打开场景");
    ImGui::PopFont();

    char d1[512], d2[512];
    shortDir(lb::demoScenesDir(), d1, sizeof(d1));
    shortDir(lb::userScenesDir(), d2, sizeof(d2));
    ImGui::PushStyleColor(ImGuiCol_Text, ImColor(120, 136, 154, 255).Value);
    ImGui::Text("%s   ·   %s", d1, d2);
    ImGui::PopStyleColor();

    const float footer = 52.0f * us;
    const float listH = h - 8.0f * us - footer;
    ImGui::BeginChild("##list", ImVec2(w, std::max(80.0f, listH)), false);
    if (ImGui::BeginTable("##scenes", 2,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp |
                              ImGuiTableFlags_ScrollY)) {
        ImGui::TableSetupColumn("名字", ImGuiTableColumnFlags_WidthStretch, 2.4f);
        ImGui::TableSetupColumn("修改时间", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();
        for (size_t i = 0; i < browserEntries_.size(); ++i) {
            const auto& e = browserEntries_[i];
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::PushID(int(i));
            const bool sel = int(i) == browserSel_;
            if (ImGui::Selectable(e.name.c_str(), sel, ImGuiSelectableFlags_SpanAllColumns |
                                                            ImGuiSelectableFlags_AllowDoubleClick)) {
                browserSel_ = int(i);
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                    openProject(e.path);
                    browserOpen_ = false;
                }
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", e.path.c_str());
            ImGui::PopID();
            ImGui::TableSetColumnIndex(1);
            ImGui::PushStyleColor(ImGuiCol_Text, ImColor(140, 154, 170, 255).Value);
            ImGui::TextUnformatted(e.when.c_str());
            ImGui::PopStyleColor();
        }
        ImGui::EndTable();
    }
    if (browserEntries_.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImColor(120, 136, 154, 255).Value);
        ImGui::TextUnformatted("这两个目录里还没有 .lbproj 文件");
        ImGui::PopStyleColor();
    }
    ImGui::EndChild();

    const float bw = 120.0f * us, bh = 40.0f * us;
    const ImVec2 btnRow(p.x + w - bw * 2.0f - 12.0f * us, p.y + h - footer + 10.0f * us);
    if (browserSel_ >= 0) {
        if (lb::ui::bigButton(btnRow, ImVec2(bw, bh), "打开", 1.0f)) {
            openProject(browserEntries_[size_t(browserSel_)].path);
            browserOpen_ = false;
        }
    } else {
        ImGui::BeginDisabled();
        lb::ui::bigButton(btnRow, ImVec2(bw, bh), "打开", 1.0f);
        ImGui::EndDisabled();
    }
    if (lb::ui::bigButton(ImVec2(btnRow.x + bw + 12.0f * us, btnRow.y), ImVec2(bw, bh), "取消", 1.0f))
        browserOpen_ = false;
    ImGui::EndGroup();

    // 键盘：Esc 关，回车开，上下移动
    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) browserOpen_ = false;
    if (ImGui::IsKeyPressed(ImGuiKey_DownArrow) && !browserEntries_.empty())
        browserSel_ = std::min(int(browserEntries_.size()) - 1, browserSel_ + 1);
    if (ImGui::IsKeyPressed(ImGuiKey_UpArrow) && !browserEntries_.empty())
        browserSel_ = std::max(0, browserSel_ - 1);
    if (ImGui::IsKeyPressed(ImGuiKey_Enter) && browserSel_ >= 0) {
        openProject(browserEntries_[size_t(browserSel_)].path);
        browserOpen_ = false;
    }
}

// 另存为：一个输入框 + 保存/取消，写到用户场景目录
void App::drawSaveAs(ImVec2 origin, ImVec2 size) {
    if (!saveAsOpen_) return;

    const float us = uiScale();
    const float w = std::min(460.0f * us, size.x - 60.0f * us);
    const float h = 210.0f * us;
    const ImVec2 p(origin.x + (size.x - w) * 0.5f, origin.y + (size.y - h) * 0.5f);

    lb::ui::Overlay overlay("##saveas", origin, size, true);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    lb::ui::dim(dl, origin, size, 186);
    lb::ui::panel(dl, p, w, h, us);

    ImGui::SetCursorScreenPos(p);
    ImGui::BeginGroup();
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.4f);
    ImGui::TextUnformatted("另存为");
    ImGui::PopFont();

    char dir[512];
    shortDir(lb::userScenesDir(), dir, sizeof(dir));
    ImGui::PushStyleColor(ImGuiCol_Text, ImColor(120, 136, 154, 255).Value);
    ImGui::Text("保存到 %s", dir);
    ImGui::PopStyleColor();

    ImGui::Spacing();
    if (saveAsFocus_) { ImGui::SetKeyboardFocusHere(); saveAsFocus_ = false; }
    ImGui::SetNextItemWidth(w - 60.0f * us);
    ImGui::InputText("##name", saveAsName_, sizeof(saveAsName_));
    ImGui::SameLine();
    ImGui::TextDisabled(".lbproj");

    ImGui::Spacing();
    const float bw = 106.0f * us, bh = 38.0f * us;
    const float rowW = bw * 2.0f + 10.0f * us;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() +
                         std::max(0.0f, ImGui::GetContentRegionAvail().x - rowW));
    const ImVec2 rowPos = ImGui::GetCursorScreenPos();
    const bool valid = saveAsName_[0] != 0;
    bool save = false;
    if (valid) {
        save = lb::ui::bigButton(rowPos, ImVec2(bw, bh), "保存", 1.0f);
    } else {
        ImGui::BeginDisabled();
        lb::ui::bigButton(rowPos, ImVec2(bw, bh), "保存", 1.0f);
        ImGui::EndDisabled();
    }
    const bool cancel = lb::ui::bigButton(ImVec2(rowPos.x + bw + 10.0f * us, rowPos.y),
                                          ImVec2(bw, bh), "取消", 1.0f);
    ImGui::EndGroup();

    const bool enter = ImGui::IsKeyPressed(ImGuiKey_Enter);
    if (cancel || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        saveAsOpen_ = false;
        return;
    }
    if (!(save || enter) || !valid) return;

    std::string name(saveAsName_);
    for (char& c : name)
        if (c == '/' || c == '\\' || c == ':') c = '_';
    if (name.size() < 7 || name.compare(name.size() - 7, 7, ".lbproj") != 0)
        name += ".lbproj";
    std::error_code ec;
    fs::create_directories(lb::userScenesDir(), ec);
    projectPath_ = (fs::path(lb::userScenesDir()) / name).string();
    project_.name = fs::path(name).stem().string();
    saveState();
    saveAsOpen_ = false;
}
