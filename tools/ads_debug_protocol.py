MODEL_BYTE = 0x03
ADS_DEBUG_TYPE = 0x25
ADS_RESET_TYPE = 0x26
ADS_SAMPLES_TYPE = 0x1D
DEBUG_PACKET_LENGTH = 41
RESET_RESPONSE_LENGTH = 5
SERIAL_PACKET_LENGTHS = {
    0x0A: 7,
    0x21: 7,
    0x22: 7,
    ADS_DEBUG_TYPE: DEBUG_PACKET_LENGTH,
    ADS_RESET_TYPE: RESET_RESPONSE_LENGTH,
    0x2A: 7,
    0xAA: 7,
    0xCE: 7,
}
HEARTBEAT_CMD = bytes([0x03, 0x0A, 0x03, 0xFF, 0xFF, 0x00, 0x0A])


def xor_checksum(data):
    checksum = 0
    for byte in data:
        checksum ^= byte
    return checksum


def append_checksum(payload):
    return payload + bytes([xor_checksum(payload)])


def build_debug_command(mode):
    return append_checksum(bytes([MODEL_BYTE, ADS_DEBUG_TYPE, mode]))


def build_reset_command(mode):
    return append_checksum(bytes([MODEL_BYTE, ADS_RESET_TYPE, mode]))


def build_samples_command(sample_count):
    count_to_byte = {1: 0x00, 2: 0x01, 4: 0x03}
    return append_checksum(bytes([MODEL_BYTE, ADS_SAMPLES_TYPE, count_to_byte[sample_count]]))


def format_hex(data):
    return " ".join(f"{byte:02X}" for byte in data)


def find_packet(buffer, packet_type, packet_length):
    offset = 0
    while offset + 1 < len(buffer):
        if buffer[offset] != MODEL_BYTE:
            offset += 1
            continue
        frameType = buffer[offset + 1]
        frameLength = packet_length if frameType == packet_type else SERIAL_PACKET_LENGTHS.get(frameType)
        if frameLength is None:
            offset += 1
            continue
        if len(buffer) < offset + frameLength:
            return None, buffer[offset:]
        packet = buffer[offset:offset + frameLength]
        if packet[-1] != xor_checksum(packet[:-1]):
            offset += 1
            continue
        offset += frameLength
        if frameType == packet_type:
            return packet, buffer[offset:]

    return None, buffer[offset:]


def find_debug_packet(buffer):
    return find_packet(buffer, ADS_DEBUG_TYPE, DEBUG_PACKET_LENGTH)


def find_reset_response(buffer):
    return find_packet(buffer, ADS_RESET_TYPE, RESET_RESPONSE_LENGTH)


def is_debug_packet(data):
    return len(data) == DEBUG_PACKET_LENGTH and data[0] == MODEL_BYTE and data[1] == ADS_DEBUG_TYPE
