#pragma once

#include <cstddef>
#include <optional>
#include <string>

namespace lumiere
{

struct Inspection
{
    std::string label;
    std::string detail;
    std::size_t start_offset = 0;
    std::size_t end_offset = 0;
};

/** Returns compiler-owned hover information for the token at a UTF-8 byte offset. */
[[nodiscard]] std::optional<Inspection> inspect_source(const std::string &source, std::size_t byte_offset);

/** Serializes an inspection response for editor tooling. */
[[nodiscard]] std::string inspection_to_json(const std::optional<Inspection> &inspection);

} // namespace lumiere
