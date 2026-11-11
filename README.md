# 弹线 Linebeat

一个 2D 重力沙盒 / 会自己演奏的乐器：摆线段、放球、放发射器，球撞线就发出声音。**线段和发射器的每个数值参数都能写成表达式**（球的位置和速度是初始状态，由物理推着走，只填数值），球会自己一直跑下去。

```
C++20 · SDL3 · Dear ImGui · OpenGL · FluidSynth
```

## 跑起来

```bash
./run.sh                 # 需要时自动编译
```

依赖：`sdl3`、`fluidsynth`、`cmake`、`gcc`、Mesa。三方库不随仓库分发，版本在 `CMakeLists.txt` 里集中固定：

- `nlohmann/json`：优先用系统的 **3.12.0**（`find_package` 精确匹配），没有才从上游 tag 拉取；
- Dear ImGui：从上游 tag 拉取 **v1.93.0** 到 `build/_deps`（首次 configure 需要网络）。

离线构建时可用 `-DFETCHCONTENT_SOURCE_DIR_IMGUI_UPSTREAM=<dir>` 指向本地副本。

```bash
cmake -S . -B build && cmake --build build -j$(nproc) && ./build/linebeat
```

## 现在能做什么

| | |
|---|---|
| 物理 | 任意线段、球、发射器；固定步长 1/240，扫掠碰撞，完全弹性（法向速度 < 0.12 m/s 视为接触并贴住） |
| 声音 | FluidSynth 实时读 SF2；每条线一个音色，音色下拉直接列出 SF2 里的**真实音色名**，bank 128 自动走鼓通道 |
| 表达式 | 线段/发射器的每个数值参数都能点 `ƒ` 换成表达式，有硬上限，不会卡界面 |
| 编辑 | 左边缘工具条、框选/多选、整组拖动、复制粘贴、滚轮缩放、F 适配、撤销、双栈撤销 |
| 文件 | 开始界面 + 多存档 + 最近列表；JSON 工程 `.lbproj` |
| 演示 | `scenes/demo_loop.lbproj`：鼓组（一个发射器 + 四条独立竖线）+ 16 分踩镲 + 4536 和弦，打开即响 |

## 操作

| 操作 | 效果 |
| --- | --- |
| 鼠标移到**左边缘** | 唤出工具条：选 / 线 / 球 / 发 / 删 |
| `1`–`5` | 切换工具 |
| 线工具：拖动 | 画一条线段 |
| **Shift** 画线 | 锁定水平 / 垂直 |
| **Alt** 画线 | 关闭 0.05 网格吸附 |
| 球 / 发射器工具：单击 | 放置并选中 |
| 选择工具：拖空白 | 框选（`Shift` 加选） |
| 选择工具：拖对象 | 移动（多选时整体移动；`Shift` 只沿主轴） |
| 右键 | 选中光标下的对象 |
| 滚轮 | 以光标为中心缩放（2 – 4000 px/m） |
| `F` | 视角适配全部内容 |
| 中键拖动 | 平移 |
| `空格` | 暂停 / 继续 |
| `Esc` | 菜单（返回 / 返回至标题 / 退出） |
| `Ctrl+Z` | 撤销 |
| `Ctrl+C` / `Ctrl+V` | 复制 / 粘贴到鼠标位置 |
| `Ctrl+A` | 全选 |
| `Ctrl+S` / `Ctrl+Shift+S` | 保存 / 另存为（自带输入框） |
| `Ctrl+O` | 打开场景（自带列表：`scenes/` 和 `~/.local/share/linebeat/scenes`） |
| `Ctrl+I` | 钉住 / 取消钉住右侧面板（平时自动隐藏：鼠标贴右边缘、或停在面板上时滑出） |
| `Ctrl+E` | 编辑存档模式 |
| `Ctrl+Tab` | 简洁 / 高级模式 |
| `Ctrl+=` / `Ctrl+-` / `Ctrl+0` | 界面缩放（开始界面、菜单、弹窗里都能用） |

简洁模式的「声音」里有两组**映射预设**，不用写表达式：

| 音高 | 力度 |
|---|---|
| 固定音高 | 固定力度 |
| 撞得越猛音越高 | 撞得越猛越响 |
| 按撞击高度 | 撞得越轻越响 |
| 按撞击位置 x | 压缩到 60-100 |
| 每小节轮换（4536） | |

选中的预设会直接写进参数（在高级模式里就能看到对应表达式），选「固定」则回到手动填数值。
| `Ctrl+L` | 变量表（高级模式，点名字复制） |
| `Ctrl+=` / `Ctrl+-` / `Ctrl+0` | 界面缩放放大 / 缩小 / 还原（0.7–2.5，记在配置里） |
| `Delete` | 删除选中 |

## 变量表

高级模式下 `Ctrl+L` 打开一个浮动窗口，实时列出表达式里能用的所有变量和它们的当前值
（`t`、`dt`、`frame`、`self.*`、`world.*`，以及撞击时才有的 `hit.*`）。
点名字复制到剪贴板，粘贴进 `ƒ` 表达式框即可。

## 撤销与保存（重要）

| 动作 | 是否进撤销栈 | 是否写盘 |
| --- | --- | --- |
| 线 / 发射器 / 参数 / 重力 | 进栈 A，常驻 | 否 |
| **暂停时**放球、删球 | 进栈 B，只在**当次暂停**内有效 | 否 |
| **运行中**放球、删球 | 不进栈 | 否 |
| **编辑存档模式**（`Ctrl+E`）下放球、删球 | 进栈 A，且写进 `project.balls` | 否 |
| 按 `Ctrl+S` 保存 | — | **是**（关窗口不保存） |

- 暂停时 `Ctrl+Z` 会比较两个栈的栈顶，撤销**较新**的那个。
- 进入编辑存档模式会强制暂停并把球恢复到初态。
- **编辑存档里做的一切改动（放球、删球、改球的坐标/半径、拖球）都会写回工程**，
  退出编辑存档、跑一会儿、再按 `Ctrl+E` 回来，这些改动仍然在。其他时候的球操作不保留。
- 「重置仿真」= 球回到场景初态；「重置为存档」= 从磁盘重读，丢弃所有改动。

## 表达式

线段和发射器的参数右边有 `ƒ`，点了换成表达式，`×` 换回常量（保留最近一次求值结果）。球的位置/速度/半径不参与表达式——它们每步都由物理算出，用户只定初值。表达式没有循环递归，嵌套 96 层 / 节点 4096 个封顶，求值时间有界。

```
t  dt  frame  i  pi  e
self.x self.y self.vx self.vy self.r
self.a.x self.a.y self.b.x self.b.y        // 线段的两个端点（实时）
self.dir self.speed                         // 发射器
world.gravity world.kill_y world.seed world.time
hit.speed hit.x hit.y hit.nx hit.ny hit.line hit.ball   // 只在撞击时存在
s.xxx                                       // 每个表达式自己的持久状态
```

```
sin cos tan asin acos atan atan2 sqrt abs floor ceil round trunc sign
min max clamp lerp step smoothstep invlerp(a,b,x) pow exp log log2 mod
if(cond,a,b)  noise(x)  hash(x)  rand(x)
scale(note, "majorPent"|"minorPent"|"major"|"minor"|"blues"|"dorian"|"wholeTone"|"chromatic")
lut(x, x0,y0, x1,y1, ...)
```

约定：`^` 右结合且优先于负号（`-2^2 == -4`）；除数为 0 返回 0；算出 `inf`/`nan` 会被替换成 0；`scale` 的音阶名编译期校验。

例子：

```
72 + hit.speed * 3
scale(60 + t * 4, "majorPent")
lut(floor(t / 2) % 4, 0,53, 1,55, 2,52, 3,57)     // 4536 和弦进行
s.p = s.p + dt * 2; sin(s.p) * 1.5
```

## 工程文件

```jsonc
{
  "format": "linebeat", "version": 2, "name": "鼓组 + 4536", "nextId": 30,
  "world": { "gravity": -9.81, "kill_y": -6.0, "seed": 7 },
  "lines": [{
    "id": 1,
    "ax": { "v": -5.0 }, "ay": { "v": 0.0 },          // "v" = 常量
    "bx": { "e": "self.a.x + 5" }, "by": { "v": 0.0 }, // "e" = 表达式
    "collide": true,
    "voice": { "type": "sf2", "file": "", "bank": 128, "program": 0 },
    "note": { "e": "lut(floor(t / 2) % 4, 0,53, 1,55, 2,52, 3,57)" },
    "vel": { "v": 90 }, "gain": { "v": 1.0 }
  }],
  "balls":    [{ "id": 11, "x": 0, "y": 1.05, "vx": 0, "vy": 0, "r": 0 }],
  "emitters": [{ "id": 21, "px": {"v": -3.6}, "py": {"v": 1.8},
                 "dir": {"v": 0.12}, "speed": {"v": 3.4},
                 "interval": {"v": 1.6}, "delay": {"v": 0.4},
                 "count": {"v": -1}, "enabled": true }]
}
```

未知音源类型降级为静音；未知字段会被丢弃（文档如实写）。保存前清洗非有限值、写临时文件再 rename。

## 代码结构

```
core/            不依赖 UI，可单独跑
  expr.*         词法 / Pratt 语法 / AST / 求值 + 内置函数
  model.*        工程、线段、球、发射器、Param（常量或表达式）
  physics.*      固定步长 + 扫掠圆 vs 线段
  audio.*        FluidSynth + SDL3 输出，16 通道 LRU，音色枚举
  project_io.*   JSON 存读、默认场景
src/             UI
  app.*          状态、主循环、编辑操作、双栈撤销
  canvas.*       相机、世界绘制、框选与拖拽
  panels.*       工具条、检查器、状态栏
  start.cpp      开始界面
  config.*       用户配置（最近列表等）
  dialog.*       SDL3 文件对话框
  i18n.*         文案表（中文已接，英文列留空）
  main.cpp       SDL3 / OpenGL / ImGui 装配，--shot / --frames
tools/lb_cli.cpp 命令行工具
docs/PRODUCT.md  产品文档
```

## 自检

```bash
./build/lb_cli --selftest                     # 物理：周期、能量守恒
./build/lb_cli --roundtrip scenes/demo_loop.lbproj   # 存→读→再存，JSON 是否稳定
./build/lb_cli --trace scene.lbproj 2.0       # 逐球打印轨迹和撞击次数
./build/lb_cli --expr 'scale(64.3, "majorPent")'
./build/lb_cli --render out.wav --seconds 16 --project scenes/demo_loop.lbproj
./build/linebeat --shot s.ppm --size 1280x800 --no-audio
```

物理精度（60 秒、65 次撞击）：周期漂移 `1.7e-7`，顶点高度 1.049997 → 1.049996。
`scenes/probe/` 里有针对性场景（静止、天花板、狭缝、正好压在线上、退化线段、`self.a` 自引用）。

## 演示工程是怎么搭的

```
鼓组  一个发射器 + 四条独立竖线，120 BPM，鼓点落在第 1 / 2 / 3.5 / 4 拍
      （0.497 / 0.991 / 1.735 / 1.979 s，发射器每 4 拍补一个球）
      两条线的水平距离 ÷ 速度 = 这一下的时间，所以「拍位」直接决定竖线摆在哪；
      竖线只覆盖那一刻周围 0.45 m，球经过别的线时不会被挡住
踩镲  一条水平线上放一颗球，落点高度 h = g·T²/8 决定弹跳周期
      0.125 s ≈ 120 BPM 的十六分
和弦  三条水平线用 lut(floor(t/2)%4, …) 让音高随小节在 F–G–Em–Am 之间切换
```

鼓点的节奏是**摆出来的**：想要哪一拍发声，就把那条竖线放在「那一刻球正好经过的 x」。
改了重力或初速，轨迹变了，位置就得跟着重算 —— `tools/make_demo.py` 干的就是这件事。

```bash
python3 tools/make_demo.py                       # 默认 120 BPM、鼓点 1/2/3.5/4 拍
python3 tools/make_demo.py --bpm 150 --beats 1,2,3,4
```

鼓点时刻、竖线坐标、发射器间隔都由这个脚本算出来；改 BPM 或拍位后重跑，
再用 `./build/lb_cli --hits scenes/demo_loop.lbproj 4` 看实际撞击时刻。

## 已知限制

- 球与球不碰撞；没有 redo
- 撤销栈 64 步；运行中的球不进撤销栈
- 关闭窗口不保存
- 音频设备热插拔未处理
- 界面只有中文（英文列已留但没翻）
- 发射器进度在载入/撤销时重置
