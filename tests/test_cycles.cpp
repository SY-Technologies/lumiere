#include "lumiere/interpreter/runtime/cycles.hpp"
#include "lumiere/interpreter/runtime/value.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <thread>
#include <cstdio>
#include <map>
#include <string>
#include <typeinfo>
#include <cstdlib>

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

namespace
{

/** @brief A native state that keeps Values, as a server keeps its handlers. */
struct HandlerState final : NativeState
{
    Value handler = Value::rien();

    void trace_references(RefVisitor &visitor) const override
    {
        trace_value(handler, visitor);
    }

    void clear_references() override { handler = Value::rien(); }
};

} // namespace

// The shape a standard-library server makes: an instance owns C++ state, the
// state holds the handler it will call, and the handler is a native function
// bound back onto the instance. Nothing here is visible to C++ reflection, so
// this only collects because the state reports its Values and the function
// declares what it captured.
TEST(CycleCollector, ReclaimsACycleThroughNativeStateAndADeclaredCapture)
{
    const std::size_t before = settled_live_count();
    {
        auto object = make_ref<LumiereObject>();
        auto state = make_ref<HandlerState>();
        object->native_state = state;

        auto handler = make_ref<LumiereFunction>();
        handler->name = "gestionnaire";
        // What the C++ lambda would capture as a raw pointer, declared here.
        handler->native_captures.push_back(object);

        state->handler = Value::fonction(handler);
    }
    EXPECT_GT(RefCounted::live_count(), before);

    EXPECT_GE(collect_cycles(), 3u);
    EXPECT_EQ(RefCounted::live_count(), before);
}

// Under-reporting an edge is the safe direction: it leaks, it never frees
// something live. This pins that the unsafe direction stays impossible to reach
// by accident -- a state that reports nothing keeps its cycle alive rather than
// letting the sweep free an object the handler still points at.
TEST(CycleCollector, LeavesACycleAliveWhenNativeStateReportsNothing)
{
    struct SilentState final : NativeState
    {
        Value handler = Value::rien();

        void trace_references(RefVisitor &) const override {}
        void clear_references() override { handler = Value::rien(); }
    };

    const std::size_t before = settled_live_count();
    auto *leaked = new SilentState();
    {
        auto object = make_ref<LumiereObject>();
        auto state = Ref<SilentState>(leaked);
        object->native_state = state;

        auto handler = make_ref<LumiereFunction>();
        handler->native_captures.push_back(object);
        state->handler = Value::fonction(handler);
    }
    EXPECT_EQ(collect_cycles(), 0u);
    // Still here, and still consistent: the cycle leaks rather than breaking.
    EXPECT_GT(RefCounted::live_count(), before);
    EXPECT_TRUE(leaked->handler.is_fonction());

    // Clean up by hand so the leak does not reach the sanitizer. The handle is
    // not optional: the state is kept alive only by the cycle it is part of, so
    // breaking the cycle from inside it would free it mid-assignment.
    {
        const Ref<SilentState> keep(leaked);
        leaked->handler = Value::rien();
    }
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

namespace
{

/**
 * @brief Collects once after the last test, so leak detection can be enabled.
 *
 * A test that builds an interpreter leaves its environments behind in a cycle,
 * exactly as a program does. The runtime collects those when a backend is
 * destroyed; the tests destroy theirs on the stack and then end, so the sweep
 * has to happen here for the sanitizer's check at exit to see a settled heap.
 */
// Two runtimes on two threads. The collector's state used to be process-global,
// so this is the shape that corrupted it: one thread's collect() swaps the
// candidate buffer into a local while the other's on_destroyed does its
// swap-removal against the new buffer, leaving a freed object in the first
// thread's copy. It crashed about one run in six of a test that happened to do
// this; nothing here was testing it on purpose.
TEST(CycleCollector, KeepsTwoThreadsOutOfEachOthersCollector)
{
    constexpr int thread_count = 4;
    constexpr int cycles_per_thread = 500;

    std::atomic<int> failures{0};
    std::vector<std::thread> threads;
    threads.reserve(thread_count);

    for (int i = 0; i < thread_count; ++i)
    {
        threads.emplace_back([&failures] {
            // Each thread's counts are its own, so its baseline is its own too.
            const std::size_t before = settled_live_count();
            for (int n = 0; n < cycles_per_thread; ++n)
            {
                auto first = make_ref<LumiereObject>();
                auto second = make_ref<LumiereObject>();
                first->fields["autre"] = Value::objet(second);
                second->fields["autre"] = Value::objet(first);
            }
            collect_cycles();
            if (RefCounted::live_count() != before)
            {
                ++failures;
            }
        });
    }

    for (std::thread &worker : threads)
    {
        worker.join();
    }
    EXPECT_EQ(failures.load(), 0);
}

// A thread reclaims what it queued as it ends, because no other thread can: the
// buffer belongs to it. Without that, a worker's cycles would leak, and the
// sanitizer build is what would notice.
TEST(CycleCollector, ReclaimsAThreadsCyclesWhenTheThreadEnds)
{
    const std::size_t before = settled_live_count();
    std::thread worker([] {
        auto first = make_ref<LumiereObject>();
        auto second = make_ref<LumiereObject>();
        first->fields["autre"] = Value::objet(second);
        second->fields["autre"] = Value::objet(first);
        // Deliberately left uncollected: the thread's own teardown must do it.
    });
    worker.join();

    // The worker's objects were never this thread's to count.
    EXPECT_EQ(RefCounted::live_count(), before);
}

class CollectAtExit final : public ::testing::Environment
{
public:
    void TearDown() override
    {
        if (std::getenv("LUMIERE_CYCLE_DEBUG") != nullptr)
        {
            std::fprintf(stderr, "[cycles] avant: vivants=%zu candidats=%zu\n",
                         RefCounted::live_count(), cycle_candidate_count());
            const std::size_t freed = collect_cycles();
            std::fprintf(stderr, "[cycles] apres: vivants=%zu liberes=%zu candidats=%zu\n",
                         RefCounted::live_count(), freed, cycle_candidate_count());
#ifndef NDEBUG
            std::map<std::string, std::size_t> by_type;
            for (const RefCounted *object : live_objects())
            {
                ++by_type[typeid(*object).name()];
            }
            for (const auto &[name, count] : by_type)
            {
                std::fprintf(stderr, "[cycles]   %zu x %s\n", count, name.c_str());
            }
#endif
            return;
        }
        collect_cycles();
    }
};

const bool registered = [] {
    ::testing::AddGlobalTestEnvironment(new CollectAtExit());
    return true;
}();

} // namespace

} // namespace lumiere
