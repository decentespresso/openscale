#ifndef MDNS_NAME_H
#define MDNS_NAME_H

#include <stddef.h>

constexpr const char *MDNS_NAME_DEFAULT = "hds";
constexpr size_t MDNS_NAME_MAX_CHARS = 24;
constexpr size_t MDNS_NAME_BUFFER_BYTES = MDNS_NAME_MAX_CHARS + 1;

constexpr size_t MDNS_NAME_OLED_LINE1_CHARS = 14;
constexpr size_t MDNS_NAME_OLED_LINE2_CHARS = 21;
constexpr size_t MDNS_NAME_OLED_LINE1_BYTES = MDNS_NAME_OLED_LINE1_CHARS + 1;

static_assert(MDNS_NAME_OLED_LINE1_CHARS + MDNS_NAME_OLED_LINE2_CHARS >=
                MDNS_NAME_MAX_CHARS,
              "OLED name lines must hold the longest accepted name");

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

inline const char *mdnsNameSplitOledLine1(const char *name, char *line1,
                                          size_t line1Size) {
  if (line1 == nullptr || line1Size == 0) {
    return "";
  }
  line1[0] = 0;
  if (name == nullptr) {
    return "";
  }

  size_t limit = line1Size - 1;
  if (limit > MDNS_NAME_OLED_LINE1_CHARS) {
    limit = MDNS_NAME_OLED_LINE1_CHARS;
  }

  size_t taken = 0;
  while (taken < limit && name[taken] != 0) {
    line1[taken] = name[taken];
    taken++;
  }
  line1[taken] = 0;
  return name + taken;
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
