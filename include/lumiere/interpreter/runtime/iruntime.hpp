#pragma once

#include <string>
#include <string_view>

#include "lumiere/interpreter/runtime/native_args.hpp"

namespace lumiere
{

struct Value;

class IRuntime
{
public:
    virtual ~IRuntime() = default;

    /**
     * Calls `callee` with the already-normalized runtime arguments stored in `args`.
     *
     * `callee` is expected to be a Lumiere callable value.
     * `args.receiver` is the bound receiver when the call originates from a method form.
     * `args.arguments` points to the evaluated arguments for the call.
     * `args.site` identifies the source location of the call for diagnostics.
     *
     * Implementations decide how to dispatch the call:
     * native call, tree-walker function execution, or VM frame execution.
     */
    virtual Value call(Value callee, const NativeArgs &args) = 0;

    /**
     * Raises a runtime error for `message` at `site`.
     *
     * `site.source_path`, `site.line`, and `site.column` are the source coordinates
     * that should appear in the final diagnostic when available.
     *
     * This method must not return.
     * Implementations are responsible for attaching traceback and backend-specific context.
     */
    [[noreturn]] virtual void raise_runtime_error(const RuntimeSite &site, const std::string &message) const = 0;

    /**
     * Returns whether `left` and `right` are equal under Lumiere equality semantics.
     *
     * This is the equality operation stdlib code should use.
     */
    virtual bool is_equal(const Value &left, const Value &right) const = 0;

    /**
     * Converts `value` to the Lumiere text form used by the runtime.
     *
     * This is the stringification path stdlib code should use when it needs
     * language-level text conversion.
     */
    virtual std::string to_text(const Value &value) const = 0;

    /**
     * Attaches runtime metadata to `value`.
     *
     * `type_name` is the language-facing annotation being attached, such as `Liste[Texte]`.
     * `site` is the source location responsible for creating or annotating the value.
     *
     * Backends own the storage strategy for this metadata, but stdlib code uses
     * this hook to preserve language-level type information on produced values.
     */
    virtual void annotate_value(const Value &value, std::string_view type_name, const RuntimeSite &site) const = 0;
};

} // namespace lumiere
