#pragma once

#include <filesystem>
#include <string>
#include <utility>
#include <vector>

#include "lumiere/interpreter/runtime/iruntime.hpp"
#include "lumiere/interpreter/runtime/runtime_argument.hpp"
#include "lumiere/interpreter/stdlib/modules.hpp"

namespace lumiere
{

    std::filesystem::path stdlib_expect_path_arg(IRuntime &runtime,
                                                 const std::vector<RuntimeArgument> &args,
                                                 const std::string &signature,
                                                 const RuntimeSite &call_site);
    void stdlib_expect_positional(IRuntime &runtime,
                                  const std::vector<RuntimeArgument> &args,
                                  std::size_t expected,
                                  const std::string &signature,
                                  const RuntimeSite &call_site);
    void stdlib_expect_positional_range(IRuntime &runtime,
                                        const std::vector<RuntimeArgument> &args,
                                        std::size_t min_count,
                                        std::size_t max_count,
                                        const std::string &signature,
                                        const RuntimeSite &call_site);
    std::string stdlib_expect_text(IRuntime &runtime,
                                   const Value &value,
                                   const std::string &context,
                                   const RuntimeSite &call_site);
    int64_t stdlib_expect_integer(IRuntime &runtime,
                                  const Value &value,
                                  const std::string &context,
                                  const RuntimeSite &call_site);
    double stdlib_expect_decimal(IRuntime &runtime,
                                 const Value &value,
                                 const std::string &context,
                                 const RuntimeSite &call_site);

    std::pair<std::string, std::string> stdlib_expect_two_text_args(IRuntime &runtime,
                                                                    const std::vector<RuntimeArgument> &args,
                                                                    const std::string &signature,
                                                                    const std::string &first_label,
                                                                    const std::string &second_label,
                                                                    const RuntimeSite &call_site);

    void stdlib_throw_filesystem_failure(IRuntime &runtime,
                                         const RuntimeSite &call_site,
                                         const std::string &signature,
                                         const std::string &message);

    void stdlib_bind_public_value(Module &module, const std::string &name, const Value &value);
    void stdlib_bind_public_function(Module &module,
                                     const NativeFunctionFactory &make_native_function,
                                     const std::string &name,
                                     LumiereFunction::NativeHandler handler);

} // namespace lumiere
