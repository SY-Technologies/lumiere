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
        TreeWalker();
        ~TreeWalker() override;

        // Backend interface
        void execute(Program &program) override;
        void add_import_path(std::filesystem::path path);
        Value call(Value callee, const NativeArgs &args) override;
        [[noreturn]] void raise_runtime_error(const RuntimeSite &site, const std::string &message) const override;
        bool is_equal(const Value &left, const Value &right) const override;
        std::string to_text(const Value &value) const override;
        void annotate_value(const Value &value, std::string_view type_name, const RuntimeSite &site) const override;
        [[noreturn]] void raise_runtime_error(const Token &token, const std::string &message) const;
        std::string require_texte_value(const Value &value, const Token &token) const;
        int64_t require_entier_value(const Value &value, const Token &token) const;
        double require_decimal_value(const Value &value, const Token &token) const;
        void annotate_runtime_value(const Value &value, const Token &annotation) const;
        void configure_lumitest(LumiTestRuntimeOptions options);
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

            StackFrameGuard(const StackFrameGuard &) = delete;//should be copiable
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

        Value evaluate(Expr &expr);
        Value call_function(const std::shared_ptr<LumiereFunction> &function,
                            const std::vector<Argument> &args,
                            const Token &call_site);
        Value call_user_function(const std::shared_ptr<LumiereFunction> &function,
                                 const std::vector<RuntimeArgument> &args,
                                 const RuntimeSite &call_site);
        Value instantiate_class(const std::shared_ptr<LumiereClass> &klass,
                                const std::vector<Argument> &args,
                                const Token &call_site);
        Value call_builtin(const std::string &name,
                           const std::vector<Argument> &args,
                           const Token &call_site);
        std::vector<RuntimeArgument> evaluate_runtime_arguments(const std::vector<Argument> &args);
        std::shared_ptr<Module> load_module(const Token &module_name_token);
        std::shared_ptr<Module> load_builtin_module(const std::string &module_name) const;
        std::shared_ptr<LumiereFunction> make_native_function(LumiereFunction::NativeHandler handler) const;
        std::shared_ptr<LumiereFunction> make_native_method(Value receiver,
                                                            LumiereFunction::NativeHandler handler) const;
        std::shared_ptr<LumiereFunction> make_declared_function(FunctionDeclStmt &decl,
                                                                Value receiver,
                                                                Environment *closure) const;
        std::shared_ptr<LumiereFunction> make_declared_function(FunctionExpr &expr,
                                                                Value receiver,
                                                                Environment *closure) const;
        std::shared_ptr<LumiereClass> make_runtime_class(ClassDeclStmt &decl) const;
        std::shared_ptr<LumiereInterface> make_runtime_interface(InterfaceDeclStmt &decl) const;
        Value resolve_native_member(const Value &object, const Token &member) const;
        std::filesystem::path resolve_module_path(const std::string &module_name) const;
        std::string default_module_alias(const std::string &module_name) const;
        std::shared_ptr<Module> execute_module(const std::string &module_name,
                                               const std::filesystem::path &path,
                                               std::shared_ptr<StmtList> statements,
                                               std::string source_text);
        std::vector<Value> enumerate_iterable(const Value &iterable, const Token &site) const;
        bool matches_catch_clause(const CatchClause &clause, const Value &thrown_value) const;
        bool is_iterable_value(const Value &value) const;
        bool supports_index_read(const Value &value) const;
        bool supports_mutable_index_assignment(const Value &value) const;
        const std::vector<Value> *sequence_elements(const Value &value) const;
        std::vector<Value> *mutable_sequence_elements(const Value &value) const;
        std::string sequence_family_name(const Value &value) const;
        std::string runtime_type_name(const Value &value) const;

        void execute(Stmt &stmt);

        void execute_block(BlockStmt &block);
        void execute_branch_with_optional_binding(Stmt &body,
                                                 const Token *binding_name,
                                                 const Value *binding_value);
        /**
         * @brief Returns true if a value is truthy.
         *
         * rien and faux are falsy. Everything else is truthy.
         */
        bool is_truthy(const Value &value) const;

        bool matches_type_name(const Value &value, const Token &type_token) const;
        std::vector<std::string> split_generic_arguments(const std::string &generic_spec) const;
        void register_value_annotation(const Value &value, const Token &annotation) const;
        void enforce_list_element_constraint(const std::shared_ptr<ListeData> &list,
                                             const Value &element,
                                             const Token &site,
                                             const std::string &context) const;
        void enforce_fixed_list_element_constraint(const std::shared_ptr<ListeFixeData> &list,
                                                   const Value &element,
                                                   const Token &site,
                                                   const std::string &context) const;
        void enforce_dict_entry_constraint(const std::shared_ptr<DictData> &dict,
                                           const Value &key,
                                           const Value &entry_value,
                                           const Token &site,
                                           const std::string &context) const;
        bool class_derives_from(const ClassDeclStmt &klass, const std::string &ancestor_name) const;
        bool class_implements_interface(const ClassDeclStmt &klass, const std::string &interface_name) const;
        void ensure_value_matches_annotation(const Value &value,
                                            const Token &annotation,
                                            const Token &site,
                                            const std::string &context) const;
        FunctionDeclStmt *function_decl(const LumiereFunction &function) const;
        FunctionExpr *function_expr(const LumiereFunction &function) const;
        Environment *function_closure(const LumiereFunction &function) const;
        std::shared_ptr<Environment> function_closure_owner(const LumiereFunction &function) const;
        ClassDeclStmt *class_decl(const std::shared_ptr<LumiereClass> &klass) const;
        InterfaceDeclStmt *interface_decl(const std::shared_ptr<LumiereInterface> &iface) const;
        Environment *module_environment(const std::shared_ptr<Module> &module) const;
        ClassDeclStmt *resolve_parent_class(const ClassDeclStmt &klass) const;
        VarDeclStmt *find_field_decl(ClassDeclStmt &klass, const std::string &name) const;
        FunctionDeclStmt *find_method_decl(ClassDeclStmt &klass, const std::string &name) const;
        FunctionDeclStmt *find_interface_method_decl(InterfaceDeclStmt &iface, const std::string &name) const;
        void validate_class_interfaces(ClassDeclStmt &klass) const;
        bool access_uses_ici(const Expr &expr) const;

        /**
         * @brief Converts a Value to a Texte representation.
         */
        std::string to_texte(const Value &value) const;

        /**
         * @brief Asserts that a value is an Entier, throws RuntimeError otherwise.
         */
        int64_t assert_entier(const Value &value, const Token &token) const;

        /**
         * @brief Asserts that a value is a Décimal, throws RuntimeError otherwise.
         */
        double assert_decimal(const Value &value, const Token &token) const;

        /**
         * @brief Asserts that a value is a Texte, throws RuntimeError otherwise.
         */
        std::string assert_texte(const Value &value, const Token &token) const;

        [[noreturn]] void throw_runtime_error(const Token &token, const std::string &message) const;
        [[noreturn]] void throw_runtime_error(const std::string &message) const;
        void push_stack_frame(StackFrame frame);
        void pop_stack_frame();

        // ── Expression visitors

        void visit(LiteralExpr &) override;
        void visit(IdentifierExpr &) override;
        void visit(BinaryExpr &) override;
        void visit(DictionaryExpr &) override;
        void visit(UnaryExpr &) override;
        void visit(CastExpr &) override;
        void visit(TypeCheckExpr &) override;
        void visit(FunctionExpr &) override;
        void visit(CallExpr &) override;
        void visit(ListExpr &) override;
        void visit(MemberAccessExpr &) override;
        void visit(IndexAccessExpr &) override;

        // ── Statement visitors

        void visit(ExprStmt &) override;
        void visit(BlockStmt &) override;
        void visit(VarDeclStmt &) override;
        void visit(FunctionDeclStmt &) override;
        void visit(ClassDeclStmt &) override;
        void visit(InterfaceDeclStmt &) override;
        void visit(ImportStmt &) override;
        void visit(IfStmt &) override;
        void visit(ForStmt &) override;
        void visit(WhileStmt &) override;
        void visit(ReturnStmt &) override;
        void visit(BreakStmt &) override;
        void visit(ContinueStmt &) override;
        void visit(ThrowStmt &) override;
        void visit(TryStmt &) override;
        void visit(AgirSelonStmt &) override;
    };
}
