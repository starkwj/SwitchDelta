#include <gtest/gtest.h>

// Demonstrate some basic assertions.
TEST(HelloTest, SimpleFirstTest) {
  // Expect two strings not to be equal.
  EXPECT_STRNE("hello", "world");
  // Expect equality.
  EXPECT_EQ(7 * 6, 42);
}

TEST(HelloTest2, SimpleFirstTest) {
  EXPECT_STRNE("I want to learn", "gtest test_case_name");
  EXPECT_EQ(1 + 1, 2);
}

TEST(HelloTest1, SimpleFirstTest2) {
  EXPECT_STRNE("I want to learn", "gtest test_name");
  EXPECT_EQ(1 + 1, 2);
}

int main(int argc, char **argv) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}