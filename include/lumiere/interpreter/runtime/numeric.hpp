#pragma once

#include <cmath>
#include <cstdint>
#include <limits>
#include <algorithm>
#include <optional>
#include <string>

namespace lumiere::numeric
{

inline constexpr auto minimum = std::numeric_limits<std::int64_t>::min();
inline constexpr auto maximum = std::numeric_limits<std::int64_t>::max();

inline std::optional<std::int64_t> add(std::int64_t a, std::int64_t b)
{
    if ((b > 0 && a > maximum - b) || (b < 0 && a < minimum - b))
        return std::nullopt;
    return a + b;
}

inline std::optional<std::int64_t> subtract(std::int64_t a, std::int64_t b)
{
    if ((b < 0 && a > maximum + b) || (b > 0 && a < minimum + b))
        return std::nullopt;
    return a - b;
}

inline std::optional<std::int64_t> multiply(std::int64_t a, std::int64_t b)
{
    if (a > 0 && ((b > 0 && a > maximum / b) || (b < 0 && b < minimum / a)))
        return std::nullopt;
    if (a < 0 && ((b > 0 && a < minimum / b) || (b < 0 && a < maximum / b)))
        return std::nullopt;
    return a * b;
}

inline std::optional<std::int64_t> divide(std::int64_t a, std::int64_t b)
{
    if (b == 0 || (a == minimum && b == -1))
        return std::nullopt;
    return a / b;
}

inline std::optional<std::int64_t> remainder(std::int64_t a, std::int64_t b)
{
    if (b == 0)
        return std::nullopt;
    // The mathematical remainder is representable even when the quotient is not.
    if (a == minimum && b == -1)
        return 0;
    return a % b;
}

inline std::optional<std::int64_t> negate(std::int64_t value)
{
    return subtract(0, value);
}

/**
 * @brief Removes the digit separators a numeric literal may carry.
 *
 * The lexer accepts an underscore between digits, so `1_000_000` reaches the
 * conversion functions with characters they do not understand.
 */
inline std::string without_digit_separators(std::string text)
{
    text.erase(std::remove(text.begin(), text.end(), '_'), text.end());
    return text;
}

/**
 * @brief Parses an Entier literal, or nothing when it does not fit.
 *
 * std::stoll throws on a literal larger than Entier, and that exception used to
 * surface as "erreur: stoll" — a C++ message, in English, with no source
 * location.
 */
inline std::optional<std::int64_t> parse_integer_literal(const std::string &text)
{
    try
    {
        std::size_t consumed = 0;
        const std::int64_t value = std::stoll(without_digit_separators(text), &consumed);
        if (consumed != without_digit_separators(text).size())
        {
            return std::nullopt;
        }
        return value;
    }
    catch (const std::exception &)
    {
        return std::nullopt;
    }
}

/**
 * @brief Parses a Décimal from text, or nothing.
 *
 * The whole text must be consumed, and the result must be finite. Every
 * arithmetic path that would reach a non-number or an infinity traps, so text
 * must not be the one door that lets them in: std::stod accepts "nan" and
 * "inf", and a value read from a file or from standard input would otherwise
 * carry them into a program that is built to exclude them.
 */
inline std::optional<double> parse_decimal(const std::string &text)
{
    try
    {
        std::size_t consumed = 0;
        const double value = std::stod(text, &consumed);
        if (consumed != text.size() || !std::isfinite(value))
        {
            return std::nullopt;
        }
        return value;
    }
    catch (const std::exception &)
    {
        return std::nullopt;
    }
}

inline std::optional<std::int64_t> to_integer(double value)
{
    // INT64_MAX rounds up to 2^63 as a double, so the upper bound is exclusive.
    const double truncated = std::trunc(value);
    if (!std::isfinite(truncated) || truncated < -0x1p63 || truncated >= 0x1p63)
        return std::nullopt;
    return static_cast<std::int64_t>(truncated);
}

} // namespace lumiere::numeric
