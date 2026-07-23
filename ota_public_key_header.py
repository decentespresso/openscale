import runpy
from pathlib import Path

try:
    Import("env")
except NameError:
    generator = Path(__file__).resolve().parent / "tools" / "write_ota_public_key_header.py"
else:
    generator = None if env.IsCleanTarget() else env.subst(
        "$PROJECT_DIR/tools/write_ota_public_key_header.py"
    )

if generator is not None:
    runpy.run_path(str(generator), run_name="__main__")
