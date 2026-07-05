#include "../luminet_shared.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <sstream>
#include <string>
#include <sys/socket.h>

namespace lumiere
{

namespace
{

constexpr std::size_t kMaxHttpHeaderBytes = 64 * 1024;
constexpr std::size_t kMaxHttpBodyBytes = 10 * 1024 * 1024;

}

void apply_timeout(IRuntime &runtime,
                   int fd,
                   int64_t timeout_ms,
                   const std::string &context,
                   const RuntimeSite &site)
{
    if (timeout_ms < 0)
    {
        runtime.raise_runtime_error(site, context + " requiert une durée non négative");
    }

    timeval tv{};
    tv.tv_sec = static_cast<time_t>(timeout_ms / 1000);
    tv.tv_usec = static_cast<suseconds_t>((timeout_ms % 1000) * 1000);
    if (::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) != 0 ||
        ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)) != 0)
    {
        raise_network_error(runtime, site, context, socket_error_text("délai"));
    }
}

std::vector<std::pair<std::string, std::string>> expect_header_entries(IRuntime &runtime,
                                                                       const Value &value,
                                                                       const std::string &context,
                                                                       const RuntimeSite &site)
{
    if (!value.is_dictionnaire())
    {
        runtime.raise_runtime_error(site, context + " attend un Dictionnaire[Texte, Texte]");
    }

    std::vector<std::pair<std::string, std::string>> headers;
    for (const auto &entry : value.as_dictionnaire()->entries)
    {
        if (!entry.first.is_texte() || !entry.second.is_texte())
        {
            runtime.raise_runtime_error(site, context + " attend un Dictionnaire[Texte, Texte]");
        }
        headers.push_back({entry.first.as_texte(), entry.second.as_texte()});
    }
    return headers;
}

Value make_text_dictionary_value(IRuntime &runtime,
                                 const std::vector<std::pair<std::string, std::string>> &entries,
                                 const RuntimeSite &site)
{
    auto dict = std::make_shared<DictData>();
    for (const auto &entry : entries)
    {
        dict->entries.push_back({Value::texte(entry.first), Value::texte(entry.second)});
    }
    Value result = Value::dictionnaire(std::move(dict));
    runtime.annotate_value(result, "Dictionnaire[Texte, Texte]", site);
    return result;
}

std::shared_ptr<ListeData> bytes_to_list(const std::vector<unsigned char> &bytes)
{
    auto result = std::make_shared<ListeData>();
    result->elements.reserve(bytes.size());
    for (unsigned char byte : bytes)
    {
        result->elements.push_back(Value::entier(byte));
    }
    return result;
}

std::vector<unsigned char> expect_byte_vector(IRuntime &runtime,
                                              const Value &value,
                                              const std::string &context,
                                              const RuntimeSite &site)
{
    if (!value.is_liste())
    {
        runtime.raise_runtime_error(site, context + " attend une Liste[Entier]");
    }

    std::vector<unsigned char> bytes;
    for (const auto &element : value.as_liste()->elements)
    {
        const int64_t byte = stdlib_expect_integer(runtime, element, context, site);
        if (byte < 0 || byte > 255)
        {
            runtime.raise_runtime_error(site, context + " requiert des octets entre 0 et 255");
        }
        bytes.push_back(static_cast<unsigned char>(byte));
    }

    return bytes;
}

void send_all(IRuntime &runtime,
              int fd,
              const unsigned char *data,
              std::size_t size,
              const std::string &context,
              const RuntimeSite &site)
{
    std::size_t sent = 0;
    while (sent < size)
    {
        const ssize_t written = socket_send_bytes(fd, data + sent, size - sent);
        if (written < 0)
        {
            raise_network_error(runtime, site, context, socket_error_text("envoi"));
        }
        if (written == 0)
        {
            runtime.raise_runtime_error(site, context + " a échoué: connexion fermée pendant l'envoi");
        }
        sent += static_cast<std::size_t>(written);
    }
}

Value make_address_value(const std::string &host,
                         int64_t port,
                         const NativeFunctionFactory &make_native_function)
{
    auto object = make_hidden_typed_object("AdresseRéseau");
    object->fields["hôte"] = Value::texte(host);
    object->fields["port"] = Value::entier(port);
    object->fields["en_texte"] = Value::fonction(make_native_function(
        [host, port](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            stdlib_expect_positional(runtime, *native_args.arguments, 0, "AdresseRéseau.en_texte", native_args.site);
            std::ostringstream out;
            if (host.find(':') != std::string::npos)
            {
                out << "[" << host << "]:" << port;
            }
            else
            {
                out << host << ":" << port;
            }
            return Value::texte(out.str());
        }));
    return Value::objet(std::move(object));
}

Value make_udp_packet_text_value(const std::string &text,
                                 const std::string &address,
                                 int64_t port)
{
    auto object = make_hidden_typed_object("PaquetUDP");
    object->fields["données"] = Value::texte(text);
    object->fields["adresse"] = Value::texte(address);
    object->fields["port"] = Value::entier(port);
    return Value::objet(std::move(object));
}

Value make_udp_packet_bytes_value(IRuntime &runtime,
                                  const std::vector<unsigned char> &bytes,
                                  const std::string &address,
                                  int64_t port,
                                  const RuntimeSite &site)
{
    auto object = make_hidden_typed_object("PaquetUDP");
    Value octets = Value::liste(bytes_to_list(bytes));
    runtime.annotate_value(octets, "Liste[Entier]", site);
    object->fields["octets"] = octets;
    object->fields["adresse"] = Value::texte(address);
    object->fields["port"] = Value::entier(port);
    return Value::objet(std::move(object));
}

void bind_object_method(const std::shared_ptr<LumiereObject> &object,
                        const NativeFunctionFactory &make_native_function,
                        const std::string &name,
                        LumiereFunction::NativeHandler handler)
{
    object->fields[name] = Value::fonction(make_native_function(std::move(handler)));
}

std::string header_value_or_empty(const std::vector<std::pair<std::string, std::string>> &headers,
                                  const std::string &name)
{
    const std::string needle = to_lower_ascii(name);
    for (const auto &entry : headers)
    {
        if (to_lower_ascii(entry.first) == needle)
        {
            return entry.second;
        }
    }
    return "";
}

bool header_contains_token(const std::vector<std::pair<std::string, std::string>> &headers,
                           const std::string &name,
                           const std::string &token)
{
    const std::string lower_token = to_lower_ascii(token);
    const std::string value = to_lower_ascii(header_value_or_empty(headers, name));
    std::size_t start = 0;
    while (start <= value.size())
    {
        const std::size_t comma = value.find(',', start);
        const std::string piece = trim_ascii_copy(
            value.substr(start, comma == std::string::npos ? std::string::npos : comma - start));
        if (piece == lower_token)
        {
            return true;
        }
        if (comma == std::string::npos)
        {
            break;
        }
        start = comma + 1;
    }
    return false;
}

int64_t parse_http_status_line(IRuntime &runtime,
                               const std::string &response_head,
                               const std::string &context,
                               const RuntimeSite &site)
{
    std::istringstream head_stream(response_head);
    std::string status_line;
    std::getline(head_stream, status_line);
    if (!status_line.empty() && status_line.back() == '\r')
    {
        status_line.pop_back();
    }

    std::istringstream status_stream(status_line);
    std::string version;
    int64_t status = 0;
    status_stream >> version >> status;
    if (version.rfind("HTTP/", 0) != 0 || status <= 0)
    {
        runtime.raise_runtime_error(site, context + " a reçu une réponse HTTP invalide");
    }
    return status;
}

std::vector<std::pair<std::string, std::string>> parse_http_headers_block(const std::string &response_head)
{
    std::istringstream head_stream(response_head);
    std::string ignored_status_line;
    std::getline(head_stream, ignored_status_line);

    std::vector<std::pair<std::string, std::string>> headers;
    for (std::string line; std::getline(head_stream, line); )
    {
        if (!line.empty() && line.back() == '\r')
        {
            line.pop_back();
        }
        const std::size_t colon = line.find(':');
        if (colon != std::string::npos)
        {
            headers.push_back({
                trim_ascii_copy(line.substr(0, colon)),
                trim_ascii_copy(line.substr(colon + 1)),
            });
        }
    }
    return headers;
}

std::vector<std::pair<std::string, std::string>> parse_query_pairs(const std::string &query)
{
    std::vector<std::pair<std::string, std::string>> result;
    std::size_t start = 0;
    while (start <= query.size())
    {
        const std::size_t amp = query.find('&', start);
        const std::string item = query.substr(start, amp == std::string::npos ? std::string::npos : amp - start);
        if (!item.empty())
        {
            const std::size_t eq = item.find('=');
            if (eq == std::string::npos)
            {
                result.push_back({item, ""});
            }
            else
            {
                result.push_back({item.substr(0, eq), item.substr(eq + 1)});
            }
        }
        if (amp == std::string::npos)
        {
            break;
        }
        start = amp + 1;
    }
    return result;
}

bool match_route_pattern(const std::string &pattern,
                         const std::string &path,
                         std::vector<std::pair<std::string, std::string>> &params)
{
    params.clear();
    std::vector<std::string> pattern_parts;
    std::vector<std::string> path_parts;
    auto split = [](const std::string &input) {
        std::vector<std::string> parts;
        std::size_t start = 0;
        while (start <= input.size())
        {
            const std::size_t slash = input.find('/', start);
            const std::string part = input.substr(start, slash == std::string::npos ? std::string::npos : slash - start);
            if (!part.empty())
            {
                parts.push_back(part);
            }
            if (slash == std::string::npos)
            {
                break;
            }
            start = slash + 1;
        }
        return parts;
    };
    pattern_parts = split(pattern);
    path_parts = split(path);
    if (pattern_parts.size() != path_parts.size())
    {
        return false;
    }
    for (std::size_t i = 0; i < pattern_parts.size(); ++i)
    {
        if (!pattern_parts[i].empty() && pattern_parts[i].front() == ':')
        {
            params.push_back({pattern_parts[i].substr(1), path_parts[i]});
            continue;
        }
        if (pattern_parts[i] != path_parts[i])
        {
            return false;
        }
    }
    return true;
}

HttpRequestData parse_http_request(IRuntime &runtime,
                                   const std::string &raw,
                                   const std::string &context,
                                   const RuntimeSite &site)
{
    const std::size_t header_end = raw.find("\r\n\r\n");
    if (header_end == std::string::npos)
    {
        runtime.raise_runtime_error(site, context + " requiert une requête HTTP valide");
    }

    HttpRequestData request;
    std::istringstream stream(raw.substr(0, header_end));
    std::string request_line;
    std::getline(stream, request_line);
    if (!request_line.empty() && request_line.back() == '\r')
    {
        request_line.pop_back();
    }

    std::istringstream line_stream(request_line);
    std::string target;
    std::string version;
    line_stream >> request.method >> target >> version;
    if (request.method.empty() || target.empty())
    {
        runtime.raise_runtime_error(site, context + " requiert une requête HTTP valide");
    }
    request.french_method = french_http_method(request.method);
    const std::size_t qmark = target.find('?');
    request.path = qmark == std::string::npos ? target : target.substr(0, qmark);
    request.query_string = qmark == std::string::npos ? "" : target.substr(qmark + 1);
    request.query_params = parse_query_pairs(request.query_string);

    for (std::string header_line; std::getline(stream, header_line); )
    {
        if (!header_line.empty() && header_line.back() == '\r')
        {
            header_line.pop_back();
        }
        if (header_line.empty())
        {
            continue;
        }
        const std::size_t colon = header_line.find(':');
        if (colon == std::string::npos)
        {
            continue;
        }
        request.headers.push_back({
            trim_ascii_copy(header_line.substr(0, colon)),
            trim_ascii_copy(header_line.substr(colon + 1)),
        });
    }

    request.body = raw.substr(header_end + 4);
    return request;
}

std::string recv_http_message(IRuntime &runtime,
                              int fd,
                              const std::string &context,
                              const RuntimeSite &site)
{
    std::string data;
    char buffer[4096];
    std::size_t header_end = std::string::npos;
    std::size_t required_size = std::string::npos;

    while (true)
    {
        const ssize_t received = socket_recv_bytes(fd, buffer, sizeof(buffer));
        if (received < 0)
        {
            raise_network_error(runtime, site, context, socket_error_text("lecture"));
        }
        if (received == 0)
        {
            if (required_size != std::string::npos && data.size() < required_size)
            {
                runtime.raise_runtime_error(site, context + " a reçu un message HTTP tronqué");
            }
            break;
        }
        data.append(buffer, buffer + received);
        if (header_end == std::string::npos && data.size() > kMaxHttpHeaderBytes)
        {
            runtime.raise_runtime_error(site, context + " a reçu des en-têtes HTTP trop volumineux");
        }

        if (header_end == std::string::npos)
        {
            header_end = data.find("\r\n\r\n");
            if (header_end != std::string::npos)
            {
                const std::string headers = data.substr(0, header_end);
                const std::string lower = to_lower_ascii(headers);
                const std::string key = "content-length:";
                const std::size_t pos = lower.find(key);
                std::size_t content_length = 0;
                if (pos != std::string::npos)
                {
                    const std::size_t end = headers.find("\r\n", pos);
                    const std::string value = trim_ascii_copy(headers.substr(pos + key.size(),
                                                                              end == std::string::npos ? std::string::npos : end - pos - key.size()));
                    try
                    {
                        content_length = static_cast<std::size_t>(std::stoull(value));
                    }
                    catch (const std::exception &)
                    {
                        runtime.raise_runtime_error(site, context + " a reçu un en-tête Content-Length invalide");
                    }
                }
                if (content_length > kMaxHttpBodyBytes)
                {
                    runtime.raise_runtime_error(site, context + " a reçu un corps HTTP trop volumineux");
                }
                required_size = header_end + 4 + content_length;
            }
        }

        if (required_size != std::string::npos && data.size() >= required_size)
        {
            break;
        }
    }
    return data;
}

bool recv_exact_bytes(int fd,
                      std::vector<unsigned char> &pending_bytes,
                      unsigned char *buffer,
                      std::size_t size)
{
    std::size_t offset = 0;
    if (!pending_bytes.empty())
    {
        const std::size_t chunk = std::min(size, pending_bytes.size());
        std::copy_n(pending_bytes.begin(), chunk, buffer);
        pending_bytes.erase(pending_bytes.begin(), pending_bytes.begin() + static_cast<std::ptrdiff_t>(chunk));
        offset = chunk;
    }
    while (offset < size)
    {
        const ssize_t received = socket_recv_bytes(fd, buffer + offset, size - offset);
        if (received <= 0)
        {
            return false;
        }
        offset += static_cast<std::size_t>(received);
    }
    return true;
}

bool send_websocket_frame(IRuntime &runtime,
                          int fd,
                          uint8_t opcode,
                          const std::vector<unsigned char> &payload,
                          bool mask,
                          const std::string &context,
                          const RuntimeSite &site)
{
    std::vector<unsigned char> frame;
    frame.push_back(static_cast<unsigned char>(0x80 | (opcode & 0x0f)));
    const std::size_t size = payload.size();
    if (size < 126)
    {
        frame.push_back(static_cast<unsigned char>((mask ? 0x80 : 0x00) | size));
    }
    else if (size <= 0xffff)
    {
        frame.push_back(static_cast<unsigned char>((mask ? 0x80 : 0x00) | 126));
        frame.push_back(static_cast<unsigned char>((size >> 8) & 0xff));
        frame.push_back(static_cast<unsigned char>(size & 0xff));
    }
    else
    {
        runtime.raise_runtime_error(site, context + " ne prend pas encore en charge des messages aussi grands");
    }

    std::array<unsigned char, 4> masking_key{0x11, 0x22, 0x33, 0x44};
    std::vector<unsigned char> body = payload;
    if (mask)
    {
        frame.insert(frame.end(), masking_key.begin(), masking_key.end());
        for (std::size_t i = 0; i < body.size(); ++i)
        {
            body[i] ^= masking_key[i % 4];
        }
    }
    frame.insert(frame.end(), body.begin(), body.end());
    send_all(runtime, fd, frame.data(), frame.size(), context, site);
    return true;
}

std::optional<WebSocketFrame> recv_websocket_frame(IRuntime &runtime,
                                                   int fd,
                                                   std::vector<unsigned char> &pending_bytes,
                                                   const std::string &context,
                                                   const RuntimeSite &site)
{
    unsigned char header[2];
    if (!pending_bytes.empty())
    {
        if (!recv_exact_bytes(fd, pending_bytes, header, sizeof(header)))
        {
            runtime.raise_runtime_error(site, context + " a reçu une trame websocket incomplète");
        }
    }
    else
    {
        const ssize_t header_peek = socket_recv_bytes(fd, header, 1, MSG_PEEK);
        if (header_peek == 0)
        {
            return std::nullopt;
        }
        if (header_peek < 0)
        {
            raise_network_error(runtime, site, context, socket_error_text("lecture websocket"));
        }
        if (!recv_exact_bytes(fd, pending_bytes, header, sizeof(header)))
        {
            runtime.raise_runtime_error(site, context + " a reçu une trame websocket incomplète");
        }
    }

    const bool masked = (header[1] & 0x80) != 0;
    uint64_t payload_length = header[1] & 0x7f;
    if (payload_length == 126)
    {
        unsigned char ext[2];
        if (!recv_exact_bytes(fd, pending_bytes, ext, sizeof(ext)))
        {
            return std::nullopt;
        }
        payload_length = (static_cast<uint64_t>(ext[0]) << 8) | ext[1];
    }
    else if (payload_length == 127)
    {
        runtime.raise_runtime_error(site, context + " ne prend pas encore en charge des trames websocket 64 bits");
    }

    std::array<unsigned char, 4> masking_key{};
    if (masked && !recv_exact_bytes(fd, pending_bytes, masking_key.data(), masking_key.size()))
    {
        return std::nullopt;
    }

    std::vector<unsigned char> payload(payload_length);
    if (payload_length > 0 && !recv_exact_bytes(fd, pending_bytes, payload.data(), payload.size()))
    {
        return std::nullopt;
    }
    if (masked)
    {
        for (std::size_t i = 0; i < payload.size(); ++i)
        {
            payload[i] ^= masking_key[i % 4];
        }
    }

    return WebSocketFrame{static_cast<uint8_t>(header[0] & 0x0f), std::move(payload)};
}

} // namespace lumiere
