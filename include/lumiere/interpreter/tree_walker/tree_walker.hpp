#pragma once

#include <filesystem>
#include <memory>
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
         * @brief Stores declared type information on a value.
         *
         * This is mainly used by native modules so values they create behave
         * like values annotated inside Lumiere code.
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
        struct TreeWalkerFunctionBody;
        struct TreeWalkerClassBody;
        struct TreeWalkerInterfaceBody;
        struct TreeWalkerModuleState;

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
        Value m_self;                 // current ici binding — RIEN if free function
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
         * @brief Calls a user-defined function with arguments that were already evaluated.
         */
        Value call_user_function(const std::shared_ptr<LumiereFunction> &function,
                                 const std::vector<RuntimeArgument> &args,
                                 const RuntimeSite &call_site);

        /**
         * @brief Creates an instance of a class and runs its initializer, if any.
         */
        Value instantiate_class(const std::shared_ptr<LumiereClass> &klass,
                                const std::vector<Argument> &args,
                                const Token &call_site);

        /**
         * @brief Calls one of the interpreter's built-in functions by name.
         */
        Value call_builtin(const std::string &name,
                           const std::vector<Argument> &args,
                           const Token &call_site);

        /**
         * @brief Evaluates AST call arguments into runtime arguments.
         */
        std::vector<RuntimeArgument> evaluate_runtime_arguments(const std::vector<Argument> &args);

        /**
         * @brief Loads a module, either from the builtin registry or from disk.
         */
        std::shared_ptr<Module> load_module(const Token &module_name_token);

        /**
         * @brief Returns the builtin module implementation for a known module name.
         */
        std::shared_ptr<Module> load_builtin_module(const std::string &module_name) const;

        /**
         * @brief Wraps a C++ callback in a Lumiere function object.
         */
        std::shared_ptr<LumiereFunction> make_native_function(LumiereFunction::NativeHandler handler) const;

        /**
         * @brief Wraps a C++ callback in a method bound to a specific receiver.
         */
        std::shared_ptr<LumiereFunction> make_native_method(Value receiver,
                                                            LumiereFunction::NativeHandler handler) const;

        /**
         * @brief Creates a function value that points to a declared function node.
         */
        std::shared_ptr<LumiereFunction> make_declared_function(FunctionDeclStmt &decl,
                                                                Value receiver,
                                                                Environment *closure) const;

        /**
         * @brief Creates a function value that points to an anonymous function node.
         */
        std::shared_ptr<LumiereFunction> make_declared_function(FunctionExpr &expr,
                                                                Value receiver,
                                                                Environment *closure) const;

        /**
         * @brief Creates a class value that points back to its class declaration node.
         */
        std::shared_ptr<LumiereClass> make_runtime_class(ClassDeclStmt &decl) const;

        /**
         * @brief Creates an interface value that points back to its interface declaration node.
         */
        std::shared_ptr<LumiereInterface> make_runtime_interface(InterfaceDeclStmt &decl) const;

        /**
         * @brief Resolves members like native methods on built-in value types.
         */
        Value resolve_native_member(const Value &object, const Token &member) const;

        /**
         * @brief Resolves a module name to an importable filesystem path.
         */
        std::filesystem::path resolve_module_path(const std::string &module_name) const;

        /**
         * @brief Derives the default local alias used when importing a module.
         */
        std::string default_module_alias(const std::string &module_name) const;

        /**
         * @brief Executes a module in its own scope and returns its exported bindings.
         */
        std::shared_ptr<Module> execute_module(const std::string &module_name,
                                               const std::filesystem::path &path,
                                               std::shared_ptr<StmtList> statements,
                                               std::string source_text);

        /**
         * @brief Collects every value produced by an iterable into a vector.
         */
        std::vector<Value> enumerate_iterable(const Value &iterable, const Token &site) const;

        /**
         * @brief Returns true if a thrown value should be caught by this catch clause.
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
         * @brief Returns the runtime type name used in diagnostics and checks.
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
         * @brief Executes a branch body and optionally defines a temporary binding for it.
         */
        void execute_branch_with_optional_binding(Stmt &body,
                                                 const Token *binding_name,
                                                 const Value *binding_value);
        /**
         * @brief Returns true if a value is truthy.
         *
         * rien and faux are falsy. Everything else is truthy.
         */
        bool is_truthy(const Value &value) const;

        /**
         * @brief Returns true if a value matches the type named by the given token.
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
         * @brief Enforces the declared element type of a mutable list insertion.
         */
        void enforce_list_element_constraint(const std::shared_ptr<ListeData> &list,
                                             const Value &element,
                                             const Token &site,
                                             const std::string &context) const;

        /**
         * @brief Checks that a fixed-list write respects its declared element type.
         *
         * Size is fixed by the container shape itself; this helper enforces the
         * element-type part of that contract during updates.
         */
        void enforce_fixed_list_element_constraint(const std::shared_ptr<ListeFixeData> &list,
                                                   const Value &element,
                                                   const Token &site,
                                                   const std::string &context) const;

        /**
         * @brief Enforces declared key and value constraints for dictionary writes.
         */
        void enforce_dict_entry_constraint(const std::shared_ptr<DictData> &dict,
                                           const Value &key,
                                           const Value &entry_value,
                                           const Token &site,
                                           const std::string &context) const;

        /**
         * @brief Returns true if the class inherits, directly or indirectly, from the named class.
         */
        bool class_derives_from(const ClassDeclStmt &klass, const std::string &ancestor_name) const;

        /**
         * @brief Returns true if the class declares or inherits the named interface.
         */
        bool class_implements_interface(const ClassDeclStmt &klass, const std::string &interface_name) const;

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
         * @brief Returns the declaration node behind a function value, if it has one.
         */
        FunctionDeclStmt *function_decl(const LumiereFunction &function) const;

        /**
         * @brief Returns the anonymous function node behind a function value, if it has one.
         */
        FunctionExpr *function_expr(const LumiereFunction &function) const;

        /**
         * @brief Returns the closure scope captured by a function value.
         */
        Environment *function_closure(const LumiereFunction &function) const;

        /**
         * @brief Returns the owning handle for the closure captured by a function value.
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
         * @brief Returns the scope that was used when the module was executed.
         */
        Environment *module_environment(const std::shared_ptr<Module> &module) const;

        /**
         * @brief Returns the direct parent class declaration, if this class extends one.
         */
        ClassDeclStmt *resolve_parent_class(const ClassDeclStmt &klass) const;

        /**
         * @brief Finds a field declaration with the given name on this class.
         */
        VarDeclStmt *find_field_decl(ClassDeclStmt &klass, const std::string &name) const;

        /**
         * @brief Finds a method declaration with the given name on this class.
         */
        FunctionDeclStmt *find_method_decl(ClassDeclStmt &klass, const std::string &name) const;

        /**
         * @brief Finds a method declaration with the given name on this interface.
         */
        FunctionDeclStmt *find_interface_method_decl(InterfaceDeclStmt &iface, const std::string &name) const;

        /**
         * @brief Verifies that a class satisfies its declared interface contracts.
         */
        void validate_class_interfaces(ClassDeclStmt &klass) const;

        /**
         * @brief Returns true if a member access ultimately starts from `ici`.
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
         * @brief Pushes a frame onto the interpreter stack trace.
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
