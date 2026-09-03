# Network for the Python sandbox: the permission model

Status as of **2026-09-03**: design.  Of the roadmap in section 10 only
N0, the demotion of the WASI runtime, is built.

## 0. The direction (2026-09-03)

The user's direction of 2026-09-03 changed the shape of the sandbox
itself and is recorded first because everything below depends on it:

1. **Pyodide is THE sandbox runtime.**  The WASI image under wasmtime
   (`docs/ExpressionImage.md`) is demoted to a *reference
   implementation*: it stays in the tree as the proof that the seam is
   runtime-agnostic and as the smallest possible confinement to compare
   against, but it is built only on request (`BUILD_EXPR_WASI_RUNTIME`,
   default OFF) and receives no new features.  Nothing in this document
   is designed for it.
2. **Network becomes a capability the host can grant**, with the
   firewall-like control a user would expect, instead of the "not
   offered, no op exists to gate" line of `docs/ExpressionSandbox.md`
   sec 3.2.
3. **The end goal is one switch**: a user flips a preference and every
   piece of Python FreeCAD runs -- expressions, document-embedded
   code, macros, the console, Python workbenches -- executes in pyodide
   instead of the in-process interpreter.  Section 11 maps the ladder
   of `ExpressionSandbox.md` sec 8 onto that switch and says where the
   network model sits on it.
4. **A missing package is an offer, not a dead end.**  When guest code
   imports a package the user has not installed, the host intercepts
   the failure and offers to install it into the user's pyodide
   directory (section 9).  Together with the bootstrap direction of
   `docs/PyodideHost.md` (users pick their own packages) this is what
   makes the ecosystem reachable from inside FreeCAD.

The security vocabulary is the one `ExpressionSandbox.md` sec 3 already
defines -- principals, permissions, grants, the host-side enforcement
point -- extended, not replaced.  Read that section first.

## 1. What a guest can reach today (ground truth, 2026-09-03)

Nothing, and for a structural reason, not a policy one:

- Pyodide has **no sockets**.  Emscripten's libc maps the POSIX socket
  calls onto `SOCKFS`, whose transport is the JavaScript `WebSocket`
  object; with no such global the calls fail with `OSError`, which is
  what the phase 0 probes measured (`docs/PyodideHost.md` sec 4).
- Every HTTP client in the pyodide ecosystem bottoms out in a host
  global: `pyodide.http.pyfetch` calls `js.fetch` (asynchronous);
  `pyodide.http.pyxhr` and `open_url` call `js.XMLHttpRequest`
  (synchronous); urllib3 2.x carries a native emscripten backend on
  `fetch` with an `XMLHttpRequest` fallback, so `requests` works with no
  patch; `aiohttp` ships a pyodide wheel; `pyodide-http` is the older
  patch layer for the same two globals.
- `host_shim.js` defines neither `fetch`, nor `XMLHttpRequest`, nor
  `WebSocket`.  The emscripten glue references all three, only inside
  the browser and node branches the shell environment never takes.

So the whole network surface of the guest is **three names the host
chooses to define or not**, and every library goes through them.  That
is the chokepoint this design builds on: the host defines the names,
and the definitions carry the policy.

## 2. Threat model: what network adds

Without network, a hostile document can at worst burn the time budget
or produce wrong values.  With network, three new things become
possible, in order of severity:

- **Exfiltration.**  The guest can already read what its grants let it
  read (the document's own objects, `doc.foreign` when granted).  A
  request carrying that content to a server the attacker controls
  turns a read grant into a leak.  Note this is exactly why the model
  gates by *destination*: a document allowed to reach one named API
  cannot also reach the attacker's host.
- **Reach into the user's network.**  The host machine sits behind a
  firewall; the guest would inherit that position.  Loopback services
  (a local Jupyter, a printer, a database), the LAN, and cloud metadata
  endpoints are the classic server-side request forgery (SSRF) targets,
  and none of them require a public destination.
- **Beaconing and tracking.**  Even a single GET to a fixed host tells
  the author when and where the file was opened.

The principal split of sec 3.1 already tells these cases apart:
document code is untrusted content, session code is the user, addon
code is an installed extension.  The defaults in sec 4.6 follow from
that split and nothing else.

What network does NOT change: memory, filesystem, process and loader
confinement stay what the bare engine gives.  A request is data in and
data out through one host op; there is no new primitive underneath.

## 3. The capability: HTTP through the host

### 3.1 Shape

The guest never opens a connection.  It formulates a request as data
and the host performs it:

    guest (fetch / XMLHttpRequest shim)
      -> host op  net.http.request { principal, method, url, headers, body }
      -> policy   (sec 4): allow, or a structured denial
      -> client   the host's own HTTP client, host TLS, host proxy
      -> reply    { status, headers, body } or { error }

This is the shape every sandbox that solved the problem converged on:
Deno's `--allow-net=host:port`, Node's `--permission --allow-net=host`,
Spin's `allowed_outbound_hosts`, wasmCloud's per-request outgoing
handler.  It is a request-level gate, and it is strictly stronger for
this purpose than a socket-level one: a socket check sees an IP and a
port after DNS and cannot tell two sites behind one CDN apart, cannot
see redirects, cannot see the method.  An HTTP gate sees the hostname,
the scheme, the port, the method, and every redirect hop.  (wasmtime's
`socket_addr_check` is the socket-level design; it exists only for
component-model guests and is not exposed by the C API.  It is not
pursued, and the reference implementation gets no network at all.)

### 3.2 The injected primitives

Two names, both added to the shim only when the runtime is created with
network enabled for its principal (a guest whose principal has no
network grant at all never sees the names -- absence stays the first
line, the policy is the second):

- **`XMLHttpRequest`** in its synchronous form.  On the desktop host a
  synchronous request is the natural one: a host hop already blocks the
  guest, and the host performs the request on the calling thread with
  the budget's deadline as its timeout.  This is what `pyxhr`,
  `open_url`, and urllib3's fallback path need, and it makes
  `requests.get(...)` work in ordinary synchronous Python, which is what
  user code is.
- **`fetch`** returning a Promise, for `pyfetch` and `aiohttp`.  The
  host issues the request off-thread and completes the promise through
  the same task queue that already drives timers and
  `WebAssembly.instantiate` (`PyodideHost.md` sec 9: the message loop
  is pumped by the host).  A `Response` object with `status`,
  `headers`, `arrayBuffer()`, `text()`, `json()` and a body stream is
  enough for both libraries; `AbortController`/`AbortSignal` come with
  it because `pyfetch` constructs them.

Both are thin: parsing arguments into the one request record, calling
the one host op, reshaping the reply.  Neither contains policy.  A
guest that could rewrite them (it can, they are JavaScript in its
context) gains nothing, because the policy runs on the other side of
the host op, in C++ (`ExpressionSandbox.md` sec 3.4: all checks
host-side, a compromised guest still cannot skip them).

`Request`/`Headers`/`Response` constructors, `URL` (already present),
`TextDecoder` (already present), and `btoa`/`atob` (already present)
complete the set urllib3's backend touches.  The shim inventory of
`PyodideHost.md` sec 3 gains one dated block listing exactly these.

### 3.3 The host client

An HTTP client behind one interface (`NetClient`: request in, reply
out, cancellable, with a deadline), so the choice can change without
touching the policy or the shim.  Two candidates exist in the
environment, and the choice is an open question (sec 12):

- **Qt Network** (`QNetworkAccessManager`).  Shipped on every platform
  FreeCAD ships on, system TLS backends, system proxy discovery, and
  what the Addon Manager already uses.  Cost: the App library links
  QtCore only; QtNetwork would be a new link for App, or the client
  lives in a small library of its own.
- **libcurl**.  Present in the conda environment (7 platforms), a
  synchronous API that fits the XHR path directly, and no event loop to
  own.  Cost: certificate-store configuration per platform is on us.

Either way the guest gets none of the client's knobs: no custom TLS,
no certificates, no proxy selection, no source address.  urllib3's own
emscripten documentation lists exactly those as unsupported in the
browser, so nothing in the ecosystem expects them.

## 4. The policy

### 4.1 Grant targets are origin patterns

A network grant names WHERE, never just "network".  The target
grammar, taken from Deno's because users already know it:

    [scheme://]host[:port]

- `host` is a hostname or an IP literal; an IPv6 literal in brackets.
- A hostname matches itself only.  `*.example.com` matches any
  subdomain (one or more labels), not `example.com` itself.  A bare
  `*` is refused as a target: "everything" is spelled by a different
  grant (sec 4.5), so it can never appear by accident in a list.
- `port` omitted means the scheme's default port only (443 for
  https, 80 for http), not "any port".
- `scheme` omitted means `https`.  `http` must be written out.  No
  other scheme exists at this layer; `ws`/`wss` arrive with sec 10's
  WebSocket capability and are separate targets.

Deny entries use the same grammar and always win over allow entries,
whatever their scope.  So "all of `*.example.com` except
`internal.example.com`" is two lines, and a user-level deny of a host
overrides a document-level allow of the same host.

### 4.2 Built-in denials

Applied before any grant is consulted, so a pattern in an allow list
cannot open them by accident.  Each can be opened only by listing the
exact address (not a pattern) in the user-level allow list, which is
the "I know what I am doing" gesture:

- Loopback (`127.0.0.0/8`, `::1`, `localhost` and any name resolving
  to loopback).
- Private ranges (`10/8`, `172.16/12`, `192.168/16`, `fc00::/7`),
  link-local (`169.254/16`, `fe80::/10`), and the cloud metadata
  address `169.254.169.254` specifically.
- The unspecified and multicast ranges.
- Any destination whose resolved address falls in the above even when
  the NAME did not (sec 4.3).

### 4.3 Resolution, redirects, and the holes an allow list leaves

A hostname allow list has two well-known holes, and both are closed on
the host side, where the information exists:

- **DNS rebinding / private resolution.**  The host resolves the name
  itself, then checks the *resolved* addresses against sec 4.2 before
  connecting, and connects to the address it checked (no second
  resolution by the client).  A public name resolving to a private
  address is denied even when the name is granted.
- **Redirects.**  The client is run with automatic redirects OFF.  The
  host follows them itself, re-running the whole policy on every hop
  as if it were a new request from the same principal; a hop to a
  non-granted origin is a denial of the original request, with the
  hop's URL in the reason.  A limit of five hops.  Cross-origin
  redirects strip the request body and any request headers the guest
  set.

### 4.4 Methods, headers, credentials

- Methods: `GET`, `HEAD`, `POST`, `PUT`, `PATCH`, `DELETE`, `OPTIONS`.
  A grant may restrict to a subset (`GET,HEAD` is the natural shape
  for "read a public API"); the default for a document grant is
  read-only, for session and addon grants all of them.
- Request headers: the guest may set any header except the ones the
  host owns (`Host`, `Content-Length`, `Connection`, `Proxy-*`,
  `Cookie`, `Authorization` unless the grant says `credentials`).
  The host adds a fixed `User-Agent` naming FreeCAD and nothing that
  identifies the user or the machine.
- No ambient credentials, ever: no cookie jar shared with anything, no
  system keychain, no proxy credentials visible to the guest.  A
  `credentials` flag on a grant lets the guest send its OWN
  `Authorization` header (an API token the user typed into the
  script); it never lends the host's.
- Response headers: passed through, minus `Set-Cookie`.

### 4.5 The wide grants

Two grants exist that are not origin patterns, so that "everything"
is a deliberate word rather than a wildcard:

- `net.http.any` -- any public origin (sec 4.2 still applies).  The
  default for the session principal (sec 4.6); promptable for addons;
  not offered to documents.
- `net.http.local` -- lifts the loopback denial only, for the "talk
  to my local Jupyter/service" case.  Session and addon principals
  only, always prompted, never persisted past the session.

### 4.6 Defaults per principal

Extending the sec 3.2 table.  Network entries carry a TARGET, so a
grant is `(principal, permission, target)`:

    permission        document      session    addon
    ----------------  ------------  ---------  -----------------
    net.http:<origin> PROMPT (a)    ALLOW      PROMPT, "always"
    net.http.any      DENY          ALLOW      PROMPT
    net.http.local    DENY          PROMPT     PROMPT (b)
    net.ws:<origin>   sec 10        sec 10     sec 10
    net.socket        DENY          sec 10     sec 10

    (a) surfaced through the permissions panel, never a dialog the
        document can open (sec 3.3: fail fast with PermissionNeeded,
        passive indicator, grant on user gesture).  A document grant
        is read-only (GET/HEAD) unless the user widens it.
    (b) never persisted past the session.

The session principal keeps what the user has today: their own console
and macros reach the network, because the user is the operator.  What
changes for them is visibility (the audit log, sec 6) and the ability
to tighten (a user-level deny list applies to every principal).

### 4.7 Limits (quotas, not permissions)

Bound cost, never capability (sec 3.4):

- Per request: response body cap (default 64 MiB), connect and total
  timeouts derived from the remaining budget of the current call, one
  request in flight per guest instance for the synchronous path.
- Per principal per session: request count and byte volume, with a
  passive indicator when a document approaches them.
- A hard termination (`Outcome::Terminated`) cancels every request the
  guest still has in flight; the guest is dropped anyway.

## 5. Where grants live

- In **user preferences**, keyed by principal identity: for documents
  the content hash of sec 3.1, for addons the addon name, for the
  session the user.  Scopes are the sec 3.3 three: once, session,
  always.
- **Never in the document.**  A file cannot carry a grant, a grant
  request, or a hint that pre-fills one.  A document that wants a
  network capability declares it the way it declares anything: by
  trying, failing fast, and leaving a `PermissionNeeded` the panel
  shows.
- Addons may ship a **manifest** of the origins they intend to use
  (the Spin `allowed_outbound_hosts` idea) -- a declaration the
  install-time prompt shows, not a grant.  The grant is still the
  user's click.
- Headless: a policy file plus `--grant net.http:<origin>` switches,
  default DENY with a structured audit line, exactly as sec 3.3 says
  for every other permission.

## 6. Enforcement point and audit

One function, host-side, C++: `NetPolicy::check(principal, request) ->
Verdict`, run by the bridge dispatcher for the `net.http.request` op
before the client is touched and again per redirect hop.  The verdict
is allow, deny-with-reason, or prompt-needed; the guest sees a
structured error for the last two (`PermissionNeeded` / `NetworkDenied`
with the origin and the rule that fired), so a library's exception
carries something a user can act on.

Every verdict is one audit line: time, principal, method, origin,
verdict, rule, bytes.  The permissions panel shows the per-document
view of the same log; the Report view shows it all.  Nothing about a
request is logged beyond the origin and the sizes -- no URLs with
query strings, no bodies.

Tests: a policy table test (pattern grammar, deny-wins, built-in
ranges, resolved-address check with a stubbed resolver), a redirect
test against a local server (allowed hop, denied hop, hop count), and
the parity habit of `PyodideHost.md` sec 9 -- the corpus gate stays
deterministic because expressions never get the names (sec 8).

## 7. Synchronous, asynchronous, and the budget

- The synchronous path is a host hop; the budget clock of
  `PyodideHost.md` sec 10 keeps running.  A request's timeout is the
  smaller of the grant's limit and the remaining soft budget, so a slow
  server produces the ordinary interrupt, not a hang.
- The asynchronous path (`fetch`) needs the guest to yield: a Python
  `await` under pyodide's event loop, which the host drives by pumping
  V8's task queue between the guest's turns.  This exists for boot
  today and needs to become a first-class "run the loop until this
  promise settles or the budget ends" primitive in the runtime.  It is
  the same primitive rung 2 (sec 11) needs for any asynchronous guest
  code, so it is not network-specific work.
- Cancellation: soft interrupt aborts the in-flight request (the
  client's cancel), then delivers `KeyboardInterrupt` as usual.

## 8. Expressions never get the network

The expression sandbox (rung 0) is explicitly excluded: its runtime is
created with no network names.  Three reasons, each sufficient: the
corpus gate and the parity habit rely on determinism; an expression's
whole budget is milliseconds; and a spreadsheet cell fetching the web
on every recompute is not a feature anyone asked for.  Document-level
Python (rung 2 onward) is where network grants apply.

## 9. Missing packages: intercept the import, offer the install

The user's question of 2026-09-03: can the host catch an import of a
package that is not installed and offer to install it?  Yes, and
pyodide already has most of the hook.

### 9.1 What pyodide does today (314.0.6, read from `python_stdlib.zip`)

- The lock file maps **import names to package names** (`imports` per
  package: 304 import names over 356 packages).  `pyodide.js` hands
  that map to `_pyodide._importhook.register_module_not_found_hook`,
  which does NOT install a finder any more: it keeps the map and, when
  a `ModuleNotFoundError` reaches pyodide's error path, **adds a note**
  to the exception ("The module 'requests' is included in the Pyodide
  distribution, but it is not installed.  You can install it by
  calling: await micropip.install(...)").  A hint, not an action.
- `pyodide.code.find_imports(source)` is a static AST scan of the
  top-level imports of a piece of code, and the JavaScript
  `loadPackagesFromImports(code)` uses it to load lock-file packages
  BEFORE running the code.  In the browser the loader fetches from the
  CDN; in our host the loader reads through the scoped reader, so it
  can only load what is already on disk.

So the two moments -- before the run, and at the failing import -- both
have a hook, and neither does anything in our host without the
installer that the bootstrap direction (`docs/PyodideHost.md`) already
calls for.  The design below is that installer wired to those hooks.

### 9.2 Before the run: scan and offer

The host has the source of whatever it is about to execute (a console
line, a macro, a scripted object's code, a document's Python).  Run
`find_imports` over it (in the guest, it is one AST walk; or a
host-side port so a document is scanned before any guest exists),
subtract the stdlib and the installed set, and map the rest through
the lock's import map and the user's manifest.  What is left is an
offer, made BEFORE anything runs:

    This macro imports `requests`, which is not installed in the
    sandbox.  Install requests 2.33.1 and 4 dependencies from the
    pyodide index (410 KB)?          [Install]  [Run anyway]  [Cancel]

For the session principal this is a dialog: the user just asked to run
something, a prompt on that gesture is not a modal mid-recompute.  For
a document principal it is the sec 3.3 flow -- fail fast with a
structured `PackageNeeded{import, package, version, source, size}`,
a passive indicator, the permissions panel lists it, the install
happens on the user's click.  A pre-run scan catches the common case
with no failed run at all.

### 9.3 At the import: a finder of our own

For what the scan cannot see (imports inside functions, `importlib`,
conditional imports), a `sys.meta_path` finder appended LAST, after
pyodide's own, whose `find_spec` calls one host op,
`pkg.missing {import_name}`.  The host answers one of three ways:

- **`unknown`** -- not in the lock, not on PyPI: return `None`, the
  ordinary `ModuleNotFoundError` follows, pyodide's note stays.
- **`offer`** -- known but not installed: the finder raises
  `ModuleNotFoundError` with OUR note ("FreeCAD can install `requests`
  2.33.1 for the sandbox; see the Sandbox panel" -- or, for the
  session principal, the dialog opens right there, and if the user
  accepts, the host installs and answers `installed` so the finder
  retries instead of raising).  The host records the request the same
  way it records a `PermissionNeeded`.
- **`installed`** -- the host has the wheel on disk (just installed,
  or installed earlier but not yet loaded into this guest instance):
  the finder loads it and returns the spec.

Loading a wheel into a RUNNING guest from inside a synchronous import
is the one piece that needs a probe before it is promised:

- A **pure-Python wheel** is a zip; unpacking it into the guest's
  site-packages (emscripten's in-memory FS) is what `micropip` does
  after its download, and it is synchronous Python.  The finder can
  receive the wheel bytes through the bridge and do exactly that.
- A **native wheel** (numpy, aiohttp) carries `.so` side modules.
  `loadPackage` pre-loads them through an asynchronous JavaScript path
  (`loadDynlibsFromPackage`, `WebAssembly.instantiate`), which cannot
  complete inside a synchronous host op.  But emscripten's C-level
  `dlopen`, which CPython's import machinery calls for an extension
  module found on disk, compiles synchronously (`new WebAssembly.Module`
  -- the glue has four such sites).  The probe: unpack numpy into
  site-packages, `import numpy` with no `loadPackage`, and see whether
  the side modules resolve.  If they do, in-place install covers
  everything; if not, native wheels take the deferred path below and
  pure wheels still install in place.

### 9.4 Deferred install and the manifest

The fallback that always works: the host downloads and verifies now,
writes the wheel into `packages/` beside the pyodide version directory,
updates the manifest, and the guest gets the package at its NEXT boot
(the runtime `loadPackage`s the manifest) -- or sooner, at the next
idle point between evaluations, when the host can drive the
asynchronous loader with the guest not mid-call.  The user sees
"installed; will be available on the next run" instead of "installed".

Resolution and download are HOST work under the Addon Manager's trust
rules, not the sandbox network capability of sec 3: the user's click
is the grant, the host's own client fetches, and every file is
verified against the sha256 the lock file carries (pyodide index) or
the hashes PyPI publishes.  Dependencies come from the lock (index
packages declare theirs) or from PyPI's metadata for the rest; the
resolver runs host-side, offline against the lock, so an air-gapped
box with a local mirror works the same way.

Two things to keep straight:

- The installed set is **per user, shared by every principal**.  A
  package installed at a document's request is available to every
  document afterwards, which is the same trust a user extends to an
  addon: packages are code the user chose.  The audit line says which
  principal asked.
- A document never installs anything by itself.  The scan produces an
  offer; the click produces an install; there is no path from file
  content to a download without the user in between.

### 9.5 Where it sits on the roadmap

Independent of the network items (no sandbox network is involved) and
dependent on the bootstrap installer, so it goes right after N0 in
sec 10 as P1/P2.

## 10. Roadmap

Ordered; each step ships alone.  The first is done in the commit that
adds this document.

- **N0 -- demotion (2026-09-03).**  `BUILD_EXPR_WASI_RUNTIME` (default
  OFF) carries the wasmtime runtime; the pyodide runtime is the default
  runtime name; docs marked.  No behaviour change on a box that sets
  the option.
- **P1 -- offer and deferred install.**  The bootstrap installer
  (download, verify, `packages/` + manifest, boot-time `loadPackage`),
  the pre-run scan (sec 9.2), the finder with the `offer` answer (sec
  9.3), the session-principal dialog, `PackageNeeded` for documents.
- **P2 -- in-place install.**  The `installed` answer with the wheel
  bytes over the bridge; pure wheels unpacked by the finder; native
  wheels the same way if the sec 9.3 probe passes, else deferred to
  the idle point.
- **N1 -- the policy engine and the two primitives.**  `NetPolicy`
  with the sec 4 grammar and built-ins, a `NetClient` behind an
  interface (sec 3.3 decision), the `net.http.request` bridge op, the
  `XMLHttpRequest` shim, preferences for the user-level allow/deny
  lists, the audit line.  Test: `requests.get` against a local server
  from a session-principal guest, denied from a document-principal
  guest, redirect hops.  No UI beyond preferences.
- **N2 -- `fetch` and the loop primitive.**  Promise completion through
  the pumped task queue, `pyfetch` and `aiohttp` working, cancellation
  by the soft interrupt.
- **N3 -- the permissions panel.**  Per-document grants with the four
  verbs (grant, deny, inspect, revoke), the passive indicator, the
  install-time addon manifest display.  Shared with the non-network
  permissions of sec 3, which have the same gap today.
- **N4 -- WebSocket.**  `net.ws:<origin>` as a separate target class
  (`wss` default, `ws` written out), the same policy engine, a host
  `WebSocket` global whose frames cross the bridge as messages.  Two
  consumers, deliberately distinct: (a) libraries that speak WebSocket
  to a real endpoint (`websockets`, `python-socketio`, the scene
  stream's own protocol); (b) emscripten's `SOCKFS`, which turns the
  Python `socket` module into WebSocket connections -- giving raw
  `socket` users a path ONLY where the far end is a WebSocket server
  (or a websockify-style proxy the user runs).  (b) is gated by its
  own `net.socket` permission: never for documents, prompted for
  session and addons, because a raw socket to an arbitrary port is a
  wider capability than an HTTP request, even over a policed origin.
- **N5 -- the switch** (sec 11), which the network model and the
  package flow are preconditions of: user code in pyodide is only
  acceptable once it can do what user code does, and reaching an API
  or importing a library is on that list.

Not on the roadmap: a socket-level capability on the host (wasmtime's
`socket_addr_check` shape), UDP, listening sockets, and any network
for the reference implementation.

## 11. The switch: everything Python in pyodide

The end state the user named: **one preference, and all Python runs in
pyodide.**  `ExpressionSandbox.md` sec 8 already lays the ladder;
this section says what the switch is and how the rungs map onto it.

**The switch is a preference, `Python/Runtime = native | pyodide`,
with per-rung overrides underneath it.**  `native` is today.
`pyodide` moves every rung that is ready; a rung that is not ready yet
(or that a particular macro opts out of) falls back to native with an
audit line saying so, never silently.  The user sees, in one place,
which rungs are in the sandbox.

The rungs, and what each needs beyond what exists:

- **Rung 0, expressions** -- built.  Both spreadsheet modes route,
  the corpus gate passes, the budget works.  Under the switch this
  rung is simply ON.  No network (sec 8).
- **Rung 1, native dispatch for `call` members** -- the annotated
  method set retargeted from Python C API calls to generated C++
  dispatch.  Not network-related; it is what makes document
  principals never touch native CPython even indirectly.
- **Rung 2, document-embedded Python** -- scripted-object Proxy code
  lives in the document's guest instance; `execute()` is a bridge
  call into it.  Needs: instance-per-principal with a fast spawn
  (pyodide snapshot as the zygote), the asynchronous loop primitive
  (sec 7), `PropertyPythonObject` restore that never instantiates
  host objects.  This is where **document-principal network grants**
  first apply, and where the panel (N3) becomes necessary rather than
  nice.
- **Rung 3, session scripting** -- the console and macros run in a
  session-principal guest with the wide grants of sec 4.6.  Needs:
  the bindings pack to cover the whole App-level API surface (the
  generated facades today cover the expression subset -- growing them
  is generator work over the same XMLs, `ExpressionSandbox.md` sec
  7.5), user-chosen packages from the bootstrap
  (`docs/PyodideHost.md`, the 2026-09-03 direction: the user installs
  wheels from the pyodide index or PyPI into their own pyodide
  directory), and **session-principal network** so `requests` in a
  macro works on day one.  Macros that import PySide/pivy declare
  themselves gui-native and stay on the island.
- **Rung 4, Python workbenches** -- App-side logic of Draft/BIM and
  friends is rung-3-shaped; the Gui-side residue (PySide6, pivy over
  live Qt/Coin objects) stays on the native island until it empties.
  The switch is honest about this: under `pyodide`, an addon's Gui
  glue still runs native at addon-grade trust, its App logic in the
  guest, and the panel shows both.

What the switch does NOT do: change the C++ core, the renderer, or the
document format.  The guest sees FreeCAD through the same generated
facades and the same bridge in every rung; the switch only decides
which interpreter a piece of Python runs on.  And a document saved
under either setting opens under the other, because a document carries
code and state, never a runtime choice or a grant (sec 5).

## 12. Open questions (to settle before P1 and N1)

- The sec 9.3 probe: does a native wheel unpacked into site-packages
  import synchronously without `loadPackage`?  Decides whether P2
  covers native wheels.
- Resolver for PyPI packages: host-side against PyPI's JSON API, or
  micropip's resolver run in the guest with a host-fed index.

- Host client: Qt Network or libcurl (sec 3.3).  Leaning libcurl for
  App-layer independence and the synchronous fit; the certificate
  store question decides it.
- Whether a document grant should ever be persistable "always", or
  only "session" -- a content-hashed identity makes "always" safe
  against tampering, but a long-lived grant to a document is still a
  long-lived exfiltration channel if the origin is later compromised.
- The audit log's retention and where the Report view shows it.
- The addon manifest format (a key in `package.xml`?), and whether
  the Addon Manager shows it at install time.
- Browser tier: the same policy runs in the page's shim as defense in
  depth, but the browser's own CORS decides what actually connects;
  whether the served-document server should proxy allowed origins for
  the browser tier is a separate design.

## References

- Deno permissions: https://docs.deno.com/runtime/reference/permissions/
- Node.js permission model: https://nodejs.org/api/permissions.html
- Spin outbound HTTP (`allowed_outbound_hosts`): https://spinframework.dev/v3/http-outbound
- wasmtime `WasiCtxBuilder` (socket-level design, not pursued): https://docs.wasmtime.dev/api/wasmtime_wasi/struct.WasiCtxBuilder.html
- pyodide.http: https://pyodide.org/en/stable/usage/api/python-api/http.html
- urllib3 emscripten support: https://urllib3.readthedocs.io/en/stable/reference/contrib/emscripten.html
- Emscripten networking (SOCKFS over WebSocket): https://emscripten.org/docs/porting/networking.html
