#pragma once

#include <vector>
#include "lumiere/parser/ast.hpp"

namespace lumiere {
struct Program {
    std::vector<StmtPtr> statements;
    std::string          source_path;
    std::string          source_text;   // for error reporting
};

class Backend {
public:
    virtual ~Backend() = default;
    virtual void execute(Program &program) = 0;
};

}// namespace lumiere

