#ifndef FINGERDETECTION_H
#define FINGERDETECTION_H
#include <Arduino.h>
#include "declare.h"
#include "config.h"
#include "parameter.h"
#include "ble.h"
#include "usbcomm.h"
#if HDS_FEATURE_WEBSOCKET
void sendWebsocketButton(int buttonNumber, int buttonShortPress);
#endif
#define TOTAL_SAMPLES 50           // Total samples per button
static_assert(TOTAL_SAMPLES < UINT8_MAX);
bool b_fingerDetectionSerialOutput = false;

#define CIRCLE_POST_RELEASE_DURATION 500      // Circle button post-release sampling duration(ms)
#define CIRCLE_FINGER_PRESS_MIN_PEAK 3.0      // Circle button minimum peak change(g)
#define CIRCLE_FINGER_PRESS_MAX_NET 2.0       // Circle button maximum net change(g)
#define CIRCLE_FINGER_PRESS_MIN_RECOVERY 0.85 // Circle button minimum recovery ratio
#define CIRCLE_FINGER_PRESS_MAX_PRESS_TIME 800 // Circle button maximum press time(ms)
#define CIRCLE_FINGER_PRESS_MIN_TOTAL_TIME 300 // Circle button minimum total time(ms)

#define SQUARE_POST_RELEASE_DURATION 500      // Square button post-release sampling duration(ms)
#define SQUARE_FINGER_PRESS_MIN_PEAK 3.0      // Square button minimum peak change(g)
#define SQUARE_FINGER_PRESS_MAX_NET 2.0       // Square button maximum net change(g)
#define SQUARE_FINGER_PRESS_MIN_RECOVERY 0.85 // Square button minimum recovery ratio
#define SQUARE_FINGER_PRESS_MAX_PRESS_TIME 800 // Square button maximum press time(ms)
#define SQUARE_FINGER_PRESS_MIN_TOTAL_TIME 300 // Square button minimum total time(ms)

enum class SamplingPhase : uint8_t {
  Idle,
  Pressing,
  Recovering
};

struct ButtonPressData {
  float startWeight;
  float lastWeight;
  float peakWeight;
  uint32_t pressStartTime;
  uint32_t releaseTime;
  uint32_t lastSampleTime;
  uint8_t sampleCount;
  uint8_t releaseIndex;
  uint8_t peakIndex;
  SamplingPhase phase;
  bool active;
};

static_assert(sizeof(ButtonPressData) <= 36);

ButtonPressData circle_press_data = {0};
ButtonPressData square_press_data = {0};

static inline void addPressSample(ButtonPressData& data, float weight) {
  if (data.sampleCount >= TOTAL_SAMPLES) return;
  if (data.sampleCount == 0) {
    data.startWeight = weight;
    data.peakWeight = weight;
    data.peakIndex = 0;
  } else if (weight > data.peakWeight) {
    data.peakWeight = weight;
    data.peakIndex = data.sampleCount;
  }
  data.lastWeight = weight;
  ++data.sampleCount;
}

ButtonPressData* getButtonPressData(int button);
bool isFingerPress(int button);
bool isQuickTap(int button);
void startPressSampling(int button);
void onButtonReleased(int button);
void analyzeCompletePressData(int button);
void updatePressSampling();
void scaleTimer();
void runRecognizedButtonAction(int button);

void runRecognizedButtonAction(int button) {
  const bool bleClientLive = bleHasLiveClient();
  if (bleClientLive && !b_btnFuncWhileConnected) return;

  const int buttonNumber = button == BUTTON_CIRCLE ? 1 : 2;
  sendUsbButton(buttonNumber, 1);
#if HDS_FEATURE_WEBSOCKET
  sendWebsocketButton(buttonNumber, 1);
#endif
  if (bleClientLive) sendBleButton(buttonNumber, 1);

  if (button == BUTTON_CIRCLE) {
    b_weight_quick_zero = true;
    t_quickZeroStart = millis();
    t_tareByButton = millis();
    b_tareByButton = true;
  } else if (button == BUTTON_SQUARE && !b_menu && !b_calibration &&
             millis() - t_menuExitTime > 1000) {
    scaleTimer();
  }
}

ButtonPressData* getButtonPressData(int button) {
  if (button == BUTTON_CIRCLE) {
    return &circle_press_data;
  } else if (button == BUTTON_SQUARE) {
    return &square_press_data;
  }
  return nullptr;
}

void getButtonPressConfig(int button,
                         float* min_peak, float* max_net,
                         float* min_recovery, unsigned long* max_press_time,
                         unsigned long* min_total_time) {
  if (button == BUTTON_CIRCLE) {
    *min_peak = CIRCLE_FINGER_PRESS_MIN_PEAK;
    *max_net = CIRCLE_FINGER_PRESS_MAX_NET;
    *min_recovery = CIRCLE_FINGER_PRESS_MIN_RECOVERY;
    *max_press_time = CIRCLE_FINGER_PRESS_MAX_PRESS_TIME;
    *min_total_time = CIRCLE_FINGER_PRESS_MIN_TOTAL_TIME;
  } else if (button == BUTTON_SQUARE) {
    *min_peak = SQUARE_FINGER_PRESS_MIN_PEAK;
    *max_net = SQUARE_FINGER_PRESS_MAX_NET;
    *min_recovery = SQUARE_FINGER_PRESS_MIN_RECOVERY;
    *max_press_time = SQUARE_FINGER_PRESS_MAX_PRESS_TIME;
    *min_total_time = SQUARE_FINGER_PRESS_MIN_TOTAL_TIME;
  }
}

bool isFingerPress(int button) {
  ButtonPressData* data = getButtonPressData(button);
  if (!data) return false;

  if (data->sampleCount < 5) {
    return isQuickTap(button);
  }

  int release_index = data->releaseIndex;
  float start_weight = data->startWeight;
  float peak_weight = data->peakWeight;
  int peak_index = data->peakIndex;

  if (release_index >= data->sampleCount) release_index = data->sampleCount - 1;

  float final_weight = data->lastWeight;

  const unsigned long press_sample_time = data->releaseIndex < data->sampleCount ? data->releaseTime : data->lastSampleTime;
  const unsigned long final_sample_time = data->releaseIndex == data->sampleCount - 1 ? data->releaseTime : data->lastSampleTime;
  unsigned long press_duration = press_sample_time - data->pressStartTime;
  unsigned long total_duration = final_sample_time - data->pressStartTime;

  float peak_change = peak_weight - start_weight;
  float net_change = final_weight - start_weight;

  float min_peak, max_net, min_recovery;
  unsigned long max_press_time, min_total_time;
  getButtonPressConfig(button, &min_peak, &max_net, &min_recovery, &max_press_time, &min_total_time);

  bool has_significant_peak = (peak_change >= min_peak);
  bool has_good_recovery = (fabs(net_change) <= max_net);
  bool reasonable_press_time = (press_duration <= max_press_time);
  bool reasonable_total_time = (total_duration >= min_total_time);

  float recovery_ratio = 0;
  if (peak_change > 0.1) {
    float recovery_amount = peak_weight - final_weight;
    recovery_ratio = recovery_amount / peak_change;
  }
  bool has_good_recovery_ratio = (recovery_ratio >= min_recovery);

  bool peak_after_release = (peak_index > release_index);

  bool is_finger_press = has_significant_peak &&
                        has_good_recovery &&
                        has_good_recovery_ratio &&
                        reasonable_press_time &&
                        reasonable_total_time;

  if (b_fingerDetectionSerialOutput) {
    Serial.print("\n🔍 ");
    Serial.print(button == BUTTON_CIRCLE ? "Circle" : "Square");
    Serial.println(" Button Finger Press Analysis:");
    Serial.print("Samples: ");
    Serial.println(data->sampleCount);
    Serial.print("Peak change: ");
    Serial.print(peak_change, 2);
    Serial.print("g (min=");
    Serial.print(min_peak, 1);
    Serial.print("g) ");
    Serial.println(has_significant_peak ? "✅" : "❌");

    Serial.print("Net change: ");
    Serial.print(net_change, 2);
    Serial.print("g (max=");
    Serial.print(max_net, 1);
    Serial.print("g) ");
    Serial.println(has_good_recovery ? "✅" : "❌");

    Serial.print("Recovery ratio: ");
    Serial.print(recovery_ratio * 100, 1);
    Serial.print("% (min=");
    Serial.print(min_recovery * 100, 0);
    Serial.print("%) ");
    Serial.println(has_good_recovery_ratio ? "✅" : "❌");

    Serial.print("Total time: ");
    Serial.print(total_duration);
    Serial.print("ms (min=");
    Serial.print(min_total_time);
    Serial.print("ms) ");
    Serial.println(reasonable_total_time ? "✅" : "❌");

    Serial.print("Peak position: ");
    Serial.print(peak_after_release ? "After release" : "Before release");
    Serial.print(" (sample ");
    Serial.print(peak_index);
    Serial.print(")");

    Serial.print("\n🎯 Result: ");
  }
  if (is_finger_press) {
    if (b_fingerDetectionSerialOutput) Serial.println("✅ FINGER PRESS");
    runRecognizedButtonAction(button);
  } else {
    if (b_fingerDetectionSerialOutput) Serial.println("❌ NOT FINGER PRESS");
  }

  return is_finger_press;
}

bool isQuickTap(int button) {
  ButtonPressData* data = getButtonPressData(button);
  if (!data || data->sampleCount < 2) return false;

  float start_weight = data->startWeight;
  float final_weight = data->lastWeight;
  float net_change = final_weight - start_weight;

  bool is_quick = (fabs(net_change) < 1.5);
  if (b_fingerDetectionSerialOutput) {
    Serial.print("Quick tap detection (");
    Serial.print(button == BUTTON_CIRCLE ? "Circle" : "Square");
    Serial.print("): Net change ");
    Serial.print(net_change, 2);
    Serial.print("g -> ");
    Serial.println(is_quick ? "✅ Likely quick tap" : "❌ Not quick tap");
  }

  return is_quick;
}

void analyzeCompletePressData(int button) {
  ButtonPressData* data = getButtonPressData(button);
  if (!data || data->sampleCount <= 3) {
    if (b_fingerDetectionSerialOutput) Serial.println("Samples <= 3, likely hand press");
    return;
  }
  if (b_fingerDetectionSerialOutput) {
    Serial.println("\n" + String(70, '='));
    Serial.print("📊 ");
    Serial.print(button == BUTTON_CIRCLE ? "CIRCLE" : "SQUARE");
    Serial.println(" BUTTON PRESS ANALYSIS");
    Serial.println(String(70, '='));
  }

  int release_index = data->releaseIndex;
  float start_weight = data->startWeight;
  float peak_weight = data->peakWeight;
  int peak_index = data->peakIndex;

  if (release_index >= data->sampleCount) release_index = data->sampleCount - 1;

  float final_weight = data->lastWeight;

  const unsigned long press_sample_time = data->releaseIndex < data->sampleCount ? data->releaseTime : data->lastSampleTime;
  const unsigned long final_sample_time = data->releaseIndex == data->sampleCount - 1 ? data->releaseTime : data->lastSampleTime;
  const unsigned long total_duration_ms = final_sample_time - data->pressStartTime;
  float avg_interval = 0;
  if (data->sampleCount > 1) {
    avg_interval = total_duration_ms / (float)(data->sampleCount-1);
  }

  if (b_fingerDetectionSerialOutput) {
    Serial.print("Samples: ");
    Serial.print(data->sampleCount);
    Serial.print(" | Avg interval: ");
    Serial.print(avg_interval, 1);
    Serial.println("ms");

    Serial.println("\n--- KEY METRICS ---");
  }

  float press_duration = press_sample_time - data->pressStartTime;
  float total_duration = total_duration_ms;
  float recovery_duration = total_duration - press_duration;

  float press_increase = peak_weight - start_weight;
  float recovery_decrease = peak_weight - final_weight;
  float net_change = final_weight - start_weight;

  if (b_fingerDetectionSerialOutput) {
    Serial.print("Press duration: ");
    Serial.print(press_duration);
    Serial.print("ms | Total duration: ");
    Serial.print(total_duration);
    Serial.println("ms");

    Serial.print("Peak increase: +");
    Serial.print(press_increase, 2);
    Serial.print("g | Recovery decrease: ");
    Serial.print(recovery_decrease, 2);
    Serial.print("g | Net change: ");
    if (net_change >= 0) Serial.print("+");
    Serial.print(net_change, 2);
    Serial.println("g");
  }
  if (press_increase > 0.1) {
    float recovery_ratio = recovery_decrease / press_increase;
    float recovery_rate = 0;
    if (recovery_duration > 0) {
      recovery_rate = recovery_decrease / (recovery_duration / 1000.0);
    }
    if (b_fingerDetectionSerialOutput) {
      Serial.print("Recovery ratio: ");
      Serial.print(recovery_ratio * 100, 1);
      Serial.print("% | Recovery rate: ");
      Serial.print(recovery_rate, 1);
      Serial.println("g/s");
    }
  }

  if (b_fingerDetectionSerialOutput) Serial.println("\n--- FINGER PRESS RECOGNITION ---");
  bool is_finger = isFingerPress(button);

  if (b_fingerDetectionSerialOutput) Serial.println(String(70, '=') + "\n");
}

void startPressSampling(int button) {
  ButtonPressData* data = getButtonPressData(button);
  if (!data) return;

  if (data->active) {
    if (b_fingerDetectionSerialOutput) {
      Serial.print(button == BUTTON_CIRCLE ? "Circle" : "Square");
      Serial.println(" button already sampling, ignore new press");
    }
    return;
  }

  data->phase = SamplingPhase::Pressing;
  data->sampleCount = 0;
  data->pressStartTime = millis();
  data->lastSampleTime = data->pressStartTime;
  data->releaseIndex = TOTAL_SAMPLES;
  data->active = true;

  addPressSample(*data, f_current_raw_value);
  if (b_fingerDetectionSerialOutput) {
    Serial.print("=== ");
    Serial.print(button == BUTTON_CIRCLE ? "Circle" : "Square");
    Serial.println(" Button Weight Sampling Started ===");
    Serial.print("Start weight: ");
    Serial.println(f_current_raw_value, 3);
  }
}

void onButtonReleased(int button) {
  ButtonPressData* data = getButtonPressData(button);
  if (!data || data->phase != SamplingPhase::Pressing) return;

  data->releaseTime = millis();
  data->phase = SamplingPhase::Recovering;

  if (data->sampleCount < TOTAL_SAMPLES) {
    data->releaseIndex = data->sampleCount;
    addPressSample(*data, f_current_raw_value);
  }
  if (b_fingerDetectionSerialOutput) {
    Serial.print("=== ");
    Serial.print(button == BUTTON_CIRCLE ? "Circle" : "Square");
    Serial.println(" Button Released, starting recovery sampling ===");
  }
}

void updatePressSampling() {
  for (int i = BUTTON_CIRCLE; i <= BUTTON_SQUARE; i++) {
    int button = static_cast<int>(i);
    ButtonPressData* data = getButtonPressData(button);

    if (!data || !data->active) continue;

    if (data->phase == SamplingPhase::Idle) {
      data->active = false;
      continue;
    }

    unsigned long current_time = millis();

    if (current_time - data->lastSampleTime < 10) continue;

    if (data->phase == SamplingPhase::Pressing) {
      if (current_time - data->pressStartTime > 2000) {
        if (b_fingerDetectionSerialOutput) {
          Serial.print(button == BUTTON_CIRCLE ? "Circle" : "Square");
          Serial.println(" button press phase timeout");
        }
        data->phase = SamplingPhase::Idle;
        data->active = false;
        continue;
      }
    } else if (data->phase == SamplingPhase::Recovering) {
      unsigned long post_release_duration = (button == BUTTON_CIRCLE) ?
                                           CIRCLE_POST_RELEASE_DURATION :
                                           SQUARE_POST_RELEASE_DURATION;
      if (current_time - data->releaseTime > post_release_duration) {
        data->phase = SamplingPhase::Idle;
        data->active = false;
        analyzeCompletePressData(button);
        continue;
      }
    }

    if (data->sampleCount < TOTAL_SAMPLES) {
      float current_weight = f_current_raw_value;

      addPressSample(*data, current_weight);
      data->lastSampleTime = current_time;
    } else {
      if (b_fingerDetectionSerialOutput) {
        Serial.print(button == BUTTON_CIRCLE ? "Circle" : "Square");
        Serial.println(" button sampling buffer full");
      }
      data->phase = SamplingPhase::Idle;
      data->active = false;
    }
  }
}

#endif
