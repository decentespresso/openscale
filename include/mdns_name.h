#ifndef MDNS_NAME_H
#define MDNS_NAME_H

#include <stddef.h>

constexpr const char *MDNS_NAME_DEFAULT = "hds";
constexpr size_t MDNS_NAME_MAX_CHARS = 24;
constexpr size_t MDNS_NAME_BUFFER_BYTES = MDNS_NAME_MAX_CHARS + 1;

inline const char *mdnsNameDefault() { return MDNS_NAME_DEFAULT; }

inline bool mdnsNameIsWhitespace(char value) {
  return value == ' ' || value == '\t' || value == '\r' || value == '\n' ||
         value == '\f' || value == '\v';
}

inline char mdnsNameLower(char value) {
  return value >= 'A' && value <= 'Z' ? (char)(value - 'A' + 'a') : value;
}

inline bool mdnsNameIsLabelChar(char value) {
  return (value >= 'a' && value <= 'z') || (value >= '0' && value <= '9') ||
         value == '-';
}

inline bool mdnsNameCopyDefault(char *out, size_t outSize) {
  if (out == nullptr || outSize < 4) {
    return false;
  }
  out[0] = 'h';
  out[1] = 'd';
  out[2] = 's';
  out[3] = 0;
  return true;
}

inline bool mdnsNameNormalize(const char *input, char *out, size_t outSize) {
  if (out == nullptr || outSize == 0) {
    return false;
  }
  out[0] = 0;
  if (input == nullptr) {
    return mdnsNameCopyDefault(out, outSize);
  }

  const char *start = input;
  while (*start != 0 && mdnsNameIsWhitespace(*start)) {
    start++;
  }
  const char *end = start;
  while (*end != 0) {
    end++;
  }
  while (end > start && mdnsNameIsWhitespace(*(end - 1))) {
    end--;
  }

  size_t length = (size_t)(end - start);
  if (length == 0) {
    return mdnsNameCopyDefault(out, outSize);
  }
  if (length > MDNS_NAME_MAX_CHARS || length + 1 > outSize) {
    return false;
  }

  for (size_t index = 0; index < length; index++) {
    char value = mdnsNameLower(start[index]);
    if (!mdnsNameIsLabelChar(value)) {
      out[0] = 0;
      return false;
    }
    out[index] = value;
  }
  out[length] = 0;

  if (out[0] == '-' || out[length - 1] == '-') {
    out[0] = 0;
    return false;
  }
  return true;
}

inline bool mdnsNameIsDefault(const char *name) {
  if (name == nullptr) {
    return true;
  }
  const char *expected = MDNS_NAME_DEFAULT;
  while (*expected != 0) {
    if (*name != *expected) {
      return false;
    }
    name++;
    expected++;
  }
  return *name == 0;
}

#endif
