#pragma once

#include "lumiere/interpreter/runtime/ref.hpp"

#include <cstddef>
#include <vector>

namespace lumiere
{

/**
 * @brief Reclaims groups of objects that only keep each other alive.
 *
 * Reference counting frees everything else the moment its last reference goes,
 * but a cycle — an object holding a second that holds the first — keeps both
 * counts above zero forever. This is Bacon and Rajan's synchronous cycle
 * collection, which suits a counted runtime because it never needs to know
 * where the roots are: it works from the counts, so a value held only in a C++
 * local during a native call is accounted for like any other reference.
 *
 * The one obligation is on RefCounted::trace_references. It must report every reference an
 * object holds, exactly once. Reporting too few leaves a cycle uncollected,
 * which is merely a leak; reporting an edge that is not held can free an object
 * that is still in use.
 */

/**
 * @brief Holds the algorithm, and is the only thing allowed near the counts.
 *
 * Every member is static: there is one collector for the process, as there is
 * one heap.
 */
class CycleCollector
{
public:
    static std::size_t collect();
    static void note_possible_root(const RefCounted *object) noexcept;
    static void on_destroyed(const RefCounted *object) noexcept;
    [[nodiscard]] static std::size_t candidate_count() noexcept;

private:
    static void mark_grey(const RefCounted *root);
    static void scan(const RefCounted *root);
    static void scan_black(const RefCounted *root);
    static void gather_white(const RefCounted *root, std::vector<const RefCounted *> &garbage);
    static std::vector<const RefCounted *> children_of(const RefCounted *object);
};

/** @brief Runs a collection now. Returns how many objects were reclaimed. */
std::size_t collect_cycles();

/** @brief How many objects are waiting to be examined as possible cycle roots. */
[[nodiscard]] std::size_t cycle_candidate_count() noexcept;

namespace detail
{
// Read inline by collect_cycles_if_due so that the common answer — not yet —
// costs two loads and a comparison rather than a call.
extern std::size_t candidates_pending;
extern std::size_t candidate_threshold;
} // namespace detail

/**
 * @brief Collects only once enough candidates have accumulated.
 *
 * Called from a point where no object is part-way through being updated — a
 * loop's back edge, or a function return — so that the counts it reads are
 * settled. Returns how many objects were reclaimed, zero when it did nothing.
 */
inline std::size_t collect_cycles_if_due()
{
    if (detail::candidates_pending < detail::candidate_threshold)
    {
        return 0;
    }
    return collect_cycles();
}

/** @brief Candidate count at which collect_cycles_if_due() runs a collection. */
void set_cycle_collection_threshold(std::size_t candidates) noexcept;

#ifndef NDEBUG
/**
 * @brief Every counted object still alive, for diagnosing a cycle that survived.
 *
 * Debug builds only. live_count() says how many objects a collection failed to
 * reclaim; this says which, so their dynamic types name the edge that
 * trace_references is not reporting.
 */
[[nodiscard]] std::vector<const RefCounted *> live_objects();
#endif

} // namespace lumiere
