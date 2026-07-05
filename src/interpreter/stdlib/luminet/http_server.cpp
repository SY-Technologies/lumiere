#include "../luminet_shared.hpp"

#include <cerrno>
#include <functional>
#include <memory>
#include <netdb.h>
#include <string>
#include <sys/socket.h>
#include <unistd.h>

namespace lumiere
{

Value make_http_server_value(const std::shared_ptr<HttpServerState> &state,
                             const NativeFunctionFactory &make_native_function)
{
    auto object = make_hidden_typed_object("ServeurHTTP");
    attach_native_state(object, state);

    const auto add_route = [state](IRuntime &runtime,
                                   const NativeArgs &native_args,
                                   const std::string &method,
                                   const std::string &signature) -> Value {
        const auto &args = *native_args.arguments;
        stdlib_expect_positional(runtime, args, 2, signature, native_args.site);
        const std::string pattern = stdlib_expect_text(runtime, args[0].value, signature, native_args.site);
        if (!args[1].value.is_fonction())
        {
            runtime.raise_runtime_error(native_args.site, signature + " attend une Fonction");
        }
        state->routes.push_back(HttpRoute{method, pattern, args[1].value});
        return Value::rien();
    };

    bind_object_method(object, make_native_function, "OBTENIR",
        [add_route](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            return add_route(runtime, native_args, "GET", "ServeurHTTP.OBTENIR");
        });
    bind_object_method(object, make_native_function, "CRÉER",
        [add_route](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            return add_route(runtime, native_args, "POST", "ServeurHTTP.CRÉER");
        });
    bind_object_method(object, make_native_function, "MODIFIER",
        [add_route](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            return add_route(runtime, native_args, "PUT", "ServeurHTTP.MODIFIER");
        });
    bind_object_method(object, make_native_function, "SUPPRIMER",
        [add_route](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            return add_route(runtime, native_args, "DELETE", "ServeurHTTP.SUPPRIMER");
        });

    bind_object_method(object, make_native_function, "avant",
        [state](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            stdlib_expect_positional(runtime, args, 1, "ServeurHTTP.avant", native_args.site);
            if (!args[0].value.is_fonction())
            {
                runtime.raise_runtime_error(native_args.site, "ServeurHTTP.avant attend une Fonction");
            }
            state->middleware.push_back(args[0].value);
            return Value::rien();
        });

    bind_object_method(object, make_native_function, "canal",
        [state](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            stdlib_expect_positional(runtime, args, 2, "ServeurHTTP.canal", native_args.site);
            const std::string pattern = stdlib_expect_text(runtime, args[0].value, "ServeurHTTP.canal", native_args.site);
            if (!args[1].value.is_fonction())
            {
                runtime.raise_runtime_error(native_args.site, "ServeurHTTP.canal attend une Fonction");
            }
            state->canal_routes.push_back({pattern, args[1].value});
            return Value::rien();
        });

    const auto stop_handler = [state](IRuntime &runtime, const NativeArgs &native_args) -> Value {
        stdlib_expect_positional(runtime, *native_args.arguments, 0, "ServeurHTTP.arrêter", native_args.site);
        state->stopped = true;
        if (state->fd >= 0)
        {
            ::shutdown(state->fd, SHUT_RDWR);
            close_socket_fd(state->fd);
        }
        return Value::rien();
    };
    object->fields["arrêter"] = Value::fonction(make_native_function(stop_handler));
    object->fields["arreter"] = Value::fonction(make_native_function(stop_handler));

    bind_object_method(object, make_native_function, "écouter",
        [state, make_native_function](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            stdlib_expect_positional(runtime, args, 2, "ServeurHTTP.écouter", native_args.site);
            const std::string host = stdlib_expect_text(runtime, args[0].value, "ServeurHTTP.écouter", native_args.site);
            const int64_t port = stdlib_expect_integer(runtime, args[1].value, "ServeurHTTP.écouter", native_args.site);
            if (port < 0 || port > 65535)
            {
                runtime.raise_runtime_error(native_args.site, "ServeurHTTP.écouter requiert un port entre 0 et 65535");
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
                raise_network_error(runtime, native_args.site, "ServeurHTTP.écouter", gai_strerror(rc));
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
                close_socket_fd(listen_fd);
            }
            if (listen_fd < 0)
            {
                raise_network_error(runtime, native_args.site, "ServeurHTTP.écouter", socket_error_text("écoute"));
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
                    if (errno == EINTR)
                    {
                        continue;
                    }
                    if (state->stopped)
                    {
                        break;
                    }
                    raise_network_error(runtime, native_args.site, "ServeurHTTP.écouter", socket_error_text("acceptation"));
                }

                int active_client_fd = client_fd;
                try
                {
                    const std::string raw = recv_http_message(runtime, active_client_fd, "ServeurHTTP.écouter", native_args.site);
                    HttpRequestData request = parse_http_request(runtime, raw, "ServeurHTTP.écouter", native_args.site);
                    auto writer_state = std::make_shared<HttpResponseWriterState>();
                    writer_state->fd = active_client_fd;
                    Value request_value;
                    Value response_value;

                    HttpRoute *matched_route = nullptr;
                    std::vector<std::pair<std::string, std::string>> route_params;
                    for (auto &route : state->routes)
                    {
                        if (route.method == request.method &&
                            match_route_pattern(route.pattern, request.path, route_params))
                        {
                            matched_route = &route;
                            request.route_params = route_params;
                            break;
                        }
                    }

                    request_value = make_http_request_value(runtime, request, make_native_function, native_args.site);
                    response_value = make_http_response_writer_value(runtime, writer_state, make_native_function, native_args.site);

                    Value canal_handler = Value::rien();
                    for (const auto &route : state->canal_routes)
                    {
                        std::vector<std::pair<std::string, std::string>> ws_params;
                        if (match_route_pattern(route.first, request.path, ws_params))
                        {
                            request.route_params = ws_params;
                            canal_handler = route.second;
                            request_value = make_http_request_value(runtime, request, make_native_function, native_args.site);
                            break;
                        }
                    }

                    if (canal_handler.is_fonction())
                    {
                        const std::string upgrade = to_lower_ascii(header_value_or_empty(request.headers, "Upgrade"));
                        const bool has_upgrade = header_contains_token(request.headers, "Connection", "Upgrade");
                        const std::string key = header_value_or_empty(request.headers, "Sec-WebSocket-Key");
                        if (request.method != "GET" || upgrade != "websocket" || !has_upgrade || key.empty())
                        {
                            send_http_response(runtime, writer_state, 400, "Requête Canal invalide", {}, "ServeurHTTP.canal", native_args.site);
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
                                 "ServeurHTTP.canal",
                                 native_args.site);
                        writer_state->sent = true;

                        auto canal_state = std::make_shared<CanalClientState>();
                        canal_state->fd = active_client_fd;
                        canal_state->client_side = false;
                        canal_state->address = address_to_text(reinterpret_cast<sockaddr *>(&client_addr));
                        Value canal_client = make_canal_client_value(runtime, canal_state, make_native_function, native_args.site);
                        std::vector<RuntimeArgument> cb_args = {RuntimeArgument{"", canal_client}};
                        runtime.call(canal_handler, NativeArgs{nullptr, &cb_args, native_args.site});
                        run_canal_loop(runtime, canal_state, false, Value::rien(), Value::rien(), Value::rien(), canal_client, native_args.site);
                        if (canal_state->fd >= 0)
                        {
                            close_socket_fd(canal_state->fd);
                        }
                        active_client_fd = -1;
                        continue;
                    }

                    std::function<void(std::size_t)> run_chain;
                    run_chain = [&](std::size_t index) {
                        if (writer_state->sent)
                        {
                            return;
                        }
                        if (index < state->middleware.size())
                        {
                            auto proceeded = std::make_shared<bool>(false);
                            Value suivant = Value::fonction(make_native_function(
                                [proceeded](IRuntime &inner_runtime, const NativeArgs &inner_args) -> Value {
                                    stdlib_expect_positional(inner_runtime, *inner_args.arguments, 0, "Middleware.suivant", inner_args.site);
                                    *proceeded = true;
                                    return Value::rien();
                                }));
                            std::vector<RuntimeArgument> cb_args = {
                                RuntimeArgument{"", request_value},
                                RuntimeArgument{"", response_value},
                                RuntimeArgument{"", suivant},
                            };
                            runtime.call(state->middleware[index], NativeArgs{nullptr, &cb_args, native_args.site});
                            if (*proceeded)
                            {
                                run_chain(index + 1);
                            }
                            return;
                        }

                        if (matched_route != nullptr)
                        {
                            std::vector<RuntimeArgument> cb_args = {
                                RuntimeArgument{"", request_value},
                                RuntimeArgument{"", response_value},
                            };
                            runtime.call(matched_route->handler, NativeArgs{nullptr, &cb_args, native_args.site});
                            return;
                        }

                        send_http_response(runtime,
                                           writer_state,
                                           404,
                                           "Introuvable",
                                           {},
                                           "ServeurHTTP.écouter",
                                           native_args.site);
                    };

                    run_chain(0);
                    if (!writer_state->sent)
                    {
                        send_http_response(runtime,
                                           writer_state,
                                           204,
                                           "",
                                           {},
                                           "ServeurHTTP.écouter",
                                           native_args.site);
                    }
                    close_socket_fd(active_client_fd);
                }
                catch (...)
                {
                    close_socket_fd(active_client_fd);
                    throw;
                }
            }

            if (state->fd >= 0)
            {
                close_socket_fd(state->fd);
            }

            return Value::rien();
        });

    return Value::objet(std::move(object));
}

} // namespace lumiere
