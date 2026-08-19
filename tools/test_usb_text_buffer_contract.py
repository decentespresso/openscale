from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
USB_HEADER = ROOT / "include" / "usbcomm.h"


def main():
    usb = USB_HEADER.read_text(encoding="utf-8")
    text_path = usb[usb.index("if (data[0] != 0x03)"):]
    text_path = text_path[:text_path.index("UsbDecentCommandSink sink;")]

    if "String input((const char *)data, len);" not in text_path:
        raise AssertionError("USB text does not use the known-length String constructor")
    if "input.reserve(len)" in text_path or "input +=" in text_path:
        raise AssertionError("USB text is accumulated byte by byte")

    print("USB text buffer contract tests passed")


if __name__ == "__main__":
    main()
