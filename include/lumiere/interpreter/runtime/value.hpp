#pragma once

#include "lumiere/interpreter/runtime/native_args.hpp"
#include <functional>
#include <memory>
#include <string>
#include <unordered_set>
#include <variant>
#include <vector>
#include <unordered_map>

namespace lumiere
{

// forward declarations
struct LumiereObject;
struct LumiereFunction;
struct LumiereClass;
struct LumiereInterface;
struct RuntimeFunctionBody;
struct RuntimeClassBody;
struct RuntimeInterfaceBody;
struct RuntimeModuleState;
class Environment;
class IRuntime;
struct Value;
struct FunctionDeclStmt;
struct ClassDeclStmt;
struct InterfaceDeclStmt;

// type aliases for collection types
struct ListeData      { std::vector<Value> elements; };
struct ListeFixeData  { std::vector<Value> elements; };
struct EnsembleData   { std::vector<Value> elements; };
using DictEntry = std::pair<Value, Value>;
struct DictData       { std::vector<DictEntry> entries; };

struct Value
{
    enum class Type
    {
        // this order must be preserved as it is used to access the variant data
        ENTIER        = 0,
        DECIMAL       = 1,
        LOGIQUE       = 2,
        SYMBOLE       = 3,
        TEXTE         = 4,
        LISTE         = 5,
        LISTE_FIXE    = 6,
        DICTIONNAIRE  = 7,
        ENSEMBLE      = 8,
        OBJET         = 9,
        FONCTION      = 10,
        CLASSE        = 11,
        INTERFACE     = 12,
        RIEN          = 13,
    };

    using Data = std::variant<
        int64_t,                            // ENTIER
        double,                             // DECIMAL
        bool,                               // LOGIQUE
        char32_t,                           // SYMBOLE
        std::string,                        // TEXTE
        std::shared_ptr<ListeData>,          // LISTE
        std::shared_ptr<ListeFixeData>,      // LISTE_FIXE
        std::shared_ptr<DictData>,           // DICTIONNAIRE
        std::shared_ptr<EnsembleData>,       // ENSEMBLE
        std::shared_ptr<LumiereObject>,     // OBJET
        std::shared_ptr<LumiereFunction>,   // FONCTION
        std::shared_ptr<LumiereClass>,      // CLASSE
        std::shared_ptr<LumiereInterface>   // INTERFACE
    >;
    static_assert(std::variant_size_v<Data> == 13,"Data variant and Type enum are out of sync — check indices");

    Type type = Type::RIEN;
    Data data;

    //factories

    static Value rien()
    {
        Value v;
        v.type = Type::RIEN;
        return v;
    }

    static Value entier(int64_t n)
    {
        Value v;
        v.type = Type::ENTIER;
        v.data = n;
        return v;
    }

    static Value decimal(double d)
    {
        Value v;
        v.type = Type::DECIMAL;
        v.data = d;
        return v;
    }

    static Value logique(bool b)
    {
        Value v;
        v.type = Type::LOGIQUE;
        v.data = b;
        return v;
    }

    static Value symbole(char32_t chtr)
    {
        Value v;
        v.type = Type::SYMBOLE;
        v.data = chtr;
        return v;
    }

    static Value texte(std::string str)
    {
        Value v;
        v.type = Type::TEXTE;
        v.data = std::move(str);
        return v;
    }

    static Value liste(std::shared_ptr<ListeData> lst)
    {
        Value v;
        v.type = Type::LISTE;
        v.data = std::move(lst);
        return v;
    }

    static Value dictionnaire(std::shared_ptr<DictData> data)
    {
        Value v;
        v.type = Type::DICTIONNAIRE;
        v.data = std::move(data);
        return v;
    }

    static Value liste_fixe(std::shared_ptr<ListeFixeData> lst)
    {
        Value v;
        v.type = Type::LISTE_FIXE;
        v.data = std::move(lst);
        return v;
    }

    static Value ensemble(std::shared_ptr<EnsembleData> ens)
    {
        Value v;
        v.type = Type::ENSEMBLE;
        v.data = std::move(ens);
        return v;
    }

    static Value objet(std::shared_ptr<LumiereObject> obj)
    {
        Value v;
        v.type = Type::OBJET;
        v.data = std::move(obj);
        return v;
    }

    static Value fonction(std::shared_ptr<LumiereFunction> fn)
    {
        Value v;
        v.type = Type::FONCTION;
        v.data = std::move(fn);
        return v;
    }

    static Value classe(std::shared_ptr<LumiereClass> cls)
    {
        Value v;
        v.type = Type::CLASSE;
        v.data = std::move(cls);
        return v;
    }

    static Value interface(std::shared_ptr<LumiereInterface> iface)
    {
        Value v;
        v.type = Type::INTERFACE;
        v.data = std::move(iface);
        return v;
    }

    //accessors

    int64_t     as_entier()  const { return std::get<int64_t>(data); }
    double      as_decimal() const { return std::get<double>(data); }
    bool        as_logique() const { return std::get<bool>(data); }
    char32_t    as_symbole() const { return std::get<char32_t>(data); }

    const std::string &as_texte() const
    {
        return std::get<std::string>(data);
    }

    std::shared_ptr<ListeData> as_liste() const
    {
        return std::get<std::shared_ptr<ListeData>>(data);
    }

    std::shared_ptr<DictData> as_dictionnaire() const
    {
        return std::get<std::shared_ptr<DictData>>(data);
    }

    std::shared_ptr<ListeFixeData> as_liste_fixe() const
    {
        return std::get<std::shared_ptr<ListeFixeData>>(data);
    }

    std::shared_ptr<EnsembleData> as_ensemble() const
    {
        return std::get<std::shared_ptr<EnsembleData>>(data);
    }

    std::shared_ptr<LumiereObject> as_objet() const
    {
        return std::get<std::shared_ptr<LumiereObject>>(data);
    }

    std::shared_ptr<LumiereFunction> as_fonction() const
    {
        return std::get<std::shared_ptr<LumiereFunction>>(data);
    }

    std::shared_ptr<LumiereClass> as_classe() const
    {
        return std::get<std::shared_ptr<LumiereClass>>(data);
    }

    std::shared_ptr<LumiereInterface> as_interface() const
    {
        return std::get<std::shared_ptr<LumiereInterface>>(data);
    }

    //type checks

    bool is_rien()        const { return type == Type::RIEN; }
    bool is_entier()      const { return type == Type::ENTIER; }
    bool is_decimal()     const { return type == Type::DECIMAL; }
    bool is_logique()     const { return type == Type::LOGIQUE; }
    bool is_symbole()     const { return type == Type::SYMBOLE; }
    bool is_texte()       const { return type == Type::TEXTE; }
    bool is_liste()       const { return type == Type::LISTE; }
    bool is_liste_fixe()  const { return type == Type::LISTE_FIXE; }
    bool is_dictionnaire()const { return type == Type::DICTIONNAIRE; }
    bool is_ensemble()    const { return type == Type::ENSEMBLE; }
    bool is_objet()       const { return type == Type::OBJET; }
    bool is_fonction()    const { return type == Type::FONCTION; }
    bool is_classe()      const { return type == Type::CLASSE; }
    bool is_interface()   const { return type == Type::INTERFACE; }
    bool is_numeric()     const { return is_entier() || is_decimal(); }

    //equality

    bool operator==(const Value &other) const;
    bool operator!=(const Value &other) const { return !(*this == other); }

    // display

    std::string to_string() const;
    std::string type_name() const;
};

//  LumiereFunction
//  A callable, either a user-defined function (eg obj.do_something())
//  or a bound method carrying its receiver (eg ici.do_something())
struct RuntimeFunctionBody
{
    virtual ~RuntimeFunctionBody() = default;
};

struct LumiereFunction
{
    // Generic runtime callback signature for native callables.
    // This is the backend-facing signature used by `LumiereFunction` itself:
    // the callee receives the active runtime plus the normalized call bundle
    // (`receiver`, evaluated arguments, and source site).
    using NativeHandler = std::function<Value(IRuntime &, const NativeArgs &)>;

    std::string                     name;
    std::shared_ptr<RuntimeFunctionBody> body;
    Value                           receiver;
    NativeHandler                   native_handler;
    std::size_t                     min_arity = 0;
    std::size_t                     max_arity = 0;

    //all functions that are not methods always have a receiver that is 'rien'
    bool is_method() const { return !receiver.is_rien(); }
    // True when this function is implemented directly by a C++ handler instead
    // of by walking a Lumiere AST body.
    //
    // Examples of native functions:
    // - builtins such as `afficher(...)`
    // - stdlib methods exposed from C++ such as `texte.majuscules()`
    //
    // Examples of non-native functions:
    // - `fonction principal() { ... }`
    // - `soit doubler = fonction(x: Entier) -> Entier { retourne x * 2 }`
    bool is_native() const { return static_cast<bool>(native_handler); }
};

struct RuntimeClassBody
{
    virtual ~RuntimeClassBody() = default;
};

struct RuntimeInterfaceBody
{
    virtual ~RuntimeInterfaceBody() = default;
};

struct LumiereClass
{
    std::string name;
    std::shared_ptr<RuntimeClassBody> body;
};

struct LumiereInterface
{
    std::string name;
    std::shared_ptr<RuntimeInterfaceBody> body;
};

//  LumiereObject
//  A class instance at runtime.
struct LumiereObject
{
    std::shared_ptr<LumiereClass>           klass;
    std::shared_ptr<void>                   native_state;
    std::unordered_map<std::string, Value> fields;
};

struct RuntimeModuleState
{
    virtual ~RuntimeModuleState() = default;
};

struct Module {
    std::string name;
    std::shared_ptr<RuntimeModuleState> state;
    std::unordered_map<std::string, Value> members;
    std::unordered_set<std::string> public_members;
};

} // namespace lumiere
