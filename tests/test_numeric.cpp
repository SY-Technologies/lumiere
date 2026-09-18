#include "lumiere/interpreter/runtime/numeric.hpp"
#include <gtest/gtest.h>

namespace lumiere::numeric
{

TEST(Numeric, ChecksIntegerBoundariesWithoutOverflow)
{
    EXPECT_FALSE(add(maximum, 1));
    EXPECT_FALSE(add(minimum, -1));
    EXPECT_FALSE(subtract(minimum, 1));
    EXPECT_FALSE(subtract(maximum, -1));
    EXPECT_FALSE(negate(minimum));
    EXPECT_EQ(add(minimum, maximum), -1);
    EXPECT_EQ(subtract(minimum, minimum), 0);
    EXPECT_EQ(subtract(maximum, maximum), 0);
    EXPECT_EQ(negate(maximum), -maximum);
}

TEST(Numeric, ChecksMultiplicationInEverySignQuadrant)
{
    EXPECT_FALSE(multiply(maximum, 2));
    EXPECT_FALSE(multiply(minimum, 2));
    EXPECT_FALSE(multiply(2, minimum));
    EXPECT_FALSE(multiply(minimum, -1));
    EXPECT_FALSE(multiply(-1, minimum));
    EXPECT_EQ(multiply(minimum, 1), minimum);
    EXPECT_EQ(multiply(maximum, 1), maximum);
    EXPECT_EQ(multiply(minimum, 0), 0);
    EXPECT_EQ(multiply(0, minimum), 0);
    EXPECT_EQ(multiply(-7, -9), 63);
    EXPECT_EQ(multiply(-7, 9), -63);
    EXPECT_EQ(multiply(7, -9), -63);
    EXPECT_EQ(multiply(minimum / 2, 2), minimum);
}

TEST(Numeric, DefinesDivisionAndRemainderAtTheMinimum)
{
    EXPECT_FALSE(divide(minimum, -1));
    EXPECT_FALSE(divide(1, 0));
    EXPECT_FALSE(remainder(1, 0));
    EXPECT_EQ(remainder(minimum, -1), 0);
    EXPECT_EQ(divide(-7, 3), -2);
    EXPECT_EQ(remainder(-7, 3), -1);
}

TEST(Numeric, RejectsNonfiniteAndUnrepresentableConversions)
{
    EXPECT_FALSE(to_integer(std::numeric_limits<double>::quiet_NaN()));
    EXPECT_FALSE(to_integer(std::numeric_limits<double>::infinity()));
    EXPECT_FALSE(to_integer(-std::numeric_limits<double>::infinity()));
    EXPECT_FALSE(to_integer(0x1p63));
    EXPECT_FALSE(to_integer(std::nextafter(-0x1p63, -INFINITY)));
    EXPECT_EQ(to_integer(-0x1p63), minimum);
    EXPECT_TRUE(to_integer(std::nextafter(0x1p63, 0.0)));
    EXPECT_EQ(to_integer(-3.9), -3);
    EXPECT_EQ(to_integer(3.9), 3);
}

} // namespace lumiere::numeric
