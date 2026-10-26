# 第三方组件与分发边界

MusicPlayer 目前未声明项目自身许可证。本地安装验收包不等同于获得公开分发授权；
公开发行前须由项目权利人明确许可证，并与实际依赖构建的许可条件核对。

编入程序的源码/头文件组件：

| 组件 | 版本或来源 | 许可文件 |
| --- | --- | --- |
| spdlog | 仓库内 1.15.3 | spdlog-LICENSE.txt，MIT |
| fmt | spdlog 内置 11.2.0 | fmt-LICENSE.txt，MIT 及嵌入对象代码例外 |
| Boost lockfree | 构建时安装的 Boost | boost-LICENSE.txt，BSL-1.0 |
| fftreal.h | Dmitry V. Benko，2018，文件内声明 | fftreal-LICENSE.txt，MIT |

fmt 原文来源：<https://github.com/fmtlib/fmt/blob/11.2.0/LICENSE>；
Boost 原文来源：<https://www.boost.org/LICENSE_1_0.txt>。

运行库的许可取决于实际安装/打包的版本与配置：

- Qt Widgets、Multimedia、SVG、SQL 使用共享库。本轮 Debian 包由系统包提供，
  其版权及第三方声明在 `/usr/share/doc/` 对应 Qt 包中；不能只凭模块名认定许可。
  参见 <https://doc.qt.io/qt-6/licensing.html>。
- SDL2 的主要许可是 zlib，具体平台代码和第三方声明随所选 SDL 构建核查。
  Debian 系统包声明在 `/usr/share/doc/libsdl2-2.0-0/copyright`。
- FFmpeg 的 LGPL/GPL 条件由编译配置决定。启用 GPL 组件后不能按纯 LGPL 构建处理；
  动态链接也不会消除这些条件。本轮 Debian 共享库使用发行版的 GPL 构建，
  版权及组件清单见 `/usr/share/doc/libavcodec59/copyright` 等依赖包。
  参见 <https://ffmpeg.org/legal.html>。
- SQLite 通过 Qt QSQLITE 驱动访问，不直接链接自行部署的 SQLite 副本。
  应同时保留实际 Qt SQL 驱动和 SQLite 依赖包的版权声明。

Linux DEB 不捆绑上述系统共享库，包管理器负责安装依赖及其许可文件。
Windows/macOS 若携带这些运行库，发布者须额外收集对应精确版本的许可、
构建配置与所需的对应源码材料；Qt 部署工具不会完成全部许可核验。
本文件不为未知构建声明整体合规，也不替代这些组件各自的许可文本。
