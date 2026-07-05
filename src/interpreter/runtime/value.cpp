#include "lumiere/interpreter/runtime/value.hpp"
#include "lumiere/parser/utf8.hpp"

#include <sstream>

namespace lumiere
{

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
        out << "<objet>";
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
        return "Objet";
    case Type::FONCTION:
        return "Fonction";
    case Type::CLASSE:
        return "Classe";
    case Type::INTERFACE:
        return "Interface";
    case Type::RIEN:
        return "Rien";
    }

    return "Inconnu";
}

} // namespace lumiere
