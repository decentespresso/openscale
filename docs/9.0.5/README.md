# HDS 9.0.5 support

本目录记录 openscale(上一正式版 8.3.1)对 **HDS 9.0.5 硬件**的支持工作。
9.0.5 与 8.3.1 的差异:板上新增 **TI BQ27427YZFR** 电池电量计(I2C 固定 0x55)。

**兼容性核心原则(经用户确认)**:开机通过 I2C 地址查找芯片;找不到 BQ27427 时,
Bat. Info 菜单、深睡钩子、化学特性配置全部不启用,8.3.1 板行为与旧版完全一致。
ADS1115 功能保留:有电量计时跳过其初始化/使用,无则照旧;未来去掉 ADS1115 的
板子走已有内部 ADC 回退路径。

## 代码位置(已合入 openscale9_0_5)

- `src/fuel_gauge.h/cpp` — 兼容层驱动(自包含,引脚宏可覆盖)
- `src/fuel_gauge_menu.h/cpp` — Bat. Info 菜单(分页数据页,末页后退出)
- `lib/BQ27427_Battery_Fuel_Gauge_Arduino_Library-1.0.4/` — 主参考库(MIT,Arduino 注册表版)
- `lib/BQ27427-Arduino-arkhipenko/` — 备选库(测试用)
- `tools/probe_9.0.5/` — 原始协议探针(裸 Wire 验证)
- `tools/driver_test_9.0.5/` — 驱动冒烟测试(真机验证驱动 API)
- `docs/9.0.5/` — 中文手册(PDF + 翻译)、本说明
- `platformio.ini` — 含 80 MHz CPU 配置(f_cpu + custom_sdkconfig 组合)
  与 BQ27427 库依赖

## 实测结论(2026-08-14,真机)

| 项目 | 结果 |
|---|---|
| I2C 总线设备 | 0x48(ADS1115)、0x55(BQ27427)|
| DEVICE_TYPE / FW_VERSION | 0x0427 / 0x0202 |
| 化学特性 | 已切 **0x1202(4.2 V)** 并持久化 |
| 电池读数 | 4162 mV / 99% SOC / 1237 mAh / SOH 94% / 27.8 °C |
| 充电识别 | AverageCurrent >30 mA 判充电;USB 用原 USB_DET 逻辑 |

## 关键踩坑记录

1. Control() 子命令:写后指针停在 0x02,必须重新指向 0x00 再读;所有写带 STOP。
2. CHEM_ID 返回十六进制字符编码(0x1202 ≠ 十进制 1202)。
3. 化学切换必须 SOFT_RESET 才生效(TRM 5.1.15)。
4. SOH=0x20、无 CycleCount(BQ27427 TRM;网上表混入 BQ27441 的信息)。
5. PlatformIO 6.1.19 的 `src_dir` 只认 `[platformio]` 段。
6. CH9102 重枚举后端口名会变,刷写前 `ls /dev/cu.*` 确认。

## 合入主固件的改动点(integration)

见 `integration.md`(同目录):
hds.ino 开机调用、menu.h 注册、power.h 电压来源切换、深睡钩子、platformio.ini。
