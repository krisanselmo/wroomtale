#include "PowerPolicy.h"
#include <unity.h>

void setUp() {}
void tearDown() {}

using V = PowerPolicy::Verdict;

void test_idle_after_the_timeout() {
	PowerPolicy p(10000, 3300, 20000);
	p.activity(1000);
	TEST_ASSERT_EQUAL(V::Awake, p.check(10999, true, 3900));
	TEST_ASSERT_EQUAL(V::Idle, p.check(11000, true, 3900));
}

void test_activity_restarts_the_count() {
	PowerPolicy p(10000, 3300, 20000);
	p.activity(0);
	p.activity(9000);
	TEST_ASSERT_EQUAL(V::Awake, p.check(15000, true, 3900));
	TEST_ASSERT_EQUAL(V::Idle, p.check(19000, true, 3900));
}

void test_zero_never_idles() {
	PowerPolicy p(0, 3300, 20000);
	TEST_ASSERT_EQUAL(V::Awake, p.check(0xFFFFFFF0u, true, 3900));
}

void test_survives_millis_wrap() {
	PowerPolicy p(10000, 3300, 20000);
	p.activity(0xFFFFF000u);
	TEST_ASSERT_EQUAL(V::Awake, p.check(0x00001000u, true, 3900));
	TEST_ASSERT_EQUAL(V::Idle, p.check(0x00002000u, true, 3900));
}

void test_flat_only_once_held() {
	PowerPolicy p(0, 3300, 20000);
	TEST_ASSERT_EQUAL(V::Awake, p.check(1000, true, 3250));
	TEST_ASSERT_EQUAL(V::Awake, p.check(20999, true, 3250));
	TEST_ASSERT_EQUAL(V::Flat, p.check(21000, true, 3250));
}

void test_a_recovery_resets_the_hold() {
	PowerPolicy p(0, 3300, 20000);
	p.check(0, true, 3250);
	p.check(15000, true, 3350); // a peak sagged the cell, it came back
	TEST_ASSERT_EQUAL(V::Awake, p.check(25000, true, 3250));
	TEST_ASSERT_EQUAL(V::Flat, p.check(45000, true, 3250));
}

void test_flat_wins_over_idle() {
	PowerPolicy p(1000, 3300, 0);
	p.check(0, true, 3250);
	TEST_ASSERT_EQUAL(V::Flat, p.check(5000, true, 3250));
}

void test_no_divider_is_never_flat() {
	PowerPolicy p(0, 3300, 0);
	TEST_ASSERT_FALSE(p.flat(false, 3000));
	TEST_ASSERT_FALSE(p.flat(true, 0));
	TEST_ASSERT_TRUE(p.flat(true, 3299));
	TEST_ASSERT_FALSE(p.flat(true, 3300));
	p.check(0, false, 1000);
	TEST_ASSERT_EQUAL(V::Awake, p.check(100000, false, 1000));
}

int main() {
	UNITY_BEGIN();
	RUN_TEST(test_idle_after_the_timeout);
	RUN_TEST(test_activity_restarts_the_count);
	RUN_TEST(test_zero_never_idles);
	RUN_TEST(test_survives_millis_wrap);
	RUN_TEST(test_flat_only_once_held);
	RUN_TEST(test_a_recovery_resets_the_hold);
	RUN_TEST(test_flat_wins_over_idle);
	RUN_TEST(test_no_divider_is_never_flat);
	return UNITY_END();
}
