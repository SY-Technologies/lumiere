#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace lumiere
{

enum class SemanticTypeKind
{
    BUILTIN,
    CLASS,
    INTERFACE,
    GENERIC,
    UNION,
    TYPE_PARAMETER,
    INTEGER_ARGUMENT,
    BOTTOM,
};

class SemanticType;
using SemanticTypeRef = std::shared_ptr<const SemanticType>;

class SemanticType final
{
public:
    [[nodiscard]] SemanticTypeKind kind() const noexcept;
    [[nodiscard]] std::string_view name() const noexcept;
    [[nodiscard]] const std::vector<SemanticTypeRef> &arguments() const noexcept;
    [[nodiscard]] std::uint64_t integer() const noexcept;
    [[nodiscard]] std::string_view display() const noexcept;
    [[nodiscard]] std::size_t hash() const noexcept;

private:
    friend class TypeInterner;

    SemanticType(SemanticTypeKind kind,
                 std::string name,
                 std::vector<SemanticTypeRef> arguments,
                 std::uint64_t integer,
                 std::string key,
                 std::string display);

    SemanticTypeKind m_kind;
    std::string m_name;
    std::vector<SemanticTypeRef> m_arguments;
    std::uint64_t m_integer;
    std::string m_key;
    std::string m_display;
};

class TypeInterner final
{
public:
    [[nodiscard]] SemanticTypeRef builtin(std::string name);
    [[nodiscard]] SemanticTypeRef class_type(std::string qualified_name);
    [[nodiscard]] SemanticTypeRef interface_type(std::string qualified_name);
    [[nodiscard]] SemanticTypeRef type_parameter(std::string name);
    [[nodiscard]] SemanticTypeRef integer_argument(std::uint64_t value);
    [[nodiscard]] SemanticTypeRef generic(std::string name,
                                          std::vector<SemanticTypeRef> arguments);
    [[nodiscard]] SemanticTypeRef union_type(std::vector<SemanticTypeRef> alternatives);
    [[nodiscard]] SemanticTypeRef bottom();

    /**
     * @brief Takes on another interner's types, so refs from both compare equal.
     *
     * `same_type` compares by pointer, which is only meaningful among types one
     * interner produced. Two interners that each interned "Entier" disagree
     * about it. The shell analyzes one submission at a time and the next one
     * has to mean the same thing by a type as the last, so its analysis starts
     * from the previous interner's table -- the same shared objects, not copies
     * of them.
     */
    void adopt(const TypeInterner &other);

private:
    [[nodiscard]] SemanticTypeRef intern(SemanticTypeKind kind,
                                         std::string name,
                                         std::vector<SemanticTypeRef> arguments,
                                         std::uint64_t integer);

    std::unordered_map<std::string, SemanticTypeRef> m_types;
};

[[nodiscard]] bool same_type(const SemanticTypeRef &left,
                             const SemanticTypeRef &right) noexcept;

struct SemanticTypeRefHash
{
    [[nodiscard]] std::size_t operator()(const SemanticTypeRef &type) const noexcept;
};

struct SemanticTypeRefEqual
{
    [[nodiscard]] bool operator()(const SemanticTypeRef &left,
                                  const SemanticTypeRef &right) const noexcept;
};

} // namespace lumiere
