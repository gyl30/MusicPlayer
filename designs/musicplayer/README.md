# MusicPlayer UI/UX 实现与验收

提交说明：用户在验收记录完成后授权本地提交；本次将源码、原型、截图和测试证据一并纳入提交，不推送。下文各阶段的未提交说明记录当时状态。

## 当前布局调整

音量已移至播放控制下方右侧，播放按钮组居中；“添加音乐”和“管理歌单”使用完整文字，集中放在歌曲列表上方。进入管理页后这两个入口随播放页隐藏，返回通过已有取消/应用按钮或 Ctrl+M。

窄窗口测试确认此前固定最小高度会导致控件重叠：播放器实际 160 高、布局需要 180，音量与歌词区域相交。新增几何断言先失败再通过；删除固定最小高度，使用 Qt 的 SetMinimumSize 布局约束。默认仍请求 360×480；本次封面/歌词夹具的实际最小尺寸为 340×471，随字体和内容计算，不硬编码 471。

本轮 GCC 全部 30/30 通过（22.99 秒）；HTML 无头 Chrome 交互及 HTTP 检查通过；Xvfb 1×/2× 截图验证完成。仅布局调整，没有重复执行上一轮的 Clang、ASan/UBSan 和性能采样，其结果不代表本轮重新测量。

最新截图与日志：`previews/placement-*`。当前 HTTP 截图页为 `comparison.html`，上一版保留 `comparison-v2.html`；当前交互原型为 `index.html`，上一版保留 `index-v2.html`。没有提交或推送。

## 上轮精简版

根据用户“简单、易操作”的反馈，参考 Nulloy、Audacious 与 fooyin 的官方资料及源码，完成第二轮精简。[参考与取舍](references.md)。

当前 Qt 界面删除标题装饰图标、播放器/列表卡片边框及独立添加底栏；添加和管理集中在顶栏。音量由分段柱改为细线，24 像素操作区域保留。主窗口默认 360×480，管理页 800×480；原最小尺寸不变。

本轮 GCC CTest 30/30（22.67 秒）、ASan/UBSan 30/30（27.20 秒）；独立 Xvfb 1×/2× 截图按钮无越界，HTML 无头 Chrome 交互检查通过。未重新执行 Clang 全量或资源采样，下文对应数据属于首版。所有代码仍未提交、未推送。

截图及本轮日志见 `previews/simple-*`；原型 `index.html` 已同步，首版保留为 `index-v1.html`。

以下记录首版实现及完整验收依据。

基线：`0795e1b80ba6a541d980009f5b4db40804b1cf8e`，开始时工作区干净。未提交、未推送；`audio_decoder.cpp` 无改动。

## 设计依据与取舍

按 baoyu-design 的 SKILL、system-prompt、Codex、hi-fi、interactive-prototype、frontend-design 指引，先阅读实际 Qt 源码，再制作并验证原型。目录由用户指定；没有已有设计系统，使用现有组件语义，不引入外部 UI 库。

优先级：

1. 操作目标：原播放按钮约 28×22，音量区 68×10，难以点击；管理入口隐藏在右键和 Ctrl+M。
2. 阅读：高亮频谱叠在标题/进度背后，强渐变与边框过多；时钟自绘数码管占据较多宽度。
3. 歌单编辑：删除、应用之间缺少清楚的视觉间隔；无明确放弃待应用内容的入口。
4. 紧凑布局：隐藏管理页影响主窗口最小尺寸；正常主窗口实测宽 386，超过代码请求的 298。

选择单一浅色方向：背景 `#F5F6F3`、正文 `#24332F`、强调 `#176D58`。保留原生树形歌单与双栏管理，不引入新的导航架构。主窗口默认 360×540、下限 340×400；管理页默认 800×540、下限 720×460。扩大操作区域意味着不再强求 298×450。

## 已落实到 Qt

- 重写 QSS，删除强渐变、重复装饰与旧字号；统一 SVG 的配色，保留应用原标志。
- 播放按钮 44×44，普通播放控制按钮至少 32×34；进度条和音量区域高 24。
- 标题、当前时间、封面沿用现有数据；频谱移到独立的底部区域，不覆盖文字。
- 普通 QLabel 替代七段数码管显示，删除已无调用的 `digital_time_label.cpp/.h` 及构建引用。
- 顶栏增加管理入口；底部增加添加音乐入口和 Qt 自带 QSizeGrip。添加入口复用已有文件选择/导入逻辑。
- 树形缩进恢复可见，正在播放使用加粗及青绿色，选中/悬停/焦点/禁用分别定义。
- 管理页增加歌曲区域占比、扩大留白，将移除与应用分开；按钮文字说明移除不会删除文件。
- 未勾选歌曲或源目标相同时禁用不适用的操作；应用按钮文字反映是否存在待应用更改。
- 新增“取消更改并返回”：重新载入管理页，丢弃暂存内容，不写数据库。成功保存继续使用原有原子提交路径，失败反馈沿用现有逻辑。
- 音量支持 Tab 聚焦、方向键每次 5、Home/End，以及原有鼠标/滚轮；仍走现有音量信号与持久化。
- 歌词颜色统一，删除过淡的边缘透明度；限制绘制范围，修复 2× 缩窗时出现的越界细线残影。
- 未新增定时器、线程、缓存或 UI 框架。播放内核、解码器、数据库、后台扫描及 third/ 均未修改。

主要变更：`style/stylesheet.qss`、`player_window.cpp`、`playlist_window.cpp`、`music_management_dialog.cpp/.h`、`lyrics_widget.cpp`、`volumemeter.cpp/.h`、相关 SVG、`tests/playback_tests.cpp`；CMake 仅删除数码管源文件并注册两个新测试。

## 原型

[打开原型](index.html) · [实际 Qt 截图对比](comparison.html)。HTTP 预览：<http://localhost:4312/musicplayer/index.html>。

```sh
python3 -m http.server 4312 --bind 127.0.0.1 --directory designs
```

纯 HTML/CSS/JS，无运行时外部资源。支持播放/暂停、切歌、模式按钮、进度、音量、选中/双击、管理页复制/移动/移除/应用/取消，以及空歌单、错误、长文件名状态选择。使用代表性中文歌名，不含真实音频或下载功能。

**仅原型内容**：上下分开的总时长、状态展示选择器、管理页下方说明条、样例歌词文案，以及空歌单时禁用播放按钮。原型的模式按钮和切歌只演示界面，不模拟正式播放器的随机队列与播放状态机。Qt 保留原有模式语义、空歌单播放处理和失败策略。

## 可复查证据

- `previews/before-*.png`：修改前真实 Qt 窗口。
- `previews/after-*.png`：修改后 Qt 窗口，含空列表、长标题、正常标题、播放、错误、窄窗、管理页。
- `previews/hidpi-*.png`：2× Qt 显示；`hidpi-clipping-before.png` 保留修复前残影。
- `previews/prototype*.png`：无头 Chrome 的 HTML 截图。
- `previews/*tests.log`：基线及最终 CTest 原始日志。
- `previews/cancel-red.log`：新增取消测试在实现前失败（未找到取消按钮）。
- `previews/keyboard-red.log`：音量键盘测试在修复前失败（右方向键后仍为 50，而非 55）。
- `previews/qt-preview.log`、`qt-hidpi.log`：尺寸、按钮边界、最小化、关闭与设置保存的探针输出。
- `qt-preview.cpp`：实际项目 QWidget 的截图探针，链接现有测试目标的项目对象；不是 HTML 重现。

基线及最初一轮 Qt 截图使用了当前 X11 桌面；用户要求后台验证后，所有 GUI 操作改用独立 Xvfb `:97`，浏览器用独立无头 Chrome。最终 Qt 探针也隔离 DBus，避免访问桌面托盘。未再使用前台鼠标。

## 实际验证范围

环境：Linux X11 / Qt 6.2.4，GCC Debug、Clang RelWithDebInfo；ASan/UBSan 为 GCC Debug。复用既有构建目录与 CTest，不增加框架。

- 修改前：28/28 CTest。
- 修改后：30/30 CTest，原 28 项保留；新增 `management_cancel` 和 `ui_controls`。后者验证紧凑窗口按钮边界、播放按钮尺寸、四种模式循环、可见管理入口及音量键盘控制。
- ASan/UBSan：30/30，启用泄漏检查与遇错停止。预编译第三方库不包含完整插桩；TSan 未执行。
- HTML：Chrome 无头测试暂停、管理复制/取消、390px 宽页面无横向溢出，运行时异常列表为空，并检查截图。
- Qt：1×/2×，340×400、360×540、500×650、720×460 和 800×540；截图探针按钮越界列表为空。观察封面、长中英文标题、歌词、错误文本、空歌单和管理页。
- Qt 探针实际加载本地 WAV，执行播放、暂停、恢复、seek、鼠标调音量、模式切换、管理页进入/返回、最小化/恢复、关闭、设置保存；原有回归另覆盖快速切歌、播放退出、格式、导入、模式边界和事务故障注入。
- 真实独立可执行文件另外在 Xvfb 启动并播放用于资源采样，日志确认进度增加。音频后端为 dummy，本轮不验证声卡与听感。

未把截图中手工注入的代表性 metadata、封面、歌词、错误文本当作新增媒体解析测试。未把调用托盘信号当作真实托盘点击。未全面重复右键菜单、文件选择器、多选拖动、真实窗口管理器拖动/最小化、原生 Wayland、Windows/macOS、读屏器与系统字体缩放的人工验收；这些属于后续验证范围。当前虚拟环境中无真实托盘，关闭被接受；此前桌面初轮观察到有托盘时关闭隐藏。两者范围分开记录。

## 资源抽测

| 状态 | 修改前 CPU / RSS | 修改后 CPU / RSS |
|---|---|---|
| 空闲 | 0.4% / 132.05 MiB | 0.2% / 125.48 MiB |
| 播放 | 2.8% / 138.49 MiB | 2.0% / 132.41 MiB |

见 `previews/performance.json`。相同 Clang 构建类型、独立 Xvfb、相同 4 分钟 WAV 和隔离歌单，SDL dummy。每状态预热后采样 5 秒，CPU 为占单个核百分比；RSS 为该窗口内采样峰值，不是整个生命周期峰值。没有新增持续装饰动画。

此次短时抽测仅用于发现明显退化，不能推出稳定性能提升或长时间无泄漏。UI 尺寸变化及系统调度都会影响结果。最终数值以 JSON 为准。

## 本地复验

```sh
cmake --build /tmp/musicplayer-round2-build -j2
DISPLAY=:97 ctest --test-dir /tmp/musicplayer-round2-build --output-on-failure -j1
cmake --build /tmp/musicplayer-round2-sanitize -j2
DISPLAY=:97 ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 ctest --test-dir /tmp/musicplayer-round2-sanitize --output-on-failure -j1
```

上述路径是本机已有构建目录；其他环境按仓库 README 配置。Xvfb 须单独启动；截图探针拒绝在非 `:97` 显示或非 `/tmp/musicplayer-d/` 数据目录运行。验收未自动 commit 或 push。
