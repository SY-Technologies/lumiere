#include "native_globals.hpp"

#include "vm_error.hpp"

#include <cctype>
#include <iostream>
#include <stdexcept>
#include <utility>

namespace lumiere
{
namespace
{

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

Value afficher_inline(const std::vector<Value> &args)
{
    for (std::size_t i = 0; i < args.size(); ++i)
    {
        if (i > 0)
        {
            std::cout << ' ';
        }
        std::cout << args[i].to_string();
    }
    return Value::rien();
}

void require_no_args(const std::vector<Value> &args, const std::string &name)
{
    if (!args.empty())
    {
        throw VmRuntimeError("VM: " + name + " n'accepte pas d'arguments");
    }
}

Value lire(const std::vector<Value> &args)
{
    require_no_args(args, "lire");
    std::string line;
    std::getline(std::cin, line);
    return Value::texte(std::move(line));
}

Value lire_entier(const std::vector<Value> &args)
{
    require_no_args(args, "lire_entier");
    std::string line;
    std::getline(std::cin, line);
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
            throw std::invalid_argument("trailing input");
        }
        return Value::entier(value);
    }
    catch (...)
    {
        throw VmRuntimeError("VM: lire_entier requiert une entree numerique valide");
    }
}

Value lire_decimal(const std::vector<Value> &args)
{
    require_no_args(args, "lire_decimal");
    std::string line;
    std::getline(std::cin, line);
    try
    {
        std::size_t parsed = 0;
        const double value = std::stod(line, &parsed);
        while (parsed < line.size() && std::isspace(static_cast<unsigned char>(line[parsed])))
        {
            ++parsed;
        }
        if (parsed != line.size())
        {
            throw std::invalid_argument("trailing input");
        }
        return Value::decimal(value);
    }
    catch (...)
    {
        throw VmRuntimeError("VM: lire_decimal requiert une entree numerique valide");
    }
}

Value lire_logique(const std::vector<Value> &args)
{
    require_no_args(args, "lire_logique");
    std::string line;
    std::getline(std::cin, line);
    if (line == "vrai")
    {
        return Value::logique(true);
    }
    if (line == "faux")
    {
        return Value::logique(false);
    }
    throw VmRuntimeError("VM: lire_logique requiert 'vrai' ou 'faux'");
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

} // namespace

std::unordered_map<std::string, NativeFunction> native_globals()
{
    return {
        {"afficher", &afficher},
        {"afficher_inline", &afficher_inline},
        {"lire", &lire},
        {"lire_entier", &lire_entier},
        {"lire_décimal", &lire_decimal},
        {"lire_decimal", &lire_decimal},
        {"lire_logique", &lire_logique},
        {"type_de", &type_de},
    };
}

} // namespace lumiere
