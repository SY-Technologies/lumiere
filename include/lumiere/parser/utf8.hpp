#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace lumiere::utf8
{

// In UTF-8, one visible character may use several bytes. These helpers walk
// text one Unicode character at a time so `'é'` behaves like one `Symbole`.
// `decode_one(...)` returns the byte offset immediately after the decoded
// character so callers can keep scanning the original byte string without
// copying substrings.
std::optional<std::size_t> decode_one(std::string_view text, std::size_t offset, char32_t &character);

// Accepts exactly one Unicode character encoded as UTF-8; rejects empty text,
// invalid byte sequences, and strings containing more than one character.
std::optional<char32_t> decode_single_character(std::string_view text);

// Returns the Nth Unicode character, where `index` counts characters rather
// than bytes.
std::optional<char32_t> character_at(std::string_view text, std::size_t index);

// Counts Unicode characters in the string and fails if any byte sequence is
// malformed UTF-8.
std::optional<std::size_t> character_count(std::string_view text);

// Encodes one Unicode scalar value back into its canonical UTF-8 byte sequence.
std::string encode_character(char32_t character);

} // namespace lumiere::utf8
