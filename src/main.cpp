// 入口：SDL3 + OpenGL + ImGui
#include <SDL3/SDL.h>
#include <GL/gl.h>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include "imgui_impl_sdl3.h"

#include "app.h"

bool writePPM(const char* path, int width, int height, const unsigned char* rgba);
namespace lb {
void loadAppFonts(App& app, ImFont** ui, ImFont** small);
} // namespace lb

namespace {

struct Options {
    std::string shot;
    std::string frames;
    std::string project;
    int         shotDelay = 1400;
    int         winW = 1280;
    int         winH = 800;
    int         frameCount = 80;
    double      frameFps = 30.0;
    bool        noAudio = false;
    bool        demo = false;
    bool        demoBall = false;
    bool        autoTest = false;
    bool        noSave = false;
    bool        vars = false;
    bool        menu = false;
    bool        browser = false;
    bool        saveAs = false;
    bool        mapDemo = false;
    bool        pinPanel = false;
    bool        menuConfirm = false;
    int         level = -1;   // -1 = 用配置，0 = 简洁，1 = 高级
};

Options parseArgs(int argc, char** argv) {
    Options o;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&](const char* d) { return (i + 1 < argc) ? std::string(argv[++i]) : std::string(d); };
        if (a == "--shot") o.shot = next("");
        else if (a == "--frames") o.frames = next("");
        else if (a == "--project") o.project = next("");
        else if (a == "--shot-delay") o.shotDelay = std::atoi(next("1400").c_str());
        else if (a == "--frame-count") o.frameCount = std::atoi(next("80").c_str());
        else if (a == "--frame-fps") o.frameFps = std::atof(next("30").c_str());
        else if (a == "--no-audio") o.noAudio = true;
        else if (a == "--demo") o.demo = true;
        else if (a == "--demo-ball") o.demoBall = true;
        else if (a == "--autotest") o.autoTest = true;
        else if (a == "--no-save") o.noSave = true;
        else if (a == "--simple") o.level = 0;
        else if (a == "--advanced") o.level = 1;
        else if (a == "--vars") o.vars = true;
        else if (a == "--menu") o.menu = true;
        else if (a == "--browser") o.browser = true;
        else if (a == "--saveas") o.saveAs = true;
        else if (a == "--map-demo") o.mapDemo = true;
        else if (a == "--pin-panel") o.pinPanel = true;
        else if (a == "--menu-confirm") o.menuConfirm = true;
        else if (a == "--size") {
            const std::string v = next("1280x800");
            const auto pos = v.find('x');
            if (pos != std::string::npos) {
                o.winW = std::atoi(v.substr(0, pos).c_str());
                o.winH = std::atoi(v.substr(pos + 1).c_str());
            }
        }
    }
    return o;
}

void applyStyle() {
    ImGui::StyleColorsDark();
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding = 14.0f;
    s.ChildRounding = 10.0f;
    s.FrameRounding = 7.0f;
    s.GrabRounding = 6.0f;
    s.PopupRounding = 8.0f;
    s.ScrollbarRounding = 8.0f;
    s.WindowBorderSize = 0.0f;
    s.FrameBorderSize = 0.0f;
    s.WindowPadding = ImVec2(12, 12);
    s.FramePadding = ImVec2(8, 5);
    s.ItemSpacing = ImVec2(8, 7);

    ImVec4* c = s.Colors;
    c[ImGuiCol_Text]             = ImVec4(0.90f, 0.92f, 0.96f, 1.00f);
    c[ImGuiCol_TextDisabled]     = ImVec4(0.45f, 0.49f, 0.56f, 1.00f);
    c[ImGuiCol_WindowBg]         = ImVec4(0.07f, 0.08f, 0.10f, 0.96f);
    c[ImGuiCol_Header]           = ImVec4(0.16f, 0.26f, 0.32f, 1.00f);
    c[ImGuiCol_HeaderHovered]    = ImVec4(0.22f, 0.38f, 0.47f, 1.00f);
    c[ImGuiCol_HeaderActive]     = ImVec4(0.26f, 0.46f, 0.56f, 1.00f);
    c[ImGuiCol_TableHeaderBg]    = ImVec4(0.10f, 0.12f, 0.15f, 1.00f);
    c[ImGuiCol_TitleBg]          = ImVec4(0.09f, 0.11f, 0.14f, 1.00f);
    c[ImGuiCol_TitleBgActive]    = ImVec4(0.12f, 0.15f, 0.19f, 1.00f);
    c[ImGuiCol_TitleBgCollapsed] = ImVec4(0.09f, 0.11f, 0.14f, 1.00f);
    c[ImGuiCol_FrameBg]          = ImVec4(0.10f, 0.11f, 0.14f, 1.00f);
    c[ImGuiCol_FrameBgHovered]   = ImVec4(0.14f, 0.16f, 0.20f, 1.00f);
    c[ImGuiCol_FrameBgActive]    = ImVec4(0.16f, 0.19f, 0.23f, 1.00f);
    c[ImGuiCol_Button]           = ImVec4(0.13f, 0.15f, 0.19f, 1.00f);
    c[ImGuiCol_ButtonHovered]    = ImVec4(0.20f, 0.24f, 0.30f, 1.00f);
    c[ImGuiCol_ButtonActive]     = ImVec4(0.26f, 0.32f, 0.40f, 1.00f);
    c[ImGuiCol_SliderGrab]       = ImVec4(0.49f, 0.89f, 1.00f, 1.00f);
    c[ImGuiCol_SliderGrabActive] = ImVec4(0.70f, 0.95f, 1.00f, 1.00f);
    c[ImGuiCol_CheckMark]        = ImVec4(0.49f, 0.89f, 1.00f, 1.00f);
    c[ImGuiCol_Separator]        = ImVec4(1.00f, 1.00f, 1.00f, 0.09f);
    c[ImGuiCol_ScrollbarBg]      = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    c[ImGuiCol_ScrollbarGrab]    = ImVec4(1.00f, 1.00f, 1.00f, 0.14f);
    c[ImGuiCol_PopupBg]          = ImVec4(0.08f, 0.09f, 0.12f, 0.98f);
}

} // namespace

int main(int argc, char** argv) {
    const Options opt = parseArgs(argc, argv);

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)) {
        std::fprintf(stderr, "SDL_Init 失败: %s\n", SDL_GetError());
        return 1;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 0);

    SDL_Window* window = SDL_CreateWindow("弹线 Linebeat", opt.winW, opt.winH,
                                          SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE |
                                          SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!window) {
        std::fprintf(stderr, "创建窗口失败: %s\n", SDL_GetError());
        return 1;
    }

    SDL_GLContext gl = SDL_GL_CreateContext(window);
    if (!gl) {
        std::fprintf(stderr, "创建 GL 上下文失败: %s\n", SDL_GetError());
        return 1;
    }
    SDL_GL_MakeCurrent(window, gl);
    SDL_GL_SetSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    applyStyle();

    ImGui_ImplSDL3_InitForOpenGL(window, gl);
    ImGui_ImplOpenGL3_Init("#version 330 core");

    App app;
    ImFont* fontUI = nullptr;
    ImFont* fontSmall = nullptr;
    lb::loadAppFonts(app, &fontUI, &fontSmall);
    (void)fontSmall;
    ImGui_ImplOpenGL3_DestroyDeviceObjects();   // 让新字体生效

    if (!app.init(window, opt.project)) {
        std::fprintf(stderr, "初始化失败\n");
        return 1;
    }

    if (opt.level >= 0) app.setLevel(opt.level == 1 ? App::UiLevel::Advanced : App::UiLevel::Simple);
    if (opt.vars) app.setShowVars(true);
    if (opt.browser) app.openSceneBrowser();
    if (opt.saveAs) app.debugBeginSaveAs();
    if (opt.mapDemo) app.debugApplyFirstLineMapping();
    if (opt.pinPanel) app.debugPinInspector();
    if (opt.menu || opt.menuConfirm) {
        app.debugMarkDirty();
        app.debugOpenMenu(opt.menuConfirm ? 2 : -1);
    }
    if (opt.demo) app.debugSelectFirstLine();
    if (opt.demoBall) { app.debugSelectBall(); app.togglePause(); }

    const bool shotMode = !opt.shot.empty();
    const bool frameMode = !opt.frames.empty();

    using clock = std::chrono::steady_clock;
    auto last = clock::now();
    auto start = last;
    bool running = true;
    int frameIndex = 0;

    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            ImGui_ImplSDL3_ProcessEvent(&e);
            app.handleEvent(e);
            if (e.type == SDL_EVENT_QUIT) {
                if (app.requestQuit()) running = false;
            }
            if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_ESCAPE && e.key.mod & SDL_KMOD_CTRL)
                running = false;
        }
        if (app.quitRequested()) running = false;
        if (!running) break;

        if (std::getenv("LB_UITEST")) {
            const SDL_WindowID wid = SDL_GetWindowID(window);
            auto key = [&](SDL_Keycode k, SDL_Keymod m) {
                SDL_Event e{};
                e.type = SDL_EVENT_KEY_DOWN;
                e.key.windowID = wid;
                e.key.key = k;
                e.key.mod = m;
                SDL_PushEvent(&e);
                SDL_Event u = e;
                u.type = SDL_EVENT_KEY_UP;
                SDL_PushEvent(&u);
            };
            auto click = [&](float x, float y) {
                SDL_Event m{};
                m.type = SDL_EVENT_MOUSE_MOTION;
                m.motion.windowID = wid;
                m.motion.x = x;
                m.motion.y = y;
                SDL_PushEvent(&m);
                SDL_Event d{};
                d.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
                d.button.windowID = wid;
                d.button.x = x;
                d.button.y = y;
                d.button.button = SDL_BUTTON_LEFT;
                d.button.down = true;
                SDL_PushEvent(&d);
                SDL_Event u{};
                u.type = SDL_EVENT_MOUSE_BUTTON_UP;
                u.button.windowID = wid;
                u.button.x = x;
                u.button.y = y;
                u.button.button = SDL_BUTTON_LEFT;
                SDL_PushEvent(&u);
            };
            auto dump = [&](const char* tag) {
                std::fprintf(stderr, "[uitest] %-22s balls=%d lines=%d doc=%d ballstk=%d paused=%d edit=%d\n",
                             tag, int(app.world().balls().size()), int(app.project().lines.size()),
                             app.docUndoDepth(), app.ballUndoDepth(), int(app.paused()), int(app.editSave()));
            };
            if (const char* hv = std::getenv("LB_HOVER")) {
                const std::string mode(hv);
                static float mx = 600, my = 400;
                switch (frameIndex) {
                case 5:  mx = (mode == "right" || mode == "panel") ? float(opt.winW) - 8.0f : 20.0f;
                         my = 400; break;
                case 40: mx = (mode == "right") ? float(opt.winW) - 8.0f
                          : (mode == "panel") ? float(opt.winW) - 150.0f : 600.0f;
                         my = 400; break;
                case 75: if (mode == "panel") { mx = 600; my = 400; } break;
                default: break;
                }
                if (frameIndex == 5 || frameIndex == 40 || frameIndex == 20 || frameIndex == 60 ||
                    frameIndex == 75) {
                    SDL_Event m{};
                    m.type = SDL_EVENT_MOUSE_MOTION;
                    m.motion.windowID = wid;
                    m.motion.x = mx; m.motion.y = my;
                    SDL_PushEvent(&m);
                }
                if (frameIndex == 20 || frameIndex == 60)
                    std::fprintf(stderr, "[hover] frame=%d reveal=%.3f\n", frameIndex, app.toolbarReveal());
                if (mode == "right" && frameIndex == 58)
                    std::fprintf(stderr, "[hover] inspector reveal=%.3f\n", app.inspectorReveal());
                if (mode == "panel" && frameIndex == 60)
                    std::fprintf(stderr, "[hover] 停在面板里 reveal=%.3f\n", app.inspectorReveal());
                if (mode == "panel" && frameIndex == 95)
                    std::fprintf(stderr, "[hover] 移开后 reveal=%.3f\n", app.inspectorReveal());
                if (frameIndex == (mode == "panel" ? 110 : 62)) std::exit(0);
            } else if (std::getenv("LB_STARTCLICK")) {
                if (frameIndex == 20) click(opt.winW / 2, int(opt.winH * 0.42f));
                if (frameIndex == 50) {
                    dump("after start click");
                    std::fprintf(stderr, "[uitest] startScreen=%d\n", int(app.startScreen()));
                    std::exit(0);
                }
            } else
            switch (frameIndex) {
            case 8:  dump("start"); key(SDLK_3, SDL_KMOD_NONE); break;
            case 12: click(400, 300); break;
            case 16: dump("placed while running"); break;
            case 20: key(SDLK_Z, SDL_KMOD_CTRL); break;
            case 24: dump("after ctrl+z (noop)"); break;
            case 28: key(SDLK_SPACE, SDL_KMOD_NONE); break;
            case 32: click(420, 320); break;
            case 36: dump("placed while paused"); break;
            case 40: key(SDLK_Z, SDL_KMOD_CTRL); break;
            case 44: dump("undo paused ball"); break;
            case 48: key(SDLK_E, SDL_KMOD_CTRL); break;
            case 52: dump("enter edit-save"); break;
            case 56: click(440, 340); break;
            case 60: dump("placed in edit-save"); break;
            case 64: key(SDLK_Z, SDL_KMOD_CTRL); break;
            case 68: dump("undo edit-save ball"); break;
            case 70: app.debugMoveBall(10, 0.0, 0.7); app.debugSetBallVel(10, 1.5, -2.5); break;
            case 72: std::fprintf(stderr, "[uitest] ball y=%.3f (moved +0.7 in edit-save)\n", app.debugBallY(10)); break;
            case 74: key(SDLK_SPACE, SDL_KMOD_NONE); break;   // 继续（空格）
            case 76: dump("resumed"); break;
            case 78: key(SDLK_E, SDL_KMOD_CTRL); break;
            case 82: std::fprintf(stderr, "[uitest] after re-enter y=%.3f vy=%.2f (expect 3.700 / -2.50)\n", app.debugBallY(10), app.debugBallVy(10)); break;
            case 128: std::fprintf(stderr, "[uitest] pre-esc start=%d menu=%d\n", int(app.startScreen()), int(app.menuOpen())); break;
            case 130: key(SDLK_ESCAPE, SDL_KMOD_NONE); break;
            case 131: std::fprintf(stderr, "[uitest] post-esc menu=%d\n", int(app.menuOpen())); break;
            case 132: std::fprintf(stderr, "[uitest] menu open=%d\n", int(app.menuOpen())); break;
            case 133: {
                SDL_Event m{};
                m.type = SDL_EVENT_MOUSE_MOTION;
                m.motion.windowID = wid;
                m.motion.x = opt.winW / 2; m.motion.y = int(opt.winH * 0.5f) - 63;
                SDL_PushEvent(&m);
                break;
            }
            case 134: {
                SDL_Event d{};
                d.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
                d.button.windowID = wid; d.button.x = opt.winW / 2; d.button.y = int(opt.winH * 0.5f) - 63;
                d.button.button = SDL_BUTTON_LEFT; d.button.down = true;
                SDL_PushEvent(&d);
                break;
            }
            case 137: {
                SDL_Event u{};
                u.type = SDL_EVENT_MOUSE_BUTTON_UP;
                u.button.windowID = wid; u.button.x = opt.winW / 2; u.button.y = int(opt.winH * 0.5f) - 63;
                u.button.button = SDL_BUTTON_LEFT;
                SDL_PushEvent(&u);
                break;
            }
            case 139: std::fprintf(stderr, "[uitest] menu closed=%d\n", int(!app.menuOpen())); break;
            case 86: key(SDLK_1, SDL_KMOD_NONE); break;
            case 90: {   // 在空白处拖一个框选
                SDL_Event m{};
                m.type = SDL_EVENT_MOUSE_MOTION;
                m.motion.windowID = wid;
                m.motion.x = 120; m.motion.y = 120;
                SDL_PushEvent(&m);
                SDL_Event d{};
                d.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
                d.button.windowID = wid; d.button.x = 120; d.button.y = 120;
                d.button.button = SDL_BUTTON_LEFT; d.button.down = true;
                SDL_PushEvent(&d);
                break;
            }
            case 94: {
                SDL_Event m{};
                m.type = SDL_EVENT_MOUSE_MOTION;
                m.motion.windowID = wid;
                m.motion.x = 900; m.motion.y = 700;
                SDL_PushEvent(&m);
                break;
            }
            case 98: {
                SDL_Event u{};
                u.type = SDL_EVENT_MOUSE_BUTTON_UP;
                u.button.windowID = wid; u.button.x = 900; u.button.y = 700;
                u.button.button = SDL_BUTTON_LEFT;
                SDL_PushEvent(&u);
                break;
            }
            case 102: std::fprintf(stderr, "[uitest] selected=%zu\n", app.selectionCount()); break;
            case 106: key(SDLK_C, SDL_KMOD_CTRL); break;
            case 110: key(SDLK_V, SDL_KMOD_CTRL); break;
            case 114:
                std::fprintf(stderr, "[uitest] after paste balls=%d lines=%d selected=%zu\n",
                             int(app.world().balls().size()), int(app.project().lines.size()),
                             app.selectionCount());
                break;
            case 158: app.debugBeginSaveAs(); app.debugSetSaveAsName("zz_short"); break;
            case 160: key(SDLK_RETURN, SDL_KMOD_NONE); break;
            case 162: std::fprintf(stderr, "[uitest] saveas done\n"); break;
            case 11: key(SDLK_EQUALS, SDL_KMOD_CTRL); break;
            case 13: std::fprintf(stderr, "[uitest] 开始界面缩放=%.2f\n", app.uiScale()); break;
            case 15: key(SDLK_0, SDL_KMOD_CTRL); break;
            case 135: key(SDLK_EQUALS, SDL_KMOD_CTRL); break;
            case 136: std::fprintf(stderr, "[uitest] 菜单开着缩放=%.2f\n", app.uiScale()); break;
            case 138: key(SDLK_0, SDL_KMOD_CTRL); break;
            case 101: key(SDLK_EQUALS, SDL_KMOD_CTRL); break;
            case 103: std::fprintf(stderr, "[uitest] 编辑器缩放=%.2f\n", app.uiScale()); break;
            case 107: key(SDLK_0, SDL_KMOD_CTRL); break;
            case 109: std::fprintf(stderr, "[uitest] 复位=%.2f\n", app.uiScale()); break;
            case 148: app.openSceneBrowser(); break;
            case 150: std::fprintf(stderr, "[uitest] browser open=%d\n", int(app.sceneBrowserOpen())); break;
            case 152: key(SDLK_RETURN, SDL_KMOD_NONE); break;
            case 154: std::fprintf(stderr, "[uitest] after enter: browser=%d name=%s\n",
                                   int(app.sceneBrowserOpen()), app.project().name.c_str()); break;
            case 170:
                if (!std::getenv("LB_HOVER")) std::exit(0);
                break;
            case 100: std::fprintf(stderr, "[uitest] reveal before ctrl+i=%.3f\n", app.inspectorReveal()); break;
            case 125: std::fprintf(stderr, "[uitest] reveal after ctrl+i=%.3f\n", app.inspectorReveal()); break;
            case 105: key(SDLK_I, SDL_KMOD_CTRL); break;
            case 210: std::fprintf(stderr, "[uitest] reveal after ctrl+i=%.3f\n", app.inspectorReveal()); break;
            case 20000: std::exit(0); break;
            default: break;
            }
        }

        if (opt.autoTest) {
            auto pushKey = [](SDL_Keycode k) {
                SDL_Event ev{};
                ev.type = SDL_EVENT_KEY_DOWN;
                ev.key.key = k;
                ev.key.down = true;
                SDL_PushEvent(&ev);
            };
            auto pushMouse = [window](Uint32 type, float x, float y) {
                SDL_Event ev{};
                ev.type = type;
                const SDL_WindowID wid = SDL_GetWindowID(window);
                if (type == SDL_EVENT_MOUSE_MOTION) {
                    ev.motion.windowID = wid;
                    ev.motion.x = x;
                    ev.motion.y = y;
                } else {
                    ev.button.windowID = wid;
                    ev.button.x = x;
                    ev.button.y = y;
                    ev.button.button = SDL_BUTTON_LEFT;
                    ev.button.down = (type == SDL_EVENT_MOUSE_BUTTON_DOWN);
                }
                SDL_PushEvent(&ev);
            };
            switch (frameIndex) {
            case 20: pushKey(SDLK_3); pushMouse(SDL_EVENT_MOUSE_MOTION, 300, 250); break;
            case 22: pushMouse(SDL_EVENT_MOUSE_BUTTON_DOWN, 300, 250); break;
            case 23: pushMouse(SDL_EVENT_MOUSE_BUTTON_UP, 300, 250); break;
            case 26: pushMouse(SDL_EVENT_MOUSE_MOTION, 700, 250); break;
            case 28: pushMouse(SDL_EVENT_MOUSE_BUTTON_DOWN, 700, 250); break;
            case 29: pushMouse(SDL_EVENT_MOUSE_BUTTON_UP, 700, 250); break;
            case 34: pushKey(SDLK_5); pushMouse(SDL_EVENT_MOUSE_MOTION, 300, 511); break;
            case 36: pushMouse(SDL_EVENT_MOUSE_BUTTON_DOWN, 300, 511); break;
            case 37: pushMouse(SDL_EVENT_MOUSE_BUTTON_UP, 300, 511); break;
            case 44: {
                SDL_Event ev{};
                ev.type = SDL_EVENT_KEY_DOWN;
                ev.key.windowID = SDL_GetWindowID(window);
                ev.key.key = SDLK_Z;
                ev.key.mod = SDL_KMOD_CTRL;
                SDL_PushEvent(&ev);
                break;
            }
            case 45: {
                SDL_Event ev{};
                ev.type = SDL_EVENT_KEY_UP;
                ev.key.windowID = SDL_GetWindowID(window);
                ev.key.key = SDLK_Z;
                SDL_PushEvent(&ev);
                break;
            }
            default: break;
            }
        }

        const auto now = clock::now();
        double dt = std::chrono::duration<double>(now - last).count();
        last = now;
        if (dt > 0.1) dt = 0.1;
        if (frameMode) dt = 1.0 / opt.frameFps;

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        int drawW = opt.winW, drawH = opt.winH;
        SDL_GetWindowSizeInPixels(window, &drawW, &drawH);
        int winW = 0, winH = 0;
        SDL_GetWindowSize(window, &winW, &winH);
        io.DisplayFramebufferScale = ImVec2(float(drawW) / float(std::max(winW, 1)),
                                            float(drawH) / float(std::max(winH, 1)));

        app.update(frameMode ? 1.0 / opt.frameFps : dt);
        app.draw(ImVec2(0, 0), io.DisplaySize);

        ImGui::Render();
        glViewport(0, 0, drawW, drawH);
        glClearColor(0.03f, 0.03f, 0.045f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        if (frameMode) {
            std::vector<unsigned char> pixels(size_t(drawW) * size_t(drawH) * 4);
            glPixelStorei(GL_PACK_ALIGNMENT, 1);
            glReadPixels(0, 0, drawW, drawH, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());

            char name[512];
            std::snprintf(name, sizeof(name), "%s/f%04d.ppm", opt.frames.c_str(), frameIndex);
            // glReadPixels 是上下颠倒的，写之前翻转
            std::vector<unsigned char> flipped(pixels.size());
            for (int y = 0; y < drawH; ++y) {
                std::memcpy(&flipped[size_t(y) * size_t(drawW) * 4],
                            &pixels[size_t(drawH - 1 - y) * size_t(drawW) * 4],
                            size_t(drawW) * 4);
            }
            writePPM(name, drawW, drawH, flipped.data());

            SDL_GL_SwapWindow(window);
            ++frameIndex;
            if (frameIndex >= opt.frameCount) break;
            continue;
        }

        if (shotMode) {
            const double elapsed = std::chrono::duration<double>(clock::now() - start).count() * 1000.0;
            if (elapsed > opt.shotDelay) {
                std::vector<unsigned char> pixels(size_t(drawW) * size_t(drawH) * 4);
                glPixelStorei(GL_PACK_ALIGNMENT, 1);
                glReadPixels(0, 0, drawW, drawH, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
                std::vector<unsigned char> flipped(pixels.size());
                for (int y = 0; y < drawH; ++y) {
                    std::memcpy(&flipped[size_t(y) * size_t(drawW) * 4],
                                &pixels[size_t(drawH - 1 - y) * size_t(drawW) * 4],
                                size_t(drawW) * 4);
                }
                const bool ok = writePPM(opt.shot.c_str(), drawW, drawH, flipped.data());
                std::fprintf(stderr, "[shot] %s -> %s\n", opt.shot.c_str(), ok ? "ok" : "fail");
                SDL_GL_SwapWindow(window);   // 读完之后再交换
                break;
            }
        }

        SDL_GL_SwapWindow(window);
        ++frameIndex;
    }

    app.shutdown();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    SDL_GL_DestroyContext(gl);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
