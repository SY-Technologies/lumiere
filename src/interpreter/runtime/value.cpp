#include "lumiere/interpreter/runtime/value.hpp"
#include "lumiere/parser/utf8.hpp"

#include <cmath>

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
    value.data = std::make_shared<const ResultData>(
        ResultData{
            success,
            std::move(payload),
            success ? std::nullopt : std::move(origin),
            {}});
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
    value.data = std::make_shared<const ResultData>(
        ResultData{
            result->success,
            result->payload,
            result->origin,
            std::move(trace)});
    return value;
}

DictEntry *DictData::find(const Value &key)
{
    for (DictEntry &entry : entries)
    {
        if (entry.first == key)
        {
            return &entry;
        }
    }
    return nullptr;
}

const DictEntry *DictData::find(const Value &key) const
{
    return const_cast<DictData *>(this)->find(key);
}

bool DictData::set(Value key, Value value)
{
    if (DictEntry *existing = find(key))
    {
        existing->second = std::move(value);
        return false;
    }
    entries.emplace_back(std::move(key), std::move(value));
    return true;
}

bool DictData::erase(const Value &key, Value &removed)
{
    for (auto it = entries.begin(); it != entries.end(); ++it)
    {
        if (it->first == key)
        {
            removed = std::move(it->second);
            entries.erase(it);
            return true;
        }
    }
    return false;
}

std::optional<std::string> dictionary_key_rejection(const Value &key)
{
    if (key.is_decimal() && std::isnan(key.as_decimal()))
    {
        return std::string(
            "une valeur non-nombre ne peut pas servir de cle : elle n'est egale a aucune valeur, pas meme a elle-meme");
    }
    return std::nullopt;
}

bool Value::operator==(const Value &other) const
{
    if (type != other.type)
    {
        return false;
    }

    if (is_rien())// we know they both have the same RIEN type else we would have returned above
    {
        return true;
    }
    if (is_resultat())
    {
        return as_resultat()->success == other.as_resultat()->success &&
               as_resultat()->payload == other.as_resultat()->payload;
    }
    if (is_liste_fixe())
    {
        // A fixed list is a value, not a handle: two of them are equal when their
        // elements are. std::vector reapplies this operator element by element, so
        // nested fixed lists compare structurally and every other type keeps its own
        // rule. Both engines used to carry this case separately, which left the
        // payload comparison above matching allocations instead of contents.
        const auto &left = as_liste_fixe();
        const auto &right = other.as_liste_fixe();
        if (left == right)
        {
            return true;
        }
        if (left == nullptr || right == nullptr)
        {
            return false;
        }
        return left->elements == right->elements;
    }

    return data == other.data;
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
        for (std::size_t i = 0; i < as_dictionnaire()->entries.size(); ++i)
        {
            if (i > 0)
            {
                out << ", ";
            }
            out << as_dictionnaire()->entries[i].first.to_string()
                << ": "
                << as_dictionnaire()->entries[i].second.to_string();
        }
        out << "}";
        break;
    case Type::ENSEMBLE:
        out << "<ensemble>";
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
        return "Decimal";
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

} // namespace lumiere
