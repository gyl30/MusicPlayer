# MusicPlayer 独立验收记录

验收时间：2026-10-25 至 2026-10-26。基线：
`73426a2790eb2afb2cf1411b4fae7db8f652b527`。

已阅读 `review-2026-10-25-followup.md`，完整复核 `60379c3`、`a419cda`、
`73426a2` 的 diff。未重新设计播放、事务或扫描架构。本轮没有推送，
`audio_decoder.cpp` 的用户字符集补丁与开始时备份逐字节一致，未纳入提交。

## CONFIRMED

未发现新的 P0/P1 缺陷。以下两项为 P2，均已处理。

### 无系统托盘时，关闭窗口会隐藏并失去恢复入口

- 路径：`tray_icon.cpp:41` → `playlist_window.cpp:254`。
- 原实现只返回 `QSystemTrayIcon::isVisible()`；`show()` 后该属性可以为真，
  即使桌面没有托盘服务。`closeEvent()` 因而执行 `hide()`、`ignore()`。
- 在隔离 D-Bus + Xvfb 的真实 xcb 平台复现：`tray_available=false`，
  `tray_requested_visible=true`，关闭返回 false，窗口消失。
- 先新增 `close_without_tray`，修复前实际失败：
  `'window.close()' returned FALSE`。
- 最小修复：可见性判断同时要求 `isSystemTrayAvailable()`。没有增加状态、
  兼容层或新关闭流程。[Qt 对托盘可用性的说明](https://doc.qt.io/qt-6/qsystemtrayicon.html#details)
  与实测一致。
- 修复后：无托盘 xcb 下关闭返回 true，真实 `QApplication::exec()` 返回 0；
  GNOME 有托盘时关闭仍返回 false，隐藏/恢复正常，调用原有退出操作后返回 0。
- 新回归纳入 CTest，原 27 项全部保留。源码与本记录同一提交。

### 退出回归的 3 秒限制会把正常导入开销误报为退出故障

- 路径：`tests/playback_tests.cpp::metadata_scan_shutdown()`。
- 初次 ASan/UBSan 全量复验中，唯一失败是 `elapsed.elapsed() < 3000`，
  没有 sanitizer 错误。计时实际覆盖建库、5000 条导入及事件处理，并非仅析构。
- 临时副本分段测得：导入后 2088ms、析构前 2863ms、析构后 2864ms；
  析构约 1ms，原断言没有隔离被测行为。
- 删除计时器、启动和总耗时断言三行，提交
  `4241ca9e7eb96368b965e49948594b7d378f5265`。
  保留扫描中退出的同步屏障、数据库内容断言和 CTest 15 秒超时保护。
- 定向 ASan/UBSan 与最终全量复验通过。没有改动扫描线程生命周期。

## D1：A/B/C 独立复核

| 检查点 | 证据与结论 |
| --- | --- |
| 多歌单整体事务 | `database_manager.cpp:342–453` 在唯一事务内处理所有歌单，所有提前返回及 COMMIT 失败均回滚，清空 changed_ids。 |
| 最小写入 | 仅对移除关联 DELETE、新关联 INSERT、变化位置 UPDATE；排序、复制和删除的禁止多余写入触发器回归通过。没有恢复全量删除再插入。 |
| 成功通知 | `playlist_manager.cpp:224–236` 只在保存返回 true 后通知；管理页 `music_management_dialog.cpp:406–415` 失败保留待应用内容并显示错误。 |
| 故障回滚 | 原有 INSERT/DELETE/UPDATE 注入覆盖排序、复制、删除、跨歌单移动及新歌曲；失败后比较 Songs、Playlists、PlaylistSongs、sqlite_sequence 快照，成功通知为零。 |
| COMMIT 本身失败 | 额外临时实验用第二 SQLite 连接的读事务阻止 COMMIT，busy_timeout=30ms。返回 false，关联和顺序快照一致，成功通知 0。不是只验证语句失败。 |
| 失败计数和 Shuffle | `playback_failure_limit`、`shuffle_manual_selection`、模式边界和历史回归通过；没有发现需要再次修改的证据。 |
| 后台所有权 | `duration_scanner.cpp` 工作线程只做路径排队与 FFmpeg 探测；无 SQLite 连接或 QWidget。结果经 queued connection 回到 manager 所在线程入库，再更新 UI。 |
| 结果身份 | 结果按唯一 file_path 匹配，不依赖 UI 行号。`database_manager.cpp:473` 仅更新仍被歌单引用且 duration_ms 为 NULL 的记录，不恢复已删除关联、不写回旧顺序。 |
| 删除/重加/失败 | `metadata_removed`、`metadata_path_reuse`、`metadata_update_failure` 通过。同一路径重加复用在途结果是当前模型的既定语义；失败回滚后保持 NULL，下次导入/应用/启动可重排。 |
| 扫描中退出 | 停止标志、FFmpeg 中断回调和 join 均有职责；5000 条扫描中退出回归通过。`scheduled_` 保留到 UI 确认以防重复排队，不能直接删除。 |

未发现可直接删除的 A/B/C 新增状态或包装层。旧 `playlists.txt` 迁移路径保持原状，
本轮没有新的实际兼容需求证据足以删除它。

## D2：真实设备链路

用户选择“只做设备链路测量”。没有进行扬声器听音或声学回录。

- 实际输出：SDL pulseaudio → PulseAudio 的
  `alsa_output.pci-0000_00_1f.3.iec958-stereo` → ALC897 S/PDIF ALSA PCM。
  `/proc/asound/card0/pcm1p/sub0/status` 在播放采样点为 RUNNING。
- `parec --monitor-stream=<本次进程的 sink-input>` 只录测试进程。
  首次录音全零是监测源本身静音，已丢弃该次录音的幅度/断音结论；
  临时解除监测源静音后完整重测，并在 finally 恢复原静音状态。
  没有更改系统默认输出或输出音量。
- 212.06 秒控制器实验：240 秒 WAV、两首 `/home/gyl/Music` MP3、
  2.5ms 包长 Opus；包含音量 60/40/80/0/60、暂停恢复、连续 seek、
  200ms 间隔切歌、seek 到末尾以及播放中退出。
- 仅在临时源码副本增加回调原子计数，回调不打印、不分配。
  WAV 播放器线程人为延迟 250/750ms，Opus 延迟 250/500ms。
  交接队列满计数始终 0；缺数据计数只在请求文件末尾时增加一个 8192 字节回调，
  其后未增加。没有在仓库引入新的缓冲方案。
- 录音按 10ms 窗口分析，RMS < 1 视为近零，检查持续至少 20ms 的段：
  连续 WAV 的 181.91 秒及 Opus 的 10.92 秒均无额外近零段；
  开始一段 WAV 的 1.02 秒静音、1.99 秒暂停与请求对应。
  两首 MP3 的极短启动片段不足以判断听感或切换间隙，未据此宣称无缝切歌。
- 音量 60/40/80 的稳定 RMS 约为 85.9/25.2/207.5，静音为 0，
  与现有立方音量映射方向一致。
- 同步切歌调用 48–86ms，临时计时的 SDL 关闭为 47–85ms；
  10ms 主线程心跳最大间隔 88.44ms，播放中退出的 stop 为约 63ms。
  PulseAudio 采样的流缓冲与 sink 延迟之和最大 85.13ms，不能当作声学端到端延迟。
- 长播放 170.001 秒窗口的进度增量与墙钟相差 15ms，线性拟合最大残差约 102ms；
  进度包含定时器上报粒度及人为线程延迟，不代表采样级时间精度。
- 该实验进程平均占单核 0.39%，5 秒采样最大 0.99%；峰值 RSS 48676KiB，
  稳态采样范围 47272–47344KiB。该进程不含 QWidget/频谱。

另以完整原生 xcb 界面、真实输出和频谱运行约 59.84 秒，执行暂停恢复、
连续 seek、快速切歌、音量变化和播放中退出，进程返回 0。
10ms UI 心跳最大间隔 87.51ms；平均占单核 1.81%，1 秒采样最大 13.98%；
峰值 RSS 132088KiB，20 秒后范围 131536–132088KiB，未观察到持续增长。
快速取消解码时出现 `Immediate exit requested` 日志，后续会话继续正常推进；
未据此认定取消日志是新的播放故障。

上述监测位于硬件之前，ALSA 状态是离散采样。不能证明实际扬声器听感、
排除采样间的短暂硬件 XRUN，或保证任意调度延迟下始终连续。
HDMI 未测试，未做数小时播放。

## D3：构建与平台边界

实际环境：Linux x86_64，Qt 6.2.4、SDL 2.0.20、本机 FFmpeg 9.0.1 安装。
GCC 16.2 Debug、Clang 23 RelWithDebInfo 均完成应用与测试构建。
已有 conversion 等警告保留，没有为消除警告进行无关修改。

POTENTIAL，仅静态风险，未修改 CMake：

- `CMakeLists.txt:36–40` 无条件加入 GNU 风格警告、stack-clash/protector 参数；
  sanitizer 和部分生产参数也没有 MSVC 分支。MSVC 的接受程度及实际保护效果待实机确认。
- AppleClang 未独立筛选编译器和 CPU 保护参数；Linux Clang 成功不能代表 AppleClang。
- Windows 仍依赖 pkg-config 与 FFmpeg .pc，SDL2 入口/运行库组合没有目标环境证据。
- 当前仅设置 WIN32_EXECUTABLE/MACOSX_BUNDLE，未看到完整依赖部署步骤。
  应在干净目标机验证 Qt 平台/SQLite/SVG/Multimedia 插件、SDL 和 FFmpeg 运行库。
  Qt 提供现成的 [Windows 部署工具](https://doc.qt.io/qt-6/windows-deployment.html)
  和 [macOS 部署工具](https://doc.qt.io/qt-6/macos-deployment.html)，
  后续有实际发布目标时优先使用，不新增通用构建框架。

PLATFORM NOT TESTED：Windows/MSVC、macOS/AppleClang，以及这些平台的安装包。

## D4：桌面与恢复

- GNOME/X11 原生 xcb，2560×1440；实际检查 DPR 1、1.5、2 截图。
  DPR 1.5 来自 QT_FONT_DPI=144，并不是独立字体缩放测试。
- 正常窗口、长中文/Latin 标题、封面、歌词、管理页、窄窗口均检查。
  请求 220px 宽时受布局最小宽 386px 限制；可见按钮没有越出窗口，
  未观察到足以修改布局的确定性缺陷。
- 最小化、关闭到托盘、托盘显示/隐藏正常；无托盘关闭问题见上文修复。
- 实际独立进程重启：位置 3349ms、Shuffle 模式值 2、音量 38 均恢复，
  初始 loaded=false，保持待播放状态。
- 管理页失败保留草稿、500 首首次/重复导入以及损坏/缺失文件由现有回归覆盖。
  本轮没有重跑上一阶段的 2000 首性能前后对比，不把旧基准算作本轮新结果。
- PLATFORM NOT TESTED：Wayland 原生、独立系统字体放大、Windows/macOS 桌面交互。

## 实际构建、回归与复现材料

| 构建 | CTest | 耗时 |
| --- | --- | --- |
| GCC Debug | 28/28，包括原 27 项 | 6.51s |
| Clang RelWithDebInfo | 28/28 | 6.46s |
| GCC ASan + UBSan | 28/28 | 7.19s |

sanitizer 最终执行：

```sh
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
ctest --test-dir /tmp/musicplayer-round2-sanitize --output-on-failure -j 4
```

第三方预编译库没有重新插桩。TSan 本轮未运行，上轮 Qt 同类报告的限制仍在，
不能宣称 TSan 通过或所有并发问题已排除。

本轮临时复现实验未引入新测试框架，用户数据库和音乐文件未改动。
以下文件保存在本机 `/tmp`，清理临时目录后不再可用：

- `/tmp/musicplayer-d-tray-red.log`：无托盘回归修复前失败。
- `/tmp/musicplayer-d/{exit-no-tray,exit-native}/result.log`：真实事件循环退出。
- `/tmp/musicplayer-d/commit/run.log`：COMMIT 失败回滚。
- `/tmp/musicplayer-d/shutdown.log`：退出回归分段诊断；初次全量失败输出见本轮执行记录，
  原全量日志已被后续复验覆盖。
- `/tmp/musicplayer-d-{gcc,clang}-tests.log`、`/tmp/musicplayer-d-sanitize-final.log`。
- `/tmp/musicplayer-d/valid/`：驱动脚本、日志、进程采样、PCM 录音和分析结果；
  源码副本为上一级的 `audio-valid.cpp`、`audio_player_timed.cpp`。
  上一级目录的全零录音不作为输出连续性证据。
- `/tmp/musicplayer-d/full-ui/`：完整 UI 运行日志、资源采样及截图；
  `/tmp/musicplayer-d/ui-{1,2,font}/`：布局和恢复截图/日志。

## 验收结论

NOT REPRODUCED：A/B/C 新的数据丢失、失败通知成功、迟到结果恢复删除关联、
跨线程 SQLite/QWidget 访问、全失效歌单无限切换，以及本轮限定调度延迟下的交接队列静音。
这表示本次复核和执行未发现问题，不是对所有输入和调度的保证。

直接删除仅限退出回归中无依据的总耗时限制。生产修改仅为托盘可用性判断，
没有新增通用框架、缓存、状态或包装函数。下一阶段先在实际 Windows/macOS
环境构建、部署和操作；有明确输出设备与听音条件后再补声学验证。

最终提交后工作区应仅保留原有 `audio_decoder.cpp` 用户补丁；
完整最终 HEAD 与实际状态以交付回复中的核实结果为准。

## Standards

新增改动另经只读独立核验。硬性规范违反 0，判断性代码气味 0。
控制语句使用花括号，未提升成员可见性；无托盘测试的环境前提有实际职责。
托盘修改只改变既有判断，没有新增状态或包装；原 27 项测试均保留。

## Spec

独立核验未发现越界行为或确定的实现错误。核验时修复和报告尚未提交，
指出交付必须区分 HEAD 与工作区；此交付状态项通过将修复、回归和本报告
一并本地提交处理，最终再次核实 HEAD 和剩余用户补丁。
无托盘 RED/GREEN、原生事件循环、28 项回归均有实际结果；
未测平台、听感和 TSan 的限制已披露。

独立核验计数：Standards 0；Spec 1 项交付状态核实项，代码实现问题 0。
