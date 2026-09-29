#pragma once

#include <cstdint>

namespace lumiere::stats
{

/**
 * @brief Counters that attribute cost to something other than the clock.
 *
 * A wall-clock difference says a change helped; it does not say why, and on a
 * shared machine it does not always say it reliably. Allocation counts are
 * exact, reproducible, and they are what most of this interpreter's time turns
 * out to be: they came first when the method-call path was measured at 3.5x
 * CPython.
 *
 * Counting is done by replacing the global allocation operators, so it covers
 * every allocation in the process, including the standard library's. The cost
 * is one increment per allocation and was measured at nothing across the
 * benchmark suite -- which is why there is no build flag to turn it off, and no
 * second binary whose numbers would have to be trusted to come from the same
 * code.
 */

/**
 * @brief Whether this build counts allocations at all.
 *
 * False under AddressSanitizer, which replaces the same operators to pair every
 * new with its matching delete. A counter is not worth a correctness check, so
 * there the counts read zero and say so.
 */
[[nodiscard]] bool counting_allocations() noexcept;

/** @brief Heap allocations since the process started. */
[[nodiscard]] std::uint64_t allocations() noexcept;

/** @brief Bytes asked of the heap since the process started. */
[[nodiscard]] std::uint64_t allocated_bytes() noexcept;

/**
 * @brief Highest resident set size the process has reached, in bytes.
 *
 * Reported by the operating system rather than counted here, so it includes
 * the binary and everything the C++ runtime allocated before main. Zero when
 * the platform declines to say.
 */
[[nodiscard]] std::uint64_t peak_resident_bytes() noexcept;

} // namespace lumiere::stats
