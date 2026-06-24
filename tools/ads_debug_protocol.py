MODEL_BYTE = 0x03
ADS_DEBUG_TYPE = 0x25
ADS_RESET_TYPE = 0x26
ADS_SAMPLES_TYPE = 0x1D
DEBUG_PACKET_LENGTH = 41
RESET_RESPONSE_LENGTH = 5
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
    for i in range(len(buffer) - 1):
        if buffer[i] == MODEL_BYTE and buffer[i + 1] == packet_type:
            if len(buffer) >= i + packet_length:
                packet = buffer[i:i + packet_length]
                remaining = buffer[i + packet_length:]
                return packet, remaining
            return None, buffer

    if len(buffer) > 0:
        return None, buffer[-1:]
    return None, buffer


def find_debug_packet(buffer):
    return find_packet(buffer, ADS_DEBUG_TYPE, DEBUG_PACKET_LENGTH)


def find_reset_response(buffer):
    return find_packet(buffer, ADS_RESET_TYPE, RESET_RESPONSE_LENGTH)


def is_debug_packet(data):
    return len(data) == DEBUG_PACKET_LENGTH and data[0] == MODEL_BYTE and data[1] == ADS_DEBUG_TYPE
