#include "Resume.h"
#include <unity.h>

void setUp() {}
void tearDown() {}

void test_key_fits_nvs() {
	const String k = Resume::key("/histoires/le-tres-long-nom-de-dossier-qui-depasse");
	TEST_ASSERT_EQUAL(9, k.length());
	TEST_ASSERT_TRUE(k.startsWith("f"));
}

void test_key_tells_folders_apart() {
	TEST_ASSERT_TRUE(Resume::key("/podcasts/a") == Resume::key("/podcasts/a"));
	TEST_ASSERT_FALSE(Resume::key("/podcasts/a") == Resume::key("/podcasts/b"));
}

void test_round_trip_keeps_spaces() {
	uint32_t offset = 0;
	String track;
	TEST_ASSERT_TRUE(Resume::parse(Resume::format(123456, "02 - le loup.mp3"), offset, track));
	TEST_ASSERT_EQUAL_UINT32(123456, offset);
	TEST_ASSERT_EQUAL_STRING("02 - le loup.mp3", track.c_str());
}

void test_parse_rejects_garbage() {
	uint32_t offset = 7;
	String track;
	TEST_ASSERT_FALSE(Resume::parse("", offset, track));
	TEST_ASSERT_FALSE(Resume::parse("123", offset, track));
	TEST_ASSERT_FALSE(Resume::parse("123 ", offset, track));
	TEST_ASSERT_FALSE(Resume::parse(" a.mp3", offset, track));
	TEST_ASSERT_FALSE(Resume::parse("12x a.mp3", offset, track));
	TEST_ASSERT_EQUAL_UINT32(7, offset);
}

void test_find_by_name() {
	const std::vector<String> paths = {"/h/01.mp3", "/h/02.mp3", "/h/03.mp3"};
	TEST_ASSERT_EQUAL(1, Resume::find(paths, "02.mp3"));
	TEST_ASSERT_EQUAL(-1, Resume::find(paths, "04.mp3"));
	TEST_ASSERT_EQUAL(-1, Resume::find({}, "01.mp3"));
}

int main() {
	UNITY_BEGIN();
	RUN_TEST(test_key_fits_nvs);
	RUN_TEST(test_key_tells_folders_apart);
	RUN_TEST(test_round_trip_keeps_spaces);
	RUN_TEST(test_parse_rejects_garbage);
	RUN_TEST(test_find_by_name);
	return UNITY_END();
}
