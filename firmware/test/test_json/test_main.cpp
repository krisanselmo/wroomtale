#include "Json.h"
#include <unity.h>

void setUp() {}
void tearDown() {}

void test_plain_text_unchanged() {
	TEST_ASSERT_EQUAL_STRING("/sons/avion", jsonEscape("/sons/avion").c_str());
}

void test_quotes_and_backslashes() {
	TEST_ASSERT_EQUAL_STRING("say \\\"hi\\\"", jsonEscape("say \"hi\"").c_str());
	TEST_ASSERT_EQUAL_STRING("a\\\\b", jsonEscape("a\\b").c_str());
}

int main() {
	UNITY_BEGIN();
	RUN_TEST(test_plain_text_unchanged);
	RUN_TEST(test_quotes_and_backslashes);
	return UNITY_END();
}
