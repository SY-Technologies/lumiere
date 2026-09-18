#pragma once

#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>

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

inline std::optional<std::int64_t> to_integer(double value)
{
    // INT64_MAX rounds up to 2^63 as a double, so the upper bound is exclusive.
    const double truncated = std::trunc(value);
    if (!std::isfinite(truncated) || truncated < -0x1p63 || truncated >= 0x1p63)
        return std::nullopt;
    return static_cast<std::int64_t>(truncated);
}

} // namespace lumiere::numeric
