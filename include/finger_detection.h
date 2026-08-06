#ifndef FINGERDETECTION_H
#define FINGERDETECTION_H
#include <Arduino.h>
#include "declare.h"
#include "config.h"
#include "parameter.h"
#include "ble.h"
#include "usbcomm.h"
void sendWebsocketButton(int buttonNumber, int buttonShortPress);
// ============================================
#define TOTAL_SAMPLES 50           // Total samples per button
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

enum SamplingPhase {
  PHASE_IDLE = 0,
  PHASE_PRESSING,
  PHASE_RECOVERING
};

struct PressSample {
  float weight;
  unsigned long timestamp;
  SamplingPhase phase;
  bool is_release_point;
};

struct ButtonPressData {
  PressSample samples[TOTAL_SAMPLES];
  int sample_index;
  SamplingPhase current_phase;
  unsigned long press_start_time;
  unsigned long release_time;
  float release_weight;
  float last_sampled_weight;
  unsigned long last_sample_real_time;
  bool is_active;
};

ButtonPressData circle_press_data = {0};
ButtonPressData square_press_data = {0};

ButtonPressData* getButtonPressData(int button);
bool isFingerPress(int button);
bool isQuickTap(int button);
void startPressSampling(int button);
void onButtonReleased(int button);
void analyzeCompletePressData(int button);
void updatePressSampling();
void scaleTimer();

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

  if (data->sample_index < 5) {
    return isQuickTap(button);
  }

  int release_index = -1;
  float start_weight = data->samples[0].weight;
  float peak_weight = start_weight;
  int peak_index = 0;

  for (int i = 0; i < data->sample_index; i++) {
    if (data->samples[i].is_release_point) {
      release_index = i;
    }
    if (data->samples[i].weight > peak_weight) {
      peak_weight = data->samples[i].weight;
      peak_index = i;
    }
  }

  if (release_index < 0) release_index = data->sample_index - 1;

  float final_weight = data->samples[data->sample_index-1].weight;

  unsigned long press_duration = data->samples[release_index].timestamp;
  unsigned long total_duration = data->samples[data->sample_index-1].timestamp;

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
                        reasonable_total_time;

  if (b_fingerDetectionSerialOutput) {
    Serial.print("\n🔍 ");
    Serial.print(button == BUTTON_CIRCLE ? "Circle" : "Square");
    Serial.println(" Button Finger Press Analysis:");
    Serial.print("Samples: ");
    Serial.println(data->sample_index);
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

    const bool bleClientLive = bleHasLiveClient();
    if (!bleClientLive || b_btnFuncWhileConnected) {
      if (button == BUTTON_CIRCLE) {
        sendUsbButton(1, 1);
        sendWebsocketButton(1, 1);
        if (bleClientLive) {
          sendBleButton(1, 1);
        }
        if (!bleClientLive || b_btnFuncWhileConnected) {
          b_weight_quick_zero = true;
          t_quickZeroStart = millis();
          t_tareByButton = millis();
          b_tareByButton = true;
        }
      } else if (button == BUTTON_SQUARE) {
        sendUsbButton(2, 1);
        sendWebsocketButton(2, 1);
        if (bleClientLive) {
          sendBleButton(2, 1);
        }
        if (!b_menu && !b_calibration && (!bleClientLive || b_btnFuncWhileConnected)) {
          if (millis() - t_menuExitTime > 1000)
            scaleTimer();
        }
      }
    }
  } else {
    if (b_fingerDetectionSerialOutput) Serial.println("❌ NOT FINGER PRESS");
  }

  return is_finger_press;
}

bool isQuickTap(int button) {
  ButtonPressData* data = getButtonPressData(button);
  if (!data || data->sample_index < 2) return false;

  float start_weight = data->samples[0].weight;
  float final_weight = data->samples[data->sample_index-1].weight;
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
  if (!data || data->sample_index <= 3) {
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

  int release_index = -1;
  float start_weight = data->samples[0].weight;
  float peak_weight = start_weight;
  int peak_index = 0;

  for (int i = 0; i < data->sample_index; i++) {
    if (data->samples[i].is_release_point) {
      release_index = i;
    }
    if (data->samples[i].weight > peak_weight) {
      peak_weight = data->samples[i].weight;
      peak_index = i;
    }
  }

  if (release_index < 0) release_index = data->sample_index - 1;

  float final_weight = data->samples[data->sample_index-1].weight;

  float avg_interval = 0;
  if (data->sample_index > 1) {
    avg_interval = data->samples[data->sample_index-1].timestamp / (float)(data->sample_index-1);
  }

  if (b_fingerDetectionSerialOutput) {
    Serial.print("Samples: ");
    Serial.print(data->sample_index);
    Serial.print(" | Avg interval: ");
    Serial.print(avg_interval, 1);
    Serial.println("ms");

    Serial.println("\n--- KEY METRICS ---");
  }

  float press_duration = data->samples[release_index].timestamp;
  float total_duration = data->samples[data->sample_index-1].timestamp;
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

  if (data->is_active) {
    if (b_fingerDetectionSerialOutput) {
      Serial.print(button == BUTTON_CIRCLE ? "Circle" : "Square");
      Serial.println(" button already sampling, ignore new press");
    }
    return;
  }

  data->current_phase = PHASE_PRESSING;
  data->sample_index = 0;
  data->press_start_time = millis();
  data->last_sample_real_time = data->press_start_time;
  data->last_sampled_weight = f_current_raw_value;
  data->is_active = true;

  if (data->sample_index < TOTAL_SAMPLES) {
    data->samples[data->sample_index].weight = f_current_raw_value;
    data->samples[data->sample_index].timestamp = 0;
    data->samples[data->sample_index].phase = PHASE_PRESSING;
    data->samples[data->sample_index].is_release_point = false;
    data->sample_index++;
  }
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
  if (!data || data->current_phase != PHASE_PRESSING) return;

  data->release_time = millis();
  data->release_weight = f_current_raw_value;
  data->current_phase = PHASE_RECOVERING;

  if (data->sample_index < TOTAL_SAMPLES) {
    data->samples[data->sample_index].weight = data->release_weight;
    data->samples[data->sample_index].timestamp = data->release_time - data->press_start_time;
    data->samples[data->sample_index].phase = PHASE_RECOVERING;
    data->samples[data->sample_index].is_release_point = true;
    data->sample_index++;
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

    if (!data || !data->is_active) continue;

    if (data->current_phase == PHASE_IDLE) {
      data->is_active = false;
      continue;
    }

    unsigned long current_time = millis();

    if (current_time - data->last_sample_real_time < 10) continue;

    if (data->current_phase == PHASE_PRESSING) {
      if (current_time - data->press_start_time > 2000) {
        if (b_fingerDetectionSerialOutput) {
          Serial.print(button == BUTTON_CIRCLE ? "Circle" : "Square");
          Serial.println(" button press phase timeout");
        }
        data->current_phase = PHASE_IDLE;
        data->is_active = false;
        continue;
      }
    } else if (data->current_phase == PHASE_RECOVERING) {
      unsigned long post_release_duration = (button == BUTTON_CIRCLE) ?
                                           CIRCLE_POST_RELEASE_DURATION :
                                           SQUARE_POST_RELEASE_DURATION;
      if (current_time - data->release_time > post_release_duration) {
        data->current_phase = PHASE_IDLE;
        data->is_active = false;
        analyzeCompletePressData(button);
        continue;
      }
    }

    if (data->sample_index < TOTAL_SAMPLES) {
      float current_weight = f_current_raw_value;

      data->samples[data->sample_index].weight = current_weight;
      data->samples[data->sample_index].timestamp = current_time - data->press_start_time;
      data->samples[data->sample_index].phase = data->current_phase;
      data->samples[data->sample_index].is_release_point = false;

      data->sample_index++;
      data->last_sample_real_time = current_time;
      data->last_sampled_weight = current_weight;
    } else {
      if (b_fingerDetectionSerialOutput) {
        Serial.print(button == BUTTON_CIRCLE ? "Circle" : "Square");
        Serial.println(" button sampling buffer full");
      }
      data->current_phase = PHASE_IDLE;
      data->is_active = false;
    }
  }
}

#endif
