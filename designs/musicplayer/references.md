# 简约界面参考与取舍

2026-10-29：只读查看以下项目的官方资料与源码，没有复制实现或引入依赖。

- [Nulloy](https://github.com/nulloy/nulloy)：查看 README 引用的[官方截图](https://nulloy.com/files/screen.png)与 [native/form.ui](https://github.com/nulloy/nulloy/blob/master/src/skins/native/form.ui)。截图将播放区和列表放在同一紧凑窗口；native UI 使用标准 Qt 布局容纳控制与列表。借鉴两区结构，保留本项目现有频谱，不引入波形扫描。
- [Audacious Qt](https://github.com/audacious-media-player/audacious-plugins/tree/master/src/qtui)：查看 main_window.cc 和 tool_bar.cc。播放/暂停共用动作，打开、添加、切歌可直接操作；工具栏在 Windows 上明确去掉与标签栏重复的边框。借鉴单层控件与直接操作，不引入其插件体系或更多工具栏功能。
- [fooyin 官方界面说明](https://docs.fooyin.org/en/latest/quick-start/interface.html)：查看官方截图与控件职责说明。歌单、播放控制、进度、音量职责清楚。其布局编辑系统不适合本项目当前的简单目标，不采用。

本次调整：删除自绘标题栏中的重复应用图标；添加与管理合并到顶栏，取消独立添加底栏；移除播放器卡片、列表卡片和标题栏分隔线；分段音量条简化为连续细线，保留原 24 像素点击高度、键盘和滚轮；缩短管理页重复文字。主窗口和管理页默认高度收至 480。

一键播放、添加、管理及现有停止/模式/音量操作保持可见。管理页隐藏顶栏添加入口，避免在编辑暂存内容时向原歌单导入歌曲。

前一版 HTML 保留为 index-v1.html。当前原型为 index.html；新 Qt 截图为 previews/simple-*.png。先前的完整验收与性能采样属于上一版，此轮另记录 30 项回归和实际虚拟显示截图，不把旧资源数据当作新测量。
