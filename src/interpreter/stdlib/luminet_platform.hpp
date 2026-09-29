#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <BaseTsd.h>
using ssize_t = SSIZE_T;
#else
#include <arpa/inet.h>
#include <cerrno>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#endif

namespace lumiere
{

using SocketHandle = decltype(::socket(AF_INET, SOCK_STREAM, 0));
using SocketSize = ssize_t;

#ifdef _WIN32
constexpr SocketHandle kInvalidSocketHandle = INVALID_SOCKET;
#else
constexpr SocketHandle kInvalidSocketHandle = -1;
#endif

bool socket_handle_valid(SocketHandle handle);
void initialize_socket_platform();
int socket_last_error_code();
bool socket_error_is_interrupted(int error_code);
std::string socket_error_message_from_code(int error_code);
std::string socket_last_error_message();
void close_socket_handle(SocketHandle &handle);
SocketSize platform_socket_send(SocketHandle handle, const void *data, std::size_t size, int flags);
SocketSize platform_socket_sendto(SocketHandle handle,
                                  const void *data,
                                  std::size_t size,
                                  int flags,
                                  const sockaddr *addr,
                                  socklen_t addrlen);
SocketSize platform_socket_recv(SocketHandle handle, void *buffer, std::size_t size, int flags);
SocketSize platform_socket_recvfrom(SocketHandle handle,
                                    void *buffer,
                                    std::size_t size,
                                    int flags,
                                    sockaddr *addr,
                                    socklen_t *addrlen);
bool platform_socket_set_timeout(SocketHandle handle, int64_t timeout_ms);
// Connects handle to addr, bounded by timeout_ms: unlike SO_RCVTIMEO/SO_SNDTIMEO
// (which platform_socket_set_timeout sets and which do not bound connect()),
// this drives the handshake itself through a non-blocking connect + poll/select,
// leaving the socket back in blocking mode before returning either way. On
// failure or timeout it returns false with the OS error state (errno /
// WSAGetLastError) set so socket_last_error_message() reports the right cause.
bool platform_socket_connect_with_timeout(SocketHandle handle,
                                          const sockaddr *addr,
                                          socklen_t addrlen,
                                          int64_t timeout_ms);
void platform_socket_enable_reuse_address(SocketHandle handle);
void platform_socket_enable_nosigpipe(SocketHandle handle);
bool platform_socket_enable_broadcast(SocketHandle handle);
void platform_socket_shutdown(SocketHandle handle);

} // namespace lumiere
