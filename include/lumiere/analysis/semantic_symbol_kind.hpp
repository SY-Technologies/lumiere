#pragma once

namespace lumiere
{

/**
 * @brief What kind of thing a declared name refers to.
 *
 * Split into its own header so both `semantic_analysis.hpp` (the existing
 * flat `SemanticModel::m_value_symbols` table) and `semantic_index.hpp`
 * (the new per-declaration `Symbol` records) can depend on it without one
 * of those two headers having to include the other.
 */
enum class SemanticSymbolKind
{
    VARIABLE,
    PARAMETER,
    FUNCTION,
    CLASS,
    INTERFACE,
    MODULE,
};

} // namespace lumiere
