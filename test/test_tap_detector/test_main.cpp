#include <limits.h>
#include <initializer_list>
#include <unity.h>
#include "tap_detector.h"

void setUp() {}
void tearDown() {}

struct Sample {
  unsigned long at;
  float weight;
  TapEvent event = TapEvent::None;
};

static void trace(TapDetector &detector, std::initializer_list<Sample> samples,
                  unsigned long offset = 0, float load = 0.0f) {
  for (const Sample &sample : samples) {
    TEST_ASSERT_EQUAL_MESSAGE(sample.event, detector.tick(offset + sample.at, load + sample.weight),
                              "Unexpected gesture in signal trace");
  }
}

static void establishBaseline(TapDetector &detector, unsigned long offset = 0,
                              float load = 0.0f) {
  detector.reset(offset, load);
  trace(detector, {{100, 0}, {200, 0}, {300, 0}, {400, 0}, {500, 0}}, offset, load);
}

static void firstPeak(TapDetector &detector) {
  trace(detector, {{550, 45}, {575, 75}, {600, 20}, {625, 8}});
}

void testFastDoubleWithoutBaselineRecovery() {
  for (unsigned long spacing : {120UL, 150UL, 200UL}) {
    TapDetector detector;
    establishBaseline(detector);
    firstPeak(detector);
    trace(detector, {{550 + spacing, 18}, {575 + spacing, 60},
                    {600 + spacing, 20}, {625 + spacing, 5},
                    {1025 + spacing, 5}, {1026 + spacing, 5, TapEvent::Double},
                    {1300, 5}});
  }
}

void testFastTripleWithoutBaselineRecovery() {
  TapDetector detector;
  establishBaseline(detector);
  trace(detector, {{550, 45}, {580, 75}, {610, 28}, {640, 8},
                  {670, 18}, {700, 60}, {730, 25}, {760, 5},
                  {790, 18}, {820, 65}, {850, 25}, {880, 8, TapEvent::Triple},
                  {1000, 8}, {1300, 8}});
}

void testExampleSecondTapStartsAtFiveGrams() {
  TapDetector detector;
  establishBaseline(detector);
  trace(detector, {{550, 20}, {575, 65}, {600, 30}, {625, 10}, {650, 5},
                  {675, 18}, {700, 55}, {725, 25}, {750, 5},
                  {1151, 5, TapEvent::Double}});
}

void testHeldFingerFluctuationsAndLongContact() {
  TapDetector detector;
  establishBaseline(detector);
  trace(detector, {{550, 70}, {575, 66}, {600, 72}, {625, 68},
                  {650, 74}, {675, 67}, {700, 71}, {751, 71},
                  {850, 68}, {950, 72}, {1050, 67}, {1150, 71},
                  {1250, 66}, {1350, 70}, {1500, 0}, {1900, 0}});
}

void testRingingDoesNotCountEvenOutsideRefractoryPeriod() {
  for (unsigned long interval : {12UL, 25UL, 50UL, 100UL}) {
    TapDetector detector;
    establishBaseline(detector);
    trace(detector, {{550, 65}, {550 + interval, 25}, {550 + 2 * interval, -6},
                    {550 + 3 * interval, 12}, {550 + 4 * interval, 3},
                    {550 + 5 * interval, 0}, {1500, 0}});
  }
}

void testRingingBeforeGenuineSecondTap() {
  TapDetector detector;
  establishBaseline(detector);
  trace(detector, {{550, 65}, {575, 25}, {600, -6}, {625, 12},
                  {650, 3}, {675, 0}, {700, 30}, {725, 60}, {750, 15},
                  {775, 4}, {1151, 4, TapEvent::Double}});
}

void testRejectedReboundDoesNotReuseAnOlderDeeperValley() {
  TapDetector detector;
  establishBaseline(detector);
  trace(detector, {{550, 65}, {575, 25}, {600, -6}, {625, 12},
                  {650, 3}, {675, 18}, {700, 5}, {725, 0}, {1100, 0}});
}

void testSlowPressureModulation() {
  TapDetector detector;
  establishBaseline(detector);
  trace(detector, {{550, 20}, {650, 30}, {750, 40}, {850, 35},
                  {950, 45}, {1050, 38}, {1200, 20}, {1400, 40},
                  {1600, 20}, {1800, 40}, {2000, 0}, {2400, 0}});
}

void testObjectPlacementAndSettledLoad() {
  TapDetector detector;
  establishBaseline(detector);
  trace(detector, {{550, 45}, {575, 100}, {600, 80}, {625, 90},
                  {650, 84}, {675, 86}, {800, 85}, {1000, 85}, {1400, 85}});
}

void testFinalHeldPeakCancelsDoubleAndTriple() {
  for (bool third : {false, true}) {
    TapDetector detector;
    establishBaseline(detector);
    firstPeak(detector);
    if (third) {
      trace(detector, {{670, 40}, {695, 75}, {720, 20}, {745, 0}});
    }
    const unsigned long start = third ? 790 : 670;
    trace(detector, {{start, 100}, {start + 25, 50}, {start + 100, 50},
                    {start + 601, 50}, {start + 800, 0}, {start + 1200, 0}});
  }
}

void testSeparationBoundaries() {
  for (unsigned long separation : {49UL, 50UL, 51UL, 411UL, 412UL, 413UL}) {
    TapDetector detector;
    establishBaseline(detector);
    trace(detector, {{550, 70}, {562, 20}, {575, 8},
                    {550 + separation, 65}, {562 + separation, 20},
                    {575 + separation, 5}});
    const TapEvent expected = separation >= 50 && separation <= 412 ?
        TapEvent::Double : TapEvent::None;
    trace(detector, {{976 + separation, 5, expected}});
  }
}

void testVeryFastTripleAtMinimumSeparation() {
  TapDetector detector;
  establishBaseline(detector);
  trace(detector, {{550, 70}, {562, 20}, {575, 8},
                  {600, 65}, {612, 20}, {625, 5},
                  {650, 60}, {662, 20}, {675, 8, TapEvent::Triple},
                  {700, 65}, {725, 5}, {1200, 5}});
}

void testThirdStartingAtDeadlineSuppressesDouble() {
  TapDetector detector;
  establishBaseline(detector);
  trace(detector, {{550, 70}, {575, 10}, {700, 65}, {725, 8},
                  {1124, 8}, {1125, 30}, {1150, 60}, {1175, 15, TapEvent::Triple},
                  {1400, 0}});
}

void testPeakHeightBoundary() {
  for (float height : {9.9f, 10.0f, 10.1f}) {
    TapDetector detector;
    establishBaseline(detector);
    trace(detector, {{550, height}, {575, 0}, {700, height}, {725, 0}});
    trace(detector, {{1126, 0, height > 10.0f ? TapEvent::Double : TapEvent::None}});
  }
}

void testLightTripleAndMixedDurations() {
  TapDetector detector;
  establishBaseline(detector);
  trace(detector, {{550, 13}, {575, 1}, {850, 16}, {925, 10}, {1025, 1},
                  {1150, 12}, {1200, 3, TapEvent::Triple}, {1600, 0}});
}

void testAbsoluteDropBoundary() {
  for (float drop : {5.9f, 6.0f, 6.1f}) {
    TapDetector detector;
    establishBaseline(detector);
    trace(detector, {{550, 11}, {575, 0}, {700, 6.1f}, {725, 6.1f - drop}});
    trace(detector, {{1126, 6.1f - drop, drop >= 6.0f ? TapEvent::Double : TapEvent::None}});
  }
}

void testRelativeDropBoundary() {
  for (float drop : {69.9f, 70.0f, 70.1f}) {
    TapDetector detector;
    establishBaseline(detector);
    trace(detector, {{550, 100}, {575, 0}, {700, 100}, {725, 100 - drop}});
    trace(detector, {{1126, 100 - drop, drop >= 70.0f ? TapEvent::Double : TapEvent::None}});
  }
}

void testRelativeRepeatProminenceBoundary() {
  for (float height : {34.9f, 35.0f, 35.1f}) {
    TapDetector detector;
    establishBaseline(detector);
    trace(detector, {{550, 100}, {575, 0}, {700, height}, {725, 0}});
    trace(detector, {{1126, 0, height > 35.0f ? TapEvent::Double : TapEvent::None}});
  }
}

void testProminenceRemainsAnchoredToStrongestTap() {
  TapDetector detector;
  establishBaseline(detector);
  trace(detector, {{550, 100}, {575, 0}, {700, 40}, {725, 0},
                  {850, 20}, {875, 0}, {1126, 0, TapEvent::Double}});
}

void testDurationBoundary() {
  for (unsigned long duration : {599UL, 600UL, 601UL}) {
    TapDetector detector;
    establishBaseline(detector);
    trace(detector, {{550, 70}, {575, 0}, {700, 70}, {700 + duration, 0}});
    trace(detector, {{1101 + duration, 0, duration <= 600 ? TapEvent::Double : TapEvent::None}});
  }
}

void testSlopeBoundary() {
  for (float slope : {1.9f, 2.0f, 2.1f}) {
    TapDetector detector;
    establishBaseline(detector);
    trace(detector, {{550, slope}, {555, slope * 2}, {560, slope * 3},
                    {565, slope * 4}, {570, slope * 5}, {575, slope * 6},
                    {600, 0}, {700, 20}, {725, 0}});
    trace(detector, {{1126, 0, slope > 2 ? TapEvent::Double : TapEvent::None}});
  }
}

void testRequiresStableBaseline() {
  TapDetector detector;
  trace(detector, {{50, 70}, {75, 8}, {170, 65}, {195, 5},
                  {290, 60}, {315, 8}, {600, 8}});
}

void testResetCancelsGesture() {
  TapDetector detector;
  establishBaseline(detector);
  firstPeak(detector);
  trace(detector, {{670, 60}, {695, 5}});
  detector.reset(700, 5);
  trace(detector, {{1001, 5}});
}

void testBaselinePhaseAndNonzeroLoadDoNotChangeRecognition() {
  for (unsigned long offset = 0; offset < 100; offset += 10) {
    TapDetector detector;
    establishBaseline(detector, 0, 350);
    trace(detector, {{550, 45}, {575, 75}, {600, 20}, {625, 8},
                    {700, 18}, {725, 60}, {750, 15},
                    {1151, 5, TapEvent::Double}}, offset, 350);
  }
}

void testCachedSamplesDoNotCreateEdgesOrDelayRecognition() {
  TapDetector detector;
  establishBaseline(detector);
  for (unsigned long now = 550; now <= 1126; ++now) {
    const float weight = now < 575 ? 70 : now < 700 ? 8 : now < 725 ? 65 : 5;
    TEST_ASSERT_EQUAL(now == 1126 ? TapEvent::Double : TapEvent::None,
                      detector.tick(now, weight));
  }
}

void testUnsignedClockWrap() {
  TapDetector detector;
  const unsigned long offset = ULONG_MAX - 650;
  establishBaseline(detector, offset);
  trace(detector, {{550, 70}, {575, 8}, {700, 65}, {725, 5},
                  {850, 60}, {875, 8, TapEvent::Triple}, {1300, 0}}, offset);
}

void testInvalidSamplesCancelGesture() {
  for (float invalid : {NAN, INFINITY, -INFINITY}) {
    TapDetector detector;
    establishBaseline(detector);
    firstPeak(detector);
    trace(detector, {{650, invalid}, {700, 60}, {725, 5}, {1100, 5}});
  }
}

void testCapturedTenSpsDouble() {
  TapDetector detector;
  establishBaseline(detector, 0, -0.95f);
  trace(detector, {{715, -0.978f}, {828, -0.955f}, {939, 0.090f},
                  {1033, 21.978f}, {1129, 30.091f}, {1226, 2.472f},
                  {1337, -0.278f}, {1434, 15.724f}, {1529, 24.785f},
                  {1624, 2.376f}, {1735, -0.993f}, {1846, -0.998f},
                  {1942, -0.987f}, {2036, -0.986f, TapEvent::Double}});
}

void testCapturedTenSpsTriple() {
  TapDetector detector;
  establishBaseline(detector, 0, -1.261f);
  trace(detector, {{729, -1.217f}, {824, -1.253f}, {919, -1.261f},
                  {1031, 4.058f}, {1143, 16.239f}, {1239, 6.215f},
                  {1333, 13.857f}, {1429, 5.446f}, {1539, 16.662f},
                  {1595, 10.325f}, {1707, -0.770f, TapEvent::Triple},
                  {1802, -1.070f}, {1897, -1.069f}, {2008, -1.068f}});
}

void testCapturedHeldPressure() {
  TapDetector detector;
  establishBaseline(detector, 0, -1.0f);
  trace(detector, {{1121, 20.940f}, {1218, 46.181f}, {1330, 45.662f},
                  {1426, 38.189f}, {1522, 35.090f}, {1633, 34.699f},
                  {1728, 32.814f}, {1824, 31.811f}, {1919, 20.030f},
                  {2031, 2.021f}, {2600, 0}});
}

void testOriginalSlowMixedDurations() {
  TapDetector detector;
  establishBaseline(detector);
  trace(detector, {{550, 100}, {750, 50}, {800, 0},
                  {1100, 100}, {1150, 0}, {1450, 100}, {1700, 50},
                  {1900, 0, TapEvent::Triple}});
}

void testOriginalSlowSecondFinishesAfterDeadline() {
  TapDetector detector;
  establishBaseline(detector);
  trace(detector, {{550, 100}, {600, 0}, {950, 100}, {1050, 100},
                  {1150, 0}, {1550, 0}, {1551, 0, TapEvent::Double}});
}

void testIntermediateDropBoundary() {
  for (float drop : {44.9f, 45.0f, 45.1f}) {
    TapDetector detector;
    establishBaseline(detector);
    trace(detector, {{550, 100}, {575, 100 - drop}, {700, 100}, {725, 0}});
    trace(detector, {{1126, 0, drop >= 45.0f ? TapEvent::Double : TapEvent::None}});
  }
}

void testSlowPartialReleaseWithCachedSamples() {
  TapDetector detector;
  establishBaseline(detector);
  for (unsigned long now = 550; now <= 1701; ++now) {
    const float weight = now < 575 ? 70 : now < 700 ? 0 :
        now < 725 ? 100 : now < 1300 ? 50 : 0;
    TEST_ASSERT_EQUAL(now == 1701 ? TapEvent::Double : TapEvent::None,
                      detector.tick(now, weight));
  }
}

void testCapturedFastDoubleWithHighValley() {
  TapDetector detector;
  establishBaseline(detector, 0, -0.163f);
  trace(detector, {{610, 10.108f}, {751, 36.089f}, {829, 18.525f},
                  {907, 25.967f}, {1068, 5.605f}, {1143, -0.002f},
                  {1470, 0.044f, TapEvent::Double}});
}

void testCapturedFastLightDouble() {
  TapDetector detector;
  establishBaseline(detector, 0, -0.017f);
  trace(detector, {{522, 11.648f}, {594, 10.298f}, {673, 4.468f},
                  {831, 17.321f}, {911, 5.157f}, {990, -0.002f},
                  {1312, 0.046f, TapEvent::Double}});
}

void testCapturedUnresolvedDoubleRemainsRejected() {
  TapDetector detector;
  establishBaseline(detector, 0, 0.008f);
  trace(detector, {{858, 9.583f}, {935, 30.545f}, {1012, 14.709f},
                  {1091, 15.346f}, {1263, 2.040f}, {1341, 0.063f}, {1900, 0}});
}

void testLocalRepeatRiseBoundary() {
  for (float height : {5.9f, 6.0f, 6.1f}) {
    TapDetector detector;
    establishBaseline(detector);
    trace(detector, {{550, 11}, {575, 0}, {700, height}, {725, 0}});
    trace(detector, {{1126, 0, height > 6.0f ? TapEvent::Double : TapEvent::None}});
  }
}

int main(int argc, char **argv) {
  UNITY_BEGIN();
  RUN_TEST(testFastDoubleWithoutBaselineRecovery);
  RUN_TEST(testFastTripleWithoutBaselineRecovery);
  RUN_TEST(testExampleSecondTapStartsAtFiveGrams);
  RUN_TEST(testHeldFingerFluctuationsAndLongContact);
  RUN_TEST(testRingingDoesNotCountEvenOutsideRefractoryPeriod);
  RUN_TEST(testRingingBeforeGenuineSecondTap);
  RUN_TEST(testRejectedReboundDoesNotReuseAnOlderDeeperValley);
  RUN_TEST(testSlowPressureModulation);
  RUN_TEST(testObjectPlacementAndSettledLoad);
  RUN_TEST(testFinalHeldPeakCancelsDoubleAndTriple);
  RUN_TEST(testSeparationBoundaries);
  RUN_TEST(testVeryFastTripleAtMinimumSeparation);
  RUN_TEST(testThirdStartingAtDeadlineSuppressesDouble);
  RUN_TEST(testPeakHeightBoundary);
  RUN_TEST(testLightTripleAndMixedDurations);
  RUN_TEST(testAbsoluteDropBoundary);
  RUN_TEST(testRelativeDropBoundary);
  RUN_TEST(testRelativeRepeatProminenceBoundary);
  RUN_TEST(testProminenceRemainsAnchoredToStrongestTap);
  RUN_TEST(testDurationBoundary);
  RUN_TEST(testSlopeBoundary);
  RUN_TEST(testRequiresStableBaseline);
  RUN_TEST(testResetCancelsGesture);
  RUN_TEST(testBaselinePhaseAndNonzeroLoadDoNotChangeRecognition);
  RUN_TEST(testCachedSamplesDoNotCreateEdgesOrDelayRecognition);
  RUN_TEST(testUnsignedClockWrap);
  RUN_TEST(testInvalidSamplesCancelGesture);
  RUN_TEST(testCapturedTenSpsDouble);
  RUN_TEST(testCapturedTenSpsTriple);
  RUN_TEST(testCapturedHeldPressure);
  RUN_TEST(testOriginalSlowMixedDurations);
  RUN_TEST(testOriginalSlowSecondFinishesAfterDeadline);
  RUN_TEST(testIntermediateDropBoundary);
  RUN_TEST(testSlowPartialReleaseWithCachedSamples);
  RUN_TEST(testCapturedFastDoubleWithHighValley);
  RUN_TEST(testCapturedFastLightDouble);
  RUN_TEST(testCapturedUnresolvedDoubleRemainsRejected);
  RUN_TEST(testLocalRepeatRiseBoundary);
  return UNITY_END();
}
