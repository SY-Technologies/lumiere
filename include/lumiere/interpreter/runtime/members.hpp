#pragma once

#include <optional>
#include <string_view>
#include <vector>

#include "lumiere/interpreter/runtime/iruntime.hpp"
#include "lumiere/interpreter/runtime/runtime_argument.hpp"

namespace lumiere
{

/**
 * @brief A member a value carries by virtue of its type, such as `Liste.ajouter`.
 *
 * Looking a member up and running it are separate because the engines do them
 * at different moments: the tree walker resolves a member where it is written
 * and binds it to its receiver, while the VM resolves it when the call runs.
 * Both then reach the same body, which is the point of this file.
 */
struct BuiltinMember;

/** @brief The builtin member of @p receiver named @p member, or nullptr. */
const BuiltinMember *find_builtin_member(const Value &receiver, std::string_view member);

/** @brief Runs @p member on @p receiver, after checking how it was called. */
Value call_builtin_member(IRuntime &runtime,
                          const BuiltinMember &member,
                          const Value &receiver,
                          const std::vector<RuntimeArgument> &args,
                          const RuntimeSite &site);

/**
 * @brief Looks @p member up on @p receiver and runs it.
 *
 * `std::nullopt` means @p receiver has no builtin member of that name, which is
 * what lets each engine fall through to its own object-and-method dispatch.
 */
std::optional<Value> call_builtin_member(IRuntime &runtime,
                                         const Value &receiver,
                                         std::string_view member,
                                         const std::vector<RuntimeArgument> &args,
                                         const RuntimeSite &site);

} // namespace lumiere
