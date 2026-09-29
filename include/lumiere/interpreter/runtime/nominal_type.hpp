#pragma once

#include "lumiere/lexer/token.hpp"
#include <filesystem>
#include <string>
#include <string_view>

namespace lumiere
{

// Keep names readable, but distinguish declarations even when imports rename them.
// Hex encoding avoids introducing generic/union delimiters from filesystem paths.
inline std::string nominal_type_identity(const std::string &source_path, const Token &name)
{
    const std::string path = source_path.empty() ? std::string{} :
        std::filesystem::weakly_canonical(source_path).string();
    constexpr char hex[] = "0123456789abcdef";
    std::string identity = name.lexeme + '@';
    for (unsigned char byte : path)
    {
        identity += hex[byte >> 4];
        identity += hex[byte & 15];
    }
    return identity + ':' + std::to_string(name.start_offset);
}

inline std::string native_nominal_type_identity(std::string_view module,
                                                std::string_view name)
{
    constexpr char hex[] = "0123456789abcdef";
    std::string identity(name);
    identity += "@native:";
    for (unsigned char byte : module)
    {
        identity += hex[byte >> 4];
        identity += hex[byte & 15];
    }
    return identity;
}

inline std::string native_nominal_type_identity(std::string_view qualified_name)
{
    const auto separator = qualified_name.rfind('.');
    if (separator == std::string_view::npos)
        return native_nominal_type_identity({}, qualified_name);
    return native_nominal_type_identity(
        qualified_name.substr(0, separator),
        qualified_name.substr(separator + 1));
}

inline std::string display_runtime_type(std::string_view type)
{
    std::string display;
    for (std::size_t i = 0; i < type.size();)
    {
        if (type[i] != '@')
            display += type[i++];
        else
        {
            const auto end = type.find_first_of("[],| \t\r\n", i);
            i = end == std::string_view::npos ? type.size() : end;
        }
    }
    return display;
}

} // namespace lumiere
