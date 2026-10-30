# 最终 v3 原生界面验收

提交说明：验收及标题栏修正完成后，用户明确授权将本轮代码与验收资料提交并推送；下文未提交说明记录当时状态，提交身份以 Git 日志为准。

验收基线与当前 HEAD：`63058c75a592e131541e7637ddf0add401666386`。开始时只有用户提供的 final-v3 目录未跟踪，原始 README.md、reference.png 未改动；参考图 SHA256：`b19ef394916214e5a0651ac41e2d25fa6d5d6e8e0828ae9f7ed3ae32d8e35e67`。本轮未提交、推送或部署。

## 对照与实际状态

以下均是实际项目 Qt Widgets/控制器的截图，不是 HTML、生成图或重新绘制的模拟界面。Before 与 After 使用同一首本地 MP3、同一组本地歌曲、同一 seek 位置和暂停状态。封面、歌手、专辑及歌词均来自真实文件的解码信号，没有注入示例数据。截图夹具 qt-preview.cpp 链接现有测试目标的相同项目对象，不是应用运行时依赖。

| 场景 | Before | After |
|---|---|---|
| 宽窗 860×720 | [列表独占](acceptance/before/wide.png) / [旧歌词替换页](acceptance/before/old-lyrics-page.png) | [列表与歌词并排](acceptance/after/wide.png) |
| 标准 540×740 | [原界面](acceptance/before/standard-collapsed.png) | [底部收起](acceptance/after/standard-collapsed.png) / [展开](acceptance/after/standard-expanded.png) |
| 紧凑 360×530 | [原界面](acceptance/before/compact-collapsed.png) | [收起](acceptance/after/compact-collapsed.png) / [展开](acceptance/after/compact-expanded.png) |
| 无歌词、无封面 | [原界面](acceptance/before/no-lyrics-no-cover.png) | [释放歌词区域](acceptance/after/no-lyrics-no-cover.png) |
| 播放及 seek | [原界面](acceptance/before/seek.png) | [播放](acceptance/after/playing.png) / [seek 后](acceptance/after/seek.png) |
| 管理页 800×480 | [原界面](acceptance/before/management.png) | [当前界面](acceptance/after/management.png) |

[1,215 首真实本地歌曲，四位序号 1200](acceptance/after/four-digit-index.png) · [2× 宽窗](acceptance/after-2x/wide.png) · [2× 标准展开](acceptance/after-2x/standard-expanded.png) · [2× 紧凑展开](acceptance/after-2x/compact-expanded.png)。

实际曲目为 `/home/gyl/Music/13.粤语劲歌金曲榜 200首/1070.捞月亮的人 - 杨千嬅.mp3`，从已有 USLT/lyrics 解析路径提供时间戳歌词；截图中的人员、专辑与封面属于文件原始内容。无歌词/封面场景使用已存在的本地 WAV 测试文件，未伪造艺人或封面。

## 实现与参考图逐项核对

- 宽度≥780：原生 QBoxLayout 切换左右方向，左侧播放器与完整列表、右侧常驻歌词，约3:2分配；无歌词时隐藏整个歌词面板，列表自动占满。
- 标准/紧凑：同一歌词 widget 放在底部。收起显示当前行及临近文字，展开增加阅读高度；窄窗展开仍保持360×530。歌词面板用最大高度和可压缩布局分配，避免展开状态把下次缩窗卡在687px。移除占用单独底栏的缩放手柄布局，将既有 QSizeGrip 放到窗口角落；整页继续由原生布局排版。
- 删除“列表/歌词”替换标签及其 QStackedWidget，保留应用切换管理页所需的主 stack。没有第二份歌词列表、播放进度缓存或新线程。
- 当前行蓝色高亮、上下文弱化、自动居中；滚轮可临时阅读上下文，下次当前行改变恢复跟随。歌词定时器仍为原来的一只，只在显示/滚动过渡需要时运行，隐藏、空内容或滚动稳定时停止；删除原先仅写入、未使用的 current_time_ms_。
- progress 更新始终传入唯一的 lyrics_widget，seek_completed 立即同步；展开/重新显示使用主窗口已有 current_progress_ms_，暂停 seek、切歌清空和无歌词隐藏均已验证。当前行同时提供 accessibleDescription，不引入重复的播放状态。
- 主界面添加、管理为统一30px、20px SVG图标，描边1.7，按钮 text/tooltip 为空，准确 accessibleName 保留；hover/pressed/focus/disabled 均有样式。测试实际点击图标，使用非原生文件选择器选择 WAV 并查数据库，随后进入管理页、取消返回。Ctrl+M 与原业务调用不变。
- 树缩进12→6，文本左内边距10→4；序号宽度按真实行数和字体计算，四位数不截断，时长仍右对齐。保留格式后缀，避免同名 MP3/FLAC 在列表中无法区分，路径/歌曲身份不变。正常/紧凑行高40/34，单个 delegate，无逐行 QWidget。
- 继续使用已有蓝白色、圆角封面及醒目播放主按钮；四模式、停止、音量、实际管理按钮和托盘路径未移除。没有恢复频谱。

## 代码文件

- `playlist_window.cpp/.h`：三种尺寸的同一布局、歌词面板及折叠按钮、SVG入口、数据到歌词的同步、列表留白与四位序号、角落缩放手柄。
- `lyrics_widget.cpp/.h`：显示/隐藏/缩放处理、当前行重绘和无障碍描述、滚动阅读及定时器停止条件，删除未使用时间副本。
- `icons/add.svg`、`icons/manage.svg`、`resources.qrc`：统一描边的添加与管理图标。
- `style/stylesheet.qss`：删除过时标签样式，统一图标入口各状态和歌词面板。
- `tests/playback_tests.cpp`、`CMakeLists.txt`：新增 svg_library_actions、adaptive_lyrics、playback_state_restore_ui；原37个用例保留，其中原标签测试改为检验列表与歌词共存以及暂停seek同步。

未修改 player_window 的播放控制、解码器、音频播放器、播放控制器、数据库、扫描线程、third/；没有新依赖、UI框架、异常处理、用户设置键或通用缓存。

## 测试与数据

环境：Linux，GCC16.2.0，Qt6.2.4，Debug；复用 `/tmp/musicplayer-round2-build` 与 `/tmp/musicplayer-round2-sanitize`。GUI 使用独立 Xvfb :97、xcb、隔离 DBus 和用户数据、SDL dummy；1×/2×。未操作前台桌面。

- 修改前：37/37 CTest，32.16秒。
- 最终：**40/40 CTest，36.50秒**，见 [ctest.log](acceptance/ctest.log)。CTest每个用例独立进程，使用offscreen/dummy。
- 最终：**40/40 ASan/UBSan，41.14秒**，见 [sanitizer.log](acceptance/sanitizer.log)。ASAN_OPTIONS=detect_leaks=1:halt_on_error=1；UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1。预编译第三方库不是全量插桩。
- 新的 SVG/自适应歌词回归在原实现上先失败：旧按钮有文字，歌词替换页不可同时显示；修复后通过。
- 旧回归继续覆盖播放/暂停/恢复、初始/连续/暂停seek、快速切歌、EOF、退出、回调释放线程、四模式、损坏/缺失/未知时长、格式、歌单生命周期及SQLite触发器故障回滚等。
- 新增重启用例在播放器完成seek后保存并重建窗口，检查位置、Shuffle、音量100及当前歌曲身份恢复，且未自动开始播放。
- xcb 独立运行 large_playlist_ui、svg_library_actions、adaptive_lyrics，全部通过，见三个对应日志。1,200个有效20ms WAV：同步导入81ms，含扫描541ms，30次选择/滚动/同步重绘最大3ms，viewport新增QWidget为0。该时间从准备好文件后计时，完整用例还包含建文件/清理，约14.65秒；不能推断千首大型FLAC导入耗时，也不据此宣称性能提升。
- GUI另加载1,215首真实本地MP3，查看1197–1203行，序号1200和右侧时长完整。歌词缺失后整页自动释放空间，列表没有额外的歌曲Widget。
- 实际GUI额外操作多歌单箭头折叠/展开、Ctrl两首多选（计数2）、右键菜单排序真实命令；见 [gui.log](acceptance/gui.log)。图标入口的文件选择/数据库查询由xcb测试实际执行。
- git diff --check 通过。

中间验证中，文件选择器测试曾因异步模型尚未填入文件名而超时，改为等待可见对话框后输入真实文件名；一次将多个共享测试数据库的用例放在同一进程中运行，造成1201/1计数不符，已按既有CTest隔离方式逐个复验通过；重启用例最初只等待解码器通知，现等待播放器seek_finished及稳定进度。没有将这些中间结果计为通过，也未为迁就测试修改业务。中间原始日志保留在 `/tmp/musicplayer-v3/`。

## 未实现及未验证

- 独立歌词窗口和歌词设置页未实现；没有添加弹出、设置、搜索、收藏等无真实功能的入口。歌词当前行排版及宽窄布局为本轮优先目标，未复制原型的海报数据或外部阴影。
- 沿用既有解析来源：部分本地文件只有相邻 `.lrc` 或 `TEXT` 标签，当前解码器不读取这些来源，本轮没有扩大解码功能。此类文件按无歌词处理；不能把“文件旁边存在lrc”当作应用已经支持。
- 行内歌手列未增加，因为持久化歌曲模型没有该字段。窗口、真实歌曲数据及系统字体决定具体行数和换行，不保证与海报像素完全相同。
- 没有验证真实声卡听感、Windows/macOS、原生Wayland、TSan、真实系统托盘/窗口管理器拖动或长时间CPU/RSS。虚拟环境的最小化/关闭及自动退出回归不能替代上述平台验收。

本轮停在工作区等待用户实际验收，不自动提交或推送。

## 标题栏按钮修正

根据用户反馈，将文字减号/乘号替换为24×24 SVG图标，点击区域从26×26扩大到44×36，标题栏36→40。最小化/关闭连接不变，并增加准确 accessibleName。

[1× 实际界面](acceptance/titlebar.png) · [2× 实际界面](acceptance/titlebar-2x.png)。GCC构建和4项定向回归通过（ui_controls、adaptive_lyrics、close_without_tray、page_size_restore，1.36秒）；虚拟Qt 1×/2×实际点击最小化、恢复和关闭成功，记录尺寸44×36、图标24×24。此项仅涉及图标、尺寸和无障碍名称，未重新执行全量40项或Sanitizer，上面的全量结果属于此项修正前。仍未提交或推送。
