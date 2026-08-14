// Bat. Info menu for the BQ27427 fuel gauge, following the showStatus
// pattern: draw a frame, hold it, then poll buttons. ENTER advances a page,
// advancing past the last page exits; NEXT steps back, stepping back past
// the first page exits.
#ifndef FUEL_GAUGE_MENU_H
#define FUEL_GAUGE_MENU_H
#include "fuel_gauge.h"
#include "display.h"
#include <string.h>

#ifndef MENU_BTN_ENTER
#define MENU_BTN_ENTER 2
#endif
#ifndef MENU_BTN_BACK
#define MENU_BTN_BACK 1
#endif

static const int BAT_INFO_PER_PAGE = 5;
static const int BAT_INFO_ITEMS = 13;
static bool b_showBatInfoData = false;
static bool s_showBatInfoBusy = false;
static int i_batInfoPage = 0;

static int batInfoTotalPages() {
  return (BAT_INFO_ITEMS + BAT_INFO_PER_PAGE - 1) / BAT_INFO_PER_PAGE;
}

static void buildBatInfoLine(int index, char *out, size_t len) {
  switch (index) {
    case 0:
      snprintf(out, len, "Voltage %.2f V", fuelGaugeVoltageV());
      break;
    case 1:
      snprintf(out, len, "Chip %.1f C", fuelGaugeTempC());
      break;
    case 2:
      snprintf(out, len, "Current %d mA", fuelGaugeCurrentMa());
      break;
    // case 3:
    //   snprintf(out, len, "Standby %d mA", fuelGaugeStandbyMa());
    //   break;
    // case 4:
    //   snprintf(out, len, "MaxLoad %d mA", fuelGaugeMaxLoadMa());
    //   break;
    case 3:
      snprintf(out, len, "Power %d mW", fuelGaugePowerMw());
      break;
    case 4:
      snprintf(out, len, "Capacity %u mAh", fuelGaugeCapacityMah());
      break;
    case 5:
      snprintf(out, len, "FullCap %u mAh", fuelGaugeFullCapacityMah());
      break;
    case 6:
      snprintf(out, len, "Health %u %%", fuelGaugeSoHPercent());
      break;
    case 7:
      snprintf(out, len, "Bat. Level %u %%", fuelGaugeSocPercent());
      break;
    case 8:
      snprintf(out, len, "Charging %s", fuelGaugeCharging() ? "Yes" : "No");
      break;
    case 9:
      snprintf(out, len, "USB %s", fuelGaugeUsbPlugged() ? "Plugged" : "None");
      break;
    case 10:
      snprintf(out, len, "True cap & health");
      break;
    case 11:
      snprintf(out, len, "after a full charge");
      break;
    default:
      snprintf(out, len, "and discharge cycle");
      break;
  }
}

static void drawBatInfoPage() {
  char pageInfo[10];
  snprintf(pageInfo, sizeof(pageInfo), "%d/%d", i_batInfoPage + 1,
           batInfoTotalPages());

  u8g2.firstPage();
  u8g2.setFont(u8g2_font_6x12_tr);
  do {
    u8g2.drawUTF8(AR(pageInfo), 12, pageInfo);
    for (int i = 0; i < BAT_INFO_PER_PAGE; i++) {
      int item = i_batInfoPage * BAT_INFO_PER_PAGE + i;
      if (item >= BAT_INFO_ITEMS) {
        break;
      }
      char line[24];
      buildBatInfoLine(item, line, sizeof(line));
      u8g2.drawUTF8(0, 12 * (i + 1), line);
    }
  } while (u8g2.nextPage());
}

void showBatInfo() {
  if (!b_hasFuelGauge || s_showBatInfoBusy) {
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
