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

// The contract decimal_to_text exists for: whatever it prints, parse_decimal
// reads back as exactly the same double. Six-significant-digit formatting broke
// this silently, so it is worth asserting rather than assuming.
TEST(Numeric, DecimalTextReadsBackAsTheSameValue)
{
    const double values[] = {
        0.0, -0.0, 1.0, -1.0, 0.5, 0.1, 0.1 + 0.2, 1.0 / 3.0,
        123456789.125, 3.14159265358979323846, 2.718281828459045,
        1e-300, 1e300, 1e10, 1.5e-5, 9007199254740993.0,
        std::numeric_limits<double>::min(),
        std::numeric_limits<double>::max(),
        std::numeric_limits<double>::denorm_min(),
        std::nextafter(1.0, 2.0),
    };

    for (const double value : values)
    {
        const std::string text = decimal_to_text(value);
        const std::optional<double> parsed = parse_decimal(text);
        ASSERT_TRUE(parsed.has_value()) << text;
        EXPECT_EQ(*parsed, value) << text;
    }
}

// parse_decimal's contract (see its docstring): no leading whitespace, no
// locale dependence, no C hex-float syntax, subnormals accepted rather than
// refused. Platform-independent so it pins the strtod_l fallback used where
// Apple's libc++ has no floating-point std::from_chars just as much as the
// from_chars path used everywhere else.
TEST(Numeric, ParseDecimalRejectsWhatFromCharsRejects)
{
    EXPECT_FALSE(parse_decimal(" 1.5").has_value());
    EXPECT_FALSE(parse_decimal("\t1.5").has_value());
    EXPECT_FALSE(parse_decimal("0x1p0").has_value());
    EXPECT_FALSE(parse_decimal("-0x1p0").has_value());
    EXPECT_FALSE(parse_decimal("").has_value());
    EXPECT_FALSE(parse_decimal("1.5 ").has_value());
    EXPECT_FALSE(parse_decimal("abc").has_value());

    ASSERT_TRUE(parse_decimal("5e-324").has_value());
    EXPECT_EQ(*parse_decimal("5e-324"), 5e-324);
    ASSERT_TRUE(parse_decimal("-1.5").has_value());
    EXPECT_EQ(*parse_decimal("-1.5"), -1.5);
}

TEST(Numeric, DecimalTextKeeps0Point1Plus0Point2Honest)
{
    // The whole point: this is not 0.3, and the runtime no longer says it is.
    EXPECT_EQ(decimal_to_text(0.1 + 0.2), "0.30000000000000004");
    EXPECT_EQ(decimal_to_text(123456789.125), "123456789.125");
}

TEST(Numeric, DecimalTextKeepsThePointOnWholeValues)
{
    // Entier and Décimal are distinct types, so a Décimal must not print as
    // something that reads back as an Entier.
    EXPECT_EQ(decimal_to_text(2.0), "2.0");
    EXPECT_EQ(decimal_to_text(-0.0), "-0.0");
    EXPECT_EQ(decimal_to_text(100.0), "100.0");
    // An exponent already marks the value as a Décimal.
    EXPECT_EQ(decimal_to_text(1e10), "1e+10");
    // A value that already has a point keeps exactly one.
    EXPECT_EQ(decimal_to_text(0.5), "0.5");
}

TEST(Numeric, DecimalTextNamesTheNonNumbersInFrench)
{
    EXPECT_EQ(decimal_to_text(std::numeric_limits<double>::quiet_NaN()), "non_nombre");
    EXPECT_EQ(decimal_to_text(std::numeric_limits<double>::infinity()), "infini");
    EXPECT_EQ(decimal_to_text(-std::numeric_limits<double>::infinity()), "-infini");
}

} // namespace lumiere::numeric
