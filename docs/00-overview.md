# LILYGO UI NES 架构总览

本应用是基于 C++17、LILYGO UI AppKit 和 LVGL 9.5 的独立 NES 模拟器。
运行目标为 CM0 的 ARM64 Linux 环境，同时提供 SDL2 主机模拟器用于开发和测试。
应用不附带 ROM；用户提供合法取得的 `.nes` 文件，由唯一支持的 FCEUmm 核心执行。

项目介绍、依赖安装、编译和打包见 [中文 README](../README.zh-CN.md) 或
[English README](../README.md)，ROM 目录、环境变量、音频排查和运行限制见
[ROM 与运行配置](02-roms-and-runtime.md)，页面修改要求见
[UI 开发规范](01-user-interface.md)。

## 数据流

```text
ROM 目录 -> RomLibrary -> LibraryViewModel -> LibraryView
目录浏览 -> RomBrowserViewModel -> RomBrowserView
按键映射 -> SettingsViewModel -> SettingsView
目录路径 -> HelpViewModel -> HelpView

触摸控件、P1 键盘 -> GameView -> GameViewModel -> NesEmulator -> FCEUmm CPU / PPU / APU
Linux 触点快照 -----^                                |                    |
                                                   |                    +-> 音频输出
LVGL Image <- 预缩放 XRGB8888 <- 主线程帧副本 <- RGB565 双缓冲 <- 256 x 240 模拟器帧
```

ROM 扫描和列表刷新由主线程执行，模拟器和设备触摸读取各自使用工作线程。
工作线程不得创建或修改 LVGL 对象。`NesView` 作为应用页面容器，负责导航、错误弹窗、
共享 keypad 订阅和 16 ms 的 LVGL 定时器。定时器调用 `GameView::tick()` 读取触点快照、
更新键盘状态并复制最新完整帧，再调用 `NesViewModel::poll()` 处理存档结果和模拟器结束状态。
`NesViewModel` 组合五个页面 ViewModel 并协调共享运行状态；页面通过导航回调请求切换，
不直接访问其他页面的 LVGL 对象。
主线程将新帧按最近邻采样预缩放为显示尺寸的 XRGB8888 缓冲，复用采样坐标和重复行。
LVGL 直接绘制与输出格式一致的图像，不再执行每帧图像变换；仅尺寸变化时重新绑定
图像描述符。收到新帧后，View 标记图像重绘并将显示刷新定时器设为就绪，避免额外等待 LVGL 默认的 33 ms
刷新周期；没有新帧时不主动请求游戏画面重绘。ROM 库保留 AppKit 的默认刷新调度。
这是消除刷新等待，不保证板端达到 60 FPS；横屏仍受 AppKit 整屏软件旋转开销影响。

`lpm pack` 默认选择 `cm0-cross-gles`，使用原生 RGB565 GPU 纹理和 `lv_3dtexture`，
每帧只上传 `256 x 240` 像素，缩放与显示旋转由 GPU 完成。
显式使用 `cm0-cross` 时保留上述 XRGB8888 软件预缩放路径。
依赖、安装和验证步骤见 [GPU 渲染说明](03-gpu-rendering.md)。

FCEUmm 通过静态链接的 libretro API 接入，无需 RetroArch。适配层管理按键位图、
帧缓冲、计时器和音频输出。GLES 包由 `cm0-cross-gles` 构建，步骤见 [FCEUmm 核心](05-fceumm.md)。
`NES_PERF=1` 记录分阶段耗时，默认关闭；字段与对比限制见 [性能统计](04-performance.md)。

## 模块边界

| 模块 | 责任 |
| --- | --- |
| `src/domain/rom_library.*` | 扫描和浏览目录、校验并导入 ROM、解析 iNES 头、生成标题和元数据、计算 CRC32、删除 ROM 文件；不依赖 LVGL。 |
| `src/emulator/nes_emulator.*` | 管理工作线程、视频、音频、输入、计时和异步即时存档命令。 |
| `src/emulator/nes_input_mapping.*` | 管理可持久化的 P1 按键映射：默认 W/S/A/D、K/J、I/U、1/2；I/U 作为 A/B 连发，P2 不提供映射。 |
| `src/emulator/fceumm_core.*` | 通过 FCEUmm libretro API 加载 ROM，处理区域时序、输入、RGB565 视频、音频、电池 RAM 和即时存档。 |
| `src/emulator/nes_performance.*` | 可选的分阶段耗时统计，由 UI 线程每秒报告。 |
| `src/platform/multitouch_reader.*` | 在 Linux 上读取 evdev 触点，将原始坐标映射到显示坐标，发布加锁的触点快照。 |
| `src/pages/library/` | `LibraryViewModel` 管理 ROM 库、选中游戏及刷新、导入、删除和启动命令；`LibraryView` 管理列表、工具栏与滑动手势。 |
| `src/pages/rom_browser/` | `RomBrowserViewModel` 管理当前目录、文件条目和浏览状态，通过 ROM 库执行导入；`RomBrowserView` 管理文件选择器。 |
| `src/pages/game/` | `GameViewModel` 持有唯一模拟器并管理运行、按键、帧与异步存档命令；`GameView` 管理游戏布局、触摸线程、输入位图和画面资源。 |
| `src/pages/settings/` | `SettingsViewModel` 管理 P1 映射、按键捕获和保存反馈；`SettingsView` 管理映射表格及状态绑定。游戏页读取同一份映射。 |
| `src/pages/help/` | `HelpViewModel` 提供 ROM 与存档目录；`HelpView` 管理说明内容及独立滚动区域。 |
| `src/pages/nes/` | `NesViewModel` 组合页面 ViewModel 并提供应用级协调；`NesView` 组合页面 View，管理导航、错误弹窗、keypad 订阅和轮询定时器。 |
| `src/components/` | 提供共享语义颜色、页面和控件构造函数。 |
| `src/app.cpp` | 接入 AppKit 生命周期，保证 View 在 ViewModel 之前销毁。 |
| `third_party/fceumm/` | 锁定提交版本的 FCEUmm Git 子模块，保留完整上游源码和许可声明。 |
| `third_party/FCEUMM_ORIGIN.md` | 父仓库维护的 FCEUmm 来源、子模块初始化方式与 GPL-2.0-or-later 许可记录。 |

构建将不依赖 LVGL 的 ROM 和模拟器代码放入 `lilygo_ui_nes_core`，将全部 ViewModel 放入
`lilygo_ui_nes_view_models`；后者只链接 core，不链接 LVGL。页面 View、组件和平台触摸适配
位于 `lilygo_ui_nes_ui`，通过 ViewModel target 使用核心能力。`nes-view-model-tests` 只链接
ViewModel target，可在不初始化 LVGL 的情况下测试页面命令和状态。
展示状态在命令完成或定时轮询后同步到控件；界面不计算或显示 ROM 数量。

## 线程与生命周期

核心及当前适配包含全局状态，只允许一个活动模拟器实例。
`app_open()` 创建 `NesViewModel` 和 `NesView` 容器，再由容器组合 ROM 库、文件选择器、
设置、帮助和游戏页各自的 ViewModel 与 View。每个页面 View 拥有自己的根对象，
`create()` 重复调用返回已有根对象，`destroy()` 可重复调用。页面通过显隐切换，
不为每次导航或运行重新创建。所有页面 View 必须先于其 ViewModel 销毁。

- 启动游戏时，`NesEmulator` 重置帧缓冲和按键状态，再启动模拟器线程。
- 模拟器视频回调发布 `256 x 240` RGB565 数据，前后缓冲交换和主线程复制由互斥锁保护，
  序号避免重复复制未更新的帧。
- 模拟线程按单调时钟的绝对期限休眠。
  自动模式每次最多补算两帧，并显示最后一帧，避免负载过高时持续跳过画面；每帧先处理输入。
- FCEUmm 加载后使用核心报告的区域帧率设置计时器，按 PAL/NTSC 等 ROM 区域信息运行，
  在模拟线程内调用 `retro_run()` 并处理视频与音频回调。普通 iNES 的 PAL 识别主要依赖
  文件名中的 `(E)`、`(Europe)`、`(PAL)` 等标记；NES 2.0 使用头部 byte 12 的区域字段。
  普通 iNES 的 byte 9 标志或列表中的 PAL 标签本身不足以保证核心选中 PAL 时序。
- 设备音频通过有界 PCM 队列交给独立写线程；模拟线程不等待播放器消费管道数据。
- Save / Load 请求交给模拟线程，在帧边界调用 FCEUmm 的 `retro_serialize()` /
  `retro_unserialize()`，不得与 `retro_run()` 并发执行；UI 定时轮询结果并更新游戏页工具栏。
  处理期间禁用存档按钮，操作成功或失败均显示结果，普通存档错误不终止游戏。
- 停止命令只设置原子标志，模拟器在输入轮询中请求正常退出。ViewModel 进入
  `stopping` 状态，LVGL 定时器等待结束通知后回到 ROM 库或显示退出错误。
- 应用关闭时，`NesView` 先删除轮询和错误弹窗定时器并注销 keypad 监听；`GameView` 停止并
  等待触摸线程、请求停止游戏、释放按键，再删除游戏控件和画面资源。各页面分别删除自己的
  LVGL 根对象并清空缓存指针，最后删除容器根对象。随后销毁 `NesViewModel` 及页面 ViewModel，
  由 `GameViewModel` 持有的模拟器析构函数等待模拟器线程退出并关闭音频输出。

不得让后台线程直接回调已销毁的页面，也不得在关闭页面时留下按住的方向键或 A/B 状态。

## ROM 库与文件状态

`RomLibrary::default_search_paths()` 只返回运行用户的 `$HOME/nes/rom`，不再使用
`NES_ROM_DIR` 或其他设备目录。旧目录内的 ROM 不会自动迁移，用户可从文件选择器导入。
`HOME` 必须是非空绝对路径；未设置、为空或为相对路径时提示目录配置错误，不回退到其他位置。
应用在启动和刷新游戏库时创建缺失的 `$HOME/nes/rom` 目录。

扫描只读取该目录当前层级的普通 `.nes` 文件，扩展名不区分大小写。
最多收集 128 个条目，再按标题排序。ROM 库在横竖屏均显示全宽单列列表，
Add ROM 位于列表最后一项并随列表滚动，列表下方固定工具栏提供刷新、设置和帮助；
没有摘要面板或目录数量统计。无 ROM 时列表中仅保留 Add ROM，不显示空库说明卡片或 Ready 状态。
帮助页显示由运行用户 `$HOME` 展开的完整 ROM 和存档目录路径，并在独立滚动区域中说明导入、
手动复制后刷新、点击启动、左滑直接删除磁盘文件、按键映射、Save / Load 和游戏控制，Back 返回游戏库。
无效 iNES 头的文件仍显示在库中并标记错误，启动命令会拒绝它。

ROM 列表最后一项 **Add ROM** 打开应用内文件选择器，从用户主目录开始，可进入根目录、
返回上一级或取消。列表只展示目录和 `.nes` 文件；选择文件后校验 iNES 头，按原文件名
复制到 `$HOME/nes/rom`，按需创建目标目录，保留源文件且拒绝覆盖同名目标。
目标目录已有 128 个 `.nes` 文件时拒绝导入并提示游戏库已满。
成功后刷新库并关闭选择器；校验或文件系统错误保留在选择器中。
导入只复制 ROM。电池存档和手动即时存档统一保存在 `$HOME/nes/states`，目录按需创建，
使用完整 ROM 文件名加后缀：`game.nes` 对应 `game.nes.sav` 和 `game.nes.state`。
每个 ROM 只有一份手动即时存档，Save 覆盖，Load 显式读取；启动游戏不会自动读取即时存档。
写入通过临时文件和原子重命名保留上一份完整文件，读取即时存档时校验格式、校验和与 ROM 身份。
电池存档继续在加载游戏时读取、正常停止时写出。旧目录和旧存档的手动迁移方式见
[ROM 与运行配置](02-roms-and-runtime.md#battery-ram-and-save-states)。

当前头校验仅检查 16 字节长度和 `NES\x1a` 签名，并不保证完整 ROM 数据或 mapper 能运行。
实际加载与兼容性由 FCEUmm 判断，错误写入标准错误并通过退出状态回到 UI。
FCEUmm 可加载其上游支持的 NES 2.0 ROM，但列表仍按经典 iNES 字段展示元数据，
扩展 Mapper 和容量等信息可能不完整；游戏库仍仅扫描 `.nes` 文件。
FCEUmm 适配层限制 ROM 文件最大为 64 MiB，并检查文件是否容纳头部声明的 PRG、CHR
及可选 512 字节 trainer，包含 NES 2.0 的指数/乘数容量编码；截断文件会在传入核心前拒绝。

删除命令直接删除对应 ROM 文件，成功后重新扫描；删除失败时保留当前列表并显示文件系统
错误；对应的电池存档和即时存档保留。运行中或停止中的游戏会阻止导入和删除。
刷新按钮在游戏页隐藏，ROM 目录不进行实时监听。

## 输入、画面与音频

LVGL 的按下、释放和失去按压事件生成一份按键位图；Linux 触摸快照根据控件当前边界
生成另一份位图。两份位图按位合并后，只将变化的按钮状态交给模拟器，支持方向键与 A/B
同时按住。无法读取直接触摸设备时保留普通 AppKit 指针输入；其他平台不启动 evdev 线程。

ROM 库的设置页显示 P1 映射。点击动作行后按下一个键即可替换绑定；已被其他动作占用的
按键会被拒绝并提示重新选择。映射保存到 `$XDG_CONFIG_HOME/lilygo-ui-nes/keymap.conf`，
未设置该变量时保存到 `$HOME/.config/lilygo-ui-nes/keymap.conf`。P2 不参与输入映射。

竖屏时画面位于上方，方向键和 A/B 位于下方；横屏时方向键、画面和 A/B 从左至右排列。
画面保持 NES 原生 `256:240` 比例，`GameView::apply_layout()` 更新现有控件及触摸坐标映射，
不重启模拟器。

| 构建 | 音频输出 | 格式 |
| --- | --- | --- |
| `host-simulator` | SDL2 默认输出设备 | 48 kHz、16 位有符号本机字节序、单声道 |
| CM0 设备预设 | `aplay` 的 ALSA `default` 设备 | 48 kHz、S16_LE、单声道 |

设备采样率适配 CM0 ES8389 编解码器的 19.2 MHz 主时钟。SDL2 队列在启动前缓冲两帧音频，
队列超过约 100 ms 时丢弃积压样本，避免画面停顿后持续播放过期声音。
音频初始化或输出错误通过标准错误排查。主机 SDL2 音频设备初始化失败时，
模拟器仍可继续生成画面。

## 失败策略

- ROM 目录不存在时自动创建，创建失败时显示文件系统错误，修复权限或路径后可刷新重试；无 ROM 时列表中仅保留 Add ROM。
- 无效 iNES 头保留在列表中并拒绝启动；启动线程失败时显示启动错误。
- 模拟器非零退出码显示为错误状态；正常停止后返回 ROM 库并清除按键状态。
- 删除失败时显示具体文件系统错误，不提前移除条目。
- 导入失败时保留文件选择器，显示校验、同名冲突或文件系统错误，不覆盖已有文件。
- 直接触摸节点不可读时使用 AppKit 普通输入；组合触控需要可用的 Linux 多点触摸节点。
- SDL2 或 ALSA 输出不可用时在标准错误报告原因，音频排查步骤见
  [ROM 与运行配置](02-roms-and-runtime.md)。

## 验收

在仓库根目录配置、编译并运行主机测试：

```sh
cmake --preset host-simulator
cmake --build --preset host-simulator --parallel
ctest --preset host-simulator
```

也可使用 `lpm test` 执行上述流程。

| 测试 | 覆盖内容 |
| --- | --- |
| `nes-core` | ROM 扫描、目录浏览、导入校验与同名冲突、标题和 iNES 元数据、删除成功与失败。 |
| `nes-frame` | 预缩放的颜色、采样位置、重复行、尺寸变化和缓冲复用。 |
| `nes-timing` | 休眠等待、绝对期限、积压计数、关闭唤醒和时钟重启。 |
| `nes-audio` | SDL2 采样数、音频资源所有权、输出失败恢复和实际 PCM 文件输出。 |
| `nes-view-model` | 空库、无效 ROM、导入与删除错误、运行中和停止时的文件操作保护。 |
| `nes-render-portrait` | `568 x 1232` 下的 ROM 库、文件浏览和导入、游戏运行、组合按键、长标题和删除手势。 |
| `nes-render-landscape` | `1232 x 568` 下的相同渲染与交互流程。 |

渲染测试自行生成最小 NROM，不需要用户 ROM；音频测试使用 SDL2 虚拟设备和文件输出，
渲染测试使用 `dummy` 音频驱动，不依赖扬声器。截图输出到 `build/host-simulator`，
包含 `nes-{portrait,landscape}.ppm`、`nes-{portrait,landscape}-library.ppm`、
`nes-{portrait,landscape}-library-end.ppm`、
`nes-{portrait,landscape}-browser.ppm`、`nes-{portrait,landscape}-delete.ppm`、
`nes-{portrait,landscape}-help.ppm`、`nes-{portrait,landscape}-settings.ppm`、
`nes-{portrait,landscape}-error.ppm` 和 `nes-{portrait,landscape}-empty.ppm`，共十八张。

UI 修改后必须按 [UI 开发规范](01-user-interface.md) 检查横竖屏产物。自动化测试不能
替代设备上对触摸节点权限、真实多指操作和 ALSA 声音的检查。涉及 SDK、目标依赖或打包
变化时，还应执行 `cm0-cross-gles` 构建和 Debian 打包验证。
