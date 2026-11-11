#include "physics.h"

#include <algorithm>
#include <cmath>

namespace {

inline Vec2 perp(Vec2 v) { return {-v.y, v.x}; }

} // namespace

bool sweepCircleSegment(Vec2 p0, Vec2 v, double radius, Vec2 A, Vec2 B, double maxT,
                        double& outT, Vec2& outPoint, Vec2& outNormal) {
    const Vec2 d = B - A;
    const double len2 = dot(d, d);
    const double len = std::sqrt(len2);

    double best = maxT + 1e-12;
    bool found = false;

    auto consider = [&](double t, Vec2 p, Vec2 n) {
        if (!(t >= 0.0) || t > maxT + 1e-12) return;   // 负数不再夹成 0：那会把“正在离开”当成撞墙
        if (t < best) { best = t; outPoint = p; outNormal = n; found = true; }
    };

    if (len2 > 1e-18) {
        const Vec2 u{d.x / len, d.y / len};
        const Vec2 n0 = perp(u);
        const double dist = dot(p0 - A, n0);
        const double vn0 = dot(v, n0);

        // 正好在线上时按速度方向选边，避免推错侧
        double side;
        if (dist > 1e-12) side = 1.0;
        else if (dist < -1e-12) side = -1.0;
        else side = vn0 > 0.0 ? -1.0 : 1.0;

        const double nd = dist * side;      // 球心离线段的距离（自己那一侧为正）
        const double vd = vn0 * side;       // <0 表示还在靠近
        const double s = dot(p0 - A, u);

        if (nd < radius - 1e-12) {
            // 已经贴住/嵌进去了：立刻推出（穿透深度由调用方一次推干净）
            if (s > -1e-9 && s < len + 1e-9) consider(0.0, p0, n0 * side);
        } else if (vd < -1e-12) {
            const double t = (radius - nd) / vd;      // nd>r 且 vd<0 ⇒ t>0
            if (t >= 0.0) {
                const Vec2 p = p0 + v * t;
                const double sp = dot(p - A, u);
                if (sp >= -1e-9 && sp <= len + 1e-9) consider(t, p, n0 * side);
            }
        }
    }

    // 端点圆：退化成点的线段也走这条
    const Vec2 caps[2] = {A, B};
    const int capCount = (len2 > 1e-18) ? 2 : 1;
    for (int ci = 0; ci < capCount; ++ci) {
        const Vec2 C = caps[ci];
        const Vec2 oc = p0 - C;
        const double c = dot(oc, oc) - radius * radius;

        if (c < 0.0) {                       // 一开始就压在端帽里
            Vec2 n = oc;
            const double nl = length(n);
            n = nl > 1e-12 ? n * (1.0 / nl) : Vec2{0.0, -1.0};
            consider(0.0, p0, n);
            continue;
        }
        const double a = dot(v, v);
        if (a < 1e-14) continue;
        const double b = 2.0 * dot(oc, v);
        const double disc = b * b - 4.0 * a * c;
        if (disc < 0.0) continue;
        const double t = (-b - std::sqrt(disc)) / (2.0 * a);
        if (!(t >= 0.0) || t > maxT + 1e-12) continue;
        const Vec2 p = p0 + v * t;
        Vec2 n = p - C;
        const double nl = length(n);
        if (nl < 1e-12) continue;
        consider(t, p, n * (1.0 / nl));
    }

    if (!found) return false;
    outT = best;
    return true;
}

void World::reset(const Project& project, double startTime) {
    project_ = &project;
    nextBallId_ = 0;
    clearGravityOverride();
    balls_ = project.balls;
    for (auto& b : balls_) b.alive = true;
    hits_.clear();
    time_ = startTime;
    frame_ = 0;
    lineA_.assign(project.lines.size(), Vec2{});
    lineB_.assign(project.lines.size(), Vec2{});

    Slots s;
    s.fillWorld(project.world, time_, 0.0, 0);
    emitters_.clear();
    for (const auto& e : project.emitters) {
        s.fillEmitter(e);
        emitters_[e.id] = EmitterRuntime{e.delay.eval(s.v), 0};
    }
    evaluateLines();
}

void World::syncStructure() {
    if (!project_) return;
    if (lineA_.size() != project_->lines.size()) {
        lineA_.resize(project_->lines.size());
        lineB_.resize(project_->lines.size());
        evaluateLines();
    }
    for (const auto& e : project_->emitters) {
        if (!emitters_.count(e.id)) {
            Slots s2;
            s2.fillWorld(project_->world, time_, kStep, frame_);
            s2.fillEmitter(e);
            emitters_[e.id] = EmitterRuntime{time_ + std::max(0.0, e.delay.eval(s2.v)), 0};
        }
    }
    for (auto it = emitters_.begin(); it != emitters_.end();) {
        bool alive = false;
        for (const auto& e : project_->emitters) if (e.id == it->first) { alive = true; break; }
        it = alive ? std::next(it) : emitters_.erase(it);
    }
}

void World::clearBalls() { balls_.clear(); }

int World::addBall(Vec2 pos, Vec2 vel, double radius, int emitterId, int forcedId) {
    Ball b;
    if (forcedId != 0) {
        b.id = forcedId;
    } else {
        b.id = 100000 + int(nextBallId_++);
        // 载入的存档里可能已经有同号的球，跳过去
        while (std::any_of(balls_.begin(), balls_.end(), [&](const Ball& x) { return x.id == b.id; }))
            b.id = 100000 + int(nextBallId_++);
    }
    b.pos = pos;
    b.vel = vel;
    b.radius = radius;
    b.alive = true;
    b.sourceEmitter = emitterId;
    balls_.push_back(b);
    return b.id;
}

void World::evaluateLines() {
    if (!project_) return;
    Slots s;
    s.fillWorld(project_->world, time_, kStep, frame_);
    for (size_t i = 0; i < project_->lines.size(); ++i) {
        const Line& l = project_->lines[i];
        s.v[SLOT_IDX] = double(i);
        s.fillLine(l, lineA_[i], lineB_[i]);
        lineA_[i] = Vec2{l.ax.eval(s.v), l.ay.eval(s.v)};
        lineB_[i] = Vec2{l.bx.eval(s.v), l.by.eval(s.v)};
    }
}

void World::updateEmitters() {
    if (!project_) return;
    Slots s;
    s.fillWorld(project_->world, time_, kStep, frame_);

    for (size_t i = 0; i < project_->emitters.size(); ++i) {
        const Emitter& e = project_->emitters[i];
        if (!e.enabled) continue;
        s.v[SLOT_IDX] = double(i);
        s.fillEmitter(e);

        EmitterRuntime& rt = emitters_[e.id];
        double countV = e.count.eval(s.v);
        if (!std::isfinite(countV)) countV = 0.0;
        const int maxCount = countV < 0.0 ? -1
                            : (countV > 1e9 ? -1 : int(std::lround(countV)));
        if (maxCount >= 0 && rt.spawned >= maxCount) continue;
        if (time_ + 1e-9 < rt.nextTime) continue;

        const double px = e.px.eval(s.v);
        const double py = e.py.eval(s.v);
        const double dir = e.dir.eval(s.v);
        const double spd = e.speed.eval(s.v);
        addBall(Vec2{px, py}, Vec2{std::cos(dir) * spd, std::sin(dir) * spd}, 0.0, e.id);

        ++rt.spawned;
        double iv = e.interval.eval(s.v);
        if (iv < 0.002) iv = 0.002;
        rt.nextTime = (rt.nextTime <= time_) ? time_ + iv : rt.nextTime + iv;
    }
}

double World::gravity() const {
    if (hasGravityOverride_) return overrideGravity_;
    return project_ ? project_->world.gravity : -9.81;
}

void World::step() {
    if (!project_) return;

    const double h = kStep;

    syncStructure();
    updateEmitters();      // 先在步首发射：新球这一步就正常运动，不会被多算一步

    time_ += h;
    ++frame_;
    hits_.clear();

    evaluateLines();

    const double g = gravity();
    const Vec2 gravity{0.0, g};

    for (Ball& ball : balls_) {
        if (!ball.alive) continue;

        const double r = ball.radius > 1e-6 ? ball.radius : kPointRadius;
        double remaining = h;
        int skipLine = -1;   // 本步内已处理的 t≈0 接触，别在同一处反复求交
        Vec2 restN{};        // 本步贴住的面法线：剩下的时间只许沿切向走
        bool hasRest = false;

        for (int iter = 0; iter < 16; ++iter) {
            if (remaining <= 1e-12) break;

            // 抛物线的精确推进：位置 p + v t + ½gt²，速度 v + gt。
            // 求交时用区间平均速度做直线扫掠，误差是二阶的。
            const Vec2 vAvg = ball.vel + gravity * (0.5 * remaining);

            // 宽相：这段剩余时间里球可能扫过的矩形（弦 + 抛物线相对弦的下垂 + 半径）。
            // 线段包围盒跟它不相交就直接跳过，不用做精确扫掠；纯排除，结果不变。
            {
                const Vec2 p1 = ball.pos + vAvg * remaining;
                const double sag = std::abs(g) * remaining * remaining / 8.0;
                sweepBox_ = {std::min(ball.pos.x, p1.x) - r,
                             std::min(ball.pos.y, p1.y) - r - sag,
                             std::max(ball.pos.x, p1.x) + r,
                             std::max(ball.pos.y, p1.y) + r + sag};
            }

            double bestT = remaining + 1e-12;
            int    bestLine = -1;
            Vec2   bestP{}, bestN{};

            for (size_t li = 0; li < lineA_.size() && li < project_->lines.size(); ++li) {
                if (!project_->lines[li].collide) continue;
                if (int(li) == skipLine) continue;
                {
                    const Vec2 a = lineA_[li], b = lineB_[li];
                    if (std::max(a.x, b.x) < sweepBox_.x0 || std::min(a.x, b.x) > sweepBox_.x1 ||
                        std::max(a.y, b.y) < sweepBox_.y0 || std::min(a.y, b.y) > sweepBox_.y1)
                        continue;
                }
                double ct = 0.0;
                Vec2 cp{}, cn{};
                if (!sweepCircleSegment(ball.pos, vAvg, r, lineA_[li], lineB_[li], remaining,
                                        ct, cp, cn))
                    continue;
                if (ct < bestT) {
                    bestT = ct;
                    bestLine = int(li);
                    bestP = cp;
                    bestN = cn;
                }
            }

            if (bestLine < 0) {
                ball.pos = ball.pos + ball.vel * remaining + gravity * (0.5 * remaining * remaining);
                ball.vel = ball.vel + gravity * remaining;
                remaining = 0.0;
                break;
            }

            const double t = bestT;
            if (t <= 1e-9) skipLine = bestLine;   // 贴着的那条线，这一步不再重复处理
            ball.pos = ball.pos + ball.vel * t + gravity * (0.5 * t * t);
            ball.vel = ball.vel + gravity * t;
            remaining -= t;

            const Vec2 A = lineA_[size_t(bestLine)];
            const Vec2 B = lineB_[size_t(bestLine)];

            const double vn = dot(ball.vel, bestN);
            if (vn >= 0.0 && t <= 1e-9) { restN = bestN; hasRest = true; }
            if (vn < 0.0) {
                if (-vn > 0.12) {
                    // 真正的撞击：完全弹性反射，记一次事件
                    ball.vel = ball.vel - bestN * (2.0 * vn);
                    Hit hit;
                    hit.lineId = project_->lines[size_t(bestLine)].id;
                    hit.ballId = ball.id;
                    hit.point = bestP;
                    hit.normal = bestN;
                    hit.speed = std::fabs(vn);
                    hit.time = time_ - remaining;
                    hits_.push_back(hit);
                } else {
                    // 轻微接触：去掉法向速度让它停住，否则每秒抖动上百次
                    ball.vel = ball.vel - bestN * vn;
                    restN = bestN;      // 贴住了：剩下的位移不能再往面里走
                    hasRest = true;
                }
            }

            // 一次性推出到表面，别每次只挪 1e-7（那会让球卡在线上）
            {
                Vec2 dp = B - A;
                const double l2 = dot(dp, dp);
                double sp = l2 > 1e-18 ? dot(ball.pos - A, dp) / l2 : 0.0;
                sp = std::clamp(sp, 0.0, 1.0);
                const Vec2 closest = A + dp * sp;
                Vec2 off = ball.pos - closest;
                const double dist = length(off);
                if (dist < r) {
                    const Vec2 n = dist > 1e-12 ? off * (1.0 / dist) : bestN;
                    ball.pos = closest + n * (r + 1e-12);
                }
            }
        }

        // 迭代次数用完了还有时间，就按自由飞行走完，别把它丢掉。
        // 但贴着面的时候只允许切向位移，否则这一点点自由落体就是“往线里陷”的来源
        if (remaining > 1e-12) {
            Vec2 dp = ball.vel * remaining + gravity * (0.5 * remaining * remaining);
            Vec2 dv = gravity * remaining;
            if (hasRest) {
                const double dn = dot(dp, restN);
                if (dn < 0.0) dp = dp - restN * dn;
            }
            ball.pos = ball.pos + dp;
            ball.vel = ball.vel + dv;
            if (hasRest) {
                const double vn2 = dot(ball.vel, restN);
                if (vn2 < 0.0) ball.vel = ball.vel - restN * vn2;
            }
        }
    }

    // 销毁线
    const double killY = project_->world.killY;
    balls_.erase(std::remove_if(balls_.begin(), balls_.end(),
                                [killY](const Ball& b) { return !b.alive || b.pos.y < killY; }),
                 balls_.end());
}
