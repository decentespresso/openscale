from pathlib import Path
import re


ROOT = Path(__file__).resolve().parents[1]
PROTOCOL_HEADER = ROOT / "include" / "decent_protocol.h"


def function_body(text, name):
    match = re.search(rf"\b\w+\s+{re.escape(name)}\([^;{{}}]*\)\s*{{", text)
    if match is None:
        raise AssertionError(f"missing function: {name}")
    opening = text.index("{", match.start())
    depth = 0
    for index in range(opening, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return text[opening + 1:index]
    raise AssertionError(f"unterminated function: {name}")


def main():
    protocol = PROTOCOL_HEADER.read_text(encoding="utf-8")
    led_response = function_body(protocol, "buildLedResponsePacket")
    firmware_index = led_response.index("data[5] = verHigh;")
    checksum_index = led_response.index("data[6] = decentXor(data, 6);")
    if checksum_index < firmware_index:
        raise AssertionError("LED response checksum precedes firmware byte")
    if "verLow" in led_response:
        raise AssertionError("LED response byte 6 is not reserved for the checksum")
    print("Decent protocol contract tests passed")


if __name__ == "__main__":
    main()
