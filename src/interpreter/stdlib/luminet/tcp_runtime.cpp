#include "../luminet_shared.hpp"

#include <memory>
#include <netdb.h>
#include <string>
#include <sys/socket.h>
#include <unistd.h>

namespace lumiere
{

Value make_tcp_connection_value(const std::shared_ptr<TcpConnectionState> &state,
                                const std::string &address,
                                int64_t port,
                                const NativeFunctionFactory &make_native_function)
{
    auto object = make_hidden_typed_object("ConnexionTCP");
    attach_native_state(object, state);
    object->fields["adresse"] = Value::texte(address);
    object->fields["port"] = Value::entier(port);

    object->fields["est_connecté"] = Value::fonction(make_native_function(
        [state](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            stdlib_expect_positional(runtime, *native_args.arguments, 0, "ConnexionTCP.est_connecté", native_args.site);
            return Value::logique(!state->closed && state->fd >= 0);
        }));

    object->fields["fermer"] = Value::fonction(make_native_function(
        [state](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            stdlib_expect_positional(runtime, *native_args.arguments, 0, "ConnexionTCP.fermer", native_args.site);
            if (!state->closed && state->fd >= 0)
            {
                ::close(state->fd);
                state->fd = -1;
                state->closed = true;
            }
            return Value::rien();
        }));

    object->fields["définir_délai"] = Value::fonction(make_native_function(
        [state](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            stdlib_expect_positional(runtime, args, 1, "ConnexionTCP.définir_délai", native_args.site);
            apply_timeout(runtime,
                          state->fd,
                          expect_duration_millis(runtime, args[0].value, "ConnexionTCP.définir_délai", native_args.site),
                          "ConnexionTCP.définir_délai",
                          native_args.site);
            return Value::rien();
        }));

    object->fields["écrire"] = Value::fonction(make_native_function(
        [state](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            stdlib_expect_positional(runtime, args, 1, "ConnexionTCP.écrire", native_args.site);
            const std::string text = stdlib_expect_text(runtime, args[0].value, "ConnexionTCP.écrire", native_args.site);
            send_all(runtime,
                     state->fd,
                     reinterpret_cast<const unsigned char *>(text.data()),
                     text.size(),
                     "ConnexionTCP.écrire",
                     native_args.site);
            return Value::rien();
        }));

    object->fields["écrire_octets"] = Value::fonction(make_native_function(
        [state](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            stdlib_expect_positional(runtime, args, 1, "ConnexionTCP.écrire_octets", native_args.site);
            const std::vector<unsigned char> bytes = expect_byte_vector(runtime, args[0].value, "ConnexionTCP.écrire_octets", native_args.site);
            send_all(runtime, state->fd, bytes.data(), bytes.size(), "ConnexionTCP.écrire_octets", native_args.site);
            return Value::rien();
        }));

    object->fields["lire"] = Value::fonction(make_native_function(
        [state](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            stdlib_expect_positional(runtime, *native_args.arguments, 0, "ConnexionTCP.lire", native_args.site);
            std::string result;
            char buffer[4096];
            const ssize_t received = ::recv(state->fd, buffer, sizeof(buffer), 0);
            if (received < 0)
            {
                raise_network_error(runtime, native_args.site, "ConnexionTCP.lire", socket_error_text("lecture"));
            }
            if (received == 0)
            {
                state->closed = true;
                return Value::texte("");
            }
            result.assign(buffer, buffer + received);
            return Value::texte(std::move(result));
        }));

    object->fields["lire_ligne"] = Value::fonction(make_native_function(
        [state](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            stdlib_expect_positional(runtime, *native_args.arguments, 0, "ConnexionTCP.lire_ligne", native_args.site);
            std::string result;
            char ch = '\0';
            while (true)
            {
                const ssize_t received = ::recv(state->fd, &ch, 1, 0);
                if (received < 0)
                {
                    raise_network_error(runtime, native_args.site, "ConnexionTCP.lire_ligne", socket_error_text("lecture"));
                }
                if (received == 0)
                {
                    state->closed = true;
                    break;
                }
                if (ch == '\n')
                {
                    break;
                }
                if (ch != '\r')
                {
                    result.push_back(ch);
                }
            }
            return Value::texte(std::move(result));
        }));

    object->fields["lire_octets"] = Value::fonction(make_native_function(
        [state](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            stdlib_expect_positional(runtime, args, 1, "ConnexionTCP.lire_octets", native_args.site);
            const int64_t count = stdlib_expect_integer(runtime, args[0].value, "ConnexionTCP.lire_octets", native_args.site);
            if (count < 0)
            {
                runtime.raise_runtime_error(native_args.site, "ConnexionTCP.lire_octets requiert un nombre non négatif");
            }
            std::vector<unsigned char> buffer(static_cast<std::size_t>(count));
            const ssize_t received = ::recv(state->fd, buffer.data(), buffer.size(), 0);
            if (received < 0)
            {
                raise_network_error(runtime, native_args.site, "ConnexionTCP.lire_octets", socket_error_text("lecture"));
            }
            if (received == 0)
            {
                state->closed = true;
            }
            buffer.resize(received > 0 ? static_cast<std::size_t>(received) : 0);
            Value result = Value::liste(bytes_to_list(buffer));
            runtime.annotate_value(result, "Liste[Entier]", native_args.site);
            return result;
        }));

    return Value::objet(std::move(object));
}

Value make_tcp_server_value(const std::shared_ptr<TcpServerState> &state,
                            const NativeFunctionFactory &make_native_function)
{
    auto object = make_hidden_typed_object("ServeurTCP");
    attach_native_state(object, state);

    object->fields["quand_connexion"] = Value::fonction(make_native_function(
        [state](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            stdlib_expect_positional(runtime, args, 1, "ServeurTCP.quand_connexion", native_args.site);
            if (!args[0].value.is_fonction())
            {
                runtime.raise_runtime_error(native_args.site, "ServeurTCP.quand_connexion attend une Fonction");
            }
            state->on_connection = args[0].value;
            return Value::rien();
        }));

    const auto stop_handler = [state](IRuntime &runtime, const NativeArgs &native_args) -> Value {
        stdlib_expect_positional(runtime, *native_args.arguments, 0, "ServeurTCP.arrêter", native_args.site);
        state->stopped = true;
        if (state->fd >= 0)
        {
            ::shutdown(state->fd, SHUT_RDWR);
            ::close(state->fd);
            state->fd = -1;
        }
        return Value::rien();
    };
    object->fields["arrêter"] = Value::fonction(make_native_function(stop_handler));
    object->fields["arreter"] = Value::fonction(make_native_function(stop_handler));

    object->fields["écouter"] = Value::fonction(make_native_function(
        [state, make_native_function](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            stdlib_expect_positional(runtime, args, 2, "ServeurTCP.écouter", native_args.site);
            const std::string host = stdlib_expect_text(runtime, args[0].value, "ServeurTCP.écouter", native_args.site);
            const int64_t port = stdlib_expect_integer(runtime, args[1].value, "ServeurTCP.écouter", native_args.site);
            if (port < 0 || port > 65535)
            {
                runtime.raise_runtime_error(native_args.site, "ServeurTCP.écouter requiert un port entre 0 et 65535");
            }
            if (!state->on_connection.is_fonction())
            {
                runtime.raise_runtime_error(native_args.site, "ServeurTCP.écouter requiert un callback quand_connexion");
            }

            addrinfo hints{};
            hints.ai_family = AF_UNSPEC;
            hints.ai_socktype = SOCK_STREAM;
            hints.ai_flags = AI_PASSIVE;

            addrinfo *result = nullptr;
            const std::string port_text = std::to_string(port);
            const int rc = ::getaddrinfo(host.c_str(), port_text.c_str(), &hints, &result);
            if (rc != 0)
            {
                raise_network_error(runtime, native_args.site, "ServeurTCP.écouter", gai_strerror(rc));
            }

            std::unique_ptr<addrinfo, decltype(&::freeaddrinfo)> guard(result, ::freeaddrinfo);
            int listen_fd = -1;
            for (addrinfo *entry = result; entry != nullptr; entry = entry->ai_next)
            {
                listen_fd = ::socket(entry->ai_family, entry->ai_socktype, entry->ai_protocol);
                if (listen_fd < 0)
                {
                    continue;
                }
                int reuse = 1;
                ::setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
                if (::bind(listen_fd, entry->ai_addr, entry->ai_addrlen) == 0 &&
                    ::listen(listen_fd, 16) == 0)
                {
                    break;
                }
                ::close(listen_fd);
                listen_fd = -1;
            }

            if (listen_fd < 0)
            {
                raise_network_error(runtime, native_args.site, "ServeurTCP.écouter", socket_error_text("écoute"));
            }

            state->fd = listen_fd;
            state->stopped = false;

            while (!state->stopped)
            {
                sockaddr_storage client_addr{};
                socklen_t client_len = sizeof(client_addr);
                const int client_fd = ::accept(listen_fd, reinterpret_cast<sockaddr *>(&client_addr), &client_len);
                if (client_fd < 0)
                {
                    if (state->stopped)
                    {
                        break;
                    }
                    raise_network_error(runtime, native_args.site, "ServeurTCP.écouter", socket_error_text("acceptation"));
                }

                auto client_state = std::make_shared<TcpConnectionState>();
                client_state->fd = client_fd;
                const std::string address = address_to_text(reinterpret_cast<sockaddr *>(&client_addr));
                const int64_t client_port = port_from_sockaddr(reinterpret_cast<sockaddr *>(&client_addr));
                Value client = make_tcp_connection_value(client_state, address, client_port, make_native_function);
                std::vector<RuntimeArgument> callback_args = {RuntimeArgument{"", client}};
                runtime.call(state->on_connection, NativeArgs{nullptr, &callback_args, native_args.site});
            }

            if (state->fd >= 0)
            {
                ::close(state->fd);
                state->fd = -1;
            }
            return Value::rien();
        }));

    return Value::objet(std::move(object));
}

} // namespace lumiere
