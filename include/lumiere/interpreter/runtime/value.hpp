#pragma once

#include "lumiere/interpreter/runtime/native_args.hpp"
#include "lumiere/interpreter/runtime/ref.hpp"
#include "lumiere/parser/type_expr.hpp"
#include <cassert>
#include <functional>
#include <memory>
#include <optional>
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
struct TraceFrame;
class IRuntime;
struct Value;
struct FunctionDeclStmt;
struct ClassDeclStmt;
struct InterfaceDeclStmt;

// Constraints belong to the allocation, never to a runtime's address registry.
struct ListConstraint { std::string element_type; };
struct FixedListConstraint { std::string element_type; std::size_t length = 0; };
struct DictConstraint { std::string key_type; std::string value_type; };
struct SetConstraint { std::string element_type; };

struct ListeData : RefCounted
{
    std::vector<Value> elements;
    std::optional<ListConstraint> constraint;

    void trace_references(RefVisitor &visitor) const override;
    void clear_references() override;
};
struct ListeFixeData : RefCounted
{
    std::vector<Value> elements;
    std::optional<FixedListConstraint> constraint;

    void trace_references(RefVisitor &visitor) const override;
    void clear_references() override;
};
/**
 * @brief Unordered-by-contract collection holding each element once.
 *
 * Elements keep insertion order so iteration is reproducible, and membership
 * uses the same index and the same key rule as a dictionary: an element must
 * stay equal to itself while it is stored.
 */
struct EnsembleData : RefCounted
{
    std::optional<SetConstraint> constraint;

    /** @brief The elements, in insertion order. */
    [[nodiscard]] const std::vector<Value> &items() const { return m_elements; }
    [[nodiscard]] std::size_t size() const { return m_elements.size(); }
    [[nodiscard]] bool empty() const { return m_elements.empty(); }
    void reserve(const std::size_t count) { m_elements.reserve(count); }

    [[nodiscard]] bool contains(const Value &element) const;
    /** @brief Adds @p element. Returns true when it was not already present. */
    bool insert(Value element);
    /** @brief Removes @p element. Returns false when it was absent. */
    bool erase(const Value &element);

    void trace_references(RefVisitor &visitor) const override;
    void clear_references() override;


private:
    std::vector<Value> m_elements;
    /** Positions into m_elements, offset by one so that zero reads as empty. */
    std::vector<std::size_t> m_index;
};
using DictEntry = std::pair<Value, Value>;

/**
 * @brief Association table holding at most one entry per key.
 *
 * Entries keep insertion order, and reassigning an existing key keeps that
 * key's original position. Lookup consults an open-addressed index over
 * value_hash once the table grows past a handful of entries; below that it
 * scans, which spares small dictionaries an allocation they would not profit
 * from. The entry vector is private because the index stores positions into
 * it, and any removal shifts them.
 */
struct DictData : RefCounted
{
    std::optional<DictConstraint> constraint;

    /** @brief The entries, in insertion order. */
    [[nodiscard]] const std::vector<DictEntry> &items() const { return m_entries; }
    [[nodiscard]] std::size_t size() const { return m_entries.size(); }
    [[nodiscard]] bool empty() const { return m_entries.empty(); }
    void reserve(const std::size_t count) { m_entries.reserve(count); }

    /** @brief Entry whose key equals @p key, or nullptr. */
    [[nodiscard]] const DictEntry *find(const Value &key) const;

    /** @brief Inserts or overwrites @p key. Returns true when a new key was added. */
    bool set(Value key, Value value);

    /** @brief Removes @p key, writing its value to @p removed. Returns false if absent. */
    bool erase(const Value &key, Value &removed);

    void trace_references(RefVisitor &visitor) const override;
    void clear_references() override;


private:
    std::vector<DictEntry> m_entries;
    /** Positions into m_entries, offset by one so that zero reads as empty. */
    std::vector<std::size_t> m_index;

    DictEntry *find_mutable(const Value &key);
    void rebuild_index();
    /** @brief Slot holding @p key, or the empty slot where it belongs. */
    [[nodiscard]] std::size_t probe(const Value &key, std::size_t hash) const;
};

/**
 * @brief Explains, in French, why @p key cannot be a dictionary key.
 *
 * A key must stay equal to itself for as long as it is stored. Every value
 * qualifies except a non-number, which is not equal to itself, so storing one
 * would create an entry that can never be found again. Text parsing reaches
 * that value: "nan".en_decimal() succeeds. Returns std::nullopt when the key
 * is admissible.
 */
std::optional<std::string> dictionary_key_rejection(const Value &key);

/**
 * @brief Hash consistent with Value::operator==.
 *
 * Equal values must hash equally. Change this function and that operator
 * together, or dictionary lookup starts missing entries that are present.
 */
std::size_t value_hash(const Value &value);
struct ResultData;

/**
 * @brief Shared text storage.
 *
 * Text used to sit inside the value as a std::string, which made a Value 48
 * bytes and copying one an allocation and a memcpy of the whole buffer. Sharing
 * the buffer makes a copy a reference count. Lumière text is immutable, which
 * is enforced by `as_texte` handing out a const reference rather than by the
 * pointee's type: the buffer is stored without const so it can share the one
 * type-erased handle every heap value uses.
 */
struct TexteData : RefCounted
{
    std::string text;

    explicit TexteData(std::string value) : text(std::move(value)) { mark_acyclic(); }

    // Text holds no references, so it can never take part in a cycle.
    void trace_references(RefVisitor &) const override {}
    void clear_references() override {}
};

using TexteRef = Ref<TexteData>;

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
        RESULTAT      = 13,
        RIEN          = 14,
    };

    Type type = Type::RIEN;

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
        v.m_payload.entier = n;
        return v;
    }

    static Value decimal(double d)
    {
        Value v;
        v.type = Type::DECIMAL;
        v.m_payload.decimal = d;
        return v;
    }

    static Value logique(bool b)
    {
        Value v;
        v.type = Type::LOGIQUE;
        v.m_payload.logique = b;
        return v;
    }

    static Value symbole(char32_t chtr)
    {
        Value v;
        v.type = Type::SYMBOLE;
        v.m_payload.symbole = chtr;
        return v;
    }

    static Value texte(std::string str)
    {
        Value v;
        v.type = Type::TEXTE;
        v.m_ref = make_ref<TexteData>(std::move(str));
        return v;
    }

    /** @brief Shares an existing text buffer instead of copying it. */
    static Value texte(TexteRef str)
    {
        Value v;
        v.type = Type::TEXTE;
        v.m_ref = std::move(str);
        return v;
    }

    static Value liste(Ref<ListeData> lst)
    {
        Value v;
        v.type = Type::LISTE;
        v.m_ref = std::move(lst);
        return v;
    }

    static Value dictionnaire(Ref<DictData> data)
    {
        Value v;
        v.type = Type::DICTIONNAIRE;
        v.m_ref = std::move(data);
        return v;
    }

    static Value liste_fixe(Ref<ListeFixeData> lst)
    {
        Value v;
        v.type = Type::LISTE_FIXE;
        v.m_ref = std::move(lst);
        return v;
    }

    static Value ensemble(Ref<EnsembleData> ens)
    {
        Value v;
        v.type = Type::ENSEMBLE;
        v.m_ref = std::move(ens);
        return v;
    }

    static Value objet(Ref<LumiereObject> obj);

    static Value fonction(Ref<LumiereFunction> fn);

    static Value classe(Ref<LumiereClass> cls);

    static Value interface(Ref<LumiereInterface> iface);

    static Value resultat(
        bool success,
        Value payload,
        std::optional<RuntimeSite> origin = std::nullopt);

    // Returns a copy of a failing Résultat with an extra frame appended to its
    // propagation traceback. Non-result values and successful results are
    // returned unchanged. Consecutive identical frames are collapsed.
    Value with_trace_frame(const TraceFrame &frame) const;

    //accessors

    int64_t     as_entier()  const { assert(is_entier());   return m_payload.entier; }
    double      as_decimal() const { assert(is_decimal());  return m_payload.decimal; }
    bool        as_logique() const { assert(is_logique());  return m_payload.logique; }
    char32_t    as_symbole() const { assert(is_symbole());  return m_payload.symbole; }

    const std::string &as_texte() const
    {
        assert(is_texte());
        return static_cast<const TexteData *>(m_ref.get())->text;
    }

    /** @brief The shared buffer, for handing text on without copying it. */
    TexteRef as_texte_ref() const
    {
        assert(is_texte());
        return TexteRef(static_cast<TexteData *>(m_ref.get()));
    }

    Ref<ListeData> as_liste() const
    {
        assert(is_liste());
        return Ref<ListeData>(static_cast<ListeData *>(m_ref.get()));
    }

    Ref<DictData> as_dictionnaire() const
    {
        assert(is_dictionnaire());
        return Ref<DictData>(static_cast<DictData *>(m_ref.get()));
    }

    Ref<ListeFixeData> as_liste_fixe() const
    {
        assert(is_liste_fixe());
        return Ref<ListeFixeData>(static_cast<ListeFixeData *>(m_ref.get()));
    }

    Ref<EnsembleData> as_ensemble() const
    {
        assert(is_ensemble());
        return Ref<EnsembleData>(static_cast<EnsembleData *>(m_ref.get()));
    }

    Ref<LumiereObject> as_objet() const;

    Ref<LumiereFunction> as_fonction() const;

    Ref<LumiereClass> as_classe() const;

    Ref<LumiereInterface> as_interface() const;

    Ref<const ResultData> as_resultat() const;

    /** @brief Address of the shared object, for identity comparison and hashing. */
    const void *ref_identity() const { return m_ref.get(); }

    /** @brief The counted object this value holds, or nullptr for a scalar. */
    RefCounted *ref() const noexcept { return m_ref.get(); }

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
    bool is_resultat()    const { return type == Type::RESULTAT; }
    bool is_numeric()     const { return is_entier() || is_decimal(); }

    //equality

    bool operator==(const Value &other) const;
    bool operator!=(const Value &other) const { return !(*this == other); }

    // display

    std::string to_string() const;
    std::string type_name() const;

private:
    /**
     * Scalars live in the payload, and every heap type shares one type-erased
     * handle. Copying a Value is therefore a tag, eight bytes and at most one
     * reference count, never a switch over fourteen alternatives — which is
     * what a std::variant generates, and what an integer pushed on the stack
     * used to pay for even though it owns nothing.
     *
     * `type` says which member is live. Reading any other one is undefined, so
     * every accessor asserts its tag; the assertions are active in the Debug
     * and sanitizer builds that run the suite, and compile away in Release.
     */
    union Payload
    {
        std::int64_t entier;
        double decimal;
        bool logique;
        char32_t symbole;
    };

    Payload m_payload {};
    Ref<RefCounted> m_ref;
};

struct TraceFrame
{
    std::string function_name;
    std::string source_path;
    uint32_t line = 0;
    uint32_t column = 0;
};

struct ResultData : RefCounted
{
    bool success;
    Value payload;
    std::optional<RuntimeSite> origin;
    std::vector<TraceFrame> trace;

    // Carrying a reference count makes this no longer an aggregate.
    ResultData(const bool success,
               Value payload,
               std::optional<RuntimeSite> origin,
               std::vector<TraceFrame> trace)
        : success(success), payload(std::move(payload)), origin(std::move(origin)), trace(std::move(trace))
    {
    }

    void trace_references(RefVisitor &visitor) const override;
    void clear_references() override;

};

//  LumiereFunction
//  A callable, either a user-defined function (eg obj.do_something())
//  or a bound method carrying its receiver (eg ici.do_something())
struct RuntimeFunctionBody
{
    virtual ~RuntimeFunctionBody() = default;
};

struct LumiereFunction : RefCounted
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

    void trace_references(RefVisitor &visitor) const override;
    void clear_references() override;

};

struct RuntimeClassBody
{
    virtual ~RuntimeClassBody() = default;
};

struct RuntimeInterfaceBody
{
    virtual ~RuntimeInterfaceBody() = default;
};

struct LumiereClass : RefCounted
{
    std::string name;
    std::string type_identity;
    std::shared_ptr<RuntimeClassBody> body;
    Ref<LumiereClass> parent;
    std::unordered_map<std::string, Ref<LumiereInterface>> interfaces;

    void trace_references(RefVisitor &visitor) const override;
    void clear_references() override;

};

struct LumiereInterface : RefCounted
{
    std::string name;
    std::string type_identity;
    std::shared_ptr<RuntimeInterfaceBody> body;

    void trace_references(RefVisitor &visitor) const override;
    void clear_references() override;

};

//  LumiereObject
//  A class instance at runtime.
struct LumiereObject : RefCounted
{
    Ref<LumiereClass>           klass;
    std::shared_ptr<void>                   native_state;
    std::unordered_map<std::string, Value> fields;

    void trace_references(RefVisitor &visitor) const override;
    void clear_references() override;

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
    std::unordered_map<std::string, TypeExpr> type_aliases;
    std::unordered_set<std::string> public_type_aliases;
    std::unordered_map<std::string, Value> public_type_values;
};

// Defined here rather than in the class body: downcasting the shared handle
// needs these types complete, and they are declared below Value.
inline Value Value::objet(Ref<LumiereObject> obj)
{
    Value v;
    v.type = Type::OBJET;
    v.m_ref = std::move(obj);
    return v;
}

inline Value Value::fonction(Ref<LumiereFunction> fn)
{
    Value v;
    v.type = Type::FONCTION;
    v.m_ref = std::move(fn);
    return v;
}

inline Value Value::classe(Ref<LumiereClass> cls)
{
    Value v;
    v.type = Type::CLASSE;
    v.m_ref = std::move(cls);
    return v;
}

inline Value Value::interface(Ref<LumiereInterface> iface)
{
    Value v;
    v.type = Type::INTERFACE;
    v.m_ref = std::move(iface);
    return v;
}

inline Ref<LumiereObject> Value::as_objet() const
{
    assert(is_objet());
    return Ref<LumiereObject>(static_cast<LumiereObject *>(m_ref.get()));
}

inline Ref<LumiereFunction> Value::as_fonction() const
{
    assert(is_fonction());
    return Ref<LumiereFunction>(static_cast<LumiereFunction *>(m_ref.get()));
}

inline Ref<LumiereClass> Value::as_classe() const
{
    assert(is_classe());
    return Ref<LumiereClass>(static_cast<LumiereClass *>(m_ref.get()));
}

inline Ref<LumiereInterface> Value::as_interface() const
{
    assert(is_interface());
    return Ref<LumiereInterface>(static_cast<LumiereInterface *>(m_ref.get()));
}

inline Ref<const ResultData> Value::as_resultat() const
{
    assert(is_resultat());
    return Ref<const ResultData>(static_cast<const ResultData *>(m_ref.get()));
}

} // namespace lumiere
