#include "../luminet_shared.hpp"

#include <algorithm>
#include <arpa/inet.h>
#include <memory>
#include <netdb.h>
#include <netinet/in.h>
#include <string>
#include <unistd.h>

namespace lumiere
{

Value make_luminet_adresse_module(const NativeFunctionFactory &make_native_function)
{
    auto adresse = make_hidden_typed_object("LumiNet.Adresse");
    bind_object_method(
        adresse,
        make_native_function,
        "analyser",
        [make_native_function](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            stdlib_expect_positional(runtime, args, 1, "LumiNet.Adresse.analyser", native_args.site);
            const std::string text = stdlib_expect_text(runtime, args[0].value, "LumiNet.Adresse.analyser", native_args.site);
            const auto [host, port] = parse_host_port(runtime, text, "LumiNet.Adresse.analyser", native_args.site);
            return make_address_value(host, port, make_native_function);
        });
    bind_object_method(
        adresse,
        make_native_function,
        "est_valide",
        [](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            stdlib_expect_positional(runtime, args, 1, "LumiNet.Adresse.est_valide", native_args.site);
            const std::string text = stdlib_expect_text(runtime, args[0].value, "LumiNet.Adresse.est_valide", native_args.site);
            return Value::logique(parse_ipv4_only(text) || parse_ipv6_only(text) || text == "localhost");
        });
    bind_object_method(
        adresse,
        make_native_function,
        "est_ipv4",
        [](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            stdlib_expect_positional(runtime, args, 1, "LumiNet.Adresse.est_ipv4", native_args.site);
            return Value::logique(parse_ipv4_only(stdlib_expect_text(runtime, args[0].value, "LumiNet.Adresse.est_ipv4", native_args.site)));
        });
    bind_object_method(
        adresse,
        make_native_function,
        "est_ipv6",
        [](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            stdlib_expect_positional(runtime, args, 1, "LumiNet.Adresse.est_ipv6", native_args.site);
            return Value::logique(parse_ipv6_only(stdlib_expect_text(runtime, args[0].value, "LumiNet.Adresse.est_ipv6", native_args.site)));
        });
    bind_object_method(
        adresse,
        make_native_function,
        "est_locale",
        [](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            stdlib_expect_positional(runtime, args, 1, "LumiNet.Adresse.est_locale", native_args.site);
            return Value::logique(is_local_host_text(stdlib_expect_text(runtime, args[0].value, "LumiNet.Adresse.est_locale", native_args.site)));
        });
    bind_object_method(
        adresse,
        make_native_function,
        "locale",
        [make_native_function](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            stdlib_expect_positional(runtime, *native_args.arguments, 0, "LumiNet.Adresse.locale", native_args.site);
            char host_name[256] = {};
            if (::gethostname(host_name, sizeof(host_name)) != 0)
            {
                raise_network_error(runtime, native_args.site, "LumiNet.Adresse.locale", socket_error_text("nom d'hôte"));
            }
            addrinfo hints{};
            hints.ai_family = AF_UNSPEC;
            hints.ai_socktype = SOCK_STREAM;
            addrinfo *result = nullptr;
            const int rc = ::getaddrinfo(host_name, nullptr, &hints, &result);
            if (rc != 0)
            {
                return make_address_value("127.0.0.1", 0, make_native_function);
            }
            std::unique_ptr<addrinfo, decltype(&::freeaddrinfo)> guard(result, ::freeaddrinfo);
            std::string host = "127.0.0.1";
            for (addrinfo *entry = result; entry != nullptr; entry = entry->ai_next)
            {
                const std::string candidate = address_to_text(entry->ai_addr);
                if (!candidate.empty())
                {
                    host = candidate;
                    break;
                }
            }
            return make_address_value(host, 0, make_native_function);
        });
    return Value::objet(std::move(adresse));
}

Value make_luminet_dns_module(const NativeFunctionFactory &make_native_function)
{
    auto dns = make_hidden_typed_object("LumiNet.DNS");
    bind_object_method(
        dns,
        make_native_function,
        "résoudre",
        [](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            stdlib_expect_positional(runtime, args, 1, "LumiNet.DNS.résoudre", native_args.site);
            const std::string host = stdlib_expect_text(runtime, args[0].value, "LumiNet.DNS.résoudre", native_args.site);
            addrinfo hints{};
            hints.ai_family = AF_UNSPEC;
            addrinfo *result = nullptr;
            const int rc = ::getaddrinfo(host.c_str(), nullptr, &hints, &result);
            if (rc != 0)
            {
                raise_network_error(runtime, native_args.site, "LumiNet.DNS.résoudre", gai_strerror(rc));
            }
            std::unique_ptr<addrinfo, decltype(&::freeaddrinfo)> guard(result, ::freeaddrinfo);
            for (addrinfo *entry = result; entry != nullptr; entry = entry->ai_next)
            {
                const std::string address = address_to_text(entry->ai_addr);
                if (!address.empty())
                {
                    return Value::texte(address);
                }
            }
            raise_network_error(runtime, native_args.site, "LumiNet.DNS.résoudre", "aucune adresse trouvée");
            return Value::rien();
        });
    bind_object_method(
        dns,
        make_native_function,
        "résoudre_tous",
        [](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            stdlib_expect_positional(runtime, args, 1, "LumiNet.DNS.résoudre_tous", native_args.site);
            const std::string host = stdlib_expect_text(runtime, args[0].value, "LumiNet.DNS.résoudre_tous", native_args.site);
            addrinfo hints{};
            hints.ai_family = AF_UNSPEC;
            addrinfo *result = nullptr;
            const int rc = ::getaddrinfo(host.c_str(), nullptr, &hints, &result);
            if (rc != 0)
            {
                raise_network_error(runtime, native_args.site, "LumiNet.DNS.résoudre_tous", gai_strerror(rc));
            }
            std::unique_ptr<addrinfo, decltype(&::freeaddrinfo)> guard(result, ::freeaddrinfo);
            auto list = std::make_shared<ListeData>();
            std::vector<std::string> seen;
            for (addrinfo *entry = result; entry != nullptr; entry = entry->ai_next)
            {
                const std::string address = address_to_text(entry->ai_addr);
                if (!address.empty() &&
                    std::find(seen.begin(), seen.end(), address) == seen.end())
                {
                    seen.push_back(address);
                    list->elements.push_back(Value::texte(address));
                }
            }
            Value values = Value::liste(std::move(list));
            runtime.annotate_value(values, "Liste[Texte]", native_args.site);
            return values;
        });
    bind_object_method(
        dns,
        make_native_function,
        "résoudre_inverse",
        [](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            stdlib_expect_positional(runtime, args, 1, "LumiNet.DNS.résoudre_inverse", native_args.site);
            const std::string ip = stdlib_expect_text(runtime, args[0].value, "LumiNet.DNS.résoudre_inverse", native_args.site);
            sockaddr_storage storage{};
            socklen_t len = 0;
            if (parse_ipv4_only(ip))
            {
                auto *addr = reinterpret_cast<sockaddr_in *>(&storage);
                addr->sin_family = AF_INET;
                ::inet_pton(AF_INET, ip.c_str(), &addr->sin_addr);
                len = sizeof(sockaddr_in);
            }
            else if (parse_ipv6_only(ip))
            {
                auto *addr = reinterpret_cast<sockaddr_in6 *>(&storage);
                addr->sin6_family = AF_INET6;
                ::inet_pton(AF_INET6, ip.c_str(), &addr->sin6_addr);
                len = sizeof(sockaddr_in6);
            }
            else
            {
                runtime.raise_runtime_error(native_args.site, "LumiNet.DNS.résoudre_inverse requiert une adresse IP valide");
            }
            char host[NI_MAXHOST] = {};
            const int rc = ::getnameinfo(reinterpret_cast<sockaddr *>(&storage), len, host, sizeof(host), nullptr, 0, 0);
            if (rc != 0)
            {
                raise_network_error(runtime, native_args.site, "LumiNet.DNS.résoudre_inverse", gai_strerror(rc));
            }
            return Value::texte(host);
        });
    return Value::objet(std::move(dns));
}

} // namespace lumiere
