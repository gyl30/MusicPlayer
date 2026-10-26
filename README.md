# MusicPlayer

基于 Qt Widgets、FFmpeg、SDL2 和 Qt SQL/SQLite 的本地音频播放器。

当前包版本 `0.0.0` 用于本地安装验收，不表示正式发行。项目自身许可证尚未声明；
公开分发前须明确授权，并核对所用 FFmpeg 构建及其他依赖的许可。
见 [第三方组件说明](packaging/THIRD_PARTY_NOTICES.md)。

## 构建

需要 CMake 3.21+、C++17、Qt6 Widgets/Multimedia/Svg/Sql、SDL2、Boost，
以及 FFmpeg 5.1 或更新版本。使用与目标编译器、架构一致的依赖。
Qt 和 FFmpeg 的库/插件部署不由源码目录中的绝对路径保证。

Debian 12 的系统依赖：

```sh
sudo apt-get install build-essential cmake pkg-config qt6-base-dev \
  qt6-multimedia-dev qt6-svg-dev libsdl2-dev libboost-dev \
  libavformat-dev libavcodec-dev libavutil-dev libswresample-dev \
  libqt6sql6-sqlite ffmpeg dpkg-dev file
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build-release --parallel 2
ctest --test-dir build-release --output-on-failure
```

Ubuntu 22.04 自带 FFmpeg 4.4 不满足当前 API 要求。自备更新版依赖时，
在首次配置前明确设置其 `PKG_CONFIG_PATH`；换依赖安装时使用新的构建目录，
避免复用旧头文件和库的查找缓存。不要将本机自定义静态 FFmpeg 构建直接视为可发行依赖。

GNU/Clang 的 ASan、UBSan 仅在明确启用时加入：

```sh
cmake -S . -B build-sanitize -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON \
  -DENABLE_ASAN=ON -DENABLE_UBSAN=ON
cmake --build build-sanitize --parallel 2
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
ctest --test-dir build-sanitize --output-on-failure
```

## Linux 安装包

Linux 采用 CPack DEB，依赖发行版共享库，不捆绑开发机运行库。
需要在所支持的 Debian 系发行版上分别构建和验收，不能将 Debian 12 的包当作所有 Linux 通用包。

```sh
cpack --config build-release/CPackConfig.cmake -G DEB -B dist
sudo apt-get install ./dist/musicplayer_0.0.0_amd64.deb
MusicPlayer
sudo apt-get remove musicplayer
```

上述文件名对应 amd64 默认版本；其他架构或版本以 CPack 实际输出为准。
包管理器推导 ELF 共享库依赖；Qt 平台插件、QSQLITE、SVG 插件另行声明，
Wayland 插件和中文字体作为推荐依赖。精简安装跳过推荐依赖时，需自行提供中文字体，
否则中文界面会显示缺字方框。安装/卸载规则只操作安装文件，不清理用户音乐、数据库或设置。

## Windows 构建准备（尚未实机验收）

安装 MSVC、匹配架构的共享 Qt6/SDL2、FFmpeg 5.1+、Boost、CMake 和 NSIS。
FFmpeg 根目录必须同时提供来自同一构建的 `include/`、`lib/` 导入库与 `bin/` DLL。

```powershell
cmake -S . -B build-windows -G "Visual Studio 17 2022" -A x64 `
  -DCMAKE_PREFIX_PATH="C:/deps/Qt;C:/deps/SDL2;C:/deps/Boost" `
  -DFFMPEG_ROOT="C:/deps/FFmpeg"
cmake --build build-windows --config Release
cpack --config build-windows/CPackConfig.cmake -C Release -G NSIS -B dist
```

CMake 扫描可执行文件的运行依赖；安装阶段调用同一 Qt 安装的 `windeployqt`，
处理 Qt 插件及编译器运行库。发布前必须检查实际收集结果，尤其是 QSQLITE、SVG、
SDL2、FFmpeg 和传递依赖。不要把静态查阅当作 MSVC 编译或 Windows 安装验证。

## macOS 构建准备（尚未实机验收）

安装同架构的共享 Qt6、SDL2、FFmpeg 5.1+、Boost、pkg-config 和 CMake，
通过 `CMAKE_PREFIX_PATH`、pkg-config 的标准路径指定依赖：

```sh
cmake -S . -B build-macos -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="/path/to/Qt;/path/to/SDL2;/path/to/Boost"
cmake --build build-macos --parallel 2
cpack --config build-macos/CPackConfig.cmake -G DragNDrop -B dist
```

安装阶段调用 `macdeployqt` 部署 `.app`，CPack DragNDrop 生成 DMG。
在真实目标机检查 `otool -L`、插件、架构、图标及启动，确保不残留开发路径。
本配置未提供正式签名身份或公证流程；不能据此宣称 Gatekeeper 验收通过。

## 安装后验收

从安装目录之外以普通用户启动，清除开发用 `LD_LIBRARY_PATH`、Qt 插件路径等环境变量。
检查动态库、平台/SVG/QSQLITE 插件、数据库保存、MP3/FLAC/WAV/AAC 播放、
暂停恢复、seek、切歌、批量与重复导入、播放中退出、重启恢复及升级/卸载后的数据保留。
X11、原生 Wayland、Windows 和 macOS 分别记录实际结果。
Windows 还需测试带空格与非 ASCII 字符的安装路径；macOS 需单独记录签名、公证状态。

Linux 安装后，可用标准 Python 启动回归验证只读安装目录和用户目录写入：

```sh
python3 tests/check_installed_startup.py /usr/bin/MusicPlayer
```

该项只验证启动和文件位置，以普通用户运行；播放和正常退出仍需上述交互验收。

没有目标平台的项目保留为待执行项。原有播放链路验收见
[独立验收记录](docs/review-2026-10-26-acceptance.md)。
