#pragma once

#include "lumiere/interpreter/stdlib/helpers.hpp"
#include "lumiere/interpreter/stdlib/modules.hpp"

#include <array>
#include <memory>
#include <netdb.h>
#include <optional>
#include <string>
#include <string_view>
#include <sys/socket.h>
#include <vector>

namespace lumiere
{

    using NativeStatePtr = std::shared_ptr<void>;

    struct TcpConnectionState
    {
        int fd = -1;
        bool closed = false;

        ~TcpConnectionState();
    };

    struct TcpServerState
    {
        int fd = -1;
        bool stopped = false;
        Value on_connection = Value::rien();

        ~TcpServerState();
    };

    struct UdpSocketState
    {
        int fd = -1;
        bool closed = false;
        int port = 0;

        ~UdpSocketState();
    };

    struct HttpRoute
    {
        std::string method;
        std::string pattern;
        Value handler = Value::rien();
    };

    struct HttpServerState
    {
        int fd = -1;
        bool stopped = false;
        std::vector<Value> middleware;
        std::vector<HttpRoute> routes;
        std::vector<std::pair<std::string, Value>> canal_routes;

        ~HttpServerState();
    };

    struct CanalClientState
    {
        int fd = -1;
        bool closed = false;
        bool opened_notified = false;
        bool client_side = false;
        std::string address;
        std::vector<unsigned char> pending_bytes;
        Value on_open = Value::rien();
        Value on_message = Value::rien();
        Value on_close = Value::rien();
        Value on_error = Value::rien();

        ~CanalClientState();
    };

    struct CanalServerState
    {
        int fd = -1;
        bool stopped = false;
        Value on_connection = Value::rien();
        Value on_message = Value::rien();
        Value on_disconnect = Value::rien();
        Value on_error = Value::rien();

        ~CanalServerState();
    };

    struct HttpResponseWriterState
    {
        int fd = -1;
        bool sent = false;
        std::vector<std::pair<std::string, std::string>> headers;
    };

    struct ParsedHttpUrl
    {
        std::string scheme;
        std::string host;
        int64_t port = 0;
        std::string target;
    };

    struct HttpRequestData
    {
        std::string method;
        std::string french_method;
        std::string path;
        std::string query_string;
        std::string body;
        std::vector<std::pair<std::string, std::string>> headers;
        std::vector<std::pair<std::string, std::string>> route_params;
        std::vector<std::pair<std::string, std::string>> query_params;
    };

    struct WebSocketFrame
    {
        uint8_t opcode = 0;
        std::vector<unsigned char> payload;
    };

    std::shared_ptr<LumiereObject> make_hidden_typed_object(const std::string &type_name);
    void attach_native_state(const std::shared_ptr<LumiereObject> &object, NativeStatePtr state);

    template <typename State>
    std::shared_ptr<State> require_native_state(IRuntime &runtime,
                                                const std::shared_ptr<LumiereObject> &object,
                                                const std::string &expected_type,
                                                const std::string &context,
                                                const RuntimeSite &site)
    {
        if (object == nullptr)
        {
            runtime.raise_runtime_error(site, context + " requiert un " + expected_type + " valide");
        }

        const auto type_it = object->fields.find("__type");
        if (type_it == object->fields.end() || !type_it->second.is_texte() || type_it->second.as_texte() != expected_type)
        {
            runtime.raise_runtime_error(site, context + " requiert un " + expected_type);
        }

        auto state = std::static_pointer_cast<State>(object->native_state);
        if (state == nullptr)
        {
            runtime.raise_runtime_error(site, context + " requiert un " + expected_type + " valide");
        }

        return state;
    }

    int64_t expect_duration_millis(IRuntime &runtime,
                                   const Value &value,
                                   const std::string &context,
                                   const RuntimeSite &site);
    std::string socket_error_text(const std::string &context);
    void close_socket_fd(int &fd);
    ssize_t socket_send_bytes(int fd, const void *data, std::size_t size, int flags = 0);
    ssize_t socket_sendto_bytes(int fd,
                                const void *data,
                                std::size_t size,
                                int flags,
                                const sockaddr *addr,
                                socklen_t addrlen);
    ssize_t socket_recv_bytes(int fd, void *buffer, std::size_t size, int flags = 0);
    ssize_t socket_recvfrom_bytes(int fd,
                                  void *buffer,
                                  std::size_t size,
                                  int flags,
                                  sockaddr *addr,
                                  socklen_t *addrlen);
    void raise_network_error(IRuntime &runtime,
                             const RuntimeSite &site,
                             const std::string &context,
                             const std::string &message);
    std::string to_lower_ascii(std::string text);
    std::string trim_ascii_copy(std::string text);
    uint32_t sha1_left_rotate(uint32_t value, int count);
    std::array<unsigned char, 20> sha1_bytes(const std::string &input);
    std::string base64_encode(const unsigned char *data, std::size_t size);
    std::string websocket_accept_key(const std::string &client_key);
    bool parse_ipv4_only(const std::string &text);
    bool parse_ipv6_only(const std::string &text);
    bool is_local_host_text(const std::string &host);
    std::string french_http_method(const std::string &method);
    std::pair<std::string, int64_t> parse_host_port(IRuntime &runtime,
                                                    const std::string &text,
                                                    const std::string &context,
                                                    const RuntimeSite &site);
    ParsedHttpUrl parse_http_url(IRuntime &runtime,
                                 const std::string &url,
                                 const std::string &context,
                                 const RuntimeSite &site);
    std::string address_to_text(const sockaddr *addr);
    int64_t port_from_sockaddr(const sockaddr *addr);
    void apply_timeout(IRuntime &runtime,
                       int fd,
                       int64_t timeout_ms,
                       const std::string &context,
                       const RuntimeSite &site);
    std::vector<std::pair<std::string, std::string>> expect_header_entries(IRuntime &runtime,
                                                                           const Value &value,
                                                                           const std::string &context,
                                                                           const RuntimeSite &site);
    Value make_text_dictionary_value(IRuntime &runtime,
                                     const std::vector<std::pair<std::string, std::string>> &entries,
                                     const RuntimeSite &site);
    std::shared_ptr<ListeData> bytes_to_list(const std::vector<unsigned char> &bytes);
    std::vector<unsigned char> expect_byte_vector(IRuntime &runtime,
                                                  const Value &value,
                                                  const std::string &context,
                                                  const RuntimeSite &site);
    void send_all(IRuntime &runtime,
                  int fd,
                  const unsigned char *data,
                  std::size_t size,
                  const std::string &context,
                  const RuntimeSite &site);
    Value make_address_value(const std::string &host,
                             int64_t port,
                             const NativeFunctionFactory &make_native_function);
    Value make_udp_packet_text_value(const std::string &text,
                                     const std::string &address,
                                     int64_t port);
    Value make_udp_packet_bytes_value(IRuntime &runtime,
                                      const std::vector<unsigned char> &bytes,
                                      const std::string &address,
                                      int64_t port,
                                      const RuntimeSite &site);
    void bind_object_method(const std::shared_ptr<LumiereObject> &object,
                            const NativeFunctionFactory &make_native_function,
                            const std::string &name,
                            LumiereFunction::NativeHandler handler);
    std::string header_value_or_empty(const std::vector<std::pair<std::string, std::string>> &headers,
                                      const std::string &name);
    bool header_contains_token(const std::vector<std::pair<std::string, std::string>> &headers,
                               const std::string &name,
                               const std::string &token);
    int64_t parse_http_status_line(IRuntime &runtime,
                                   const std::string &response_head,
                                   const std::string &context,
                                   const RuntimeSite &site);
    std::vector<std::pair<std::string, std::string>> parse_http_headers_block(const std::string &response_head);
    std::vector<std::pair<std::string, std::string>> parse_query_pairs(const std::string &query);
    bool match_route_pattern(const std::string &pattern,
                             const std::string &path,
                             std::vector<std::pair<std::string, std::string>> &params);
    HttpRequestData parse_http_request(IRuntime &runtime,
                                       const std::string &raw,
                                       const std::string &context,
                                       const RuntimeSite &site);
    std::string recv_http_message(IRuntime &runtime,
                                  int fd,
                                  const std::string &context,
                                  const RuntimeSite &site);
    bool recv_exact_bytes(int fd,
                          std::vector<unsigned char> &pending_bytes,
                          unsigned char *buffer,
                          std::size_t size);
    bool send_websocket_frame(IRuntime &runtime,
                              int fd,
                              uint8_t opcode,
                              const std::vector<unsigned char> &payload,
                              bool mask,
                              const std::string &context,
                              const RuntimeSite &site);
    std::optional<WebSocketFrame> recv_websocket_frame(IRuntime &runtime,
                                                       int fd,
                                                       std::vector<unsigned char> &pending_bytes,
                                                       const std::string &context,
                                                       const RuntimeSite &site);
    Value make_http_response_value(IRuntime &runtime,
                                   int64_t status,
                                   const std::string &body,
                                   const std::vector<std::pair<std::string, std::string>> &headers,
                                   const NativeFunctionFactory &make_native_function,
                                   const RuntimeSite &site);
    Value make_http_request_value(IRuntime &runtime,
                                  const HttpRequestData &request,
                                  const NativeFunctionFactory &make_native_function,
                                  const RuntimeSite &site);
    std::string guess_content_type(const std::string &path);
    void send_http_response(IRuntime &runtime,
                            const std::shared_ptr<HttpResponseWriterState> &state,
                            int64_t status,
                            const std::string &body,
                            std::vector<std::pair<std::string, std::string>> headers,
                            const std::string &context,
                            const RuntimeSite &site);
    Value make_http_response_writer_value(IRuntime &runtime,
                                          const std::shared_ptr<HttpResponseWriterState> &state,
                                          const NativeFunctionFactory &make_native_function,
                                          const RuntimeSite &site);
    void run_canal_loop(IRuntime &runtime,
                        const std::shared_ptr<CanalClientState> &state,
                        bool server_dispatch_mode,
                        const Value &server_message_callback,
                        const Value &server_disconnect_callback,
                        const Value &server_error_callback,
                        Value client_value,
                        const RuntimeSite &site);
    Value make_canal_client_value(IRuntime &runtime,
                                  const std::shared_ptr<CanalClientState> &state,
                                  const NativeFunctionFactory &make_native_function,
                                  const RuntimeSite &site);
    Value make_canal_server_value(const std::shared_ptr<CanalServerState> &state,
                                  const NativeFunctionFactory &make_native_function);
    Value make_tcp_connection_value(const std::shared_ptr<TcpConnectionState> &state,
                                    const std::string &address,
                                    int64_t port,
                                    const NativeFunctionFactory &make_native_function);
    Value make_tcp_server_value(const std::shared_ptr<TcpServerState> &state,
                                const NativeFunctionFactory &make_native_function);
    Value make_http_server_value(const std::shared_ptr<HttpServerState> &state,
                                 const NativeFunctionFactory &make_native_function);
    Value make_udp_socket_value(const std::shared_ptr<UdpSocketState> &state,
                                const NativeFunctionFactory &make_native_function);

    Value make_luminet_adresse_module(const NativeFunctionFactory &make_native_function);
    Value make_luminet_dns_module(const NativeFunctionFactory &make_native_function);
    Value make_luminet_tcp_module(const NativeFunctionFactory &make_native_function);
    Value make_luminet_udp_module(const NativeFunctionFactory &make_native_function);
    Value make_luminet_http_module(const NativeFunctionFactory &make_native_function);
    Value make_luminet_canal_module(const NativeFunctionFactory &make_native_function);

} // namespace lumiere
