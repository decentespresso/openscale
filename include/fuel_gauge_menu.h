// Bat. Info menu for the BQ27427 fuel gauge, following the showStatus
// pattern: draw a frame, hold it, then poll buttons. ENTER cycles pages,
// NEXT steps back (exit on first page).
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

static bool b_showBatInfoData = false;
static bool s_showBatInfoBusy = false;
static int i_batInfoPage = 0;

static int batInfoTotalPages() {
  return b_hasFuelGauge ? 2 : 1;
}

static void drawBatInfoPage() {
  char pageInfo[10];
  snprintf(pageInfo, sizeof(pageInfo), "%d/%d", i_batInfoPage + 1,
           batInfoTotalPages());

  u8g2.firstPage();
  u8g2.setFont(u8g2_font_6x12_tr);
  do {
    u8g2.drawUTF8(AR(pageInfo), 12, pageInfo);
    if (!b_hasFuelGauge) {
      char l[8];
      snprintf(l, sizeof(l), "%.2fV", f_batteryVoltage);
      u8g2.drawUTF8(0, 12, l);
      snprintf(l, sizeof(l), "%d%%", batteryPercent());
      u8g2.drawUTF8(0, 24, l);
      snprintf(l, sizeof(l), "CRG %s", fuelGaugeUsbPlugged() ? "Y" : "N");
      u8g2.drawUTF8(0, 36, l);
      snprintf(l, sizeof(l), "USB %s", fuelGaugeUsbPlugged() ? "Y" : "N");
      u8g2.drawUTF8(0, 48, l);
    } else if (i_batInfoPage == 0) {
      char l[12];
      char r[12];
      snprintf(l, sizeof(l), "%.2f V", fuelGaugeVoltageV());
      snprintf(r, sizeof(r), "%.1f C", fuelGaugeTempC());
      u8g2.drawUTF8(0, 12, l);
      u8g2.drawUTF8(64, 12, r);
      snprintf(l, sizeof(l), "%d mA", fuelGaugeCurrentMa());
      snprintf(r, sizeof(r), "%d mW", fuelGaugePowerMw());
      u8g2.drawUTF8(0, 24, l);
      u8g2.drawUTF8(64, 24, r);
      snprintf(l, sizeof(l), "Cap %u", fuelGaugeCapacityMah());
      snprintf(r, sizeof(r), "FCC %u", fuelGaugeFullCapacityMah());
      u8g2.drawUTF8(0, 36, l);
      u8g2.drawUTF8(64, 36, r);
      snprintf(l, sizeof(l), "Health %u%%", fuelGaugeSoHPercent());
      snprintf(r, sizeof(r), "Bat. %u%%", fuelGaugeSocPercent());
      u8g2.drawUTF8(0, 48, l);
      u8g2.drawUTF8(64, 48, r);
      snprintf(l, sizeof(l), "CRG %s", fuelGaugeCharging() ? "Y" : "N");
      snprintf(r, sizeof(r), "USB %s", fuelGaugeUsbPlugged() ? "Y" : "N");
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
  if (s_showBatInfoBusy) {
    return;
  }
  s_showBatInfoBusy = true;
  b_showBatInfoData = true;
  i_batInfoPage = 0;
  drawBatInfoPage();
  delay(1000);
  unsigned long t_lastRefresh = millis();
  while (b_showBatInfoData) {
    if (digitalRead(MENU_BTN_ENTER) == LOW) {
      i_batInfoPage = (i_batInfoPage + 1) % batInfoTotalPages();
      drawBatInfoPage();
      t_lastRefresh = millis();
      delay(250);
    }
    if (digitalRead(MENU_BTN_BACK) == LOW) {
      if (i_batInfoPage <= 0) {
        b_showBatInfoData = false;
      } else {
        i_batInfoPage--;
        drawBatInfoPage();
        t_lastRefresh = millis();
        delay(250);
      }
    }
    if (millis() - t_lastRefresh >= 500) {
      drawBatInfoPage();
      t_lastRefresh = millis();
    }
    delay(50);
  }
  s_showBatInfoBusy = false;
}

#endif
