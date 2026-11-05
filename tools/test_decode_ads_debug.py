import struct
import unittest
from contextlib import redirect_stdout
from io import StringIO

from ads_debug_protocol import append_checksum
from decode_ads_debug import decode_ads_debug_packet, decode_ads_reset_response, print_reset_response


def build_debug_packet():
    payload = bytearray()
    payload.extend([0x03, 0x25])
    payload.extend(struct.pack(">I", 1234))
    payload.extend(struct.pack(">i", -100))
    payload.extend(struct.pack(">i", -90))
    payload.extend(struct.pack(">i", 10))
    payload.extend(struct.pack(">H", 156))
    payload.extend(struct.pack(">H", 8000))
    payload.extend([4, 2])
    payload.extend([7])
    payload.extend(bytes(13))
    payload.extend([0x03, 0x00])
    return append_checksum(bytes(payload))


class DecodeAdsDebugTest(unittest.TestCase):
    def test_current_debug_packet_layout(self):
        info = decode_ads_debug_packet(build_debug_packet())
        self.assertEqual(info["timestamp"], 1234)
        self.assertEqual(info["rawValue"], -100)
        self.assertEqual(info["smoothedValue"], -90)
        self.assertEqual(info["tareOffset"], 10)
        self.assertEqual(info["conversionTime"], 1.56)
        self.assertEqual(info["sps"], 80.0)
        self.assertEqual(info["readIndex"], 4)
        self.assertEqual(info["samplesInUse"], 2)
        self.assertEqual(info["resetReason"], 7)
        self.assertEqual(info["reserved"], bytes(13))
        self.assertTrue(info["dataOutOfRange"])
        self.assertTrue(info["signalTimeout"])
        self.assertNotIn("dataMin", info)
        self.assertNotIn("tareInProgress", info)

    def test_reset_response_checksum(self):
        info = decode_ads_reset_response(bytes([0x03, 0x26, 0x02, 0x00, 0x27]))
        self.assertTrue(info["success"])
        self.assertEqual(info["mode"], 0x02)

    def test_reset_response_reports_each_status(self):
        cases = (
            (0x00, "SUCCESS"),
            (0x01, "FAILED (DOUT timeout)"),
            (0x02, "FAILED (dataset/tare refresh timeout)"),
            (0xAB, "FAILED (unknown status 0xAB)"),
        )
        for status, expected in cases:
            with self.subTest(status=status):
                packet = append_checksum(bytes([0x03, 0x26, 0x02, status]))
                info = decode_ads_reset_response(packet)
                self.assertEqual(info["status"], status)
                self.assertEqual(info["success"], status == 0)
                output = StringIO()
                with redirect_stdout(output):
                    print_reset_response(info)
                self.assertIn(f"Status: {expected}\n", output.getvalue())
                if status == 0x02:
                    self.assertNotIn("DOUT timeout", output.getvalue())


if __name__ == "__main__":
    unittest.main()
