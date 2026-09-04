#!/usr/bin/env python3
import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
MENU = ROOT / "include" / "menu.h"
SKETCH = ROOT / "src" / "hds.ino"
SUBMENU_LINKS = {
    "menuConnections": "menuConnectionsBack",
    "menuDisplay": "menuDisplayBack",
    "menuGrinder": "menuGrinderBack",
    "menuInfo": "menuInfoBack",
    "menuPower": "menuPowerBack",
    "menuScale": "menuScaleBack",
}

OBSOLETE_SUBMENUS = (
    "buzzerMenu",
    "heartbeatMenu",
    "flipScreenMenu",
    "timeOnTopMenu",
    "btnFuncWhileConnectedMenu",
    "autoSleepMenu",
    "quickBootMenu",
    "driftCompMenu",
    "tapActionsMenu",
    "energyMenu",
)


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
    assert all(name not in menu for name in OBSOLETE_SUBMENUS)
    assert "const Menu *const scaleMenu[]" in menu
    assert "const Menu *const connectionsMenu[]" in menu
    assert "const Menu *const displayMenu[]" in menu
    assert "const Menu *const powerMenu[]" in menu
    assert "const Menu *const infoMenu[]" in menu
    assert re.search(
        r"#if HDS_ENABLE_GRINDER\s+&menuGrinder,\s+#endif", menu
    )
    assert re.search(
        r"#if HDS_ENABLE_ENERGY_MENU\s+&menuEnergyOledRedraw", menu
    )
    assert "void backMenu()" in menu
    assert "navigateMenu(-1);" in sketch
    for view in ("showWifiStatus", "showStatus", "showAbout", "showLogo"):
        body = menu[menu.rindex(f"void {view}()") :]
        body = body[: body.index("\n}")]
        assert "waitForMenuButtonRelease();" in body
    pressed = sketch[sketch.index("case AceButton::kEventPressed:") :]
    pressed = pressed[: pressed.index("case AceButton::kEventDoubleClicked:")]
    menu_pressed = pressed[pressed.index("if (b_menu) {") :]
    assert "recordEnergyActivity();" in menu_pressed

    print("menu memory contract tests passed")


if __name__ == "__main__":
    main()
