import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
HDS = (ROOT / "src" / "hds.ino").read_text(encoding="utf-8")
MENU = (ROOT / "include" / "menu.h").read_text(encoding="utf-8")
PARAMETER = (ROOT / "include" / "parameter.h").read_text(encoding="utf-8")
WEBSOCKET = (ROOT / "include" / "websocket.h").read_text(encoding="utf-8")
BLE = (ROOT / "include" / "ble.h").read_text(encoding="utf-8")
WIFI = (ROOT / "src" / "wifi_setup.cpp").read_text(encoding="utf-8")
WIFI_HEADER = (ROOT / "include" / "wifi_setup.h").read_text(encoding="utf-8")


def function_body(source, name):
    match = re.search(rf"\b\w+\s+{re.escape(name)}\([^;{{}}]*\)\s*{{", source)
    if match is None:
        raise AssertionError(f"missing function: {name}")
    opening = source.index("{", match.start())
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[opening + 1:index]
    raise AssertionError(f"unterminated function: {name}")


def elapsed(now, last, interval):
    return ((now - last) & 0xFFFFFFFF) >= interval


class ClientCount:
    def __init__(self):
        self.count = 0

    def connect(self, actual_count):
        self.count = actual_count

    def disconnect(self, actual_count):
        self.count = actual_count


class IdleOptimizationContractTests(unittest.TestCase):
    def test_websocket_client_cache_and_cleanup_contract(self):
        self.assertIn("std::atomic<uint8_t> g_websocketClientCount{0};", PARAMETER)
        self.assertIn("g_websocketClientCount.load(std::memory_order_relaxed)", PARAMETER)
        self.assertIn("inline bool websocketHasClients()", PARAMETER)
        connect = WEBSOCKET[WEBSOCKET.index("if (type == WS_EVT_CONNECT)"):]
        self.assertIn("server->cleanupClients(4);", connect)
        self.assertIn("g_websocketClientCount.store(server->count(), std::memory_order_relaxed);", connect)
        self.assertEqual(WEBSOCKET.count("server->cleanupClients(4);"), 1)
        self.assertEqual(WEBSOCKET.count("server->count()"), 2)
        loop = function_body(HDS, "loop")
        self.assertNotIn("websocket.count()", loop)
        self.assertNotIn("cleanupClients", loop)
        wifi_init = function_body(HDS, "_wifi_init")
        self.assertLess(wifi_init.index("setupWebsocketEvents();"), wifi_init.index("startWebServer();"))

        cache = ClientCount()
        cache.connect(1)
        self.assertEqual(cache.count, 1)
        cache.disconnect(0)
        self.assertEqual(cache.count, 0)

    def test_wifi_cadence_and_rollover_contract(self):
        self.assertIn("WIFI_SUPERVISE_INTERVAL_MS = 250", WIFI_HEADER)
        body = function_body(WIFI, "wifiSupervise")
        gate = "hdsIntervalElapsed(now, lastRun, WIFI_SUPERVISE_INTERVAL_MS)"
        self.assertIn(gate, body)
        self.assertLess(body.index(gate), body.index("WiFi.status()"))
        self.assertFalse(elapsed(249, 0, 250))
        self.assertTrue(elapsed(250, 0, 250))
        self.assertTrue(elapsed(0x00000010, 0xFFFFFF00, 250))

    def test_menu_dirty_refresh_contract(self):
        self.assertIn("MENU_SAFETY_REFRESH_INTERVAL_MS = 100", MENU)
        self.assertIn("void invalidateMenuFrame()", MENU)
        self.assertIn("menuFrameShowsActionMessage && !actionMessageVisible", MENU)
        show_menu = function_body(MENU, "showMenu")
        self.assertLess(show_menu.index("menuFrameNeedsRender(now)"), show_menu.index("u8g2.firstPage()"))
        self.assertIn("lastMenuFrameRender = now;", show_menu)
        self.assertIn("menuFrameDirty = false;", show_menu)
        self.assertIn("invalidateMenuFrame();", function_body(MENU, "navigateMenu"))
        self.assertIn("invalidateMenuFrame();", function_body(MENU, "selectMenu"))
        self.assertIn("invalidateMenuFrame();", function_body(MENU, "menuActionMessageChanged"))
        self.assertTrue(elapsed(100, 0, 100))
        self.assertTrue(elapsed(0x00000010, 0xFFFFFFA0, 100))

    def test_pending_consumers_fast_path_before_lock(self):
        ws = function_body(WEBSOCKET, "processWsPendingCmds")
        ble = function_body(BLE, "processBleStatusResponse")
        self.assertLess(ws.index("if (wsPendingMask == 0) return;"), ws.index("portENTER_CRITICAL(&wsPendingMux)"))
        self.assertLess(ble.index("if (bleStatusResponsesPending == 0) return;"), ble.index("portENTER_CRITICAL(&bleFff4Mux)"))
        self.assertLess(ws.index("uint32_t mask = b_ota ? (wsPendingMask & WSP_OTA_RESET) : wsPendingMask;"), ws.index("portEXIT_CRITICAL(&wsPendingMux)"))
        self.assertLess(ble.index("bleStatusResponsesPending = bleStatusResponsesPending - 1;"), ble.index("portEXIT_CRITICAL(&bleFff4Mux)"))

    def test_button_poll_cadence_contract(self):
        self.assertIn("BUTTON_POLL_INTERVAL_MS = 2", HDS)
        loop = function_body(HDS, "loop")
        gate = "hdsIntervalElapsed(now, lastButtonPoll, BUTTON_POLL_INTERVAL_MS)"
        self.assertIn(gate, loop)
        self.assertLess(loop.index(gate), loop.index("buttonCircle.check();"))
        self.assertLess(loop.index(gate), loop.index("buttonSquare.check();"))
        self.assertLess(loop.index("handleGrinderMenuChord()"), loop.index(gate))
        self.assertTrue(elapsed(2, 0, 2))
        self.assertFalse(elapsed(1, 0, 2))
        self.assertTrue(elapsed(0x00000001, 0xFFFFFFFF, 2))


if __name__ == "__main__":
    unittest.main()
