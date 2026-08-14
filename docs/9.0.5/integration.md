# HDS 9.0.5 合入改动清单(openscale9_0_5 分支)

本次合入的完整改动。未推送 GitHub。

## 已改文件

| 文件 | 改动 |
|---|---|
| `src/hds.ino` | `#include "fuel_gauge.h"`;`Wire.begin` 后调用 `fuelGaugeBegin(); compactMainMenu();` |
| `src/fuel_gauge.h` | 驱动 API 声明;`extern volatile bool b_hasFuelGauge`;引脚宏(可覆盖) |
| `src/fuel_gauge.cpp` | 检测(0x55 + DEVICE_TYPE 0x0427)、Chem 1202 强制(TRM 流程)、读取 API、深睡钩子 |
| `src/fuel_gauge_menu.h` | Bat. Info 菜单(header-only,14 项分页,末页后 NEXT 退出) |
| `include/menu.h` | include 两个新头;`Menu menuBatInfo`;`mainMenu[]` 加入;`compactMainMenu()`(无芯片时运行时移除菜单项) |
| `include/parameter.h` | `volatile bool b_hasFuelGauge`(全局状态按规范放这里) |
| `platformio.ini` | `lib_deps` 加 `edrean/BQ27427 Battery Fuel Gauge Arduino Library @ 1.0.4`(注册表自动下载);80 MHz CPU 配置(f_cpu + custom_sdkconfig) |
| `.gitignore` | 加 `.dummy/`、`managed_components/`、`sdkconfig.*` |
| `lib/README.md` | 库来源说明(注册表 + GitHub,勿手动导入) |

## 兼容性原则(用户确认)

- 开机 I2C 探测 0x55 + DEVICE_TYPE 验证;找不到 → 菜单无 Bat. Info、深睡钩子 no-op、Chem 配置跳过,8.3.1 板行为与旧版完全一致(`compactMainMenu` 运行时移除菜单项)。
- ADS1115:有电量计时跳过初始化/使用;无则照旧;未来去 ADS1115 的板子走内部 ADC 回退。

## 待办(合入后)

1. `power.h` 的 `updateBattery()`:电量计存在时用 `fuelGaugeVoltageV()` 作为电压源,跳过 ADS1115 路径(`ADS_init` 调用加 `if (!b_hasFuelGauge)` 条件)
2. `power.h` 的 `esp32_sleep()`:深睡前调用 `fuelGaugeSleep()`
3. 菜单注册单点化重构(单独任务)
4. 8.3.1 板回归测试(无 0x55 时菜单与旧版一致)
