// SDL3 的异步文件对话框封装：结果在事件循环里回调，UI 每帧 poll 一下。
#pragma once
#include <string>
#include <vector>

struct SDL_Window;

namespace lb {

struct FileDialogResult {
    bool done = false;      // 本次拿到结果（用户可能取消）
    std::string path;       // 取消时为空
};

// filterName 例如 "Linebeat 场景"，ext 例如 "lbproj"
void openFileDialog(SDL_Window* window, const std::string& filterName, const std::string& ext);

FileDialogResult pollFileDialog();
} // namespace lb
