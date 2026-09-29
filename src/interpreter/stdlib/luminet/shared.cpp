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

// Each state below hands the collector the Values it keeps alive, and drops
// them on request. Only the Values: the socket stays with the destructor, so a
// state the collector breaks still closes its descriptor when it is freed.

void TcpServerState::trace_references(RefVisitor &visitor) const
{
    trace_value(on_connection, visitor);
}

void TcpServerState::clear_references() { on_connection = Value::rien(); }

void HttpServerState::trace_references(RefVisitor &visitor) const
{
    for (const Value &handler : middleware)
    {
        trace_value(handler, visitor);
    }
    for (const HttpRoute &route : routes)
    {
        trace_value(route.handler, visitor);
    }
    for (const auto &[pattern, handler] : canal_routes)
    {
        trace_value(handler, visitor);
    }
}

void HttpServerState::clear_references()
{
    middleware.clear();
    routes.clear();
    canal_routes.clear();
}

void CanalClientState::trace_references(RefVisitor &visitor) const
{
    trace_value(on_open, visitor);
    trace_value(on_message, visitor);
    trace_value(on_close, visitor);
    trace_value(on_error, visitor);
}

void CanalClientState::clear_references()
{
    on_open = Value::rien();
    on_message = Value::rien();
    on_close = Value::rien();
    on_error = Value::rien();
}

void CanalServerState::trace_references(RefVisitor &visitor) const
{
    trace_value(on_connection, visitor);
    trace_value(on_message, visitor);
    trace_value(on_disconnect, visitor);
    trace_value(on_error, visitor);
}

void CanalServerState::clear_references()
{
    on_connection = Value::rien();
    on_message = Value::rien();
    on_disconnect = Value::rien();
    on_error = Value::rien();
}

} // namespace lumiere
