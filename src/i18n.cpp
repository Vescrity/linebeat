#include <cstddef>
#include "i18n.h"

namespace lb {
namespace {

Lang g_lang = Lang::Zh;

struct Entry {
    const char* zh;
    const char* en;
};

// 顺序必须和 i18n.h 里的 enum 一致
const Entry kTable[] = {
    {"弹线 Linebeat", "Linebeat"},
    {"重力、线段与小球构成的一小段循环", "Gravity, lines and balls, looping"},
    {"新建空场景", "New blank scene"},
    {"默认场景（一条线一颗球）", "Default scene"},
    {"打开场景…", "Open scene…"},
    {"演示", "Demo"},
    {"最近", "Recent"},
    {"（还没有打开过场景）", "(no scenes opened yet)"},
    {"（没有找到 scenes/）", "(scenes/ not found)"},
    {"拖动球 / 滚轮缩放 / 空格暂停 / Esc 菜单", "Drag balls / wheel to zoom / Space pause / Esc menu"},
    {"暂停 (空格)", "Pause (Space)"},
    {"继续 (空格)", "Resume (Space)"},
    {"重置仿真", "Reset sim"},
    {"重置为存档", "Reset to saved"},
    {"保存状态 (Ctrl+S)", "Save (Ctrl+S)"},
    {"另存为… (Ctrl+Shift+S)", "Save as… (Ctrl+Shift+S)"},
    {"高级模式 (Ctrl+Tab)", "Advanced mode (Ctrl+Tab)"},
    {"编辑存档模式 (Ctrl+E)", "Edit-save mode (Ctrl+E)"},
    {"世界", "World"},
    {"线段", "Line"},
    {"球", "Ball"},
    {"发射器", "Emitter"},
    {"提示", "Hints"},
    {"多选", "Multiple"},
    {"删除线段", "Delete line"},
    {"删除球", "Delete ball"},
    {"删除发射器", "Delete emitter"},
    {"删除 (Del)", "Delete (Del)"},
    {"复制 (Ctrl+C)", "Copy (Ctrl+C)"},
    {"粘贴 (Ctrl+V)", "Paste (Ctrl+V)"},
    {"● 未保存", "● Unsaved"},
    {"已保存", "Saved"},
    {"返回", ""},
    {"返回至标题", ""},
    {"退出", ""},
    {"有未保存的改动", ""},
    {"离开前要保存吗？", ""},
    {"保存并继续", ""},
    {"不保存", ""},
    {"取消", ""},
};

static_assert(sizeof(kTable) / sizeof(kTable[0]) == size_t(S::Count), "i18n 表与枚举不一致");

} // namespace

void setLanguage(Lang l) { g_lang = l; }
Lang language() { return g_lang; }

const char* tr(S key) {
    const size_t i = size_t(key);
    if (i >= size_t(S::Count)) return "";
    const Entry& e = kTable[i];
    if (g_lang == Lang::En && e.en[0]) return e.en;
    return e.zh;
}

} // namespace lb
