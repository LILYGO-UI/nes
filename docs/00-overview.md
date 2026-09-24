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
ROM 目录 -> RomLibrary 扫描和 iNES 元数据 -> NesViewModel -> ROM 库页面
                                                |
触摸控件 -> NesView -> 按键位图 -> NesEmulator -> FCEUmm CPU / PPU / APU
               ^                    |                    |
Linux 触点快照 -+                    |                    +-> 音频输出
                                    |
LVGL Image <- 预缩放 XRGB8888 <- 主线程帧副本 <- RGB565 双缓冲 <- 256 x 240 模拟器帧
```

ROM 扫描和列表刷新由主线程执行，模拟器和设备触摸读取各自使用工作线程。
工作线程不得创建或修改 LVGL 对象。`NesView` 使用 16 ms 的 LVGL 定时器读取触点快照、
复制最新完整帧，并处理模拟器结束状态。
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
| `src/domain/rom_library.*` | 搜索目录、解析 iNES 头、生成标题和元数据、计算 CRC32、删除 ROM 文件；不依赖 LVGL。 |
| `src/emulator/nes_emulator.*` | 管理工作线程、视频、音频、输入和计时接口。 |
| `src/emulator/fceumm_core.*` | 通过 FCEUmm libretro API 加载 ROM，处理区域时序、输入、RGB565 视频、音频和电池 RAM。 |
| `src/emulator/nes_performance.*` | 可选的分阶段耗时统计，由 UI 线程每秒报告。 |
| `src/platform/multitouch_reader.*` | 在 Linux 上读取 evdev 触点，将原始坐标映射到显示坐标，发布加锁的触点快照。 |
| `src/pages/nes/nes_view_model.*` | 管理 ROM 列表、选中游戏、运行与错误状态；提供刷新、启动、停止和删除命令。 |
| `src/pages/nes/nes_view.*` | 创建响应式 LVGL 控件，处理点击和滑动，合并输入并显示完整帧。 |
| `src/components/` | 提供共享颜色、页面和控件构造函数，以及 Subject 的 RAII 封装。 |
| `src/app.cpp` | 接入 AppKit 生命周期，保证 View 在 ViewModel 之前销毁。 |
| `third_party/fceumm/` | 锁定提交版本的 FCEUmm Git 子模块，保留完整上游源码和许可声明。 |
| `third_party/FCEUMM_ORIGIN.md` | 父仓库维护的 FCEUmm 来源、子模块初始化方式与 GPL-2.0-or-later 许可记录。 |

构建将不依赖 LVGL 的 ROM 和模拟器代码放入 `lilygo_ui_nes_core`，将页面、组件和
平台触摸适配放入 `lilygo_ui_nes_ui`。界面通过 ViewModel 使用核心能力；ROM 数量使用
Subject 绑定，其他状态由命令完成或定时轮询后同步到控件。

## 线程与生命周期

核心及当前适配包含全局状态，只允许一个活动模拟器实例。
`app_open()` 创建一个 ViewModel 和一个 View；ROM 库与游戏页是同一个 View 内的
两个面板，通过显隐切换，不为每次运行重新创建页面。

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
- 停止命令只设置原子标志，模拟器在输入轮询中请求正常退出。ViewModel 进入
  `stopping` 状态，LVGL 定时器等待结束通知后回到 ROM 库或显示退出错误。
- 页面关闭时，View 先删除 LVGL 定时器、停止并等待触摸线程、请求停止游戏、释放按键，
  再删除 LVGL 根对象。随后销毁 ViewModel，其模拟器析构函数等待模拟器线程退出，
  关闭音频输出。

不得让后台线程直接回调已销毁的页面，也不得在关闭页面时留下按住的方向键或 A/B 状态。

## ROM 库与文件状态

`RomLibrary::default_search_paths()` 将非空 `NES_ROM_DIR` 放在搜索列表首位，随后加入
用户主目录和设备常见 ROM 目录。默认列表按相同路径字符串去重；它不解析等价路径，
也不按 CRC32 去重 ROM。

扫描会访问列表中存在的目录，只读取当前层级的普通 `.nes` 文件，扩展名不区分大小写。
最多收集 128 个条目，再按标题排序。界面显示的目录是搜索列表首项，无论该目录是否存在。
无效 iNES 头的文件仍显示在库中并标记错误，启动命令会拒绝它。

当前头校验仅检查 16 字节长度和 `NES\x1a` 签名，并不保证完整 ROM 数据或 mapper 能运行。
实际加载与兼容性由 FCEUmm 判断，错误写入标准错误并通过退出状态回到 UI。
FCEUmm 可加载其上游支持的 NES 2.0 ROM，但列表仍按经典 iNES 字段展示元数据，
扩展 Mapper 和容量等信息可能不完整；游戏库仍仅扫描 `.nes` 文件。
FCEUmm 适配层限制 ROM 文件最大为 64 MiB，并检查文件是否容纳头部声明的 PRG、CHR
及可选 512 字节 trainer，包含 NES 2.0 的指数/乘数容量编码；截断文件会在传入核心前拒绝。

删除命令直接删除对应 ROM 文件，成功后重新扫描；删除失败时保留当前列表并显示文件系统
错误。运行中或停止中的游戏会阻止删除。刷新按钮在游戏页隐藏，ROM 目录不进行实时监听。

## 输入、画面与音频

LVGL 的按下、释放和失去按压事件生成一份按键位图；Linux 触摸快照根据控件当前边界
生成另一份位图。两份位图按位合并后，只将变化的按钮状态交给模拟器，支持方向键与 A/B
同时按住。无法读取直接触摸设备时保留普通 AppKit 指针输入；其他平台不启动 evdev 线程。

竖屏时画面位于上方，方向键和 A/B 位于下方；横屏时方向键、画面和 A/B 从左至右排列。
画面保持 NES 原生 `256:240` 比例，`apply_layout()` 更新现有控件及触摸坐标映射，
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

- 搜索目录不存在或无法访问时跳过该目录；无 ROM 时显示空列表状态。
- 无效 iNES 头保留在列表中并拒绝启动；启动线程失败时显示启动错误。
- 模拟器非零退出码显示为错误状态；正常停止后返回 ROM 库并清除按键状态。
- 删除失败时显示具体文件系统错误，不提前移除条目或修改计数。
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
| `nes-core` | ROM 扫描、标题和 iNES 元数据、删除成功与失败。 |
| `nes-frame` | 预缩放的颜色、采样位置、重复行、尺寸变化和缓冲复用。 |
| `nes-timing` | 休眠等待、绝对期限、积压计数、关闭唤醒和时钟重启。 |
| `nes-audio` | SDL2 采样数、音频资源所有权、输出失败恢复和实际 PCM 文件输出。 |
| `nes-view-model` | 空库、无效 ROM、删除错误、运行中和停止时的删除保护。 |
| `nes-render-portrait` | `568 x 1232` 下的 ROM 库、游戏运行、组合按键、长标题和删除手势。 |
| `nes-render-landscape` | `1232 x 568` 下的相同渲染与交互流程。 |

渲染测试自行生成最小 NROM，不需要用户 ROM；音频测试使用 SDL2 虚拟设备和文件输出，
渲染测试使用 `dummy` 音频驱动，不依赖扬声器。截图输出到 `build/host-simulator`，
包含 `nes-{portrait,landscape}.ppm`、`nes-{portrait,landscape}-library.ppm` 和
`nes-{portrait,landscape}-delete.ppm`，共六张。

UI 修改后必须按 [UI 开发规范](01-user-interface.md) 检查横竖屏产物。自动化测试不能
替代设备上对触摸节点权限、真实多指操作和 ALSA 声音的检查。涉及 SDK、目标依赖或打包
变化时，还应执行 `cm0-cross-gles` 构建和 Debian 打包验证。
