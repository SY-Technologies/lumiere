#include "lumiere/interpreter/runtime/cycles.hpp"

#include <algorithm>
#include <memory>
#include <vector>

#ifndef NDEBUG
#include <unordered_set>
#endif

namespace lumiere
{

namespace detail
{
thread_local std::size_t candidates_pending = 0;
thread_local std::size_t candidate_threshold = 4096;
} // namespace detail

namespace
{

/**
 * @brief One thread's collector.
 *
 * A Lumière runtime belongs to a thread, and its collector belongs with it.
 * This state used to be process-global, which made two interpreters on two
 * threads share one candidate buffer, and that is not merely unsynchronised --
 * it is wrong even with a lock, because one runtime's garbage is not the
 * other's to collect. It also crashed: collect() swaps the buffer into a local,
 * and the other thread's on_destroyed then does its swap-removal against the
 * new buffer, so the freed object stays in the collector's copy and is read
 * there. That was roughly one run in six of a test that drives an interpreter
 * from a worker thread.
 *
 * Non-atomic reference counts already say a counted object may not be shared
 * between threads. This says the same thing about the collector, and makes it
 * true rather than merely intended.
 */
struct ThreadCollector
{
    std::vector<const RefCounted *> candidates;
    bool collecting = false;
    std::size_t live = 0;
#ifndef NDEBUG
    // Debug builds keep the identity of every live object, not just the count.
    // A cycle the collector fails to reclaim is otherwise invisible: the count
    // says how many survived, this says what they are.
    std::unordered_set<const RefCounted *> live_objects;
#endif

    ~ThreadCollector();
};

// The raw pointer is what the hot paths read, and it is null before the first
// counted object on this thread and again after the thread's collector is torn
// down. Both are states where there is simply no buffer to queue into.
thread_local ThreadCollector *g_collector = nullptr;
thread_local std::unique_ptr<ThreadCollector> g_collector_owner;

ThreadCollector &collector()
{
    if (g_collector == nullptr)
    {
        g_collector_owner = std::make_unique<ThreadCollector>();
        g_collector = g_collector_owner.get();
    }
    return *g_collector;
}

/** @brief Gathers the references an object holds. */
class ChildCollector final : public RefVisitor
{
public:
    explicit ChildCollector(std::vector<const RefCounted *> &out) : m_out(out) {}
    void visit(RefCounted *child) override
    {
        if (child != nullptr)
        {
            m_out.push_back(child);
        }
    }

private:
    std::vector<const RefCounted *> &m_out;
};

} // namespace

std::vector<const RefCounted *> CycleCollector::children_of(const RefCounted *object)
{
    std::vector<const RefCounted *> children;
    ChildCollector collector(children);
    object->trace_references(collector);
    return children;
}

// Every walk below is written with an explicit stack. The recursive form in the
// paper is shorter, but a deep structure would then decide how much C++ stack a
// collection needs.

void CycleCollector::mark_grey(const RefCounted *root)
{
    if (root->m_colour == RefColour::Grey)
    {
        return;
    }
    root->m_colour = RefColour::Grey;
    std::vector<const RefCounted *> pending{root};
    while (!pending.empty())
    {
        const RefCounted *object = pending.back();
        pending.pop_back();
        for (const RefCounted *child : children_of(object))
        {
            // One subtraction per edge, whether or not the child is new: this is
            // what removes the cycle's own references from the counts.
            --child->m_count;
            if (child->m_colour != RefColour::Grey)
            {
                child->m_colour = RefColour::Grey;
                pending.push_back(child);
            }
        }
    }
}

void CycleCollector::scan_black(const RefCounted *root)
{
    root->m_colour = RefColour::Black;
    std::vector<const RefCounted *> pending{root};
    while (!pending.empty())
    {
        const RefCounted *object = pending.back();
        pending.pop_back();
        for (const RefCounted *child : children_of(object))
        {
            ++child->m_count;
            if (child->m_colour != RefColour::Black)
            {
                child->m_colour = RefColour::Black;
                pending.push_back(child);
            }
        }
    }
}

void CycleCollector::scan(const RefCounted *root)
{
    std::vector<const RefCounted *> pending{root};
    while (!pending.empty())
    {
        const RefCounted *object = pending.back();
        pending.pop_back();
        if (object->m_colour != RefColour::Grey)
        {
            continue;
        }
        if (object->m_count > 0)
        {
            // Something outside the cycle still refers to it, so it and
            // everything it reaches are live.
            scan_black(object);
            continue;
        }
        object->m_colour = RefColour::White;
        for (const RefCounted *child : children_of(object))
        {
            pending.push_back(child);
        }
    }
}

void CycleCollector::gather_white(const RefCounted *root, std::vector<const RefCounted *> &garbage)
{
    std::vector<const RefCounted *> pending{root};
    while (!pending.empty())
    {
        const RefCounted *object = pending.back();
        pending.pop_back();
        if (object->m_colour != RefColour::White || object->m_buffered)
        {
            continue;
        }
        object->m_colour = RefColour::Black;
        garbage.push_back(object);
        for (const RefCounted *child : children_of(object))
        {
            pending.push_back(child);
        }
    }
}

RefCounted::RefCounted()
{
    ThreadCollector &state = collector();
    ++state.live;
#ifndef NDEBUG
    state.live_objects.insert(this);
#endif
}

RefCounted::~RefCounted()
{
    // Null only once this thread's collector has been torn down, which happens
    // after everything it was tracking is already gone.
    if (g_collector != nullptr)
    {
        --g_collector->live;
#ifndef NDEBUG
        g_collector->live_objects.erase(this);
#endif
    }
}

std::size_t RefCounted::live_count() noexcept
{
    return g_collector != nullptr ? g_collector->live : 0;
}

void RefCounted::destroy() const noexcept
{
    CycleCollector::on_destroyed(this);
    // Otherwise it is in the candidate buffer, which owns it until the next
    // collection: freeing it here would leave a dangling entry behind.
}

void RefCounted::note_possible_root() const noexcept { CycleCollector::note_possible_root(this); }

void CycleCollector::on_destroyed(const RefCounted *object) noexcept
{
    object->m_colour = RefColour::Black;
    if (object->m_buffered && g_collector != nullptr)
    {
        // Leaving a dangling entry behind would be worse than the leak, so the
        // object takes itself out of the buffer first. The last entry moves into
        // the vacated slot and is told where it now lives.
        std::vector<const RefCounted *> &candidates = g_collector->candidates;
        const std::size_t slot = object->m_buffer_slot;
        candidates[slot] = candidates.back();
        candidates[slot]->m_buffer_slot = slot;
        candidates.pop_back();
        detail::candidates_pending = candidates.size();
        object->m_buffered = false;
    }
    delete object;
}

void CycleCollector::note_possible_root(const RefCounted *object) noexcept
{
    // No collector means the thread is past its own teardown; not queueing then
    // leaves a cycle uncollected, which is the safe direction.
    if (g_collector == nullptr || g_collector->collecting || object->m_colour == RefColour::Purple)
    {
        return;
    }
    object->m_colour = RefColour::Purple;
    if (!object->m_buffered)
    {
        std::vector<const RefCounted *> &candidates = g_collector->candidates;
        object->m_buffered = true;
        object->m_buffer_slot = candidates.size();
        candidates.push_back(object);
        detail::candidates_pending = candidates.size();
    }
}

std::size_t CycleCollector::candidate_count() noexcept
{
    return g_collector != nullptr ? g_collector->candidates.size() : 0;
}

void CycleCollector::forget_candidates(const std::vector<const RefCounted *> &candidates) noexcept
{
    for (const RefCounted *object : candidates)
    {
        object->m_buffered = false;
    }
}

std::size_t cycle_candidate_count() noexcept { return CycleCollector::candidate_count(); }

void set_cycle_collection_threshold(const std::size_t candidates) noexcept { detail::candidate_threshold = candidates; }

// A thread reclaims its own cycles as it finishes, because nobody else can: the
// buffer they were queued in belongs to this thread and is about to go. Without
// this, a worker thread's garbage would simply leak.
ThreadCollector::~ThreadCollector()
{
    CycleCollector::collect();

    // Whatever collection re-queued is not going to be looked at again, so the
    // objects must not be left believing they are still in a buffer: one of
    // them outliving this thread would otherwise try to remove itself from it.
    CycleCollector::forget_candidates(candidates);
    candidates.clear();
    detail::candidates_pending = 0;
    g_collector = nullptr;
}

std::size_t CycleCollector::collect()
{
    if (g_collector == nullptr || g_collector->collecting)
    {
        return 0;
    }
    ThreadCollector &state = *g_collector;
    state.collecting = true;

    std::vector<const RefCounted *> candidates;
    candidates.swap(state.candidates);
    detail::candidates_pending = 0;

    // Anything that lost its last reference while buffered is plain garbage.
    std::vector<const RefCounted *> roots;
    roots.reserve(candidates.size());
    std::vector<const RefCounted *> freed_early;
    for (const RefCounted *candidate : candidates)
    {
        candidate->m_buffered = false;
        if (candidate->m_colour == RefColour::Purple && candidate->m_count > 0)
        {
            roots.push_back(candidate);
        }
        else if (candidate->m_colour == RefColour::Black && candidate->m_count == 0)
        {
            freed_early.push_back(candidate);
        }
    }

    for (const RefCounted *root : roots)
    {
        mark_grey(root);
    }
    for (const RefCounted *root : roots)
    {
        scan(root);
    }

    std::vector<const RefCounted *> garbage;
    for (const RefCounted *root : roots)
    {
        gather_white(root, garbage);
    }

    // mark_grey subtracted every edge inside the cycle, so these counts no longer
    // describe the references the objects actually hold. Put those back before
    // breaking the links, or clearing them would subtract the same edge twice and
    // free an object this sweep is still holding a pointer to.
    //
    // Only white objects can refer to a white object: anything reachable from a
    // live one was restored by scan_black and would not be here.
    for (const RefCounted *object : garbage)
    {
        for (const RefCounted *child : children_of(object))
        {
            ++child->m_count;
        }
    }

    // Then hold each one up, so that breaking a link cannot free its neighbour
    // mid-sweep.
    for (const RefCounted *object : garbage)
    {
        ++object->m_count;
    }
    for (const RefCounted *object : garbage)
    {
        const_cast<RefCounted *>(object)->clear_references();
    }
    const std::size_t reclaimed = garbage.size();
    for (const RefCounted *object : garbage)
    {
        if (--object->m_count == 0)
        {
            delete object;
        }
    }
    for (const RefCounted *object : freed_early)
    {
        delete object;
    }

    state.collecting = false;
    return reclaimed + freed_early.size();
}

std::size_t collect_cycles() { return CycleCollector::collect(); }

#ifndef NDEBUG
std::vector<const RefCounted *> live_objects()
{
    if (g_collector == nullptr)
    {
        return {};
    }
    return {g_collector->live_objects.begin(), g_collector->live_objects.end()};
}
#endif

} // namespace lumiere