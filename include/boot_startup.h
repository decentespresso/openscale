#ifndef BOOT_STARTUP_H
#define BOOT_STARTUP_H

#if defined(V8_1) && defined(ADS1232ADC) && !defined(ACC_MPU6050) && !defined(ACC_BMA400)
#define HDS_OVERLAP_GAUGE_STARTUP 1
#else
#define HDS_OVERLAP_GAUGE_STARTUP 0
#endif

void initializeBootDisplay() {
  u8g2.begin();
  b_u8g2Sleep = false;
  u8g2.enableUTF8Print();
  u8g2.setContrast(255);
  u8g2.setFont(FONT_M);
}

void renderBootLogo() {
  b_screenFlipped = storageGetBool(KEY_SCREEN_FLIP, false);
  if (b_screenFlipped)
    u8g2.setDisplayRotation(U8G2_R0);
  else
    u8g2.setDisplayRotation(U8G2_R2);

  invalidateEnergyOledFrame();
  u8g2.firstPage();
  do {
    u8g2.setFont(u8g2_font_logisoso22_tf);
    u8g2.drawStr(AC("Half"), 26, "Half");
    u8g2.drawBox(4, LCDHeight / 2, LCDWidth - 4 * 2, 2);
    u8g2.drawStr(AC("Decent"), LCDHeight - 2, "Decent");
  } while (u8g2.nextPage());
}

#if HDS_OVERLAP_GAUGE_STARTUP
void stabilizeScaleWithFuelGauge() {
  const unsigned long stabilizationStartedAt = millis();
  bool detectionComplete = false;
  while (!detectionComplete || millis() - stabilizationStartedAt < 900) {
    scale.update();
    if (!detectionComplete && fuelGaugePollDetection()) {
      detectionComplete = true;
    }
    yield();
  }
}
#endif

#endif
