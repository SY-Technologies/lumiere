#include "../luminet_shared.hpp"

namespace lumiere
{

TcpConnectionState::~TcpConnectionState()
{
    close_socket_fd(fd);
}

TcpServerState::~TcpServerState()
{
    close_socket_fd(fd);
}

UdpSocketState::~UdpSocketState()
{
    close_socket_fd(fd);
}

HttpServerState::~HttpServerState()
{
    close_socket_fd(fd);
}

CanalClientState::~CanalClientState()
{
    close_socket_fd(fd);
}

CanalServerState::~CanalServerState()
{
    close_socket_fd(fd);
}

} // namespace lumiere
