// Native Unity tests for include/util/pure.h.
//
// These cover the pure helpers extracted from the firmware (compass8,
// uvCode, to12h, parseIsoHourMinute, isQuietHour, lipoPercent). They run
// under the [env:native] PlatformIO env — no Arduino/ESP32 headers are
// pulled in, so nothing here can touch hardware.

#include <unity.h>
#include <cstring>          // strcmp
#include "util/pure.h"

// ---- helpers ----------------------------------------------------------------

static void assert_cstr_eq(const char *expected, const char *actual) {
  TEST_ASSERT_EQUAL_STRING(expected, actual);
}

// ---- compass8 ---------------------------------------------------------------

static void test_compass8_N_at_0(void)         { assert_cstr_eq("N",  pure::compass8(0)); }
static void test_compass8_N_at_22(void)        { assert_cstr_eq("N",  pure::compass8(22)); }
static void test_compass8_NE_at_23(void)       { assert_cstr_eq("NE", pure::compass8(23)); }
static void test_compass8_NE_at_45(void)       { assert_cstr_eq("NE", pure::compass8(45)); }
static void test_compass8_NE_at_67(void)       { assert_cstr_eq("NE", pure::compass8(67)); }
static void test_compass8_E_at_68(void)        { assert_cstr_eq("E",  pure::compass8(68)); }
static void test_compass8_E_at_90(void)        { assert_cstr_eq("E",  pure::compass8(90)); }
static void test_compass8_SE_at_135(void)      { assert_cstr_eq("SE", pure::compass8(135)); }
static void test_compass8_S_at_180(void)       { assert_cstr_eq("S",  pure::compass8(180)); }
static void test_compass8_SW_at_225(void)      { assert_cstr_eq("SW", pure::compass8(225)); }
static void test_compass8_W_at_270(void)       { assert_cstr_eq("W",  pure::compass8(270)); }
static void test_compass8_NW_at_315(void)      { assert_cstr_eq("NW", pure::compass8(315)); }
static void test_compass8_NW_at_337(void)      { assert_cstr_eq("NW", pure::compass8(337)); }
static void test_compass8_N_at_338(void)       { assert_cstr_eq("N",  pure::compass8(338)); }
static void test_compass8_N_at_360(void)       { assert_cstr_eq("N",  pure::compass8(360)); }
static void test_compass8_NW_at_neg45(void)    { assert_cstr_eq("NW", pure::compass8(-45)); }
static void test_compass8_N_at_neg1(void)      { assert_cstr_eq("N",  pure::compass8(-1)); }

// ---- uvCode -----------------------------------------------------------------

static void test_uvCode_lo_at_0(void)          { assert_cstr_eq("lo", pure::uvCode(0.0f)); }
static void test_uvCode_lo_at_2_9(void)        { assert_cstr_eq("lo", pure::uvCode(2.9f)); }
static void test_uvCode_md_at_3_0(void)        { assert_cstr_eq("md", pure::uvCode(3.0f)); }
static void test_uvCode_md_at_5_9(void)        { assert_cstr_eq("md", pure::uvCode(5.9f)); }
static void test_uvCode_hi_at_6_0(void)        { assert_cstr_eq("hi", pure::uvCode(6.0f)); }
static void test_uvCode_hi_at_7_9(void)        { assert_cstr_eq("hi", pure::uvCode(7.9f)); }
static void test_uvCode_vh_at_8_0(void)        { assert_cstr_eq("vh", pure::uvCode(8.0f)); }
static void test_uvCode_vh_at_10_9(void)       { assert_cstr_eq("vh", pure::uvCode(10.9f)); }
static void test_uvCode_ex_at_11_0(void)       { assert_cstr_eq("ex", pure::uvCode(11.0f)); }
static void test_uvCode_ex_at_15(void)         { assert_cstr_eq("ex", pure::uvCode(15.0f)); }

// ---- to12h ------------------------------------------------------------------
//
// Each case asserts BOTH the returned 1..12 value AND the am/pm flag.

static void test_to12h_0_is_12am(void) {
  bool pm = true;
  TEST_ASSERT_EQUAL(12, pure::to12h(0, pm));
  TEST_ASSERT_FALSE(pm);
}
static void test_to12h_1_is_1am(void) {
  bool pm = true;
  TEST_ASSERT_EQUAL(1, pure::to12h(1, pm));
  TEST_ASSERT_FALSE(pm);
}
static void test_to12h_11_is_11am(void) {
  bool pm = true;
  TEST_ASSERT_EQUAL(11, pure::to12h(11, pm));
  TEST_ASSERT_FALSE(pm);
}
static void test_to12h_12_is_12pm(void) {
  bool pm = false;
  TEST_ASSERT_EQUAL(12, pure::to12h(12, pm));
  TEST_ASSERT_TRUE(pm);
}
static void test_to12h_13_is_1pm(void) {
  bool pm = false;
  TEST_ASSERT_EQUAL(1, pure::to12h(13, pm));
  TEST_ASSERT_TRUE(pm);
}
static void test_to12h_23_is_11pm(void) {
  bool pm = false;
  TEST_ASSERT_EQUAL(11, pure::to12h(23, pm));
  TEST_ASSERT_TRUE(pm);
}

// ---- parseIsoHourMinute -----------------------------------------------------

static void test_parseIso_13_04(void) {
  int h = -1, m = -1;
  const char *s = "2026-05-22T13:04";
  TEST_ASSERT_TRUE(pure::parseIsoHourMinute(s, std::strlen(s), h, m));
  TEST_ASSERT_EQUAL(13, h);
  TEST_ASSERT_EQUAL(4, m);
}
static void test_parseIso_00_00(void) {
  int h = -1, m = -1;
  const char *s = "2026-05-22T00:00";
  TEST_ASSERT_TRUE(pure::parseIsoHourMinute(s, std::strlen(s), h, m));
  TEST_ASSERT_EQUAL(0, h);
  TEST_ASSERT_EQUAL(0, m);
}
static void test_parseIso_tooShort_returnsFalse(void) {
  int h = -1, m = -1;
  const char *s = "2026-05-22T13:0";   // len 15
  TEST_ASSERT_EQUAL(15, (int)std::strlen(s));
  TEST_ASSERT_FALSE(pure::parseIsoHourMinute(s, std::strlen(s), h, m));
}

// ---- isQuietHour ------------------------------------------------------------

// start=0, end=6: plain non-wrapping range [0, 6).
static void test_quiet_0_6_at_0(void)  { TEST_ASSERT_TRUE(pure::isQuietHour(0, 0, 6)); }
static void test_quiet_0_6_at_5(void)  { TEST_ASSERT_TRUE(pure::isQuietHour(5, 0, 6)); }
static void test_quiet_0_6_at_6(void)  { TEST_ASSERT_FALSE(pure::isQuietHour(6, 0, 6)); }
static void test_quiet_0_6_at_12(void) { TEST_ASSERT_FALSE(pure::isQuietHour(12, 0, 6)); }

// start=22, end=6: wraps midnight.
static void test_quiet_22_6_at_22(void) { TEST_ASSERT_TRUE(pure::isQuietHour(22, 22, 6)); }
static void test_quiet_22_6_at_23(void) { TEST_ASSERT_TRUE(pure::isQuietHour(23, 22, 6)); }
static void test_quiet_22_6_at_0(void)  { TEST_ASSERT_TRUE(pure::isQuietHour(0, 22, 6)); }
static void test_quiet_22_6_at_5(void)  { TEST_ASSERT_TRUE(pure::isQuietHour(5, 22, 6)); }
static void test_quiet_22_6_at_6(void)  { TEST_ASSERT_FALSE(pure::isQuietHour(6, 22, 6)); }
static void test_quiet_22_6_at_12(void) { TEST_ASSERT_FALSE(pure::isQuietHour(12, 22, 6)); }
static void test_quiet_22_6_at_21(void) { TEST_ASSERT_FALSE(pure::isQuietHour(21, 22, 6)); }

// start == end: feature disabled, always false.
static void test_quiet_disabled_at_0(void)  { TEST_ASSERT_FALSE(pure::isQuietHour(0, 7, 7)); }
static void test_quiet_disabled_at_5(void)  { TEST_ASSERT_FALSE(pure::isQuietHour(5, 7, 7)); }
static void test_quiet_disabled_at_12(void) { TEST_ASSERT_FALSE(pure::isQuietHour(12, 7, 7)); }

// ---- lipoPercent ------------------------------------------------------------
//
// Breakpoints matched verbatim against the extracted util/pure.h curve:
//   4200->100, 4100->95, 4000->85, 3900->75, 3800->55, 3700->35,
//   3600->20, 3400->5, 3300->0; below 3300 clamps to 0.

static void test_lipo_0_mv(void)      { TEST_ASSERT_EQUAL(0,   pure::lipoPercent(0)); }
static void test_lipo_3299_mv(void)   { TEST_ASSERT_EQUAL(0,   pure::lipoPercent(3299)); }
static void test_lipo_3300_mv(void)   { TEST_ASSERT_EQUAL(0,   pure::lipoPercent(3300)); }
static void test_lipo_3400_mv(void)   { TEST_ASSERT_EQUAL(5,   pure::lipoPercent(3400)); }
static void test_lipo_3600_mv(void)   { TEST_ASSERT_EQUAL(20,  pure::lipoPercent(3600)); }
static void test_lipo_3700_mv(void)   { TEST_ASSERT_EQUAL(35,  pure::lipoPercent(3700)); }
static void test_lipo_3800_mv(void)   { TEST_ASSERT_EQUAL(55,  pure::lipoPercent(3800)); }
static void test_lipo_3900_mv(void)   { TEST_ASSERT_EQUAL(75,  pure::lipoPercent(3900)); }
static void test_lipo_4000_mv(void)   { TEST_ASSERT_EQUAL(85,  pure::lipoPercent(4000)); }
static void test_lipo_4100_mv(void)   { TEST_ASSERT_EQUAL(95,  pure::lipoPercent(4100)); }
static void test_lipo_4200_mv(void)   { TEST_ASSERT_EQUAL(100, pure::lipoPercent(4200)); }
static void test_lipo_4300_clamp(void){ TEST_ASSERT_EQUAL(100, pure::lipoPercent(4300)); }

// ---- runner -----------------------------------------------------------------

int main(void) {
  UNITY_BEGIN();

  // compass8
  RUN_TEST(test_compass8_N_at_0);
  RUN_TEST(test_compass8_N_at_22);
  RUN_TEST(test_compass8_NE_at_23);
  RUN_TEST(test_compass8_NE_at_45);
  RUN_TEST(test_compass8_NE_at_67);
  RUN_TEST(test_compass8_E_at_68);
  RUN_TEST(test_compass8_E_at_90);
  RUN_TEST(test_compass8_SE_at_135);
  RUN_TEST(test_compass8_S_at_180);
  RUN_TEST(test_compass8_SW_at_225);
  RUN_TEST(test_compass8_W_at_270);
  RUN_TEST(test_compass8_NW_at_315);
  RUN_TEST(test_compass8_NW_at_337);
  RUN_TEST(test_compass8_N_at_338);
  RUN_TEST(test_compass8_N_at_360);
  RUN_TEST(test_compass8_NW_at_neg45);
  RUN_TEST(test_compass8_N_at_neg1);

  // uvCode
  RUN_TEST(test_uvCode_lo_at_0);
  RUN_TEST(test_uvCode_lo_at_2_9);
  RUN_TEST(test_uvCode_md_at_3_0);
  RUN_TEST(test_uvCode_md_at_5_9);
  RUN_TEST(test_uvCode_hi_at_6_0);
  RUN_TEST(test_uvCode_hi_at_7_9);
  RUN_TEST(test_uvCode_vh_at_8_0);
  RUN_TEST(test_uvCode_vh_at_10_9);
  RUN_TEST(test_uvCode_ex_at_11_0);
  RUN_TEST(test_uvCode_ex_at_15);

  // to12h
  RUN_TEST(test_to12h_0_is_12am);
  RUN_TEST(test_to12h_1_is_1am);
  RUN_TEST(test_to12h_11_is_11am);
  RUN_TEST(test_to12h_12_is_12pm);
  RUN_TEST(test_to12h_13_is_1pm);
  RUN_TEST(test_to12h_23_is_11pm);

  // parseIsoHourMinute
  RUN_TEST(test_parseIso_13_04);
  RUN_TEST(test_parseIso_00_00);
  RUN_TEST(test_parseIso_tooShort_returnsFalse);

  // isQuietHour
  RUN_TEST(test_quiet_0_6_at_0);
  RUN_TEST(test_quiet_0_6_at_5);
  RUN_TEST(test_quiet_0_6_at_6);
  RUN_TEST(test_quiet_0_6_at_12);
  RUN_TEST(test_quiet_22_6_at_22);
  RUN_TEST(test_quiet_22_6_at_23);
  RUN_TEST(test_quiet_22_6_at_0);
  RUN_TEST(test_quiet_22_6_at_5);
  RUN_TEST(test_quiet_22_6_at_6);
  RUN_TEST(test_quiet_22_6_at_12);
  RUN_TEST(test_quiet_22_6_at_21);
  RUN_TEST(test_quiet_disabled_at_0);
  RUN_TEST(test_quiet_disabled_at_5);
  RUN_TEST(test_quiet_disabled_at_12);

  // lipoPercent
  RUN_TEST(test_lipo_0_mv);
  RUN_TEST(test_lipo_3299_mv);
  RUN_TEST(test_lipo_3300_mv);
  RUN_TEST(test_lipo_3400_mv);
  RUN_TEST(test_lipo_3600_mv);
  RUN_TEST(test_lipo_3700_mv);
  RUN_TEST(test_lipo_3800_mv);
  RUN_TEST(test_lipo_3900_mv);
  RUN_TEST(test_lipo_4000_mv);
  RUN_TEST(test_lipo_4100_mv);
  RUN_TEST(test_lipo_4200_mv);
  RUN_TEST(test_lipo_4300_clamp);

  return UNITY_END();
}
