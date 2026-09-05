# Network for the Python sandbox: the permission model

> **Superseded 2026-09-03 by `docs/Sandbox.md`**, the consolidated
> sandbox reference audited against the code. This file is kept as the
> historical record; `Sandbox.md` sec 14 says which of its sections
> moved where and which statements here are stale. Do not update this
> file; update `Sandbox.md`.


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
touching the policy or the shim.  What the policy of sec 4 demands of
the client, in the order it matters:

    R1  synchronous perform with a deadline, cancellable from the
        watchdog thread (the XHR path; sec 7)
    R2  connect to the ADDRESS the policy checked while still
        validating TLS and sending SNI/Host for the NAME (the
        rebinding defence of sec 4.3 is void without this)
    R3  redirects left to us, one hop at a time (sec 4.3)
    R4  protocols restricted to http/https at the client, not only
        by our scheme check
    R5  response streamed through a size cap (sec 4.7)
    R6  no cookie engine, no stored credentials (sec 4.4)
    R7  no event loop and no QCoreApplication requirement: the
        client runs inside a bridge op in the App layer, and in
        FreeCADCmd and the headless server there is no Qt event loop
    R8  system certificate store on Linux, Windows and macOS
    R9  system proxy settings
    R10 packaging cost

Three things exist in the environment; one is rejected outright:

- **The host's own CPython** (`urllib`, `requests`): rejected.  It is
  the interpreter this whole arc retires (sec 11), it needs the GIL
  inside a bridge op, and rung 3 removes it from the process.
- **Qt Network** (`QNetworkAccessManager`).  R3 (`ManualRedirectPolicy`),
  R5, R6 (a null cookie jar), R8 and R9 are native.  R1 needs a
  worker thread running a `QEventLoop`, and that needs a
  `QCoreApplication` -- R7 fails in the App layer as it stands (Base
  and App use QtCore for strings and translation only; nothing in
  FreeCADCmd creates an application object).  R2 is murky: Qt 6 has
  `QNetworkRequest::setPeerVerifyName`, so connecting to the checked
  IP and verifying the certificate against the name is possible, but
  whether SNI follows that name through the manager is not documented,
  and getting it wrong silently breaks every virtual-hosted site.  R10
  is nil, but App would link QtNetwork for the first time.
- **libcurl**.  R1 (`curl_easy_perform` + `CURLOPT_TIMEOUT_MS` + a
  progress callback for cancel), R2 (`CURLOPT_RESOLVE` pins
  `host:port:address` and curl still speaks SNI and validates for the
  name -- the exact primitive), R3 (`FOLLOWLOCATION` off, read
  `Location`), R4 (`CURLOPT_PROTOCOLS_STR`), R5 (`MAXFILESIZE` plus
  the write callback), R6 (no cookie engine unless enabled), R7 (a
  handle per call, no loop) are all direct.  R8: the conda libcurl
  (8.21 in `.conda/freecad`, OpenSSL backend, already present as a
  dependency of hdf5 for the FEM build) trusts the `ca-certificates`
  bundle conda ships; whether the Windows and macOS conda builds use
  Schannel / SecureTransport or the same bundle is to be verified on
  the feedstock before N1.  R9: curl honours the proxy environment
  variables but does not read the OS proxy settings; the Gui layer
  can read them through Qt and hand the string down.  R10: an
  explicit `libcurl` dependency in the feedstock.  Bonus for N4: this
  libcurl lists `WS`/`WSS` among its protocols, so the WebSocket
  capability would ride the same client.

**Leaning libcurl**, on R2 and R7: the address pin is the one thing
the design cannot do without, and a client that needs an event loop
inside a bridge op is the wrong shape for the App layer.  The
interface keeps Qt Network as the fallback if the certificate-store
check on Windows or macOS goes badly.

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
deterministic because it runs with no grants (sec 8), so every
network op it meets is the same structured denial.

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

## 8. Expressions are document code, nothing less

The first draft of this document excluded expressions from the network
and from packages.  The user struck that (2026-09-03): the expression
extension exists precisely so that users can embed code in a document,
and with the sandbox in place there is no reason to restrict that code
more than any other user Python.  The security model agrees -- its
axis is the PRINCIPAL, not the carrier.  An expression, a scripted
object's Proxy, an embedded script: all of them run as
`document:<hash>`, and what they may do is decided by that principal's
grants (sec 4.6) and nothing else.  A second policy keyed on "how the
code got into the file" would be exactly the inconsistency sec 3 was
written to remove.

So: an expression that imports a package gets the sec 9 offer; an
expression that reaches the network gets the sec 4 verdict; both
surface the way sec 3.3 already says document code surfaces anything
-- fail fast with `PermissionNeeded` / `PackageNeeded`, a cell error
carrying the payload, the panel entry, re-evaluation on grant.
Recompute never blocks on a prompt, and the pre-run scan of sec 9.2
runs over expression source as it does over any other.

What the earlier exclusion was protecting, restated as what it
actually is:

- **The corpus gate stays deterministic by construction**, not by
  restriction: it runs with no grants and no installed set, so every
  network op is a structured denial and every foreign import a
  structured `PackageNeeded`, both of them stable outputs the gate
  compares like any other error.  No expression in the corpus can
  reach the network under the gate's principal, whatever it says.
- **The budget is the same budget.**  A request's timeout is bounded
  by the remaining soft budget (sec 7); the default 5 s fits a fetch,
  and the user who wants a longer one raises `BudgetMs` for the
  document.  An expression that spends its budget on the network
  produces the ordinary interrupt, not a hang.
- **Re-evaluation cost is the user's choice**, as it is for any
  expensive expression.  An expression re-runs on every recompute of
  what it depends on; one that fetches does so each time.  The
  per-recompute memoization the ladder already plans for rung 2
  (`ExpressionSandbox.md` sec 8) applies to expressions in the same
  way when it lands, and until then the audit log (sec 6) makes the
  cost visible.  A document whose value depends on a server is a
  document the user chose to write; the model's job is to make sure
  they chose it, not to forbid it.

Rung 0 is therefore ON under the switch with the same grants as the
rest of the document's code, which is the only reading of "one
principal, one policy" that holds up.

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

**REVISED 2026-09-05: the finder is retired.**  Asking at `find_spec`
asked for EVERY failed import, including one the workload catches
itself -- lark's `try: import regex` (in BIM's generated SQL parser),
uuid's `_uuid` -- and `regex` is in pyodide's lock, so a recompute of
the BIM corpus recorded an install offer nobody asked for.  The same
`pkg.missing {import_name}` op is now sent by the guest's error reply
(`ImageDispatch.cpp` `errorReply`) for a `ModuleNotFoundError` that
LEAVES the guest uncaught: the import that actually ended the work,
and only that one.  The host's three answers below are unchanged; an
`offer`/`installed` text replaces the exception's message.

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
- A **wheel with compiled extension modules** (numpy, aiohttp) is
  wasm too -- every pyodide wheel is -- but its `.so` files are
  emscripten SIDE MODULES (`*.cpython-314-wasm32-emscripten.so`) that
  must be instantiated as `WebAssembly.Module`s and dynamically linked
  into the main pyodide module before Python can import them.
  `loadPackage` pre-loads them through an asynchronous JavaScript path
  (`loadDynlibsFromPackage`, `WebAssembly.instantiate`), which cannot
  complete inside a synchronous host op.  But emscripten's C-level
  `dlopen`, which CPython's import machinery calls for an extension
  module found on disk, compiles synchronously (`new WebAssembly.Module`
  -- the glue has four such sites, and its `_dlopen_js` runs
  `loadDynamicLibrary` with `loadAsync:false`).  The probe: unpack
  numpy into site-packages, `import numpy` with no `loadPackage`, and
  see whether the side modules resolve.  If they do, in-place install
  covers everything; if not, wheels with extension modules take the
  deferred path below and pure wheels still install in place.

What the in-place path has to get right besides the loader (the list
the probe and P2 work through):

- **Unpacking is already synchronous Python**:
  `pyodide._package_loader.unpack_buffer(buffer, filename, format,
  target, calculate_dynlibs=True)` is what `loadPackage` itself calls
  from JavaScript; it lays the wheel out into site-packages, puts
  shared libraries into `DSO_DIR` (two levels above site-packages, on
  the default `LD_LIBRARY_PATH` the glue sets), and returns the list
  of `.so` files.  The finder reuses it; nothing is reimplemented.
- **Dependency shared libraries and load order.**  Lock packages
  marked `shared_library` (openblas for scipy, libhdf5, ...) must be
  on disk before the wheel that needs them and loaded with global
  symbol visibility.  Emscripten's synchronous `dlopen` resolves a
  side module's `dylink.0` `needed` list recursively from
  `LD_LIBRARY_PATH`, so placing them in `DSO_DIR` first should be
  enough; the probe uses scipy, not only numpy, to see it.
- **Pyodide's bookkeeping.**  `pyodide.loadedPackages` (JavaScript)
  and micropip's view of `dist-info/INSTALLER` must learn about the
  package, or a later `loadPackage("scipy")` re-fetches numpy and
  `micropip.list()` lies.  The runtime's glue gets a hook to register
  what the finder installed.
- **`importlib.invalidate_caches()`** after writing into
  site-packages, or the path finder keeps its stale directory listing
  and the retry fails the same way.
- **Getting the bytes in without copying them through CBOR.**  The
  host writes the verified wheel under the pyodide root; the finder
  reads it with the scoped `readbuffer(path)` the guest already has
  (the loader uses it), straight into an `ArrayBuffer` for
  `unpack_buffer`.  A 3 MB wheel never crosses the bridge as a value.
- **The budget.**  Unpacking plus compiling numpy's thirteen side
  modules took ~250 ms under the asynchronous loader; synchronous
  compile is comparable and is not interruptible.  The host knows an
  install is in progress and extends the current call's budget by the
  install's own limit rather than letting the watchdog fire on it.
- **Memory growth.**  Instantiating side modules can grow linear
  memory, which detaches the transport's typed-array views; the
  buffered transport already re-acquires them per call
  (`PyodideHost.md` sec 9), and the install happens inside the guest
  after the host op returned, so no view is live across it.
- **ABI tags.**  The resolver selects wheels by the installed
  pyodide's ABI (`info.abi_version` in the lock, `2026_0` today:
  `pyodide_2026_0_wasm32` for index packages, `pyemscripten_2026_0`
  for PEP 783 PyPI uploads) and refuses anything else, so a wheel
  never reaches `dlopen` with the wrong `dylink` expectations.
- **Where the browser tier differs.**  Synchronous `WebAssembly.Module`
  compile is refused on a browser main thread above a small size,
  which is why pyodide preloads in the first place.  The browser-tier
  guest runs on the MAIN thread today (`ExpressionImage.md`, "The
  browser tier": reach-back needs a Worker with `Atomics.wait`, and
  that is not built).  So in the browser the in-place path is not
  used at all; sec 9.6 has the browser flow, which is asynchronous
  and needs no synchronous compile.  On the bare V8 host there is no
  such limit.

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

**Built 2026-09-03** (`docs/PyodideHost.md` sec 12): P1 as the finder
plus the `pkg.missing` op plus a pending `pkg.install:<name>` request
the permissions panel turns into the install; the deferred path of
sec 9.4 (install now, fresh guest at the next evaluation) is what
ships.  P2 (in-place) was not probed.  The pre-run scan was dropped:
the expression language reaches a module only through `import`
statements, which fail at the guest's import with the same offer.

### 9.6 The browser tier: where the packages come from there

The browser tier moves to pyodide with the rest (sec 0); today it runs
the reference image on the page's main thread, and nothing of this
section exists there yet.  In the browser "the host" is the page's own
JavaScript, and the same finder and the same `pkg.missing` op apply;
what changes is where wheels come from, where they persist, and who
may install.

**Sources, in order of preference:**

1. **The FreeCAD that serves the document.**  The desktop bootstrap
   already keeps the user's chosen set in `packages/` with a manifest
   and the lock's hashes.  `SceneStreamServer` serves HTTP endpoints
   behind the same access gate as the scene; it adds `/pyodide/...`
   (the runtime files of the served pyodide version) and
   `/packages/...` (the manifest and the wheels).  A viewer's guest
   boots from the server's pyodide and loads the server's set, so the
   packages the owner installed on the desktop follow the document
   into every browser that opens it -- LAN and air-gapped included,
   and never a third-party CDN involved.  Same version, same hashes,
   same behaviour as the desktop guest: this is what keeps the parity
   gate meaningful across tiers.
2. **The public indexes**, for a standalone page with no serving
   FreeCAD (static hosting, a shared snapshot): pyodide's ordinary
   browser flow -- `loadPackage` from the jsDelivr pyodide CDN,
   `micropip` from PyPI -- through the browser's own `fetch`.  Both
   hosts send CORS headers, which is why micropip works in browsers at
   all.  Hashes still come from the lock file the page ships.

**Persistence.**  A browser has no user package directory.  Downloaded
wheels go into the origin's Cache API (or IndexedDB), keyed by file
name and hash, and the page keeps its own manifest in `localStorage`;
at the next guest boot the page installs from the cache without a
network round trip, and the ordinary HTTP cache covers the runtime
files themselves.  This is the JupyterLite pattern (piplite caches
wheels per origin and re-installs at kernel start).  Nothing is
persisted into the guest's in-memory filesystem across page loads;
mounting site-packages on IDBFS would be the alternative, at the cost
of pyodide-version coupling in the stored tree.

**Who may install.**  A viewer installs into THEIR browser's storage,
never onto the server: the serving FreeCAD's set is the owner's, and
the access-grant model of `MultiDocServe.md` does not give a viewer
write access to the owner's machine.  A viewer of a served document
therefore sees two kinds of offer: "load `scipy` from the server's
set" (already installed by the owner, one click, no download decision
to make) and "install `scipy` from the pyodide index into this
browser" (source 2), which is only offered when the page is allowed
to reach the index at all -- a static page is, a page served from a
FreeCAD on a LAN may not be, and the offer says which.

**The flow, and why it is simpler than the desktop's.**  The finder
answers `offer`; the page shows the prompt (session-principal code,
the viewer's own gesture) or records the `PackageNeeded` for a
document principal; on accept the page runs pyodide's asynchronous
loader (`fetch`, `unpack_buffer`, `loadDynlibsFromPackage`) with
`await`, registers the package in `loadedPackages`, and re-runs the
evaluation that failed.  Nothing needs the synchronous in-place path
of sec 9.3: the browser's event loop is right there, and an
evaluation in the browser tier is cheap to re-run.  If the guest ever
moves to a Worker (which reach-back needs anyway), the in-place path
becomes legal there too, but it buys nothing the retry does not.

**What does not change.**  Document code never installs by itself;
the offer/click/install separation of sec 9.4 holds.  Expressions
are document code like any other, with the same offers under the same
principal (sec 8).  And the
sandbox network capability of sec 3 is a separate matter from the
package download: in the browser the download is the PAGE's fetch
under the browser's CORS, exactly as the desktop's is the HOST's
client under the Addon Manager's trust, and the guest sees neither.

## 10. Roadmap

Ordered; each step ships alone.  The first is done in the commit that
adds this document.

- **N0 -- demotion (2026-09-03).**  `BUILD_EXPR_WASI_RUNTIME` (default
  OFF) carries the wasmtime runtime; the pyodide runtime is the default
  runtime name; docs marked.  No behaviour change on a box that sets
  the option.
- **P1 -- offer and deferred install (DONE 2026-09-03,
  `docs/PyodideHost.md` sec 12).**  The bootstrap installer (download,
  verify, `packages/` + manifest, boot-time `loadPackage`), the finder
  with the `offer` answer (sec 9.3), the pending `pkg.install` request
  the permissions panel turns into the install.  The pre-run scan (sec
  9.2) was dropped: the expression language reaches a module only
  through an `import` statement, which fails at the guest's import
  with the same offer; it returns only with N5, when macros and
  console lines run in the guest and a scan before a long script is
  worth having.
- **G0 (DONE 2026-09-03, `SandboxGui.md` sec 8), G1** (sized in
  `SandboxGui.md` sec 9; G3-G5 revised in its sec 10: the scene is
  mirrored from pivy in the guest, forms use the Jupyter widget
  protocol): the porting linter, then Draft's
  and BIM's App side in the guest as LOCAL wheels through the same
  package loader -- boot-time loading only, which is why P2 is not a
  prerequisite of them.
- **P2 -- in-place install** (moved after G1, 2026-09-03: nothing
  built depends on it, and a fresh guest boots with the package in
  about 1.5 s).  The `installed` answer with the wheel bytes over the
  bridge; pure wheels unpacked by the finder; native wheels the same
  way if the sec 9.3 probe passes, else deferred to the idle point.
- **N1 -- the policy engine and the two primitives.**  `NetPolicy`
  with the sec 4 grammar and built-ins, a `NetClient` behind an
  interface (sec 3.3 decision), the `net.http.request` bridge op, the
  `XMLHttpRequest` shim, preferences for the user-level allow/deny
  lists, the audit line.  Test: `requests.get` against a local server
  from a session-principal guest, denied from a document-principal
  guest, redirect hops.  No UI beyond preferences.
- **PyPI sources for the package installer** (PEP 783
  `pyemscripten_<abi>` wheels and pure wheels, metadata from PyPI's
  JSON API, dependencies resolved host-side, hashes from PyPI).  After
  N1, so the installer's fetches move from urllib / the Addon
  Manager's client to the `NetClient` N1 brings and are written once.
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
  rung is simply ON, with the document principal's grants applying to
  expression code exactly as to the rest (sec 8).
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
  guest, and the panel shows both.  How the island empties -- a
  FreeCAD UI protocol in place of a PySide shim, with Draft/BIM as
  the test -- is `docs/SandboxGui.md`.

What the switch does NOT do: change the C++ core, the renderer, or the
document format.  The guest sees FreeCAD through the same generated
facades and the same bridge in every rung; the switch only decides
which interpreter a piece of Python runs on.  And a document saved
under either setting opens under the other, because a document carries
code and state, never a runtime choice or a grant (sec 5).

## 12. Open questions (to settle before P1 and N1)

- The sec 9.3 probe: does a wheel with compiled extension modules,
  unpacked into site-packages by `unpack_buffer`, import synchronously
  without `loadPackage`, including one with a `shared_library`
  dependency (scipy over openblas)?  Decides whether P2 covers such
  wheels or only pure-Python ones.  The list under sec 9.3 is what the
  probe checks beyond the bare import.
- Resolver for PyPI packages: host-side against PyPI's JSON API, or
  micropip's resolver run in the guest with a host-fed index.
- Host client: libcurl unless the certificate-store check on the
  Windows and macOS conda builds fails (sec 3.3, R8); then Qt Network
  behind the same interface, with R2 and R7 solved some other way.
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
