#include "../luminet_shared.hpp"

#include <memory>
#include <optional>
#include <sstream>
#include <string>

namespace lumiere
{

Value make_luminet_http_module(const NativeFunctionFactory &make_native_function)
{
    auto http = make_hidden_typed_object("LumiNet.HTTP");
    const auto make_http_request = [make_native_function](IRuntime &runtime,
                                                          const NativeArgs &native_args,
                                                          const std::string &method,
                                                          const std::string &signature) -> Value {
        const auto &args = *native_args.arguments;
        if (args.empty() || args.size() > 4)
        {
            runtime.raise_runtime_error(native_args.site, signature + " requiert entre 1 et 4 argument(s)");
        }

        const std::string url = stdlib_expect_text(runtime, args[0].value, signature, native_args.site);
        std::vector<std::pair<std::string, std::string>> headers;
        std::string body;
        std::optional<int64_t> timeout_ms;

        for (std::size_t i = 1; i < args.size(); ++i)
        {
            const auto &arg = args[i];
            if (arg.name == "entêtes" || arg.name == "entetes")
            {
                headers = expect_header_entries(runtime, arg.value, signature, native_args.site);
            }
            else if (arg.name == "corps")
            {
                body = stdlib_expect_text(runtime, arg.value, signature, native_args.site);
            }
            else if (arg.name == "type")
            {
                headers.push_back({"Content-Type", stdlib_expect_text(runtime, arg.value, signature, native_args.site)});
            }
            else if (arg.name == "délai" || arg.name == "delai")
            {
                timeout_ms = expect_duration_millis(runtime, arg.value, signature, native_args.site);
            }
            else
            {
                runtime.raise_runtime_error(native_args.site, signature + " ne reconnaît pas l'argument nommé '" + arg.name + "'");
            }
        }

        const ParsedHttpUrl parsed = parse_http_url(runtime, url, signature, native_args.site);
        if (parsed.scheme != "http")
        {
            runtime.raise_runtime_error(native_args.site, signature + " ne prend actuellement en charge que http");
        }
        addrinfo hints{};
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        addrinfo *result = nullptr;
        const std::string port_text = std::to_string(parsed.port);
        const int rc = ::getaddrinfo(parsed.host.c_str(), port_text.c_str(), &hints, &result);
        if (rc != 0)
        {
            raise_network_error(runtime, native_args.site, signature, gai_strerror(rc));
        }
        std::unique_ptr<addrinfo, decltype(&::freeaddrinfo)> guard(result, ::freeaddrinfo);

        SocketHandle fd = kInvalidSocketHandle;
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
                apply_timeout(runtime, fd, *timeout_ms, signature, native_args.site);
            }
            if (::connect(fd, entry->ai_addr, entry->ai_addrlen) == 0)
            {
                break;
            }
            close_socket_fd(fd);
        }
        if (!socket_handle_valid(fd))
        {
            raise_network_error(runtime, native_args.site, signature, socket_error_text("connexion"));
        }
        try
        {
            std::ostringstream request;
            request << method << " " << parsed.target << " HTTP/1.1\r\n";
            request << "Host: " << parsed.host << "\r\n";
            request << "Connection: close\r\n";
            bool has_length = false;
            for (const auto &header : headers)
            {
                if (to_lower_ascii(header.first) == "content-length")
                {
                    has_length = true;
                }
                request << header.first << ": " << header.second << "\r\n";
            }
            if (!body.empty() && !has_length)
            {
                request << "Content-Length: " << body.size() << "\r\n";
            }
            request << "\r\n";
            request << body;
            const std::string payload = request.str();
            send_all(runtime,
                     fd,
                     reinterpret_cast<const unsigned char *>(payload.data()),
                     payload.size(),
                     signature,
                     native_args.site);
            const std::string raw_response = recv_http_message(runtime, fd, signature, native_args.site);
            close_socket_fd(fd);

            const std::size_t header_end = raw_response.find("\r\n\r\n");
            if (header_end == std::string::npos)
            {
                runtime.raise_runtime_error(native_args.site, signature + " a reçu une réponse HTTP invalide");
            }
            const std::string response_head = raw_response.substr(0, header_end);
            const int64_t status = parse_http_status_line(runtime, response_head, signature, native_args.site);
            const std::vector<std::pair<std::string, std::string>> response_headers = parse_http_headers_block(response_head);
            const std::string response_body = raw_response.substr(header_end + 4);
            return make_http_response_value(runtime, status, response_body, response_headers, make_native_function, native_args.site);
        }
        catch (...)
        {
            close_socket_fd(fd);
            throw;
        }
    };

    bind_object_method(http, make_native_function, "requête",
        [make_http_request](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            if (args.size() < 2)
            {
                runtime.raise_runtime_error(native_args.site, "LumiNet.HTTP.requête requiert au moins 2 argument(s)");
            }
            const std::string method = stdlib_expect_text(runtime, args[0].value, "LumiNet.HTTP.requête", native_args.site);
            std::vector<RuntimeArgument> shifted(args.begin() + 1, args.end());
            NativeArgs shifted_args{native_args.receiver, &shifted, native_args.site};
            return make_http_request(runtime, shifted_args, method, "LumiNet.HTTP.requête");
        });
    bind_object_method(http, make_native_function, "obtenir",
        [make_http_request](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            return make_http_request(runtime, native_args, "GET", "LumiNet.HTTP.obtenir");
        });
    bind_object_method(http, make_native_function, "créer",
        [make_http_request](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            return make_http_request(runtime, native_args, "POST", "LumiNet.HTTP.créer");
        });
    bind_object_method(http, make_native_function, "modifier",
        [make_http_request](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            return make_http_request(runtime, native_args, "PUT", "LumiNet.HTTP.modifier");
        });
    bind_object_method(http, make_native_function, "supprimer",
        [make_http_request](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            return make_http_request(runtime, native_args, "DELETE", "LumiNet.HTTP.supprimer");
        });
    bind_object_method(http, make_native_function, "Serveur",
        [make_native_function](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            stdlib_expect_positional(runtime, *native_args.arguments, 0, "LumiNet.HTTP.Serveur", native_args.site);
            return make_http_server_value(std::make_shared<HttpServerState>(), make_native_function);
        });
    return Value::objet(std::move(http));
}

Value make_luminet_canal_module(const NativeFunctionFactory &make_native_function)
{
    auto canal = make_hidden_typed_object("LumiNet.Canal");
    bind_object_method(canal, make_native_function, "connecter",
        [make_native_function](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            stdlib_expect_positional(runtime, args, 1, "LumiNet.Canal.connecter", native_args.site);
            const ParsedHttpUrl parsed = parse_http_url(runtime,
                                                       stdlib_expect_text(runtime, args[0].value, "LumiNet.Canal.connecter", native_args.site),
                                                       "LumiNet.Canal.connecter",
                                                       native_args.site);

            addrinfo hints{};
            hints.ai_family = AF_UNSPEC;
            hints.ai_socktype = SOCK_STREAM;
            addrinfo *result = nullptr;
            const std::string port_text = std::to_string(parsed.port);
            const int rc = ::getaddrinfo(parsed.host.c_str(), port_text.c_str(), &hints, &result);
            if (rc != 0)
            {
                raise_network_error(runtime, native_args.site, "LumiNet.Canal.connecter", gai_strerror(rc));
            }
            std::unique_ptr<addrinfo, decltype(&::freeaddrinfo)> guard(result, ::freeaddrinfo);
            SocketHandle fd = kInvalidSocketHandle;
            for (addrinfo *entry = result; entry != nullptr; entry = entry->ai_next)
            {
                initialize_socket_platform();
                fd = ::socket(entry->ai_family, entry->ai_socktype, entry->ai_protocol);
                if (!socket_handle_valid(fd))
                {
                    continue;
                }
                if (::connect(fd, entry->ai_addr, entry->ai_addrlen) == 0)
                {
                    break;
                }
                close_socket_fd(fd);
            }
            if (!socket_handle_valid(fd))
            {
                raise_network_error(runtime, native_args.site, "LumiNet.Canal.connecter", socket_error_text("connexion"));
            }
            try
            {
                const std::string client_key = "dGhlIHNhbXBsZSBub25jZQ==";
                const std::string request =
                    "GET " + parsed.target + " HTTP/1.1\r\n"
                    "Host: " + parsed.host + "\r\n"
                    "Upgrade: websocket\r\n"
                    "Connection: Upgrade\r\n"
                    "Sec-WebSocket-Key: " + client_key + "\r\n"
                    "Sec-WebSocket-Version: 13\r\n\r\n";
                send_all(runtime, fd,
                         reinterpret_cast<const unsigned char *>(request.data()),
                         request.size(),
                         "LumiNet.Canal.connecter",
                         native_args.site);
                const std::string response = recv_http_message(runtime, fd, "LumiNet.Canal.connecter", native_args.site);
                const std::size_t header_end = response.find("\r\n\r\n");
                if (header_end == std::string::npos)
                {
                    runtime.raise_runtime_error(native_args.site, "LumiNet.Canal.connecter a échoué: réponse websocket invalide");
                }
                const std::string response_head = response.substr(0, header_end);
                const int64_t status = parse_http_status_line(runtime, response_head, "LumiNet.Canal.connecter", native_args.site);
                const std::vector<std::pair<std::string, std::string>> response_headers = parse_http_headers_block(response_head);
                const std::string accept = header_value_or_empty(response_headers, "Sec-WebSocket-Accept");
                const std::string upgrade = to_lower_ascii(header_value_or_empty(response_headers, "Upgrade"));
                const bool has_upgrade = header_contains_token(response_headers, "Connection", "Upgrade");
                if (status != 101 || upgrade != "websocket" || !has_upgrade || accept != websocket_accept_key(client_key))
                {
                    runtime.raise_runtime_error(native_args.site, "LumiNet.Canal.connecter a échoué: poignée de main websocket refusée");
                }
                auto state = std::make_shared<CanalClientState>();
                state->fd = fd;
                state->client_side = true;
                state->address = parsed.host;
                state->pending_bytes.assign(response.begin() + static_cast<std::ptrdiff_t>(header_end + 4), response.end());
                return make_canal_client_value(runtime, state, make_native_function, native_args.site);
            }
            catch (...)
            {
                close_socket_fd(fd);
                throw;
            }
        });
    bind_object_method(canal, make_native_function, "Serveur",
        [make_native_function](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            stdlib_expect_positional(runtime, *native_args.arguments, 0, "LumiNet.Canal.Serveur", native_args.site);
            return make_canal_server_value(std::make_shared<CanalServerState>(), make_native_function);
        });
    return Value::objet(std::move(canal));
}

} // namespace lumiere
