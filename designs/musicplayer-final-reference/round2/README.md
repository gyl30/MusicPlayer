# UI 第二轮：真实 Qt 对照验收

基线及验收 HEAD：`4578b7eb06f8e26f39fa86304ef808abc9b57f80`。开始时工作区干净。验收时未提交、未推送；随后用户明确授权提交并推送本轮代码及验收资料，提交身份以 Git 日志为准。播放控制器、解码、SDL、数据库、扫描线程、持久化语义和 third/ 未修改，不恢复频谱。

## 前后对比

所有图片来自实际项目 Qt Widgets，独立 Xvfb `:97` / xcb、隔离 DBus 和用户数据、SDL dummy；未使用前台鼠标。Before 和 After 使用相同的八个 WAV 文件、相同的元数据、相同的封面及操作顺序。原型 HTML 没有修改，也不是这些图片的来源。

| 尺寸/状态 | Before | After |
|---|---|---|
| 正常 540×740 | [修改前](before/normal.png) | [修改后](after/normal.png) |
| 紧凑 360×480 | [修改前](before/compact.png) | [修改后](after/compact.png) |
| 最小 340×400 | [修改前](before/minimum.png) | [修改后](after/minimum.png) |
| 管理 800×480 | [修改前](before/management.png) | [修改后](after/management.png) |
| 播放中 | [修改前](before/playing.png) | [修改后](after/playing.png) |
| 无封面 | [修改前](before/no-cover.png) | [修改后](after/no-cover.png) |
| 长标题 | [修改前](before/long.png) | [修改后](after/long.png) |
| 歌词 | [修改前](before/lyrics.png) | [修改后](after/lyrics.png) |
| 空列表 | [修改前](before/empty.png) | [修改后](after/empty.png) |

[当前播放、选择与悬停同时展示](after/list-states.png) · [真实本地 MP3 及原始元数据](after/real-local.png) · [2× 正常界面](after-2x/normal.png) · [2× 紧凑界面](after-2x/compact.png)。

对照夹具的 WAV 是有效音频文件，但不是歌名所指的音乐内容；封面是测试图片，标题/歌手/专辑/日期/歌词通过应用现有槽输入，用于隔离视觉差异。它们并非来自这些 WAV 的媒体标签，不能作为元数据解析证据。额外的 real-local 场景从 `/home/gyl/Music` 实际加载 MP3，展示解码器真实返回的文件信息、日期及缺省封面，没有替换元数据。该场景作为补充，不与 Before 伪装成同一文件对照。

## 参考图主要目标

| 目标 | 结论及实际依据 |
|---|---|
| 封面左、标题/歌手/其他真实信息/进度/时间右 | 达到；使用原生 QHBoxLayout + QVBoxLayout，两个布局回归先失败再通过；截图中进度不再横跨封面下方 |
| 播放控制独立位于信息组下方 | 达到；正常 54px 主按钮、紧凑 44px；其余控制低强调，保留停止和四模式 |
| 音量在同一行最右侧且弱化 | 达到；14px 图标、44px 低对比短条，鼠标、滚轮、键盘仍走原路径 |
| 正常与紧凑布局有区别 | 达到；默认 540×740；封面 144/104px、标题 18/14pt、歌曲行 44/36px，按宽度改变；340×400 下无信息/控制重叠，但只显示少量歌曲，需要滚动 |
| 歌曲行清楚、有序号、时长对齐 | 达到；一个 delegate 绘制序号/当前曲标记及文字，11/10pt 层级、细分隔线；按实际加粗字体预留时长宽度，去掉上限截断风险 |
| 当前播放、选择、悬停、焦点分开 | 达到；当前曲蓝色及标记、选择灰蓝、悬停浅底、键盘焦点底部虚线，不再用矩形框框住名称 |
| 歌单弱化默认文件树外观 | 达到；去掉每行文件/文件夹图标，分组用字重、数量和可点击展开箭头；实际鼠标点箭头日志为 false→true，原多选、右键与多歌单模型保持 |
| 管理页返回恢复用户尺寸 | 达到；两页只记录会话内各自尺寸。610×710 返回保持原尺寸，再进入管理仍保留用户改成的850×530；无新增 QSettings 键 |
| 参考图歌手列、艺术封面、所有装饰 | 部分未实现；歌曲模型没有逐曲歌手字段，不扩展数据库或伪造数据。实际封面来自文件；测试封面仅证明显示裁切。未增加收藏、更多菜单、假最大化或窗外阴影，不追求海报像素复制 |

正常窗采用更宽松的排版，不能保证任意字体下恰好完整显示参考图的八首歌曲；列表仍可滚动。主窗口外轮廓继续使用既有无边框窗口方案，本轮未引入透明窗口/阴影绘制。管理页业务与布局未重写，仅验证切换尺寸和现有操作。

## 代码变更

- `player_window.cpp/.h`：将进度、时间移入右侧信息布局；显示已有 album/date；切歌清空旧值；新增宽度响应与圆角封面显示。保留一份当前封面的原始 QPixmap，供改变显示尺寸重新取样，避免把缩小图片再次放大；没有通用图片缓存、定时器或额外线程。
- `playlist_window.cpp/.h`：默认正常尺寸；保存两页各自会话尺寸；一个局部 song_delegate 绘制整行视觉，保留 QTreeWidget 的模型、选择、展开与右键；隐藏显示扩展名但 UserRole 路径和数据库文件名不变；去除树中的图标设置与单列焦点矩形。
- `style/stylesheet.qss`：正常/紧凑字号和主按钮、圆角、列表焦点与展开状态。
- `icons/chevron-down.svg`、`icons/chevron-right.svg`、`resources.qrc`：真实展开操作的提示图标。
- `tests/playback_tests.cpp`、`CMakeLists.txt`：新增三个回归，无删除或放宽原有测试。

## 实测与限制

Linux、GCC 16.2.0、Qt 6.2.4、Debug；复用既有构建目录。

- 先运行新增 page_size_restore / responsive_layout，原实现分别返回错误尺寸、进度位于封面下方，2/2 失败；修复后通过。
- 最终完整 CTest：**37/37，通过，36.68 秒**，见 tests.log。
- 最终 ASan/UBSan：**37/37，通过，45.49 秒**，见 sanitizer.log。启用 `ASAN_OPTIONS=detect_leaks=1:halt_on_error=1`、`UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1`；预编译库并非全部插桩。
- 1,200 首有效的 20ms WAV：xcb 实际显示测量同步导入 **122ms**，含异步时长扫描 **894ms**，30 次选择/滚动/同步重绘最大 **2ms**，viewport 没有增加 QWidget；见 large-xcb.log。样本为独立小文件，结果不能推断千首大型 FLAC 的导入耗时或所有硬件的响应时间；本轮未改变导入实现。另一次 offscreen 全量中的对应值为78/773/1ms。
- Qt 1×/2× 核对上述逻辑尺寸；两页切换、列表/歌词切换、暂停/恢复、组展开鼠标操作、真实 MP3 播放以及回归中的 seek/模式/音量/事务/退出均有实际执行。没有把 CTest 通过等同于视觉还原：布局、层级、列表、圆角与长标题分别查看了实际图片。
- `git diff --check` 通过。未运行 TSan、Windows/macOS、原生 Wayland、真实声卡/听感、真实系统托盘与窗口拖动、读屏器或长期播放。没有新增持续重绘机制，但没有重新测量长期 CPU/RSS。

可复验：

```sh
cmake --build /tmp/musicplayer-round2-build -j2
ctest --test-dir /tmp/musicplayer-round2-build --output-on-failure -j1
cmake --build /tmp/musicplayer-round2-sanitize -j2
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 ctest --test-dir /tmp/musicplayer-round2-sanitize --output-on-failure -j1
DISPLAY=:97 QT_QPA_PLATFORM=xcb SDL_AUDIODRIVER=dummy DBUS_SESSION_BUS_ADDRESS=unix:path=/tmp/no-dbus /tmp/musicplayer-round2-build/playback_tests large_playlist_ui
```

qt-preview.cpp 为截图夹具，链接现有测试目标相同项目对象；它不加入应用运行时或生产构建。其测试目录和显示限制防止误用用户数据或前台桌面。输入音频路径是本机测试夹具位置，其他机器须替换对应路径。
