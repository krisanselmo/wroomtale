#include "ButtonScheme.h"
#include <unity.h>

void setUp() {}
void tearDown() {}

void test_common_gestures() {
	TEST_ASSERT_TRUE(ButtonScheme::actionFor(BtnEvent::PlayShort) == BtnAction::PlayPause);
	TEST_ASSERT_TRUE(ButtonScheme::actionFor(BtnEvent::PlayLong) == BtnAction::Stop);
	// A ramp must not beep at every step.
	TEST_ASSERT_TRUE(ButtonScheme::actionFor(BtnEvent::NextRepeat) == BtnAction::VolUpSilent);
	TEST_ASSERT_TRUE(ButtonScheme::actionFor(BtnEvent::PrevRepeat) == BtnAction::VolDownSilent);
	TEST_ASSERT_TRUE(ButtonScheme::actionFor(BtnEvent::None) == BtnAction::None);
}

#if BTN_SCHEME == 1
void test_scheme() {
	TEST_ASSERT_TRUE(ButtonScheme::actionFor(BtnEvent::PrevShort) == BtnAction::VolDown);
	TEST_ASSERT_TRUE(ButtonScheme::actionFor(BtnEvent::NextShort) == BtnAction::VolUp);
	TEST_ASSERT_TRUE(ButtonScheme::actionFor(BtnEvent::PlayDouble) == BtnAction::NextTrack);
	TEST_ASSERT_TRUE(ButtonScheme::usesDoubleClick());
}
#else
void test_scheme() {
	TEST_ASSERT_TRUE(ButtonScheme::actionFor(BtnEvent::PrevShort) == BtnAction::PrevTrack);
	TEST_ASSERT_TRUE(ButtonScheme::actionFor(BtnEvent::NextShort) == BtnAction::NextTrack);
	TEST_ASSERT_TRUE(ButtonScheme::actionFor(BtnEvent::NextLong) == BtnAction::VolUp);
	TEST_ASSERT_TRUE(ButtonScheme::actionFor(BtnEvent::PlayDouble) == BtnAction::None);
	// No double bound: the single click must answer at once.
	TEST_ASSERT_FALSE(ButtonScheme::usesDoubleClick());
}
#endif

int main() {
	UNITY_BEGIN();
	RUN_TEST(test_common_gestures);
	RUN_TEST(test_scheme);
	return UNITY_END();
}
