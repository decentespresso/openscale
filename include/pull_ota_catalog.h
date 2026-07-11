#ifndef PULL_OTA_CATALOG_H
#define PULL_OTA_CATALOG_H

#include <stdint.h>

template <typename Release, typename SameRelease, typename CompareVersions>
bool pullOtaCatalogAdd(
    Release *releases,
    uint8_t &count,
    uint8_t capacity,
    const Release &candidate,
    SameRelease sameRelease,
    CompareVersions compareVersions) {
  for (uint8_t index = 0; index < count; index++) {
    if (sameRelease(releases[index], candidate)) {
      return false;
    }
  }
  uint8_t insertAt = 0;
  while (insertAt < count && compareVersions(releases[insertAt], candidate) > 0) {
    insertAt++;
  }
  if (count == capacity && insertAt >= capacity) {
    return false;
  }
  if (count < capacity) {
    count++;
  }
  for (int index = static_cast<int>(count) - 1; index > static_cast<int>(insertAt); index--) {
    releases[index] = releases[index - 1];
  }
  releases[insertAt] = candidate;
  return true;
}

#endif
