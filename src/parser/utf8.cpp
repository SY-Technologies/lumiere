#include "lumiere/parser/utf8.hpp"

namespace lumiere::utf8
{

namespace
{

bool is_continuation(unsigned char byte)
{
    // Continuation bytes always have the `10xxxxxx` shape. We keep this check
    // separate because every multi-byte branch relies on it.
    return (byte & 0xC0) == 0x80;
}

} // namespace

std::optional<std::size_t> decode_one(std::string_view text, std::size_t offset, char32_t &character)
{
    if (offset >= text.size())
    {
        return std::nullopt;
    }

    const unsigned char first = static_cast<unsigned char>(text[offset]);
    std::size_t width = 0;
    char32_t value = 0;

    // The leading byte tells us both how many bytes this character occupies
    // and which payload bits belong to the scalar value itself.
    if (first <= 0x7F)
    {
        width = 1;
        value = first;
    }
    else if ((first & 0xE0) == 0xC0)
    {
        width = 2;
        value = first & 0x1F;
    }
    else if ((first & 0xF0) == 0xE0)
    {
        width = 3;
        value = first & 0x0F;
    }
    else if ((first & 0xF8) == 0xF0)
    {
        width = 4;
        value = first & 0x07;
    }
    else
    {
        return std::nullopt;
    }

    // Reject truncated input before reading continuation bytes.
    if (offset + width > text.size())
    {
        return std::nullopt;
    }

    for (std::size_t i = 1; i < width; ++i)
    {
        const unsigned char byte = static_cast<unsigned char>(text[offset + i]);
        if (!is_continuation(byte))
        {
            return std::nullopt;
        }
        // Each continuation byte contributes six payload bits.
        value = (value << 6) | (byte & 0x3F);
    }

    // Reject non-canonical encodings and Unicode values that are not valid
    // scalar values. This keeps one character mapped to one canonical UTF-8
    // byte sequence and avoids accepting UTF-16 surrogate halves.
    if ((width == 2 && value < 0x80) ||
        (width == 3 && value < 0x800) ||
        (width == 4 && value < 0x10000) ||
        value > 0x10FFFF ||
        (value >= 0xD800 && value <= 0xDFFF))
    {
        return std::nullopt;
    }

    character = value;
    return offset + width;
}

std::optional<char32_t> decode_single_character(std::string_view text)
{
    char32_t character = 0;
    const std::optional<std::size_t> next = decode_one(text, 0, character);
    // A valid first character is not enough here: callers use this helper for
    // places where the entire string must be exactly one Unicode character.
    if (!next.has_value() || *next != text.size())
    {
        return std::nullopt;
    }
    return character;
}

std::optional<char32_t> character_at(std::string_view text, std::size_t index)
{
    std::size_t offset = 0;
    std::size_t current = 0;

    while (offset < text.size())
    {
        char32_t character = 0;
        const std::optional<std::size_t> next = decode_one(text, offset, character);
        if (!next.has_value())
        {
            return std::nullopt;
        }
        // `current` counts Unicode characters, not byte positions, so callers
        // can index user-visible symbols in multi-byte text.
        if (current == index)
        {
            return character;
        }
        offset = *next;
        ++current;
    }

    return std::nullopt;
}

std::optional<std::size_t> character_count(std::string_view text)
{
    std::size_t offset = 0;
    std::size_t count = 0;

    while (offset < text.size())
    {
        char32_t character = 0;
        const std::optional<std::size_t> next = decode_one(text, offset, character);
        if (!next.has_value())
        {
            return std::nullopt;
        }
        // Advancing by the returned byte offset lets us count characters
        // without materializing substrings or decoding twice.
        offset = *next;
        ++count;
    }

    return count;
}

std::string encode_character(char32_t character)
{
    std::string result;

    if (character <= 0x7F)
    {
        result.push_back(static_cast<char>(character));
    }
    else if (character <= 0x7FF)
    {
        // For multi-byte encodings, the leading byte carries the high payload
        // bits and the remaining bytes store six bits each in `10xxxxxx` form.
        result.push_back(static_cast<char>(0xC0 | ((character >> 6) & 0x1F)));
        result.push_back(static_cast<char>(0x80 | (character & 0x3F)));
    }
    else if (character <= 0xFFFF)
    {
        result.push_back(static_cast<char>(0xE0 | ((character >> 12) & 0x0F)));
        result.push_back(static_cast<char>(0x80 | ((character >> 6) & 0x3F)));
        result.push_back(static_cast<char>(0x80 | (character & 0x3F)));
    }
    else
    {
        result.push_back(static_cast<char>(0xF0 | ((character >> 18) & 0x07)));
        result.push_back(static_cast<char>(0x80 | ((character >> 12) & 0x3F)));
        result.push_back(static_cast<char>(0x80 | ((character >> 6) & 0x3F)));
        result.push_back(static_cast<char>(0x80 | (character & 0x3F)));
    }

    return result;
}

} // namespace lumiere::utf8
