#include <cassert>
#include <cstring>
#include <limits>
#include "app_prompt_state.h"

int main() {
  const uint8_t show[] = {1, 'A', 'd', 'd', ' ', 'G', 'i', 'n', ':', ' ', '3', '5', 'g'};
  const uint8_t clear[] = {0};
  const uint8_t heartbeat[] = {3, 10, 3, 255, 255, 0, 10};
  const AppPromptState disconnected{};
  assert(!appPromptCommand(disconnected, show, sizeof(show), 0).active);
  const auto connection = appPromptConnection(7, 100);
  const auto shown = appPromptCommand(connection, show, sizeof(show), 100);
  assert(shown.active && std::strcmp(shown.text, "Add Gin: 35g") == 0);
  assert(!connection.active);
  assert(appPromptExpire(shown, 10099).active);
  assert(!appPromptExpire(shown, 10100).active);
  const auto renewed = appPromptHeartbeat(shown, 4100);
  assert(appPromptExpire(renewed, 14099).active);
  assert(!appPromptHeartbeat(shown, 10100).active);
  assert(!appPromptCommand(shown, show, sizeof(show), 10100).active);
  assert(!appPromptCommand(shown, clear, sizeof(clear), 200).active);
  const auto cancelled = appPromptCancel(shown);
  assert(cancelled.cancelled && !cancelled.active);
  assert(!appPromptCommand(cancelled, show, sizeof(show), 200).active);
  assert(!appPromptCommand(appPromptHeartbeat(cancelled, 200), show, sizeof(show), 200).active);
  assert(appPromptCommand(appPromptConnection(7, 200), show, sizeof(show), 200).active);
  assert(!appPromptConnection(APP_PROMPT_NO_CONNECTION, 200).active);

  uint8_t maximum[19];
  maximum[0] = 1;
  std::memset(maximum + 1, '~', 18);
  const auto full = appPromptCommand(connection, maximum, sizeof(maximum), 100);
  assert(full.active && std::strlen(full.text) == 18 && full.text[18] == '\0');
  const uint8_t invalid[][3] = {{1, 0, 0}, {1, 31, 0}, {1, 127, 0}, {1, 255, 0}, {2, 'A', 0}, {0, 'A', 0}};
  for (const auto &frame : invalid) {
    const auto unchanged = appPromptCommand(shown, frame, 2, 100);
    assert(std::strcmp(unchanged.text, shown.text) == 0 && unchanged.active);
  }
  assert(!appPromptCommand(connection, nullptr, 1, 100).active);
  assert(!appPromptCommand(connection, show, 0, 100).active);
  assert(!appPromptCommand(connection, show, 1, 100).active);
  uint8_t oversized[20] = {1};
  std::memset(oversized + 1, 'A', 19);
  assert(!appPromptCommand(connection, oversized, sizeof(oversized), 100).active);
  for (int value = 0; value <= 255; ++value) {
    const uint8_t frame[] = {1, static_cast<uint8_t>(value)};
    assert(appPromptCommand(connection, frame, sizeof(frame), 100).active == (value >= 32 && value <= 126));
  }
  assert(appPromptIsHeartbeat(heartbeat, sizeof(heartbeat)));
  assert(!appPromptIsHeartbeat(heartbeat, 6));
  assert(!appPromptIsHeartbeat(nullptr, 7));
  const uint8_t badHeartbeat[] = {3, 10, 3, 255, 255, 0, 11};
  assert(!appPromptIsHeartbeat(badHeartbeat, sizeof(badHeartbeat)));
  const auto nearWrap = appPromptCommand(appPromptConnection(9, std::numeric_limits<unsigned long>::max() - 5000),
                                        show, sizeof(show), std::numeric_limits<unsigned long>::max() - 5000);
  assert(appPromptExpire(nearWrap, 4998).active);
  assert(!appPromptExpire(nearWrap, 4999).active);
}
