#ifndef PARAMETER_H
#define PARAMETER_H
#include <Arduino.h>
#include <Preferences.h>
#include <math.h>
#include "calibration_validation.h"

Preferences settingsPreferences;

volatile bool b_ble_enabled = false;
volatile uint16_t bleFff4SubscriptionHandle = 0xFFFF;
volatile uint16_t bleStatusResponsesPending = 0;
volatile unsigned long bleStatusRequestAt = 0;
volatile bool bleNotifyFailureLogged = false;
volatile uint32_t bleFff4ConnectionGeneration = 0;
portMUX_TYPE bleFff4Mux = portMUX_INITIALIZER_UNLOCKED;
volatile bool b_usbweight_enabled = false;
unsigned long weightBleNotifyInterval = 100;
volatile unsigned long weightUsbNotifyInterval = 100;
unsigned long weightTextNotifyInterval = 1000;
const unsigned long WEIGHT_BASE_INTERVAL_MS = 100;
const unsigned long WEBSOCKET_2HZ_NOTIFY_INTERVAL_MS = 500;
const unsigned long WEBSOCKET_5HZ_NOTIFY_INTERVAL_MS = 200;
const unsigned long WEBSOCKET_10HZ_NOTIFY_INTERVAL_MS = 100;
const unsigned long WEBSOCKET_DEFAULT_NOTIFY_INTERVAL_MS = WEBSOCKET_2HZ_NOTIFY_INTERVAL_MS;
volatile unsigned long weightWebsocketNotifyInterval = WEBSOCKET_DEFAULT_NOTIFY_INTERVAL_MS;
volatile bool b_websocketEventsEnabled = false;
volatile bool b_websocketLowPowerEnabled = false;

volatile bool g_timerRunning = false;
volatile unsigned long g_timerElapsed = 0;

const uint32_t WSP_DISPLAY_ON  = 1u << 0;
const uint32_t WSP_DISPLAY_OFF = 1u << 1;
const uint32_t WSP_LOWPWR_ON   = 1u << 2;
const uint32_t WSP_LOWPWR_OFF  = 1u << 3;
const uint32_t WSP_SLEEP_ON    = 1u << 4;
const uint32_t WSP_SLEEP_OFF   = 1u << 5;
const uint32_t WSP_POWER_OFF   = 1u << 6;
const uint32_t WSP_TIMER_START = 1u << 7;
const uint32_t WSP_TIMER_STOP  = 1u << 8;
const uint32_t WSP_TIMER_ZERO  = 1u << 9;
const uint32_t WSP_SET_SAMPLES = 1u << 10;
const uint32_t WSP_WIFI_UPDATE = 1u << 11;
const uint32_t WSP_RESET       = 1u << 12;
const uint32_t WSP_BLE_GYRO    = 1u << 13;
portMUX_TYPE wsPendingMux = portMUX_INITIALIZER_UNLOCKED;
volatile uint32_t wsPendingMask = 0;
volatile uint8_t pendingSamplesInUse = 0;
volatile unsigned long pendingResetAt = 0;

const uint8_t OTA_DISPLAY_NONE = 0;
const uint8_t OTA_DISPLAY_PROGRESS = 1;
const uint8_t OTA_DISPLAY_SUCCESS = 2;
const uint8_t OTA_DISPLAY_FAILURE = 3;
portMUX_TYPE otaDisplayMux = portMUX_INITIALIZER_UNLOCKED;
volatile uint8_t otaDisplayState = OTA_DISPLAY_NONE;
volatile uint8_t otaDisplayPercent = 0;

inline void remoteQueueResetAt(unsigned long resetAt) {
  portENTER_CRITICAL(&wsPendingMux);
  pendingResetAt = resetAt;
  wsPendingMask |= WSP_RESET;
  portEXIT_CRITICAL(&wsPendingMux);
}

int i_onWrite_counter = 0;
volatile unsigned long t_heartBeat = 0;
volatile unsigned long t_firstConnect = 0;
volatile bool b_requireHeartBeat = true;
volatile bool b_screenFlipped = false;
volatile bool b_timeOnTop = false;
volatile bool b_btnFuncWhileConnected = false;

int windowLength = 5;
float circularBuffer[5];
int bufferIndex = 0;


const int i_margin_top = 8;
const int i_margin_bottom = 8;

int b_beep = 1;
bool b_about = false;
bool b_debug = false;

unsigned long t_batteryIcon = 0;
bool b_showBatteryIcon = true;
volatile bool b_softSleep = false;
#if defined(ACC_MPU6050) || defined(ACC_BMA400)
bool b_gyroEnabled = true;
#endif

uint64_t GPIO_reason = 0;
bool b_usbLinked = false;
int GPIO_power_on_with = -1;

unsigned long t_power_on_button = 0;
bool b_button_pressed = false;
bool b_buttonChordSuppressUntilRelease = false;
#if HDS_ENABLE_GRINDER
bool b_grinderMenuDirectEntry = false;
#endif


float INPUTCOFFEEPOUROVER = 20.0;
float INPUTCOFFEEESPRESSO = 20.0;
float f_batteryCalibrationFactor = 0.66;
String str_welcome = "welcome";
float f_calibration_value = CALIBRATION_VALUE_DEFAULT;
bool b_calibrationInvalid = false;
char c_calibrationStatus[32] = "ok";
float f_lastCalibrationCandidate = 0.0f;
float f_lastCalibrationVerifiedWeight = 0.0f;
long i_lastCalibrationZeroRaw = 0;
long i_lastCalibrationLoadRaw = 0;
long i_lastCalibrationRawDelta = 0;
long i_lastCalibrationSpread = 0;
float f_up_battery;
unsigned long t_up_battery;

bool b_chargingOLED = true;
bool b_heartBeatIcon = false;
unsigned long t_shutdownFailBle = 0;
bool b_shutdownFailBle = false;
volatile bool b_u8g2Sleep = true;
unsigned long t_bootTare = 0;
bool b_bootTare = false;
int i_bootTareDelay = 1000;
bool b_bootFreshTarePending = false;
uint8_t i_bootFreshTareSamplesInUse = 1;
unsigned long t_bootFreshTare = 0;
const uint8_t BOOT_FRESH_TARE_SAMPLES = 4;
const unsigned long BOOT_FRESH_TARE_TIMEOUT = 3000;
const unsigned long BOOT_FRESH_TARE_INPUT_SETTLE = 1000;
int i_tareDelay = 0;
unsigned long t_tareByButton = 0;
unsigned long t_quickZeroStart = 0;
bool b_tareByButton = false;
unsigned long t_tareByBle = 0;
uint8_t i_remoteTareRequests = 0;
bool b_tareByBle = false;
portMUX_TYPE remoteTareMux = portMUX_INITIALIZER_UNLOCKED;
unsigned long t_tareStatus = 0;
unsigned long t_power_off;
volatile bool b_powerOff = false;
#if defined(ACC_MPU6050) || defined(ACC_BMA400)
unsigned long t_power_off_gyro = 0;
#endif
unsigned long t_button_pressed;
unsigned long t_temp;
float f_temp_tare = 0;

void requestRemoteTare() {
  unsigned long now = millis();
  portENTER_CRITICAL(&remoteTareMux);
  if (i_remoteTareRequests < 255) {
    i_remoteTareRequests++;
  }
  b_tareByBle = true;
  t_tareByBle = now;
  portEXIT_CRITICAL(&remoteTareMux);
}

bool hasRemoteTareRequest() {
  bool hasRequest;
  portENTER_CRITICAL(&remoteTareMux);
  hasRequest = i_remoteTareRequests > 0;
  portEXIT_CRITICAL(&remoteTareMux);
  return hasRequest;
}

uint8_t consumeRemoteTareRequests() {
  uint8_t requests;
  portENTER_CRITICAL(&remoteTareMux);
  requests = i_remoteTareRequests;
  i_remoteTareRequests = 0;
  b_tareByBle = false;
  portEXIT_CRITICAL(&remoteTareMux);
  return requests;
}
int i_icon = 0;
int i_setContainerWeight = 0;
float f_filtered_temperature = 0;
bool b_ads1115InitFail = true;
volatile bool b_wifiOnBoot = false;
volatile bool b_autoSleep = true;
volatile bool b_quickBoot = false;
unsigned int i_buttonBootDelay = 500;
bool b_showChargingUI = false;
#if HDS_ENABLE_GRINDER
struct GrinderSettings;
struct GrinderRuntime;
struct GrinderMdnsCandidate;
extern GrinderSettings grinderSettings;
extern GrinderRuntime grinderRuntime;
portMUX_TYPE grinderMdnsMux = portMUX_INITIALIZER_UNLOCKED;
GrinderMdnsCandidate * volatile grinderMdnsCandidateBuffer = nullptr;
#endif

static float f_tracking_offset = 0.0;
static float f_tracking_target = 0.0;
static unsigned long t_last_tracking_update = 0;
static unsigned long TRACKING_UPDATE_INTERVAL = 1000;
static float TRACKING_THRESHOLD = 0.1;
static const int i_STABLE_COUNT_THRESHOLD = 5;
static const float MAX_TRACKING_ADJUSTMENT = 0.5;

static unsigned long t_last_status_display = 0;
static const unsigned long STATUS_DISPLAY_INTERVAL = 5000;
static bool b_weight_in_serial = false;

static int i_stable_count = 0;
static bool b_tracking_enabled = true;
static bool b_tracking_active = false;

static float f_previous_stable_value = 0.0;
static float f_current_raw_value = 0.0;
static float STABLE_OUTPUT_THRESHOLD = 0.1;
static bool b_stable_output_enabled = true;
static unsigned long t_last_stable_change = 0;
static float f_driftCompensation = 0.0;
static float f_maxDriftCompensation = 0.05;
static const unsigned long QUICK_ZERO_HOLD_TIMEOUT = 3000;
static const unsigned long ZERO_DISPLAY_MISMATCH_TIMEOUT = 1500;
static const float ZERO_DISPLAY_MISMATCH_THRESHOLD = 0.5;
static const uint8_t ADC_ERROR_RECOVERY_COUNT = 2;
static bool b_adc_recovery_active = false;
static uint8_t i_adc_recovery_count = 0;

static uint8_t g_resetReasonCode = 0;

bool b_negativeWeight = false;

bool b_weight_quick_zero = false;
char c_weight[10];
char c_brew_ratio[10];

static inline void resetAdcRecoveryState() {
  b_adc_recovery_active = false;
  i_adc_recovery_count = 0;
}
bool refreshScaleDatasetAfterDiscontinuity(const char *context);
void resetScaleOutputAfterAdcDiscontinuity();
bool tareScaleWhenAdcReady(const char *context, bool userRequested = false);
bool setScaleSamplesInUseWhenReady(uint8_t samplesInUse, const char *context);
bool wakeScaleFromSoftSleep(const char *context);
void consumeScaleTareStatus();
void clearPendingAutomaticTareState();
unsigned long t_extraction_begin = 0;
unsigned long t_extraction_first_drop = 0;
unsigned long t_extraction_last_drop = 0;
unsigned long t_ready_to_brew = 0;
int i_extraction_minimium_timer = 7;

unsigned long t_PowerDog = 0;
int tareCounter = 0;
const float f_weight_default_coffee = 0;

float aWeight = 0;
float aWeightDiff = 0.15;
float atWeight = 0;
float atWeightDiff = 0.3;
float asWeight = 0;
float asWeightDiff = 0.1;
float f_weight_adc = 0.0;
float f_weight_smooth;
float f_displayedValue;
#if HDS_ENABLE_GRINDER
float f_grinder_fast_weight = 0.0f;
#endif
float f_flow_rate;
#if HDS_ENABLE_GRINDER
uint32_t grinderFastWeightSequence = 0;
#endif

unsigned long t_auto_tare = 0;
unsigned long t_auto_stop = 0;
unsigned long t_scale_stable = 0;
unsigned long t_time_out = 0;
unsigned long t_last_weight_adc = 0;
unsigned long t_oled_refresh = 0;
unsigned long t_esp_now_refresh = 0;
unsigned long t_flow_rate = 0;
int t_extraction_first_drop_num = 0;
int b_power_off = 0;
struct CoffeeData {
  int b_mode;
  int b_running;
  bool b_extraction;
  float f_flow_rate;
  float f_displayedValue;
  float f_weight_dose;
  unsigned long t_extraction_begin;
  unsigned long t_extraction_first_drop_num;
  unsigned long t_extraction_last_drop;
  unsigned long t_elapsed;
  long dataFlag;
  int b_power_off;
};

const int autoTareInterval = 500;
const int autoStopInterval = 500;
const int scaleStableInterval = 500;
const int timeOutInterval = 30 * 1000;
const int i_oled_print_interval = 100;
const int i_esp_now_interval = 100;
const int i_serial_print_interval = 0;
bool b_extraction = false;
int b_mode = 0;

bool b_menu = false;
unsigned long t_menuExitTime = 0;

bool b_calibration = false;
volatile bool b_ota = false;
volatile bool b_pullOtaRunning = false;
int i_calibration = 0;
bool b_show_info = false;
bool b_set_container = false;
bool b_minus_container = false;
bool b_minus_container_button = false;
bool b_ready_to_brew = false;
bool b_is_charging = false;
bool b_espnow = false;
//bool b_debug = DEBUG;                                //debug信息显示
#if DEBUG_BATTERY
bool b_debug_battery = false;
#endif                         //DEBUG_BATTERY

int i_button_cal_status = 0;
int i_cal_weight = 0;
float f_weight_dose = 0.0;
float f_weight_container = 0.0;

int i_decimal_precision = 1;
char c_flow_rate[10];
float f_flow_rate_last_weight = 0.0;

char* c_battery = (char*)"0";
char* c_batteryTemp = (char*)"0";
unsigned long t_battery = 0;
int i_battery = 0;
int i_batteryRefreshTareInterval = 30 * 1000;
unsigned long t_batteryRefresh = 0;
float f_batteryVoltage = 0;
#ifdef AVR
float f_vref = 4.72;
float f_true_battery_reading = 4.72;
float f_adc_battery_reading = 4.72;
float f_divider_factor = f_true_battery_reading / f_adc_battery_reading * 4.07 / 4.38;
#endif
#if defined(ESP8266) || defined(ESP32) || defined(ARDUINO_ARCH_RP2040) || defined(ARDUINO_ARCH_MBED_RP2040)
float f_vref = 3.3;
float f_true_battery_reading = 4.24;
float f_adc_battery_reading = 1.99;
float f_divider_factor = f_true_battery_reading / f_adc_battery_reading * 4.07 / 4.38;
#endif

int i_display_rotation = 0;

#endif
