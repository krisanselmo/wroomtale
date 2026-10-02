#include "Tones.h"
#include <string.h>
#include <unity.h>

void setUp() {}
void tearDown() {}

void test_names_round_trip() {
	for (uint8_t i = 0; i < static_cast<uint8_t>(Sfx::Count); i++) {
		const Sfx id = static_cast<Sfx>(i);
		TEST_ASSERT_TRUE(strlen(sfxName(id)) > 0);
		TEST_ASSERT_EQUAL(i, static_cast<uint8_t>(sfxFromName(sfxName(id))));
	}
}

void test_unknown_name() {
	TEST_ASSERT_EQUAL(static_cast<uint8_t>(Sfx::Count), static_cast<uint8_t>(sfxFromName("nope")));
	TEST_ASSERT_EQUAL(static_cast<uint8_t>(Sfx::Count), static_cast<uint8_t>(sfxFromName("")));
}

void test_every_sound_has_notes() {
	for (uint8_t i = 0; i < static_cast<uint8_t>(Sfx::Count); i++) {
		size_t count = 0;
		TEST_ASSERT_NOT_NULL(sfxNotes(static_cast<Sfx>(i), count));
		TEST_ASSERT_TRUE(count > 0);
	}
}

int main() {
	UNITY_BEGIN();
	RUN_TEST(test_names_round_trip);
	RUN_TEST(test_unknown_name);
	RUN_TEST(test_every_sound_has_notes);
	return UNITY_END();
}
