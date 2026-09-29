#include "native_globals.hpp"

#include "lumiere/interpreter/stdlib/helpers.hpp"
#include "lumiere/interpreter/runtime/numeric.hpp"
#include "vm_error.hpp"

#include <cctype>
#include <iostream>
#include <stdexcept>
#include <utility>

namespace lumiere
{
namespace
{

Value input_failure(
    const std::string &operation,
    std::string cause)
{
    return Value::resultat(
        false,
        stdlib_error_value(
            "ErreurEntrée",
            operation,
            std::move(cause)));
}

Value afficher(const std::vector<Value> &args)
{
    for (std::size_t i = 0; i < args.size(); ++i)
    {
        if (i > 0)
        {
            std::cout << ' ';
        }
        std::cout << args[i].to_string();
    }
    std::cout << '\n';
    return Value::rien();
}

void require_no_args(const std::vector<Value> &args, const std::string &name)
{
    if (!args.empty())
    {
        throw VmRuntimeError("VM: " + name + " n'accepte pas d'arguments");
    }
}

void print_optional_prompt(const std::vector<Value> &args, const std::string &name)
{
    if (args.size() > 1)
    {
        throw VmRuntimeError("VM: " + name + " accepte au plus 1 argument");
    }
    if (!args.empty())
    {
        if (!args[0].is_texte())
        {
            throw VmRuntimeError("VM: " + name + " attend une invite de type Texte");
        }
        std::cout << args[0].as_texte();
    }
}

Value lire(const std::vector<Value> &args)
{
    print_optional_prompt(args, "lire");
    std::string line;
    if (!std::getline(std::cin, line))
    {
        return input_failure("lire", "fin de l'entrée");
    }
    return Value::resultat(
        true,
        Value::texte(std::move(line)));
}

Value lire_entier(const std::vector<Value> &args)
{
    print_optional_prompt(args, "lire_entier");
    std::string line;
    if (!std::getline(std::cin, line))
    {
        return input_failure(
            "lire_entier",
            "fin de l'entrée");
    }
    try
    {
        std::size_t parsed = 0;
        const std::int64_t value = std::stoll(line, &parsed);
        while (parsed < line.size() && std::isspace(static_cast<unsigned char>(line[parsed])))
        {
            ++parsed;
        }
        if (parsed != line.size())
        {
            throw std::invalid_argument("caractères restants");
        }
        return Value::resultat(
            true,
            Value::entier(value));
    }
    catch (...)
    {
        return input_failure(
            "lire_entier",
            "entier invalide: " + line);
    }
}

Value lire_decimal(const std::vector<Value> &args)
{
    require_no_args(args, "lire_decimal");
    std::string line;
    if (!std::getline(std::cin, line))
    {
        return input_failure(
            "lire_décimal",
            "fin de l'entrée");
    }
    while (!line.empty() && std::isspace(static_cast<unsigned char>(line.back())))
    {
        line.pop_back();
    }
    if (const auto value = numeric::parse_decimal(line))
    {
        return Value::resultat(true, Value::decimal(*value));
    }
    return input_failure(
        "lire_décimal",
        "décimal invalide: " + line);
}

Value lire_logique(const std::vector<Value> &args)
{
    require_no_args(args, "lire_logique");
    std::string line;
    if (!std::getline(std::cin, line))
    {
        return input_failure(
            "lire_logique",
            "fin de l'entrée");
    }
    if (line == "vrai")
    {
        return Value::resultat(
            true,
            Value::logique(true));
    }
    if (line == "faux")
    {
        return Value::resultat(
            true,
            Value::logique(false));
    }
    return input_failure(
        "lire_logique",
        "logique invalide: " + line);
}

Value type_de(const std::vector<Value> &args)
{
    if (args.size() != 1)
    {
        throw VmRuntimeError("VM: type_de requiert exactement 1 argument");
    }
    if (args[0].is_objet() && args[0].as_objet()->klass != nullptr)
    {
        return Value::texte(args[0].as_objet()->klass->name);
    }
    if (args[0].is_classe())
    {
        return Value::texte(args[0].as_classe()->name);
    }
    if (args[0].is_interface())
    {
        return Value::texte(args[0].as_interface()->name);
    }
    return Value::texte(args[0].type_name());
}

Value succes(const std::vector<Value> &args)
{
    if (args.size() != 1)
    {
        throw VmRuntimeError("VM: Succès requiert exactement 1 argument");
    }
    return Value::resultat(true, args.front());
}

Value echec(const std::vector<Value> &args)
{
    if (args.size() != 1)
    {
        throw VmRuntimeError("VM: Échec requiert exactement 1 argument");
    }
    return Value::resultat(false, args.front());
}

} // namespace

std::unordered_map<std::string, NativeFunction> native_globals()
{
    return {
        {"afficher", &afficher},
        {"lire", &lire},
        {"lire_entier", &lire_entier},
        {"lire_décimal", &lire_decimal},
        {"lire_decimal", &lire_decimal},
        {"lire_logique", &lire_logique},
        {"type_de", &type_de},
        {"Succès", &succes},
        {"Échec", &echec},
    };
}

} // namespace lumiere
