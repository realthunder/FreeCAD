# Serving-tier port -- portable, async, and fit for a service

**Status: design, nothing implemented.** Written 2026-09-05 (session 28)
from the question "why is this platform specific, is this server run
standalone, and re-evaluate boost beast". The decision at the end of
section 5 is settled enough to build against; sections 7 and 8 are the
work and the questions still open. Design resumes next session.

## 1. Why this document

The scene-serving tier (`src/Gui/Renderer/SceneServer.cpp`,
`Render::SceneStreamServer`) is the backend half of the browser/thin-client
story: scene snapshots and deltas, the blob and level fetches, the
thin-client control channel, the sharing door, and the transport served
Cycles frames ride out on. It is also POSIX-only, and on Windows it is a
stub that returns false.

That was noticed while starting a small latency fix (the connection
loop's 200 ms poll delays a finished path-traced frame by up to a tick).
The fix is real and still wanted, but it sits on a transport that cannot
run on two of the three platforms this project targets, and that the
roadmap intends to put on the public internet. Fixing the tick first
would be building on that.

## 2. What the server is today

### 2.1 Where it runs

**Not standalone.** It runs in-process inside `libFreeCADGui`, started on
demand by `Gui.serveDocument` (`SceneServeSource.cpp:886`, `:908`) and by
TechDraw's page serving (`Mod/TechDraw/Gui/PageServe.cpp:192`). One
detached accept thread, one detached `std::thread` per connection
(`SceneServer.cpp:1848`), plus the LOD builder threads. There is no
server binary and no separate process.

This matters for the transport choice, because
`docs/ComputeBoundaries.md` section 1 (boundary A) wants a headless
document server in a process of its own later. A transport that assumes
a GUI process argues against that direction.

### 2.2 Where it came from

The origin commit is `7fbc82749b`, "Gui: live scene streaming into the
WASM viewer", which describes it as "a **minimal** HTTP server
(SceneServer.h, POSIX sockets) in the desktop bridge" whose job was to
answer `GET /scene?v=<seen>` twice a second for the WASM viewer.

So POSIX-only was never a decision with a rationale behind it. It was a
bring-up expedient that was never revisited while the thing grew into
WebSocket upgrade, multi-document groups, the access door (tokens,
grants, forwarded-for, asserted identity), roster/kick/sharing, TechDraw
page serve and the Cycles frame stream. The `#else` at line 2959 is the
tell -- `bool start(int) { return false; }` and an empty
`stopListening()` make Windows *compile*, not work.

## 3. What the roadmap commits it to

`docs/MultiDocServe.md` section 7 states the cloud shape: a **gateway**
terminates TLS and auth, maps user to backend, and "proxies the scene
WebSocket unchanged -- the wire in section 4 needs nothing from it"; a
**session manager** spawns per-session backends and bin-packs them by
memory; hibernation is a save to `.FCStd`. Section 7.1 argues the
economics: idle backend near 0% CPU, no rendering tier, density
RAM-bound. `docs/RoadMap.md` workstream 4 adopts the Onshape model
explicitly -- client is a thin renderer of streamed tessellation, the
kernel lives in the geometry service.

Two consequences for this file:

- **TLS is not the backend's job.** The gateway terminates it. That
  removes a whole category of work from this port.
- **But the backend's peer becomes a real proxy** -- nginx, envoy,
  Cloudflare -- rather than our own viewer. A proxy exercises keep-alive,
  chunked bodies, header folding, pipelining and HEAD that our viewer
  never sends. "It works with our viewer" stops being evidence.

## 4. Audit: what a real deployment hits

Verified by reading, 2026-09-05. These are the findings that turned the
library question from taste into evidence.

1. **IPv4 only.** `AF_INET`, `sockaddr_in`, `inet_ntop(AF_INET, ...)`;
   no `AF_INET6` anywhere in the file. An IPv6 gateway, or an IPv6-only
   host, cannot reach it.
2. **No receive timeout.** There is a 20 s `SO_SNDTIMEO` (line 1807) but
   no `SO_RCVTIMEO`, and the request-head loop (line 1868) has no
   deadline -- it reads until `\r\n\r\n` or 16 KB. A client dribbling a
   byte at a time parks a detached OS thread for as long as it likes.
3. **The per-IP cap breaks behind a gateway.** `kMaxConnsPerIp = 16`
   (line 1186) is keyed on the *socket peer*, which behind a proxy is
   the gateway for every user. It has not bitten only because a same-box
   `cloudflared` tunnel arrives on loopback, which is exempt. A gateway
   on another host caps the whole service at 16 connections. This one is
   policy, not transport: it needs fixing whichever library wins, by
   keying the cap on the judged address the door already computes.
4. **POST reads `Content-Length` only.** No chunked `Transfer-Encoding`,
   and when both headers are present it uses CL and ignores TE, which is
   the classic request-smuggling shape. Low risk while nothing reuses
   the connection, but it is the wrong default in front of a proxy.
5. **WebSocket control frames are not held to their rules.** RFC 6455
   caps control frames at 125 bytes and forbids fragmenting them;
   `consumeFrames` (line 2544) applies only the general 64 MB cap, so a
   64 MB "ping" is buffered and echoed back as a pong.

What the audit also found, in fairness: the WebSocket frame parser is
otherwise careful -- masking is required, the length cap is enforced
before allocation, fragmentation is reassembled with its own bound, and
close/ping are handled. The HTTP head is capped at 16 KB. This is not
bad code. It is a minimal server being asked to become a service.

**There is no socket-level test.** `tests/src/Gui/SceneDump.cpp:2284`
says it outright: "the server's work queue end to end, **no sockets**".
The existing tests drive the blob and level machinery directly;
`PublishOnly.cpp:193` asserts the server is running but never speaks to
it over a socket. Nothing exercises the handshake, the framing or the
door over a wire.

## 5. The decision

**Boost.Beast, asynchronous, C++20 coroutines.**

Availability is already there: boost 1.90 in `.conda/freecad`, with
`boost/beast.hpp`, `boost/asio/awaitable.hpp` and
`boost/asio/co_spawn.hpp` present; `src/Gui` already links Boost
(`src/Gui/CMakeLists.txt:44`, `:82`); `BUILD_ENABLE_CXX_STD` defaults to
`C++20` (`cMake/FreeCAD_Helpers/CompilerChecksAndSetups.cmake:33`).

### 5.1 Why not bare Asio (transport only, keep our parsers)

This was the first recommendation, and it was wrong. It optimized for
"do not disturb tested code" -- the right instinct for a debug utility,
the wrong one for a product surface. Treated as a service, the
hand-rolled HTTP and WebSocket layer is not an asset to protect but
roughly 500 lines of security-relevant code that we own, do not fuzz,
and now know has the five defects in section 4. Beast is a maintained,
widely deployed HTTP/1.1 and RFC 6455 implementation; adopting it
deletes that code rather than hardening it.

### 5.2 Why not Qt (QTcpServer + QWebSocketServer)

Qt is already a dependency of `Gui`, and `Qt6WebSockets` exists in the
env -- but it is **not currently linked**, so adopting it is a new build
dependency for every packager, including the feedstocks this project
maintains. It needs a Qt event loop per server thread. It ties the
serving transport to a GUI framework at the moment the roadmap wants a
headless server process. And `QWebSocketServer` does not hand over the
raw upgrade request that the door inspects.

### 5.3 The objection that did not survive, recorded

The main technical argument against a WebSocket library was that it
would hide the raw upgrade request the door reads (headers,
`X-Forwarded-For`, the asserted identity, the `?v=&s=` query). **That is
true of Qt and false of Beast.** Beast parses into a
`http::request` object that the application holds and inspects, and the
application passes it to `ws.accept(req)` itself. The door logic keeps
reading exactly what it reads today.

The secondary objection -- that async is a risky rewrite -- was also
overstated, because Beast has a synchronous API that maps onto the
current loop almost 1:1. We are choosing async anyway, for the reasons
in section 6, but it is a choice and not a forced cost.

## 6. Shape of the ported server

### 6.1 Async deletes the Cycles latency problem

In a fully async server there is no 200 ms poll. A finished traced frame
does not wait for a tick and does not need a waker: it posts a write on
the connection's strand and goes out. The latency this port started from
ceases to exist as a concept rather than being reduced.

### 6.2 A strand per connection preserves the invariant already documented

The current code is explicit that `conn.group` and `conn.sent` are
"touched only by this connection's own loop thread". An `asio::strand`
gives exactly that serialization without owning a thread. That
correspondence is what makes this a port rather than a rewrite of the
concurrency model, and it is the property to hold on to while writing
it.

### 6.3 Thread-per-connection goes away

Today: one detached thread per connection plus an accept thread, up to
`kMaxConns = 128`. That becomes an `io_context` with a small pool --
plausibly a single io thread per backend, since process-per-session
means each backend serves one room's viewers. At default 8 MB stacks,
128 potential threads per backend across many backends per box is real
memory, and section 7.1 of MultiDocServe rests on density being bounded
by the model rather than the runtime.

### 6.4 A write queue replaces the blocking send

`sendAll` can block up to the 20 s `SO_SNDTIMEO` on the connection's
thread, and `pendingBinary` holds only the newest frame -- frame
dropping by accident of the data structure. Async gives one outstanding
`async_write` plus an explicit queue, so backpressure is visible and
"keep only the newest traced frame" becomes a stated policy on that
queue.

### 6.5 THE HAZARD: blocking work must leave the io_context

`payloadFor()` serializes a snapshot under the big scene `mutex` on the
connection's thread today. On a shared io thread that stalls every other
connection in the process. Snapshot serialization, and any file I/O in
`serveViewerFile`, must be posted to a worker pool with only the
completion coming back on the strand. Get this wrong and one publishing
document janks every viewer in the backend. This is the constraint the
port must be built around from the start, not retrofitted.

## 7. Staged plan

Each stage lands alone and is judged by the stage-0 test.

- **Stage 0 -- the socket-level test that does not exist.** Start the
  server on a port and drive a raw client through handshake, snapshot,
  a control op, a binary push, fragmentation, the door refusing a bad
  token, and kick. This is the oracle for every later stage and is worth
  having whatever else happens.
- **Stage 1 -- the seam.** Put an acceptor/connection abstraction
  between the protocol logic and the socket calls, still POSIX
  underneath. No behaviour change.
- **Stage 2 -- Beast underneath it, async.** Delete `Sha1`, the base64
  helper, the handshake, `consumeFrames`, `sendFrame`, the HTTP head
  parser, the `#ifndef _WIN32` and the stub. Three platforms build the
  real server.
- **Stage 3 -- the frame push.** The Cycles latency item, which by then
  is a `post` to a strand rather than a fix.
- **Stage 4 -- the cloud-readiness items** from section 4: IPv6 listen,
  read deadlines, the connection cap keyed on the judged address,
  control-frame rules, and a decision on chunked bodies.
- **Stage 5 -- verify on all three platforms.**

## 8. Open questions for next session

1. **Where do Windows and macOS get tested?** No Windows or macOS box is
   available here, and no CI in this repo builds them. Stages 2 and 5
   are unverifiable without one.
2. **C++17.** C++20 is the default and nothing in `src/` currently
   forces it. Coroutines would make this TU require C++20. Accept that,
   or stay callback-based to keep C++17 building?
3. **How many io threads**, and does the worker pool for serialization
   belong to the server or to the existing LOD builder pool?
4. **permessage-deflate**: worth measuring on the cloud wire. Control
   JSON compresses well; mesh blobs are content-addressed and may
   already be compressed.
5. **Keep-alive and HTTP/2**: the current server closes after every
   request. Behind a gateway that is inefficient but correct. Does the
   gateway design want keep-alive from the backend?
6. Does the port change the wire at all? The intent is no. The stage-0
   test is what will prove it.

## 9. What was not verified

- macOS behaviour of `MSG_NOSIGNAL` (line 1734) is asserted from
  knowledge, not measured -- Darwin does not define it and wants
  `SO_NOSIGPIPE` instead, and there is no shim anywhere in `src/`. No
  Mac was available to compile on.
- Beast's exact API shapes quoted here (`ws.accept(req)`,
  `tcp_stream::expires_after`) are design intent from the library's
  documented model, not code that has been compiled in this tree.
