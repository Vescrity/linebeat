// 固定步长的 2D 物理：球只和线段碰撞，线段可以每步被表达式驱动。
#pragma once
#include <unordered_map>
#include <vector>

#include "model.h"

class World {
public:
    static constexpr double kStep = 1.0 / 240.0;
    static constexpr int    kMaxCatchUp = 8;

    void reset(const Project& project, double startTime = 0.0);

    // 结构变了（增删线段/发射器）时调用：保留正在跑的球，只同步缓存与计时器
    void syncStructure();

    // 暂停时参数改了也能立刻看到线段动
    void refreshLines() { syncStructure(); evaluateLines(); }

    void setBalls(std::vector<Ball> balls) { balls_ = std::move(balls); }


    // 推进一个固定步
    void step();

    // 返回新球的 id
    int addBall(Vec2 pos, Vec2 vel, double radius, int emitterId = 0, int forcedId = 0);
    void clearBalls();

    const std::vector<Ball>& balls() const { return balls_; }
    std::vector<Ball>&       balls() { return balls_; }
    const std::vector<Hit>&  hits() const { return hits_; }
    void clearHits() { hits_.clear(); }

    double time() const { return time_; }
    int    frame() const { return frame_; }
    const Project& project() const { return *project_; }

    // 本 tick 解析出的线段端点（表达式已求值）
    const std::vector<Vec2>& lineA() const { return lineA_; }
    const std::vector<Vec2>& lineB() const { return lineB_; }

    void setGravity(double g) { overrideGravity_ = g; hasGravityOverride_ = true; }
    void clearGravityOverride() { hasGravityOverride_ = false; }
    double gravity() const;

private:
    struct SweepBox { double x0, y0, x1, y1; } sweepBox_{};   // 宽相用的扫掠包围盒
    void  evaluateLines();
    void  updateEmitters();

    const Project* project_ = nullptr;
    std::vector<Ball> balls_;
    std::vector<Hit>  hits_;
    std::vector<Vec2> lineA_, lineB_;
    struct EmitterRuntime { double nextTime = 0.0; int spawned = 0; };
    std::unordered_map<int, EmitterRuntime> emitters_;
    unsigned            nextBallId_ = 0;

    double time_ = 0.0;
    int    frame_ = 0;
    double overrideGravity_ = 0.0;
    bool   hasGravityOverride_ = false;
};

// 扫掠：球心从 p0 出发、以速度 v 直线运动，在 [0,maxT] 内最早接触线段 AB 的时刻。
bool sweepCircleSegment(Vec2 p0, Vec2 v, double radius, Vec2 A, Vec2 B, double maxT,
                        double& outT, Vec2& outPoint, Vec2& outNormal);
