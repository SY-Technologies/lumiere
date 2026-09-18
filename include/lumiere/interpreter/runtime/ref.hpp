#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <utility>

namespace lumiere
{

/**
 * @brief Base for every heap value the runtime owns.
 *
 * The count lives in the object rather than in a separate control block, and it
 * is not atomic. The runtime has no threads — nothing in src/ creates one — so
 * the atomic read-modify-write that std::shared_ptr performs was paid on every
 * copy of every value for a guarantee nothing needed. Releasing a shared_ptr
 * also calls out of line, which a compare-and-branch here does not.
 *
 * A subclass must never be deleted through a raw pointer while references
 * remain; hand it to Ref and let the count decide.
 */
class RefCounted
{
public:
    virtual ~RefCounted() = default;

    void retain() const noexcept { ++m_count; }

    /** @brief Drops one reference, destroying the object when it was the last. */
    void release() const noexcept
    {
        if (--m_count == 0)
        {
            destroy();
        }
    }

    [[nodiscard]] std::uint32_t use_count() const noexcept { return m_count; }

protected:
    RefCounted() = default;
    // A copy is a new object, so it starts with its own count rather than the
    // original's.
    RefCounted(const RefCounted &) noexcept {}
    RefCounted &operator=(const RefCounted &) noexcept { return *this; }
    RefCounted(RefCounted &&) noexcept {}
    RefCounted &operator=(RefCounted &&) noexcept { return *this; }

private:
    // Defined out of line on purpose. Destroying the last reference is cold and
    // ends in a virtual call, and inlining that into every place a value is
    // released bloated the interpreter's dispatch loop enough to show up on the
    // integer benchmark.
    void destroy() const noexcept;

    mutable std::uint32_t m_count = 0;
};

/**
 * @brief Owning handle to a RefCounted, with the shape of std::shared_ptr.
 *
 * Supports the operations the runtime actually used, so call sites read the
 * same: `->`, `*`, `get()`, a boolean test, and comparison against another
 * handle or nullptr.
 */
template <typename T>
class Ref
{
public:
    Ref() noexcept = default;
    Ref(std::nullptr_t) noexcept {}

    /** @brief Takes shared ownership of @p raw, which may be null. */
    explicit Ref(T *raw) noexcept : m_raw(raw)
    {
        if (m_raw != nullptr)
        {
            m_raw->retain();
        }
    }

    Ref(const Ref &other) noexcept : m_raw(other.m_raw)
    {
        if (m_raw != nullptr)
        {
            m_raw->retain();
        }
    }

    Ref(Ref &&other) noexcept : m_raw(other.m_raw) { other.m_raw = nullptr; }

    /** @brief Converts from a handle to a more derived or less const type. */
    template <typename U, typename = std::enable_if_t<std::is_convertible_v<U *, T *>>>
    Ref(const Ref<U> &other) noexcept : m_raw(other.get())
    {
        if (m_raw != nullptr)
        {
            m_raw->retain();
        }
    }

    Ref &operator=(const Ref &other) noexcept
    {
        // Retain first, so self-assignment and aliasing cannot free the object
        // before it is stored.
        if (other.m_raw != nullptr)
        {
            other.m_raw->retain();
        }
        reset_raw();
        m_raw = other.m_raw;
        return *this;
    }

    Ref &operator=(Ref &&other) noexcept
    {
        if (this != &other)
        {
            reset_raw();
            m_raw = other.m_raw;
            other.m_raw = nullptr;
        }
        return *this;
    }

    ~Ref() { reset_raw(); }

    void reset() noexcept
    {
        reset_raw();
        m_raw = nullptr;
    }

    [[nodiscard]] T *get() const noexcept { return m_raw; }
    T *operator->() const noexcept { return m_raw; }
    T &operator*() const noexcept { return *m_raw; }
    explicit operator bool() const noexcept { return m_raw != nullptr; }

    friend bool operator==(const Ref &left, const Ref &right) noexcept { return left.m_raw == right.m_raw; }
    friend bool operator!=(const Ref &left, const Ref &right) noexcept { return left.m_raw != right.m_raw; }
    friend bool operator==(const Ref &left, std::nullptr_t) noexcept { return left.m_raw == nullptr; }
    friend bool operator!=(const Ref &left, std::nullptr_t) noexcept { return left.m_raw != nullptr; }
    friend bool operator==(std::nullptr_t, const Ref &right) noexcept { return right.m_raw == nullptr; }
    friend bool operator!=(std::nullptr_t, const Ref &right) noexcept { return right.m_raw != nullptr; }

private:
    void reset_raw() noexcept
    {
        if (m_raw != nullptr)
        {
            m_raw->release();
        }
    }

    T *m_raw = nullptr;
};

/** @brief Allocates a T and returns the first handle to it. */
template <typename T, typename... Arguments>
[[nodiscard]] Ref<T> make_ref(Arguments &&...arguments)
{
    return Ref<T>(new T(std::forward<Arguments>(arguments)...));
}

} // namespace lumiere

template <typename T>
struct std::hash<lumiere::Ref<T>>
{
    std::size_t operator()(const lumiere::Ref<T> &ref) const noexcept
    {
        return std::hash<const void *>{}(static_cast<const void *>(ref.get()));
    }
};
