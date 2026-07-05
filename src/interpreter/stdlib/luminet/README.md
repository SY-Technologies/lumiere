# LumiNet Internal Layout

This folder holds the internal implementation details behind the public
`LumiNet` stdlib module.

File responsibilities:

- `foundation.cpp`: shared low-level helpers such as object setup, text
  normalization, hashing, IP parsing, and URL/address parsing.
- `shared.cpp`: out-of-line state destructors for native socket-backed
  objects.
- `address_dns.cpp`: `LumiNet.Adresse` and `LumiNet.DNS` module builders.
- `tcp_udp.cpp`: `LumiNet.TCP` and `LumiNet.UDP` module builders.
- `http_canal.cpp`: `LumiNet.HTTP` and `LumiNet.Canal` module builders.
- `protocol.cpp`: transport/protocol helpers for HTTP and WebSocket framing,
  parsing, and byte-oriented IO.
- `http_values.cpp`: request/response value objects and HTTP response writer
  helpers.
- `canal_runtime.cpp`: websocket client/server runtime behavior.
- `tcp_runtime.cpp`: TCP connection and TCP server runtime behavior.
- `http_server.cpp`: HTTP server runtime behavior and middleware routing.
- `udp_runtime.cpp`: UDP socket runtime behavior.

The top-level `../luminet.cpp` file should stay thin and only assemble the
public `LumiNet` module from these internal pieces.
