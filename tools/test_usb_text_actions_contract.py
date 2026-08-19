from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
USB_HEADER = ROOT / "include" / "usbcomm.h"
HDS_SOURCE = ROOT / "src" / "hds.ino"


def block_after(text, marker):
    start = text.index(marker)
    opening = text.index("{", start)
    depth = 0
    for index in range(opening, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return text[opening + 1:index]
    raise AssertionError(f"unterminated block: {marker}")


def main():
    usb = USB_HEADER.read_text(encoding="utf-8")
    hds = HDS_SOURCE.read_text(encoding="utf-8")

    tare = block_after(usb, 'if (inputString.startsWith("tare"))')
    if "requestRemoteTare();" not in tare:
        raise AssertionError("USB text tare does not request a remote tare")
    if "b_menu || b_calibration || b_showChargingUI" not in tare:
        raise AssertionError("USB text tare lost non-weighing button behavior")

    timer = block_after(usb, 'if (inputString.startsWith("set"))')
    if "toggleTimer();" not in timer:
        raise AssertionError("USB text set does not toggle the timer")
    if "b_menu || b_calibration || b_showChargingUI" not in timer:
        raise AssertionError("USB text set lost non-weighing button behavior")

    if "usbCallbacks.toggleTimer = scaleTimer;" not in hds:
        raise AssertionError("USB timer callback is not wired")

    print("USB text action contract tests passed")


if __name__ == "__main__":
    main()
