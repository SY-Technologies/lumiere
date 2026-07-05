#include "../luminet_shared.hpp"

#include <memory>
#include <optional>
#include <string>

namespace lumiere
{

Value make_luminet_tcp_module(const NativeFunctionFactory &make_native_function)
{
    auto tcp = make_hidden_typed_object("LumiNet.TCP");
    bind_object_method(
        tcp,
        make_native_function,
        "connecter",
        [make_native_function](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            if (args.size() < 2 || args.size() > 3)
            {
                runtime.raise_runtime_error(native_args.site, "LumiNet.TCP.connecter requiert 2 ou 3 argument(s)");
            }
            const std::string host = stdlib_expect_text(runtime, args[0].value, "LumiNet.TCP.connecter", native_args.site);
            const int64_t port = stdlib_expect_integer(runtime, args[1].value, "LumiNet.TCP.connecter", native_args.site);
            std::optional<int64_t> timeout_ms;
            if (args.size() == 3)
            {
                if (!args[2].name.empty() && args[2].name != "délai")
                {
                    runtime.raise_runtime_error(native_args.site, "LumiNet.TCP.connecter n'accepte que l'argument nommé délai");
                }
                timeout_ms = expect_duration_millis(runtime, args[2].value, "LumiNet.TCP.connecter", native_args.site);
            }

            addrinfo hints{};
            hints.ai_family = AF_UNSPEC;
            hints.ai_socktype = SOCK_STREAM;
            addrinfo *result = nullptr;
            const std::string port_text = std::to_string(port);
            const int rc = ::getaddrinfo(host.c_str(), port_text.c_str(), &hints, &result);
            if (rc != 0)
            {
                raise_network_error(runtime, native_args.site, "LumiNet.TCP.connecter", gai_strerror(rc));
            }
            std::unique_ptr<addrinfo, decltype(&::freeaddrinfo)> guard(result, ::freeaddrinfo);

            SocketHandle fd = kInvalidSocketHandle;
            std::string peer_address;
            for (addrinfo *entry = result; entry != nullptr; entry = entry->ai_next)
            {
                initialize_socket_platform();
                fd = ::socket(entry->ai_family, entry->ai_socktype, entry->ai_protocol);
                if (!socket_handle_valid(fd))
                {
                    continue;
                }
                if (timeout_ms.has_value())
                {
                    apply_timeout(runtime, fd, *timeout_ms, "LumiNet.TCP.connecter", native_args.site);
                }
                if (::connect(fd, entry->ai_addr, entry->ai_addrlen) == 0)
                {
                    peer_address = address_to_text(entry->ai_addr);
                    break;
                }
                close_socket_fd(fd);
            }

            if (!socket_handle_valid(fd))
            {
                raise_network_error(runtime, native_args.site, "LumiNet.TCP.connecter", socket_error_text("connexion"));
            }

            auto state = std::make_shared<TcpConnectionState>();
            state->fd = fd;
            return make_tcp_connection_value(state, peer_address.empty() ? host : peer_address, port, make_native_function);
        });
    bind_object_method(
        tcp,
        make_native_function,
        "Serveur",
        [make_native_function](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            stdlib_expect_positional(runtime, *native_args.arguments, 0, "LumiNet.TCP.Serveur", native_args.site);
            return make_tcp_server_value(std::make_shared<TcpServerState>(), make_native_function);
        });
    return Value::objet(std::move(tcp));
}

Value make_luminet_udp_module(const NativeFunctionFactory &make_native_function)
{
    auto udp = make_hidden_typed_object("LumiNet.UDP");
    bind_object_method(
        udp,
        make_native_function,
        "ouvrir",
        [make_native_function](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            if (args.size() > 1)
            {
                runtime.raise_runtime_error(native_args.site, "LumiNet.UDP.ouvrir requiert au plus 1 argument");
            }
            for (const auto &arg : args)
            {
                if (!arg.name.empty())
                {
                    runtime.raise_runtime_error(native_args.site, "LumiNet.UDP.ouvrir n'accepte pas d'arguments nommés");
                }
            }
            const int64_t port = args.empty() ? 0 : stdlib_expect_integer(runtime, args[0].value, "LumiNet.UDP.ouvrir", native_args.site);
            if (port < 0 || port > 65535)
            {
                runtime.raise_runtime_error(native_args.site, "LumiNet.UDP.ouvrir requiert un port entre 0 et 65535");
            }

            initialize_socket_platform();
            const SocketHandle fd = ::socket(AF_INET, SOCK_DGRAM, 0);
            if (!socket_handle_valid(fd))
            {
                raise_network_error(runtime, native_args.site, "LumiNet.UDP.ouvrir", socket_error_text("socket"));
            }

            sockaddr_in addr{};
            addr.sin_family = AF_INET;
            addr.sin_addr.s_addr = htonl(INADDR_ANY);
            addr.sin_port = htons(static_cast<uint16_t>(port));
            if (::bind(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0)
            {
                int mutable_fd = fd;
                close_socket_fd(mutable_fd);
                raise_network_error(runtime, native_args.site, "LumiNet.UDP.ouvrir", socket_error_text("liaison"));
            }

            sockaddr_in bound{};
            socklen_t bound_len = sizeof(bound);
            if (::getsockname(fd, reinterpret_cast<sockaddr *>(&bound), &bound_len) != 0)
            {
                int mutable_fd = fd;
                close_socket_fd(mutable_fd);
                raise_network_error(runtime, native_args.site, "LumiNet.UDP.ouvrir", socket_error_text("port"));
            }

            auto state = std::make_shared<UdpSocketState>();
            state->fd = fd;
            state->port = ntohs(bound.sin_port);
            return make_udp_socket_value(state, make_native_function);
        });
    return Value::objet(std::move(udp));
}

} // namespace lumiere
