#include "lumiere/interpreter/runtime/cycles.hpp"
#include "lumiere/interpreter/runtime/value.hpp"

#include <gtest/gtest.h>

#include <algorithm>

namespace lumiere
{
namespace
{

/** @brief Live counted objects, after clearing anything already pending. */
std::size_t settled_live_count()
{
    collect_cycles();
    return RefCounted::live_count();
}

} // namespace

TEST(CycleCollector, FreesANonCyclicValueWithoutBeingAsked)
{
    const std::size_t before = settled_live_count();
    {
        const Value list = Value::liste(make_ref<ListeData>());
        EXPECT_EQ(RefCounted::live_count(), before + 1);
    }
    // Reference counting alone reclaims this; the collector has nothing to do.
    EXPECT_EQ(RefCounted::live_count(), before);
}

TEST(CycleCollector, ReclaimsAListThatContainsItself)
{
    const std::size_t before = settled_live_count();
    {
        auto list = make_ref<ListeData>();
        list->elements.push_back(Value::liste(list)); // the cycle
    }
    // The list still refers to itself, so counting alone cannot free it.
    EXPECT_GT(RefCounted::live_count(), before);

    EXPECT_GE(collect_cycles(), 1u);
    EXPECT_EQ(RefCounted::live_count(), before);
}

TEST(CycleCollector, ReclaimsTwoObjectsThatReferToEachOther)
{
    const std::size_t before = settled_live_count();
    {
        auto first = make_ref<LumiereObject>();
        auto second = make_ref<LumiereObject>();
        first->fields["autre"] = Value::objet(second);
        second->fields["autre"] = Value::objet(first);
    }
    EXPECT_GT(RefCounted::live_count(), before);

    EXPECT_GE(collect_cycles(), 2u);
    EXPECT_EQ(RefCounted::live_count(), before);
}

TEST(CycleCollector, ReclaimsACycleThroughADictionary)
{
    const std::size_t before = settled_live_count();
    {
        auto dictionary = make_ref<DictData>();
        dictionary->set(Value::texte("moi"), Value::dictionnaire(dictionary));
    }
    EXPECT_GT(RefCounted::live_count(), before);

    collect_cycles();
    EXPECT_EQ(RefCounted::live_count(), before);
}

TEST(CycleCollector, KeepsACycleThatIsStillReachable)
{
    const std::size_t before = settled_live_count();
    auto held = make_ref<ListeData>();
    held->elements.push_back(Value::liste(held));

    // The cycle is garbage from the inside, but this handle is outside it.
    collect_cycles();
    EXPECT_GT(RefCounted::live_count(), before);
    ASSERT_EQ(held->elements.size(), 1u);
    EXPECT_EQ(held->elements.front().as_liste().get(), held.get());

    held->elements.clear();
    held.reset();
    collect_cycles();
    EXPECT_EQ(RefCounted::live_count(), before);
}

TEST(CycleCollector, LeavesAcyclicStructuresAloneWhileCollecting)
{
    const std::size_t before = settled_live_count();
    auto outer = make_ref<ListeData>();
    auto inner = make_ref<ListeData>();
    inner->elements.push_back(Value::entier(7));
    outer->elements.push_back(Value::liste(inner));

    collect_cycles();

    // Nothing here is garbage, so the sweep must not have touched it.
    ASSERT_EQ(outer->elements.size(), 1u);
    ASSERT_EQ(inner->elements.size(), 1u);
    EXPECT_EQ(inner->elements.front().as_entier(), 7);
    EXPECT_EQ(RefCounted::live_count(), before + 2);
}

TEST(CycleCollector, KeepsRepeatedGarbageBounded)
{
    // What an interpreter loop does: build a cycle, drop it, repeat. Without a
    // collector the live count climbs with every iteration.
    const std::size_t before = settled_live_count();
    set_cycle_collection_threshold(64);

    std::size_t high_water = 0;
    for (int iteration = 0; iteration < 5000; ++iteration)
    {
        {
            auto first = make_ref<LumiereObject>();
            auto second = make_ref<LumiereObject>();
            first->fields["autre"] = Value::objet(second);
            second->fields["autre"] = Value::objet(first);
        }
        collect_cycles_if_due();
        high_water = std::max(high_water, RefCounted::live_count());
    }

    set_cycle_collection_threshold(4096);
    collect_cycles();

    // 10,000 objects were created; the collector must have kept far fewer alive
    // at any moment, and none of them at the end.
    EXPECT_LT(high_water, before + 1000);
    EXPECT_EQ(RefCounted::live_count(), before);
}

} // namespace lumiere
