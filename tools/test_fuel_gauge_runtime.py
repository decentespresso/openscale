import os
from pathlib import Path
import shutil
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[1]


def main():
    compiler = os.environ.get("CXX") or shutil.which("g++") or shutil.which("clang++")
    if not compiler:
        raise SystemExit("A C++ compiler is required (g++, clang++, or CXX).")
    with tempfile.TemporaryDirectory(prefix="fuel-gauge-runtime-") as directory:
        binary = Path(directory) / ("runtime.exe" if os.name == "nt" else "runtime")
        subprocess.run([
            compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror", "-DESP32",
            "-I", str(ROOT / "tools/fuel_gauge_test"), "-I", str(ROOT / "include"),
            str(ROOT / "src/fuel_gauge.cpp"), str(ROOT / "src/fuel_gauge_config.cpp"),
            str(ROOT / "tools/fuel_gauge_test/runtime.cpp"), "-o", str(binary),
        ], check=True)
        subprocess.run([str(binary)], check=True)
    print("fuel gauge runtime tests passed")


if __name__ == "__main__":
    main()
