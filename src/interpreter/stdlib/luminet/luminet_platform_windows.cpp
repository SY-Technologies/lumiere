#include "../luminet_platform.hpp"

#ifdef _WIN32

#include <mutex>
#include <sstream>

namespace lumiere
{

namespace
{

std::once_flag g_winsock_once;

}

bool socket_handle_valid(SocketHandle handle)
{
    return handle != INVALID_SOCKET;
}

void initialize_socket_platform()
{
    std::call_once(g_winsock_once, []() {
        WSADATA wsa_data{};
        ::WSAStartup(MAKEWORD(2, 2), &wsa_data);
    });
}

int socket_last_error_code()
{
    return ::WSAGetLastError();
}

bool socket_error_is_interrupted(int error_code)
{
    return error_code == WSAEINTR;
}

std::string socket_error_message_from_code(int error_code)
{
    std::ostringstream out;
    out << "erreur réseau Windows " << error_code;
    return out.str();
}

std::string socket_last_error_message()
{
    return socket_error_message_from_code(socket_last_error_code());
}

void close_socket_handle(SocketHandle &handle)
{
    if (socket_handle_valid(handle))
    {
        ::closesocket(handle);
        handle = kInvalidSocketHandle;
    }
}

SocketSize platform_socket_send(SocketHandle handle, const void *data, std::size_t size, int flags)
{
    return ::send(handle, static_cast<const char *>(data), static_cast<int>(size), flags);
}

SocketSize platform_socket_sendto(SocketHandle handle,
                                  const void *data,
                                  std::size_t size,
                                  int flags,
                                  const sockaddr *addr,
                                  socklen_t addrlen)
{
    return ::sendto(handle,
                    static_cast<const char *>(data),
                    static_cast<int>(size),
                    flags,
                    addr,
                    static_cast<int>(addrlen));
}

SocketSize platform_socket_recv(SocketHandle handle, void *buffer, std::size_t size, int flags)
{
    return ::recv(handle, static_cast<char *>(buffer), static_cast<int>(size), flags);
}

SocketSize platform_socket_recvfrom(SocketHandle handle,
                                    void *buffer,
                                    std::size_t size,
                                    int flags,
                                    sockaddr *addr,
                                    socklen_t *addrlen)
{
    int mutable_len = static_cast<int>(*addrlen);
    const int result = ::recvfrom(handle, static_cast<char *>(buffer), static_cast<int>(size), flags, addr, &mutable_len);
    *addrlen = static_cast<socklen_t>(mutable_len);
    return result;
}

bool platform_socket_set_timeout(SocketHandle handle, int64_t timeout_ms)
{
    const DWORD timeout = static_cast<DWORD>(timeout_ms);
    return ::setsockopt(handle, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char *>(&timeout), sizeof(timeout)) == 0 &&
           ::setsockopt(handle, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char *>(&timeout), sizeof(timeout)) == 0;
}

void platform_socket_enable_reuse_address(SocketHandle handle)
{
    const BOOL reuse = TRUE;
    ::setsockopt(handle, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char *>(&reuse), sizeof(reuse));
}

bool platform_socket_enable_broadcast(SocketHandle handle)
{
    const BOOL enabled = TRUE;
    return ::setsockopt(handle, SOL_SOCKET, SO_BROADCAST, reinterpret_cast<const char *>(&enabled), sizeof(enabled)) == 0;
}

void platform_socket_shutdown(SocketHandle handle)
{
    if (socket_handle_valid(handle))
    {
        ::shutdown(handle, SD_BOTH);
    }
}

} // namespace lumiere

#endif
