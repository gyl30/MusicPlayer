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
