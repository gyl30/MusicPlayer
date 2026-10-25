"""在已有 Linux/Unix Makefiles 构建上测量导入，所有用户数据写入临时目录。"""
import json
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile

build = Path(sys.argv[1]).resolve()
paths = Path(sys.argv[2]).resolve()
source = Path(__file__).with_name("import_benchmark.cpp").resolve()
entries = json.loads((build / "compile_commands.json").read_text())
entry = next(item for item in entries if item["file"].endswith("/main.cpp"))
with tempfile.TemporaryDirectory(prefix="musicplayer-import-benchmark-") as directory:
    executable = str(Path(directory) / "import_benchmark")
    object_file = executable + ".o"
    compile_args = shlex.split(entry["command"])
    compile_args[compile_args.index("-o") + 1] = object_file
    compile_args[-1] = str(source)
    compile_args.extend(["-I", str(Path(entry["file"]).parent)])
    subprocess.run(compile_args, cwd=entry["directory"], check=True)
    ui_entry = next(item for item in entries if item["file"].endswith("/playlist_window.cpp"))
    ui_source = Path(ui_entry["file"]).read_text()
    ui_source = "#include <atomic>\n#include <QElapsedTimer>\n#include \"scoped_exit.h\"\n" + ui_source
    declaration_at = ui_source.index("static QTreeWidgetItem*")
    declarations = "extern std::atomic<qint64> benchmark_ui_ns;\nextern std::atomic<qint64> benchmark_metadata_ui_ns;\n"
    ui_source = ui_source[:declaration_at] + declarations + ui_source[declaration_at:]
    marker = "void playlist_window::on_songs_changed(qint64 playlist_id)\n{"
    assert ui_source.count(marker) == 1
    ui_source = ui_source.replace(marker, marker + "\n    QElapsedTimer measured; measured.start();\n    DEFER(benchmark_ui_ns += measured.nsecsElapsed());")
    marker = "[this](const QMap<QString, qint64>& durations)\n    {"
    ui_source = ui_source.replace(marker, marker + "\n        QElapsedTimer measured; measured.start();\n        DEFER(benchmark_metadata_ui_ns += measured.nsecsElapsed());")
    measured_source = Path(directory) / "playlist_window.cpp"
    measured_source.write_text(ui_source)
    ui_object = executable + "-ui.o"
    ui_args = shlex.split(ui_entry["command"])
    ui_args[ui_args.index("-o") + 1] = ui_object
    ui_args[-1] = str(measured_source)
    ui_args.extend(["-I", str(Path(ui_entry["file"]).parent)])
    subprocess.run(ui_args, cwd=ui_entry["directory"], check=True)
    link_args = shlex.split((build / "CMakeFiles/MusicPlayer.dir/link.txt").read_text())
    link_args = [object_file if item.endswith("/main.cpp.o") else
                 ui_object if item.endswith("/playlist_window.cpp.o") else item for item in link_args]
    link_args[link_args.index("-o") + 1] = executable
    link_args.extend(["-lsqlite3", "-Wl,--export-dynamic-symbol=sqlite3_step",
                      "-Wl,--wrap=avformat_open_input", "-Wl,--wrap=avformat_find_stream_info"])
    subprocess.run(link_args, cwd=build, check=True)
    environment = dict(os.environ, QT_QPA_PLATFORM="offscreen", SDL_AUDIODRIVER="dummy")
    subprocess.run([executable, str(paths)], env=environment, check=True)
