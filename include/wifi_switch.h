#ifndef WIFI_SWITCH_H
#define WIFI_SWITCH_H

#include <stdint.h>

enum class WifiSwitchPhase : uint8_t {
  Idle, Queued, Connecting, Verifying, Saving, Restoring, Succeeded, Failed
};

enum class WifiSetupError : uint8_t {
  None, Authentication, NotFound, Dhcp, Timeout, Storage, Aborted, Scan
};

enum class WifiSwitchAction : uint8_t { None, Connect, Save, Restore, AccessPoint };

struct WifiSwitch {
  WifiSwitchPhase phase = WifiSwitchPhase::Idle;
  WifiSetupError error = WifiSetupError::None;
  unsigned long startedAt = 0;
  uint32_t connectionGeneration = 0;
  bool hasPrevious = false;

  bool busy() const {
    return phase == WifiSwitchPhase::Queued || phase == WifiSwitchPhase::Connecting ||
           phase == WifiSwitchPhase::Verifying || phase == WifiSwitchPhase::Saving ||
           phase == WifiSwitchPhase::Restoring;
  }

  WifiSwitchAction restore(unsigned long now, WifiSetupError reason) {
    error = reason;
    startedAt = now;
    phase = hasPrevious ? WifiSwitchPhase::Restoring : WifiSwitchPhase::Failed;
    return hasPrevious ? WifiSwitchAction::Restore : WifiSwitchAction::AccessPoint;
  }

  WifiSwitchAction saved(unsigned long now, bool success) {
    if (!success) return restore(now, WifiSetupError::Storage);
    phase = WifiSwitchPhase::Succeeded;
    return WifiSwitchAction::None;
  }

  WifiSwitchAction tick(unsigned long now, bool connected,
                        WifiSetupError failure = WifiSetupError::Timeout,
                        uint32_t generation = 0) {
    switch (phase) {
      case WifiSwitchPhase::Queued:
        if (now - startedAt >= 500) {
          phase = WifiSwitchPhase::Connecting;
          startedAt = now;
          return WifiSwitchAction::Connect;
        }
        break;
      case WifiSwitchPhase::Connecting:
        if (now - startedAt >= 20000) return restore(now, failure);
        if (connected) {
          phase = WifiSwitchPhase::Verifying;
          startedAt = now;
          connectionGeneration = generation;
        }
        break;
      case WifiSwitchPhase::Verifying:
        if (!connected || generation != connectionGeneration) return restore(now, failure);
        if (now - startedAt >= 3000) {
          phase = WifiSwitchPhase::Saving;
          return WifiSwitchAction::Save;
        }
        break;
      case WifiSwitchPhase::Restoring:
        if (now - startedAt >= 20000) {
          phase = WifiSwitchPhase::Failed;
          return WifiSwitchAction::AccessPoint;
        }
        if (connected) phase = WifiSwitchPhase::Failed;
        break;
      default:
        break;
    }
    return WifiSwitchAction::None;
  }
};

#endif
