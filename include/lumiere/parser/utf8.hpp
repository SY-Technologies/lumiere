#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace lumiere::utf8
{

// In UTF-8, one visible character may use several bytes. These helpers walk
// text one Unicode character at a time so `'é'` behaves like one `Symbole`.
std::optional<std::size_t> decode_one(std::string_view text, std::size_t offset, char32_t &character);

std::optional<char32_t> decode_single_character(std::string_view text);

std::optional<char32_t> character_at(std::string_view text, std::size_t index);

std::optional<std::size_t> character_count(std::string_view text);

std::string encode_character(char32_t character);

} // namespace lumiere::utf8
