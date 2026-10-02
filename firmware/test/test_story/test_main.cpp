#include "Story.h"
#include <unity.h>

void setUp() { Story::stop(); }
void tearDown() {}

constexpr uint8_t PREV = 1 << Story::BTN_PREV;
constexpr uint8_t PLAY = 1 << Story::BTN_PLAY;
constexpr uint8_t NEXT = 1 << Story::BTN_NEXT;

void test_demo_walk() {
	TEST_ASSERT_TRUE(Story::loadDemo());
	TEST_ASSERT_EQUAL_STRING("depart", Story::currentId().c_str());
	TEST_ASSERT_EQUAL_UINT8(PREV | NEXT, Story::choiceMask());
	TEST_ASSERT_FALSE(Story::choose(Story::BTN_PLAY));

	TEST_ASSERT_TRUE(Story::choose(Story::BTN_PREV));
	TEST_ASSERT_EQUAL_STRING("gare", Story::currentId().c_str());
	TEST_ASSERT_EQUAL_STRING("billet", Story::flagList().c_str());
	TEST_ASSERT_TRUE(Story::choose(Story::BTN_PREV));
	TEST_ASSERT_TRUE(Story::atEnd());
}

void test_flag_unlocks_a_branch() {
	const char *text = "a a.mp3 next=b prev=c\n"
	                   "b b.mp3 prev=key?d next=a\n"
	                   "c c.mp3 set=key next=b\n"
	                   "d d.mp3\n";
	TEST_ASSERT_TRUE(Story::loadFromText(text, "/h"));
	TEST_ASSERT_TRUE(Story::choose(Story::BTN_NEXT));
	TEST_ASSERT_EQUAL_UINT8(NEXT, Story::choiceMask()); // d still locked
	TEST_ASSERT_FALSE(Story::choose(Story::BTN_PREV));

	TEST_ASSERT_TRUE(Story::choose(Story::BTN_NEXT)); // back to a
	TEST_ASSERT_TRUE(Story::choose(Story::BTN_PREV)); // c grants key
	TEST_ASSERT_TRUE(Story::choose(Story::BTN_NEXT)); // b again
	TEST_ASSERT_EQUAL_UINT8(PREV | NEXT, Story::choiceMask());
	TEST_ASSERT_TRUE(Story::choose(Story::BTN_PREV));
	TEST_ASSERT_EQUAL_STRING("d", Story::currentId().c_str());
}

void test_paths_resolve_against_folder() {
	const char *text = "# a comment line\n"
	                   "a a.mp3 play=b   # trailing comment\r\n"
	                   "b /abs/b.mp3 play=c\n"
	                   "c builtin:boot\n";
	TEST_ASSERT_TRUE(Story::loadFromText(text, "/histoires/ours"));
	TEST_ASSERT_EQUAL_STRING("/histoires/ours/a.mp3", Story::currentAudio().c_str());
	TEST_ASSERT_EQUAL_UINT8(PLAY, Story::choiceMask());
	Story::choose(Story::BTN_PLAY);
	TEST_ASSERT_EQUAL_STRING("/abs/b.mp3", Story::currentAudio().c_str());
	Story::choose(Story::BTN_PLAY);
	TEST_ASSERT_EQUAL_STRING("builtin:boot", Story::currentAudio().c_str());
}

void test_rejects_broken_manifests() {
	TEST_ASSERT_FALSE(Story::loadFromText("a a.mp3 next=nowhere\n", "/h"));
	TEST_ASSERT_FALSE(Story::active());
	TEST_ASSERT_FALSE(Story::loadFromText("lonely\n", "/h"));
	TEST_ASSERT_FALSE(Story::loadFromText("# nothing but comments\n", "/h"));
}

void test_too_many_nodes() {
	String text;
	for (int i = 0; i <= Story::MAX_NODES; i++) text += "n" + String(i) + " x.mp3\n";
	TEST_ASSERT_FALSE(Story::loadFromText(text, "/h"));
}

int main() {
	UNITY_BEGIN();
	RUN_TEST(test_demo_walk);
	RUN_TEST(test_flag_unlocks_a_branch);
	RUN_TEST(test_paths_resolve_against_folder);
	RUN_TEST(test_rejects_broken_manifests);
	RUN_TEST(test_too_many_nodes);
	return UNITY_END();
}
