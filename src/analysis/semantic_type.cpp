#include "lumiere/analysis/semantic_type.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace lumiere
{
namespace
{

char kind_tag(const SemanticTypeKind kind)
{
    switch (kind)
    {
    case SemanticTypeKind::BUILTIN:
        return 'b';
    case SemanticTypeKind::CLASS:
        return 'c';
    case SemanticTypeKind::INTERFACE:
        return 'i';
    case SemanticTypeKind::GENERIC:
        return 'g';
    case SemanticTypeKind::UNION:
        return 'u';
    case SemanticTypeKind::TYPE_PARAMETER:
        return 'p';
    case SemanticTypeKind::INTEGER_ARGUMENT:
        return 'n';
    case SemanticTypeKind::BOTTOM:
        return 'x';
    }
    return 'x';
}

std::string encode_part(const std::string_view value)
{
    return std::to_string(value.size()) + ':' + std::string(value);
}

std::string make_display(const SemanticTypeKind kind,
                         const std::string &name,
                         const std::vector<SemanticTypeRef> &arguments,
                         const std::uint64_t integer)
{
    if (kind == SemanticTypeKind::INTEGER_ARGUMENT)
    {
        return std::to_string(integer);
    }
    if (kind == SemanticTypeKind::BOTTOM)
    {
        return "Jamais";
    }
    if (kind == SemanticTypeKind::UNION)
    {
        std::string display;
        for (std::size_t i = 0; i < arguments.size(); ++i)
        {
            if (i > 0)
            {
                display += " | ";
            }
            display += arguments[i]->display();
        }
        return display;
    }
    if (kind != SemanticTypeKind::GENERIC)
    {
        return name;
    }

    std::string display = name + '[';
    for (std::size_t i = 0; i < arguments.size(); ++i)
    {
        if (i > 0)
        {
            display += ',';
        }
        display += arguments[i]->display();
    }
    display += ']';
    return display;
}

} // namespace

SemanticType::SemanticType(const SemanticTypeKind kind,
                           std::string name,
                           std::vector<SemanticTypeRef> arguments,
                           const std::uint64_t integer,
                           std::string key,
                           std::string display)
    : m_kind(kind),
      m_name(std::move(name)),
      m_arguments(std::move(arguments)),
      m_integer(integer),
      m_key(std::move(key)),
      m_display(std::move(display))
{
}

SemanticTypeKind SemanticType::kind() const noexcept
{
    return m_kind;
}

std::string_view SemanticType::name() const noexcept
{
    return m_name;
}

const std::vector<SemanticTypeRef> &SemanticType::arguments() const noexcept
{
    return m_arguments;
}

std::uint64_t SemanticType::integer() const noexcept
{
    return m_integer;
}

std::string_view SemanticType::display() const noexcept
{
    return m_display;
}

std::size_t SemanticType::hash() const noexcept
{
    std::size_t hash = sizeof(std::size_t) == 8
                           ? static_cast<std::size_t>(14695981039346656037ull)
                           : static_cast<std::size_t>(2166136261u);
    const std::size_t prime = sizeof(std::size_t) == 8
                                  ? static_cast<std::size_t>(1099511628211ull)
                                  : static_cast<std::size_t>(16777619u);
    for (const unsigned char byte : m_key)
    {
        hash ^= byte;
        hash *= prime;
    }
    return hash;
}

SemanticTypeRef TypeInterner::builtin(std::string name)
{
    return intern(SemanticTypeKind::BUILTIN, std::move(name), {}, 0);
}

SemanticTypeRef TypeInterner::class_type(std::string qualified_name)
{
    return intern(SemanticTypeKind::CLASS, std::move(qualified_name), {}, 0);
}

SemanticTypeRef TypeInterner::interface_type(std::string qualified_name)
{
    return intern(SemanticTypeKind::INTERFACE, std::move(qualified_name), {}, 0);
}

SemanticTypeRef TypeInterner::type_parameter(std::string name)
{
    return intern(SemanticTypeKind::TYPE_PARAMETER, std::move(name), {}, 0);
}

SemanticTypeRef TypeInterner::integer_argument(const std::uint64_t value)
{
    return intern(SemanticTypeKind::INTEGER_ARGUMENT, {}, {}, value);
}

SemanticTypeRef TypeInterner::generic(std::string name,
                                      std::vector<SemanticTypeRef> arguments)
{
    if (arguments.empty())
    {
        throw std::invalid_argument("un type générique requiert au moins un argument");
    }
    return intern(SemanticTypeKind::GENERIC, std::move(name), std::move(arguments), 0);
}

SemanticTypeRef TypeInterner::union_type(std::vector<SemanticTypeRef> alternatives)
{
    if (alternatives.empty())
    {
        throw std::invalid_argument("une union requiert au moins un type");
    }
    std::vector<SemanticTypeRef> normalized;
    for (const SemanticTypeRef &alternative : alternatives)
    {
        if (alternative == nullptr)
        {
            throw std::invalid_argument("une union ne peut pas contenir un type nul");
        }
        if (alternative->kind() == SemanticTypeKind::UNION)
        {
            normalized.insert(normalized.end(),
                              alternative->arguments().begin(),
                              alternative->arguments().end());
        }
        else
        {
            normalized.push_back(alternative);
        }
    }
    std::sort(normalized.begin(), normalized.end(),
              [](const SemanticTypeRef &left, const SemanticTypeRef &right) {
                  return left->display() < right->display();
              });
    normalized.erase(std::unique(normalized.begin(), normalized.end()), normalized.end());
    if (normalized.size() == 1)
    {
        return normalized.front();
    }
    return intern(SemanticTypeKind::UNION, {}, std::move(normalized), 0);
}

SemanticTypeRef TypeInterner::bottom()
{
    return intern(SemanticTypeKind::BOTTOM, {}, {}, 0);
}

SemanticTypeRef TypeInterner::intern(const SemanticTypeKind kind,
                                     std::string name,
                                     std::vector<SemanticTypeRef> arguments,
                                     const std::uint64_t integer)
{
    std::string key(1, kind_tag(kind));
    key += encode_part(name);
    key += encode_part(std::to_string(integer));
    for (const SemanticTypeRef &argument : arguments)
    {
        if (argument == nullptr)
        {
            throw std::invalid_argument("un type sémantique ne peut pas contenir un argument nul");
        }
        key += encode_part(argument->m_key);
    }
    if (const auto found = m_types.find(key); found != m_types.end())
    {
        return found->second;
    }

    const std::string display = make_display(kind, name, arguments, integer);
    auto type = std::shared_ptr<const SemanticType>(
        new SemanticType(kind,
                         std::move(name),
                         std::move(arguments),
                         integer,
                         key,
                         display));
    m_types.emplace(key, type);
    return type;
}

bool same_type(const SemanticTypeRef &left, const SemanticTypeRef &right) noexcept
{
    return left == right;
}

std::size_t SemanticTypeRefHash::operator()(const SemanticTypeRef &type) const noexcept
{
    return type == nullptr ? 0 : type->hash();
}

bool SemanticTypeRefEqual::operator()(const SemanticTypeRef &left,
                                      const SemanticTypeRef &right) const noexcept
{
    return same_type(left, right);
}

} // namespace lumiere
