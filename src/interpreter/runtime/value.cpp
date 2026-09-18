#include "lumiere/interpreter/runtime/value.hpp"
#include "lumiere/interpreter/runtime/cycles.hpp"
#include "lumiere/parser/utf8.hpp"

#include <cmath>
#include "lumiere/diagnostics/runtime_messages.hpp"
#include <cstdint>
#include <type_traits>

#include <sstream>

namespace lumiere
{

Value Value::resultat(
    const bool success,
    Value payload,
    std::optional<RuntimeSite> origin)
{
    Value value;
    value.type = Type::RESULTAT;
    value.m_ref = make_ref<ResultData>(
        success,
        std::move(payload),
        success ? std::nullopt : std::move(origin),
        std::vector<TraceFrame>{});
    return value;
}

Value Value::with_trace_frame(const TraceFrame &frame) const
{
    if (!is_resultat() || as_resultat()->success)
    {
        return *this;
    }

    const auto result = as_resultat();
    std::vector<TraceFrame> trace = result->trace;
    if (!trace.empty() &&
        trace.back().function_name == frame.function_name &&
        trace.back().source_path == frame.source_path &&
        trace.back().line == frame.line &&
        trace.back().column == frame.column)
    {
        return *this;
    }
    trace.push_back(frame);

    Value value;
    value.type = Type::RESULTAT;
    value.m_ref = make_ref<ResultData>(
        result->success,
        result->payload,
        result->origin,
        std::move(trace));
    return value;
}

namespace
{

/** Below this many entries, a scan beats an index and its allocation. */
constexpr std::size_t kIndexThreshold = 8;

std::size_t hash_mix(const std::size_t seed, const std::size_t value)
{
    return seed ^ (value + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2));
}

/**
 * @brief Slot holding @p key, or the empty slot where it belongs.
 *
 * A dictionary indexes pairs by their first element and a set indexes values by
 * themselves, so the entry type and the way a key is read out of it are the only
 * differences between the two tables.
 */
template <typename Entry, typename KeyOf>
std::size_t probe_index(const std::vector<std::size_t> &index,
                        const std::vector<Entry> &entries,
                        const KeyOf &key_of,
                        const Value &key,
                        const std::size_t hash)
{
    const std::size_t mask = index.size() - 1;
    std::size_t slot = hash & mask;
    while (index[slot] != 0 && !(key_of(entries[index[slot] - 1]) == key))
    {
        slot = (slot + 1) & mask;
    }
    return slot;
}

template <typename Entry, typename KeyOf>
void rebuild_positions(std::vector<std::size_t> &index,
                       const std::vector<Entry> &entries,
                       const KeyOf &key_of)
{
    if (entries.size() < kIndexThreshold)
    {
        index.clear();
        return;
    }

    std::size_t capacity = 16;
    while (capacity * 3 < (entries.size() + 1) * 4)
    {
        capacity *= 2;
    }

    index.assign(capacity, 0);
    for (std::size_t position = 0; position < entries.size(); ++position)
    {
        const Value &key = key_of(entries[position]);
        index[probe_index(index, entries, key_of, key, value_hash(key))] = position + 1;
    }
}

/** @brief True when the table has grown past three quarters of its slots. */
inline bool index_is_crowded(const std::size_t entries, const std::size_t slots)
{
    return (entries + 1) * 4 > slots * 3;
}

constexpr auto dict_key = [](const DictEntry &entry) -> const Value & { return entry.first; };
constexpr auto set_key = [](const Value &element) -> const Value & { return element; };

} // namespace

std::size_t value_hash(const Value &value)
{
    const auto tag = static_cast<std::size_t>(value.type);
    switch (value.type)
    {
    case Value::Type::RIEN:
        return hash_mix(tag, 0);
    case Value::Type::ENTIER:
        return hash_mix(tag, std::hash<std::int64_t>{}(value.as_entier()));
    case Value::Type::DECIMAL:
    {
        const double number = value.as_decimal();
        // 0.0 and -0.0 compare equal, so they must hash alike. A non-number is
        // refused as a key, but the function stays total.
        if (number == 0.0)
        {
            return hash_mix(tag, std::hash<double>{}(0.0));
        }
        if (std::isnan(number))
        {
            return hash_mix(tag, 1);
        }
        return hash_mix(tag, std::hash<double>{}(number));
    }
    case Value::Type::LOGIQUE:
        return hash_mix(tag, value.as_logique() ? 1 : 0);
    case Value::Type::SYMBOLE:
        return hash_mix(tag, static_cast<std::size_t>(value.as_symbole()));
    case Value::Type::TEXTE:
        return hash_mix(tag, std::hash<std::string>{}(value.as_texte()));
    case Value::Type::LISTE_FIXE:
    {
        // Compared by content, so hashed by content.
        std::size_t seed = tag;
        if (const auto &list = value.as_liste_fixe())
        {
            for (const Value &element : list->elements)
            {
                seed = hash_mix(seed, value_hash(element));
            }
        }
        return seed;
    }
    case Value::Type::RESULTAT:
    {
        const auto &result = value.as_resultat();
        if (result == nullptr)
        {
            return hash_mix(tag, 0);
        }
        return hash_mix(hash_mix(tag, result->success ? 1 : 0), value_hash(result->payload));
    }
    default:
        break;
    }

    // Everything left is a handle, compared by identity and so hashed by address.
    return hash_mix(tag, std::hash<const void *>{}(value.ref_identity()));
}

std::size_t DictData::probe(const Value &key, const std::size_t hash) const
{
    return probe_index(m_index, m_entries, dict_key, key, hash);
}

void DictData::rebuild_index()
{
    rebuild_positions(m_index, m_entries, dict_key);
}

DictEntry *DictData::find_mutable(const Value &key)
{
    if (m_index.empty())
    {
        for (DictEntry &entry : m_entries)
        {
            if (entry.first == key)
            {
                return &entry;
            }
        }
        return nullptr;
    }

    const std::size_t slot = probe(key, value_hash(key));
    return m_index[slot] == 0 ? nullptr : &m_entries[m_index[slot] - 1];
}

const DictEntry *DictData::find(const Value &key) const
{
    return const_cast<DictData *>(this)->find_mutable(key);
}

bool DictData::set(Value key, Value value)
{
    if (DictEntry *existing = find_mutable(key))
    {
        existing->second = std::move(value);
        return false;
    }

    m_entries.emplace_back(std::move(key), std::move(value));
    if (m_entries.size() < kIndexThreshold)
    {
        return true;
    }
    if (m_index.empty() || index_is_crowded(m_entries.size(), m_index.size()))
    {
        rebuild_index();
        return true;
    }

    const Value &stored = m_entries.back().first;
    m_index[probe(stored, value_hash(stored))] = m_entries.size();
    return true;
}

bool DictData::erase(const Value &key, Value &removed)
{
    DictEntry *entry = find_mutable(key);
    if (entry == nullptr)
    {
        return false;
    }

    removed = std::move(entry->second);
    m_entries.erase(m_entries.begin() + (entry - m_entries.data()));
    // Removal shifts every later position, so the index is rebuilt rather than
    // patched. The vector erase is already linear.
    rebuild_index();
    return true;
}

bool EnsembleData::contains(const Value &element) const
{
    if (m_index.empty())
    {
        for (const Value &candidate : m_elements)
        {
            if (candidate == element)
            {
                return true;
            }
        }
        return false;
    }

    return m_index[probe_index(m_index, m_elements, set_key, element, value_hash(element))] != 0;
}

bool EnsembleData::insert(Value element)
{
    if (contains(element))
    {
        return false;
    }

    m_elements.push_back(std::move(element));
    if (m_elements.size() < kIndexThreshold)
    {
        return true;
    }
    if (m_index.empty() || index_is_crowded(m_elements.size(), m_index.size()))
    {
        rebuild_positions(m_index, m_elements, set_key);
        return true;
    }

    const Value &stored = m_elements.back();
    m_index[probe_index(m_index, m_elements, set_key, stored, value_hash(stored))] = m_elements.size();
    return true;
}

bool EnsembleData::erase(const Value &element)
{
    for (auto it = m_elements.begin(); it != m_elements.end(); ++it)
    {
        if (*it == element)
        {
            m_elements.erase(it);
            rebuild_positions(m_index, m_elements, set_key);
            return true;
        }
    }
    return false;
}

std::optional<std::string> dictionary_key_rejection(const Value &key)
{
    if (key.is_decimal() && std::isnan(key.as_decimal()))
    {
        return messages::cle_non_nombre();
    }
    return std::nullopt;
}

bool Value::operator==(const Value &other) const
{
    if (type != other.type)
    {
        return false;
    }

    switch (type)
    {
    case Type::RIEN:
        // Both carry the same tag, so there is nothing left to compare.
        return true;
    case Type::ENTIER:
        return as_entier() == other.as_entier();
    case Type::DECIMAL:
        // IEEE comparison, so 0.0 equals -0.0 and a non-number equals nothing.
        return as_decimal() == other.as_decimal();
    case Type::LOGIQUE:
        return as_logique() == other.as_logique();
    case Type::SYMBOLE:
        return as_symbole() == other.as_symbole();
    case Type::TEXTE:
    {
        // Sharing a buffer must not turn text equality into pointer equality.
        const void *left = ref_identity();
        const void *right = other.ref_identity();
        return left == right || as_texte() == other.as_texte();
    }
    case Type::LISTE_FIXE:
    {
        // A fixed list is a value, not a handle: two of them are equal when their
        // elements are. std::vector reapplies this operator element by element, so
        // nested fixed lists compare structurally and every other type keeps its
        // own rule.
        if (ref_identity() == other.ref_identity())
        {
            return true;
        }
        const auto &left = as_liste_fixe();
        const auto &right = other.as_liste_fixe();
        if (left == nullptr || right == nullptr)
        {
            return false;
        }
        return left->elements == right->elements;
    }
    case Type::RESULTAT:
        return as_resultat()->success == other.as_resultat()->success &&
               as_resultat()->payload == other.as_resultat()->payload;
    default:
        break;
    }

    // Everything left is a handle, compared by identity.
    return ref_identity() == other.ref_identity();
}

std::string Value::to_string() const
{
    std::ostringstream out;

    switch (type)
    {
    case Type::ENTIER:
        out << as_entier();
        break;
    case Type::DECIMAL:
        out << as_decimal();
        break;
    case Type::LOGIQUE:
        out << (as_logique() ? "vrai" : "faux");
        break;
    case Type::SYMBOLE:
        out << utf8::encode_character(as_symbole());
        break;
    case Type::TEXTE:
        out << as_texte();
        break;
    case Type::LISTE:
        out << "[";
        for (std::size_t i = 0; i < as_liste()->elements.size(); ++i)
        {
            if (i > 0)
            {
                out << ", ";
            }
            out << as_liste()->elements[i].to_string();
        }
        out << "]";
        break;
    case Type::LISTE_FIXE:
        out << "[";
        for (std::size_t i = 0; i < as_liste_fixe()->elements.size(); ++i)
        {
            if (i > 0)
            {
                out << ", ";
            }
            out << as_liste_fixe()->elements[i].to_string();
        }
        out << "]";
        break;
    case Type::DICTIONNAIRE:
        out << "{";
        for (std::size_t i = 0; i < as_dictionnaire()->size(); ++i)
        {
            if (i > 0)
            {
                out << ", ";
            }
            out << as_dictionnaire()->items()[i].first.to_string()
                << ": "
                << as_dictionnaire()->items()[i].second.to_string();
        }
        out << "}";
        break;
    case Type::ENSEMBLE:
        out << "{";
        for (std::size_t i = 0; i < as_ensemble()->items().size(); ++i)
        {
            if (i > 0)
            {
                out << ", ";
            }
            out << as_ensemble()->items()[i].to_string();
        }
        out << "}";
        break;
    case Type::OBJET:
        if (as_objet() != nullptr &&
            as_objet()->klass != nullptr)
        {
            out << as_objet()->klass->name;
            const auto cause =
                as_objet()->fields.find("cause");
            if (cause != as_objet()->fields.end())
            {
                out << '(' << cause->second.to_string() << ')';
            }
        }
        else
        {
            out << "<objet>";
        }
        break;
    case Type::FONCTION:
        out << "<fonction>";
        break;
    case Type::CLASSE:
        out << "<classe>";
        break;
    case Type::INTERFACE:
        out << "<interface>";
        break;
    case Type::RESULTAT:
        out << (as_resultat()->success ? "Succès(" : "Échec(")
            << as_resultat()->payload.to_string() << ')';
        break;
    case Type::RIEN:
        out << "rien";
        break;
    }

    return out.str();
}

std::string Value::type_name() const
{
    switch (type)
    {
    case Type::ENTIER:
        return "Entier";
    case Type::DECIMAL:
        // The accented spelling is the language's own name for the type. The
        // unaccented one is accepted in annotations as a convenience for
        // keyboards, but a diagnostic reports what the type is called.
        return "Décimal";
    case Type::LOGIQUE:
        return "Logique";
    case Type::SYMBOLE:
        return "Symbole";
    case Type::TEXTE:
        return "Texte";
    case Type::LISTE:
        return "Liste";
    case Type::LISTE_FIXE:
        return "ListeFixe";
    case Type::DICTIONNAIRE:
        return "Dictionnaire";
    case Type::ENSEMBLE:
        return "Ensemble";
    case Type::OBJET:
        return as_objet() != nullptr &&
                       as_objet()->klass != nullptr
                   ? as_objet()->klass->name
                   : "Objet";
    case Type::FONCTION:
        return "Fonction";
    case Type::CLASSE:
        return "Classe";
    case Type::INTERFACE:
        return "Interface";
    case Type::RESULTAT:
        return "Résultat";
    case Type::RIEN:
        return "Rien";
    }

    return "Inconnu";
}


// ── Cycle tracing ────────────────────────────────────────────────────────────
//
// Each object reports the references it holds so the collector can subtract the
// cycle's own edges from the counts. An edge must be reported exactly once, and
// only when it is genuinely held.
//
// Two kinds of reference are deliberately not reported, and both are recorded in
// the hardening notes: the captures inside a native handler's std::function,
// which C++ gives no way to enumerate, and the tree-walker's environments and
// bodies, which are not counted objects yet. Leaving an edge out only means a
// cycle through it is kept alive, never that something live is freed.

void trace_value(const Value &value, RefVisitor &visitor)
{
    if (RefCounted *held = value.ref())
    {
        visitor.visit(held);
    }
}

void ListeData::trace_references(RefVisitor &visitor) const
{
    for (const Value &element : elements)
    {
        trace_value(element, visitor);
    }
}

void ListeData::clear_references() { elements.clear(); }

void ListeFixeData::trace_references(RefVisitor &visitor) const
{
    for (const Value &element : elements)
    {
        trace_value(element, visitor);
    }
}

void ListeFixeData::clear_references() { elements.clear(); }

void EnsembleData::trace_references(RefVisitor &visitor) const
{
    for (const Value &element : m_elements)
    {
        trace_value(element, visitor);
    }
}

void EnsembleData::clear_references()
{
    m_elements.clear();
    m_index.clear();
}

void DictData::trace_references(RefVisitor &visitor) const
{
    for (const DictEntry &entry : m_entries)
    {
        trace_value(entry.first, visitor);
        trace_value(entry.second, visitor);
    }
}

void DictData::clear_references()
{
    m_entries.clear();
    m_index.clear();
}

void ResultData::trace_references(RefVisitor &visitor) const { trace_value(payload, visitor); }

void ResultData::clear_references() { payload = Value::rien(); }

CellData::CellData(Value initial) : value(std::move(initial)) {}

void CellData::trace_references(RefVisitor &visitor) const { trace_value(value, visitor); }

void CellData::clear_references() { value = Value::rien(); }

void LumiereFunction::trace_references(RefVisitor &visitor) const
{
    trace_value(receiver, visitor);
    if (body)
    {
        visitor.visit(body.get());
    }
    for (const Ref<RefCounted> &captured : native_captures)
    {
        if (captured)
        {
            visitor.visit(captured.get());
        }
    }
}

void LumiereFunction::clear_references()
{
    receiver = Value::rien();
    // The handler goes before its captures: it may hold raw pointers into them.
    native_handler = nullptr;
    native_captures.clear();
    body.reset();
}

void LumiereClass::trace_references(RefVisitor &visitor) const
{
    if (body)
    {
        visitor.visit(body.get());
    }
    if (parent)
    {
        visitor.visit(parent.get());
    }
    for (const auto &[name, interface] : interfaces)
    {
        if (interface)
        {
            visitor.visit(interface.get());
        }
    }
}

void LumiereClass::clear_references()
{
    parent.reset();
    interfaces.clear();
    body.reset();
}

void LumiereInterface::trace_references(RefVisitor &visitor) const
{
    if (body)
    {
        visitor.visit(body.get());
    }
}

void LumiereInterface::clear_references() { body.reset(); }

void LumiereObject::trace_references(RefVisitor &visitor) const
{
    if (klass)
    {
        visitor.visit(klass.get());
    }
    for (const auto &[name, field] : fields)
    {
        trace_value(field, visitor);
    }
    // The native half of the instance can hold Values of its own; a server's
    // route table is the usual case. Without this edge the cycle it closes
    // back to the defining scope would never be collectable.
    if (native_state)
    {
        visitor.visit(native_state.get());
    }
}

void LumiereObject::clear_references()
{
    klass.reset();
    fields.clear();
    native_state.reset();
}

} // namespace lumiere
