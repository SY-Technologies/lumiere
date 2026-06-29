#pragma once

#include <string>

namespace lumiere
{

struct RuntimeSite
{
    std::string source_path;
    int line = 0;
    int column = 0;
};

} // namespace lumiere
