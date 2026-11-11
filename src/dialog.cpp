#include "dialog.h"

#include <SDL3/SDL.h>

namespace lb {
namespace {

FileDialogResult g_result;
bool              g_open = false;     // 同一时刻只允许一个对话框
std::string       g_filterName;
std::string       g_pattern;
SDL_DialogFileFilter g_filter{};

void callback(void* /*userdata*/, const char* const* filelist, int /*filter*/) {
    g_open = false;
    g_result.done = true;
    if (filelist && filelist[0]) g_result.path = filelist[0];
    else g_result.path.clear();   // 用户取消
}

// 字符串要活到对话框关闭为止：SDL 只借用指针，这里用静态 storage，
// 且只在没有对话框打开时改写，避免把正在用的字符串换掉。
bool prepare(const std::string& name, const std::string& ext) {
    if (g_open) return false;
    g_filterName = name;
    g_pattern = "*." + ext;
    g_filter = {g_filterName.c_str(), g_pattern.c_str()};
    g_result = {};
    g_open = true;
    return true;
}

} // namespace

void openFileDialog(SDL_Window* window, const std::string& filterName, const std::string& ext) {
    if (!prepare(filterName, ext)) return;
    SDL_ShowOpenFileDialog(callback, nullptr, window, &g_filter, 1, nullptr, false);
}

FileDialogResult pollFileDialog() {
    FileDialogResult r = g_result;
    if (r.done) g_result = {};
    return r;
}

} // namespace lb
