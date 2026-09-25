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
    /** A `pour` loop's own binding -- kept distinct from VARIABLE because
     *  it has no backing VarDeclStmt (there's no `soit`/`fixe` in `pour x
     *  dans ...`), so it needs its own hover formatting
     *  (inspection.cpp's inspection_from_symbol). */
    LOOP_VARIABLE,
    /** A `type X = ...` alias. Declared in SymbolNamespace::Type like CLASS
     *  and INTERFACE, but its Symbol::type starts null and is filled in by
     *  SemanticIndex::set_type once resolve_alias resolves the target --
     *  aliases can forward-reference each other, so the resolved type isn't
     *  known at the same collect_module_declarations pass that declares it. */
    TYPE_ALIAS,
};

} // namespace lumiere
