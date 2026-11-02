import argparse
import os
from pathlib import Path
import shutil
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument("--zig", default=shutil.which("zig"))
args = parser.parse_args()
if not args.zig:
    parser.error("Pass --zig with the Zig compiler path")
project = Path(__file__).resolve().parents[1]
build = project / ".pio/native-tests"
build.mkdir(parents=True, exist_ok=True)
common = [args.zig, "c++", "-std=c++17", "-I" + str(project / "test/stubs"), "-I" + str(project / "src")]
cases = [
    ("bond_store", [project / "src/CairnBondStore.cpp"], []),
    ("clock_handoff", [project.parent / "src/graphics/CairnClock.cpp"],
     ["-DCAIRN_MUI=1", "-DARDUINO_ARCH_ESP32=1", "-DCAIRN_CORE_PROFILE=1", "-I" + str(project.parent / "src/graphics")]),
    ("clock", [project.parent / "src/graphics/CairnClock.cpp"],
     ["-DCAIRN_MUI=1", "-I" + str(project.parent / "src/graphics")]),
    ("journal", [project / "src/CairnJournal.cpp"], []),
    ("local_interface", [], ["-I" + str(project / ".pio/libdeps/sensecap-meshcore/MeshCore/src")]),
    ("hamview_protocol", [project.parent / "src/modules/HamViewProtocol.cpp"],
     ["-DHAMVIEW_MUI=1", "-I" + str(project.parent / "src/modules"),
      "-I" + str(project / ".pio/libdeps/sensecap-meshcore/ArduinoJson/src")]),
]
for name, sources, flags in cases:
    if name.startswith("clock") and os.name == "nt":
        print(f"{name} requires POSIX; run {name}.cpp under Linux/WSL", flush=True)
        continue
    executable = build / (name + ".exe")
    subprocess.run(common + flags + [str(path) for path in sources] +
                   [str(project / "test" / (name + ".cpp")), "-o", str(executable)], check=True)
    subprocess.run([str(executable)], check=True)
