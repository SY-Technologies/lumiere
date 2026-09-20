#include "lumiere/diagnostics/runtime_stats.hpp"

#include <cstdlib>
#include <new>

#if defined(__unix__) || defined(__APPLE__)
#include <sys/resource.h>
#endif

// AddressSanitizer replaces the allocation operators itself to pair each new
// with the matching delete. Replacing them here would take that check away, and
// a counter is not worth a correctness check: under the sanitizers the counts
// read zero and the suite keeps everything it had.
#if defined(__SANITIZE_ADDRESS__)
#define LUMIERE_COUNT_ALLOCATIONS 0
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#define LUMIERE_COUNT_ALLOCATIONS 0
#else
#define LUMIERE_COUNT_ALLOCATIONS 1
#endif
#else
#define LUMIERE_COUNT_ALLOCATIONS 1
#endif

namespace lumiere::stats
{
namespace detail
{

// Not atomic, for the same reason the reference counts are not: a runtime and
// everything it makes live on one thread. A second thread would cost an
// inaccurate number and nothing else. They sit here rather than in an unnamed
// namespace because the replaced operators below are outside lumiere::stats.
std::uint64_t allocations = 0;
std::uint64_t bytes = 0;

} // namespace detail

std::uint64_t allocations() noexcept { return detail::allocations; }

std::uint64_t allocated_bytes() noexcept { return detail::bytes; }

bool counting_allocations() noexcept { return LUMIERE_COUNT_ALLOCATIONS != 0; }

std::uint64_t peak_resident_bytes() noexcept
{
#if defined(__unix__) || defined(__APPLE__)
    rusage usage{};
    if (getrusage(RUSAGE_SELF, &usage) != 0)
    {
        return 0;
    }
#if defined(__APPLE__)
    return static_cast<std::uint64_t>(usage.ru_maxrss);  // bytes
#else
    return static_cast<std::uint64_t>(usage.ru_maxrss) * 1024;  // kilobytes
#endif
#else
    return 0;
#endif
}

} // namespace lumiere::stats

#if LUMIERE_COUNT_ALLOCATIONS

namespace
{

void *allocate(const std::size_t size)
{
    ++lumiere::stats::detail::allocations;
    lumiere::stats::detail::bytes += size;
    return std::malloc(size == 0 ? 1 : size);
}

} // namespace

void *operator new(const std::size_t size)
{
    if (void *memory = allocate(size))
    {
        return memory;
    }
    throw std::bad_alloc();
}

void *operator new[](const std::size_t size) { return ::operator new(size); }

void *operator new(const std::size_t size, const std::nothrow_t &) noexcept
{
    return allocate(size);
}

void *operator new[](const std::size_t size, const std::nothrow_t &tag) noexcept
{
    return ::operator new(size, tag);
}

void operator delete(void *memory) noexcept { std::free(memory); }
void operator delete[](void *memory) noexcept { std::free(memory); }
void operator delete(void *memory, std::size_t) noexcept { std::free(memory); }
void operator delete[](void *memory, std::size_t) noexcept { std::free(memory); }
void operator delete(void *memory, const std::nothrow_t &) noexcept { std::free(memory); }
void operator delete[](void *memory, const std::nothrow_t &) noexcept { std::free(memory); }

#endif
