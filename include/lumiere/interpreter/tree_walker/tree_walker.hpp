#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "lumiere/interpreter/runtime/iruntime.hpp"
#include "lumiere/interpreter/runtime/runtime_argument.hpp"
#include "lumiere/interpreter/stdlib/modules.hpp"
#include "lumiere/interpreter/iinterpreter.hpp"
#include "lumiere/interpreter/tree_walker/environment.hpp"
#include "lumiere/interpreter/tree_walker/runtime.hpp"
#include "lumiere/interpreter/runtime/value.hpp"

namespace lumiere
{

    class TreeWalker : public Backend,
                       public IRuntime,
                       public ExprVisitor,
                       public StmtVisitor
    {
    public:
        /**
         * @brief Constructs a tree-walking interpreter backend.
         */
        TreeWalker();

        /**
         * @brief Releases interpreter-owned runtime state.
         */
        ~TreeWalker() override;

        // Backend interface
        /**
         * @brief Executes a parsed program from top to bottom.
         *
         * Resets the interpreter state, installs a fresh global environment,
         * evaluates every top-level statement, and calls `principal()` if it
         * exists and is a function.
         */
        void execute(Program &program) override;

        /** Executes one REPL submission in the existing global environment. */
        std::optional<Value> execute_incremental(Program &program);

        /**
         * @brief Adds a filesystem location to the module import search list.
         */
        void add_import_path(std::filesystem::path path);

        /**
         * @brief Calls a function value.
         *
         * Native functions are called directly. User-declared functions are
         * executed through the tree walker.
         */
        Value call(Value callee, const NativeArgs &args) override;

        /**
         * @brief Throws a runtime error at the given source location.
         */
        [[noreturn]] void raise_runtime_error(const RuntimeSite &site, const std::string &message) const override;

        /**
         * @brief Compares two runtime values using Lumiere equality rules.
         */
        bool is_equal(const Value &left, const Value &right) const override;

        /**
         * @brief Converts a value to text.
         */
        std::string to_text(const Value &value) const override;

        /**
         * @brief Stores a Lumiere type annotation on an already-created runtime value.
         *
         * This attaches the type information to the runtime value itself, not
         * just to the variable that first received it. That distinction matters
         * for container and object values that can be aliased, returned from a
         * function, stored in a field, or passed through native code.
         *
         * Example: after evaluating
         * `soit noms: Liste[Texte] = ["Ada", "Linus"]`
         * the list object must keep behaving as `Liste[Texte]` even if it is
         * later accessed through another variable or returned from a function.
         *
         * Native C++ code uses this after constructing values that should carry
         * the same language-level type information as values created from
         * annotated Lumiere source. For example:
         *
         * - a builtin returning a list of strings can attach `Liste[Texte]`
         * - a builtin returning an HTTP response object can attach `RéponseHTTP`
         *
         * In the tree walker this metadata is not just descriptive. It feeds
         * later runtime checks, especially for generic containers. Once a list
         * is annotated as `Liste[Texte]`, later operations such as
         * `Liste.ajouter(...)` can reject non-`Texte` values the same way they
         * would for a list created from annotated Lumiere code.
         */
        void annotate_value(const Value &value, std::string_view type_name, const RuntimeSite &site) const override;

        /**
         * @brief Raises a runtime error using a token for source location.
         */
        [[noreturn]] void raise_runtime_error(const Token &token, const std::string &message) const;

        /**
         * @brief Requires that a value be `Texte` and returns the underlying string.
         *
         * Throws a `RuntimeError` at `token` if the value has any other runtime type.
         */
        std::string require_texte_value(const Value &value, const Token &token) const;

        /**
         * @brief Requires that a value be `Entier` and returns the underlying integer.
         *
         * Throws a `RuntimeError` at `token` if the value has any other runtime type.
         */
        int64_t require_entier_value(const Value &value, const Token &token) const;

        /**
         * @brief Requires that a value be `Décimal` and returns the underlying number.
         *
         * Throws a `RuntimeError` at `token` if the value has any other runtime type.
         */
        double require_decimal_value(const Value &value, const Token &token) const;

        /**
         * @brief Registers declared type information on a value.
         *
         * This is used mainly for annotated container values such as
         * `Liste[Texte]` or `Dictionnaire[Texte, Entier]`. The interpreter
         * records the element/key/value type constraints so later writes can
         * be checked against the original annotation.
         */
        void annotate_runtime_value(const Value &value, const Token &annotation) const;

        /**
         * @brief Configures interpreter options used by the LumiTest builtin module.
         */
        void configure_lumitest(LumiTestRuntimeOptions options);

        /**
         * @brief Returns the accumulated LumiTest execution summary, if any.
         */
        LumiTestRunSummary lumitest_summary() const;

    private:
        // Tree-walker-specific helper signature used while implementing native
        // members such as `liste.ajouter(...)`. Unlike `LumiereFunction::NativeHandler`,
        // this already assumes the backend is a TreeWalker and exposes a Token
        // for error reporting instead of the generic RuntimeSite bundle.
        using NativeMethodHandler = std::function<Value(
            TreeWalker &,
            const std::vector<RuntimeArgument> &,
            const Token &)>;

        struct TreeWalkerFunctionBody;
        struct TreeWalkerClassBody;
        struct TreeWalkerInterfaceBody;
        struct TreeWalkerModuleState : RuntimeModuleState
        {
            std::shared_ptr<Environment> environment;
        };

        struct ListConstraint
        {
            std::string element_type;
        };

        struct FixedListConstraint
        {
            std::string element_type;
            std::size_t length = 0;
        };

        struct DictConstraint
        {
            std::string key_type;
            std::string value_type;
        };

        struct SetConstraint
        {
            std::string element_type;
        };

        class StackFrameGuard
        {
        public:
            StackFrameGuard(TreeWalker &walker, StackFrame frame);
            ~StackFrameGuard();

            StackFrameGuard(const StackFrameGuard &) = delete; // non-copyable
            StackFrameGuard &operator=(const StackFrameGuard &) = delete;

        private:
            TreeWalker *m_walker = nullptr;
        };

        Value m_result;               // for expression results
        Environment *m_env = nullptr; // current scope, never null during execution
        std::shared_ptr<Environment> m_env_owner;
        Value m_self; // current ici binding — RIEN if free function
        std::unordered_map<std::string, std::shared_ptr<Module>> m_modules;
        std::unordered_set<std::string> m_loading_modules;
        std::vector<std::filesystem::path> m_import_paths;
        std::vector<std::shared_ptr<StmtList>> m_loaded_module_programmes;
        std::vector<StackFrame> m_stack_trace;
        std::string m_current_source_path;
        std::string m_current_source_text;
        LumiTestRuntimeOptions m_lumitest_options;
        mutable std::unordered_map<const ListeData *, ListConstraint> m_list_constraints;
        mutable std::unordered_map<const ListeFixeData *, FixedListConstraint> m_fixed_list_constraints;
        mutable std::unordered_map<const DictData *, DictConstraint> m_dict_constraints;
        mutable std::unordered_map<const EnsembleData *, SetConstraint> m_set_constraints;

        /**
         * @brief Evaluates an expression and returns its resulting runtime value.
         */
        Value evaluate(Expr &expr);

        /**
         * @brief Calls a user-defined function from an AST call expression.
         *
         * This helper evaluates the call arguments, applies parameter binding,
         * and executes the function body in its captured environment.
         */
        Value call_function(const std::shared_ptr<LumiereFunction> &function,
                            const std::vector<Argument> &args,
                            const Token &call_site);

        /**
         * @brief Runs a user-defined function after argument evaluation is complete.
         *
         * This is the shared call path for declared functions and anonymous
         * function values. It creates the callee environment, binds positional
         * and named arguments, restores `ici` when needed, and executes the
         * saved AST body against the function's captured closure.
         */
        Value call_user_function(const std::shared_ptr<LumiereFunction> &function,
                                 const std::vector<RuntimeArgument> &args,
                                 const RuntimeSite &call_site);

        /**
         * @brief Creates an instance and, if present, invokes its initializer as a method call.
         *
         * This is the constructor path for Lumiere classes: allocate the
         * object, bind `ici`, resolve the initializer member, and dispatch it
         * through the ordinary call machinery so arity checks and runtime
         * errors behave the same as any other method call.
         */
        Value instantiate_class(const std::shared_ptr<LumiereClass> &klass,
                                const std::vector<Argument> &args,
                                const Token &call_site);

        /**
         * @brief Dispatches a top-level builtin by its public Lumiere name.
         *
         * Unlike `resolve_native_member(...)`, this path is for free functions
         * exposed directly in scope rather than methods retrieved from a
         * receiver value.
         */
        Value call_builtin(const std::string &name,
                           const std::vector<Argument> &args,
                           const Token &call_site);

        /**
         * @brief Evaluates call arguments left-to-right into the runtime call format.
         *
         * The resulting vector preserves source order and keeps any named
         * argument labels so later binding logic can match them to parameters.
         */
        std::vector<RuntimeArgument> evaluate_runtime_arguments(const std::vector<Argument> &args);

        /**
         * @brief Returns a loaded module, reusing cache and guarding against import cycles.
         *
         * The token is used both for module-name resolution and for attaching
         * precise source locations to import errors.
         */
        std::shared_ptr<Module> load_module(const Token &module_name_token);

        /**
         * @brief Builds the runtime representation for a builtin module, if one exists.
         *
         * This is the bridge from a string such as `Texte` or `LumiNet` to the
         * C++ registration code that populates the module's exported members.
         */
        std::shared_ptr<Module> load_builtin_module(const std::string &module_name) const;

        /**
         * @brief Wraps a plain C++ callback in a runtime function value with no receiver.
         */
        std::shared_ptr<LumiereFunction> make_native_function(LumiereFunction::NativeHandler handler) const;

        /**
         * @brief Wraps a C++ callback in a runtime function value already bound to a receiver.
         *
         * The resulting function reports itself as a method and will expose the
         * stored receiver through `NativeArgs.receiver` when called.
         */
        std::shared_ptr<LumiereFunction> make_native_method(Value receiver,
                                                            LumiereFunction::NativeHandler handler) const;

        /**
         * @brief Captures a named function declaration as a callable runtime value.
         *
         * The returned object keeps both the AST node and the surrounding
         * environment alive so later calls can re-enter the declaration with
         * the correct closure.
         */
        std::shared_ptr<LumiereFunction> make_declared_function(FunctionDeclStmt &decl,
                                                                Value receiver,
                                                                Environment *closure) const;

        /**
         * @brief Captures an anonymous function expression as a callable runtime value.
         *
         * This is the expression-form counterpart to the declaration overload:
         * same closure rules, but the body comes from a `FunctionExpr`.
         */
        std::shared_ptr<LumiereFunction> make_declared_function(FunctionExpr &expr,
                                                                Value receiver,
                                                                Environment *closure) const;

        /**
         * @brief Resolves a built-in member access on a runtime value.
         *
         * This is the hook behind methods that are provided directly by the
         * runtime rather than declared in Lumiere source, for example
         * `texte.majuscules()`, `liste.ajouter(x)`, or `dictionnaire.cles()`.
         *
         * If `object.member` is one of those built-in operations, this returns
         * a callable/bound value representing that method. If the runtime does
         * not provide such a member for this value, it returns `rien` so the
         * caller can continue with ordinary field/class/interface lookup.
         */
        Value resolve_native_member(const Value &object, const Token &member) const;

        /**
         * @brief Creates a class value that points back to its class declaration node.
         */
        std::shared_ptr<LumiereClass> make_runtime_class(ClassDeclStmt &decl) const;

        /**
         * @brief Creates an interface value that points back to its interface declaration node.
         */
        std::shared_ptr<LumiereInterface> make_runtime_interface(InterfaceDeclStmt &decl) const;

        /**
         * @brief Turns a tree-walker helper into the method value returned by `object.member`.
         *
         * Example: when `resolve_native_member(...)` handles `liste.ajouter`,
         * it uses this helper to create the callable Lumiere value that already
         * remembers which list instance is the receiver. Later, when user code
         * calls that method, the shared native-call path invokes `handler` with:
         *
         * - the current `TreeWalker`
         * - the evaluated runtime arguments
         * - a token rebuilt from the call site for error reporting
         */
        Value make_tree_walker_native_method(
            Value receiver,
            NativeMethodHandler handler) const;

        /**
         * @brief Validates a native method call that only accepts positional arguments.
         */
        void require_positional_args(const std::vector<RuntimeArgument> &args,
                                     std::size_t min_count,
                                     std::size_t max_count,
                                     const std::string &signature,
                                     const Token &call_site) const;

        /**
         * @brief Resolves sequence methods shared by mutable and fixed-size sequence families.
         *
         * Keeping these here avoids duplicating method definitions such as
         * `taille`, `vide`, `contient`, and `joindre` across list-like values.
         */
        Value resolve_sequence_common_native_member(const std::vector<Value> &elements,
                                                   const std::string &family_name,
                                                   const Token &member,
                                                   Value receiver) const;

        /**
         * @brief Resolves methods specific to mutable `Liste` values.
         *
         * This adds mutation-oriented operations on top of the sequence-common
         * methods, for example insertion and removal.
         */
        Value resolve_list_native_member(const std::shared_ptr<ListeData> &list,
                                        const Token &member,
                                        Value receiver) const;

        /**
         * @brief Resolves methods specific to `ListeFixe` values.
         *
         * These methods preserve fixed-length semantics while still exposing
         * the read-only/common sequence surface where appropriate.
         */
        Value resolve_fixed_list_native_member(const std::shared_ptr<ListeFixeData> &list,
                                              const Token &member,
                                              Value receiver) const;

        /**
         * @brief Resolves methods specific to `Dictionnaire` values.
         *
         * This is the dictionary counterpart to the list/member helpers above:
         * turn a member token into a bound native method when the runtime owns
         * that operation.
         */
        Value resolve_dict_native_member(const std::shared_ptr<DictData> &dict,
                                        const Token &member,
                                        Value receiver) const;

        /**
         * @brief Resolves a dotted module name to the configured source file that should be imported.
         *
         * The search walks the configured import paths and applies the
         * repository's module-to-filesystem naming convention.
         */
        std::filesystem::path resolve_module_path(const std::string &module_name) const;

        /**
         * @brief Derives the default binding name introduced by `importer module.sous_module`.
         *
         * For example, this chooses the local namespace name when the import
         * does not provide an explicit alias.
         */
        std::string default_module_alias(const std::string &module_name) const;

        /**
         * @brief Executes a parsed module in its own environment and packages the exports.
         *
         * The returned `Module` remembers both the exported member table and
         * the module execution scope so later runtime operations can inspect
         * module state if needed.
         */
        std::shared_ptr<Module> execute_module(const std::string &module_name,
                                               const std::filesystem::path &path,
                                               std::shared_ptr<StmtList> statements,
                                               std::string source_text);

        /**
         * @brief Materializes an iterable into a vector for `pour` loops and similar consumers.
         *
         * This normalizes strings, lists, fixed lists, dictionaries, and other
         * supported iterable runtime values into one traversal format.
         */
        std::vector<Value> enumerate_iterable(const Value &iterable, const Token &site) const;

        /**
         * @brief Returns true when a catch clause's declared type accepts the thrown value.
         *
         * This is a type-match check only; clause ordering and execution are
         * handled by the caller.
         */
        bool matches_catch_clause(const CatchClause &clause, const Value &thrown_value) const;

        /**
         * @brief Returns whether a runtime value can participate in `pour`-style iteration.
         */
        bool is_iterable_value(const Value &value) const;

        /**
         * @brief Returns whether a value supports read-only indexing.
         */
        bool supports_index_read(const Value &value) const;

        /**
         * @brief Returns whether a value supports in-place indexed assignment.
         */
        bool supports_mutable_index_assignment(const Value &value) const;

        /**
         * @brief Returns the backing elements for a readable sequence value.
         *
         * Returns `nullptr` when the value is not a sequence handled by this helper.
         */
        const std::vector<Value> *sequence_elements(const Value &value) const;

        /**
         * @brief Returns the backing elements for a mutable sequence value.
         *
         * Returns `nullptr` when the value cannot be modified through index assignment.
         */
        std::vector<Value> *mutable_sequence_elements(const Value &value) const;

        /**
         * @brief Returns the user-facing family name for a sequence value.
         */
        std::string sequence_family_name(const Value &value) const;

        /**
         * @brief Runs a branch in a fresh nested scope, with an optional branch-local variable.
         *
         * This is used for constructs such as pattern/catch branches that may
         * expose a matched or thrown value by name. The binding, when present,
         * exists only while this branch body executes.
         *
         * Example: an `attraper (e: Texte) { ... }` branch can bind the caught
         * value as `e` for the duration of that branch without leaking `e`
         * into the surrounding scope.
         */
        void execute_branch_with_optional_binding(Stmt &body,
                                                  const Token *binding_name,
                                                  const Value *binding_value);

        /**
         * @brief Returns the language-level type name used in diagnostics and type matching.
         *
         * This is the string form shown to users in runtime errors and reused
         * by helpers that compare a value against declared type annotations.
         */
        std::string runtime_type_name(const Value &value) const;

        /**
         * @brief Executes one statement by dispatching to its visitor method.
         */
        void execute(Stmt &stmt);

        /**
         * @brief Executes a block statement inside a nested lexical scope.
         */
        void execute_block(BlockStmt &block);

        /**
         * @brief Evaluates an assignment expression and stores the assigned value in `m_result`.
         */
        void evaluate_assignment(BinaryExpr &expr);

        /**
         * @brief Assigns into a named variable target and stores the assigned value in `m_result`.
         */
        void assign_identifier(IdentifierExpr &target, Expr &value_expr);

        /**
         * @brief Assigns into an object field target and stores the assigned value in `m_result`.
         */
        void assign_member(MemberAccessExpr &target, Expr &value_expr);

        /**
         * @brief Assigns through an index target and stores the assigned value in `m_result`.
         */
        void assign_index(IndexAccessExpr &target, Expr &value_expr);

        /**
         * @brief Returns true if a value is truthy.
         *
         * rien and faux are falsy. Everything else is truthy.
         */
        bool is_truthy(const Value &value) const;

        /**
         * @brief Returns true if a runtime value satisfies a source-level type annotation token.
         *
         * This helper understands both simple builtins and the container/class
         * names that the interpreter exposes in error messages and checks.
         */
        bool matches_type_name(const Value &value, const Token &type_token) const;

        /**
         * @brief Splits a generic type string into its top-level type arguments.
         *
         * For example, this separates the inner parts of `Dictionnaire[Texte, Liste[Entier]]`
         * without breaking nested generic types apart incorrectly.
         */
        std::vector<std::string> split_generic_arguments(const std::string &generic_spec) const;

        /**
         * @brief Extracts and stores runtime constraints from a declared type annotation.
         *
         * This is where annotated containers such as typed lists, fixed lists,
         * dictionaries, and sets record the type rules that later mutations
         * must respect.
         */
        void register_value_annotation(const Value &value, const Token &annotation) const;

        /**
         * @brief Enforces the declared element type before mutating a `Liste`.
         *
         * Callers supply a context string so any resulting error can name the
         * operation that attempted the invalid write.
         */
        void enforce_list_element_constraint(const std::shared_ptr<ListeData> &list,
                                             const Value &element,
                                             const Token &site,
                                             const std::string &context) const;

        /**
         * @brief Checks that a fixed-list write respects its declared element type.
         *
         * Size is fixed by the container definition itself; this helper enforces the
         * element-type part of that contract during updates.
         */
        void enforce_fixed_list_element_constraint(const std::shared_ptr<ListeFixeData> &list,
                                                   const Value &element,
                                                   const Token &site,
                                                   const std::string &context) const;

        /**
         * @brief Enforces declared key and value constraints before mutating a dictionary.
         *
         * This keeps `Dictionnaire[K, V]` runtime writes aligned with the type
         * annotation recorded when the value was first created or assigned.
         */
        void enforce_dict_entry_constraint(const std::shared_ptr<DictData> &dict,
                                           const Value &key,
                                           const Value &entry_value,
                                           const Token &site,
                                           const std::string &context) const;

        /**
         * @brief Returns true if the class reaches the named ancestor through its parent chain.
         */
        bool class_derives_from(const std::shared_ptr<LumiereClass> &klass,
                                const std::string &ancestor_name) const;

        /**
         * @brief Returns true if the class or one of its ancestors advertises the named interface.
         */
        bool class_implements_interface(const std::shared_ptr<LumiereClass> &klass,
                                        const std::string &interface_name) const;

        /**
         * @brief Checks that a value matches a declared type annotation.
         *
         * When the check succeeds, the annotation is also registered on the
         * value so future operations can keep enforcing the same type rules.
         */
        void ensure_value_matches_annotation(const Value &value,
                                             const Token &annotation,
                                             const Token &site,
                                             const std::string &context) const;

        /**
         * @brief Returns the named declaration AST for a function value, if this function came from one.
         */
        FunctionDeclStmt *function_decl(const LumiereFunction &function) const;

        /**
         * @brief Returns the anonymous-function AST for a function value, if this function came from one.
         */
        FunctionExpr *function_expr(const LumiereFunction &function) const;

        /**
         * @brief Returns the raw environment pointer captured when the function value was created.
         *
         * The returned pointer is only safe because the sibling
         * `function_closure_owner(...)` handle keeps that environment alive.
         */
        Environment *function_closure(const LumiereFunction &function) const;

        /**
         * @brief Returns the shared owner that keeps a captured closure environment alive.
         */
        std::shared_ptr<Environment> function_closure_owner(const LumiereFunction &function) const;

        /**
         * @brief Returns the class declaration behind a class value, if present.
         */
        ClassDeclStmt *class_decl(const std::shared_ptr<LumiereClass> &klass) const;

        /**
         * @brief Returns the interface declaration behind an interface value, if present.
         */
        InterfaceDeclStmt *interface_decl(const std::shared_ptr<LumiereInterface> &iface) const;

        /**
         * @brief Returns the environment that held the module's top-level execution state.
         */
        Environment *module_environment(const std::shared_ptr<Module> &module) const;

        /**
         * @brief Returns the direct parent declaration for a class, resolving the stored parent token.
         */
        ClassDeclStmt *resolve_parent_class(const ClassDeclStmt &klass) const;

        /**
         * @brief Returns the direct parent class value for a runtime class, if any.
         */
        std::shared_ptr<LumiereClass> parent_class(const std::shared_ptr<LumiereClass> &klass) const;

        /**
         * @brief Returns true when two method declarations expose the same callable contract.
         */
        bool method_signatures_match(const FunctionDeclStmt &expected,
                                     const FunctionDeclStmt &actual) const;

        /**
         * @brief Finds a field declaration with the given name on this class.
         */
        VarDeclStmt *find_field_decl(const std::shared_ptr<LumiereClass> &klass, const std::string &name) const;

        /**
         * @brief Finds a method declaration with the given name on this class.
         */
        FunctionDeclStmt *find_method_decl(const std::shared_ptr<LumiereClass> &klass, const std::string &name) const;

        /**
         * @brief Finds a method declaration with the given name on this interface.
         */
        FunctionDeclStmt *find_interface_method_decl(InterfaceDeclStmt &iface, const std::string &name) const;

        /**
         * @brief Verifies that a class provides every method required by its declared interfaces.
         *
         * The current check is structural and declaration-based: it ensures the
         * methods exist before class creation proceeds.
         */
        void validate_class_interfaces(ClassDeclStmt &klass,
                                       const std::shared_ptr<LumiereClass> &class_value) const;

        /**
         * @brief Returns true if a member access chain is rooted at `ici`.
         *
         * This is used when visibility rules need to distinguish internal
         * access through the current receiver from ordinary external access.
         */
        bool access_uses_ici(const Expr &expr) const;

        /**
         * @brief Converts a runtime value to the Lumiere text representation shown to users.
         */
        std::string to_texte(const Value &value) const;

        /**
         * @brief Returns the underlying integer if the value is an `Entier`.
         *
         * Throws a `RuntimeError` at `token` if the value has any other type.
         */
        int64_t assert_entier(const Value &value, const Token &token) const;

        /**
         * @brief Returns the underlying number if the value is a `Décimal`.
         *
         * Throws a `RuntimeError` at `token` if the value has any other type.
         */
        double assert_decimal(const Value &value, const Token &token) const;

        /**
         * @brief Returns the underlying string if the value is a `Texte`.
         *
         * Throws a `RuntimeError` at `token` if the value has any other type.
         */
        std::string assert_texte(const Value &value, const Token &token) const;

        /**
         * @brief Throws a `RuntimeError` at the given token location.
         */
        [[noreturn]] void throw_runtime_error(const Token &token, const std::string &message) const;

        /**
         * @brief Throws a `RuntimeError` without a specific token location.
         */
        [[noreturn]] void throw_runtime_error(const std::string &message) const;

        /**
         * @brief Pushes a frame onto the interpreter stack trace used in runtime errors.
         */
        void push_stack_frame(StackFrame frame);

        /**
         * @brief Pops the most recent frame from the interpreter stack trace.
         */
        void pop_stack_frame();

        // Expression visitors

        /**
         * @brief Evaluates a literal and stores the resulting value in `m_result`.
         */
        void visit(LiteralExpr &) override;

        /**
         * @brief Resolves an identifier from the current scope chain into `m_result`.
         */
        void visit(IdentifierExpr &) override;

        /**
         * @brief Evaluates a binary operator expression.
         *
         * This includes arithmetic, comparison, logical, and assignment-related
         * binary operations supported by the language.
         */
        void visit(BinaryExpr &) override;

        /**
         * @brief Builds a dictionary value from its key and value expressions.
         */
        void visit(DictionaryExpr &) override;

        /**
         * @brief Evaluates a unary operator expression and stores the result.
         */
        void visit(UnaryExpr &) override;

        /**
         * @brief Evaluates an explicit cast expression.
         */
        void visit(CastExpr &) override;

        /**
         * @brief Evaluates a runtime type-check expression.
         */
        void visit(TypeCheckExpr &) override;

        /**
         * @brief Creates a function value from an anonymous function expression.
         */
        void visit(FunctionExpr &) override;

        /**
         * @brief Evaluates a function, method, or constructor call expression.
         */
        void visit(CallExpr &) override;

        /**
         * @brief Builds a list value from its element expressions.
         */
        void visit(ListExpr &) override;

        /**
         * @brief Resolves field or method access on an object value.
         */
        void visit(MemberAccessExpr &) override;

        /**
         * @brief Evaluates indexed access into strings, sequences, or dictionaries.
         */
        void visit(IndexAccessExpr &) override;

        // Statement visitors

        /**
         * @brief Evaluates an expression statement and discards the resulting value.
         */
        void visit(ExprStmt &) override;

        /**
         * @brief Executes a block in a fresh nested scope.
         */
        void visit(BlockStmt &) override;

        /**
         * @brief Declares a variable, checks its annotation if present, and stores it in scope.
         */
        void visit(VarDeclStmt &) override;

        /**
         * @brief Declares a named function in the current scope.
         */
        void visit(FunctionDeclStmt &) override;

        /**
         * @brief Declares a class and binds its runtime class value.
         */
        void visit(ClassDeclStmt &) override;

        /**
         * @brief Declares an interface and binds its runtime interface value.
         */
        void visit(InterfaceDeclStmt &) override;

        /**
         * @brief Loads a module and binds its imported names in the current scope.
         */
        void visit(ImportStmt &) override;

        /**
         * @brief Executes an `if` statement and runs the first matching branch.
         */
        void visit(IfStmt &) override;

        /**
         * @brief Executes a `for` loop over an iterable value.
         */
        void visit(ForStmt &) override;

        /**
         * @brief Executes a `while` loop until its condition becomes falsy.
         */
        void visit(WhileStmt &) override;

        /**
         * @brief Evaluates a return statement and unwinds with a `ReturnSignal`.
         */
        void visit(ReturnStmt &) override;

        /**
         * @brief Exits the current loop by throwing a `BreakSignal`.
         */
        void visit(BreakStmt &) override;

        /**
         * @brief Skips to the next loop iteration by throwing a `ContinueSignal`.
         */
        void visit(ContinueStmt &) override;

        /**
         * @brief Evaluates a throw statement and unwinds with a `ThrownSignal`.
         */
        void visit(ThrowStmt &) override;

        /**
         * @brief Executes a `try` block and routes thrown values through matching catches.
         */
        void visit(TryStmt &) override;

        /**
         * @brief Executes an `agir selon` statement by testing its cases in order.
         */
        void visit(AgirSelonStmt &) override;
    };
}
