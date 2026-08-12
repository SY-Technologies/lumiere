#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace lumiere
{

struct Inspection
{
    // Display name of the symbol under the cursor.
    std::string label;
    // Declaration category: "fonction", "variable", "classe", "interface",
    // "alias de type", "variable de boucle", "mot-clé" or a stdlib kind.
    std::string kind;
    // One-line declarative signature rendered in a code block.
    std::string signature;
    // Formatted parameters, each "name : Type".
    std::vector<std::string> parameters;
    // Returned type, empty for values that return nothing meaningful.
    std::string return_type;
    // Free-form documentation from source comments or the stdlib registry.
    std::string documentation;
    std::size_t start_offset = 0;
    std::size_t end_offset = 0;
};

/** Returns compiler-owned hover information for the token at a UTF-8 byte offset. */
[[nodiscard]] std::optional<Inspection> inspect_source(const std::string &source, std::size_t byte_offset);

/** Serializes an inspection response for editor tooling. */
[[nodiscard]] std::string inspection_to_json(const std::optional<Inspection> &inspection);

} // namespace lumiere
