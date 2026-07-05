#include "../luminet_shared.hpp"

#include <algorithm>
#include <array>
#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <memory>
#include <netdb.h>
#include <netinet/in.h>
#include <sstream>
#include <string>
#include <string_view>
#include <unistd.h>

namespace lumiere
{

std::shared_ptr<LumiereObject> make_hidden_typed_object(const std::string &type_name)
{
    auto object = std::make_shared<LumiereObject>();
    object->fields["__type"] = Value::texte(type_name);
    return object;
}

void attach_native_state(const std::shared_ptr<LumiereObject> &object, NativeStatePtr state)
{
    object->native_state = std::move(state);
}

int64_t expect_duration_millis(IRuntime &runtime,
                               const Value &value,
                               const std::string &context,
                               const RuntimeSite &site)
{
    if (!value.is_objet())
    {
        runtime.raise_runtime_error(site, context + " attend une Durée");
    }

    const auto object = value.as_objet();
    const auto type_it = object->fields.find("__type");
    const auto millis_it = object->fields.find("__millis");
    if (type_it == object->fields.end() || !type_it->second.is_texte() ||
        type_it->second.as_texte() != "Durée" ||
        millis_it == object->fields.end() || !millis_it->second.is_entier())
    {
        runtime.raise_runtime_error(site, context + " attend une Durée");
    }

    return millis_it->second.as_entier();
}

std::string socket_error_text(const std::string &context)
{
    return context + ": " + std::strerror(errno);
}

void close_socket_fd(int &fd)
{
    if (fd >= 0)
    {
        ::close(fd);
        fd = -1;
    }
}

ssize_t socket_send_bytes(int fd, const void *data, std::size_t size, int flags)
{
    for (;;)
    {
#ifdef MSG_NOSIGNAL
        const ssize_t result = ::send(fd, data, size, flags | MSG_NOSIGNAL);
#else
        const ssize_t result = ::send(fd, data, size, flags);
#endif
        if (result < 0 && errno == EINTR)
        {
            continue;
        }
        return result;
    }
}

ssize_t socket_sendto_bytes(int fd,
                            const void *data,
                            std::size_t size,
                            int flags,
                            const sockaddr *addr,
                            socklen_t addrlen)
{
    for (;;)
    {
#ifdef MSG_NOSIGNAL
        const ssize_t result = ::sendto(fd, data, size, flags | MSG_NOSIGNAL, addr, addrlen);
#else
        const ssize_t result = ::sendto(fd, data, size, flags, addr, addrlen);
#endif
        if (result < 0 && errno == EINTR)
        {
            continue;
        }
        return result;
    }
}

ssize_t socket_recv_bytes(int fd, void *buffer, std::size_t size, int flags)
{
    for (;;)
    {
        const ssize_t result = ::recv(fd, buffer, size, flags);
        if (result < 0 && errno == EINTR)
        {
            continue;
        }
        return result;
    }
}

ssize_t socket_recvfrom_bytes(int fd,
                              void *buffer,
                              std::size_t size,
                              int flags,
                              sockaddr *addr,
                              socklen_t *addrlen)
{
    for (;;)
    {
        const ssize_t result = ::recvfrom(fd, buffer, size, flags, addr, addrlen);
        if (result < 0 && errno == EINTR)
        {
            continue;
        }
        return result;
    }
}

void raise_network_error(IRuntime &runtime,
                         const RuntimeSite &site,
                         const std::string &context,
                         const std::string &message)
{
    runtime.raise_runtime_error(site, context + " a echoue: " + message);
}

std::string to_lower_ascii(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return text;
}

std::string trim_ascii_copy(std::string text)
{
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t' || text.front() == '\r' || text.front() == '\n'))
    {
        text.erase(text.begin());
    }
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r' || text.back() == '\n'))
    {
        text.pop_back();
    }
    return text;
}

uint32_t sha1_left_rotate(uint32_t value, int count)
{
    return (value << count) | (value >> (32 - count));
}

std::array<unsigned char, 20> sha1_bytes(const std::string &input)
{
    uint64_t bit_length = static_cast<uint64_t>(input.size()) * 8;
    std::vector<unsigned char> data(input.begin(), input.end());
    data.push_back(0x80);
    while ((data.size() % 64) != 56)
    {
        data.push_back(0x00);
    }
    for (int i = 7; i >= 0; --i)
    {
        data.push_back(static_cast<unsigned char>((bit_length >> (i * 8)) & 0xff));
    }

    uint32_t h0 = 0x67452301;
    uint32_t h1 = 0xEFCDAB89;
    uint32_t h2 = 0x98BADCFE;
    uint32_t h3 = 0x10325476;
    uint32_t h4 = 0xC3D2E1F0;

    for (std::size_t chunk = 0; chunk < data.size(); chunk += 64)
    {
        uint32_t w[80] = {};
        for (int i = 0; i < 16; ++i)
        {
            w[i] = (static_cast<uint32_t>(data[chunk + i * 4]) << 24) |
                   (static_cast<uint32_t>(data[chunk + i * 4 + 1]) << 16) |
                   (static_cast<uint32_t>(data[chunk + i * 4 + 2]) << 8) |
                   static_cast<uint32_t>(data[chunk + i * 4 + 3]);
        }
        for (int i = 16; i < 80; ++i)
        {
            w[i] = sha1_left_rotate(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        }

        uint32_t a = h0;
        uint32_t b = h1;
        uint32_t c = h2;
        uint32_t d = h3;
        uint32_t e = h4;

        for (int i = 0; i < 80; ++i)
        {
            uint32_t f = 0;
            uint32_t k = 0;
            if (i < 20)
            {
                f = (b & c) | ((~b) & d);
                k = 0x5A827999;
            }
            else if (i < 40)
            {
                f = b ^ c ^ d;
                k = 0x6ED9EBA1;
            }
            else if (i < 60)
            {
                f = (b & c) | (b & d) | (c & d);
                k = 0x8F1BBCDC;
            }
            else
            {
                f = b ^ c ^ d;
                k = 0xCA62C1D6;
            }

            const uint32_t temp = sha1_left_rotate(a, 5) + f + e + k + w[i];
            e = d;
            d = c;
            c = sha1_left_rotate(b, 30);
            b = a;
            a = temp;
        }

        h0 += a;
        h1 += b;
        h2 += c;
        h3 += d;
        h4 += e;
    }

    std::array<unsigned char, 20> digest{};
    const uint32_t words[5] = {h0, h1, h2, h3, h4};
    for (int i = 0; i < 5; ++i)
    {
        digest[i * 4] = static_cast<unsigned char>((words[i] >> 24) & 0xff);
        digest[i * 4 + 1] = static_cast<unsigned char>((words[i] >> 16) & 0xff);
        digest[i * 4 + 2] = static_cast<unsigned char>((words[i] >> 8) & 0xff);
        digest[i * 4 + 3] = static_cast<unsigned char>(words[i] & 0xff);
    }
    return digest;
}

std::string base64_encode(const unsigned char *data, std::size_t size)
{
    static constexpr char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string output;
    for (std::size_t i = 0; i < size; i += 3)
    {
        const uint32_t chunk = (static_cast<uint32_t>(data[i]) << 16) |
                               (static_cast<uint32_t>(i + 1 < size ? data[i + 1] : 0) << 8) |
                               static_cast<uint32_t>(i + 2 < size ? data[i + 2] : 0);
        output.push_back(alphabet[(chunk >> 18) & 0x3f]);
        output.push_back(alphabet[(chunk >> 12) & 0x3f]);
        output.push_back(i + 1 < size ? alphabet[(chunk >> 6) & 0x3f] : '=');
        output.push_back(i + 2 < size ? alphabet[chunk & 0x3f] : '=');
    }
    return output;
}

std::string websocket_accept_key(const std::string &client_key)
{
    static constexpr std::string_view magic = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    const auto digest = sha1_bytes(client_key + std::string(magic));
    return base64_encode(digest.data(), digest.size());
}

bool parse_ipv4_only(const std::string &text)
{
    sockaddr_in addr{};
    return ::inet_pton(AF_INET, text.c_str(), &addr.sin_addr) == 1;
}

bool parse_ipv6_only(const std::string &text)
{
    sockaddr_in6 addr{};
    return ::inet_pton(AF_INET6, text.c_str(), &addr.sin6_addr) == 1;
}

bool is_local_host_text(const std::string &host)
{
    return host == "localhost" ||
           host == "127.0.0.1" ||
           host == "::1" ||
           host.rfind("192.168.", 0) == 0 ||
           host.rfind("10.", 0) == 0 ||
           (host.rfind("172.", 0) == 0 && [&host]() {
               const std::size_t first_dot = host.find('.');
               const std::size_t second_dot = host.find('.', first_dot == std::string::npos ? first_dot : first_dot + 1);
               if (first_dot == std::string::npos || second_dot == std::string::npos)
               {
                   return false;
               }
               int segment = 0;
               try
               {
                   segment = std::stoi(host.substr(first_dot + 1, second_dot - first_dot - 1));
               }
               catch (const std::exception &)
               {
                   return false;
               }
               return segment >= 16 && segment <= 31;
           }());
}

std::string french_http_method(const std::string &method)
{
    if (method == "GET")
    {
        return "OBTENIR";
    }
    if (method == "POST")
    {
        return "CRÉER";
    }
    if (method == "PUT")
    {
        return "MODIFIER";
    }
    if (method == "DELETE")
    {
        return "SUPPRIMER";
    }
    return method;
}

std::pair<std::string, int64_t> parse_host_port(IRuntime &runtime,
                                                const std::string &text,
                                                const std::string &context,
                                                const RuntimeSite &site)
{
    std::string host;
    std::string port_text;

    if (!text.empty() && text.front() == '[')
    {
        const std::size_t closing = text.find(']');
        if (closing == std::string::npos || closing + 1 >= text.size() || text[closing + 1] != ':')
        {
            runtime.raise_runtime_error(site, context + " requiert une adresse hôte:port valide");
        }
        host = text.substr(1, closing - 1);
        port_text = text.substr(closing + 2);
    }
    else
    {
        const std::size_t colon = text.rfind(':');
        if (colon == std::string::npos || colon == 0 || colon + 1 >= text.size())
        {
            runtime.raise_runtime_error(site, context + " requiert une adresse hôte:port valide");
        }
        if (text.find(':') != colon)
        {
            runtime.raise_runtime_error(site, context + " requiert une adresse IPv6 entre crochets");
        }
        host = text.substr(0, colon);
        port_text = text.substr(colon + 1);
    }

    try
    {
        const int64_t port = std::stoll(port_text);
        if (port < 0 || port > 65535)
        {
            runtime.raise_runtime_error(site, context + " requiert un port entre 0 et 65535");
        }
        return {host, port};
    }
    catch (const std::exception &)
    {
        runtime.raise_runtime_error(site, context + " requiert un port valide");
    }
}

ParsedHttpUrl parse_http_url(IRuntime &runtime,
                             const std::string &url,
                             const std::string &context,
                             const RuntimeSite &site)
{
    const std::size_t scheme_sep = url.find("://");
    if (scheme_sep == std::string::npos)
    {
        runtime.raise_runtime_error(site, context + " requiert une URL absolue");
    }

    ParsedHttpUrl parsed;
    parsed.scheme = url.substr(0, scheme_sep);
    const std::string remainder = url.substr(scheme_sep + 3);

    const std::size_t slash = remainder.find('/');
    const std::string authority = slash == std::string::npos ? remainder : remainder.substr(0, slash);
    parsed.target = slash == std::string::npos ? "/" : remainder.substr(slash);

    if (authority.empty())
    {
        runtime.raise_runtime_error(site, context + " requiert un hôte URL valide");
    }

    if (!authority.empty() && authority.front() == '[')
    {
        const std::size_t closing = authority.find(']');
        if (closing == std::string::npos)
        {
            runtime.raise_runtime_error(site, context + " requiert une URL IPv6 valide");
        }
        parsed.host = authority.substr(1, closing - 1);
        try
        {
            if (closing + 1 < authority.size())
            {
                if (authority[closing + 1] != ':')
                {
                    runtime.raise_runtime_error(site, context + " requiert une URL valide");
                }
                parsed.port = std::stoll(authority.substr(closing + 2));
            }
            else
            {
                parsed.port = 80;
            }
        }
        catch (const std::exception &)
        {
            runtime.raise_runtime_error(site, context + " requiert un port URL valide");
        }
    }
    else
    {
        const std::size_t colon = authority.rfind(':');
        if (colon != std::string::npos && authority.find(':') == colon)
        {
            parsed.host = authority.substr(0, colon);
            try
            {
                parsed.port = std::stoll(authority.substr(colon + 1));
            }
            catch (const std::exception &)
            {
                runtime.raise_runtime_error(site, context + " requiert un port URL valide");
            }
        }
        else
        {
            parsed.host = authority;
            parsed.port = 80;
        }
    }

    if (parsed.host.empty())
    {
        runtime.raise_runtime_error(site, context + " requiert un hôte URL valide");
    }
    if (parsed.port < 0 || parsed.port > 65535)
    {
        runtime.raise_runtime_error(site, context + " requiert un port URL entre 0 et 65535");
    }

    return parsed;
}

std::string address_to_text(const sockaddr *addr)
{
    char host_buffer[NI_MAXHOST] = {};
    if (::getnameinfo(addr,
                      addr->sa_family == AF_INET ? sizeof(sockaddr_in) : sizeof(sockaddr_in6),
                      host_buffer,
                      sizeof(host_buffer),
                      nullptr,
                      0,
                      NI_NUMERICHOST) != 0)
    {
        return "";
    }
    return host_buffer;
}

int64_t port_from_sockaddr(const sockaddr *addr)
{
    if (addr->sa_family == AF_INET)
    {
        return ntohs(reinterpret_cast<const sockaddr_in *>(addr)->sin_port);
    }
    if (addr->sa_family == AF_INET6)
    {
        return ntohs(reinterpret_cast<const sockaddr_in6 *>(addr)->sin6_port);
    }
    return 0;
}

} // namespace lumiere
