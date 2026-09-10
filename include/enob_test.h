#ifndef ENOB_TEST_H
#define ENOB_TEST_H

#include <Arduino.h>

// ENOB (effective resolution) test — samples raw 24-bit ADC values and
// prints noise, resolution and noise-free-bit statistics.
// Uses polling mode (scale.update()) so it does NOT conflict with the
// AsyncTCP task or the existing sampling model.

static void runEnobTest(const char *mode, int sampleCount = 100) {
  Serial.println();
  Serial.println("=============================================");
  Serial.print("=== ENOB (Effective Number of Bits) Test: ");
  Serial.print(mode);
  Serial.println(" ===");
  Serial.println("Hardware-paced, SPS measured below");
  Serial.println("=============================================");

  // Allocate on heap — ESP32 has plenty of PSRAM
  long *rawData = (long *)malloc(sampleCount * sizeof(long));
  if (rawData == NULL) {
    Serial.println("ERROR: Failed to allocate sample buffer");
    return;
  }

  unsigned long t_start = millis();
  int collected = 0;

  while (collected < sampleCount) {
    // scale.update() returns 1 when a fresh conversion was read; the ADC
    // paces the sampling, so 10 SPS and 80 SPS hardware both work as-is.
    if (scale.update()) {
      rawData[collected] = scale.getDebugInfo().rawValue;
      collected++;
    }
    delay(1);  // yield to other tasks; sampling is gated by DOUT readiness
  }

  unsigned long t_end = millis();
  float elapsedSec = (t_end - t_start) / 1000.0f;

  // --- Statistics ---

  // Sum + average
  int64_t sum = 0;
  for (int i = 0; i < sampleCount; i++) {
    sum += rawData[i];
  }
  float avg = (float)((double)sum / sampleCount);

  // Variance + standard deviation
  double variance = 0.0;
  for (int i = 0; i < sampleCount; i++) {
    double diff = (double)rawData[i] - (double)avg;
    variance += diff * diff;
  }
  variance /= sampleCount;
  float stdDev = (float)sqrt(variance);

  // Peak-to-peak (max - min)
  long minVal = rawData[0];
  long maxVal = rawData[0];
  for (int i = 1; i < sampleCount; i++) {
    if (rawData[i] < minVal) minVal = rawData[i];
    if (rawData[i] > maxVal) maxVal = rawData[i];
  }
  long drift = maxVal - minVal;

  // Effective resolution: noise-free bits = log2(full_scale / p-p noise)
  // ADS1232 full scale with 2.5V REF = 2.5V / 128 (PGA=128) = 19.53125 mV
  // = 2^23 LSBs = 8,388,608 codes (±4,194,304 signed range)
  float noiseFreeBits = log2f((float)(0x800000L) / (float)max(drift, 1L));

  // --- Report ---
  Serial.println();
  Serial.println("--- Raw Readings (first 20) ---");
  for (int i = 0; i < min(sampleCount, 20); i++) {
    Serial.print("  [");
    Serial.print(i);
    Serial.print("] ");
    Serial.println(rawData[i]);
  }
  if (sampleCount > 20) {
    Serial.print("  ... (+");
    Serial.print(sampleCount - 20);
    Serial.println(" more)");
  }

  Serial.println();
  Serial.println("--- Statistics ---");
  Serial.print("Elapsed time:        ");
  Serial.print(elapsedSec, 2);
  Serial.println(" s");
  Serial.print("Effective SPS:       ");
  Serial.print((float)sampleCount / elapsedSec, 2);
  Serial.println(" Hz");

  Serial.print("Average (raw):       ");
  Serial.println(avg, 2);

  Serial.print("Std Dev (raw LSBs):  ");
  Serial.print(stdDev, 2);
  Serial.print("  (");
  Serial.print(stdDev * 1000.0f, 2);
  Serial.println(" μLSBs)");

  Serial.print("Peak-to-peak drift:  ");
  Serial.print(drift);
  Serial.println(" LSBs");

  Serial.print("ENOB (Effective Number of Bits), p-p:  ");
  Serial.print(noiseFreeBits, 2);
  Serial.print("  (of 24, ");
  Serial.print(24.0f - noiseFreeBits, 2);
  Serial.println(" noise bits)");
  Serial.print("ENOB (Effective Number of Bits), RMS:  ");
  Serial.print(24.0f - log2f((stdDev > 0.0f ? stdDev : 1.0f) * 6.6f), 2);
  Serial.println("  (6.6 sigma p-p equivalent)");

  // Resolution in grams using current calibration factor
  {
    float calFactor = scale.getCalFactor();
    Serial.print("Calibration factor:  ");
    Serial.print(calFactor, 4);
    Serial.print("  (raw LSBs per gram)");
    if (calFactor == 1000.0f) {
      Serial.print(" [DEFAULT — uncalibrated]");
    }
    Serial.println();
    Serial.println("Note: the mg resolutions below are derived from the current");
    Serial.println("      calibration factor; with a wrong or missing calibration");
    Serial.println("      they do not reflect the real weighing resolution.");

    if (calFactor != 0.0f) {
      float cf = fabsf(calFactor);
      float p2pGrams = (float)drift / cf;
      float rmsGrams = stdDev / cf;

      Serial.print("Resolution (p-p):    ");
      if (p2pGrams >= 1.0f) {
        Serial.print(p2pGrams, 3);
        Serial.println(" g");
      } else if (p2pGrams >= 0.001f) {
        Serial.print(p2pGrams * 1000.0f, 3);
        Serial.println(" mg");
      } else {
        Serial.print(p2pGrams * 1000000.0f, 2);
        Serial.println(" µg");
      }
      Serial.print("  (= ");
      Serial.print(drift);
      Serial.println(" LSBs / calFactor)");

      Serial.print("Resolution (RMS):    ");
      if (rmsGrams >= 1.0f) {
        Serial.print(rmsGrams, 3);
        Serial.println(" g");
      } else if (rmsGrams >= 0.001f) {
        Serial.print(rmsGrams * 1000.0f, 3);
        Serial.println(" mg");
      } else {
        Serial.print(rmsGrams * 1000000.0f, 2);
        Serial.println(" µg");
      }
      Serial.print("  (= ");
      Serial.print(stdDev, 2);
      Serial.println(" LSBs / calFactor)");
    }
  }

  Serial.print("Min raw:             ");
  Serial.println(minVal);
  Serial.print("Max raw:             ");
  Serial.println(maxVal);
  Serial.print("Total samples:       ");
  Serial.println(sampleCount);

  // Noise-quality assessment, graded by the RMS resolution in mg (the
  // figure that matters for weighing) rather than raw LSB counts, so the
  // verdict is comparable across units with different calibration factors.
  Serial.println();
  Serial.print("Assessment: ");
  const float calFactorAbs = fabsf(scale.getCalFactor());
  const float rmsMg = calFactorAbs > 0.0f ? (stdDev / calFactorAbs) * 1000.0f : -1.0f;
  if (rmsMg < 0.0f) {
    Serial.println("UNKNOWN — no valid calibration factor.");
  } else if (rmsMg <= 50.0f) {
    Serial.println("GOOD — RMS resolution within 50 mg; low enough for stable, repeatable weighing.");
  } else if (rmsMg <= 100.0f) {
    Serial.println("ACCEPTABLE — RMS resolution within 100 mg; usable, but worth reviewing the circuit (decoupling, layout).");
  } else if (rmsMg <= 200.0f) {
    Serial.println("FAIR — RMS resolution within 200 mg; above average consumer-scale level, yet below this front-end's potential — further improvement is possible.");
  } else {
    Serial.println("POOR — RMS resolution above 200 mg; check the analog front-end: grounding, supply ripple, reference and wiring.");
  }

  Serial.println("=============================================");
  Serial.println();

  free(rawData);
}

#endif  // NOISE_TEST_H
