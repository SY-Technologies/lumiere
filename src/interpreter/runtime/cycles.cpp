#include "lumiere/interpreter/runtime/cycles.hpp"

#include <algorithm>
#include <vector>

namespace lumiere
{

namespace detail
{
std::size_t candidates_pending = 0;
std::size_t candidate_threshold = 4096;
} // namespace detail

namespace
{

std::size_t g_live = 0;
std::vector<const RefCounted *> g_candidates;
bool g_collecting = false;

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

RefCounted::RefCounted() { ++g_live; }

RefCounted::~RefCounted() { --g_live; }

std::size_t RefCounted::live_count() noexcept { return g_live; }

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
    if (object->m_buffered)
    {
        // Leaving a dangling entry behind would be worse than the leak, so the
        // object takes itself out of the buffer first. The last entry moves into
        // the vacated slot and is told where it now lives.
        const std::size_t slot = object->m_buffer_slot;
        g_candidates[slot] = g_candidates.back();
        g_candidates[slot]->m_buffer_slot = slot;
        g_candidates.pop_back();
        detail::candidates_pending = g_candidates.size();
        object->m_buffered = false;
    }
    delete object;
}

void CycleCollector::note_possible_root(const RefCounted *object) noexcept
{
    if (g_collecting || object->m_colour == RefColour::Purple)
    {
        return;
    }
    object->m_colour = RefColour::Purple;
    if (!object->m_buffered)
    {
        object->m_buffered = true;
        object->m_buffer_slot = g_candidates.size();
        g_candidates.push_back(object);
        detail::candidates_pending = g_candidates.size();
    }
}

std::size_t CycleCollector::candidate_count() noexcept { return g_candidates.size(); }

std::size_t cycle_candidate_count() noexcept { return CycleCollector::candidate_count(); }

void set_cycle_collection_threshold(const std::size_t candidates) noexcept { detail::candidate_threshold = candidates; }

std::size_t CycleCollector::collect()
{
    if (g_collecting)
    {
        return 0;
    }
    g_collecting = true;

    std::vector<const RefCounted *> candidates;
    candidates.swap(g_candidates);
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

    g_collecting = false;
    return reclaimed + freed_early.size();
}

std::size_t collect_cycles() { return CycleCollector::collect(); }

} // namespace lumiere