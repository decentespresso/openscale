#ifndef GRINDER_SHUTDOWN_H
#define GRINDER_SHUTDOWN_H

#include <errno.h>
#include <lwip/sockets.h>

inline void grinderPrepareForShutdown() {
  grinderRuntime.menuPaused = true;
  grinderResetLineReader();
  const char command[] = "OFF\n";
  const unsigned long startedAt = millis();
  size_t sentBytes = 0;
  bool confirmed = false;
  while (grinderRuntime.client.connected() && millis() - startedAt < 250) {
    if (sentBytes < sizeof(command) - 1) {
      const int written = send(grinderRuntime.client.fd(), command + sentBytes,
                               sizeof(command) - 1 - sentBytes, MSG_DONTWAIT);
      if (written > 0) {
        sentBytes += static_cast<size_t>(written);
      } else if (written < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
        break;
      }
    }
    if (grinderRuntime.client.available()) {
      const int value = grinderRuntime.client.read();
      if (value >= 0 && grinderLineRead(static_cast<char>(value))) {
        GrinderTcpResponse response;
        confirmed = sentBytes == sizeof(command) - 1 &&
                    grinderParseResponse(grinderRuntime.rxLine, &response) &&
                    grinderResponseMatchesSelection(response) &&
                    response.kind == GRINDER_TCP_RESPONSE_OK && !response.relayOn;
        if (confirmed) {
          grinderRuntime.pendingCommand = GRINDER_COMMAND_OFF;
          grinderHandleOkResponse(response, f_displayedValue);
          break;
        }
        grinderResetLineReader();
      }
    } else {
      delay(1);
    }
  }
  if (!confirmed && sentBytes > 0) Serial.println("[grinder] shutdown OFF not confirmed");
  grinderRuntimeReset();
}

#endif
