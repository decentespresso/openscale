#ifndef WIFI_CREDENTIALS_H
#define WIFI_CREDENTIALS_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

struct WifiCredentials {
  uint8_t version = 1;
  char ssid[33] = {};
  char pass[65] = {};
};

static_assert(sizeof(WifiCredentials) == 99, "WiFi credential record size");

inline bool wifiReadCodePoint(const char *text, size_t length, size_t &offset,
                              uint32_t &value) {
  const uint8_t first = static_cast<uint8_t>(text[offset++]);
  if (first < 0x80) {
    value = first;
    return true;
  }
  const unsigned count = first >= 0xc2 && first <= 0xdf ? 1 :
                         first >= 0xe0 && first <= 0xef ? 2 :
                         first >= 0xf0 && first <= 0xf4 ? 3 : 0;
  if (count == 0 || offset + count > length) return false;
  value = first & (0x7f >> (count + 1));
  for (unsigned index = 0; index < count; ++index) {
    const uint8_t next = static_cast<uint8_t>(text[offset++]);
    if ((next & 0xc0) != 0x80) return false;
    value = (value << 6) | (next & 0x3f);
  }
  const uint32_t minimum = count == 1 ? 0x80 : count == 2 ? 0x800 : 0x10000;
  return value >= minimum && value <= 0x10ffff &&
         (value < 0xd800 || value > 0xdfff);
}

inline bool wifiEdgeCharacter(uint32_t value) {
  constexpr uint32_t ranges[][2] = {
      {0x0009, 0x000d}, {0x0020, 0x0020}, {0x0085, 0x0085},
      {0x00a0, 0x00a0}, {0x00ad, 0x00ad}, {0x034f, 0x034f},
      {0x061c, 0x061c}, {0x115f, 0x1160}, {0x1680, 0x1680},
      {0x17b4, 0x17b5}, {0x180b, 0x180f}, {0x2000, 0x200f},
      {0x2028, 0x202f}, {0x205f, 0x206f}, {0x3000, 0x3000},
      {0x3164, 0x3164}, {0xfe00, 0xfe0f}, {0xfeff, 0xfeff},
      {0xffa0, 0xffa0}, {0xfff0, 0xfff8}, {0x1bca0, 0x1bca3},
      {0x1d173, 0x1d17a}, {0xe0000, 0xe0fff}};
  for (const auto &range : ranges) {
    if (value >= range[0] && value <= range[1]) return true;
  }
  return false;
}

inline bool wifiNormalizeField(const char *text, size_t length, char *output,
                                size_t capacity) {
  if (text == nullptr || output == nullptr || capacity == 0) return false;
  size_t start = length;
  size_t end = 0;
  size_t offset = 0;
  while (offset < length) {
    const size_t before = offset;
    uint32_t value;
    if (!wifiReadCodePoint(text, length, offset, value) || value == 0) return false;
    if (!wifiEdgeCharacter(value)) {
      if (start == length) start = before;
      end = offset;
    }
  }
  const size_t bytes = end > start ? end - start : 0;
  if (bytes >= capacity) return false;
  offset = start;
  while (offset < end) {
    uint32_t value;
    if (!wifiReadCodePoint(text, end, offset, value) || value < 0x20 ||
        (value >= 0x7f && value <= 0x9f)) return false;
  }
  memcpy(output, text + start, bytes);
  output[bytes] = 0;
  return true;
}

inline bool wifiPasswordValid(const char *pass) {
  const size_t bytes = strlen(pass);
  if (bytes == 0 || (bytes >= 8 && bytes <= 63)) return true;
  if (bytes != 64) return false;
  for (size_t index = 0; index < bytes; ++index) {
    const char value = pass[index];
    if (!((value >= '0' && value <= '9') || (value >= 'a' && value <= 'f') ||
          (value >= 'A' && value <= 'F'))) return false;
  }
  return true;
}

inline bool wifiNormalizeCredentials(const char *ssid, size_t ssidLength,
                                     const char *pass, size_t passLength,
                                     WifiCredentials &output) {
  WifiCredentials normalized;
  if (!wifiNormalizeField(ssid, ssidLength, normalized.ssid, sizeof(normalized.ssid)) ||
      normalized.ssid[0] == 0 ||
      !wifiNormalizeField(pass, passLength, normalized.pass, sizeof(normalized.pass)) ||
      !wifiPasswordValid(normalized.pass)) return false;
  output = normalized;
  return true;
}

inline bool wifiCredentialRecordValid(const WifiCredentials &record) {
  return record.version == 1 && memchr(record.ssid, 0, sizeof(record.ssid)) != nullptr &&
         memchr(record.pass, 0, sizeof(record.pass)) != nullptr;
}

#endif
