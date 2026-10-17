import unittest

from ads_debug_protocol import (
    append_checksum,
    build_debug_command,
    build_reset_command,
    build_samples_command,
    find_debug_packet,
    find_reset_response,
    format_hex,
    HEARTBEAT_CMD,
    xor_checksum,
)


DEBUG_CHECKSUM_COLLISION_RESPONSE = bytes.fromhex(
    "03 25 00 01 D5 B8 00 01 86 A0 00 01 86 A0 00 01 82 B8 04 E2 "
    "1F 40 00 04 01 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 CD"
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
        packet = append_checksum(bytes([0x03, 0x25]) + bytes(range(38)))
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

    def test_find_packets_resynchronizes_after_weight_traffic(self):
        cases = (
            (find_debug_packet, bytes.fromhex("03 CE 03 25 00 00 EB"),
             append_checksum(bytes([0x03, 0x25]) + bytes(38))),
            (find_reset_response, bytes.fromhex("03 CE 03 26 00 00 E8"),
             append_checksum(bytes([0x03, 0x26, 0x02, 0x00]))),
        )
        for finder, weight, response in cases:
            stream = weight + response + b"tail"
            for split in range(len(stream) + 1):
                with self.subTest(finder=finder.__name__, split=split):
                    packet, remaining = finder(bytearray(stream[:split]))
                    if packet is None:
                        packet, remaining = finder(remaining + stream[split:])
                    else:
                        remaining = remaining + stream[split:]
                    self.assertEqual(packet, response)
                    self.assertEqual(remaining, bytearray(b"tail"))

    def test_find_packets_skips_invalid_checksum_candidates(self):
        cases = (
            (find_debug_packet, append_checksum(bytes([0x03, 0x25]) + bytes(38))),
            (find_reset_response, append_checksum(bytes([0x03, 0x26, 0x02, 0x00]))),
        )
        for finder, response in cases:
            with self.subTest(finder=finder.__name__):
                corrupt = response[:-1] + bytes([response[-1] ^ 0x01])
                packet, remaining = finder(bytearray(corrupt + response + b"tail"))
                self.assertEqual(packet, response)
                self.assertEqual(remaining, bytearray(b"tail"))

    def test_find_debug_packet_skips_valid_weight_checksum_collision(self):
        response = DEBUG_CHECKSUM_COLLISION_RESPONSE
        stream = bytes.fromhex("03 CE 03 25 00 00 EB") + response + b"tail"
        falseCandidate = stream[2:43]
        self.assertEqual(falseCandidate[-1], xor_checksum(falseCandidate[:-1]))
        for chunkSize in range(1, len(stream) + 1):
            with self.subTest(chunkSize=chunkSize):
                packet = None
                remaining = bytearray()
                for offset in range(0, len(stream), chunkSize):
                    remaining = remaining + stream[offset:offset + chunkSize]
                    if packet is None:
                        packet, remaining = find_debug_packet(remaining)
                self.assertEqual(packet, response)
                self.assertEqual(remaining, bytearray(b"tail"))

    def test_find_reset_response_skips_debug_payload_with_valid_reset_header(self):
        debug = DEBUG_CHECKSUM_COLLISION_RESPONSE
        snapshot = append_checksum(
            debug[:6] + bytes.fromhex("00 03 26 02 00 27 00 00") + debug[14:-1]
        )
        response = append_checksum(bytes([0x03, 0x26, 0x02, 0x00]))
        stream = snapshot + response + b"tail"
        for split in range(len(stream) + 1):
            with self.subTest(split=split):
                packet, remaining = find_reset_response(bytearray(stream[:split]))
                if packet is None:
                    packet, remaining = find_reset_response(remaining + stream[split:])
                else:
                    remaining = remaining + stream[split:]
                self.assertEqual(packet, response)
                self.assertEqual(remaining, bytearray(b"tail"))

    def test_find_packet_consumes_complete_non_target_frame(self):
        weight = bytes.fromhex("03 CE 00 CE 00 00 03")
        packet, remaining = find_debug_packet(bytearray(weight))
        self.assertIsNone(packet)
        self.assertEqual(remaining, bytearray())

    def test_format_hex_and_heartbeat(self):
        self.assertEqual(format_hex(bytes([0x03, 0x25, 0x02, 0x24])), "03 25 02 24")
        self.assertEqual(HEARTBEAT_CMD, bytes([0x03, 0x0A, 0x03, 0xFF, 0xFF, 0x00, 0x0A]))


if __name__ == "__main__":
    unittest.main()
