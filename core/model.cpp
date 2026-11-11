#include "model.h"

void Param::setSource(const std::string& src) {
    source = src;
    if (src.empty()) {
        expr.reset();
        state.clear();
        error.clear();
        return;
    }
    std::string err;
    auto compiled = Expr::compile(src, &err);
    if (compiled) {
        expr = compiled;
        state.assign(size_t(compiled->stateCount()), 0.0);
        error.clear();
    } else {
        error = err;   // 保留上一版可用的 expr，不打断正在跑的东西
    }
}

Param pConst(double v) {
    Param p;
    p.constant = v;
    return p;
}

Param pExpr(const std::string& src, double fallback) {
    Param p;
    p.constant = fallback;
    p.setSource(src);
    return p;
}

Line makeDefaultLine(int id, Vec2 a, Vec2 b, const VoiceRef& voice) {
    Line l;
    l.id = id;
    l.ax = pConst(a.x);
    l.ay = pConst(a.y);
    l.bx = pConst(b.x);
    l.by = pConst(b.y);
    l.a = a;
    l.b = b;
    l.voice = voice;
    l.note = pConst(72);
    l.vel = pConst(90);
    l.gain = pConst(1.0);
    return l;
}
