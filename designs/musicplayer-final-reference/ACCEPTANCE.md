# 原生 Qt 界面重构验收

实施与验收时 HEAD 为 `651f0fb48c7af35b36a1a52bd4638e1835e610ff`。开始时只有用户提供的 `musicplayer-final-reference/` 未跟踪；任务书和参考图未修改。验收完成时源码修改留在工作区，没有提交或推送；随后用户明确授权将本轮代码和验收资料提交并推送，提交身份以 Git 日志为准。原有 `designs/musicplayer/` 不变，`audio_decoder.cpp`、数据库、后台扫描与 `third/` 无修改。

## 实现与取舍

- 原生 Qt Widgets 布局：76px 封面/缺省音符、滚动标题与完整 tooltip、独立歌手信息、进度及当前/总时间。未知时长显示 `--:--`。
- 蓝色为主按钮和当前播放强调色。播放/暂停 44px，其余控制次级；四种模式仍由一个按钮循环，tooltip 和 accessibleName 同步。保留停止。
- 音量采用同一控制行最右侧 14px 图标、44px 短滑杆，保留鼠标、滚轮、键盘和原持久化。
- 下方只有“播放列表 / 歌词”，切换不停止播放；无歌词显示已有空态。添加/管理与标签同一行。树、多选、右键菜单、Ctrl+M、托盘和关闭逻辑继续复用。
- 歌单管理保留原双栏列表、中央复制/移动/移除及取消/应用。现有歌单条目显示数量；没有修改时返回不访问写事务。
- 未加入收藏、年份、搜索和行末更多占位按钮，没有新增字段。列表只有现有文件名和时长，歌手仅展示播放器实际收到的元数据。暂停仍使用统一蓝色主按钮；未复制海报的大尺寸、阴影或虚构数据。

## 删除及播放路径

删除 `spectrum_widget.cpp/.h`、`spectrum_processor.cpp/.h`、`fftreal.h`、FFT 许可部署项，去除 CMake 源文件、频谱线程、渲染定时器、包转发信号、频谱启动/停止调用及仅用于频谱的 seek_result 状态。

旧：播放器就绪 → 频谱 reset_and_start → 频谱就绪 → 解码；seek 后也重新启动频谱。

新：播放器就绪 →（存在恢复位置则先 seek）→ resume_decoding；decoder seek 完成 → player handle_seek → 待处理 seek 优先 → 按暂停状态恢复 feeding → resume_decoding。恢复解码检查会话、媒体仍加载且不在 seek；seek 回调也检查正在 seek，旧会话和停止后回调被忽略。低水位补充解码保持原路径。

`audio_player::played_packets_` 仍负责把内存最终释放移出 SDL 回调，不能随频谱一起删除。只移除 `packet_played` 信号，播放器线程消费并释放包；回归继续验证实际释放线程。另删除播放器内无接收者的歌词状态信号、重复歌词缓存/逐行查询、频谱几何和失效标题宽度刷新；歌词页继续使用唯一的 lyrics_widget。

## 已复现并修复

1. `music_management_dialog::on_source_select_all_clicked`：200 首全选发出 200 次 itemChanged，每次扫描整表。新增测试先失败；QSignalBlocker 后批量更新仅显式刷新一次，通知为 0，勾选和按钮状态正确。
2. `on_done_button_clicked`：没有暂存更改仍尝试写事务。测试先占用事务，原实现报 cannot start a transaction within a transaction，无法返回；现在直接发出返回信号，不触碰事务。存在修改的原子提交及失败留页路径不变。
3. `playlist_window::setup_ui`：QTreeWidget 默认 stretchLastSection 抢占最后一列，窄窗歌名过早省略。截图发现后新增列宽回归先失败；关闭末列拉伸。时长列另留出加粗和 QSS 内边距，防止当前歌曲时长被省略。

## 构建与自动回归

Linux、GCC 16.2.0、C++17、Qt 6.2.4，Debug。复用 `/tmp/musicplayer-round2-build` 和 `/tmp/musicplayer-round2-sanitize`。

- 修改前基线 30/30，通过，21.47 秒。
- 修改后完整 34 项，保留原 30 项；新增批量选择、无修改返回、旧会话就绪/seek 回调、列表/歌词切换及未知总时长检查。
- 全量包含正常播放、初始/暂停/连续 seek、快速切歌、停止/EOF/退出、释放线程、无效文件、MP3/FLAC/WAV/AAC、四模式边界、数据库触发器故障注入、异步导入等既有回归。
- 最终 GCC 全量 34/34 通过（24.28 秒）；ASan/UBSan 全量 34/34 通过（29.02 秒）。原始结果见 `acceptance/tests.log` 与 `acceptance/sanitizer.log`。ASan 开启 detect_leaks=1:halt_on_error=1，UBSan 开启 halt_on_error=1:print_stacktrace=1。预编译 Qt/FFmpeg/SDL 并非完整插桩。

```sh
cmake --build /tmp/musicplayer-round2-build -j2
ctest --test-dir /tmp/musicplayer-round2-build --output-on-failure -j1
cmake --build /tmp/musicplayer-round2-sanitize -j2
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 ctest --test-dir /tmp/musicplayer-round2-sanitize --output-on-failure -j1
```

## 真实 Qt 显示与操作

使用实际项目 QWidget/控制器对象生成截图（链接测试目标的相同项目对象），不是 HTML。独立 Xvfb `:97`、xcb、隔离 DBus/用户数据、SDL dummy，1×/2×；未操作前台桌面。截图输入含本地 MP3，实际执行播放、暂停/恢复、seek、鼠标音量、模式切换、管理进出、全选、最小化/恢复及无托盘关闭，保存位置/模式/音量。自动回归另验证取消/应用、键盘音量和页面切换不中断进度。

`actual-track.png` 使用真实文件及解析结果；其他截图为视觉夹具：主动注入代表性标题、歌词、错误和以应用图标编码的封面，未知时长为 UI 状态夹具。不能把这些注入内容当成媒体解析测试。无封面/真实文件、播放、暂停、长标题、空列表、错误、歌词、管理及窄窗 2× 截图见 acceptance。360×480、340×396、500×650；管理 800×480 和720×460，探针可见按钮越界列表均为空。

本轮未全面人工操作文件选择器、右键编辑/删除、拖拽多选、真实窗口管理器拖动及真实托盘。没有宣称模拟托盘信号等于真实托盘测试。Windows、macOS、Wayland、读屏器、实际音频输出与听感、TSan、长期播放未验证。

## 资源短测

相同 Debug 构建、Xvfb、4 分钟 WAV、SDL dummy；每状态预热后测量 5 秒，CPU 为进程全部线程 CPU 时间/墙钟时间（单核百分比），RSS 为进程生命周期峰值，非播放阶段单独峰值。

| 状态 | 修改前 CPU / 峰值 RSS | 修改后 CPU / 峰值 RSS |
|---|---|---|
| 空闲 | 0.344% / 122.4 MiB | 0.171% / 123.2 MiB |
| 播放 | 1.704% / 129.5 MiB | 0.578% / 130.2 MiB |

用于排除明显退化，不代表稳定性能收益或长期无泄漏；未新增绘制定时器。最终调整仅涉及列宽，不影响此空歌单采样负载。

## 变更文件

界面：`player_window.cpp/.h`、`playlist_window.cpp`、`music_management_dialog.cpp`、`lyrics_widget.cpp`、`volumemeter.cpp`、`style/stylesheet.qss`、控制 SVG 和新增 `icons/volume.svg`、`resources.qrc`。

频谱移除：`playback_controller.cpp/.h`、`audio_player.cpp/.h`、上述删除文件、`CMakeLists.txt`、`packaging/Packaging.cmake`、`packaging/THIRD_PARTY_NOTICES.md`。

验证：`tests/playback_tests.cpp`、本报告及 acceptance 中必要截图/最终日志。实验构建和中间截图在 `/tmp/musicplayer-final/`，不纳入项目交付。
