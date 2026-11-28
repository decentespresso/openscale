#ifndef GRINDER_BLE_TRANSPORT_STATE_H
#define GRINDER_BLE_TRANSPORT_STATE_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

constexpr uint16_t grinderBleNoConnection = 0xffff;
constexpr uint8_t grinderBleDiscoveryLimit = 6;
constexpr uint8_t grinderBleFragmentSize = 19;
constexpr uint8_t grinderBleReplyLimit = 12;
constexpr uint8_t grinderBleCommandLimit = 64;

enum class GrinderBlePhase : uint8_t {
  Idle, StartScan, Scanning, Connecting, Service, Characteristics, Descriptors, Subscribe, Ready, Closing
};

struct GrinderDiscoveredPlug {
  char mac[18];
};

struct GrinderBleReply {
  uint8_t id;
  uint8_t size;
  uint8_t bytes[grinderBleFragmentSize];
};

struct GrinderBleCommand {
  uint8_t id = 0;
  uint8_t size = 0;
  char bytes[grinderBleCommandLimit] = {};

  static bool isOff(const char *data, size_t length) {
    return (length == 1 && data[0] == '!') || (length == 4 && memcmp(data, "OFF\n", 4) == 0);
  }
};

struct GrinderBleTransport {
  volatile GrinderBlePhase phase = GrinderBlePhase::Idle;
  volatile bool closeRequested = false;
  volatile bool wakePending = false;
  volatile uint32_t generation = 0;
  volatile uint16_t handle = grinderBleNoConnection;
  volatile unsigned long phaseAt = 0;
  volatile unsigned long closeAt = 0;
  volatile uint8_t discoveryCount = 0;
  volatile uint8_t replyStart = 0;
  volatile uint8_t replyCount = 0;
  char target[18] = {};
  GrinderDiscoveredPlug discoveries[grinderBleDiscoveryLimit];
  GrinderBleReply replies[grinderBleReplyLimit];
  GrinderBleCommand command;
  uint8_t ownAddressType = 0;
  uint16_t serviceStart = 0;
  uint16_t serviceEnd = 0;
  uint16_t commandHandle = 0;
  uint16_t replyHandle = 0;
  uint16_t replyEnd = 0;
  uint16_t subscriptionHandle = 0;

  bool current(uint32_t expectedGeneration, uint16_t expectedHandle) const {
    return generation == expectedGeneration && handle == expectedHandle;
  }

  bool connected() const {
    return phase == GrinderBlePhase::Ready && !closeRequested;
  }

  void reset() {
    generation = generation + 1;
    phase = GrinderBlePhase::Idle;
    handle = grinderBleNoConnection;
    closeRequested = false;
    command.size = 0;
    replyStart = 0;
    replyCount = 0;
  }

  bool queueCommand(const char *data, size_t size, uint8_t requestId) {
    if (!connected() || data == nullptr || size == 0 || size > grinderBleCommandLimit || requestId == 0) return false;
    if (command.size != 0 && !GrinderBleCommand::isOff(data, size)) return false;
    command.id = requestId;
    command.size = static_cast<uint8_t>(size);
    memcpy(command.bytes, data, size);
    return true;
  }

  GrinderBleCommand takeCommand() {
    const bool allowed = phase == GrinderBlePhase::Ready &&
                         (!closeRequested || GrinderBleCommand::isOff(command.bytes, command.size));
    const GrinderBleCommand pending = allowed ? command : GrinderBleCommand{};
    command.size = 0;
    return pending;
  }

  bool queueReply(const uint8_t *packet, size_t size) {
    if (size < 2 || size > grinderBleFragmentSize + 1 || replyCount >= grinderBleReplyLimit) return false;
    for (size_t i = 1; i < size; ++i) {
      if (packet[i] != '\n' && (packet[i] < 0x20 || packet[i] > 0x7e)) return false;
    }
    GrinderBleReply &reply = replies[(replyStart + replyCount) % grinderBleReplyLimit];
    reply.id = packet[0];
    reply.size = static_cast<uint8_t>(size - 1);
    memcpy(reply.bytes, packet + 1, reply.size);
    replyCount = replyCount + 1;
    return true;
  }

  size_t readReply(uint8_t *requestId, char *data, size_t capacity) {
    if (!connected() || replyCount == 0 || requestId == nullptr || data == nullptr) return 0;
    const GrinderBleReply &reply = replies[replyStart];
    if (capacity < reply.size) return 0;
    *requestId = reply.id;
    memcpy(data, reply.bytes, reply.size);
    replyStart = (replyStart + 1) % grinderBleReplyLimit;
    replyCount = replyCount - 1;
    return reply.size;
  }
};

static inline bool grinderBleAdvertisedMac(const uint8_t *data, size_t size, char (&mac)[18]) {
  if (data == nullptr || size != 8 || data[0] != 0xff || data[1] != 0xff) return false;
  constexpr char digits[] = "0123456789ABCDEF";
  for (size_t i = 0; i < 6; ++i) {
    mac[i * 3] = digits[data[i + 2] >> 4];
    mac[i * 3 + 1] = digits[data[i + 2] & 0xf];
    mac[i * 3 + 2] = i == 5 ? '\0' : ':';
  }
  return true;
}

#endif
