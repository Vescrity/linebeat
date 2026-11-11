// 画布：相机变换、世界绘制、鼠标交互
#include <algorithm>
#include <cmath>

#include "app.h"
#include "imgui.h"
#include "ui.h"
#include "imgui_internal.h"

namespace {
constexpr ImU32 kBgTop     = IM_COL32(14, 16, 22, 255);
constexpr ImU32 kBgBottom  = IM_COL32(6, 7, 10, 255);
constexpr ImU32 kGrid      = IM_COL32(255, 255, 255, 12);
constexpr ImU32 kGridMajor = IM_COL32(255, 255, 255, 26);
constexpr ImU32 kLine      = IM_COL32(126, 226, 255, 255);
constexpr ImU32 kLineGlow  = IM_COL32(126, 226, 255, 36);
constexpr ImU32 kLineGlow2 = IM_COL32(126, 226, 255, 70);
constexpr ImU32 kBall      = IM_COL32(240, 252, 255, 255);
constexpr ImU32 kBallGlow  = IM_COL32(150, 230, 255, 46);
constexpr ImU32 kEmitter   = IM_COL32(255, 186, 120, 255);
constexpr ImU32 kSelection = IM_COL32(255, 214, 90, 255);
constexpr ImU32 kKill      = IM_COL32(255, 90, 110, 90);

ImU32 withAlpha(ImU32 col, int a) {
    return (col & 0x00FFFFFFu) | (ImU32(std::clamp(a, 0, 255)) << 24);
}

double distPointSegment(Vec2 p, Vec2 a, Vec2 b) {
    const Vec2 ab = b - a;
    const double l2 = dot(ab, ab);
    if (l2 < 1e-12) return length(p - a);
    double t = dot(p - a, ab) / l2;
    t = std::clamp(t, 0.0, 1.0);
    return length(p - (a + ab * t));
}

void drawArrow(ImDrawList* dl, ImVec2 c, float angle, float size, ImU32 col) {
    const ImVec2 dir(std::cos(angle), std::sin(angle));
    const ImVec2 perp(-dir.y, dir.x);
    const ImVec2 tip(c.x + dir.x * size, c.y + dir.y * size);
    const ImVec2 l(c.x - dir.x * size * 0.4f + perp.x * size * 0.7f,
                   c.y - dir.y * size * 0.4f + perp.y * size * 0.7f);
    const ImVec2 r(c.x - dir.x * size * 0.4f - perp.x * size * 0.7f,
                   c.y - dir.y * size * 0.4f - perp.y * size * 0.7f);
    dl->AddTriangleFilled(tip, l, r, col);
}
} // namespace

void App::draw(ImVec2 origin, ImVec2 size) {
    if (startScreen_) {
        drawStartScreen(origin, size);
        if (browserOpen_) drawSceneBrowser(origin, size);
    if (saveAsOpen_) drawSaveAs(origin, size);
        return;
    }
    const float us = uiScale();

    // 右侧面板：浮在画布上（不挤占画布，所以开合时视图不跳）。
    // 默认自动隐藏——鼠标贴右边缘或停在面板上时滑出；Ctrl+I 钉住不放。
    ImGuiIO& ioEdge = ImGui::GetIO();
    const float panelEst = 322.0f * us;
    const ImVec2 mp = ioEdge.MousePos;
    const bool inWindow = mp.y > origin.y && mp.y < origin.y + size.y - 1.0f;
    const bool nearRight = inWindow && mp.x >= origin.x + size.x - 36.0f * us &&
                           mp.x < origin.x + size.x;
    const bool overPanel = inWindow && inspectorReveal_ > 0.01 &&
                           mp.x >= origin.x + size.x - panelEst - 28.0f * us;
    const bool peek = inspectorPeekFrames_ > 0;
    if (inspectorPeekFrames_ > 0) --inspectorPeekFrames_;

    // 正在拖面板里的控件时别收：鼠标滑出控件也不该跳到一半就消失
    const bool holding = inspectorReveal_ > 0.5 && ImGui::IsAnyItemActive();
    const double target = (inspectorPinned_ || nearRight || overPanel || peek || holding) ? 1.0 : 0.0;
    inspectorReveal_ += (target - inspectorReveal_) * 0.25;
    if (inspectorReveal_ < 0.004) inspectorReveal_ = 0.0;
    if (inspectorReveal_ > 0.996) inspectorReveal_ = 1.0;
    const float reveal = float(inspectorReveal_);
    const bool panelVisible = reveal > 0.01f;

    viewCenter_ = ImVec2(origin.x + size.x * 0.5f, origin.y + size.y * 0.5f);
    canvasSize_ = ImVec2(std::max(1.0f, size.x), std::max(1.0f, size.y));
    if (pendingFit_) {
        pendingFit_ = false;
        fitToContent();
    }

    ImDrawList* bg = ImGui::GetBackgroundDrawList();
    drawWorld(bg, origin, size);

    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(origin);
    ImGui::SetNextWindowSize(size);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::Begin("##canvas", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                 ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus |
                 ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::InvisibleButton("##canvasbtn", ImGui::GetContentRegionAvail(),
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight |
                           ImGuiButtonFlags_MouseButtonMiddle);
    if (!menuOpen_) handleCanvasInput(origin, size);
    ImGui::End();
    ImGui::PopStyleVar(2);
    (void)io;

    drawMarquee(ImGui::GetBackgroundDrawList());

    // 左边缘的抓手：工具条收起时提示这里能拉出工具
    if (toolbarReveal_ < 0.85) {
        ImDrawList* fg = ImGui::GetBackgroundDrawList();
        const float a = (1.0f - float(toolbarReveal_)) * 0.45f;
        const float cy = origin.y + size.y * 0.5f;
        for (int k = -1; k <= 1; ++k) {
            fg->AddCircleFilled(ImVec2(origin.x + 7.0f, cy + k * 9.0f), 2.0f,
                                IM_COL32(180, 200, 220, int(140 * a)));
        }
    }

    // 右边缘的抓手：面板收起时提示这里能滑出
    if (inspectorReveal_ < 0.85 && size.x >= 380.0f * us) {
        ImDrawList* fg = ImGui::GetBackgroundDrawList();
        const float a = (1.0f - float(inspectorReveal_)) * 0.45f;
        const float cy = origin.y + size.y * 0.5f;
        for (int k = -1; k <= 1; ++k) {
            fg->AddCircleFilled(ImVec2(origin.x + size.x - 7.0f, cy + k * 9.0f), 2.0f,
                                IM_COL32(180, 200, 220, int(140 * a)));
        }
    }

    drawToolbar(origin, size);
    if (panelVisible) drawInspector(origin, size);
    if (menuOpen_) drawMenu(origin, size);
    if (browserOpen_) drawSceneBrowser(origin, size);
    if (saveAsOpen_) drawSaveAs(origin, size);
    drawVariables(origin, size);
    drawStatus(origin, size);
}

// ---------------------------------------------------------------- 绘制

void App::drawWorld(ImDrawList* dl, ImVec2 origin, ImVec2 size) {
    dl->AddRectFilledMultiColor(origin, ImVec2(origin.x + size.x, origin.y + size.y),
                                kBgTop, kBgTop, kBgBottom, kBgBottom);

    drawGrid(dl, origin, size);

    // 销毁线
    const double killY = project_.world.killY;
    const double halfSpan = size.x / cam_.scale;
    const double centerX = cam_.toWorld(ImVec2(viewCenter_.x, viewCenter_.y), viewCenter_).x;
    const ImVec2 k0 = cam_.toScreen(Vec2{centerX - halfSpan, killY}, viewCenter_);
    const ImVec2 k1 = cam_.toScreen(Vec2{centerX + halfSpan, killY}, viewCenter_);
    if (k0.y > origin.y && k0.y < origin.y + size.y) {
        dl->AddLine(k0, k1, kKill, 1.0f);
    }

    // 线段
    const auto& la = world_.lineA();
    const auto& lb = world_.lineB();
    for (size_t i = 0; i < project_.lines.size() && i < la.size(); ++i) {
        const Line& line = project_.lines[i];
        const ImVec2 a = cam_.toScreen(la[i], viewCenter_);
        const ImVec2 b = cam_.toScreen(lb[i], viewCenter_);
        const bool selected = (sel_.kind == SelKind::Line && sel_.id == line.id);
        const bool isFlash = (flashLineId_ == line.id && flashStrength_ > 0.01f);

        if (!line.collide) {
            dl->AddLine(a, b, IM_COL32(120, 130, 145, 120), 1.0f);
        } else {
            const int glowBoost = int(60 * flashStrength_ * (isFlash ? 1.0f : 0.0f));
            dl->AddLine(a, b, withAlpha(kLineGlow, 36 + glowBoost), 9.0f);
            dl->AddLine(a, b, withAlpha(kLineGlow2, 70 + glowBoost), 4.5f);
            dl->AddLine(a, b, kLine, 1.8f);
        }

        dl->AddCircleFilled(a, 2.5f, selected ? kSelection : withAlpha(kLine, 200));
        dl->AddCircleFilled(b, 2.5f, selected ? kSelection : withAlpha(kLine, 200));

        if (selected) {
            dl->AddLine(a, b, withAlpha(kSelection, 70), 1.0f);
            dl->AddCircle(a, 6.0f, kSelection, 0, 1.5f);
            dl->AddCircle(b, 6.0f, kSelection, 0, 1.5f);
        }
    }

    // 发射器
    for (const auto& e : project_.emitters) {
        Slots s;
        s.fillWorld(project_.world, world_.time(), World::kStep, world_.frame());
        s.fillEmitter(e);
        const Vec2 p{e.px.peek(s.v), e.py.peek(s.v)};
        const double dir = e.dir.peek(s.v);
        const ImVec2 c = cam_.toScreen(p, viewCenter_);
        const bool selected = (sel_.kind == SelKind::Emitter && sel_.id == e.id);
        dl->AddCircle(c, 9.0f, selected ? kSelection : withAlpha(kEmitter, 200), 0, 2.0f);
        drawArrow(dl, c, float(-dir), 10.0f, selected ? kSelection : kEmitter);
    }

    // 拖尾
    for (const auto& ball : world_.balls()) {
        auto it = trails_.find(ball.id);
        if (it == trails_.end() || it->second.size() < 2) continue;
        const auto& pts = it->second;
        for (size_t k = 0; k < pts.size(); ++k) {
            const float t = float(k) / float(pts.size() - 1);
            const ImVec2 p = cam_.toScreen(pts[k], viewCenter_);
            const float r = 1.5f + 3.5f * t;
            dl->AddCircleFilled(p, r, withAlpha(kBallGlow, int(6 + 40 * t * t)), 16);
        }
    }

    // 球
    for (const auto& ball : world_.balls()) {
        const ImVec2 c = cam_.toScreen(ball.pos, viewCenter_);
        const float r = std::max(5.0f, float(ball.radius * cam_.scale));
        const bool selected = (sel_.kind == SelKind::Ball && sel_.id == ball.id);
        dl->AddCircleFilled(c, r * 3.0f, kBallGlow, 48);
        dl->AddCircleFilled(c, r * 1.8f, withAlpha(kBallGlow, 70), 48);
        dl->AddCircleFilled(c, r, kBall, 48);
        if (selected) dl->AddCircle(c, r + 4.0f, kSelection, 0, 1.5f);
    }

    // 涟漪
    for (const auto& r : ripples_) {
        const float t = float(r.age / r.life);
        const ImVec2 c = cam_.toScreen(r.pos, viewCenter_);
        const float rad = 3.0f + 26.0f * t;
        const int a = int(220 * (1.0f - t) * (1.0f - t));
        dl->AddCircle(c, rad, IM_COL32(255, 200, 140, a), 0, 1.4f);
    }

    // 正在拖出的新线段
    if (dragging_ && dragLineId_ == 0 && tool_ == Tool::Line) {
        const ImVec2 a = cam_.toScreen(dragStartWorld_, viewCenter_);
        const ImVec2 b = ImGui::GetIO().MousePos;
        dl->AddLine(a, b, withAlpha(kLine, 160), 1.5f);
        dl->AddCircleFilled(a, 3.0f, kLine);
    }
}

void App::drawMarquee(ImDrawList* dl) {
    if (!marquee_) return;
    const ImVec2 a = cam_.toScreen(marqueeFrom_, viewCenter_);
    const ImVec2 b = cam_.toScreen(marqueeTo_, viewCenter_);
    const ImVec2 p0(std::min(a.x, b.x), std::min(a.y, b.y));
    const ImVec2 p1(std::max(a.x, b.x), std::max(a.y, b.y));
    dl->AddRectFilled(p0, p1, IM_COL32(126, 226, 255, 26));
    dl->AddRect(p0, p1, IM_COL32(126, 226, 255, 170), 0.0f, 0, 1.0f);
}

void App::drawGrid(ImDrawList* dl, ImVec2 origin, ImVec2 size) {
    const Vec2 tl = cam_.toWorld(origin, viewCenter_);
    const Vec2 br = cam_.toWorld(ImVec2(origin.x + size.x, origin.y + size.y), viewCenter_);

    const double step = 1.0;
    for (double x = std::floor(tl.x); x <= br.x + step; x += step) {
        const ImVec2 p0 = cam_.toScreen(Vec2{x, tl.y}, viewCenter_);
        const ImVec2 p1 = cam_.toScreen(Vec2{x, br.y}, viewCenter_);
        dl->AddLine(p0, p1, kGrid, 1.0f);
    }
    for (double y = std::floor(br.y); y <= tl.y + step; y += step) {
        const ImVec2 p0 = cam_.toScreen(Vec2{tl.x, y}, viewCenter_);
        const ImVec2 p1 = cam_.toScreen(Vec2{br.x, y}, viewCenter_);
        const bool axis = std::fabs(y) < 1e-6;
        dl->AddLine(p0, p1, axis ? kGridMajor : kGrid, 1.0f);
    }
}

// ---------------------------------------------------------------- 交互

bool App::hitTest(Vec2 world, SelKind& kind, int& id) const {
    const double tol = 11.0 / cam_.scale;

    double best = 1e9;
    for (const auto& b : world_.balls()) {
        const double d = length(b.pos - world);
        if (d < std::max(0.16, b.radius) + tol && d < best) {
            best = d;
            kind = SelKind::Ball;
            id = b.id;
        }
    }
    for (const auto& e : project_.emitters) {
        Slots s;
        s.fillWorld(project_.world, world_.time(), World::kStep, world_.frame());
        s.fillEmitter(e);
        const Vec2 p{e.px.peek(s.v), e.py.peek(s.v)};
        const double d = length(p - world);
        if (d < 0.18 + tol && d < best) {
            best = d;
            kind = SelKind::Emitter;
            id = e.id;
        }
    }
    for (size_t i = 0; i < project_.lines.size() && i < world_.lineA().size(); ++i) {
        const double d = distPointSegment(world, world_.lineA()[i], world_.lineB()[i]);
        if (d < tol && d < best) {
            best = d;
            kind = SelKind::Line;
            id = project_.lines[i].id;
        }
    }
    return kind != SelKind::None;
}

void App::handleCanvasInput(ImVec2 origin, ImVec2 size) {
    ImGuiIO& io = ImGui::GetIO();
    const bool hovered = ImGui::IsItemHovered();
    const bool active = ImGui::IsItemActive();
    const bool mouseValid = ImGui::IsMousePosValid();
    const Vec2 mouseWorld = cam_.toWorld(io.MousePos, viewCenter_);
    lastMouseWorld_ = mouseWorld;

    // 工具条唤出：靠近左边缘，或鼠标已经在工具条矩形上（几何判断，不受它自己遮挡影响）
    const float edge = io.MousePos.x - origin.x;
    const ImVec2 tb = lb::ui::toolbarPos(origin, size, float(toolbarReveal_));
    const bool overToolbar = io.MousePos.x >= tb.x - 6.0f &&
                             io.MousePos.x <= tb.x + lb::ui::kToolbarW + 6.0f &&
                             io.MousePos.y >= tb.y - 6.0f &&
                             io.MousePos.y <= tb.y + lb::ui::kToolbarH + 6.0f;
    const double target = (edge < 46.0f || overToolbar) ? 1.0 : 0.0;
    toolbarReveal_ += (target - toolbarReveal_) * 0.18;
    if (toolbarReveal_ < 0.002) toolbarReveal_ = 0.0;

    if (hovered && io.MouseWheel != 0.0f) {
        const Vec2 before = mouseWorld;
        const double f = std::pow(1.12, double(io.MouseWheel));
        cam_.scale = std::clamp(cam_.scale * f, 2.0, 4000.0);
        const Vec2 after = cam_.toWorld(io.MousePos, viewCenter_);
        cam_.cx += before.x - after.x;
        cam_.cy += before.y - after.y;
    }

    if (hovered && ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
        const ImVec2 d = io.MouseDelta;
        cam_.cx -= double(d.x) / cam_.scale;
        cam_.cy += double(d.y) / cam_.scale;
    }

    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        SelKind k = SelKind::None;
        int id = 0;
        if (mouseValid && hitTest(mouseWorld, k, id)) {
            selectOnly({k, id});
            inspectorPeekFrames_ = 120;
        }
    }

    // 松手：不管鼠标此刻在不在画布上，都要收尾
    const bool gestureOver = (dragging_ || marquee_) &&
                             (ImGui::IsMouseReleased(ImGuiMouseButton_Left) ||
                              !ImGui::IsMouseDown(ImGuiMouseButton_Left));
    if (gestureOver) {
        if (gestureTool_ == Tool::Line && dragLineId_ == 0 && mouseValid) {
            const bool noSnap = gestureAlt_;
            const bool axis = gestureShift_ && !noSnap;
            Vec2 a = dragStartWorld_;
            Vec2 b = mouseWorld;
            if (axis) {   // Shift：锁正交，取位移大的那个轴
                if (std::fabs(b.x - a.x) >= std::fabs(b.y - a.y)) b.y = a.y;
                else b.x = a.x;
            }
            if (!noSnap) {
                const double st = 0.05;
                a = Vec2{std::round(a.x / st) * st, std::round(a.y / st) * st};
                b = Vec2{std::round(b.x / st) * st, std::round(b.y / st) * st};
            }
            if (length(b - a) > 0.12) addLineAt(a, b);
        }
        dragging_ = false;
        draggingBall_ = false;
        draggingLinePoint_ = false;
        draggingBody_ = false;
        dragLineId_ = 0;
        dragBallId_ = 0;
        dragMoved_ = false;
        dragItems_.clear();
        if (marquee_) {
            marqueeApply(marqueeFrom_, marqueeTo_, gestureShift_);
            marquee_ = false;
        }
    }

    if (!hovered && !active) return;

    const bool noSnap = io.KeyAlt;
    const double snapStep = noSnap ? 0.0 : 0.05;
    auto snapv = [snapStep](double v) { return snapStep > 0 ? std::round(v / snapStep) * snapStep : v; };
    auto snapPoint = [&](Vec2 anchor, Vec2 p) {
        Vec2 q = p;
        if (io.KeyShift && !noSnap) {
            if (std::fabs(p.x - anchor.x) >= std::fabs(p.y - anchor.y)) q.y = anchor.y;
            else q.x = anchor.x;
        }
        return Vec2{snapv(q.x), snapv(q.y)};
    };

    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && mouseValid) {
        gestureTool_ = tool_;
        gestureShift_ = io.KeyShift;
        gestureAlt_ = io.KeyAlt;
        switch (tool_) {
        case Tool::Line:
            dragging_ = true;
            dragMoved_ = false;
            dragLineId_ = 0;
            dragStartWorld_ = mouseWorld;
            break;

        case Tool::Ball:
            addBallAt(mouseWorld);
            break;

        case Tool::Emitter:
            addEmitterAt(mouseWorld);
            break;

        case Tool::Erase: {
            SelKind k = SelKind::None;
            int id = 0;
            if (hitTest(mouseWorld, k, id)) {
                sel_ = {k, id};
                deleteSelected();
            }
            break;
        }

        case Tool::Select: {
            SelKind k = SelKind::None;
            int id = 0;
            if (hitTest(mouseWorld, k, id)) {
                if (io.KeyShift) toggleSelection({k, id});
                else if (!isSelected(k, id)) selectOnly({k, id});
                else sel_ = {k, id};

                dragging_ = true;
                dragMoved_ = false;
                dragStartWorld_ = mouseWorld;
                dragStartPos_ = mouseWorld;
                draggingLinePoint_ = false;
                draggingBody_ = false;
                draggingBall_ = false;

                // 只有单选一条线、且抓的是端点，才进入端点编辑
                if (selSet_.size() == 1 && k == SelKind::Line && isSelected(k, id)) {
                    size_t idx = 0;
                    for (size_t i = 0; i < project_.lines.size(); ++i)
                        if (project_.lines[i].id == id) idx = i;
                    if (idx < world_.lineA().size()) {
                        const Vec2 a = world_.lineA()[idx];
                        const Vec2 b = world_.lineB()[idx];
                        const double da = length(mouseWorld - a);
                        const double db = length(mouseWorld - b);
                        dragLineId_ = id;
                        if (std::min(da, db) < 0.25) {
                            draggingLinePoint_ = true;
                            dragEndpointA_ = (da < db);
                        }
                    }
                }

                if (!draggingLinePoint_) {
                    dragItems_.clear();
                    for (const auto& sel : selSet_) {
                        if (sel.kind == SelKind::Line) {
                            if (Line* l = findLine(sel.id))
                                dragItems_.push_back({SelKind::Line, sel.id, l->ax.constant,
                                                      l->ay.constant, l->bx.constant,
                                                      l->by.constant, {}});
                        } else if (sel.kind == SelKind::Ball) {
                            if (Ball* b = findBall(sel.id))
                                dragItems_.push_back({SelKind::Ball, sel.id, 0, 0, 0, 0, b->pos});
                        } else if (sel.kind == SelKind::Emitter) {
                            if (Emitter* e = findEmitter(sel.id))
                                dragItems_.push_back({SelKind::Emitter, sel.id, e->px.constant,
                                                      e->py.constant, 0, 0, {}});
                        }
                    }
                }
            } else {
                if (!io.KeyShift) {
                    selSet_.clear();
                    sel_.clear();
                }
                marquee_ = true;
                marqueeFrom_ = mouseWorld;
                marqueeTo_ = mouseWorld;
            }
            break;
        }
        }
    }

    if (marquee_ && ImGui::IsMouseDown(ImGuiMouseButton_Left) && mouseValid)
        marqueeTo_ = mouseWorld;

    if (dragging_ && ImGui::IsMouseDown(ImGuiMouseButton_Left) && mouseValid) {
        const bool worthwhile = draggingLinePoint_ || !dragItems_.empty() ||
                                (gestureTool_ == Tool::Line && dragLineId_ == 0);
        if (worthwhile && !dragMoved_ && length(mouseWorld - dragStartWorld_) > 0.03) {
            dragMoved_ = true;
            // 只记一次，别在每次移动里堆快照。球/发射器/整组拖动以前漏了这一步
            if (paused_) pushUndoWithBalls();
            else pushUndo();
        }

        if (tool_ == Tool::Line && dragLineId_ == 0) {
            // 预览由 drawWorld 负责
        } else if (draggingLinePoint_) {
            if (Line* l = findLine(dragLineId_)) {
                Vec2 anchor = dragEndpointA_ ? Vec2{l->b.x, l->b.y} : Vec2{l->a.x, l->a.y};
                for (size_t i = 0; i < project_.lines.size(); ++i) {
                    if (project_.lines[i].id != dragLineId_) continue;
                    if (i < world_.lineA().size())
                        anchor = dragEndpointA_ ? world_.lineB()[i] : world_.lineA()[i];
                    break;
                }
                const Vec2 q = snapPoint(anchor, mouseWorld);
                Param& px = dragEndpointA_ ? l->ax : l->bx;
                Param& py = dragEndpointA_ ? l->ay : l->by;
                px.constant = q.x; px.source.clear(); px.expr.reset(); px.error.clear();
                py.constant = q.y; py.source.clear(); py.expr.reset(); py.error.clear();
            }
        } else if (!dragItems_.empty()) {
            Vec2 delta = mouseWorld - dragStartPos_;
            if (io.KeyShift && !noSnap) {   // Shift：整体只沿主轴移动
                if (std::fabs(delta.x) >= std::fabs(delta.y)) delta.y = 0.0;
                else delta.x = 0.0;
            }
            if (!noSnap) {
                const double st = 0.05;
                delta.x = std::round(delta.x / st) * st;
                delta.y = std::round(delta.y / st) * st;
            }
            for (const auto& it : dragItems_) {
                if (it.kind == SelKind::Line) {
                    if (Line* l = findLine(it.id)) {
                        l->ax.constant = it.ax + delta.x; l->ay.constant = it.ay + delta.y;
                        l->bx.constant = it.bx + delta.x; l->by.constant = it.by + delta.y;
                        for (Param* p : {&l->ax, &l->ay, &l->bx, &l->by}) {
                            p->source.clear(); p->expr.reset(); p->error.clear();
                        }
                    }
                } else if (it.kind == SelKind::Ball) {
                    if (Ball* b = findBall(it.id)) {
                        b->pos = Vec2{it.pos.x + delta.x, it.pos.y + delta.y};
                        b->vel = Vec2{};
                    }
                } else if (it.kind == SelKind::Emitter) {
                    if (Emitter* e = findEmitter(it.id)) {
                        e->px.constant = it.ax + delta.x;
                        e->py.constant = it.ay + delta.y;
                        e->px.source.clear(); e->px.expr.reset(); e->px.error.clear();
                        e->py.source.clear(); e->py.expr.reset(); e->py.error.clear();
                    }
                }
            }
        }
        world_.refreshLines();
    }
}
