#include "../luminet_shared.hpp"

#include <memory>
#include <string>

namespace lumiere
{

void run_canal_loop(IRuntime &runtime,
                    const std::shared_ptr<CanalClientState> &state,
                    bool server_dispatch_mode,
                    const Value &server_message_callback,
                    const Value &server_disconnect_callback,
                    const Value &server_error_callback,
                    Value client_value,
                    const RuntimeSite &site)
{
    if (!state->opened_notified)
    {
        state->opened_notified = true;
        if (state->on_open.is_fonction())
        {
            const std::vector<RuntimeArgument> args;
            runtime.call(state->on_open, NativeArgs{nullptr, &args, site});
        }
    }

    while (!state->closed)
    {
        std::optional<WebSocketFrame> frame;
        try
        {
            frame = recv_websocket_frame(runtime, state->fd, state->pending_bytes, "Canal.attendre", site);
        }
        catch (...)
        {
            if (state->on_error.is_fonction())
            {
                std::vector<RuntimeArgument> args = {RuntimeArgument{"", Value::texte("erreur websocket")}};
                runtime.call(state->on_error, NativeArgs{nullptr, &args, site});
            }
            if (server_error_callback.is_fonction())
            {
                std::vector<RuntimeArgument> args = {RuntimeArgument{"", client_value}, RuntimeArgument{"", Value::texte("erreur websocket")}};
                runtime.call(server_error_callback, NativeArgs{nullptr, &args, site});
            }
            throw;
        }

        if (!frame.has_value())
        {
            state->closed = true;
            break;
        }

        if (frame->opcode == 0x8)
        {
            state->closed = true;
            break;
        }
        if (frame->opcode == 0x9)
        {
            send_websocket_frame(runtime, state->fd, 0xA, frame->payload, state->client_side, "Canal.attendre", site);
            continue;
        }
        if (frame->opcode == 0xA)
        {
            continue;
        }

        if (frame->opcode == 0x1)
        {
            const std::string message(frame->payload.begin(), frame->payload.end());
            if (state->on_message.is_fonction())
            {
                std::vector<RuntimeArgument> args = {RuntimeArgument{"", Value::texte(message)}};
                runtime.call(state->on_message, NativeArgs{nullptr, &args, site});
            }
            if (server_dispatch_mode && server_message_callback.is_fonction())
            {
                std::vector<RuntimeArgument> args = {RuntimeArgument{"", client_value}, RuntimeArgument{"", Value::texte(message)}};
                runtime.call(server_message_callback, NativeArgs{nullptr, &args, site});
            }
        }
    }

    if (state->on_close.is_fonction())
    {
        std::vector<RuntimeArgument> args = {
            RuntimeArgument{"", Value::entier(1000)},
            RuntimeArgument{"", Value::texte("")},
        };
        runtime.call(state->on_close, NativeArgs{nullptr, &args, site});
    }
    if (server_dispatch_mode && server_disconnect_callback.is_fonction())
    {
        std::vector<RuntimeArgument> args = {
            RuntimeArgument{"", client_value},
            RuntimeArgument{"", Value::entier(1000)},
            RuntimeArgument{"", Value::texte("")},
        };
        runtime.call(server_disconnect_callback, NativeArgs{nullptr, &args, site});
    }
}

Value make_canal_client_value(IRuntime &runtime,
                              const std::shared_ptr<CanalClientState> &state,
                              const NativeFunctionFactory &make_native_function,
                              const RuntimeSite &site)
{
    auto object = make_hidden_typed_object("CanalClient");
    attach_native_state(object, state);
    object->fields["adresse"] = Value::texte(state->address);

    bind_object_method(object, make_native_function, "quand_ouvert",
        [state](IRuntime &inner_runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            stdlib_expect_positional(inner_runtime, args, 1, "CanalClient.quand_ouvert", native_args.site);
            state->on_open = args[0].value;
            return Value::rien();
        });
    bind_object_method(object, make_native_function, "quand_message",
        [state](IRuntime &inner_runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            stdlib_expect_positional(inner_runtime, args, 1, "CanalClient.quand_message", native_args.site);
            state->on_message = args[0].value;
            return Value::rien();
        });
    bind_object_method(object, make_native_function, "quand_fermé",
        [state](IRuntime &inner_runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            stdlib_expect_positional(inner_runtime, args, 1, "CanalClient.quand_fermé", native_args.site);
            state->on_close = args[0].value;
            return Value::rien();
        });
    bind_object_method(object, make_native_function, "quand_erreur",
        [state](IRuntime &inner_runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            stdlib_expect_positional(inner_runtime, args, 1, "CanalClient.quand_erreur", native_args.site);
            state->on_error = args[0].value;
            return Value::rien();
        });
    bind_object_method(object, make_native_function, "envoyer",
        [state](IRuntime &inner_runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            stdlib_expect_positional(inner_runtime, args, 1, "CanalClient.envoyer", native_args.site);
            const std::string message = stdlib_expect_text(inner_runtime, args[0].value, "CanalClient.envoyer", native_args.site);
            std::vector<unsigned char> payload(message.begin(), message.end());
            send_websocket_frame(inner_runtime, state->fd, 0x1, payload, state->client_side, "CanalClient.envoyer", native_args.site);
            return Value::rien();
        });
    bind_object_method(object, make_native_function, "envoyer_octets",
        [state](IRuntime &inner_runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            stdlib_expect_positional(inner_runtime, args, 1, "CanalClient.envoyer_octets", native_args.site);
            std::vector<unsigned char> payload = expect_byte_vector(inner_runtime, args[0].value, "CanalClient.envoyer_octets", native_args.site);
            send_websocket_frame(inner_runtime, state->fd, 0x2, payload, state->client_side, "CanalClient.envoyer_octets", native_args.site);
            return Value::rien();
        });
    bind_object_method(object, make_native_function, "fermer",
        [state](IRuntime &inner_runtime, const NativeArgs &native_args) -> Value {
            stdlib_expect_positional_range(inner_runtime, *native_args.arguments, 0, 2, "CanalClient.fermer", native_args.site);
            send_websocket_frame(inner_runtime, state->fd, 0x8, {}, state->client_side, "CanalClient.fermer", native_args.site);
            state->closed = true;
            if (socket_handle_valid(state->fd))
            {
                close_socket_fd(state->fd);
            }
            return Value::rien();
        });
    bind_object_method(object, make_native_function, "est_connecté",
        [state](IRuntime &inner_runtime, const NativeArgs &native_args) -> Value {
            stdlib_expect_positional(inner_runtime, *native_args.arguments, 0, "CanalClient.est_connecté", native_args.site);
            return Value::logique(!state->closed && socket_handle_valid(state->fd));
        });
    bind_object_method(object, make_native_function, "attendre",
        [state, object_value = Value::objet(object)](IRuntime &inner_runtime, const NativeArgs &native_args) -> Value {
            stdlib_expect_positional(inner_runtime, *native_args.arguments, 0, "CanalClient.attendre", native_args.site);
            run_canal_loop(inner_runtime, state, false, Value::rien(), Value::rien(), Value::rien(), object_value, native_args.site);
            return Value::rien();
        });

    Value result = Value::objet(std::move(object));
    runtime.annotate_value(result, "CanalClient", site);
    return result;
}

Value make_canal_server_value(const std::shared_ptr<CanalServerState> &state,
                              const NativeFunctionFactory &make_native_function)
{
    auto object = make_hidden_typed_object("ServeurCanal");
    attach_native_state(object, state);

    bind_object_method(object, make_native_function, "quand_connexion",
        [state](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            stdlib_expect_positional(runtime, args, 1, "ServeurCanal.quand_connexion", native_args.site);
            state->on_connection = args[0].value;
            return Value::rien();
        });
    bind_object_method(object, make_native_function, "quand_message",
        [state](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            stdlib_expect_positional(runtime, args, 1, "ServeurCanal.quand_message", native_args.site);
            state->on_message = args[0].value;
            return Value::rien();
        });
    bind_object_method(object, make_native_function, "quand_déconnexion",
        [state](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            stdlib_expect_positional(runtime, args, 1, "ServeurCanal.quand_déconnexion", native_args.site);
            state->on_disconnect = args[0].value;
            return Value::rien();
        });
    bind_object_method(object, make_native_function, "quand_erreur",
        [state](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            stdlib_expect_positional(runtime, args, 1, "ServeurCanal.quand_erreur", native_args.site);
            state->on_error = args[0].value;
            return Value::rien();
        });
    const auto stop_handler = [state](IRuntime &runtime, const NativeArgs &native_args) -> Value {
        stdlib_expect_positional(runtime, *native_args.arguments, 0, "ServeurCanal.arrêter", native_args.site);
        state->stopped = true;
        if (socket_handle_valid(state->fd))
        {
            platform_socket_shutdown(state->fd);
            close_socket_fd(state->fd);
        }
        return Value::rien();
    };
    object->fields["arrêter"] = Value::fonction(make_native_function(stop_handler));
    object->fields["arreter"] = Value::fonction(make_native_function(stop_handler));
    bind_object_method(object, make_native_function, "écouter",
        [state, make_native_function](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            stdlib_expect_positional(runtime, args, 2, "ServeurCanal.écouter", native_args.site);
            const std::string host = stdlib_expect_text(runtime, args[0].value, "ServeurCanal.écouter", native_args.site);
            const int64_t port = stdlib_expect_integer(runtime, args[1].value, "ServeurCanal.écouter", native_args.site);
            if (port < 0 || port > 65535)
            {
                runtime.raise_runtime_error(native_args.site, "ServeurCanal.écouter requiert un port entre 0 et 65535");
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
                raise_network_error(runtime, native_args.site, "ServeurCanal.écouter", gai_strerror(rc));
            }
            std::unique_ptr<addrinfo, decltype(&::freeaddrinfo)> guard(result, ::freeaddrinfo);
            SocketHandle listen_fd = kInvalidSocketHandle;
            for (addrinfo *entry = result; entry != nullptr; entry = entry->ai_next)
            {
                initialize_socket_platform();
                listen_fd = ::socket(entry->ai_family, entry->ai_socktype, entry->ai_protocol);
                if (!socket_handle_valid(listen_fd))
                {
                    continue;
                }
                platform_socket_enable_reuse_address(listen_fd);
                if (::bind(listen_fd, entry->ai_addr, entry->ai_addrlen) == 0 && ::listen(listen_fd, 16) == 0)
                {
                    break;
                }
                close_socket_fd(listen_fd);
            }
            if (!socket_handle_valid(listen_fd))
            {
                raise_network_error(runtime, native_args.site, "ServeurCanal.écouter", socket_error_text("écoute"));
            }
            state->fd = listen_fd;
            state->stopped = false;

            while (!state->stopped)
            {
                sockaddr_storage client_addr{};
                socklen_t client_len = sizeof(client_addr);
                const SocketHandle client_fd = ::accept(listen_fd, reinterpret_cast<sockaddr *>(&client_addr), &client_len);
                if (!socket_handle_valid(client_fd))
                {
                    if (socket_error_is_interrupted(socket_last_error_code()))
                    {
                        continue;
                    }
                    if (state->stopped)
                    {
                        break;
                    }
                    raise_network_error(runtime, native_args.site, "ServeurCanal.écouter", socket_error_text("acceptation"));
                }

                SocketHandle active_client_fd = client_fd;
                try
                {
                    const std::string raw = recv_http_message(runtime, active_client_fd, "ServeurCanal.écouter", native_args.site);
                    const HttpRequestData request = parse_http_request(runtime, raw, "ServeurCanal.écouter", native_args.site);
                    const std::string upgrade = to_lower_ascii(header_value_or_empty(request.headers, "Upgrade"));
                    const bool has_upgrade = header_contains_token(request.headers, "Connection", "Upgrade");
                    const std::string key = header_value_or_empty(request.headers, "Sec-WebSocket-Key");
                    if (request.method != "GET" || upgrade != "websocket" || !has_upgrade || key.empty())
                    {
                        const std::string response =
                            "HTTP/1.1 400 Bad Request\r\n"
                            "Content-Length: 0\r\n"
                            "Connection: close\r\n\r\n";
                        send_all(runtime,
                                 active_client_fd,
                                 reinterpret_cast<const unsigned char *>(response.data()),
                                 response.size(),
                                 "ServeurCanal.écouter",
                                 native_args.site);
                        close_socket_fd(active_client_fd);
                        continue;
                    }
                    const std::string handshake =
                        "HTTP/1.1 101 Switching Protocols\r\n"
                        "Upgrade: websocket\r\n"
                        "Connection: Upgrade\r\n"
                        "Sec-WebSocket-Accept: " + websocket_accept_key(key) + "\r\n\r\n";
                    send_all(runtime,
                             active_client_fd,
                             reinterpret_cast<const unsigned char *>(handshake.data()),
                             handshake.size(),
                             "ServeurCanal.écouter",
                             native_args.site);

                    auto client_state = std::make_shared<CanalClientState>();
                    client_state->fd = active_client_fd;
                    client_state->client_side = false;
                    client_state->address = address_to_text(reinterpret_cast<sockaddr *>(&client_addr));
                    Value client = make_canal_client_value(runtime, client_state, make_native_function, native_args.site);
                    if (state->on_connection.is_fonction())
                    {
                        std::vector<RuntimeArgument> cb_args = {RuntimeArgument{"", client}};
                        runtime.call(state->on_connection, NativeArgs{nullptr, &cb_args, native_args.site});
                    }
                    run_canal_loop(runtime, client_state, true, state->on_message, state->on_disconnect, state->on_error, client, native_args.site);
                    if (socket_handle_valid(client_state->fd))
                    {
                        close_socket_fd(client_state->fd);
                    }
                    active_client_fd = kInvalidSocketHandle;
                }
                catch (...)
                {
                    close_socket_fd(active_client_fd);
                    throw;
                }
            }

            if (socket_handle_valid(state->fd))
            {
                close_socket_fd(state->fd);
            }
            return Value::rien();
        });

    return Value::objet(std::move(object));
}

} // namespace lumiere
