#include "Target.h"
#include <unity.h>

void setUp() {}
void tearDown() {}

void test_builtin() {
	TEST_ASSERT_TRUE(Target::isBuiltin("builtin:boot"));
	TEST_ASSERT_FALSE(Target::isBuiltin("/sons/builtin:boot"));
	TEST_ASSERT_EQUAL_STRING("boot", Target::builtinId("builtin:boot").c_str());
}

void test_story_prefix() {
	TEST_ASSERT_EQUAL_STRING("/histoires/ours", Target::stripStoryPrefix("story:/histoires/ours").c_str());
	TEST_ASSERT_EQUAL_STRING("/histoires/ours", Target::stripStoryPrefix("/histoires/ours").c_str());
}

void test_mp3_ignores_case() {
	TEST_ASSERT_TRUE(Target::isMp3("a.mp3"));
	TEST_ASSERT_TRUE(Target::isMp3("A.MP3"));
	TEST_ASSERT_FALSE(Target::isMp3("a.mp3.txt"));
	TEST_ASSERT_FALSE(Target::isMp3("/sons/mp3"));
}

void test_file_target() {
	TEST_ASSERT_TRUE(Target::isFile("/sons/avion/01.mp3"));
	TEST_ASSERT_FALSE(Target::isFile("/sons/avion"));
	TEST_ASSERT_FALSE(Target::isFile("builtin:boot.mp3"));
}

void test_base_name() {
	TEST_ASSERT_EQUAL_STRING("01.mp3", Target::baseName("/sons/avion/01.mp3").c_str());
	TEST_ASSERT_EQUAL_STRING("01.mp3", Target::baseName("01.mp3").c_str());
	TEST_ASSERT_EQUAL_STRING("", Target::baseName("/sons/").c_str());
}

int main() {
	UNITY_BEGIN();
	RUN_TEST(test_builtin);
	RUN_TEST(test_story_prefix);
	RUN_TEST(test_mp3_ignores_case);
	RUN_TEST(test_file_target);
	RUN_TEST(test_base_name);
	return UNITY_END();
}
