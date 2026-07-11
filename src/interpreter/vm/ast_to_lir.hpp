#pragma once

#include "lumiere/interpreter/iinterpreter.hpp"
#include "lumiere/interpreter/vm/lir.hpp"

#include <unordered_map>

namespace lumiere
{

struct ResolvedVmImport
{
    std::unordered_map<std::string, std::string> export_symbols;
    std::vector<std::string> initializer_symbols;
};

using ResolvedVmImports = std::unordered_map<const ImportStmt *, ResolvedVmImport>;

class AstToLir
{
public:
    LirModule lower(Program &program, const ResolvedVmImports &imports = {});
};

} // namespace lumiere
