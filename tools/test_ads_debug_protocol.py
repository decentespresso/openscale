import unittest

from ads_debug_protocol import (
    build_debug_command,
    build_reset_command,
    build_samples_command,
    find_debug_packet,
    find_reset_response,
    format_hex,
    HEARTBEAT_CMD,
)


class AdsDebugProtocolTest(unittest.TestCase):
    def test_build_debug_commands(self):
        self.assertEqual(build_debug_command(0), bytes([0x03, 0x25, 0x00, 0x26]))
        self.assertEqual(build_debug_command(1), bytes([0x03, 0x25, 0x01, 0x27]))
        self.assertEqual(build_debug_command(2), bytes([0x03, 0x25, 0x02, 0x24]))

    def test_build_reset_command(self):
        self.assertEqual(build_reset_command(2), bytes([0x03, 0x26, 0x02, 0x27]))

    def test_build_samples_commands(self):
        self.assertEqual(build_samples_command(1), bytes([0x03, 0x1D, 0x00, 0x1E]))
        self.assertEqual(build_samples_command(2), bytes([0x03, 0x1D, 0x01, 0x1F]))
        self.assertEqual(build_samples_command(4), bytes([0x03, 0x1D, 0x03, 0x1D]))

    def test_find_debug_packet_preserves_remainder(self):
        packet = bytes([0x03, 0x25]) + bytes(range(39))
        found, remaining = find_debug_packet(bytearray(b"xx") + packet + bytearray(b"tail"))
        self.assertEqual(found, packet)
        self.assertEqual(remaining, bytearray(b"tail"))

    def test_find_packet_keeps_possible_header_byte(self):
        found, remaining = find_debug_packet(bytearray([0x99, 0x03]))
        self.assertIsNone(found)
        self.assertEqual(remaining, bytearray([0x03]))

    def test_find_reset_response(self):
        packet = bytes([0x03, 0x26, 0x02, 0x00, 0x27])
        found, remaining = find_reset_response(bytearray(packet))
        self.assertEqual(found, packet)
        self.assertEqual(remaining, bytearray())

    def test_format_hex_and_heartbeat(self):
        self.assertEqual(format_hex(bytes([0x03, 0x25, 0x02, 0x24])), "03 25 02 24")
        self.assertEqual(HEARTBEAT_CMD, bytes([0x03, 0x0A, 0x03, 0xFF, 0xFF, 0x00, 0x0A]))


if __name__ == "__main__":
    unittest.main()
