#include "expr.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string_view>

namespace {

// 变量表

struct SlotEntry { const char* name; int slot; };

const SlotEntry kSlots[] = {
    {"t", SLOT_T}, {"dt", SLOT_DT}, {"frame", SLOT_FRAME},
    {"pi", SLOT_PI}, {"e", SLOT_E}, {"i", SLOT_IDX},
    {"self.x", SLOT_SELF_X}, {"self.y", SLOT_SELF_Y},
    {"self.vx", SLOT_SELF_VX}, {"self.vy", SLOT_SELF_VY},
    {"self.r", SLOT_SELF_R},
    {"self.a.x", SLOT_SELF_AX}, {"self.a.y", SLOT_SELF_AY},
    {"self.b.x", SLOT_SELF_BX}, {"self.b.y", SLOT_SELF_BY},
    {"self.dir", SLOT_SELF_DIR}, {"self.speed", SLOT_SELF_SPEED},
    {"world.gravity", SLOT_WORLD_GRAVITY},
    {"world.kill_y", SLOT_WORLD_KILLY},
    {"world.seed", SLOT_WORLD_SEED},
    {"world.time", SLOT_WORLD_TIME},
    {"hit.speed", SLOT_HIT_SPEED}, {"hit.x", SLOT_HIT_X}, {"hit.y", SLOT_HIT_Y},
    {"hit.nx", SLOT_HIT_NX}, {"hit.ny", SLOT_HIT_NY},
    {"hit.line", SLOT_HIT_LINE}, {"hit.ball", SLOT_HIT_BALL},
};

// 内置函数

enum Fn {
    FN_SIN, FN_COS, FN_TAN, FN_ASIN, FN_ACOS, FN_ATAN, FN_ATAN2,
    FN_SQRT, FN_ABS, FN_FLOOR, FN_CEIL, FN_ROUND, FN_TRUNC, FN_SIGN,
    FN_MIN, FN_MAX, FN_CLAMP, FN_LERP, FN_STEP, FN_SMOOTHSTEP, FN_INVLERP,
    FN_POW, FN_EXP, FN_LOG, FN_LOG2, FN_MOD,
    FN_IF, FN_NOISE, FN_HASH, FN_RAND, FN_SCALE, FN_LUT,
    FN_COUNT
};

struct FnEntry { const char* name; int id; int minArgs; int maxArgs; };

const FnEntry kFns[] = {
    {"sin", FN_SIN, 1, 1}, {"cos", FN_COS, 1, 1}, {"tan", FN_TAN, 1, 1},
    {"asin", FN_ASIN, 1, 1}, {"acos", FN_ACOS, 1, 1}, {"atan", FN_ATAN, 1, 1},
    {"atan2", FN_ATAN2, 2, 2},
    {"sqrt", FN_SQRT, 1, 1}, {"abs", FN_ABS, 1, 1},
    {"floor", FN_FLOOR, 1, 1}, {"ceil", FN_CEIL, 1, 1},
    {"round", FN_ROUND, 1, 1}, {"trunc", FN_TRUNC, 1, 1},
    {"sign", FN_SIGN, 1, 1},
    {"min", FN_MIN, 1, 16}, {"max", FN_MAX, 1, 16},
    {"clamp", FN_CLAMP, 3, 3}, {"lerp", FN_LERP, 3, 3},
    {"step", FN_STEP, 2, 2}, {"smoothstep", FN_SMOOTHSTEP, 3, 3},
    {"invlerp", FN_INVLERP, 3, 3},
    {"pow", FN_POW, 2, 2}, {"exp", FN_EXP, 1, 1},
    {"log", FN_LOG, 1, 1}, {"log2", FN_LOG2, 1, 1}, {"mod", FN_MOD, 2, 2},
    {"if", FN_IF, 3, 3},
    {"noise", FN_NOISE, 1, 1}, {"hash", FN_HASH, 1, 1}, {"rand", FN_RAND, 1, 1},
    {"scale", FN_SCALE, 2, 2}, {"lut", FN_LUT, 5, 17},
};

const FnEntry* findFn(const std::string& name) {
    for (const auto& f : kFns)
        if (name == f.name) return &f;
    return nullptr;
}

// 确定性随机

inline double hash01(double x) {
    if (!std::isfinite(x)) return 0.5;
    // 折回可表示的整数区间，避免 double->int64 越界（UB）
    x = std::fmod(std::floor(x), 4294967296.0);
    uint64_t v = static_cast<uint64_t>(static_cast<int64_t>(x));
    v ^= 0x9e3779b97f4a7c15ull;
    v ^= v >> 30; v *= 0xbf58476d1ce4e5b9ull;
    v ^= v >> 27; v *= 0x94d049bb133111ebull;
    v ^= v >> 31;
    return double(v >> 11) * (1.0 / 9007199254740992.0);
}

inline double smoothNoise1(double x) {
    const double i = std::floor(x);
    const double f = x - i;
    const double u = f * f * (3.0 - 2.0 * f);
    const double a = hash01(i);
    const double b = hash01(i + 1.0);
    return a + (b - a) * u;
}

// 音阶

struct ScaleEntry { const char* name; std::vector<int> steps; };

const std::vector<ScaleEntry>& scaleTable() {
    static const std::vector<ScaleEntry> t = {
        {"majorPent", {0, 2, 4, 7, 9}},
        {"minorPent", {0, 3, 5, 7, 10}},
        {"major",     {0, 2, 4, 5, 7, 9, 11}},
        {"minor",     {0, 2, 3, 5, 7, 8, 10}},
        {"blues",     {0, 3, 5, 6, 7, 10}},
        {"dorian",    {0, 2, 3, 5, 7, 9, 10}},
        {"wholeTone", {0, 2, 4, 6, 8, 10}},
        {"chromatic", {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11}},
    };
    return t;
}

bool scaleExists(const std::string& name) {
    for (const auto& s : scaleTable())
        if (name == s.name) return true;
    return false;
}

double quantizeToScale(double note, const std::string& scaleName) {
    if (!std::isfinite(note)) return 0.0;
    note = std::clamp(note, -12000.0, 12000.0);
    const std::vector<int>* steps = nullptr;
    for (const auto& s : scaleTable())
        if (scaleName == s.name) steps = &s.steps;
    if (!steps || steps->empty()) return std::round(note);

    const double base = std::floor(note / 12.0) * 12.0;
    double best = note;
    double bestDist = 1e9;
    for (int oct = -1; oct <= 1; ++oct) {
        for (int st : *steps) {
            const double cand = base + double(oct * 12 + st);
            const double d = std::fabs(cand - note);
            if (d < bestDist) { bestDist = d; best = cand; }
        }
    }
    return best;
}

// 词法

struct Tok {
    enum Type { End, Num, Str, Ident, Op } type = End;
    std::string s;
    double num = 0;
    int pos = 0;   // 0-based
};

bool isIdentStart(char c) { return std::isalpha(static_cast<unsigned char>(c)) || c == '_'; }
bool isIdentChar(char c)  { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; }

bool tokenize(const std::string& src, std::vector<Tok>& out, std::string* err) {
    size_t i = 0;
    auto fail = [&](const std::string& msg, size_t at) {
        if (err) *err = "第 " + std::to_string(at + 1) + " 列: " + msg;
        return false;
    };

    while (i < src.size()) {
        const char c = src[i];
        if (std::isspace(static_cast<unsigned char>(c))) { ++i; continue; }

        Tok t;
        t.pos = int(i);

        if (std::isdigit(static_cast<unsigned char>(c)) || (c == '.' && i + 1 < src.size() && std::isdigit(static_cast<unsigned char>(src[i + 1])))) {
            char* end = nullptr;
            t.num = std::strtod(src.c_str() + i, &end);
            t.type = Tok::Num;
            i = size_t(end - src.c_str());
            out.push_back(t);
            continue;
        }
        if (isIdentStart(c)) {
            size_t j = i;
            while (j < src.size() && isIdentChar(src[j])) ++j;
            t.type = Tok::Ident;
            t.s = src.substr(i, j - i);
            i = j;
            out.push_back(t);
            continue;
        }
        if (c == '"' || c == '\'') {
            const char q = c;
            size_t j = i + 1;
            std::string s;
            while (j < src.size() && src[j] != q) {
                if (src[j] == '\\' && j + 1 < src.size()) ++j;
                s.push_back(src[j]);
                ++j;
            }
            if (j >= src.size()) return fail("字符串没有闭合", i);
            t.type = Tok::Str;
            t.s = s;
            i = j + 1;
            out.push_back(t);
            continue;
        }

        const char n1 = (i + 1 < src.size()) ? src[i + 1] : '\0';
        if ((c == '&' && n1 == '&') || (c == '|' && n1 == '|') ||
            (c == '<' && n1 == '=') || (c == '>' && n1 == '=') ||
            (c == '=' && n1 == '=') || (c == '!' && n1 == '=')) {
            t.type = Tok::Op;
            t.s = std::string(1, c) + n1;
            i += 2;
            out.push_back(t);
            continue;
        }
        if (std::strchr("+-*/%^()[],?:<>!=.;", c)) {
            t.type = Tok::Op;
            t.s = std::string(1, c);
            ++i;
            out.push_back(t);
            continue;
        }
        return fail(std::string("不认识的字符 '") + c + "'", i);
    }

    Tok end;
    end.type = Tok::End;
    end.pos = int(src.size());
    out.push_back(end);
    return true;
}

// 语法

struct Parser {
    const std::vector<Tok>& t;
    size_t p = 0;
    int depth_ = 0;
    static constexpr int kMaxDepth = 96;
    static constexpr size_t kMaxNodes = 4096;
    std::vector<ExprNode> nodes;
    std::vector<int> args;
    std::vector<std::string> strs;
    std::vector<std::string> stateNames;
    std::string error;

    explicit Parser(const std::vector<Tok>& toks) : t(toks) {}

    const Tok& cur() const { return t[p]; }
    bool isOp(const char* s) const { return t[p].type == Tok::Op && t[p].s == s; }
    bool acceptOp(const char* s) { if (isOp(s)) { ++p; return true; } return false; }

    bool fail(const std::string& msg, int pos) {
        if (error.empty()) error = "第 " + std::to_string(pos + 1) + " 列: " + msg;
        return false;
    }

    int add(ExprNode n) {
        if (nodes.size() >= kMaxNodes) {
            fail("表达式太长", cur().pos);
            return -1;
        }
        nodes.push_back(n);
        return int(nodes.size()) - 1;
    }
    int num(double v) { ExprNode n; n.op = ExprNode::Num; n.num = v; return add(n); }
    int str(const std::string& s) {
        strs.push_back(s);
        ExprNode n; n.op = ExprNode::Str; n.a = int(strs.size()) - 1;
        return add(n);
    }

    int stateSlot(const std::string& name) {
        for (size_t i = 0; i < stateNames.size(); ++i)
            if (stateNames[i] == name) return int(i);
        stateNames.push_back(name);
        return int(stateNames.size()) - 1;
    }

    int parseProgram();
    int parseAssign();
    int parseConditional();
    int parseBinary(int minPrec);
    int parseUnary();
    int parsePrimary();
    int parseCallOrIdent();
};

int Parser::parseProgram() {
    int last = -1;
    while (cur().type != Tok::End) {
        int v = parseAssign();
        if (v < 0) return -1;
        if (last < 0) {
            last = v;
        } else {
            ExprNode n; n.op = ExprNode::Seq; n.a = last; n.b = v;
            last = add(n);
        }
        if (!acceptOp(";")) break;
    }
    if (cur().type != Tok::End) return fail("表达式后面还有多余的内容", cur().pos) ? -1 : -1;
    if (last < 0) return fail("表达式是空的", cur().pos) ? -1 : -1;
    return last;
}

int Parser::parseAssign() {
    if (cur().type == Tok::Ident && cur().s == "s" && p + 3 < t.size() &&
        t[p + 1].type == Tok::Op && t[p + 1].s == "." &&
        t[p + 2].type == Tok::Ident &&
        t[p + 3].type == Tok::Op && t[p + 3].s == "=") {
        const std::string name = t[p + 2].s;
        p += 4;
        const int value = parseAssign();
        if (value < 0) return -1;
        ExprNode n; n.op = ExprNode::Assign; n.slot = stateSlot(name); n.a = value;
        return add(n);
    }
    return parseConditional();
}

int Parser::parseConditional() {
    const int cond = parseBinary(0);
    if (cond < 0) return -1;
    if (acceptOp("?")) {
        const int a = parseAssign();
        if (a < 0) return -1;
        if (!acceptOp(":")) return fail("条件表达式缺少 ':'", cur().pos) ? -1 : -1;
        const int b = parseAssign();
        if (b < 0) return -1;
        ExprNode n; n.op = ExprNode::Cond; n.a = cond; n.b = a; n.c = b;
        return add(n);
    }
    return cond;
}

int opPrec(const std::string& s) {
    if (s == "||") return 1;
    if (s == "&&") return 2;
    if (s == "==" || s == "!=") return 3;
    if (s == "<" || s == "<=" || s == ">" || s == ">=") return 4;
    if (s == "+" || s == "-") return 5;
    if (s == "*" || s == "/" || s == "%") return 6;
    if (s == "^") return 7;
    return -1;
}

int Parser::parseBinary(int minPrec) {
    int left = parseUnary();
    if (left < 0) return -1;
    while (cur().type == Tok::Op) {
        const int prec = opPrec(cur().s);
        if (prec < 0 || prec < minPrec) break;
        const std::string op = cur().s;
        ++p;
        const int right = parseBinary(op == "^" ? prec : prec + 1);   // ^ 右结合
        if (right < 0) return -1;

        ExprNode n;
        if (op == "+") n.op = ExprNode::Add;
        else if (op == "-") n.op = ExprNode::Sub;
        else if (op == "*") n.op = ExprNode::Mul;
        else if (op == "/") n.op = ExprNode::Div;
        else if (op == "%") n.op = ExprNode::Mod;
        else if (op == "^") n.op = ExprNode::Pow;
        else if (op == "<") n.op = ExprNode::Lt;
        else if (op == "<=") n.op = ExprNode::Le;
        else if (op == ">") n.op = ExprNode::Gt;
        else if (op == ">=") n.op = ExprNode::Ge;
        else if (op == "==") n.op = ExprNode::Eq;
        else if (op == "!=") n.op = ExprNode::Ne;
        else if (op == "&&") n.op = ExprNode::And;
        else n.op = ExprNode::Or;
        n.a = left; n.b = right;
        left = add(n);
    }
    return left;
}

int Parser::parseUnary() {
    if (++depth_ > kMaxDepth) {
        --depth_;
        return fail("嵌套太深", cur().pos) ? -1 : -1;
    }
    struct Guard { Parser& p; ~Guard() { --p.depth_; } } guard{*this};

    if (isOp("-")) {
        ++p;
        const int v = parseBinary(7);   // 让 ^ 优先于负号：-2^2 == -(2^2)
        if (v < 0) return -1;
        if (nodes[v].op == ExprNode::Num) return num(-nodes[v].num);
        ExprNode n; n.op = ExprNode::Neg; n.a = v;
        return add(n);
    }
    if (isOp("!")) {
        ++p;
        const int v = parseUnary();
        if (v < 0) return -1;
        ExprNode n; n.op = ExprNode::Not; n.a = v;
        return add(n);
    }
    if (isOp("+")) { ++p; return parseUnary(); }
    return parsePrimary();
}

int Parser::parsePrimary() {
    if (cur().type == Tok::Num) {
        const double v = cur().num;
        ++p;
        return num(v);
    }
    if (cur().type == Tok::Str) {
        const std::string s = cur().s;
        ++p;
        return str(s);
    }
    if (isOp("(")) {
        ++p;
        const int v = parseAssign();
        if (v < 0) return -1;
        if (!acceptOp(")")) return fail("缺少 ')'", cur().pos) ? -1 : -1;
        return v;
    }
    if (cur().type == Tok::Ident) return parseCallOrIdent();
    return fail("这里需要一个数、变量或 '('", cur().pos) ? -1 : -1;
}

int Parser::parseCallOrIdent() {
    std::string name = cur().s;
    const int startPos = cur().pos;
    ++p;

    if (isOp("(")) {
        const FnEntry* fn = findFn(name);
        if (!fn) return fail("没有这个函数: '" + name + "'", startPos) ? -1 : -1;
        ++p;
        std::vector<int> callArgs;
        if (!isOp(")")) {
            for (;;) {
                const int a = parseAssign();
                if (a < 0) return -1;
                callArgs.push_back(a);
                if (acceptOp(",")) continue;
                break;
            }
        }
        if (!acceptOp(")")) return fail("缺少 ')'", cur().pos) ? -1 : -1;
        const int n = int(callArgs.size());
        if (fn->id == FN_LUT && ((n - 1) % 2) != 0) {
            return fail("lut(x, x0,y0, x1,y1, ...) 的坐标必须成对", startPos) ? -1 : -1;
        }
        if (fn->id == FN_SCALE && n == 2 && nodes[callArgs[1]].op == ExprNode::Str) {
            const std::string& name = strs[size_t(nodes[callArgs[1]].a)];
            if (!scaleExists(name)) {
                return fail("没有这个音阶: '" + name + "'", startPos) ? -1 : -1;
            }
        }
        if (n < fn->minArgs || n > fn->maxArgs) {
            return fail("函数 '" + name + "' 需要 " + std::to_string(fn->minArgs) +
                        "~" + std::to_string(fn->maxArgs) + " 个参数，给了 " + std::to_string(n),
                        startPos) ? -1 : -1;
        }
        ExprNode node;
        node.op = ExprNode::Call;
        node.fn = fn->id;
        node.argStart = int(args.size());
        node.argCount = n;
        for (int a : callArgs) args.push_back(a);
        return add(node);
    }

    // 变量：允许 a.b.c 这种形式
    while (isOp(".") && p + 1 < t.size() && t[p + 1].type == Tok::Ident) {
        name += "." + t[p + 1].s;
        p += 2;
    }

    if (name.size() > 2 && name[0] == 's' && name[1] == '.') {
        ExprNode node;
        node.op = ExprNode::State;
        node.slot = stateSlot(name.substr(2));
        return add(node);
    }

    const int slot = slotFromName(name);
    if (slot >= 0) {
        ExprNode node;
        node.op = ExprNode::Var;
        node.slot = slot;
        return add(node);
    }
    return fail("没有这个变量: '" + name + "'", startPos) ? -1 : -1;
}

} // namespace

// 对外接口

int slotFromName(const std::string& name) {
    for (const auto& e : kSlots)
        if (name == e.name) return e.slot;
    return -1;
}

std::shared_ptr<Expr> Expr::compile(const std::string& source, std::string* error) {
    if (error) error->clear();

    std::vector<Tok> toks;
    if (!tokenize(source, toks, error)) return nullptr;

    Parser ps(toks);
    const int root = ps.parseProgram();
    if (root < 0) {
        if (error) *error = ps.error.empty() ? "表达式有误" : ps.error;
        return nullptr;
    }

    auto e = std::shared_ptr<Expr>(new Expr());
    e->source_ = source;
    e->nodes_ = std::move(ps.nodes);
    e->args_ = std::move(ps.args);
    e->strs_ = std::move(ps.strs);
    e->stateNames_ = std::move(ps.stateNames);
    e->root_ = root;
    return e;
}

namespace {

struct Val {
    double n = 0.0;
    const std::string* s = nullptr;
};

double callBuiltin(int fn, const Val* a, int n);

} // namespace

double Expr::evalNode(int idx, const double* slots, double* state) const {
    const ExprNode& nd = nodes_[idx];
    switch (nd.op) {
    case ExprNode::Num:   return nd.num;
    case ExprNode::Str:   return 0.0;
    case ExprNode::Var:   return slots[nd.slot];
    case ExprNode::State: return state ? state[nd.slot] : 0.0;
    case ExprNode::Neg:   return -evalNode(nd.a, slots, state);
    case ExprNode::Not:   return evalNode(nd.a, slots, state) == 0.0 ? 1.0 : 0.0;
    case ExprNode::Add: {
        const double l = evalNode(nd.a, slots, state);
        const double r = evalNode(nd.b, slots, state);
        return l + r;
    }
    case ExprNode::Sub: {
        const double l = evalNode(nd.a, slots, state);
        const double r = evalNode(nd.b, slots, state);
        return l - r;
    }
    case ExprNode::Mul: {
        const double l = evalNode(nd.a, slots, state);
        const double r = evalNode(nd.b, slots, state);
        return l * r;
    }
    case ExprNode::Div: {
        const double l = evalNode(nd.a, slots, state);
        const double r = evalNode(nd.b, slots, state);
        return r == 0.0 ? 0.0 : l / r;
    }
    case ExprNode::Mod: {
        const double l = evalNode(nd.a, slots, state);
        const double r = evalNode(nd.b, slots, state);
        return r == 0.0 ? 0.0 : std::fmod(l, r);
    }
    case ExprNode::Pow: {
        const double l = evalNode(nd.a, slots, state);
        const double r = evalNode(nd.b, slots, state);
        return std::pow(l, r);
    }
    case ExprNode::Lt: { const double l = evalNode(nd.a, slots, state); const double r = evalNode(nd.b, slots, state); return l < r ? 1.0 : 0.0; }
    case ExprNode::Le: { const double l = evalNode(nd.a, slots, state); const double r = evalNode(nd.b, slots, state); return l <= r ? 1.0 : 0.0; }
    case ExprNode::Gt: { const double l = evalNode(nd.a, slots, state); const double r = evalNode(nd.b, slots, state); return l > r ? 1.0 : 0.0; }
    case ExprNode::Ge: { const double l = evalNode(nd.a, slots, state); const double r = evalNode(nd.b, slots, state); return l >= r ? 1.0 : 0.0; }
    case ExprNode::Eq: { const double l = evalNode(nd.a, slots, state); const double r = evalNode(nd.b, slots, state); return l == r ? 1.0 : 0.0; }
    case ExprNode::Ne: { const double l = evalNode(nd.a, slots, state); const double r = evalNode(nd.b, slots, state); return l != r ? 1.0 : 0.0; }
    case ExprNode::And:   return (evalNode(nd.a, slots, state) != 0.0 && evalNode(nd.b, slots, state) != 0.0) ? 1.0 : 0.0;
    case ExprNode::Or:    return (evalNode(nd.a, slots, state) != 0.0 || evalNode(nd.b, slots, state) != 0.0) ? 1.0 : 0.0;
    case ExprNode::Cond:  return evalNode(nd.a, slots, state) != 0.0
                                 ? evalNode(nd.b, slots, state)
                                 : evalNode(nd.c, slots, state);
    case ExprNode::Seq: {
        evalNode(nd.a, slots, state);
        return evalNode(nd.b, slots, state);
    }
    case ExprNode::Assign: {
        const double v = evalNode(nd.a, slots, state);
        if (state) state[nd.slot] = v;
        return v;
    }
    case ExprNode::Call: {
        Val vals[16];
        const int n = nd.argCount;

        if (nd.fn == FN_IF) {   // 条件与分支都要惰性
            const double cond = evalNode(args_[size_t(nd.argStart)], slots, state);
            return evalNode(args_[size_t(nd.argStart + (cond != 0.0 ? 1 : 2))], slots, state);
        }
        for (int k = 0; k < n; ++k) {
            const int ci = args_[size_t(nd.argStart + k)];
            const ExprNode& cn = nodes_[ci];
            if (cn.op == ExprNode::Str) {
                vals[k].s = &strs_[size_t(cn.a)];
                vals[k].n = 0.0;
            } else {
                vals[k].n = evalNode(ci, slots, state);
            }
        }
        return callBuiltin(nd.fn, vals, n);
    }
    }
    return 0.0;
}

double Expr::eval(const double* slots, double* state) const {
    if (root_ < 0) return 0.0;
    return evalNode(root_, slots, state);
}

namespace {

double callBuiltin(int fn, const Val* a, int n) {
    auto arg = [&](int i) { return a[i].n; };
    switch (fn) {
    case FN_SIN:  return std::sin(arg(0));
    case FN_COS:  return std::cos(arg(0));
    case FN_TAN:  return std::tan(arg(0));
    case FN_ASIN: return std::asin(std::clamp(arg(0), -1.0, 1.0));
    case FN_ACOS: return std::acos(std::clamp(arg(0), -1.0, 1.0));
    case FN_ATAN: return std::atan(arg(0));
    case FN_ATAN2: return std::atan2(arg(0), arg(1));
    case FN_SQRT: return std::sqrt(std::max(0.0, arg(0)));
    case FN_ABS:  return std::fabs(arg(0));
    case FN_FLOOR: return std::floor(arg(0));
    case FN_CEIL:  return std::ceil(arg(0));
    case FN_ROUND: return std::round(arg(0));
    case FN_TRUNC: return std::trunc(arg(0));
    case FN_SIGN:  return arg(0) > 0.0 ? 1.0 : (arg(0) < 0.0 ? -1.0 : 0.0);
    case FN_MIN: {
        double m = arg(0);
        for (int i = 1; i < n; ++i) m = std::min(m, arg(i));
        return m;
    }
    case FN_MAX: {
        double m = arg(0);
        for (int i = 1; i < n; ++i) m = std::max(m, arg(i));
        return m;
    }
    case FN_CLAMP: return std::clamp(arg(0), std::min(arg(1), arg(2)), std::max(arg(1), arg(2)));
    case FN_LERP:  return arg(0) + (arg(1) - arg(0)) * arg(2);
    case FN_STEP:  return arg(1) < arg(0) ? 0.0 : 1.0;
    case FN_SMOOTHSTEP: {
        const double d = arg(1) - arg(0);
        double t = std::fabs(d) < 1e-12 ? (arg(2) >= arg(0) ? 1.0 : 0.0) : (arg(2) - arg(0)) / d;
        t = std::clamp(t, 0.0, 1.0);
        return t * t * (3.0 - 2.0 * t);
    }
    case FN_INVLERP: {
        const double d = arg(1) - arg(0);
        return std::fabs(d) < 1e-12 ? 0.0 : (arg(2) - arg(0)) / d;
    }
    case FN_POW:  return std::pow(arg(0), arg(1));
    case FN_EXP:  return std::exp(arg(0));
    case FN_LOG:  return arg(0) <= 0.0 ? 0.0 : std::log(arg(0));
    case FN_LOG2: return arg(0) <= 0.0 ? 0.0 : std::log2(arg(0));
    case FN_MOD:  return arg(1) == 0.0 ? 0.0 : std::fmod(arg(0), arg(1));
    case FN_IF:   return arg(0) != 0.0 ? arg(1) : arg(2);
    case FN_NOISE: return smoothNoise1(arg(0));
    case FN_HASH:  return hash01(arg(0));
    case FN_RAND:  return hash01(arg(0) + 0.5);
    case FN_SCALE: {
        if (n < 2 || !a[1].s) return std::round(arg(0));
        return quantizeToScale(arg(0), *a[1].s);
    }
    case FN_LUT: {
        // lut(x, x0,y0, x1,y1, ...) 线性插值，超出两端取端点
        const double x = arg(0);
        const int pairs = (n - 1) / 2;
        if (pairs <= 0) return 0.0;
        auto px = [&](int i) { return arg(1 + i * 2); };
        auto py = [&](int i) { return arg(2 + i * 2); };
        if (x <= px(0)) return py(0);
        for (int i = 1; i < pairs; ++i) {
            if (x <= px(i)) {
                const double d = px(i) - px(i - 1);
                const double t = std::fabs(d) < 1e-12 ? 0.0 : (x - px(i - 1)) / d;
                return py(i - 1) + (py(i) - py(i - 1)) * t;
            }
        }
        return py(pairs - 1);
    }
    default: return 0.0;
    }
}

} // namespace
