# Serving-tier port -- portable, async, and fit for a service

**Status: design settled, stage 0 done.** Written 2026-09-05
(session 28) from the question "why is this platform specific, is this
server run standalone, and re-evaluate boost beast". The decision at
the end of section 5 is settled enough to build against; section 5.4
(same day, session 29) picks the coroutine style; section 7 has the
stage-0 test built and green; section 8 holds the questions still
open.

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

**There was no socket-level test** until stage 0.
`tests/src/Gui/SceneDump.cpp:2284` says it outright: "the server's
work queue end to end, **no sockets**". The older tests drive the blob
and level machinery directly; `PublishOnly.cpp:193` asserts the server
is running but never speaks to it over a socket. Nothing exercised the
handshake, the framing or the door over a wire. Section 7, stage 0, is
what now does.

## 5. The decision

**Boost.Beast, asynchronous, stackless `asio::coroutine` (not C++20
coroutines -- section 5.4).**

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

### 5.4 Stackless `asio::coroutine`, not C++20 coroutines

The async bodies are written with `boost::asio::coroutine` and the
`reenter` / `yield` macros from `boost/asio/yield.hpp`. This is the
Duff's-device coroutine (Tatham's "Coroutines in C"; the same trick
the JUCE forum thread "Coroutines in C++, simple but nice discovery"
rediscovers), which Asio has shipped as a class since Boost 1.54. The
coroutine is one `int`: `reenter(c)` opens a switch on it, `yield`
stores `__LINE__` and returns, and the next invocation jumps to
`case __LINE__`. No stack, no heap frame, no compiler support beyond
C++98.

The prior art read for this is the `vsk-gateway` proxy
(`~/works/veno/vsk-gateway`, `src/regsrv/httpclient.hpp`,
`src/parser/ftp.cpp`, `src/regsrv/idenbase.hpp`), a production Asio
server built entirely on the idiom. Four mechanisms recur there, and
the port uses all four:

1. **The completion handler is the coroutine.** A small copyable
   struct derives from `coroutine`, holds a pointer to the real
   context, and its call operator re-enters the body. Every
   `yield async_op(..., self)` passes a copy of itself as the
   handler, so Asio carries the resume point along for free.
2. **All state lives in the context object.** Nothing local survives a
   yield -- the switch jumps over its initialisation. Locals are fine
   inside a block between two yields.
3. **Two loops on one connection.** vsk-gateway splits them with the
   `fork` macro; we write two handler structs instead (section 6.2),
   because it reads better and because `fork` as a macro shadows POSIX
   `fork` for the rest of the translation unit.
4. **Resume from a foreign event.** Its timer stashes the coroutine
   int and later constructs a fresh handler from the saved state and
   invokes it. Any thread can wake a parked coroutine by posting a
   handler that carries its state. This is how the frame push works
   (section 6.6).

Why this over `asio::awaitable` / `co_spawn`:

- It settles open question 2: the file builds as C++17 or C++20 on
  every compiler, emscripten included should the transport ever move
  to the browser side.
- Cost per connection is a Beast stream, two ints and a queue, with
  no frame allocation per operation. `awaitable` allocates a frame
  per coroutine and, with `use_awaitable`, typically per `co_await`.
- Debugging is plain: the state is an int readable in gdb, and every
  resume is an ordinary handler invocation on the strand, not a
  coroutine frame the debugger has to reconstruct.

The price is discipline, not machinery: no locals across a yield, no
two yields on one source line, exceptions unwind straight out of the
switch, and the macros are scoped to the bodies -- `yield.hpp` before,
`unyield.hpp` after, exactly as vsk-gateway does it.

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

The per-connection loop today (`SceneServer.cpp:2399`) is already a
hand-rolled state machine: poll, kick check, read and consume frames,
serialize, send body, drain texts, send the pending binary, ping. It
becomes **two coroutines per connection on one strand**, because Beast
allows one read and one write in flight concurrently but never two of
the same:

- **Reader**: a loop of `yield ws.async_read(buf, self)`, then dispatch
  the message. Beast enforces the control-frame rules the audit found
  missing (section 4, item 5).
- **Writer**: drains an explicit queue with one
  `yield ws.async_write(front, self)` in flight, then *parks* itself
  when the queue is empty -- it simply returns, leaving its coroutine
  state where the next wake finds it. Publish, kick, the reload push
  and the traced-frame push all post onto the strand, append to the
  queue, and re-invoke the parked writer (section 6.6).

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

In the coroutine idiom this has the same shape as a socket operation:
`yield post(workerPool, ...)` where the worker, once done, posts the
handler (carrying the saved coroutine state) back onto the strand.

### 6.6 The parked writer is the frame push

The writer coroutine parks by returning. Its coroutine int still holds
the resume line, and the connection object holds the coroutine. To
wake it, any thread does

    post(conn->strand, [conn]{ conn->writer(); });

after appending to the queue under the strand. That is vsk-gateway's
timer trick from section 5.4 item 4, and it is the whole of stage 3:
`sendBinary` from the Cycles thread becomes "append, wake". "Keep only
the newest traced frame" is then a stated rule applied to that queue
at append time, replacing the accidental single-slot `pendingBinary`
of section 6.4.

## 7. Staged plan

Each stage lands alone and is judged by the stage-0 test.

- **Stage 0 -- the socket-level test. DONE 2026-09-05**, as
  `tests/src/Gui/SceneServerWire.cpp` (`SceneServerWire_tests_run`,
  12 cases, in ctest). It starts the listener on a free port and
  drives it through the HTTP routes (`/scene` with and without a held
  version and session, `/blob`, the `/blobs` batch framing, 404s), the
  WebSocket handshake, hello and snapshot, a held version costing no
  payload, resync, a control op with its reply and the NoHandler
  refusal, view-only carried on the request, text and binary pushes
  from a foreign thread arriving in order, a fragmented pick and a
  fragmented op, a ping answered with its payload, the door (403 on
  HTTP, an unauthorized socket answering nothing, BadToken, the token
  in the hello, the token on the upgrade), a kick, a named document
  join, a failed switch, the re-home on unserve, and stop plus
  restart. The client is **Boost.Beast**, so the handshake is judged
  by an implementation that is not ours, and the same client will
  speak to the ported server unchanged. Two wire facts the test
  recorded that the design above had not stated: a view-only change
  is announced to the client as `{"cmd":"config","viewOnly":...}`
  before it takes effect on its requests; and a door refusal rides the
  kick path, so the wire is the reason (`BadToken` / `Refused`), then
  the `Kicked` farewell, then the close. The port keeps both. One
  client-side lesson: Beast treats a cancelled read as the end of the
  stream, so the "nothing arrives" probe leaves its read pending and
  the next read picks it up.
- **Stage 1 -- the seam.** Put an acceptor/connection abstraction
  between the protocol logic and the socket calls, still POSIX
  underneath. No behaviour change.
- **Stage 2 -- Beast underneath it, async.** Delete `Sha1`, the base64
  helper, the handshake, `consumeFrames`, `sendFrame`, the HTTP head
  parser, the `#ifndef _WIN32` and the stub. Three platforms build the
  real server.
- **Stage 3 -- the frame push.** The Cycles latency item, which by then
  is "append to the writer's queue and wake it" (section 6.6) rather
  than a fix.
- **Stage 4 -- the cloud-readiness items** from section 4: IPv6 listen,
  read deadlines, the connection cap keyed on the judged address,
  control-frame rules, and a decision on chunked bodies.
- **Stage 5 -- verify on all three platforms.**

## 8. Open questions for next session

1. **Where do Windows and macOS get tested?** No Windows or macOS box is
   available here, and no CI in this repo builds them. Stages 2 and 5
   are unverifiable without one.
2. ~~C++17.~~ Answered by section 5.4: the stackless idiom builds under
   either standard.
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
- Beast's server-side API shapes quoted here (`ws.accept(req)`,
  `tcp_stream::expires_after`) are design intent from the library's
  documented model, not code that has been compiled in this tree. The
  client side (`websocket::stream::handshake`, `async_read`,
  `write_some` fragments, `ping`, `http::read/write`) has: the stage-0
  test compiles and runs it against Boost 1.90.
- The `asio::coroutine` headers (`boost/asio/coroutine.hpp`,
  `yield.hpp`, `unyield.hpp`) were confirmed present in
  `.conda/freecad/include`; nothing in this tree includes them yet.
