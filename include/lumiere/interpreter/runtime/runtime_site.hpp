#pragma once

#include <string>

namespace lumiere
{

struct RuntimeSite
{
    // Source file that triggered the current runtime action. Native helpers use
    // this when they need to report an error "at the call site" rather than at
    // the location of the C++ builtin implementation.
    std::string source_path;
    // 1-based line number of the Lumiere call site when available. For
    // synthetic entry points such as the implicit principal() call this may be 0.
    int line = 0;
    // 1-based column number of the Lumiere call site when available.
    int column = 0;
};

} // namespace lumiere
