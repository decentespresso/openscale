#!/usr/bin/env python3
import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
MENU = ROOT / "include" / "menu.h"
SKETCH = ROOT / "src" / "hds.ino"
SUBMENU_LINKS = {
    "menuAutoSleep": "menuAutoSleepBack",
    "menuBtnFuncWhileConnected": "menuBtnFuncWhileConnectedBack",
    "menuBuzzer": "menuBuzzerBack",
    "menuCalibration": "menuCalibrationBack",
    "menuDriftComp": "menuDriftCompBack",
    "menuFlipScreen": "menuFlipScreenBack",
    "menuGrinder": "menuGrinderBack",
    "menuHeartbeat": "menuHeartbeatBack",
    "menuQuickBoot": "menuQuickBootBack",
    "menuTapActions": "menuTapActionsBack",
    "menuTimeOnTop": "menuTimeOnTopBack",
    "menuWifi": "menuWiFiUpdateBack",
}


def main():
    menu = MENU.read_text(encoding="utf-8")
    sketch = SKETCH.read_text(encoding="utf-8")
    declarations = re.findall(
        r"^const Menu (menu\w+) = \{(.*?)\};",
        menu,
        flags=re.MULTILINE | re.DOTALL,
    )
    links = {}
    for name, initializer in declarations:
        match = re.search(r",\s*(&menu\w+|NULL)\s*,\s*(?:&menu\w+|NULL)\s*$", initializer)
        assert match is not None
        if match.group(1) != "NULL":
            links[name] = match.group(1)[1:]

    assert links == SUBMENU_LINKS
    assert "\nMenu menu" not in menu
    assert re.search(r"^Menu \*const", menu, flags=re.MULTILINE) is None
    assert "currentSelection->subMenu" in menu
    assert "linkSubmenus" not in menu
    assert "linkSubmenus" not in sketch

    print("menu memory contract tests passed")


if __name__ == "__main__":
    main()
