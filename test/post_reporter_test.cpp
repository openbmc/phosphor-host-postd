#include <gtest/gtest.h>

class PostReporterTest : public ::testing::Test {};

TEST(PostReporterTest, DummyTest) {
    EXPECT_EQ(1, 1);
}
