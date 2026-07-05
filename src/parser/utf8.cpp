#include "lumiere/parser/utf8.hpp"

namespace lumiere::utf8
{

namespace
{

bool is_continuation(unsigned char byte)
{
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
        value = (value << 6) | (byte & 0x3F);
    }

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
