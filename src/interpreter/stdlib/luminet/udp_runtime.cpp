#include "../luminet_shared.hpp"

#include <arpa/inet.h>
#include <memory>
#include <netdb.h>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <unistd.h>

namespace lumiere
{

Value make_udp_socket_value(const std::shared_ptr<UdpSocketState> &state,
                            const NativeFunctionFactory &make_native_function)
{
    auto object = make_hidden_typed_object("SocketUDP");
    attach_native_state(object, state);
    object->fields["port"] = Value::entier(state->port);

    object->fields["fermer"] = Value::fonction(make_native_function(
        [state](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            stdlib_expect_positional(runtime, *native_args.arguments, 0, "SocketUDP.fermer", native_args.site);
            if (!state->closed && state->fd >= 0)
            {
                close_socket_fd(state->fd);
                state->closed = true;
            }
            return Value::rien();
        }));

    object->fields["définir_délai"] = Value::fonction(make_native_function(
        [state](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            stdlib_expect_positional(runtime, args, 1, "SocketUDP.définir_délai", native_args.site);
            apply_timeout(runtime,
                          state->fd,
                          expect_duration_millis(runtime, args[0].value, "SocketUDP.définir_délai", native_args.site),
                          "SocketUDP.définir_délai",
                          native_args.site);
            return Value::rien();
        }));

    object->fields["envoyer"] = Value::fonction(make_native_function(
        [state](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            stdlib_expect_positional(runtime, args, 3, "SocketUDP.envoyer", native_args.site);
            const std::string text = stdlib_expect_text(runtime, args[0].value, "SocketUDP.envoyer", native_args.site);
            const std::string host = stdlib_expect_text(runtime, args[1].value, "SocketUDP.envoyer", native_args.site);
            const int64_t port = stdlib_expect_integer(runtime, args[2].value, "SocketUDP.envoyer", native_args.site);
            if (port < 0 || port > 65535)
            {
                runtime.raise_runtime_error(native_args.site, "SocketUDP.envoyer requiert un port entre 0 et 65535");
            }

            addrinfo hints{};
            hints.ai_family = AF_UNSPEC;
            hints.ai_socktype = SOCK_DGRAM;
            addrinfo *result = nullptr;
            const std::string port_text = std::to_string(port);
            const int rc = ::getaddrinfo(host.c_str(), port_text.c_str(), &hints, &result);
            if (rc != 0)
            {
                raise_network_error(runtime, native_args.site, "SocketUDP.envoyer", gai_strerror(rc));
            }
            std::unique_ptr<addrinfo, decltype(&::freeaddrinfo)> guard(result, ::freeaddrinfo);
            const ssize_t sent = socket_sendto_bytes(state->fd, text.data(), text.size(), 0, result->ai_addr, result->ai_addrlen);
            if (sent < 0)
            {
                raise_network_error(runtime, native_args.site, "SocketUDP.envoyer", socket_error_text("envoi"));
            }
            return Value::rien();
        }));

    object->fields["envoyer_octets"] = Value::fonction(make_native_function(
        [state](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            stdlib_expect_positional(runtime, args, 3, "SocketUDP.envoyer_octets", native_args.site);
            const std::vector<unsigned char> bytes = expect_byte_vector(runtime, args[0].value, "SocketUDP.envoyer_octets", native_args.site);
            const std::string host = stdlib_expect_text(runtime, args[1].value, "SocketUDP.envoyer_octets", native_args.site);
            const int64_t port = stdlib_expect_integer(runtime, args[2].value, "SocketUDP.envoyer_octets", native_args.site);
            if (port < 0 || port > 65535)
            {
                runtime.raise_runtime_error(native_args.site, "SocketUDP.envoyer_octets requiert un port entre 0 et 65535");
            }

            addrinfo hints{};
            hints.ai_family = AF_UNSPEC;
            hints.ai_socktype = SOCK_DGRAM;
            addrinfo *result = nullptr;
            const std::string port_text = std::to_string(port);
            const int rc = ::getaddrinfo(host.c_str(), port_text.c_str(), &hints, &result);
            if (rc != 0)
            {
                raise_network_error(runtime, native_args.site, "SocketUDP.envoyer_octets", gai_strerror(rc));
            }
            std::unique_ptr<addrinfo, decltype(&::freeaddrinfo)> guard(result, ::freeaddrinfo);
            const ssize_t sent = socket_sendto_bytes(state->fd, bytes.data(), bytes.size(), 0, result->ai_addr, result->ai_addrlen);
            if (sent < 0)
            {
                raise_network_error(runtime, native_args.site, "SocketUDP.envoyer_octets", socket_error_text("envoi"));
            }
            return Value::rien();
        }));

    object->fields["diffuser"] = Value::fonction(make_native_function(
        [state](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            stdlib_expect_positional(runtime, args, 2, "SocketUDP.diffuser", native_args.site);
            const std::string text = stdlib_expect_text(runtime, args[0].value, "SocketUDP.diffuser", native_args.site);
            const int64_t port = stdlib_expect_integer(runtime, args[1].value, "SocketUDP.diffuser", native_args.site);
            if (port < 0 || port > 65535)
            {
                runtime.raise_runtime_error(native_args.site, "SocketUDP.diffuser requiert un port entre 0 et 65535");
            }
            int enabled = 1;
            if (::setsockopt(state->fd, SOL_SOCKET, SO_BROADCAST, &enabled, sizeof(enabled)) != 0)
            {
                raise_network_error(runtime, native_args.site, "SocketUDP.diffuser", socket_error_text("diffusion"));
            }
            sockaddr_in addr{};
            addr.sin_family = AF_INET;
            addr.sin_port = htons(static_cast<uint16_t>(port));
            addr.sin_addr.s_addr = INADDR_BROADCAST;
            const ssize_t sent = socket_sendto_bytes(state->fd, text.data(), text.size(), 0, reinterpret_cast<sockaddr *>(&addr), sizeof(addr));
            if (sent < 0)
            {
                raise_network_error(runtime, native_args.site, "SocketUDP.diffuser", socket_error_text("diffusion"));
            }
            return Value::rien();
        }));

    object->fields["recevoir"] = Value::fonction(make_native_function(
        [state](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            stdlib_expect_positional(runtime, *native_args.arguments, 0, "SocketUDP.recevoir", native_args.site);
            std::vector<char> buffer(65536);
            sockaddr_storage from{};
            socklen_t from_len = sizeof(from);
            const ssize_t received = socket_recvfrom_bytes(state->fd, buffer.data(), buffer.size(), 0, reinterpret_cast<sockaddr *>(&from), &from_len);
            if (received < 0)
            {
                raise_network_error(runtime, native_args.site, "SocketUDP.recevoir", socket_error_text("réception"));
            }
            return make_udp_packet_text_value(std::string(buffer.data(), buffer.data() + received),
                                              address_to_text(reinterpret_cast<sockaddr *>(&from)),
                                              port_from_sockaddr(reinterpret_cast<sockaddr *>(&from)));
        }));

    object->fields["recevoir_octets"] = Value::fonction(make_native_function(
        [state](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            stdlib_expect_positional(runtime, *native_args.arguments, 0, "SocketUDP.recevoir_octets", native_args.site);
            std::vector<unsigned char> buffer(65536);
            sockaddr_storage from{};
            socklen_t from_len = sizeof(from);
            const ssize_t received = socket_recvfrom_bytes(state->fd, buffer.data(), buffer.size(), 0, reinterpret_cast<sockaddr *>(&from), &from_len);
            if (received < 0)
            {
                raise_network_error(runtime, native_args.site, "SocketUDP.recevoir_octets", socket_error_text("réception"));
            }
            buffer.resize(static_cast<std::size_t>(received));
            return make_udp_packet_bytes_value(runtime,
                                               buffer,
                                               address_to_text(reinterpret_cast<sockaddr *>(&from)),
                                               port_from_sockaddr(reinterpret_cast<sockaddr *>(&from)),
                                               native_args.site);
        }));

    return Value::objet(std::move(object));
}

} // namespace lumiere
