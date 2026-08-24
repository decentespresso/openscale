from pathlib import Path


HEADER = Path(__file__).resolve().parents[1] / "include" / "decent_protocol.h"


def main():
    source = HEADER.read_text(encoding="utf-8")
    start = source.index("    case 0x20:", source.index("decentCommandFrameLength"))
    end = source.index("#if defined(ACC_MPU6050)", start)
    weight_frame = source[start:end]
    expected = [
        "if (len < 3)",
        "if (data[2] != 0x01)",
        "return 3;",
        "if (len >= 4)",
        "return 4;",
        "return allowShort ? 3 : 0;",
    ]
    cursor = 0
    for snippet in expected:
        cursor = weight_frame.index(snippet, cursor) + len(snippet)
    print("Decent weight frame contract tests passed")


if __name__ == "__main__":
    main()
