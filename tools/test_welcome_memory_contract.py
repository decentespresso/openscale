#!/usr/bin/env python3
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
PARAMETER = (ROOT / "include" / "parameter.h").read_text(encoding="utf-8")
SKETCH = (ROOT / "src" / "hds.ino").read_text(encoding="utf-8")


assert "str_welcome" not in PARAMETER
assert "str_welcome" not in SKETCH
assert SKETCH.count("String welcome =") == 2
assert "String welcome = storageGetString(KEY_WELCOME, String(WELCOME1));" in SKETCH
assert "welcome.trim();" in SKETCH
assert 'String welcome = "welcome";' in SKETCH
assert "if (welcome.length() == 127)" in SKETCH
assert "Serial.print(welcome);" in SKETCH

print("welcome memory contract tests passed")
