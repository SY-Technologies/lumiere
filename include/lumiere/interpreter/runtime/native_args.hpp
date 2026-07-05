#pragma once

#include <cstddef>
#include <vector>

#include "lumiere/interpreter/runtime/runtime_site.hpp"

namespace lumiere
{

struct Value;
struct RuntimeArgument;

// Bundle passed to a native C++ handler when Lumiere code calls it. This gives
// the builtin the same pieces of context a user-defined function would receive:
// an optional bound receiver, the already-evaluated arguments, and the source
// location of the call for diagnostics.
struct NativeArgs
{
    // Bound receiver for method-style calls such as `texte.majuscules()`.
    // Null for free functions such as `afficher("ok")`.
    const Value *receiver = nullptr;
    // Already-evaluated call arguments in source order. Named arguments keep
    // their names inside each RuntimeArgument entry; positional arguments have
    // an empty name.
    const std::vector<RuntimeArgument> *arguments = nullptr;
    // Source location of the Lumiere call that entered this native handler.
    // Native code should reuse this when raising runtime errors so messages
    // point back to user code.
    RuntimeSite site;
};

} // namespace lumiere
