// 表达式引擎：无循环、无递归、无副作用，求值时间与表达式长度成正比。
// 解析期把变量解析成槽位下标，求值期只做数组访问。
#pragma once
#include <memory>
#include <string>
#include <vector>

// 固定的变量槽位。解析时按名字确定下标，求值时直接读数组。
enum Slot {
    SLOT_T = 0,
    SLOT_DT,
    SLOT_FRAME,
    SLOT_PI,
    SLOT_E,
    SLOT_IDX,

    SLOT_SELF_X,
    SLOT_SELF_Y,
    SLOT_SELF_VX,
    SLOT_SELF_VY,
    SLOT_SELF_R,
    SLOT_SELF_AX,
    SLOT_SELF_AY,
    SLOT_SELF_BX,
    SLOT_SELF_BY,
    SLOT_SELF_DIR,
    SLOT_SELF_SPEED,

    SLOT_WORLD_GRAVITY,
    SLOT_WORLD_KILLY,
    SLOT_WORLD_SEED,
    SLOT_WORLD_TIME,

    SLOT_HIT_SPEED,
    SLOT_HIT_X,
    SLOT_HIT_Y,
    SLOT_HIT_NX,
    SLOT_HIT_NY,
    SLOT_HIT_LINE,
    SLOT_HIT_BALL,

    SLOT_COUNT
};

int         slotFromName(const std::string& name);   // -1 = 未知

struct ExprNode {
    enum Op {
        Num, Str, Var, State, Neg, Not,
        Add, Sub, Mul, Div, Mod, Pow,
        Lt, Le, Gt, Ge, Eq, Ne, And, Or,
        Cond, Call, Seq, Assign
    } op = Num;

    double num   = 0.0;
    int    a     = -1;   // 子节点 / 字符串下标
    int    b     = -1;
    int    c     = -1;
    int    argStart = 0;
    int    argCount = 0;
    int    fn    = 0;    // 内置函数下标
    int    slot  = 0;    // 变量槽位 / 状态槽位
};

class Expr {
public:
    // 编译失败返回 nullptr，error 里带 "第 N 列: ..."
    static std::shared_ptr<Expr> compile(const std::string& source, std::string* error);

    double eval(const double* slots, double* state) const;
    int  stateCount() const { return int(stateNames_.size()); }

private:
    Expr() = default;

    double evalNode(int idx, const double* slots, double* state) const;

    std::string              source_;
    std::vector<ExprNode>    nodes_;
    std::vector<int>         args_;
    std::vector<std::string> strs_;
    std::vector<std::string> stateNames_;
    int                      root_ = -1;
};
