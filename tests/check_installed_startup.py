"""以普通 Linux 用户检查已安装程序的启动和用户目录写入。"""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time

executable = Path(sys.argv[1]).resolve()
if sys.platform != "linux" or os.access(executable.parent, os.W_OK):
    raise SystemExit("请以普通 Linux 用户测试不可写安装目录中的程序")

with tempfile.TemporaryDirectory(prefix="musicplayer-installed-") as directory:
    root = Path(directory)
    environment = dict(os.environ)
    for name in ("LD_LIBRARY_PATH", "QT_PLUGIN_PATH", "QT_QPA_PLATFORM_PLUGIN_PATH"):
        environment.pop(name, None)
    environment.update(XDG_DATA_HOME=str(root / "data"), XDG_CONFIG_HOME=str(root / "config"),
                       QT_QPA_PLATFORM="offscreen", SDL_AUDIODRIVER="dummy")
    with (root / "output.log").open("w+") as output:
        process = subprocess.Popen([str(executable)], cwd=root, env=environment, stdout=output, stderr=output)
        try:
            deadline = time.monotonic() + 10
            while process.poll() is None and time.monotonic() < deadline:
                if list((root / "data").rglob("music_library.db")) and list((root / "data").rglob("MusicPlayer.log")):
                    print("PASS: 已安装程序启动，并将数据库和日志写入用户目录")
                    break
                time.sleep(0.05)
            else:
                output.seek(0)
                raise SystemExit(f"FAIL: 启动未完成，exit={process.poll()}\n{output.read()}")
        finally:
            if process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()
