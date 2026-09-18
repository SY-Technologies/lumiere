#pragma once

#include <cmath>
#include <cstdint>
#include <limits>
#include <algorithm>
#include <array>
#include <charconv>
#include <cstdio>
#include <optional>
#include <string>
#include <system_error>

namespace lumiere::numeric
{

/**
 * @brief The shortest text that reads back as exactly this value.
 *
 * Stream formatting defaults to six significant digits, which rounds in
 * silence: 123456789.125 came out as 1.23457e+08 and 0.1 + 0.2 as 0.3, so the
 * language misreported its own arithmetic and a printed number could not be
 * trusted or pasted back into a program. std::to_chars without a precision
 * gives the shortest digit string that parses back to the same double, which is
 * the only formatting with that property.
 *
 * The two values that are not numbers are named as Lumière names them. They do
 * not round-trip -- neither is a literal -- but printing C's spelling of them in
 * a language whose constants are 'infini' and 'non_nombre' helps no one.
 */
inline std::string decimal_to_text(const double value)
{
    if (std::isnan(value))
    {
        return "non_nombre";
    }
    if (std::isinf(value))
    {
        return value < 0 ? "-infini" : "infini";
    }

    // The longest shortest-round-trip form of a double is 17 significant
    // digits with a sign, a point and an exponent: comfortably under this.
    std::array<char, 64> buffer{};
    const auto [end, failure] = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);

    // Unreachable with a buffer this size, and the fallback still round-trips
    // rather than inventing a value.
    std::string text;
    if (failure == std::errc{})
    {
        text.assign(buffer.data(), end);
    }
    else
    {
        const int written = std::snprintf(buffer.data(), buffer.size(), "%.17g", value);
        text = written > 0 ? std::string(buffer.data(), static_cast<std::size_t>(written)) : "0";
    }

    // A whole-numbered Décimal keeps its point. Entier and Décimal are distinct
    // types here -- they are not even equal as dictionary keys -- so printing
    // 2.0 as "2" makes two values that are not equal print identically, and the
    // text no longer reads back as the type it came from. An exponent already
    // marks the value as a Décimal, so only the plain form needs the suffix.
    if (text.find('.') == std::string::npos &&
        text.find('e') == std::string::npos &&
        text.find('E') == std::string::npos)
    {
        text += ".0";
    }
    return text;
}

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
 * The whole text must be consumed, and the result must be representable.
 *
 * std::from_chars rather than std::stod, for four reasons that all bit:
 * stod skips leading whitespace and then reports it as consumed, so " 1.5"
 * was accepted while "1.5 " was refused; stod reads the decimal point from the
 * C locale, which makes the language's own numbers depend on the environment;
 * stod throws on a subnormal such as 5e-324, which is a perfectly representable
 * Décimal; and it signals failure by throwing, which is how "erreur: stod"
 * reached users. from_chars has none of those properties and is the exact
 * inverse of decimal_to_text above.
 *
 * A magnitude the type cannot hold is refused rather than rounded to zero or to
 * an infinity. That matches how Entier behaves -- arithmetic that leaves the
 * range traps rather than wrapping -- and it is the same principle as printing
 * every digit: the runtime does not quietly substitute a different number.
 */
inline std::optional<double> parse_decimal(const std::string &text)
{
    std::string_view body(text);
    // from_chars does not accept a leading '+', which callers may well pass.
    if (!body.empty() && body.front() == '+')
    {
        body.remove_prefix(1);
    }

    double value = 0.0;
    const auto [end, failure] =
        std::from_chars(body.data(), body.data() + body.size(), value, std::chars_format::general);
    if (failure != std::errc{} || end != body.data() + body.size() || !std::isfinite(value))
    {
        return std::nullopt;
    }
    return value;
}

/**
 * @brief Parses a Décimal literal written in source, or nothing when it cannot
 *        be represented.
 *
 * Checked in the tokenizer, where the source location still exists, for the
 * same reason as parse_integer_literal above.
 */
inline std::optional<double> parse_decimal_literal(const std::string &text)
{
    return parse_decimal(without_digit_separators(text));
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
