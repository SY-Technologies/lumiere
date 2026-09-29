#include "../luminet_platform.hpp"

#include <climits>
#include <cstring>
#include <fcntl.h>
#include <poll.h>

namespace lumiere
{

bool socket_handle_valid(SocketHandle handle)
{
    return handle >= 0;
}

void initialize_socket_platform()
{
}

int socket_last_error_code()
{
    return errno;
}

bool socket_error_is_interrupted(int error_code)
{
    return error_code == EINTR;
}

std::string socket_error_message_from_code(int error_code)
{
    return std::strerror(error_code);
}

std::string socket_last_error_message()
{
    return socket_error_message_from_code(socket_last_error_code());
}

void close_socket_handle(SocketHandle &handle)
{
    if (socket_handle_valid(handle))
    {
        ::close(handle);
        handle = kInvalidSocketHandle;
    }
}

SocketSize platform_socket_send(SocketHandle handle, const void *data, std::size_t size, int flags)
{
    for (;;)
    {
#ifdef MSG_NOSIGNAL
        const SocketSize result = ::send(handle, data, size, flags | MSG_NOSIGNAL);
#else
        const SocketSize result = ::send(handle, data, size, flags);
#endif
        if (result < 0 && errno == EINTR)
        {
            continue;
        }
        return result;
    }
}

SocketSize platform_socket_sendto(SocketHandle handle,
                                  const void *data,
                                  std::size_t size,
                                  int flags,
                                  const sockaddr *addr,
                                  socklen_t addrlen)
{
    for (;;)
    {
#ifdef MSG_NOSIGNAL
        const SocketSize result = ::sendto(handle, data, size, flags | MSG_NOSIGNAL, addr, addrlen);
#else
        const SocketSize result = ::sendto(handle, data, size, flags, addr, addrlen);
#endif
        if (result < 0 && errno == EINTR)
        {
            continue;
        }
        return result;
    }
}

SocketSize platform_socket_recv(SocketHandle handle, void *buffer, std::size_t size, int flags)
{
    for (;;)
    {
        const SocketSize result = ::recv(handle, buffer, size, flags);
        if (result < 0 && errno == EINTR)
        {
            continue;
        }
        return result;
    }
}

SocketSize platform_socket_recvfrom(SocketHandle handle,
                                    void *buffer,
                                    std::size_t size,
                                    int flags,
                                    sockaddr *addr,
                                    socklen_t *addrlen)
{
    for (;;)
    {
        const SocketSize result = ::recvfrom(handle, buffer, size, flags, addr, addrlen);
        if (result < 0 && errno == EINTR)
        {
            continue;
        }
        return result;
    }
}

bool platform_socket_set_timeout(SocketHandle handle, int64_t timeout_ms)
{
    timeval tv{};
    tv.tv_sec = static_cast<time_t>(timeout_ms / 1000);
    tv.tv_usec = static_cast<suseconds_t>((timeout_ms % 1000) * 1000);
    return ::setsockopt(handle, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) == 0 &&
           ::setsockopt(handle, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)) == 0;
}

bool platform_socket_connect_with_timeout(SocketHandle handle,
                                          const sockaddr *addr,
                                          socklen_t addrlen,
                                          int64_t timeout_ms)
{
    const int flags = ::fcntl(handle, F_GETFL, 0);
    if (flags == -1 || ::fcntl(handle, F_SETFL, flags | O_NONBLOCK) == -1)
    {
        return false;
    }
    const auto restore_blocking = [&]() { ::fcntl(handle, F_SETFL, flags); };

    if (::connect(handle, addr, addrlen) == 0)
    {
        restore_blocking();
        return true;
    }
    if (errno != EINPROGRESS)
    {
        const int connect_errno = errno;
        restore_blocking();
        errno = connect_errno;
        return false;
    }

    pollfd pfd{};
    pfd.fd = handle;
    pfd.events = POLLOUT;
    const int poll_timeout_ms =
        timeout_ms > static_cast<int64_t>(INT_MAX) ? INT_MAX : static_cast<int>(timeout_ms);
    const int poll_rc = ::poll(&pfd, 1, poll_timeout_ms);
    if (poll_rc == 0)
    {
        restore_blocking();
        errno = ETIMEDOUT;
        return false;
    }
    if (poll_rc < 0)
    {
        const int poll_errno = errno;
        restore_blocking();
        errno = poll_errno;
        return false;
    }

    int so_error = 0;
    socklen_t so_error_len = sizeof(so_error);
    if (::getsockopt(handle, SOL_SOCKET, SO_ERROR, &so_error, &so_error_len) != 0)
    {
        const int getsockopt_errno = errno;
        restore_blocking();
        errno = getsockopt_errno;
        return false;
    }
    restore_blocking();
    if (so_error != 0)
    {
        errno = so_error;
        return false;
    }
    return true;
}

void platform_socket_enable_reuse_address(SocketHandle handle)
{
    int reuse = 1;
    ::setsockopt(handle, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
}

void platform_socket_enable_nosigpipe(SocketHandle handle)
{
    (void)handle;
#ifndef MSG_NOSIGNAL
#ifdef SO_NOSIGPIPE
    int enable = 1;
    ::setsockopt(handle, SOL_SOCKET, SO_NOSIGPIPE, &enable, sizeof(enable));
#endif
#endif
}

bool platform_socket_enable_broadcast(SocketHandle handle)
{
    int enabled = 1;
    return ::setsockopt(handle, SOL_SOCKET, SO_BROADCAST, &enabled, sizeof(enabled)) == 0;
}

void platform_socket_shutdown(SocketHandle handle)
{
    if (socket_handle_valid(handle))
    {
        ::shutdown(handle, SHUT_RDWR);
    }
}

} // namespace lumiere
