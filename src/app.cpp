#include "app.h"

#include "dialog.h"
#include "i18n.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>

#include "imgui.h"
#include "imgui_internal.h"

namespace {

const char* kDefaultSf2 = "/usr/share/soundfonts/FluidR3_GM.sf2";

std::string autosavePath() {
    const char* home = std::getenv("HOME");
    std::string dir = std::string(home ? home : ".") + "/.local/share/linebeat";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir + "/autosave.lbproj";
}
} // namespace

bool App::init(SDL_Window* window, const std::string& projectPath) {
    window_ = window;

    config_ = lb::loadConfig();
    lb::setLanguage(config_.language == "en" ? lb::Lang::En : lb::Lang::Zh);
    level_ = config_.advanced ? UiLevel::Advanced : UiLevel::Simple;

    if (audio_.init(kDefaultSf2, true)) {
        audioReady_ = true;
        audio_.setMasterGain(config_.masterGain);
    }

    projectPath_ = autosavePath();
    project_ = project_io::defaultProject();
    world_.reset(project_);
    cam_.cx = 0.0;
    cam_.cy = 1.2;
    cam_.scale = 92.0;

    // 界面缩放：只乘 FontScaleMain，高 DPI 由 DisplayFramebufferScale 处理
    ImGui::GetStyle().FontScaleMain = float(config_.uiScale);

    // 开始界面背后的演示：优先用 scenes/demo_loop.lbproj
    {
        const std::string dir = lb::demoScenesDir();
        demoCache_ = lb::listProjects(dir, 8);
        const auto& demos = demoCache_;
        bool ok = false;
        for (const auto& d : demos)
            if (d.find("demo_loop") != std::string::npos)
                ok = project_io::load(bgProject_, d, nullptr);
        if (!ok) bgProject_ = project_io::defaultProject();
        bgWorld_.reset(bgProject_);
        updateBgBounds();
    }

    if (!projectPath.empty()) {
        openProject(projectPath);
    } else {
        startScreen_ = true;
    }

    status_ = "拖动球 / 滚轮缩放 / 空格暂停 / Esc 菜单";
    return true;
}

void App::openProject(const std::string& path) {
    Project p;
    std::string err;
    int exprErr = 0;
    if (project_io::load(p, path, &err, &exprErr)) {
        project_ = p;
        projectPath_ = path;
        lb::rememberRecent(config_, path);
        lb::saveConfig(config_);
        status_ = exprErr > 0 ? ("已打开（" + std::to_string(exprErr) + " 个表达式有问题）")
                              : "已打开";
    } else {
        status_ = "打不开：" + err;
        statusAge_ = 0.0;
        return;                       // 保持当前文档不动
    }
    world_.reset(project_);
    sel_.clear();
    selSet_.clear();
    docStack_.clear();
    ballStack_.clear();
    trails_.clear();
    ripples_.clear();
    accumulator_ = 0.0;
    paused_ = false;
    editSave_ = false;
    dirty_ = false;
    startScreen_ = false;
    statusAge_ = 0.0;
    pendingFit_ = true;   // 等这一帧拿到真实画布尺寸再适配
}

void App::newProject(bool blank) {
    project_ = blank ? Project{} : project_io::defaultProject();
    selSet_.clear();
    if (blank) {
        project_.name = "untitled";
        project_.nextId = 1;
    }
    projectPath_ = autosavePath();
    world_.reset(project_);
    sel_.clear();
    docStack_.clear();
    ballStack_.clear();
    trails_.clear();
    ripples_.clear();
    paused_ = false;
    editSave_ = false;
    dirty_ = true;
    startScreen_ = false;
    cam_.cx = 0.0; cam_.cy = 1.0; cam_.scale = 92.0;
    status_ = "新场景（Ctrl+S 保存）";
    statusAge_ = 0.0;
}

void App::showStartScreen() {
    startScreen_ = true;
    demoCache_ = lb::listProjects(lb::demoScenesDir(), 8);
    bgWorld_.reset(bgProject_);
    updateBgBounds();
}

// 背景相机取一次景就别动了，否则球一多一少画面就跳
void App::updateBgBounds() {
    double minX = 1e9, minY = 1e9, maxX = -1e9, maxY = -1e9;
    auto add = [&](Vec2 q) {
        minX = std::min(minX, q.x); maxX = std::max(maxX, q.x);
        minY = std::min(minY, q.y); maxY = std::max(maxY, q.y);
    };
    for (const auto& l : bgProject_.lines) add(l.a), add(l.b);
    for (const auto& b : bgProject_.balls) add(b.pos);
    if (minX > maxX) { bgBounds_ = ImVec4(0, 0, 0, 0); return; }
    const double pad = 1.4;
    bgBounds_ = ImVec4(float(minX - pad), float(minY - pad), float(maxX + pad), float(maxY + pad));
}

void App::stepBackground(double dt) {
    bgAccum_ += dt;
    if (bgAccum_ > 0.25) bgAccum_ = 0.25;
    int steps = 0;
    while (bgAccum_ >= World::kStep && steps < 12) {
        bgWorld_.step();
        bgAccum_ -= World::kStep;
        ++steps;
    }
}


void App::shutdown() {
    // 只有「保存状态」会写盘，退出时什么都不做
    audio_.shutdown();
}

// ---------------------------------------------------------------- 逻辑

void App::debugSelectBall() {
    if (!world_.balls().empty()) selectOnly({SelKind::Ball, world_.balls()[0].id});
}

void App::debugSetBallVel(int id, double vx, double vy) {
    if (Ball* b = findBall(id)) { b->vel = Vec2{vx, vy}; }
}

double App::debugBallVy(int id) {
    const Ball* b = findBall(id);
    return b ? b->vel.y : -999.0;
}

void App::debugMoveBall(int id, double dx, double dy) {
    if (Ball* b = findBall(id)) { b->pos.x += dx; b->pos.y += dy; }
}

double App::debugBallY(int id) {
    const Ball* b = findBall(id);
    return b ? b->pos.y : -999.0;
}

void App::debugSelectFirstLine() {
    if (!project_.lines.empty()) sel_ = {SelKind::Line, project_.lines.front().id};
    toolbarReveal_ = 1.0;
    inspectorPinned_ = true;   // 调试截图时把面板钉住
}

void App::setUiScale(double s) {
    config_.uiScale = std::clamp(s, 0.7, 2.5);
    ImGui::GetStyle().FontScaleMain = float(config_.uiScale);
    lb::saveConfig(config_);
    status_ = "界面缩放 " + std::to_string(int(std::lround(config_.uiScale * 100.0))) + "%";
    statusAge_ = 0.0;
}

void App::togglePause() {
    paused_ = !paused_;
    if (paused_) {
        audio_.allNotesOff();
        status_ = "已暂停（空格 继续）";
    } else {
        ballStack_.clear();          // 暂停会话结束，球操作不再可撤销
        editSave_ = false;           // 编辑存档只在暂停时成立
        status_ = "运行中";
    }
    statusAge_ = 0.0;
}

void App::setEditSave(bool on) {
    if (on == editSave_) return;
    editSave_ = on;
    if (on) {
        paused_ = true;              // 编辑存档必然处于暂停
        ballStack_.clear();
        world_.reset(project_);      // 球回到初态
        trails_.clear();
        ripples_.clear();
        accumulator_ = 0.0;
        if (audioReady_) audio_.allNotesOff();
        status_ = "编辑存档：已暂停，球回到初态";
    } else {
        status_ = "退出编辑存档";
    }
    statusAge_ = 0.0;
}

void App::fitToContent() {
    if (project_.lines.empty() && world_.balls().empty() && project_.emitters.empty()) {
        cam_.cx = 0.0; cam_.cy = 0.0; cam_.scale = 92.0;
        return;
    }
    double minX = 1e9, minY = 1e9, maxX = -1e9, maxY = -1e9;
    auto add = [&](Vec2 p) {
        minX = std::min(minX, p.x); maxX = std::max(maxX, p.x);
        minY = std::min(minY, p.y); maxY = std::max(maxY, p.y);
    };
    for (size_t i = 0; i < project_.lines.size(); ++i) {
        if (i < world_.lineA().size()) { add(world_.lineA()[i]); add(world_.lineB()[i]); }
    }
    for (const auto& b : world_.balls()) add(b.pos);
    for (const auto& e : project_.emitters) add(Vec2{e.px.constant, e.py.constant});

    const double pad = 0.8;
    minX -= pad; minY -= pad; maxX += pad; maxY += pad;

    const double vw = std::max(1.0, double(canvasSize_.x));
    const double vh = std::max(1.0, double(canvasSize_.y));
    const double sx = vw / std::max(1e-6, maxX - minX);
    const double sy = vh / std::max(1e-6, maxY - minY);
    cam_.scale = std::clamp(std::min(sx, sy), 2.0, 4000.0);
    cam_.cx = 0.5 * (minX + maxX);
    cam_.cy = 0.5 * (minY + maxY);
}

void App::resetToSaved() {
    Project p;
    std::string err;
    int exprErr = 0;
    if (project_io::load(p, projectPath_, &err, &exprErr)) {
        project_ = p;
        status_ = "已重置为存档";
    } else {
        project_ = project_io::defaultProject();
        status_ = "读不到存档，已回到默认场景";
    }
    world_.reset(project_);
    sel_.clear();
    selSet_.clear();
    docStack_.clear();
    ballStack_.clear();
    trails_.clear();
    ripples_.clear();
    accumulator_ = 0.0;
    dirty_ = false;
    editSave_ = false;
    if (audioReady_) audio_.allNotesOff();
    statusAge_ = 0.0;
}

void App::resetSimulation() {
    for (auto& l : project_.lines) {
        l.note.clearState();
        l.vel.clearState();
        l.gain.clearState();
        l.ax.clearState(); l.ay.clearState();
        l.bx.clearState(); l.by.clearState();
    }
    for (auto& e : project_.emitters) {
        e.px.clearState(); e.py.clearState();
        e.dir.clearState(); e.speed.clearState();
        e.interval.clearState(); e.delay.clearState(); e.count.clearState();
    }
    world_.reset(project_);
    ripples_.clear();
    ballStack_.clear();          // 重置之后，之前的放球记录不该再能撤销
    accumulator_ = 0.0;
    if (audioReady_) audio_.allNotesOff();
    status_ = "已重置";
    statusAge_ = 0.0;
}

void App::beginLoadSf2(int lineId) {
    pendingDialog_ = PendingDialog::LoadSf2;
    pendingLineId_ = lineId;
    lb::openFileDialog(window_, "SoundFont", "sf2");
}

void App::beginSaveAs() {
    // 走自带输入框，不依赖系统对话框
    std::string base = project_.name.empty() ? std::string("untitled") : project_.name;
    if (!projectPath_.empty()) {
        std::error_code ec;
        const std::string stem = std::filesystem::path(projectPath_).stem().string();
        if (!stem.empty()) base = stem;
    }
    std::snprintf(saveAsName_, sizeof(saveAsName_), "%s", base.c_str());
    saveAsOpen_ = true;
}

void App::update(double dt) {
    const lb::FileDialogResult dlg = lb::pollFileDialog();
    if (dlg.done) {
        if (!dlg.path.empty()) {
            if (pendingDialog_ == PendingDialog::LoadSf2) {
                if (Line* l = findLine(pendingLineId_)) {
                    l->voice.file = dlg.path;
                    status_ = "已载入 SoundFont";
                    statusAge_ = 0.0;
                }
            }
        }
        pendingDialog_ = PendingDialog::None;
    }

    if (startScreen_) { stepBackground(dt); return; }

    time_ += dt;
    ++frame_;
    fps_ = fps_ * 0.9 + (dt > 1e-6 ? 1.0 / dt : 60.0) * 0.1;

    if (paused_) world_.refreshLines();

    if (!paused_) {
        accumulator_ += dt;
        int steps = 0;
        while (accumulator_ >= World::kStep && steps < World::kMaxCatchUp) {
            world_.step();
            triggerHits();
            accumulator_ -= World::kStep;
            ++steps;
        }
        if (accumulator_ > World::kStep * World::kMaxCatchUp) accumulator_ = 0.0;
    } else {
        accumulator_ = 0.0;
    }

    if (!paused_) {
        for (const auto& b : world_.balls()) {
            auto& tr = trails_[b.id];
            tr.push_back(b.pos);
            if (tr.size() > 16) tr.erase(tr.begin());
        }
        for (auto it = trails_.begin(); it != trails_.end();) {
            bool alive = false;
            for (const auto& b : world_.balls()) if (b.id == it->first) { alive = true; break; }
            it = alive ? std::next(it) : trails_.erase(it);
        }
    }

    if (editSave_) syncBallsToProject();
    updateEffects(dt);
    statusAge_ += dt;
}

// 编辑存档模式下，球的一切改动（拖动、检查器改参数）都要写回工程，
// 否则切回来时 world_.reset(project_) 会把它恢复成旧值
void App::syncBallsToProject() {
    for (auto& b : project_.balls) {
        if (const Ball* live = findBall(b.id)) {
            b.pos = live->pos;
            b.vel = live->vel;
            b.radius = live->radius;
        }
    }
}

void App::updateEffects(double dt) {
    for (auto& r : ripples_) r.age += dt;
    ripples_.erase(std::remove_if(ripples_.begin(), ripples_.end(),
                                  [](const Ripple& r) { return r.age >= r.life; }),
                   ripples_.end());
    flashStrength_ = std::max(0.0f, flashStrength_ - float(dt / 0.20));
}

void App::triggerHits() {
    for (const auto& hit : world_.hits()) {
        playHitSound(hit);
        ripples_.push_back({hit.point, 0.0, 0.55});
        if (ripples_.size() > 40) ripples_.erase(ripples_.begin());

        flashLineId_ = hit.lineId;
        flashPoint_ = hit.point;
        flashStrength_ = 1.0f;
    }
}

void App::playHitSound(const Hit& hit) {
    if (!audioReady_) return;
    Line* line = findLine(hit.lineId);
    if (!line) return;

    Slots s;
    s.fillWorld(project_.world, world_.time(), World::kStep, world_.frame());
    for (size_t i = 0; i < project_.lines.size(); ++i)
        if (project_.lines[i].id == line->id) { s.v[SLOT_IDX] = double(i); break; }
    {
        size_t li = 0;
        for (size_t i = 0; i < project_.lines.size(); ++i)
            if (project_.lines[i].id == line->id) { li = i; break; }
        if (li < world_.lineA().size()) s.fillLine(*line, world_.lineA()[li], world_.lineB()[li]);
        else s.fillLine(*line);
    }
    s.fillHit(hit);

    const double noteV = line->note.eval(s.v);
    const double velV  = line->vel.eval(s.v);
    const double gainV = line->gain.eval(s.v);
    if (!std::isfinite(noteV) || !std::isfinite(velV) || !std::isfinite(gainV)) return;

    const int note = std::clamp(int(std::lround(noteV)), 0, 127);
    const int vel  = std::clamp(int(std::lround(velV)), 1, 127);
    const float g  = float(std::clamp(gainV, 0.0, 2.0));
    const int ch   = audio_.ensureVoice(line->voice);
    audio_.trigger(ch, note, vel, g);
}

// ---------------------------------------------------------------- 对象查找

Line* App::findLine(int id) {
    for (auto& l : project_.lines) if (l.id == id) return &l;
    return nullptr;
}
Ball* App::findBall(int id) {
    for (auto& b : world_.balls()) if (b.id == id) return &b;
    return nullptr;
}
Emitter* App::findEmitter(int id) {
    for (auto& e : project_.emitters) if (e.id == id) return &e;
    return nullptr;
}

Line*    App::selectedLine()    { return sel_.kind == SelKind::Line ? findLine(sel_.id) : nullptr; }
Ball*    App::selectedBall()    { return sel_.kind == SelKind::Ball ? findBall(sel_.id) : nullptr; }
Emitter* App::selectedEmitter() { return sel_.kind == SelKind::Emitter ? findEmitter(sel_.id) : nullptr; }

// ---------------------------------------------------------------- 编辑

void App::pushUndo() {
    dirty_ = true;
    docStack_.push_back({project_io::toJsonString(project_), {}, false, ++undoSeq_});
    if (docStack_.size() > 64) docStack_.erase(docStack_.begin());
}

void App::pushUndoWithBalls() {
    dirty_ = true;
    docStack_.push_back({project_io::toJsonString(project_), world_.balls(), true, ++undoSeq_});
    if (docStack_.size() > 64) docStack_.erase(docStack_.begin());
}

void App::pushBallUndo() {
    ballStack_.push_back({project_io::toJsonString(project_), world_.balls(), true, ++undoSeq_});
    if (ballStack_.size() > 64) ballStack_.erase(ballStack_.begin());
}

void App::undo() {
    UndoEntry* d = docStack_.empty() ? nullptr : &docStack_.back();
    UndoEntry* b = (paused_ && !ballStack_.empty()) ? &ballStack_.back() : nullptr;

    const bool fromBall = b && (!d || b->seq > d->seq);
    if (!b && !d) return;

    // 先解析、成功了再出栈：解析失败不能白吃一条撤销记录
    UndoEntry entry = fromBall ? ballStack_.back() : docStack_.back();
    Project p;
    if (!project_io::fromJsonString(p, entry.json, nullptr)) return;
    if (fromBall) ballStack_.pop_back(); else docStack_.pop_back();
    sel_.clear();
    selSet_.clear();
    {
        const int keepNext = project_.nextId;
        project_ = p;
        project_.nextId = std::max(project_.nextId, keepNext);   // id 不回退，避免复用
    }
    if (entry.hasBalls && paused_) world_.setBalls(entry.balls);   // 只有暂停时球列表才是准的
    else                           world_.refreshLines();
    trails_.clear();
    ripples_.clear();
    if (audioReady_) audio_.allNotesOff();
    dirty_ = true;
    status_ = "已撤销";
    statusAge_ = 0.0;
}

void App::addLineAt(Vec2 a, Vec2 b) {
    pushUndo();
    VoiceRef voice;
    voice.program = 10;
    // 新线沿用当前选中线的音色
    if (Line* l = selectedLine()) voice = l->voice;
    project_.lines.push_back(makeDefaultLine(project_.allocId(), a, b, voice));
    selectOnly({SelKind::Line, project_.lines.back().id});
    world_.syncStructure();
}

void App::addBallAt(Vec2 p) {
    // 编辑存档模式下，放球是文档编辑；普通暂停下可撤销；运行中只是运行时动作
    if (editSave_) pushUndoWithBalls();
    else if (paused_) pushBallUndo();

    Ball rec;
    rec.id = project_.allocId();
    rec.pos = p;

    if (editSave_) project_.balls.push_back(rec);   // 成为场景的一部分
    world_.addBall(p, Vec2{0, 0}, 0.0, 0, rec.id);

    dirty_ = true;
    sel_ = {SelKind::Ball, rec.id};
    status_ = editSave_ ? "放了颗球（编辑存档）"
                        : (paused_ ? "放了颗球（暂停中，可撤销）" : "放了颗球（保存状态才会存盘）");
    statusAge_ = 0.0;
}

void App::saveState() {
    Project tmp = project_;          // 失败时不动内存里的文档
    tmp.balls.clear();
    for (const auto& b : world_.balls()) {
        Ball nb;
        nb.id = b.id;
        nb.pos = b.pos;
        nb.vel = b.vel;
        nb.radius = b.radius;
        tmp.balls.push_back(nb);
    }
    std::string err;
    if (project_io::save(tmp, projectPath_, &err)) {
        project_ = std::move(tmp);
        dirty_ = false;
        status_ = "已保存：" + std::to_string(project_.balls.size()) + " 颗球";
    } else {
        status_ = "保存失败: " + err;
    }
    statusAge_ = 0.0;
}

void App::addEmitterAt(Vec2 p) {
    pushUndo();
    Emitter e;
    e.id = project_.allocId();
    e.px = pConst(p.x);
    e.py = pConst(p.y);
    e.dir = pConst(0.0);
    e.speed = pConst(5.0);
    e.interval = pConst(1.0);
    e.delay = pConst(0.0);
    e.count = pConst(-1.0);
    project_.emitters.push_back(e);
    sel_ = {SelKind::Emitter, e.id};
    world_.syncStructure();
    status_ = "放了台发射器";
    statusAge_ = 0.0;
}

bool App::isSelected(SelKind k, int id) const {
    for (const auto& s : selSet_)
        if (s.kind == k && s.id == id) return true;
    return sel_.kind == k && sel_.id == id;
}

void App::selectOnly(Selection s) {
    selSet_.clear();
    sel_ = s;
    if (s.valid()) selSet_.push_back(s);
}

void App::toggleSelection(Selection s) {
    for (auto it = selSet_.begin(); it != selSet_.end(); ++it) {
        if (it->kind == s.kind && it->id == s.id) {
            selSet_.erase(it);
            sel_ = selSet_.empty() ? Selection{} : selSet_.back();
            return;
        }
    }
    selSet_.push_back(s);
    sel_ = s;
}

void App::copySelection() {
    clipboard_ = Project{};
    if (selSet_.empty() && sel_.valid()) selSet_.push_back(sel_);
    for (const auto& s : selSet_) {
        if (s.kind == SelKind::Line) {
            for (const auto& l : project_.lines) if (l.id == s.id) clipboard_.lines.push_back(l);
        } else if (s.kind == SelKind::Ball) {
            for (const auto& b : world_.balls()) if (b.id == s.id) clipboard_.balls.push_back(b);
        } else if (s.kind == SelKind::Emitter) {
            for (const auto& e : project_.emitters) if (e.id == s.id) clipboard_.emitters.push_back(e);
        }
    }
    const size_t n = clipboard_.lines.size() + clipboard_.balls.size() + clipboard_.emitters.size();
    status_ = n ? ("已复制 " + std::to_string(n) + " 个对象") : "没有可复制的对象";
    statusAge_ = 0.0;
}

void App::pasteClipboard() {
    const size_t n = clipboard_.lines.size() + clipboard_.balls.size() + clipboard_.emitters.size();
    if (!n) return;
    if (paused_) pushUndoWithBalls();
    else pushUndo();

    // 以剪贴板内容的中心对齐到鼠标位置
    double minX = 1e9, minY = 1e9, maxX = -1e9, maxY = -1e9;
    auto grow = [&](double x, double y) {
        minX = std::min(minX, x); maxX = std::max(maxX, x);
        minY = std::min(minY, y); maxY = std::max(maxY, y);
    };
    for (const auto& l : clipboard_.lines) {
        grow(l.ax.constant, l.ay.constant);
        grow(l.bx.constant, l.by.constant);
    }
    for (const auto& b : clipboard_.balls) grow(b.pos.x, b.pos.y);
    for (const auto& e : clipboard_.emitters) grow(e.px.constant, e.py.constant);

    const Vec2 target = lastMouseWorld_;
    const double dx = target.x - 0.5 * (minX + maxX);
    const double dy = target.y - 0.5 * (minY + maxY);

    selSet_.clear();
    for (auto l : clipboard_.lines) {
        l.id = project_.allocId();
        l.ax.constant += dx; l.bx.constant += dx;
        l.ay.constant += dy; l.by.constant += dy;
        project_.lines.push_back(l);
        selSet_.push_back({SelKind::Line, l.id});
    }
    for (auto b : clipboard_.balls) {
        b.id = project_.allocId();
        b.pos = Vec2{b.pos.x + dx, b.pos.y + dy};
        world_.addBall(b.pos, b.vel, b.radius, 0, b.id);
        if (editSave_) project_.balls.push_back(b);   // 编辑存档里粘贴的球属于场景
        selSet_.push_back({SelKind::Ball, b.id});
    }
    for (auto e : clipboard_.emitters) {
        e.id = project_.allocId();
        e.px.constant += dx; e.py.constant += dy;
        project_.emitters.push_back(e);
        selSet_.push_back({SelKind::Emitter, e.id});
    }
    sel_ = selSet_.empty() ? Selection{} : selSet_.back();
    world_.syncStructure();
    dirty_ = true;
    status_ = "已粘贴 " + std::to_string(n) + " 个对象";
    statusAge_ = 0.0;
}

void App::selectAll() {
    selSet_.clear();
    for (const auto& l : project_.lines) selSet_.push_back({SelKind::Line, l.id});
    for (const auto& b : world_.balls()) selSet_.push_back({SelKind::Ball, b.id});
    for (const auto& e : project_.emitters) selSet_.push_back({SelKind::Emitter, e.id});
    sel_ = selSet_.empty() ? Selection{} : selSet_.back();
    status_ = "已全选 " + std::to_string(selSet_.size()) + " 个对象";
    statusAge_ = 0.0;
}

void App::marqueeApply(Vec2 a, Vec2 b, bool additive) {
    const double x0 = std::min(a.x, b.x), x1 = std::max(a.x, b.x);
    const double y0 = std::min(a.y, b.y), y1 = std::max(a.y, b.y);
    auto inRect = [&](Vec2 p) { return p.x >= x0 && p.x <= x1 && p.y >= y0 && p.y <= y1; };

    if (!additive) selSet_.clear();
    for (size_t i = 0; i < project_.lines.size(); ++i) {
        const Vec2 A = i < world_.lineA().size() ? world_.lineA()[i]
                                                 : Vec2{project_.lines[i].ax.constant,
                                                        project_.lines[i].ay.constant};
        const Vec2 B = i < world_.lineB().size() ? world_.lineB()[i]
                                                 : Vec2{project_.lines[i].bx.constant,
                                                        project_.lines[i].by.constant};
        const bool hit = inRect(A) || inRect(B) ||
                         (std::min(A.x, B.x) <= x1 && std::max(A.x, B.x) >= x0 &&
                          std::min(A.y, B.y) <= y1 && std::max(A.y, B.y) >= y0);
        if (hit && !isSelected(SelKind::Line, project_.lines[i].id))
            selSet_.push_back({SelKind::Line, project_.lines[i].id});
    }
    for (const auto& ball : world_.balls())
        if (inRect(ball.pos) && !isSelected(SelKind::Ball, ball.id))
            selSet_.push_back({SelKind::Ball, ball.id});
    for (const auto& e : project_.emitters) {
        const Vec2 p{e.px.constant, e.py.constant};
        if (inRect(p) && !isSelected(SelKind::Emitter, e.id))
            selSet_.push_back({SelKind::Emitter, e.id});
    }
    sel_ = selSet_.empty() ? Selection{} : selSet_.back();
}

void App::deleteSelected() {
    if (selSet_.empty() && sel_.valid()) selSet_.push_back(sel_);
    if (selSet_.empty()) return;

    bool anyDoc = false, anyBall = false;
    for (const auto& s : selSet_) {
        if (s.kind == SelKind::Ball) anyBall = true;
        else anyDoc = true;
    }
    if (anyDoc) {
        if (anyBall && paused_) pushUndoWithBalls();
        else pushUndo();
    } else if (editSave_) pushUndoWithBalls();
    else if (paused_) pushBallUndo();

    for (const auto& s : selSet_) {
        if (s.kind == SelKind::Line) {
            project_.lines.erase(std::remove_if(project_.lines.begin(), project_.lines.end(),
                                                [&](const Line& l) { return l.id == s.id; }),
                                 project_.lines.end());
        } else if (s.kind == SelKind::Emitter) {
            project_.emitters.erase(
                std::remove_if(project_.emitters.begin(), project_.emitters.end(),
                               [&](const Emitter& e) { return e.id == s.id; }),
                project_.emitters.end());
        } else if (s.kind == SelKind::Ball) {
            auto& balls = world_.balls();
            const int id = s.id;
            balls.erase(std::remove_if(balls.begin(), balls.end(),
                                       [id](const Ball& b) { return b.id == id; }),
                        balls.end());
            if (editSave_)
                project_.balls.erase(std::remove_if(project_.balls.begin(), project_.balls.end(),
                                                    [id](const Ball& b) { return b.id == id; }),
                                     project_.balls.end());
        }
    }
    const size_t n = selSet_.size();
    selSet_.clear();
    sel_.clear();
    world_.syncStructure();
    status_ = n > 1 ? ("已删除 " + std::to_string(n) + " 个对象")
                    : (anyBall ? (editSave_ ? "已删除（编辑存档）" : "已删除（只在本次暂停内可撤销）")
                               : "已删除");
    statusAge_ = 0.0;
}

// ---------------------------------------------------------------- 输入

void App::handleEvent(const SDL_Event& e) {
    // 界面缩放是全局的：开始界面、菜单、弹窗、输入框里都能用
    if (e.type == SDL_EVENT_KEY_DOWN) {
        const bool ctrl = (e.key.mod & (SDL_KMOD_CTRL | SDL_KMOD_GUI)) != 0;
        if (ctrl && !e.key.repeat) {
            const SDL_Keycode k = e.key.key;
            if (k == SDLK_EQUALS || k == SDLK_KP_PLUS) { setUiScale(config_.uiScale + 0.1); return; }
            if (k == SDLK_MINUS || k == SDLK_KP_MINUS) { setUiScale(config_.uiScale - 0.1); return; }
            if (k == SDLK_0) { setUiScale(1.0); return; }
        }
    }

    if (startScreen_) return;      // 开始界面上不要让编辑快捷键落到隐藏的工程上
    if (e.type == SDL_EVENT_KEY_DOWN) {
        const bool ctrl = (e.key.mod & (SDL_KMOD_CTRL | SDL_KMOD_GUI)) != 0;
        const bool typing = ImGui::GetIO().WantTextInput;
        const bool repeat = e.key.repeat;

        // Esc 全局优先：先关确认框，再开关菜单
        if (e.key.key == SDLK_ESCAPE && !typing) {
            if (!repeat) {
                if (confirmOpen_) confirmOpen_ = false;
                else menuOpen_ = !menuOpen_;
            }
            return;
        }
        if (saveAsOpen_) return;    // 另存为输入框自己处理按键
        if (browserOpen_) return;   // 存档浏览界面自己处理按键
        if (menuOpen_) return;      // 菜单盖着时不响应编辑快捷键
        if (typing) return;     // 正在文本框里打字，不抢快捷键
        switch (e.key.key) {

        case SDLK_DELETE:
        case SDLK_BACKSPACE:
            deleteSelected();
            break;
        case SDLK_SPACE:
            if (!repeat) togglePause();
            break;
        case SDLK_1: tool_ = Tool::Select; break;
        case SDLK_2: tool_ = Tool::Line; break;
        case SDLK_3: tool_ = Tool::Ball; break;
        case SDLK_4: tool_ = Tool::Emitter; break;
        case SDLK_5: tool_ = Tool::Erase; break;
        case SDLK_Z:
            if (ctrl) undo();
            break;
        case SDLK_S:
            if (ctrl && (e.key.mod & SDL_KMOD_SHIFT)) beginSaveAs();
            else if (ctrl) saveState();
            break;

        case SDLK_O:
            if (ctrl) openSceneBrowser();
            break;

        case SDLK_E:
            if (ctrl) setEditSave(!editSave_);
            break;

        case SDLK_C:
            if (ctrl) copySelection();
            break;

        case SDLK_V:
            if (ctrl) pasteClipboard();
            break;

        case SDLK_A:
            if (ctrl) selectAll();
            break;

        case SDLK_L:
            if (ctrl) showVars_ = !showVars_;
            break;

        case SDLK_F:
            if (!ctrl) fitToContent();
            break;

        case SDLK_TAB:
            if (ctrl) {
                level_ = level_ == UiLevel::Advanced ? UiLevel::Simple : UiLevel::Advanced;
                status_ = level_ == UiLevel::Advanced ? "高级模式" : "简洁模式";
                statusAge_ = 0.0;
            }
            break;
        case SDLK_I:
            if (ctrl) { inspectorPinned_ = !inspectorPinned_; inspectorPeekFrames_ = 0; }
            break;
        default:
            break;
        }
    }
}
