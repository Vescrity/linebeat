// 面板：左边缘工具条、右侧 inspector、变量表、左下状态
#include <algorithm>
#include <cmath>
#include <cstdio>

#include "app.h"
#include "i18n.h"
#include "imgui.h"
#include "ui.h"
#include "misc/cpp/imgui_stdlib.h"

namespace {

constexpr ImU32 kPanelBg   = IM_COL32(18, 20, 26, 246);
constexpr ImU32 kPanelEdge = IM_COL32(255, 255, 255, 22);
constexpr ImU32 kWarn      = IM_COL32(255, 120, 110, 255);
constexpr ImU32 kTextDim   = IM_COL32(140, 152, 170, 255);

struct PresetEntry { const char* name; int program; };
const PresetEntry kPresets[] = {
    {"音乐盒", 10}, {"钢片琴", 9}, {"马林巴", 12}, {"木琴", 13},
    {"卡林巴", 108}, {"竖琴", 46}, {"电钢琴", 4},
};

void sectionTitle(const char* text) {
    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Text, ImColor(120, 132, 150, 220).Value);
    ImGui::TextUnformatted(text);
    ImGui::PopStyleColor();
    ImGui::Separator();
    ImGui::Spacing();
}

void dimText(const char* text) {
    ImGui::PushStyleColor(ImGuiCol_Text, kTextDim);
    ImGui::TextUnformatted(text);
    ImGui::PopStyleColor();
}


// 标签在左、控件在右的固定布局
bool dragRow(const char* label, float* v, float speed, float lo, float hi, const char* fmt) {
    ImGui::PushID(label);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::SameLine(78.0f);
    ImGui::SetNextItemWidth(-1.0f);
    const bool changed = ImGui::DragFloat("##v", v, speed, lo, hi, fmt);
    ImGui::PopID();
    return changed;
}


// ---- 简洁模式的映射预设：把常用表达式包成下拉项 -------------------------
struct MapPreset { const char* name; const char* expr; };

const MapPreset kNoteMaps[] = {
    {"固定音高", ""},
    {"撞得越猛音越高", "scale(45 + hit.speed * 5, \"majorPent\")"},
    {"按撞击高度", "scale(45 + hit.y * 8, \"majorPent\")"},
    {"按撞击位置 x", "scale(45 + hit.x * 5, \"majorPent\")"},
    {"每小节轮换（4536）", "lut(floor(t / 2) % 4, 0,53, 1,55, 2,52, 3,57)"},
};

const MapPreset kVelMaps[] = {
    {"固定力度", ""},
    {"撞得越猛越响", "clamp(24 + hit.speed * 10, 20, 127)"},
    {"撞得越轻越响", "clamp(120 - hit.speed * 12, 20, 127)"},
    {"压缩到 60-100", "clamp(60 + hit.speed * 4, 60, 100)"},
};

// 下拉选映射；返回是否改动
bool mappingRow(const char* label, Param& p, const MapPreset* list, int n) {
    int cur = 0;
    if (p.expr) {
        cur = -1;
        for (int i = 0; i < n; ++i) {
            if (i > 0 && p.source == list[i].expr) { cur = i; break; }
        }
    }

    bool changed = false;
    ImGui::PushID(label);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::SameLine(78.0f);
    ImGui::SetNextItemWidth(-1.0f);
    const char* shown = cur >= 0 ? list[cur].name : "自定义（高级）";
    if (ImGui::BeginCombo("##map", shown)) {
        for (int i = 0; i < n; ++i) {
            if (ImGui::Selectable(list[i].name, i == cur)) {
                p.setSource(list[i].expr);
                changed = true;
            }
        }
        ImGui::EndCombo();
    }
    ImGui::PopID();
    return changed;
}

// 一个可驱动参数的编辑行：拖动数值 / ƒ 切换为表达式
bool paramRow(const char* label, Param& p, float speed, float lo, float hi, const char* fmt,
              bool allowExpr) {
    bool changed = false;
    ImGui::PushID(label);

    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::SameLine(78.0f);

    const float avail = std::max(40.0f, ImGui::GetContentRegionAvail().x);
    ImGui::SetNextItemWidth(avail - 26.0f);

    if (!p.driven()) {
        float v = float(p.constant);
        if (ImGui::DragFloat("##v", &v, speed, lo, hi, fmt)) {
            p.constant = double(v);
            changed = true;
        }
        if (!allowExpr) { ImGui::PopID(); return changed; }
        ImGui::SameLine(0, 4);
        if (ImGui::SmallButton("ƒ")) {
            char buf[48];
            std::snprintf(buf, sizeof(buf), "%g", p.constant);
            p.setSource(buf);
            changed = true;
        }
    } else {
        const std::string before = p.source;
        bool commit = false;
        if (ImGui::InputText("##e", &p.source, ImGuiInputTextFlags_EnterReturnsTrue)) commit = true;
        else if (ImGui::IsItemDeactivatedAfterEdit()) commit = true;

        if (commit) {
            if (p.source.empty()) {
                p.source = before;          // 清空 = 取消，别让参数掉回旧常量
            } else if (p.source != before) {
                p.setSource(p.source);
                changed = true;
            }
        }

        if (!allowExpr) { ImGui::PopID(); return changed; }
        ImGui::SameLine(0, 4);
        if (ImGui::SmallButton("×")) {
            if (std::isfinite(p.lastValue)) p.constant = p.lastValue;
            p.source.clear();
            p.setSource("");
            changed = true;
        }
    }

    if (!p.error.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImColor(255, 120, 110, 255).Value);
        ImGui::TextWrapped("%s", p.error.c_str());
        ImGui::PopStyleColor();
    }

    ImGui::PopID();
    return changed;
}

} // namespace

// ---------------------------------------------------------------- 工具条

void App::drawToolbar(ImVec2 origin, ImVec2 size) {
    if (toolbarReveal_ < 0.015) return;

    const float reveal = float(toolbarReveal_);
    const float w = lb::ui::kToolbarW;
    const float itemH = lb::ui::kToolbarItemH;
    const float h = lb::ui::kToolbarH;

    ImGui::SetNextWindowPos(lb::ui::toolbarPos(origin, size, reveal));
    ImGui::SetNextWindowSize(ImVec2(w, h));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6, 6));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 14.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, reveal);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImGui::ColorConvertU32ToFloat4(kPanelBg));
    ImGuiWindowFlags toolFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                 ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar;
    if (reveal < 0.2f) toolFlags |= ImGuiWindowFlags_NoInputs;
    ImGui::Begin("##toolbar", nullptr, toolFlags);

    struct ToolBtn { const char* label; const char* tip; Tool tool; };
    const ToolBtn buttons[] = {
        {"选", "选择 / 拖动  (1)", Tool::Select},
        {"线", "画线段  (2)", Tool::Line},
        {"球", "放球  (3)", Tool::Ball},
        {"发", "放发射器  (4)", Tool::Emitter},
        {"删", "删除  (5)", Tool::Erase},
    };

    for (int i = 0; i < lb::ui::kToolCount; ++i) {
        const bool activeTool = (tool_ == buttons[i].tool);
        ImGui::PushStyleColor(ImGuiCol_Button,
                              activeTool ? ImColor(126, 226, 255, 60).Value : ImColor(255, 255, 255, 12).Value);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImColor(126, 226, 255, 90).Value);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImColor(126, 226, 255, 120).Value);
        ImGui::PushStyleColor(ImGuiCol_Text, activeTool ? ImColor(240, 252, 255, 255).Value
                                                        : ImColor(190, 200, 214, 255).Value);
        if (ImGui::Button(buttons[i].label, ImVec2(w - 12.0f, itemH))) tool_ = buttons[i].tool;
        ImGui::PopStyleColor(4);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", buttons[i].tip);
    }

    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(3);
}

// ---------------------------------------------------------------- 检查器

void App::drawVariables(ImVec2 origin, ImVec2 size) {
    if (!showVars_) return;
    Slots s;
    s.fillWorld(project_.world, world_.time(), World::kStep, world_.frame());

    struct Row { const char* name; int slot; const char* note; };
    std::vector<Row> rows = {
        {"t", SLOT_T, "秒"},
        {"dt", SLOT_DT, "步长"},
        {"frame", SLOT_FRAME, nullptr},
        {"pi", SLOT_PI, nullptr},
        {"e", SLOT_E, nullptr},
    };

    if (Line* l = selectedLine()) {
        size_t idx = 0;
        for (size_t i = 0; i < project_.lines.size(); ++i)
            if (project_.lines[i].id == l->id) idx = i;
        if (idx < world_.lineA().size()) s.fillLine(*l, world_.lineA()[idx], world_.lineB()[idx]);
        else s.fillLine(*l);
        rows.push_back({"self.a.x", SLOT_SELF_AX, "A 点"});
        rows.push_back({"self.a.y", SLOT_SELF_AY, nullptr});
        rows.push_back({"self.b.x", SLOT_SELF_BX, "B 点"});
        rows.push_back({"self.b.y", SLOT_SELF_BY, nullptr});
        rows.push_back({"self.x", SLOT_SELF_X, "中点"});
        rows.push_back({"self.y", SLOT_SELF_Y, nullptr});
    } else if (Ball* b = selectedBall()) {
        s.fillBall(*b);
        rows.push_back({"self.x", SLOT_SELF_X, "位置"});
        rows.push_back({"self.y", SLOT_SELF_Y, nullptr});
        rows.push_back({"self.vx", SLOT_SELF_VX, "速度"});
        rows.push_back({"self.vy", SLOT_SELF_VY, nullptr});
        rows.push_back({"self.r", SLOT_SELF_R, "半径"});
    } else if (Emitter* e = selectedEmitter()) {
        s.fillEmitter(*e);
        s.v[SLOT_SELF_X] = e->px.peek(s.v);
        s.v[SLOT_SELF_Y] = e->py.peek(s.v);
        s.v[SLOT_SELF_DIR] = e->dir.peek(s.v);
        s.v[SLOT_SELF_SPEED] = e->speed.peek(s.v);
        rows.push_back({"self.x", SLOT_SELF_X, "位置"});
        rows.push_back({"self.y", SLOT_SELF_Y, nullptr});
        rows.push_back({"self.dir", SLOT_SELF_DIR, "方向"});
        rows.push_back({"self.speed", SLOT_SELF_SPEED, "初速"});
    }

    rows.push_back({"world.gravity", SLOT_WORLD_GRAVITY, nullptr});
    rows.push_back({"world.kill_y", SLOT_WORLD_KILLY, nullptr});
    rows.push_back({"world.seed", SLOT_WORLD_SEED, nullptr});

    // 默认贴在画布左下角，不要压住右侧检查器
    const float us = uiScale();
    const float panelReserve = 0.0f;   // 面板浮在画布上，不再挤占布局
    const float vw = std::clamp(size.x - panelReserve - 110.0f, 250.0f, 352.0f);
    const float vh = std::min(442.0f, std::max(180.0f, size.y - 150.0f));
    ImGui::SetNextWindowPos(ImVec2(origin.x + 76.0f, origin.y + size.y - vh - 56.0f),
                            ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(vw, vh), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(ImVec2(260.0f, 180.0f), ImVec2(FLT_MAX, FLT_MAX));
    if (!ImGui::Begin("变量", &showVars_, ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::End();
        return;
    }
    dimText("点名字复制到剪贴板");
    if (sel_.kind == SelKind::Ball) dimText("球是算出来的：这些值只读，不能写进表达式");

    auto nameCell = [](const char* n) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImColor(126, 226, 255, 255).Value);
        ImGui::Selectable(n, false, ImGuiSelectableFlags_AllowOverlap);
        if (ImGui::IsItemClicked()) ImGui::SetClipboardText(n);
        ImGui::PopStyleColor();
    };

    if (ImGui::TreeNodeEx("当前值", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth)) {
        if (ImGui::BeginTable("##vars", 3,
                              ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit |
                                  ImGuiTableFlags_NoPadOuterX)) {
            ImGui::TableSetupColumn("名", ImGuiTableColumnFlags_WidthFixed, 106.0f);
            ImGui::TableSetupColumn("值", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("说明", ImGuiTableColumnFlags_WidthFixed, 58.0f);
            for (const auto& r : rows) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                nameCell(r.name);
                ImGui::TableSetColumnIndex(1);
                if (r.slot == SLOT_FRAME) ImGui::Text("%d", int(s.v[r.slot]));
                else ImGui::Text("%.3f", s.v[r.slot]);
                ImGui::TableSetColumnIndex(2);
                if (r.note) dimText(r.note);
            }
            ImGui::EndTable();
        }
        ImGui::TreePop();
    }

    if (ImGui::TreeNodeEx("撞击时可用（hit.*）", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth)) {
        dimText("只在 note / vel / gain 表达式里有效");
        if (ImGui::BeginTable("##hitvars", 2,
                              ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit |
                                  ImGuiTableFlags_NoPadOuterX)) {
            ImGui::TableSetupColumn("名", ImGuiTableColumnFlags_WidthFixed, 106.0f);
            ImGui::TableSetupColumn("说明", ImGuiTableColumnFlags_WidthStretch);
            const char* names[] = {"hit.speed", "hit.x", "hit.y", "hit.nx", "hit.ny", "hit.line", "hit.ball"};
            const char* notes[] = {"撞击速度", "撞击点 x", "撞击点 y", "法线 x", "法线 y", "线段 id", "球 id"};
            for (int i = 0; i < 7; ++i) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                nameCell(names[i]);
                ImGui::TableSetColumnIndex(1);
                dimText(notes[i]);
            }
            ImGui::EndTable();
        }
        ImGui::TreePop();
    }
    ImGui::End();
}

void App::drawInspector(ImVec2 origin, ImVec2 size) {
    const float us = uiScale();
    const float margin = 14.0f * us;
    if (size.x < 380.0f * us || size.y < 240.0f * us) return;   // 窗口太小就不占地方
    const float reveal = float(inspectorReveal_);
    if (reveal < 0.01f) return;
    const float w = std::min(306.0f * us, size.x - margin * 2.0f - 60.0f * us);

    // 收起时整块往右滑出去，透明度跟着走
    const float slide = (1.0f - reveal) * (w + margin * 2.0f);
    ImGui::SetNextWindowPos(ImVec2(origin.x + size.x - w - margin + slide, origin.y + margin));
    ImGui::SetNextWindowSize(ImVec2(w, std::max(160.0f, size.y - margin * 2.0f)));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16, 16));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 16.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, reveal);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImGui::ColorConvertU32ToFloat4(kPanelBg));
    ImGuiWindowFlags panelFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                  ImGuiWindowFlags_NoSavedSettings;
    if (reveal < 0.5f) panelFlags |= ImGuiWindowFlags_NoInputs;   // 滑出途中别响应点击
    ImGui::Begin("##inspector", nullptr, panelFlags);

    ImGui::PushStyleColor(ImGuiCol_Text, ImColor(96, 108, 124, 255).Value);
    ImGui::TextUnformatted("控制台");
    ImGui::PopStyleColor();

    bool dirty = false;

    // ---- 世界
    sectionTitle(lb::tr(lb::S::SectionWorld));
    {
        float g = float(project_.world.gravity);
        if (dragRow("重力", &g, 0.05f, -30.0f, 30.0f, "%.2f m/s²")) {
            project_.world.gravity = double(g);
            dirty = true;
        }
        if (level_ == UiLevel::Advanced) {
            float k = float(project_.world.killY);
            if (dragRow("销毁线", &k, 0.05f, -50.0f, 50.0f, "%.1f")) {
                project_.world.killY = double(k);
                dirty = true;
            }
        }
    }

    ImGui::Spacing();
    if (ImGui::Button(lb::tr(paused_ ? lb::S::BtnResume : lb::S::BtnPause), ImVec2(-1, 0))) togglePause();
    if (ImGui::Button(lb::tr(lb::S::BtnReset), ImVec2(-1, 0))) resetSimulation();
    if (ImGui::Button(lb::tr(lb::S::BtnResetToSaved), ImVec2(-1, 0))) resetToSaved();
    if (ImGui::Button(lb::tr(lb::S::BtnSave), ImVec2(-1, 0))) saveState();
    if (ImGui::Button(lb::tr(lb::S::BtnSaveAs), ImVec2(-1, 0))) beginSaveAs();
    {
        bool adv = level_ == UiLevel::Advanced;
        if (ImGui::Checkbox(lb::tr(lb::S::BtnAdvanced), &adv))
            setLevel(adv ? UiLevel::Advanced : UiLevel::Simple);
    }
    {
        bool es = editSave_;
        if (ImGui::Checkbox(lb::tr(lb::S::BtnEditSave), &es)) setEditSave(es);
        if (editSave_) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImColor(126, 226, 255, 255).Value);
            ImGui::TextWrapped("已暂停。此模式下放的球属于场景，可撤销、会存盘。");
            ImGui::PopStyleColor();
        }
    }
    ImGui::PushStyleColor(ImGuiCol_Text, ImColor(110, 120, 136, 255).Value);
    ImGui::TextWrapped("在此之前所有改动都只在内存里；按这个键才写盘。");
    ImGui::PopStyleColor();

    // ---- 选中对象
    if (selSet_.size() > 1) {
        sectionTitle(lb::tr(lb::S::SectionMulti));
        ImGui::Text("%d 个对象", int(selSet_.size()));
        ImGui::Spacing();
        if (ImGui::Button("复制 (Ctrl+C)", ImVec2(-1, 0))) copySelection();
        if (ImGui::Button("粘贴 (Ctrl+V)", ImVec2(-1, 0))) pasteClipboard();
        if (ImGui::Button("删除 (Del)", ImVec2(-1, 0))) {
            deleteSelected();
            ImGui::End();
            ImGui::PopStyleColor();
            ImGui::PopStyleVar(2);
            return;
        }
        dimText("拖动任意一个对象可整体移动，Shift 只沿主轴。");
    } else if (Line* line = selectedLine()) {
        sectionTitle(lb::tr(lb::S::SectionLine));
        ImGui::PushID(line->id);

        dirty |= paramRow("A.x", line->ax, 0.02f, -50, 50, "%.2f", level_ == UiLevel::Advanced);
        dirty |= paramRow("A.y", line->ay, 0.02f, -50, 50, "%.2f", level_ == UiLevel::Advanced);
        dirty |= paramRow("B.x", line->bx, 0.02f, -50, 50, "%.2f", level_ == UiLevel::Advanced);
        dirty |= paramRow("B.y", line->by, 0.02f, -50, 50, "%.2f", level_ == UiLevel::Advanced);

        ImGui::Spacing();
        if (ImGui::Checkbox("可碰撞", &line->collide)) dirty = true;

        sectionTitle("声音");
        {
            const bool adv0 = level_ == UiLevel::Advanced;
            if (!adv0) {
                dirty |= mappingRow("音高", line->note, kNoteMaps, int(std::size(kNoteMaps)));
                dirty |= mappingRow("力度", line->vel, kVelMaps, int(std::size(kVelMaps)));
                if (!line->note.expr) dirty |= paramRow("  基准音高", line->note, 0.5f, 0, 127, "%.0f", false);
                if (!line->vel.expr)  dirty |= paramRow("  固定力度", line->vel, 0.5f, 1, 127, "%.0f", false);
                if (line->note.expr) dimText(line->note.source.c_str());
                if (line->vel.expr)  dimText(line->vel.source.c_str());
                ImGui::Separator();
            }
            const auto& plist = audio_.presets(line->voice.file);

            auto findName = [&](int bank, int program) -> std::string {
                for (const auto& p : plist)
                    if (p.bank == bank && p.program == program) return p.name;
                return {};
            };
            auto setVoice = [&](int bank, int program) {
                line->voice.bank = bank;
                line->voice.program = program;
                dirty = true;
            };

            // 常驻显示当前音色
            std::string cur = findName(line->voice.bank, line->voice.program);
            ImGui::PushStyleColor(ImGuiCol_Text, ImColor(126, 226, 255, 255).Value);
            ImGui::TextWrapped("当前：%s", cur.empty() ? "（音色缺失）" : cur.c_str());
            ImGui::PopStyleColor();
            ImGui::PushStyleColor(ImGuiCol_Text, ImColor(110, 120, 136, 255).Value);
            ImGui::Text("bank %d / program %d%s", line->voice.bank, line->voice.program,
                        line->voice.bank >= 128 ? "（鼓）" : "");
            ImGui::PopStyleColor();

            if (level_ == UiLevel::Advanced) {
                static char search[64] = "";
                static int searchLineId = -1;
                if (searchLineId != line->id) {
                    searchLineId = line->id;
                    search[0] = 0;
                }
                ImGui::SetNextItemWidth(-1.0f);
                if (ImGui::BeginCombo("##voice", cur.empty() ? "选择音色…" : cur.c_str())) {
                    ImGui::SetNextItemWidth(-1.0f);
                    ImGui::InputTextWithHint("##search", "搜索音色…", search, sizeof(search));
                    ImGui::Separator();
                    auto contains = [](const std::string& s, const char* needle) {
                        if (!needle[0]) return true;
                        std::string a = s, b = needle;
                        for (auto& c : a) c = char(std::tolower((unsigned char)c));
                        for (auto& c : b) c = char(std::tolower((unsigned char)c));
                        return a.find(b) != std::string::npos;
                    };
                    ImGui::BeginChild("##list", ImVec2(0, 340), false);
                    int lastBank = -1;
                    for (const auto& p : plist) {
                        if (!contains(p.name, search)) continue;
                        if (p.bank != lastBank) {
                            lastBank = p.bank;
                            ImGui::PushStyleColor(ImGuiCol_Text, ImColor(120, 132, 150, 255).Value);
                            ImGui::TextUnformatted(p.bank >= 128 ? "鼓组（bank 128）" : "常规音色");
                            ImGui::PopStyleColor();
                        }
                        ImGui::PushID(p.bank * 1000 + p.program);
                        const bool sel = (p.bank == line->voice.bank && p.program == line->voice.program);
                        char label[192];
                        std::snprintf(label, sizeof(label), "%03d  %s", p.program, p.name.c_str());
                        if (ImGui::Selectable(label, sel)) {
                            setVoice(p.bank, p.program);
                            ImGui::CloseCurrentPopup();
                        }
                        if (sel) ImGui::SetItemDefaultFocus();
                        ImGui::PopID();
                    }
                    ImGui::EndChild();
                    ImGui::EndCombo();
                }
                if (ImGui::Button("载入其它 SF2…", ImVec2(-1, 0))) beginLoadSf2(line->id);
                if (!line->voice.file.empty()) {
                    dimText(line->voice.file.c_str());
                    if (ImGui::SmallButton("用回默认音源")) {
                        line->voice.file.clear();
                        dirty = true;
                    }
                }
            } else {
                // 简洁模式：只给几个常用音色
                static const struct { const char* zh; int bank, program; } kQuick[] = {
                    {"大钢琴", 0, 0},       {"电钢琴", 0, 4},     {"音乐盒", 0, 10},
                    {"颤音琴", 0, 11},      {"马林巴", 0, 12},    {"尼龙吉他", 0, 24},
                    {"指弹贝斯", 0, 33},    {"弦乐组", 0, 48},    {"合唱", 0, 52},
                    {"长笛", 0, 73},        {"鼓组", 128, 0},     {"鼓组·房间", 128, 8},
                };
                ImGui::SetNextItemWidth(-1.0f);
                if (ImGui::BeginCombo("##quick", cur.empty() ? "选择音色…" : cur.c_str())) {
                    for (const auto& q : kQuick) {
                        ImGui::PushID(q.program * 1000 + q.bank);
                        const bool sel = (q.bank == line->voice.bank && q.program == line->voice.program);
                        const std::string nm = findName(q.bank, q.program);
                        char label[128];
                        std::snprintf(label, sizeof(label), "%s  ·  %s", q.zh,
                                      nm.empty() ? "缺失" : nm.c_str());
                        if (ImGui::Selectable(label, sel)) {
                            setVoice(q.bank, q.program);
                            ImGui::CloseCurrentPopup();
                        }
                        ImGui::PopID();
                    }
                    ImGui::EndCombo();
                }
            }
        }

        const bool adv = level_ == UiLevel::Advanced;
        if (adv) {
            dirty |= paramRow("音高", line->note, 0.5f, 0, 127, "%.0f", true);
            dirty |= paramRow("力度", line->vel, 0.5f, 0, 127, "%.0f", true);
        }
        dirty |= paramRow("音量", line->gain, 0.01f, 0, 2, "%.2f", adv);

        ImGui::Spacing();
        if (ImGui::Button("删除线段", ImVec2(-1, 0))) {
            deleteSelected();
            ImGui::PopID();
            ImGui::End();
            ImGui::PopStyleColor();
            ImGui::PopStyleVar(2);
            return;
        }
        ImGui::PopID();
    } else if (Ball* ball = selectedBall()) {
        sectionTitle(lb::tr(lb::S::SectionBall));
        ImGui::PushID(ball->id);
        float px = float(ball->pos.x), py = float(ball->pos.y);
        if (dragRow("x", &px, 0.02f, -60, 60, "%.2f")) { ball->pos.x = px; dirty = true; }
        if (dragRow("y", &py, 0.02f, -60, 60, "%.2f")) { ball->pos.y = py; dirty = true; }
        float r = float(ball->radius);
        if (dragRow("半径", &r, 0.005f, 0.0f, 1.0f, "%.3f")) { ball->radius = r; dirty = true; }

        ImGui::Spacing();
        sectionTitle("速度");
        if (paused_) {
            float vx = float(ball->vel.x), vy = float(ball->vel.y);
            if (dragRow("vx", &vx, 0.05f, -60.0f, 60.0f, "%.2f")) { ball->vel.x = vx; dirty = true; }
            if (dragRow("vy", &vy, 0.05f, -60.0f, 60.0f, "%.2f")) { ball->vel.y = vy; dirty = true; }
            const double sp = std::sqrt(ball->vel.x * ball->vel.x + ball->vel.y * ball->vel.y);
            char buf[64];
            std::snprintf(buf, sizeof(buf), "速率 %.2f m/s  ·  %.0f°", sp,
                          std::atan2(ball->vel.y, ball->vel.x) * 180.0 / 3.14159265358979);
            dimText(buf);
        } else {
            ImGui::Text("%.2f, %.2f", ball->vel.x, ball->vel.y);
            dimText("暂停（空格）后可以改初速");
        }
        dimText("位置和速度每步由物理算出，不能写成表达式");
        ImGui::Spacing();
        const bool delBall = ImGui::Button("删除球", ImVec2(-1, 0));
        ImGui::PopID();
        if (delBall) {
            deleteSelected();
            ImGui::End();
            ImGui::PopStyleColor();
            ImGui::PopStyleVar(2);
            return;
        }
    } else if (Emitter* em = selectedEmitter()) {
        sectionTitle(lb::tr(lb::S::SectionEmitter));
        ImGui::PushID(em->id);
        dirty |= paramRow("x", em->px, 0.02f, -60, 60, "%.2f", level_ == UiLevel::Advanced);
        dirty |= paramRow("y", em->py, 0.02f, -60, 60, "%.2f", level_ == UiLevel::Advanced);
        dirty |= paramRow("方向", em->dir, 0.01f, -7, 7, "%.2f", level_ == UiLevel::Advanced);
        dirty |= paramRow("速度", em->speed, 0.05f, -40, 40, "%.2f", level_ == UiLevel::Advanced);
        dirty |= paramRow("间隔", em->interval, 0.01f, 0.02f, 10, "%.2f", level_ == UiLevel::Advanced);
        dirty |= paramRow("延迟", em->delay, 0.01f, 0, 30, "%.2f", level_ == UiLevel::Advanced);
        dirty |= paramRow("次数", em->count, 0.5f, -1, 200, "%.0f", level_ == UiLevel::Advanced);
        ImGui::Spacing();
        if (ImGui::Button("删除发射器", ImVec2(-1, 0))) {
            deleteSelected();
            ImGui::PopID();
            ImGui::End();
            ImGui::PopStyleColor();
            ImGui::PopStyleVar(2);
            return;
        }
        ImGui::PopID();
    } else {
        sectionTitle(lb::tr(lb::S::SectionHints));
        dimText("左边缘唤出工具 · 拖球改变高度");
        dimText("滚轮缩放 · 中键平移 · F 适配");
        dimText("右键选中 · 空格 暂停 · Esc 菜单 · Ctrl+Z 撤销");
        dimText("Ctrl+I 钉住面板 · 平时自动收起，鼠标贴右边滑出");
        ImGui::Spacing();
        dimText("每个参数点 ƒ 都能写成表达式：");
        dimText("  72 + hit.speed * 3");
        dimText("  scale(60 + t * 4, \"majorPent\")");
        dimText("  sin(t * 2) * 1.5");
    }

    if (level_ == UiLevel::Advanced) {
        bool sv = showVars_;
        if (ImGui::Checkbox("变量表 (Ctrl+L)", &sv)) showVars_ = sv;
    }

    // 编辑开始的那一刻留一份快照，真正改动时才压进撤销栈
    const bool itemActive = ImGui::IsAnyItemActive();
    if (itemActive && !anyItemActive_)
        pendingSnapshot_ = project_io::toJsonString(project_);
    anyItemActive_ = itemActive;

    if (dirty) {
        dirty_ = true;
        if (!pendingSnapshot_.empty()) {
            docStack_.push_back({pendingSnapshot_, world_.balls(), editSave_, ++undoSeq_});
            if (docStack_.size() > 64) docStack_.erase(docStack_.begin());
            pendingSnapshot_.clear();
        }
        world_.refreshLines();
    }

    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(3);
}

// ---------------------------------------------------------------- 状态

void App::drawStatus(ImVec2 origin, ImVec2 size) {
    ImDrawList* dl = ImGui::GetForegroundDrawList();

    char buf[256];
    std::snprintf(buf, sizeof(buf), "t %.1fs   %d 球   %d 线   %d 发   %.0f fps%s%s",
                  world_.time(), int(world_.balls().size()), int(project_.lines.size()),
                  int(project_.emitters.size()), fps_,
                  paused_ ? "   ‖ 暂停" : "", dirty_ ? "   ● 未保存" : "");

    const ImVec2 pos(origin.x + 16.0f, origin.y + size.y - 26.0f);
    dl->AddText(ImVec2(pos.x + 1, pos.y + 1), IM_COL32(0, 0, 0, 160), buf);
    dl->AddText(pos, kTextDim, buf);

    if (statusAge_ < 4.0 && !status_.empty()) {
        dl->AddText(ImVec2(pos.x, pos.y - 22.0f), IM_COL32(126, 226, 255, 220), status_.c_str());
    }
}
