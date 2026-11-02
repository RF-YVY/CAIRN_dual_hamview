import argparse
from pathlib import Path
import shutil
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument("--zig", default=shutil.which("zig"))
parser.add_argument("--output", required=True, type=Path)
args = parser.parse_args()
if not args.zig:
    parser.error("Pass --zig with the path to a Zig 0.14.1 executable")
project = Path(__file__).resolve().parents[1]
lvgl = project / ".pio/libdeps/sensecap-meshcore/lvgl"
build = project / ".pio/native-preview"
build.mkdir(parents=True, exist_ok=True)
args.output.mkdir(parents=True, exist_ok=True)
exe = build / "cairn-preview.exe"
sources = sorted((lvgl / "src").rglob("*.c"))
sources.append(project / "src/fonts/cairn_clock_72.c")
command = ["-target", "x86_64-windows-gnu", "-O", "ReleaseSafe", "-lc", "-lc++",
           "-DLV_CONF_INCLUDE_SIMPLE", "-I" + str(project / "ui"), "-I" + str(project / "src"),
           "-I" + str(lvgl), "-femit-bin=" + str(exe), "-cflags", "-std=c++17", "--",
           str(project / "src/CairnDashboard.cpp"), str(project / "simulator/main.cpp"),
           "-cflags", "-std=gnu11", "-fno-sanitize=undefined", "--"]
command.extend(str(path) for path in sources)
response = build / "arguments.rsp"
response.write_text("\n".join('"' + arg.replace("\\", "/") + '"' for arg in command), encoding="utf-8")
subprocess.run([args.zig, "build-exe", "@" + str(response)], check=True)
for name, flags in (("cairn-home-empty", []), ("cairn-home-sample", ["--sample"])):
    bmp = args.output / (name + ".bmp")
    subprocess.run([str(exe), str(bmp)] + flags, check=True)
    from PIL import Image
    Image.open(bmp).save(args.output / (name + ".png"))
print(args.output)
