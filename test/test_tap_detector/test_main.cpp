#include <unity.h>
#include "tap_detector.h"

void setUp() {}
void tearDown() {}

static void establishBaseline(TapDetector &detector) {
  for (unsigned long now = 100; now <= 500; now += 100) {
    TEST_ASSERT_EQUAL(TapEvent::None, detector.tick(now, 0.0f));
  }
}

static TapEvent tap(TapDetector &detector, unsigned long riseMs) {
  TEST_ASSERT_EQUAL(TapEvent::None, detector.tick(riseMs, 100.0f));
  return detector.tick(riseMs + 50, 0.0f);
}

void testRequiresStableBaselineAndRecovery() {
  TapDetector detector;
  TEST_ASSERT_EQUAL(TapEvent::None, tap(detector, 100));
  TEST_ASSERT_EQUAL(TapEvent::None, tap(detector, 250));

  detector.reset(0, 0.0f);
  establishBaseline(detector);
  TEST_ASSERT_EQUAL(TapEvent::None, detector.tick(550, 100.0f));
  TEST_ASSERT_EQUAL(TapEvent::None, detector.tick(600, 95.0f));
  TEST_ASSERT_EQUAL(TapEvent::None, detector.tick(650, 101.0f));
  TEST_ASSERT_EQUAL(TapEvent::None, detector.tick(700, 98.0f));
  TEST_ASSERT_EQUAL(TapEvent::None, detector.tick(1100, 98.0f));
}

void testResetCancelsMenuExitGesture() {
  TapDetector detector;
  establishBaseline(detector);
  TEST_ASSERT_EQUAL(TapEvent::None, tap(detector, 550));
  TEST_ASSERT_EQUAL(TapEvent::None, detector.tick(650, 0.0f));
  TEST_ASSERT_EQUAL(TapEvent::None, tap(detector, 700));
  detector.reset(800, 0.0f);
  TEST_ASSERT_EQUAL(TapEvent::None, detector.tick(1200, 0.0f));
}

void testDoubleTapRequiresRelease() {
  TapDetector detector;
  establishBaseline(detector);
  TEST_ASSERT_EQUAL(TapEvent::None, tap(detector, 550));
  TEST_ASSERT_EQUAL(TapEvent::None, detector.tick(650, 0.0f));
  TEST_ASSERT_EQUAL(TapEvent::None, tap(detector, 700));
  TEST_ASSERT_EQUAL(TapEvent::Double, detector.tick(1150, 0.0f));
}

void testUnreleasedFinalPeakDoesNotCompleteDoubleOrTriple() {
  TapDetector detector;
  establishBaseline(detector);
  TEST_ASSERT_EQUAL(TapEvent::None, tap(detector, 550));
  TEST_ASSERT_EQUAL(TapEvent::None, detector.tick(650, 0.0f));
  TEST_ASSERT_EQUAL(TapEvent::None, detector.tick(700, 100.0f));
  TEST_ASSERT_EQUAL(TapEvent::None, detector.tick(750, 50.0f));
  TEST_ASSERT_EQUAL(TapEvent::None, detector.tick(1150, 50.0f));

  detector.reset(0, 0.0f);
  establishBaseline(detector);
  TEST_ASSERT_EQUAL(TapEvent::None, tap(detector, 550));
  TEST_ASSERT_EQUAL(TapEvent::None, detector.tick(650, 0.0f));
  TEST_ASSERT_EQUAL(TapEvent::None, tap(detector, 700));
  TEST_ASSERT_EQUAL(TapEvent::None, detector.tick(800, 0.0f));
  TEST_ASSERT_EQUAL(TapEvent::None, detector.tick(850, 100.0f));
  TEST_ASSERT_EQUAL(TapEvent::None, detector.tick(900, 50.0f));
  TEST_ASSERT_EQUAL(TapEvent::None, detector.tick(1300, 50.0f));
}

void testFourthPeakCannotReplaceTriple() {
  TapDetector detector;
  establishBaseline(detector);
  TEST_ASSERT_EQUAL(TapEvent::None, tap(detector, 550));
  TEST_ASSERT_EQUAL(TapEvent::None, detector.tick(650, 0.0f));
  TEST_ASSERT_EQUAL(TapEvent::None, tap(detector, 700));
  TEST_ASSERT_EQUAL(TapEvent::None, detector.tick(800, 0.0f));
  TEST_ASSERT_EQUAL(TapEvent::Triple, tap(detector, 850));
  TEST_ASSERT_EQUAL(TapEvent::None, detector.tick(950, 0.0f));
  TEST_ASSERT_EQUAL(TapEvent::None, tap(detector, 1000));
  TEST_ASSERT_EQUAL(TapEvent::None, detector.tick(1400, 0.0f));
}

int main(int argc, char **argv) {
  UNITY_BEGIN();
  RUN_TEST(testRequiresStableBaselineAndRecovery);
  RUN_TEST(testResetCancelsMenuExitGesture);
  RUN_TEST(testDoubleTapRequiresRelease);
  RUN_TEST(testUnreleasedFinalPeakDoesNotCompleteDoubleOrTriple);
  RUN_TEST(testFourthPeakCannotReplaceTriple);
  return UNITY_END();
}
