#ifndef PARAMETER_H
#define PARAMETER_H
//declaration

//ble
bool b_ble_enabled = false;
bool b_usbweight_enabled = false;
unsigned long lastBleWeightNotifyTime = 0;  // Stores the last time the weight notification was sent
unsigned long lastUsbWeightNotifyTime = 0;  // Stores the last time the weight notification was sent

unsigned long lastWeightTextNotifyTime = 0;   // Stores the last time the weight notification was sent
unsigned long weightBleNotifyInterval = 100;  // Interval at which to send weight notifications (milliseconds)
unsigned long weightUsbNotifyInterval = 100;  // Interval at which to send weight notifications (milliseconds)
unsigned long weightTextNotifyInterval = 1000;
int i_onWrite_counter = 0;
unsigned long t_heartBeat = 0;
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
bool b_softSleep = false;
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
unsigned long t_shutdownFailBle = 0;  //for popping up shut down fail due to ble is connected.
bool b_shutdownFailBle = false;
bool b_u8g2Sleep = true;
unsigned long t_bootTare = 0;
bool b_bootTare = false;
int i_bootTareDelay = 1000;
int i_tareDelay = 200;             //tare delay for button
unsigned long t_tareByButton = 0;  //tare time stamp used by button to mimic delay
bool b_tareByButton = false;
unsigned long t_tareByBle = 0;
bool b_tareByBle = false;
unsigned long t_tareStatus = 0;  //tare done time stamp
unsigned long t_power_off;       //关机倒计时
#if defined(ACC_MPU6050) || defined(ACC_BMA400)
unsigned long t_power_off_gyro = 0;  //侧放关机倒计时
#endif
unsigned long t_button_pressed;  //进入萃取模式的时间点
unsigned long t_temp;            //上次更新温度和度数时间
float f_temp_tare = 0;
// int i_sample = 0;       //采样数0-7
// int i_sample_step = 0;  //设置采样数的第几步
int i_icon = 0;  //充电指示电量数字0-6
int i_setContainerWeight = 0;
float f_filtered_temperature = 0;
bool b_ads1115InitFail = false;  //ads1115 not detected flag



//电子秤参数和计时点
bool b_weight_in_serial = false;
bool b_negativeWeight = false;

bool b_weight_quick_zero = false;           //Tare后快速显示为0优化
char c_weight[10];                          //咖啡重量显示
char c_brew_ratio[10];                      //粉水比显示
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

float f_weight_before_input;  //按录入按钮之前的第几个读数（重量）

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
const int i_oled_print_interval = 0;    //oled刷新间隔（毫秒）
const int i_esp_now_interval = 100;     //espnow刷新间隔（毫秒）
const int i_serial_print_interval = 0;  //称重输出间隔（毫秒）
//flags 模式标识
bool b_extraction = false;  //萃取模式标识
int b_mode = 0;             //0 = pourover; 1 = espresso;

bool b_menu = false;
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
int batteryRefreshTareInterval = 1 * 1000;  //1秒刷新一次电量显示
unsigned long t_batteryRefresh = 0;         //电池充电循环判断打点
#ifdef AVR
float f_vref = 4.72;                  //5V pin true reading
float f_true_battery_reading = 4.72;  //需测量usb vcc电压（不一定是usb，可以是电池电压）
float f_adc_battery_reading = 4.72;   //mcu读取的电压
float f_divider_factor = f_true_battery_reading / f_adc_battery_reading * 4.07 / 4.38;
#endif
#if defined(ESP8266) || defined(ESP32) || defined(ARDUINO_ARCH_RP2040) || defined(ARDUINO_ARCH_MBED_RP2040)
float f_vref = 3.3;                                                                     //3.3V pin true reading
float f_true_battery_reading = 4.24;                                                    //需测量usb vcc电压（不一定是usb，可以是电池电压）
float f_adc_battery_reading = 1.99;                                                     //mcu读取的电压
float f_divider_factor = f_true_battery_reading / f_adc_battery_reading * 4.07 / 4.38;  //for 3.3v board use 2x100k resistor
#endif

int i_display_rotation = 0;  //旋转方向 0 1 2 3 : 0 90 180 270

//EEPROM地址列表
int i_addr_calibration_value = 0;  //EEPROM从0开始记录 校准值以float类型表示 float calibrationValue;
//int i_addr_sample = i_addr_calibration_value + sizeof(f_calibration_value);                              //int sample值
int i_addr_container = i_addr_calibration_value + sizeof(f_calibration_value);  //采样数以float类型表示 float f_weight_container
int i_addr_mode = i_addr_container + sizeof(f_weight_container);                //模式以int类型表示 int b_mode
int INPUTCOFFEEPOUROVER_ADDRESS = i_addr_mode + sizeof(b_mode);
int INPUTCOFFEEESPRESSO_ADDRESS = INPUTCOFFEEPOUROVER_ADDRESS + sizeof(INPUTCOFFEEPOUROVER);
int i_addr_beep = INPUTCOFFEEESPRESSO_ADDRESS + sizeof(INPUTCOFFEEESPRESSO);
int i_addr_welcome = i_addr_beep + sizeof(b_beep);                                                   //str_welcome
int i_addr_batteryCalibrationFactor = i_addr_welcome + sizeof(str_welcome);                          //f_batteryCalibrationFactor
int i_addr_requireHeartBeat = i_addr_batteryCalibrationFactor + sizeof(f_batteryCalibrationFactor);  //b_requireHeartBeat
int i_addr_screenFlipped = i_addr_requireHeartBeat + sizeof(b_requireHeartBeat);                     //b_screenFlipped
int i_addr_timeOnTop = i_addr_screenFlipped + sizeof(b_screenFlipped);                               //b_timeOnTop
int i_addr_btnFuncWhileConnected = i_addr_timeOnTop + sizeof(b_timeOnTop);                           //b_btnFuncWhileConnectedOn


//int i_addr_debug = i_addr_batteryCalibrationFactor + sizeof(f_batteryCalibrationFactor);  //str_welcome

#endif