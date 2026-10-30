MusicPlayer Codex UI 重构交接包

包含：
- reference.png  用户确认的最终 UI 参考图（无频谱）
- TASK.md        Codex 的完整开发任务、关键代码路径与验收要求

使用方法：
1. 下载本 ZIP，在本地 MusicPlayer 仓库根目录执行：
   mkdir -p designs/musicplayer-final-reference
   unzip /path/to/MusicPlayer-Codex-UI-Refactor.zip -d designs/musicplayer-final-reference
2. 运行 codex，输入：
   请严格按照 designs/musicplayer-final-reference/TASK.md 执行；先查看同目录 reference.png，实际修改 Qt Widgets 项目代码并测试；不要自动提交或推送。

若 Codex CLI 支持图片参数，也可使用：
   codex -i designs/musicplayer-final-reference/reference.png
然后提供上面的任务说明。
