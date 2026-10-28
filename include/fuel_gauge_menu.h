#ifndef FUEL_GAUGE_MENU_H
#define FUEL_GAUGE_MENU_H
#include "fuel_gauge.h"
#include "power.h"
#include "display.h"

#ifndef MENU_BTN_ENTER
#define MENU_BTN_ENTER 2
#endif
#ifndef MENU_BTN_BACK
#define MENU_BTN_BACK 1
#endif

static uint8_t batInfoTotalPages() {
  return b_hasFuelGauge ? 2 : 1;
}

static void drawBatInfoPage(uint8_t page) {
  const FuelGaugeReading reading = fuelGaugeReading();
  const bool charging = fuelGaugeCharging();
  const bool usbPlugged = fuelGaugeUsbPlugged();
  const float voltage = b_hasFuelGauge ? reading.voltageMv / 1000.0f : f_batteryVoltage;
  const int percent = batteryPercent();
  char pageInfo[10];
  snprintf(pageInfo, sizeof(pageInfo), "%u/%u", static_cast<unsigned int>(page + 1),
           static_cast<unsigned int>(batInfoTotalPages()));

  u8g2.firstPage();
  u8g2.setFont(u8g2_font_6x12_tr);
  do {
    u8g2.drawUTF8(AR(pageInfo), 12, pageInfo);
    if (!b_hasFuelGauge) {
      char l[8];
      snprintf(l, sizeof(l), "%.2fV", voltage);
      u8g2.drawUTF8(0, 12, l);
      snprintf(l, sizeof(l), "%d%%", percent);
      u8g2.drawUTF8(0, 24, l);
      snprintf(l, sizeof(l), "CRG %s", charging ? "Y" : "N");
      u8g2.drawUTF8(0, 36, l);
      snprintf(l, sizeof(l), "USB %s", usbPlugged ? "Y" : "N");
      u8g2.drawUTF8(0, 48, l);
    } else if (!reading.valid) {
      u8g2.drawUTF8(0, 24, "Gauge unavailable");
      u8g2.drawUTF8(0, 36, "Charging disabled");
    } else if (page == 0) {
      char l[16];
      char r[16];
      snprintf(l, sizeof(l), "%.2f V", voltage);
      snprintf(r, sizeof(r), "%.1f C", reading.temperature * 0.1f - 273.15f);
      u8g2.drawUTF8(0, 12, l);
      u8g2.drawUTF8(64, 12, r);
      snprintf(l, sizeof(l), "%d mA", reading.currentMa);
      snprintf(r, sizeof(r), "%d mW", reading.powerMw);
      u8g2.drawUTF8(0, 24, l);
      u8g2.drawUTF8(64, 24, r);
      snprintf(l, sizeof(l), "Cap %u", reading.capacityMah);
      snprintf(r, sizeof(r), "FCC %u", reading.fullCapacityMah);
      u8g2.drawUTF8(0, 36, l);
      u8g2.drawUTF8(64, 36, r);
      snprintf(l, sizeof(l), "Health %u%%", reading.soh);
      snprintf(r, sizeof(r), "Bat. %u%%", reading.soc);
      u8g2.drawUTF8(0, 48, l);
      u8g2.drawUTF8(64, 48, r);
      snprintf(l, sizeof(l), "CRG %s", charging ? "Y" : "N");
      snprintf(r, sizeof(r), "USB %s", usbPlugged ? "Y" : "N");
      u8g2.drawUTF8(0, 60, l);
      u8g2.drawUTF8(64, 60, r);
    } else {
      u8g2.drawUTF8(0, 12, "True cap & health");
      u8g2.drawUTF8(0, 24, "after a full charge");
      u8g2.drawUTF8(0, 36, "and discharge cycle");
    }
  } while (u8g2.nextPage());
}

void showBatInfo() {
  bool showing = true;
  uint8_t page = 0;
  power_off(-1);
  drawBatInfoPage(page);
  delay(1000);
  unsigned long t_lastRefresh = millis();
  while (showing) {
    power_off(-1);
    if (digitalRead(MENU_BTN_ENTER) == LOW) {
      page = (page + 1) % batInfoTotalPages();
      drawBatInfoPage(page);
      t_lastRefresh = millis();
      delay(250);
    }
    if (digitalRead(MENU_BTN_BACK) == LOW) {
      if (page <= 0) {
        showing = false;
      } else {
        page--;
        drawBatInfoPage(page);
        t_lastRefresh = millis();
        delay(250);
      }
    }
    if (millis() - t_lastRefresh >= 500) {
      drawBatInfoPage(page);
      t_lastRefresh = millis();
    }
    delay(50);
  }
}

#endif
