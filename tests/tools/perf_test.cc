#include <gtest/gtest.h>
#include "perfv2.h"

// Demonstrate some basic assertions.
TEST(PerfTest, SimpleFirstTest) {
  // Expect two strings not to be equal.
  // perf::PerfTool tool;
  EXPECT_STRNE("hello", "world");
  // Expect equality.
  EXPECT_EQ(7 * 6, 42);
}


int main(int argc, char **argv) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}