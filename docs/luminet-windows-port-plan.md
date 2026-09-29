# LumiNet Windows Port Plan

This document lays out a clean, robust plan for making `LumiNet` truly work on macOS, Linux, and Windows.

The goal is not just "make Windows compile". The goal is:

- build `LumiNet` on all three platforms
- run `LumiNet` correctly on all three platforms
- make the existing `LumiNet` test suite pass on all three platforms
- keep one public LumiNet API instead of letting platform differences leak into user code

## Goal

Make LumiNet a genuinely cross-platform networking standard library with equivalent behavior on:

- Linux
- macOS
- Windows

Success means:

- Windows no longer disables `LumiNet`
- Windows runs the real LumiNet implementation
- Windows passes the LumiNet tests
- CI and release artifacts for all three platforms honestly represent the same feature set

## Guiding Principles

- Keep the public LumiNet API identical across platforms.
- Move platform-specific socket logic behind a narrow internal boundary.
- Keep HTTP, DNS, TCP, UDP, and Canal logic shared as much as possible.
- Let CI tell the truth at every stage.
- Prefer small safe milestones over one giant portability rewrite.

## Current Problem

LumiNet currently mixes:

- feature logic
- protocol logic
- operating-system socket logic

That is what makes Windows support hard.

Today the code depends directly on Unix/POSIX things like:

- `netdb.h`
- `sys/socket.h`
- `arpa/inet.h`
- `unistd.h`
- `errno`
- `EINTR`
- `close`
- `MSG_NOSIGNAL`

These assumptions are spread across:

- DNS/address resolution
- TCP
- UDP
- HTTP client
- HTTP server
- Canal/WebSocket
- shared socket helpers

So the core issue is not one missing include. The core issue is the lack of a platform abstraction boundary.

## Scope

This port covers:

- `LumiNet.Adresse`
- `LumiNet.DNS`
- `LumiNet.TCP`
- `LumiNet.UDP`
- `LumiNet.HTTP`
- `LumiNet.Canal`

This plan does not try to redesign the public API. It is a portability effort, not a feature redesign.

## Phase 0: Stabilize The Ground

Before touching the platform layer:

- treat the current Linux/macOS LumiNet behavior as the reference
- avoid changing the public LumiNet API during the port
- document the intended platform boundary before implementation begins
- define the minimum Windows target as Winsock 2 with IPv4 and IPv6 support

Deliverable:

- a short internal design note describing the platform boundary and the implementation order

## Phase 1: Create A Platform Boundary

Introduce a small internal platform layer, for example:

- `luminet_platform.hpp`
- `luminet_platform_posix.cpp`
- `luminet_platform_windows.cpp`

This layer should own:

- socket handle type
- invalid socket value
- startup and shutdown requirements
- close behavior
- last-error retrieval
- error-to-text translation
- send/recv/sendto/recvfrom wrappers
- timeout application
- `SO_REUSEADDR` handling
- retry behavior for interrupted operations
- any OS-specific name/address lookup wrappers if needed

Deliverable:

- LumiNet feature files stop including OS socket headers directly unless there is a strong reason

## Phase 2: Normalize Core Socket Lifetimes

Replace raw Unix assumptions about socket handles.

Update all LumiNet-owned state structs so their lifecycle is platform-safe:

- `TcpConnectionState`
- `TcpServerState`
- `UdpSocketState`
- `HttpServerState`
- `CanalClientState`
- `CanalServerState`
- `HttpResponseWriterState`

Requirements:

- all destruction goes through one close helper
- Windows startup initialization for Winsock is handled safely
- initialization happens once
- shutdown is safe and deterministic

Main risk:

- accidental double-close
- invalid-handle assumptions copied from Unix into Windows paths

Deliverable:

- LumiNet compiles on Windows without stubbing the whole module out

## Phase 3: Port Shared Foundation Helpers

Move these helpers behind the platform layer:

- `socket_error_text`
- `close_socket_fd`
- `socket_send_bytes`
- `socket_sendto_bytes`
- `socket_recv_bytes`
- `socket_recvfrom_bytes`
- `apply_timeout`
- address-to-text helpers
- port extraction helpers

Remove direct dependence on:

- `errno`
- `EINTR`
- `MSG_NOSIGNAL`
- `close`

Main risk:

- timeout semantics are not identical between Winsock and POSIX

Deliverable:

- `foundation.cpp` and `protocol.cpp` work through the platform abstraction

## Phase 4: Port DNS And Address Resolution

Port:

- `LumiNet.Adresse`
- `LumiNet.DNS`

Verify:

- `getaddrinfo`
- `getnameinfo`
- localhost detection
- IPv4 parsing
- IPv6 parsing
- reverse lookup

Keep error messages consistent enough that cross-platform tests remain meaningful.

Why this phase comes early:

- it is smaller than the transport layers
- it provides early Windows runtime progress

Deliverable:

- Windows passes the `Adresse` and `DNS` LumiNet tests

## Phase 5: Port TCP And UDP

Port:

- `LumiNet.TCP.connecter`
- `LumiNet.TCP.Serveur`
- `ConnexionTCP`
- `LumiNet.UDP.ouvrir`
- UDP send and receive flows

Verify:

- connect
- bind
- listen
- accept
- read
- write
- close
- local port discovery

Main risk:

- address family handling and timeout behavior may expose hidden Unix assumptions

Deliverable:

- Windows passes TCP and UDP LumiNet tests

## Phase 6: Port HTTP Client And Server

Port:

- HTTP request send path
- HTTP response read path
- HTTP server listen/accept/respond loop
- file response path
- redirect path

Verify:

- content length handling
- header parsing
- partial read behavior
- close behavior
- timeout behavior
- request/response formatting

Main risk:

- HTTP tends to reveal buffering and socket-read edge cases that basic TCP tests do not catch

Deliverable:

- Windows passes HTTP client and HTTP server LumiNet tests

## Phase 7: Port Canal / WebSocket

Port:

- WebSocket handshake
- upgrade validation
- frame read/write
- fragmented frame header handling
- close behavior

Keep protocol logic shared. Only the transport boundary should vary.

Main risk:

- this is the most timing-sensitive and subtle part of the port

Deliverable:

- Windows passes all Canal/WebSocket LumiNet tests

## Phase 8: Rebuild The Test Strategy

Once Windows support is real:

- remove Windows-specific `GTEST_SKIP()` guards for LumiNet
- keep platform-neutral tests shared
- only allow platform-specific assertions where behavior must legitimately differ
- normalize machine-dependent values in tests

Test audit targets:

- absolute paths
- path separators
- newline assumptions
- exact socket error wording
- timing assumptions

Deliverable:

- one LumiNet test suite running on Linux, macOS, and Windows

## Phase 9: Tighten CI And Release

After the port is complete:

- remove `-DLUMIERE_ENABLE_LUMINET=OFF` from Windows CI
- remove `-DLUMIERE_ENABLE_LUMINET=OFF` from Windows release builds
- run the full LumiNet suite on Windows
- watch for flaky tests and stabilize them before declaring the port done

Deliverable:

- green CI on Linux, macOS, and Windows
- release artifacts for all three platforms with the same LumiNet feature set

## Recommended Implementation Order

1. Platform layer skeleton
2. Socket handle lifecycle
3. DNS/address
4. TCP/UDP
5. HTTP
6. Canal/WebSocket
7. Test unskip and CI restoration

This order gives early progress while keeping the hardest debugging for later phases.

## Estimated Work

Rough estimate:

- 1 to 2 days to get LumiNet compiling on Windows
- 3 to 6 more days to make the full LumiNet tests pass reliably
- about 1 to 2 weeks total for a robust result

This is based on the current LumiNet footprint being about 3.7k lines of implementation plus shared state/helpers.

## Main Risks

- timeout semantics differ between Winsock and POSIX
- network error text differs enough to make tests brittle
- partial reads and writes behave differently under Windows CI
- WebSocket framing tests may expose race or timing issues
- platform assumptions may remain spread across feature files if boundary discipline slips

## Success Criteria

The port is complete only when all of the following are true:

- LumiNet builds on Windows with no stubbing
- there are no Windows-specific `GTEST_SKIP()` guards for LumiNet tests
- existing LumiNet tests pass on Linux, macOS, and Windows
- CI and release workflows include all three platforms honestly
- platform-specific code is isolated to a small internal layer rather than spread throughout feature code

## Non-Goals

This effort does not aim to:

- redesign the public LumiNet API
- add unrelated new networking features
- change higher-level language syntax
- weaken tests just to make Windows appear green

The goal is true portability, not a cosmetic pass.
