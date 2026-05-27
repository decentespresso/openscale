#ifndef PARAMETER_H
#define PARAMETER_H
//declaration

//ble
bool b_ble_enabled = false;
bool b_usbweight_enabled = false;
unsigned long weightBleNotifyInterval = 100;  // BLE notify interval (ms). Fixed at 100ms (10Hz); not runtime-configurable over BLE.
unsigned long weightUsbNotifyInterval = 100;  // USB binary notify interval (ms)
unsigned long weightTextNotifyInterval = 1000;  // USB text/debug line interval (ms)
// Base period of the unified weight-output tick. One grid timer drives every
// interface; each sends every (its NotifyInterval / base) ticks. All the
// per-interface intervals above (and the WS ones below) are multiples of this.
const unsigned long WEIGHT_BASE_INTERVAL_MS = 100;
const unsigned long WEBSOCKET_2HZ_NOTIFY_INTERVAL_MS = 500;
const unsigned long WEBSOCKET_5HZ_NOTIFY_INTERVAL_MS = 200;
const unsigned long WEBSOCKET_10HZ_NOTIFY_INTERVAL_MS = 100;
const unsigned long WEBSOCKET_DEFAULT_NOTIFY_INTERVAL_MS = WEBSOCKET_2HZ_NOTIFY_INTERVAL_MS;
const unsigned long WEBSOCKET_STATUS_NOTIFY_INTERVAL_MS = 5000;
// volatile: written from the AsyncTCP task (WS event callback) and read
// from the main loop. Without volatile, the compiler may keep these cached
// in a register across the loop's WS gate check on the other core.
volatile unsigned long weightWebsocketNotifyInterval = WEBSOCKET_DEFAULT_NOTIFY_INTERVAL_MS;
volatile bool b_websocketEventsEnabled = false;
volatile bool b_websocketLowPowerEnabled = false;
volatile unsigned long t_lastWebsocketStatusUpdate = 0;

// Websocket pending-command mask. Set on the AsyncTCP task by the WS event
// callback; drained on the main loop. Defers hardware-touching ops (u8g2,
// stopWatch, power-rail GPIOs) so they never race the main loop.
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
portMUX_TYPE wsPendingMux = portMUX_INITIALIZER_UNLOCKED;
volatile uint32_t wsPendingMask = 0;

int i_onWrite_counter = 0;
unsigned long t_heartBeat = 0;
unsigned long t_firstConnect = 0;
bool b_requireHeartBeat = true;
bool b_screenFlipped = false;
bool b_timeOnTop = false;
bool b_btnFuncWhileConnected = false;

//
int windowLength = 5;  // default window length
// define circular buffer
float circularBuffer[5];
int bufferIndex = 0;

//constant 常量
//const int sample[] = { 1, 2, 4, 8, 16, 32, 64, 128 };

const int i_margin_top = 8;
const int i_margin_bottom = 8;

int b_beep = 1;  //although it should be bool, change it from int to bool will affect eeprom address, because sizeof(b_beep) was used.
bool b_about = false;
bool b_debug = false;

unsigned long t_batteryIcon = 0;
bool b_showBatteryIcon = true;
// volatile: now also written from the AsyncTCP task (WS soft-sleep command).
volatile bool b_softSleep = false;
#if defined(ACC_MPU6050) || defined(ACC_BMA400)
bool b_gyroEnabled = true;
#endif

//varables 变量
uint64_t GPIO_reason = 0;
bool b_usbLinked = false;
int GPIO_power_on_with = -1;

unsigned long t_power_on_button = 0;  // Variable to store the timestamp when the button is pressed
bool b_button_pressed = false;        // Boolean flag to indicate whether the button is currently pressed


float INPUTCOFFEEPOUROVER = 20.0;
float INPUTCOFFEEESPRESSO = 20.0;
float f_batteryCalibrationFactor = 0.66;
String str_welcome = "welcome";
float f_calibration_value;   //称重单元校准值
float f_up_battery;          //开机时电池电压
unsigned long t_up_battery;  //开机到现在时间

bool b_chargingOLED = true;
bool b_heartBeatIcon = false; //debug ble heart icon
unsigned long t_shutdownFailBle = 0;  //for popping up shut down fail due to ble is connected.
bool b_shutdownFailBle = false;
// volatile: now also written from the AsyncTCP task (WS display/sleep commands).
volatile bool b_u8g2Sleep = true;
unsigned long t_bootTare = 0;
bool b_bootTare = false;
int i_bootTareDelay = 1000;
//int i_tareDelay = 200;             //tare delay for button
int i_tareDelay = 0;             //tare delay 0ms for finger detection
unsigned long t_tareByButton = 0;  //tare time stamp used by button to mimic delay
unsigned long t_quickZeroStart = 0;
bool b_tareByButton = false;
unsigned long t_tareByBle = 0;
uint8_t i_remoteTareRequests = 0;
bool b_tareByBle = false;
portMUX_TYPE remoteTareMux = portMUX_INITIALIZER_UNLOCKED;
unsigned long t_tareStatus = 0;  //tare done time stamp
unsigned long t_power_off;       //关机倒计时
// volatile: set in processWsPendingCmds (main loop) and read in loop's top
// guard; main loop also writes it from other paths. Keep volatile defensively.
volatile bool b_powerOff = false;
#if defined(ACC_MPU6050) || defined(ACC_BMA400)
unsigned long t_power_off_gyro = 0;  //侧放关机倒计时
#endif
unsigned long t_button_pressed;  //进入萃取模式的时间点
unsigned long t_temp;            //上次更新温度和度数时间
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
// int i_sample = 0;       //采样数0-7
// int i_sample_step = 0;  //设置采样数的第几步
int i_icon = 0;  //充电指示电量数字0-6
int i_setContainerWeight = 0;
float f_filtered_temperature = 0;
bool b_ads1115InitFail = true;  //ads1115 not detected flag
bool b_wifiOnBoot = false;
bool b_autoSleep = true;
bool b_quickBoot = false;
unsigned int i_buttonBootDelay = 500;
bool b_showChargingUI = false;

//电子秤参数和计时点
// Enhanced tracking system global variables
static float f_tracking_offset = 0.0;              // Current tracking offset
static float f_tracking_target = 0.0;              // Current tracking target weight
static unsigned long t_last_tracking_update = 0;   // Last tracking update time
static unsigned long TRACKING_UPDATE_INTERVAL = 1000; // Tracking update interval 1 seconds
static float TRACKING_THRESHOLD = 0.1;      // Tracking stability threshold
static const int i_STABLE_COUNT_THRESHOLD = 5;     // Stable count threshold
static const float MAX_TRACKING_ADJUSTMENT = 0.5;  // Maximum single adjustment

static unsigned long t_last_status_display = 0;
static const unsigned long STATUS_DISPLAY_INTERVAL = 5000;
static bool b_weight_in_serial = false;

static int i_stable_count = 0;                     // Stable state counter
static bool b_tracking_enabled = true;          // Tracking enable flag
static bool b_tracking_active = false;          // Whether tracking is currently active

// Stable output system global variables
static float f_previous_stable_value = 0.0;        // Previous stable output value
static float f_current_raw_value = 0.0;            // Current raw input value
static float STABLE_OUTPUT_THRESHOLD = 0.1;       // Minimum change to update output
static bool b_stable_output_enabled = true;     // Stable output enable flag
static unsigned long t_last_stable_change = 0;     // Time of last stable change
static float f_driftCompensation = 0.0;  // Continuous temperature drift compensation
static float f_maxDriftCompensation = 0.05;  // Maximum micro-drift range for temperature compensation (g)
// Range: 0.01g to this value will be considered as temperature drift
// Values above this are considered as real weight changes, not drift
static const unsigned long QUICK_ZERO_HOLD_TIMEOUT = 3000;
static const unsigned long ZERO_DISPLAY_MISMATCH_TIMEOUT = 1500;
static const float ZERO_DISPLAY_MISMATCH_THRESHOLD = 0.5;
static const uint8_t ADC_ERROR_RECOVERY_COUNT = 2;
static bool b_adc_recovery_active = false;
// volatile: written on the main loop -- incremented on each ADC power-cycle
// recovery, reset to 0 by resetAdcRecoveryState() -- and read in the WS status
// frame (which can be built on the AsyncTCP task). uint32_t (not uint8_t) so a
// *perpetual* recovery loop -- the one failure mode the stall watchdog is blind
// to -- keeps counting truthfully over a long soak instead of saturating at 255.
static volatile uint32_t i_adc_recovery_count = 0;
//bool b_tempDisablePowerOff = true;

// Instrumentation for diagnosing the "weight stops being collected" failure
// under sustained load (suspected thermal). These are all written on the main
// loop and read by the WS status frame, which is built BOTH on the main loop
// (periodic) AND on the AsyncTCP task (command responses) -- so the read crosses
// a task boundary. volatile prevents the AsyncTCP reader caching a stale value
// (single aligned scalars => the load/store is atomic on Xtensa, no mutex
// needed). b_weightStalled is set by the pureScale() stall watchdog when the ADC
// raw value is frozen/railed.
volatile bool b_weightStalled = false;
// volatile for the same cross-task reason; written once at boot in setup().
volatile const char *g_resetReason = "unknown";
// Peak/last-event stats since boot (no NVS; reset on reboot, which g_resetReason
// then explains). g_socTempMaxC = highest SoC die temp seen. The *_stall_*
// fields capture the most recent stall so the failure is visible after the fact
// -- consumers must treat last_stall_temp_c as valid only when g_lastStallMs != 0
// (0.0 otherwise means "no stall yet", not a real 0 C reading).
volatile float g_socTempC = 0.0f;          // latest SoC temperature (C)
volatile float g_socTempMaxC = -100.0f;    // peak SoC temperature since boot (C); -100 = no valid sample yet
volatile uint32_t g_stallCount = 0;        // number of weight-stall events since boot
volatile unsigned long g_lastStallMs = 0;  // millis() of the last stall onset (0 = none)
volatile float g_lastStallTempC = 0.0f;    // SoC temp when the last stall began (valid only if g_lastStallMs != 0)

// Snapshot of the stopWatch state, refreshed once per main-loop iteration. The
// WS status frame is built BOTH on the main loop AND on the AsyncTCP task
// (command responses); stopWatch is a multi-field object (running flag + start
// ts + accumulator) also mutated from BLE/USB, so reading it directly off the
// AsyncTCP task can tear (CLAUDE.md). The status frame reads these single
// aligned volatiles instead. g_timerElapsed carries stopWatch.elapsed() in its
// configured resolution (SECONDS) -- it is the WS "timer_seconds" field.
volatile bool g_timerRunning = false;
volatile unsigned long g_timerElapsed = 0;

bool b_negativeWeight = false;

bool b_weight_quick_zero = false;           //Tare后快速显示为0优化
char c_weight[10];                          //咖啡重量显示
char c_brew_ratio[10];                      //粉水比显示

static inline void resetAdcRecoveryState() {
  b_adc_recovery_active = false;
  i_adc_recovery_count = 0;
}
unsigned long t_extraction_begin = 0;       //开始萃取打点
unsigned long t_extraction_first_drop = 0;  //下液第一滴打点
unsigned long t_extraction_last_drop = 0;   //下液结束打点
unsigned long t_ready_to_brew = 0;          //准备好冲煮的时刻(手冲)
int i_extraction_minimium_timer = 7;        //前7秒不判断停止计时

unsigned long t_PowerDog = 0;             //电源5s看门狗
int tareCounter = 0;                      //不稳计数器
const float f_weight_default_coffee = 0;  //默认咖啡粉重量

float aWeight = 0;         //稳定状态比对值（g）
float aWeightDiff = 0.15;  //稳定停止波动值（g）
float atWeight = 0;        //自动归零比对值（g）
float atWeightDiff = 0.3;  //自动归零波动值（g）
float asWeight = 0;        //下液停止比对值（g）
float asWeightDiff = 0.1;  //下液停止波动值（g）
float f_weight_adc = 0.0;  //原始读出值（g）
float f_weight_smooth;
float f_displayedValue;
float f_flow_rate;

unsigned long t_auto_tare = 0;        //自动归零打点
unsigned long t_auto_stop = 0;        //下液停止打点
unsigned long t_scale_stable = 0;     //稳定状态打点
unsigned long t_time_out = 0;         //超时打点
unsigned long t_last_weight_adc = 0;  //最后一次重量输出打点
unsigned long t_oled_refresh = 0;     //最后一次oled刷新打点
unsigned long t_esp_now_refresh = 0;  //最后一次espnow刷新打点
unsigned long t_flow_rate = 0;        //上次流量计时
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
  long dataFlag;    // Flag to identify the type of data
  int b_power_off;  //if 1 then power off.
};

const int autoTareInterval = 500;       //自动归零检测间隔（毫秒）
const int autoStopInterval = 500;       //下液停止检测间隔（毫秒）
const int scaleStableInterval = 500;    //稳定状态监测间隔（毫秒）
const int timeOutInterval = 30 * 1000;  //超时检测间隔（毫秒）
const int i_oled_print_interval = 100;    //oled刷新间隔（毫秒）
const int i_esp_now_interval = 100;     //espnow刷新间隔（毫秒）
const int i_serial_print_interval = 0;  //称重输出间隔（毫秒）
//flags 模式标识
bool b_extraction = false;  //萃取模式标识
int b_mode = 0;             //0 = pourover; 1 = espresso;

bool b_menu = false;
unsigned long t_menuExitTime = 0;
// Timestamp recording when the menu exit process started
// Used to implement a protection period preventing unintended operations

bool b_calibration = false;  //Calibration flag
bool b_ota = false;          //wifi ota flag
int i_calibration = 0;       //0 for manual cal, 1 for smart cal
//bool b_set_sample = false;              //开机菜单 设置采样数
bool b_show_info = false;               //开机菜单 显示信息
bool b_set_container = false;           //开机菜单 设置称豆容器重量
bool b_minus_container = false;         //是否减去称豆容器
bool b_minus_container_button = false;  //是否减去称豆容器
bool b_ready_to_brew = false;           //准备冲煮并计时
bool b_is_charging = false;             //正在充电标识
bool b_espnow = false;
//bool b_debug = DEBUG;                                //debug信息显示
#if DEBUG_BATTERY
bool b_debug_battery = false;  //debug电池信息
#endif                         //DEBUG_BATTERY

//电子秤校准参数
int i_button_cal_status = 0;     //校准的不同阶段
int i_cal_weight = 0;            //the weight to select
float f_weight_dose = 0.0;       //咖啡粉重量
float f_weight_container = 0.0;  //咖啡手柄重量

int i_decimal_precision = 1;          //小数精度 0.1g/0.01g
char c_flow_rate[10];                 //流速文字
float f_flow_rate_last_weight = 0.0;  //流速上次记重

char* c_battery = (char*)"0";               //电池字符 0-5有显示 6是充电图标
char* c_batteryTemp = (char*)"0";           //临时暂存电池状态 以便电量显示不跳跃
unsigned long t_battery = 0;                //电池充电循环判断打点
int i_battery = 0;                          //电池充电循环变量
int i_batteryRefreshTareInterval = 30 * 1000;  //Refresh battery every 30 seconds
unsigned long t_batteryRefresh = 0;         //Battery refresh timestamp
float f_batteryVoltage = 0;
#ifdef AVR
float f_vref = 4.72;                  //5V pin true reading
float f_true_battery_reading = 4.72;  //需测量usb vcc电压（不一定是usb，可以是电池电压）
float f_adc_battery_reading = 4.72;   //mcu读取的电压
float f_divider_factor = f_true_battery_reading / f_adc_battery_reading * 4.07 / 4.38;
#endif
#if defined(ESP8266) || defined(ESP32) || defined(ARDUINO_ARCH_RP2040) || defined(ARDUINO_ARCH_MBED_RP2040)
float f_vref = 3.3;                                                                     //3.3V pin true reading
float f_true_battery_reading = 4.24;                                                    //Measure usb vcc voltage（could not be usb，but can be battery voltage）
float f_adc_battery_reading = 1.99;                                                     //mcu adc reading voltage
float f_divider_factor = f_true_battery_reading / f_adc_battery_reading * 4.07 / 4.38;  //for 3.3v board use 2x100k resistor
#endif

int i_display_rotation = 0;  //rotation 0 1 2 3 : 0 90 180 270

//EEPROM Address
int i_addr_calibration_value = 0;  //EEPROM start from 0, the calibration is shown as float calibrationValue;
//int i_addr_sample = i_addr_calibration_value + sizeof(f_calibration_value);                              //int sample
int i_addr_container = i_addr_calibration_value + sizeof(f_calibration_value);  //sample number float f_weight_container
int i_addr_mode = i_addr_container + sizeof(f_weight_container);                //mode int b_mode
int INPUTCOFFEEPOUROVER_ADDRESS = i_addr_mode + sizeof(b_mode);
int INPUTCOFFEEESPRESSO_ADDRESS = INPUTCOFFEEPOUROVER_ADDRESS + sizeof(INPUTCOFFEEPOUROVER);
int i_addr_beep = INPUTCOFFEEESPRESSO_ADDRESS + sizeof(INPUTCOFFEEESPRESSO);
int i_addr_welcome = i_addr_beep + sizeof(b_beep);                                                   //str_welcome
int i_addr_batteryCalibrationFactor = i_addr_welcome + sizeof(str_welcome);                          //f_batteryCalibrationFactor
int i_addr_requireHeartBeat = i_addr_batteryCalibrationFactor + sizeof(f_batteryCalibrationFactor);  //b_requireHeartBeat
int i_addr_screenFlipped = i_addr_requireHeartBeat + sizeof(b_requireHeartBeat);                     //b_screenFlipped
int i_addr_timeOnTop = i_addr_screenFlipped + sizeof(b_screenFlipped);                               //b_timeOnTop
int i_addr_btnFuncWhileConnected = i_addr_timeOnTop + sizeof(b_timeOnTop);                           //b_btnFuncWhileConnected
int i_addr_enableWifiOnBoot = i_addr_btnFuncWhileConnected + sizeof(b_btnFuncWhileConnected);        //b_wifiOnBoot
int i_addr_autoSleep = i_addr_enableWifiOnBoot + sizeof(b_wifiOnBoot);
int i_addr_quickBoot = i_addr_autoSleep + sizeof(b_autoSleep);//b_quickBoot
int i_addr_driftCompensation = i_addr_quickBoot + sizeof(b_quickBoot);//f_maxDriftCompensation

//int i_addr_enableWifiOnBoot = i_addr_btnFuncWhileConnected + sizeof(b_wifiOnBoot);


//int i_addr_debug = i_addr_batteryCalibrationFactor + sizeof(f_batteryCalibrationFactor);  //str_welcome

#endif
