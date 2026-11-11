// 数据模型：工程、线段、球、发射器。
// 每个可调参数都是 Param = 常量 或 表达式。
#pragma once
#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

#include "expr.h"

inline constexpr double kPointRadius = 1e-5;    // 半径 0 的“点球”的碰撞蒙皮（0.01 mm，任何缩放下都看不见）

struct Vec2 {
    double x = 0.0;
    double y = 0.0;
};

inline Vec2 operator+(Vec2 a, Vec2 b) { return {a.x + b.x, a.y + b.y}; }
inline Vec2 operator-(Vec2 a, Vec2 b) { return {a.x - b.x, a.y - b.y}; }
inline Vec2 operator*(Vec2 a, double s) { return {a.x * s, a.y * s}; }
inline double dot(Vec2 a, Vec2 b) { return a.x * b.x + a.y * b.y; }
inline double length(Vec2 a) { return std::sqrt(dot(a, a)); }

// 一个可驱动参数：source 非空时按表达式求值，否则用 constant。
struct Param {
    std::string source;                     // 表达式原文，空 = 常量
    double      constant = 0.0;
    std::string error;                      // 最近一次编译错误

    std::shared_ptr<const Expr> expr;
    mutable std::vector<double> state;
    mutable double lastValue = 0.0;
    mutable std::vector<double> scratch_;   // peek() 的状态副本，避免预览改动持久状态

    bool driven() const { return expr != nullptr; }

    // 编译表达式；失败时保留上一个可用版本，把错误写进 error
    void setSource(const std::string& src);

    // 只读求值：不动 s.* 状态，界面拿来画预览用
    double peek(const double* slots) const {
        if (!expr) return constant;
        const size_t n = size_t(expr->stateCount());
        scratch_.assign(state.begin(), state.begin() + std::min(state.size(), n));
        scratch_.resize(n, 0.0);
        const double v = expr->eval(slots, scratch_.data());
        return std::isfinite(v) ? v : 0.0;   // 和 eval 一样兜住 inf/nan
    }

    double eval(const double* slots) const {
        if (!expr) return constant;
        if (state.size() != size_t(expr->stateCount())) state.assign(size_t(expr->stateCount()), 0.0);
        const double v = expr->eval(slots, state.data());
        lastValue = std::isfinite(v) ? v : 0.0;   // 别把 inf/nan 放进物理
        return lastValue;
    }

    void clearState() const { std::fill(state.begin(), state.end(), 0.0); }
};

struct VoiceRef {
    std::string type    = "sf2";
    std::string file;              // 空 = 用默认音源
    int         bank    = 0;
    int         program = 10;
};

struct Line {
    int   id      = 0;
    Param ax, ay, bx, by;
    bool  collide = true;
    VoiceRef voice;
    Param note, vel, gain;

    // 运行时：本 tick 的端点
    Vec2  a{}, b{};
};

struct Ball {
    int   id     = 0;
    Vec2  pos{};
    Vec2  vel{};
    double radius = 0.0;
    bool  alive   = true;
    int   sourceEmitter = 0;   // 0 = 手放/初始
};

struct Emitter {
    int   id = 0;
    Param px, py, dir, speed, interval, delay;
    Param count;               // -1 = 无限
    double nextTime = 0.0;
    int    spawned  = 0;
    bool   enabled  = true;
};

struct WorldSettings {
    double gravity = -9.81;
    double killY   = -8.0;
    unsigned seed  = 1u;
};

struct Project {
    std::string   name = "untitled";
    WorldSettings world;
    std::vector<Line>    lines;
    std::vector<Ball>    balls;       // 初始球（运行时会被复制）
    std::vector<Emitter> emitters;
    int nextId = 1;

    int  allocId() { return nextId++; }
};

// 一次撞击
struct Hit {
    int   lineId = 0;
    int   ballId = 0;
    Vec2  point{};
    Vec2  normal{};
    double speed = 0.0;
    double time  = 0.0;
};

// 表达式求值用的槽位
struct Slots {
    double v[SLOT_COUNT]{};
    void fillWorld(const WorldSettings& w, double t, double dt, int frame) {
        v[SLOT_T] = t; v[SLOT_DT] = dt; v[SLOT_FRAME] = frame;
        v[SLOT_PI] = 3.14159265358979323846;
        v[SLOT_E] = 2.71828182845904523536;
        v[SLOT_WORLD_GRAVITY] = w.gravity;
        v[SLOT_WORLD_KILLY] = w.killY;
        v[SLOT_WORLD_SEED] = double(w.seed);
        v[SLOT_WORLD_TIME] = t;
    }
    void fillLine(const Line& l, Vec2 a, Vec2 b) {
        v[SLOT_SELF_AX] = a.x; v[SLOT_SELF_AY] = a.y;
        v[SLOT_SELF_BX] = b.x; v[SLOT_SELF_BY] = b.y;
        v[SLOT_SELF_X] = 0.5 * (a.x + b.x);
        v[SLOT_SELF_Y] = 0.5 * (a.y + b.y);
        (void)l;
    }

    void fillLine(const Line& l) {
        v[SLOT_SELF_AX] = l.a.x; v[SLOT_SELF_AY] = l.a.y;
        v[SLOT_SELF_BX] = l.b.x; v[SLOT_SELF_BY] = l.b.y;
        v[SLOT_SELF_X] = 0.5 * (l.a.x + l.b.x);
        v[SLOT_SELF_Y] = 0.5 * (l.a.y + l.b.y);
    }
    void fillBall(const Ball& b) {
        v[SLOT_SELF_X] = b.pos.x; v[SLOT_SELF_Y] = b.pos.y;
        v[SLOT_SELF_VX] = b.vel.x; v[SLOT_SELF_VY] = b.vel.y;
        v[SLOT_SELF_R] = b.radius;
    }
    void fillEmitter(const Emitter& e) {
        v[SLOT_SELF_X] = e.px.constant; v[SLOT_SELF_Y] = e.py.constant;
        v[SLOT_SELF_DIR] = e.dir.constant;
        v[SLOT_SELF_SPEED] = e.speed.constant;
    }
    void fillHit(const Hit& h) {
        v[SLOT_HIT_SPEED] = h.speed;
        v[SLOT_HIT_X] = h.point.x; v[SLOT_HIT_Y] = h.point.y;
        v[SLOT_HIT_NX] = h.normal.x; v[SLOT_HIT_NY] = h.normal.y;
        v[SLOT_HIT_LINE] = double(h.lineId);
        v[SLOT_HIT_BALL] = double(h.ballId);
    }
};

Param pConst(double v);
Param pExpr(const std::string& src, double fallback);
Line  makeDefaultLine(int id, Vec2 a, Vec2 b, const VoiceRef& voice);
