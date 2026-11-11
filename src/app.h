// 应用层：窗口内容、相机、工具、选择、面板
#pragma once
#include <SDL3/SDL.h>

#include <string>
#include <unordered_map>
#include <vector>

#include "imgui.h"

#include "audio.h"
#include "model.h"
#include "physics.h"
#include "config.h"
#include "project_io.h"

struct Camera {
    double cx = 0.0;      // 世界中心（米）
    double cy = 1.0;
    double scale = 92.0;  // 每个世界单位多少像素

    ImVec2 toScreen(Vec2 w, ImVec2 origin) const {
        return ImVec2(origin.x + float((w.x - cx) * scale),
                      origin.y - float((w.y - cy) * scale));
    }
    Vec2 toWorld(ImVec2 s, ImVec2 origin) const {
        return Vec2{cx + double(s.x - origin.x) / scale,
                    cy - double(s.y - origin.y) / scale};
    }
};

enum class Tool { Select, Line, Ball, Emitter, Erase };
enum class SelKind { None, Line, Ball, Emitter };

struct Selection {
    SelKind kind = SelKind::None;
    int     id   = 0;
    bool    valid() const { return kind != SelKind::None; }
    void    clear() { kind = SelKind::None; id = 0; }
};

struct Ripple {
    Vec2   pos;
    double age = 0.0;
    double life = 0.55;
};

class App {
public:
    enum class UiLevel { Simple, Advanced };

    bool init(SDL_Window* window, const std::string& projectPath);
    void shutdown();

    void handleEvent(const SDL_Event& e);
    void update(double dt);
    void draw(ImVec2 viewOrigin, ImVec2 viewSize);

    bool paused() const { return paused_; }
    const World& world() const { return world_; }
    const Project& project() const { return project_; }
    void togglePause();
    void setUiScale(double s);
    float uiScale() const { return float(config_.uiScale); }
    void resetSimulation();
    void fitToContent();
    void debugSelectFirstLine();
    void debugSelectBall();
    void debugPinInspector() { inspectorPinned_ = true; }
    void debugApplyFirstLineMapping() {
        if (project_.lines.empty()) return;
        Line& l = project_.lines[0];
        l.note.setSource("scale(45 + hit.speed * 5, \"majorPent\")");
        l.vel.setSource("clamp(24 + hit.speed * 10, 20, 127)");
        selectOnly({SelKind::Line, l.id});
        world_.refreshLines();
        dirty_ = true;
    }
    void   debugSetBallVel(int id, double vx, double vy);
    double debugBallVy(int id);
    void   debugMoveBall(int id, double dx, double dy);
    double debugBallY(int id);
    bool quitRequested() const { return quitRequested_; }
    bool requestQuit();     // 窗口关闭按钮：有改动就弹确认，返回 false
    bool menuOpen() const { return menuOpen_; }
    void debugOpenMenu(int confirmAction) {   // -1 = 普通菜单
        menuOpen_ = true;
        if (confirmAction >= 0) { confirmOpen_ = true; confirmAction_ = confirmAction; }
    }
    void debugMarkDirty() { dirty_ = true; }
    void openMenu() { menuOpen_ = true; }
    void openSceneBrowser();
    void debugBeginSaveAs() { beginSaveAs(); }
    void debugSetSaveAsName(const char* n) {
        std::snprintf(saveAsName_, sizeof(saveAsName_), "%s", n);
    }
    bool sceneBrowserOpen() const { return browserOpen_; }
    double inspectorReveal() const { return inspectorReveal_; }
    void refreshSceneEntries();
    void drawSceneBrowser(ImVec2 origin, ImVec2 size);
    void drawSaveAs(ImVec2 origin, ImVec2 size);
    bool editSave() const { return editSave_; }
    bool startScreen() const { return startScreen_; }
    void setLevel(UiLevel l) { level_ = l; config_.advanced = (l == UiLevel::Advanced); }
    int  docUndoDepth() const { return int(docStack_.size()); }
    int  ballUndoDepth() const { return int(ballStack_.size()); }
    size_t selectionCount() const { return selSet_.size(); }
    double toolbarReveal() const { return toolbarReveal_; }

private:
    // 世界绘制与交互
    void drawWorld(ImDrawList* dl, ImVec2 origin, ImVec2 size);
    void drawGrid(ImDrawList* dl, ImVec2 origin, ImVec2 size);
    void drawMarquee(ImDrawList* dl);
    void selectAll();
    void updateEffects(double dt);
    void handleCanvasInput(ImVec2 origin, ImVec2 size);
    bool hitTest(Vec2 world, SelKind& kind, int& id) const;
    void triggerHits();

    // 面板
    void drawToolbar(ImVec2 origin, ImVec2 size);
    void drawInspector(ImVec2 origin, ImVec2 size);
    void drawStatus(ImVec2 origin, ImVec2 size);

    // 工具
    Line*    selectedLine();
    Ball*    selectedBall();
    Emitter* selectedEmitter();
    Line*    findLine(int id);
    Ball*    findBall(int id);
    Emitter* findEmitter(int id);

    void pushUndo();
    void pushUndoWithBalls();
    void pushBallUndo();
    void undo();
    void setEditSave(bool on);
    void resetToSaved();
    void openProject(const std::string& path);
    void newProject(bool blank);
    void showStartScreen();
    void drawStartScreen(ImVec2 origin, ImVec2 size);
    void drawMenu(ImVec2 origin, ImVec2 size);
    void doMenuAction(int action);      // 0=返回 1=返回至标题 2=退出
    bool performMenuAction(int action); // 真正执行，返回是否要退出程序
    void stepBackground(double dt);
    void updateBgBounds();
    void syncBallsToProject();
    void drawVariables(ImVec2 origin, ImVec2 size);
    bool showVars_ = false;
public:
    void setShowVars(bool v) { showVars_ = v; }
private:
    const lb::Config& config() const { return config_; }
    void beginLoadSf2(int lineId);
    void beginSaveAs();
    SDL_Window* window_ = nullptr;
    bool anyItemActive_ = false;
    void addLineAt(Vec2 a, Vec2 b);
    void addBallAt(Vec2 p);
    void saveState();
    void addEmitterAt(Vec2 p);
    void deleteSelected();
    void copySelection();
    void pasteClipboard();
    void selectOnly(Selection s);
    void toggleSelection(Selection s);
    bool isSelected(SelKind k, int id) const;
    void marqueeApply(Vec2 a, Vec2 b, bool additive);

    void playHitSound(const Hit& hit);


    Project project_;
    World   world_;
    AudioEngine audio_;
    bool    audioReady_ = false;

    Camera    cam_;
    ImVec2    viewCenter_{};
    ImVec2    canvasSize_{1280, 800};
    Selection sel_;
    std::vector<Selection> selSet_;

    struct DragItem { SelKind kind; int id; double ax, ay, bx, by; Vec2 pos; };
    std::vector<DragItem> dragItems_;
    Vec2 dragStartPos_{};
    Vec2 lastMouseWorld_{};
    bool marquee_ = false;
    Vec2 marqueeFrom_{}, marqueeTo_{};
    Project clipboard_;
    Tool      tool_ = Tool::Select;

    bool   paused_ = false;
    bool   inspectorPinned_ = false;  // Ctrl+I：钉住不自动收
    double inspectorReveal_ = 0.0;    // 0=收起 1=展开，用于滑入滑出动画
    int    inspectorPeekFrames_ = 0;  // 选中对象后短暂亮一下
    double time_ = 0.0;
    int    frame_ = 0;
    double accumulator_ = 0.0;
    double fps_ = 60.0;

    double toolbarReveal_ = 0.0;
    std::vector<Ripple> ripples_;
    std::unordered_map<int, std::vector<Vec2>> trails_;

    float flashStrength_ = 0.0f;
    int   flashLineId_ = 0;
    Vec2  flashPoint_{};

    // 拖拽状态
    bool    dragging_ = false;
    bool    draggingBall_ = false;
    bool    draggingLinePoint_ = false;
    int     dragLineId_ = 0;
    int     dragBallId_ = 0;
    bool    dragMoved_ = false;
    Tool    gestureTool_ = Tool::Select;
    bool    gestureShift_ = false;
    bool    gestureAlt_ = false;
    bool    dragEndpointA_ = true;
    bool    draggingBody_ = false;
    Vec2    dragBodyOffset_{};
    Vec2    dragStartWorld_{};
    ImVec2  mouseDownPos_{};

    // 文本编辑
    std::string status_;
    double statusAge_ = 10.0;

    struct UndoEntry {
        std::string json;
        std::vector<Ball> balls;
        bool hasBalls = false;
        unsigned long long seq = 0;
    };
    std::vector<UndoEntry> docStack_;     // A：落到工程的编辑
    std::vector<UndoEntry> ballStack_;    // B：本次暂停内的球操作
    unsigned long long undoSeq_ = 0;
    bool editSave_ = false;
    UiLevel level_ = UiLevel::Advanced;
    lb::Config config_;
    bool startScreen_ = false;
    bool menuOpen_ = false;
    bool confirmOpen_ = false;
    int  confirmAction_ = 0;
    bool quitRequested_ = false;

    struct SceneEntry { std::string path, name, dir, when; };
    bool browserOpen_ = false;
    int  browserSel_ = -1;
    std::vector<SceneEntry> browserEntries_;

    bool saveAsOpen_ = false;
    char saveAsName_[128] = {0};
    bool saveAsFocus_ = false;
    Project   bgProject_;
    World     bgWorld_;
    ImVec4    bgBounds_{0, 0, 0, 0};   // 背景相机：进场时算一次，之后固定
    double    bgAccum_ = 0.0;
    std::vector<std::string> demoCache_;
    bool pendingFit_ = false;
    enum class PendingDialog { None, LoadSf2 };
    PendingDialog pendingDialog_ = PendingDialog::None;
    int pendingLineId_ = 0;
    std::string pendingSnapshot_;
    std::string projectPath_;
    bool        dirty_ = false;
};
