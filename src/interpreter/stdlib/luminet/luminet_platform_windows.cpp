#include "../luminet_platform.hpp"

#ifdef _WIN32

#include <climits>
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

bool platform_socket_connect_with_timeout(SocketHandle handle,
                                          const sockaddr *addr,
                                          socklen_t addrlen,
                                          int64_t timeout_ms)
{
    u_long non_blocking = 1;
    if (::ioctlsocket(handle, FIONBIO, &non_blocking) != 0)
    {
        return false;
    }
    const auto restore_blocking = [&]() {
        u_long blocking = 0;
        ::ioctlsocket(handle, FIONBIO, &blocking);
    };

    if (::connect(handle, addr, static_cast<int>(addrlen)) == 0)
    {
        restore_blocking();
        return true;
    }
    if (::WSAGetLastError() != WSAEWOULDBLOCK)
    {
        const int connect_error = ::WSAGetLastError();
        restore_blocking();
        ::WSASetLastError(connect_error);
        return false;
    }

    WSAPOLLFD pfd{};
    pfd.fd = handle;
    pfd.events = POLLOUT;
    const int poll_timeout_ms =
        timeout_ms > static_cast<int64_t>(INT_MAX) ? INT_MAX : static_cast<int>(timeout_ms);
    const int poll_rc = ::WSAPoll(&pfd, 1, poll_timeout_ms);
    if (poll_rc == 0)
    {
        restore_blocking();
        ::WSASetLastError(WSAETIMEDOUT);
        return false;
    }
    if (poll_rc == SOCKET_ERROR)
    {
        const int poll_error = ::WSAGetLastError();
        restore_blocking();
        ::WSASetLastError(poll_error);
        return false;
    }

    int so_error = 0;
    int so_error_len = sizeof(so_error);
    if (::getsockopt(handle, SOL_SOCKET, SO_ERROR, reinterpret_cast<char *>(&so_error), &so_error_len) != 0)
    {
        const int getsockopt_error = ::WSAGetLastError();
        restore_blocking();
        ::WSASetLastError(getsockopt_error);
        return false;
    }
    restore_blocking();
    if (so_error != 0)
    {
        ::WSASetLastError(so_error);
        return false;
    }
    return true;
}

void platform_socket_enable_reuse_address(SocketHandle handle)
{
    const BOOL reuse = TRUE;
    ::setsockopt(handle, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char *>(&reuse), sizeof(reuse));
}

void platform_socket_enable_nosigpipe(SocketHandle)
{
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
