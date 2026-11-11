// 用户配置：最近打开的存档、界面模式、语言、音量。
#pragma once
#include <string>
#include <vector>

namespace lb {

struct Config {
    std::vector<std::string> recent;      // 最近打开的场景路径，新的在前
    bool advanced = true;                 // 高级模式
    std::string language = "zh";          // 界面语言
    double masterGain = 0.62;
    double uiScale = 1.0;      // 界面缩放（Ctrl+= / Ctrl+- / Ctrl+0）
    std::string lastProject;              // 上次打开的存档
};

std::string userDataDir();      // ~/.local/share/linebeat
std::string userConfigDir();    // ~/.config/linebeat
std::string userScenesDir();    // ~/.local/share/linebeat/scenes
std::string configPath();       // ~/.config/linebeat/config.json

Config loadConfig();
void   saveConfig(const Config& c);

// 把路径塞进最近列表（去重、限制 8 条）
void rememberRecent(Config& c, const std::string& path);

// 程序自带的演示场景目录（exe 旁边或上一级的 scenes/）
std::string demoScenesDir();
std::vector<std::string> listProjects(const std::string& dir, int limit = 32);

} // namespace lb
