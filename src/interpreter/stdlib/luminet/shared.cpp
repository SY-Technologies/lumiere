#include "../luminet_shared.hpp"

#include <unistd.h>

namespace lumiere
{

TcpConnectionState::~TcpConnectionState()
{
    if (fd >= 0)
    {
        ::close(fd);
    }
}

TcpServerState::~TcpServerState()
{
    if (fd >= 0)
    {
        ::close(fd);
    }
}

UdpSocketState::~UdpSocketState()
{
    if (fd >= 0)
    {
        ::close(fd);
    }
}

HttpServerState::~HttpServerState()
{
    if (fd >= 0)
    {
        ::close(fd);
    }
}

CanalClientState::~CanalClientState()
{
    if (fd >= 0)
    {
        ::close(fd);
    }
}

CanalServerState::~CanalServerState()
{
    if (fd >= 0)
    {
        ::close(fd);
    }
}

} // namespace lumiere
