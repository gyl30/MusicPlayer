# 数据一致性、播放失败与导入响应审查

起点为 `6db54a8`，`main` 与远程跟踪分支一致。工作区只有用户原有 `audio_decoder.cpp` 字符集补丁，逐阶段核对并保留，不提交该文件。本轮不推送。

## 阶段 A：原子提交歌单修改

原触发路径：`music_management_dialog::on_done_button_clicked()` → `playlist_manager::apply_changes_from_dialog()` → 删除旧歌曲并提交 → 添加新歌曲并再次提交。后一次失败不能撤销前一次成功；管理页还无条件清除 pending 并返回。

修复前实际运行 `storage_atomicity` 的 8 行数据、`storage_minimal_writes` 的 3 行数据和 `management_failure`：全部测试组失败。复制 INSERT 失败导致目标列表缺行；跨列表移动失败改变了整个数据库快照；DELETE 失败仍发送成功通知；管理页 pending 被清除。排序的 UPDATE 故障和禁止 INSERT/DELETE 测试还证明旧实现根本没有走最小排序路径。

数据库新增 `save_playlists()`，直接使用既有 Qt SQL transaction/commit/rollback 与 scoped_exit。一个事务处理本次应用涉及的所有歌单：删除移除的关联、插入新关联、仅更新位置变化的旧关联；未变化的歌单不写入。SQLite 原有 position 无唯一约束，直接交换位置即可，不需要临时位置或重建关系。

删除数据库全删再重插的 remove 路径、独立排序事务，以及 manager 分段提交路径。增删改函数返回真实 bool；Songs 插入失败不再跳过并提交其余内容。提交成功后仅通知实际变更的歌单一次。管理页失败时保留编辑内容、停留原页并提示错误，重试可成功；主窗口操作同样检查返回结果。

验证包含：排序 UPDATE、复制 INSERT、删除 DELETE/UPDATE、移动 INSERT/DELETE/UPDATE、新 Songs INSERT 失败；失败后重新读取 Songs、Playlists、PlaylistSongs、sqlite_sequence 全表，比较内容、位置、关联及计数器；撤除触发器后重试。最小写入测试禁止不应发生的 SQL。管理页覆盖单列表删除及跨列表移动后插入失败的保留/重试；普通添加、删除歌单、重命名失败验证无通知。

常规构建与 CTest：20/20 通过（包含原 16 项；数据驱动行不冒充独立 CTest 数量）。ASan/UBSan 同样 20/20 通过，启用泄漏检测和遇错停止；依赖库未重新插桩。

独立复核：Standards 未发现确定违规，提示重复错误文案为轻微异味；Spec 未发现确定实现错误，指出的跨歌单 UI 用例已补充。重复路径受已有唯一关联约束保护，管理页本身防重；未增加通用输入验证层。

API 核对：[Qt SQL 事务](https://doc.qt.io/qt-6/qsqldatabase.html#transaction)、[SQLite 触发器及 RAISE](https://www.sqlite.org/lang_createtrigger.html)。

## 阶段 B：播放失败计数与 Shuffle 手动选择

`on_playback_started()` 原在解码启动前清零连续失败计数。全缺失文件 ListLoop 回归实际观察到没有阈值停止；“先失败四次、正常播放、再失败五次”同样不能停止。修复仅删除该清零，并在已有进度通知实际向前推进时清零；seek 完成已有位置更新，单纯 seek 起始偏移不构成成功播放证据，不增加播放成功标志或历史管理器。

手动依次双击 0、3、1、5 号不同歌曲，每次 next/previous 均回到所选歌曲。该回归在修改前就通过，故不修改 Shuffle 代码。

常规及 ASan/UBSan 均 22/22 CTest 通过，包含原 16 项。失败计数测试含全缺失、成功音频后重计数两行；阈值停止后继续处理事件确认没有自动跳转。仅使用 offscreen/dummy 音频，不声称真实设备或 TSan 已验证。

## 阶段 C：基本信息先提交，后台补充时长

原触发代码位于 `get_or_create_song_id()` 的同步 `probe_audio_duration_ms(file_path)`，以及 `get_playlist_with_songs()` 中逐首回填 MissingDuration 的循环。两者都由 UI 线程调用；FFmpeg 探测还占住导入事务。

先实测再改动：在 `6db54a8` 的独立构建中，500 首前台约 1618ms，其中 FFmpeg open/find_stream_info 约 1560ms、SQLite step 约 28ms、歌曲树刷新（含读取）约 9.5ms；2000 首相应为 2833ms、2695ms、37ms、38ms。主要耗时明确在媒体探测，因此没有异步化全部业务。新增 `async_import` 先在旧实现运行，返回后已无待探测项，异步交接断言失败。

删除数据库内 FFmpeg 探测、读歌单时补写时长及其临时列表。新记录的 duration_ms 保持已有数据库允许的 NULL，Song 对外仍将未知值表达为 0。单个 `duration_scanner` 线程仅执行 FFmpeg，按最多 64 项返回结果；Qt queued connection 回到 UI 线程，用原有 SQLite 连接批量事务写入。时长通知只更新现有节点的时长列和列宽，不重建歌曲树或随机序列。

初次实现的 2000 首前台仍约 160ms，测得待扫描查询的相关 EXISTS 逐行查找有开销；改为一次 JOIN/DISTINCT 后约 82ms。没有增加索引、额外数据库连接或持久缓存。

### 结果与取消规则

- queued/in-flight 路径集合只负责防止重复排队，直到 UI 确认批次结果才移除；不是长期媒体缓存。立即重复导入的 502 个路径只探测一次。
- 只有已提交且仍有关联、时长仍为 NULL 的 Songs 记录接受结果；已移出所有歌单的结果丢弃。原模型移除歌曲只删除关联，不删除 Songs 记录，因此同一路径在投递前重新加入允许复用在途结果；有独立回归，不新增 generation 状态。
- 探测完成但无法确定时长、缺失或损坏的文件仍使用既有 0 语义，UI 显示未知，不无限重试。文件原地替换后没有自动失效/重扫机制。
- 时长写库失败时整个批次回滚，不发送更新通知；NULL 保留，下次启动、导入或保存操作可重新排队。当前仅日志提示，不立即循环重试或逐首弹窗；这与管理页应用失败时保留编辑并弹窗是两条不同路径。
- manager 析构先取消并等待扫描线程；停止标志同时用于条件变量和 FFmpeg interrupt callback。未处理、未提交的时长留 NULL，下次启动继续，基本信息不撤销。已提交的元数据保留。
- 删除不会中断已经开始的单首探测，其结果由上述关联规则过滤。任意不可中断的文件系统阻塞仍可能延长退出；未声称固定退出时限。

### 性能对比

同一组 `/home/gyl/Music` 文件，按完整 MP3 路径排序取前 500/2000 首，原文件只读。每个进程从临时空库导入，再立即重复导入；offscreen + SDL dummy。最大 UI 间隔来自 10ms 心跳；总完成时间等待关联歌曲的所有 NULL 时长均写为结果。峰 RSS 为该进程累计峰值，重复行可能包含首次导入峰值。

| 文件数与操作 | 前台返回 ms：前→后 | 全部完成 ms：前→后 | 最大 UI 间隔 ms：前→后 | 峰 RSS KiB：前→后 |
| --- | --- | --- | --- | --- |
| 500 首次 | 1617.8 → 30.6 | 1618.1 → 1613.8 | 1618.0 → 35.1 | 65260 → 66464 |
| 500 重复 | 12.3 → 12.5 | 12.4 → 12.6 | 12.4 → 12.5 | 65260 → 66464 |
| 2000 首次 | 2833.1 → 82.4 | 2833.5 → 2770.7 | 2833.3 → 82.6 | 65752 → 70560 |
| 2000 重复 | 50.5 → 49.4 | 50.8 → 49.6 | 50.5 → 49.4 | 66520 → 70560 |

每一行均实际确认：条目数正确、文件路径和 position 顺序一致、foreign_key_check 无错误、没有待写入的 NULL。重复导入无 FFmpeg 调用。修复后基本歌曲树刷新约 9.6/37.3ms，全部时长列更新累计约 5.9/87.3ms（分多次事件执行）。后台全部完成时间没有被“导入函数返回快”替代。

这些是本机单次配对观察，缓存和调度会影响数值，不代表统计性能保证。FFmpeg、SQLite step 和 UI 刷新是可重叠的墙钟观测，不能直接相加；SQLite step 不含 prepare/bind，且包含基准查询，不能当作完整 SQLite 独占耗时。

复现工具 `tests/benchmark_import.py` 使用已有 Linux/Unix Makefiles 构建，在临时目录编译基准入口和带计时的 UI 源码副本，然后链接原有应用对象；不会改动应用对象或工作区源码。需本机已有 SQLite 开发头文件/库，仅用于诊断，不增加产品依赖。FFmpeg 用 linker wrap，SQLite 通过 step 符号观测。不要将此工具的观测能力扩大为任意驱动/平台的通用 profiler 保证。

```bash
python3 tests/benchmark_import.py /tmp/musicplayer-round2-build /tmp/musicplayer-import-500.txt
python3 tests/benchmark_import.py /tmp/musicplayer-round2-build /tmp/musicplayer-import-2000.txt
```

## 最终验证及限制

环境沿用 GCC 16.2、Qt 6.2.4、FFmpeg 9.0.1、SDL2，独立构建目录 `/tmp/musicplayer-round2-build` 与 `/tmp/musicplayer-round2-sanitize`，未修改原 build 或 build.sh。

```bash
PKG_CONFIG_PATH=/home/gyl/ffmpeg901/lib/pkgconfig cmake -S . -B /tmp/musicplayer-round2-build \
  -DBUILD_TESTING=ON -DCMAKE_BUILD_TYPE=Debug -DFFMPEG_EXECUTABLE=/usr/bin/ffmpeg
cmake --build /tmp/musicplayer-round2-build -j4
ctest --test-dir /tmp/musicplayer-round2-build --output-on-failure -j4
```

另从暂存内容导出不含用户字符集补丁的独立快照，重新构建应用及测试并通过 **27/27 CTest**，确认本轮提交不依赖用户补丁。

普通应用/测试构建通过，**27/27 CTest 通过**，原 16 项全部保留运行。`ENABLE_ASAN=ON`、`ENABLE_UBSAN=ON` 同样 **27/27 通过**，启用 `detect_leaks=1:halt_on_error=1` 与 UBSan 遇错停止；第三方依赖没有重新插桩，基准使用普通构建。

C 新回归覆盖 500 个有效 WAV 加缺失/损坏文件、立即重复导入且不重复扫描、元数据更新不删除树项、移除后丢弃结果及重新加入、元数据 UPDATE 故障回滚后重试、同路径在途复用。5000 待扫描项退出测试用首批结果的同步观察者建立屏障，确保队列未处理完时触发销毁，验证取消/join 和基本信息保留；不是任意内核 I/O 可中断性的证明。

本轮未重跑 TSan；上轮 Qt 最小程序也报告同类竞态的限制仍在，不能宣称并发问题全部解决。没有 Windows/macOS、真实音频设备、网络文件系统、文件原地变更或视觉布局验证。全量基本信息提交和初次树构建仍同步，2000 首实测最长前台约 82ms；未通过新增通用任务系统掩盖更大规模可能出现的限制。

独立 Standards 复核未发现确定的锁/生命周期违规，确认线程无 SQLite/QWidget、in-flight 去重状态有职责；提出探测失败返回 0 的观察，按既有未知时长语义记录。独立 Spec 复核提出路径重用、失败批次延后重试、删除不主动取消、I/O 退出边界和 profiler 范围限制，均按上面的明确规则交付；没有据这些假设增加代次、历史管理器或复杂缓存。

阶段 A 提交 `60379c3`，阶段 B 提交 `a419cda`；阶段 C 提交及最终 HEAD 见本轮交付。每阶段 `git diff --check` 通过，最终工作区仅保留起点的用户字符集补丁，不包含在本轮三个提交中。没有推送。

当前源码定位：原子歌单写入 `database_manager.cpp:342`，提交后通知 `playlist_manager.cpp:224`，管理页失败保留 `music_management_dialog.cpp:406`；实际进度成功证据 `playlist_window.cpp:431`，开始请求不再清零 `playlist_window.cpp:574`；扫描及取消 `duration_scanner.cpp:20,64,98`，结果事务与有效性 `database_manager.cpp:473`，时长列更新 `playlist_window.cpp:395`。
