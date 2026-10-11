# Share access — who may join a shared document, and how we know

Status: **design discussion, nothing here is implemented.** What *is* implemented is
described in [MultiDocServe.md](./MultiDocServe.md) §4 (the door token) and §6.1 (the
desktop sharing UI, client records, rules, kick/ban/forget). This document records the
2026-08-06 discussion that concluded the shipped model is the wrong shape, and what to
build instead.

Companions: [MultiDocServe.md](./MultiDocServe.md) (the wire and the shipped UI),
[ComputeBoundaries.md](./ComputeBoundaries.md) §1 (process-per-session, the isolation
boundary this never crosses).

---

## 1. What is shipped, and why it is not enough

One shared secret gates every endpoint. Separately, a persisted list of *client records*
(name pattern, address pattern, access, banned) is matched against connections by the
desktop sharing manager, which then applies view-only or kicks.

Two structural faults, both found by the user in use:

- **The name is not a credential.** The token answers "may you connect at all"; a record
  only modulates access afterwards, and the default for an unknown client is *full
  access*. So renaming a client from `delta` to `delta2` in the viewer menu changed
  nothing — the rule naming `delta` was never a gate, only a modifier. Access control
  that fails **open** for anyone not named in it is not access control.
- **Enforcement is after the fact.** The check lives in the sharing manager's roster
  poll, so a banned client connects, is registered, may receive scene bytes, and is
  disconnected a tick later. A door that admits you and then shows you out is not a door.

A third fault, fixed on the day (`ae3c438ec7`) but worth remembering as a design
pressure: a connection exists *before* its hello arrives, so the first judgement of it
is nameless. Any scheme keyed on the client-supplied name has to survive the name
arriving late.

## 2. The user's model: grants, and two lists

Settled in discussion:

- **A token is an invitation, and it carries who may use it.** The backend holds a list
  of tokens; each entry has a name pattern, an address pattern, and an access level. A
  connection presents a token, the entry is looked up, and the connection's name and
  address must satisfy that entry — **a mismatch is denied by construction**, at the
  door, before any scene bytes. A `*` / `*` entry is the open invitation: anyone from
  anywhere.
- **Which grant fields are boundaries.** Only two: the **token**, which is a secret,
  and the **identity**, which the front door verified (§4). The **name** is self-declared
  in the viewer's hello and the **address** is where the connection appears to come
  from, so both narrow *who among the invited* a grant covers — they do not keep out
  anyone holding the token, who can simply present another name. This is by
  construction, not a gap to close: the name is a display label. Say so wherever a host
  authors a grant (the panel's grant help does), and read a name-keyed ban as
  housekeeping, not enforcement.
- **Two lists.** The **persistent** list (`user.cfg`) is every grant ever issued, each
  with an enabled flag. The **live** list is what the door actually checks: seeded from
  the enabled persistent grants when sharing starts, and free to evolve at runtime.
- **Ban = drop from the live list, keep the persistent entry disabled**, so it can be
  re-enabled later without reissuing a link. Ban is *not* deletion.
- **Live-only easing.** A client admitted as `delta` who renames itself to `delta2` gets
  a live rule accepting the new name on that token, so it can reconnect — and that easing
  **dies with the process**. Restart and the door is exactly what is written down. The
  panel must show live-only rules differently, with a way to promote ("Keep") or drop
  them, or the host cannot explain why yesterday's access is gone.

### 2.1 Session tokens

The user's refinement: the persisted token is the **authorization** token; once a
connection is approved the backend issues it a **session token**, and that is what
identifies the client thereafter — for renaming itself and for the host's management
(view/edit switch, kick, ban).

This is the standard **ticket-exchange** pattern (§3), and it buys:

- **identity that survives renaming** — the session is the handle, the name is a label;
- **kick that means something** — today kick closes a socket and the client reconnects;
  ban the session and the reconnect is refused;
- **revocation that is not all-or-nothing** — today the only real revocation is minting a
  new token, which cuts off everyone.

⚠️ With a session token, checking the *name* at the door matters much less. Make the
grant's name pattern a constraint on **first join only** ("this invite was issued for
delta"); after that the session token is the identity and renames are free. Otherwise
every rename becomes a door problem and live rules exist only to work around our own gate.

### 2.2 Full control: the host level

Added 2026-09-14. A grant's access is one of four: **edit** (0), **view only** (1),
**banned** (2) and **full control** (3), which makes the connection a **host**: it may
act as the desktop user would, beyond the document it is joined to -- the host's
preferences, and the commands and tool bar actions an editing connection is refused.
An editing connection asking for one of those is answered `Forbidden`. It is for the
desktop's owner reaching their own machine from another device.

The rules that keep it that:

- **Only a verified identity, named literally.** A full-control grant makes a host only
  of a connection whose front-door identity (sec 4) the grant's identity field names
  exactly -- no `*` or `?`. On a pattern, or for a connection with no verified identity,
  the same grant admits an editor. A name is what a client chose and an address is where
  it appears to be; neither is a person.
- **By hand, the same.** Making a live connection a host for the session needs a
  verified identity too (`SceneStreamServer::setClientAccess`,
  `Gui.serveSetClientMode(id, 'host')`).
- **The panel says so.** The Share panel offers "Full control" in the invite row, the
  grant editor, a grant's row and a live connection's row, disabled where it cannot
  apply -- a grant whose identity is a pattern, a connection with no verified identity
  -- and an invite or a new grant of full control on a pattern is refused with a
  warning rather than stored as a host grant that would admit editors.
- **An easing never carries it.** The live-only rule a rename mints is at most edit.
- **Re-judged like any access.** A change of the grant list re-judges every connection,
  a hand promotion included, and the client is told its level
  (`{"cmd":"config","viewOnly":...,"access":"view|edit|host"}`) -- at that change, at a
  hand change, and at the hello whenever the level is not edit.
- **The process's own callers are hosts.** `FormWidgets.control()` and the C++ tests
  answer as the desktop unless they say which connection they stand in for.

Which ops need a host is the control channel's to say: an op's level is checked before it
runs (`OmniControl::requiredAccess` for the omni ops -- `param.set`/`param.reset`), and an
op whose answer depends on what it touches asks `Gui::sceneControlAccess()`.

The browser viewer draws what the level allows, and the server judges either way. The
config message's `access` reaches the page as `window.fcviewerAccess` and an `fc:access`
event (wasm `fcviewer_access_event`). A host's tool bars and `/cmd` rows are not drawn
refused; an editor's commands off the browser allowlist are. The omni box offers no
`/param` mode to anyone: the host's preferences are not a browser's to change, whatever
its level.

### 2.3 Who may write: someone known

Added 2026-10-04, with the transaction log's authors (docs/TransactionLog.md sec 30.6
U4 and U6, 30.9, 30.11). What a connection writes is recorded under who it is, so a
connection that is nobody does not write. **Sec 2.2's rule for a host, extended to
edit:**

- **A connection may edit when it is known**, one of two ways:
  - a **verified identity** -- the front door asserted one (sec 4);
  - an **invitation issued to its one name** -- the grant that admits it has a token,
    which is a secret, and a name written out in full rather than a pattern, which is
    the host saying whose it is, and the connection gives that name. Logged as
    `invited`, so the record shows no sign-in stood behind it.
- **Anyone else a grant admits may look**, whatever access the grant says: an edit or a
  full-control grant matched on a name pattern, an address, or nothing but a token; and
  the shared token, or no token at all, which is an open invitation. A view-only
  connection's picks are dropped and its mutating ops are answered `ViewOnly`, as they
  were.
- **An invitation is to the name it names.** A client that renames itself is whoever it
  now says it is: it keeps its place and goes view-only, and the log has it as another
  user from there on. The easing a rename mints (sec 2) never invites.
- **The shared token can be issued to one person too.** With no grant list, the host
  may say whose the shared token is (`SceneStreamServer::setTokenInvitee`, or
  `FC_SERVE_INVITE=<name>` beside `FC_SERVE_TOKEN`): the token is then an invitation to
  that one name, and the connection that presents it and gives that name may edit,
  logged as `invited`. Under any other name it admits to look, as before. It does
  nothing without a token -- a name with no secret behind it invites nobody -- and
  nothing while a grant list is the door.
- **By hand, the same.** The host cannot make an unknown connection an editor from the
  roster (`SceneStreamServer::setClientAccess`, `Gui.serveSetClientMode(id, 'edit')`
  answer false); the panel's "Can edit" is disabled for it and says why.
- **One function decides** (`writerRule` in `SceneServer.cpp`), applied wherever a
  level is: at the upgrade, at every hello, when the grant list changes, at a rename,
  at a hand change. The roster says which (`SceneClientInfo::invited`,
  `Gui.serveClients()[i]["invited"]`).

**What it costs.** Sharing with no front door -- a LAN, a tunnel with a bare token --
is view-only for everyone but the desktop, until the host invites by name: a grant
with the token and the person's name, and a link carrying both (`?token=...&client=
...`; the viewer says `?client=` in its hello). A page that fetches before it has said
a name is admitted for those fetches by the same token as an open invitation to look
beside the named one; the most specific match judges the socket's hello. For the one
person a served scene is started for -- the demo recipes of `docs/Sandbox.md`, a quick
tunnel -- `FC_SERVE_INVITE=<name>` does the same with no grant list: the link is
`?token=<secret>&client=<name>`.

**From the sharing panel** (2026-10-05, docs/TransactionLog.md sec 30.26, 30.32). The
Invite row follows the door. Behind a sign-in door it takes an identity, as it did.
Behind none it takes a name and a count of browsers, and makes a grant with a token of
its own that counts them (sec 2.5); the link that carries the token is put on the
clipboard. A pattern is refused, a share with no token is refused, and full control is
not offered. Every grant with a link of its own -- one that counts, or a token with a
name written out -- has a **Link** button on its row. An invitation by name on the
share's own token, which this row made for a day, still works where one is stored: a
name is not a secret, and whoever holds the plain link and gives that name gets what
the name gets.

### 2.5 A grant that counts its browsers

Added 2026-10-05 (docs/TransactionLog.md sec 30.32; numbered after 2.4, read with
2.3). **A token is half of who its holder is; the browser keeps the other half.** The
viewer makes an id once, keeps it (`localStorage`), and says it in every hello. The
host knows a browser by a hash of that id, and keeps a **record** of every browser an
admitted hello came from, under whatever token or sign-in -- so one browser is the
same row under another link.

A grant may say **how many browsers it is for** (`maxUsers`; 0, as every grant was,
counts none). Such a grant admits a hello only with a browser's id, **enrols** the
first so many, and refuses any other -- or leaves it to a lesser grant on the same
token. A browser enrolled is someone known in the sense of sec 2.3: it may edit where
the grant says so, and what it writes is recorded under it (`enrolled`, named
`<name>~<start of its key>`). A name is a label there: renaming does not make it
anyone else. The host names a browser, turns it off (out, and kept out) or forgets it
(out, and enrolled again if it comes back while there is room), from the grant's
**Users...** or the panel's **Browsers...**.

The panel's Invite row, behind a door that signs nobody in, makes such a grant: a
token of its own, for one browser unless the count beside the name says more, and
the link that carries it. A grant that names whom a door signed in can count its
browsers the same way.

Nobody verified who sits at a browser. What holds: the link works in the browsers it
was first opened in and in no other, each is a row the host can act on, and each
one's writes are its own. A plain request -- the first load, the polling route, a
blob -- says which browser it is in a header, `X-FC-Device`, and is judged as a hello
is: a browser the grant has, or has room for, reads; one that says nothing, and one
past the count, reads nothing of the scene (docs/TransactionLog.md sec 30.40).

### 2.4 What a client may send: files

Added 2026-10-05 (docs/TransactionLog.md sec 30.23). Two control ops carry
a file from a browser to the host, and both go one way only: the client
sends a name and bytes, the host alone decides where they land, and no op
lists a directory, reads a host file or takes a path from a client.

- **`widgets.upload`** -- a mirrored task panel's file chooser
  (docs/Sandbox.md 7.22). The answer is the path the host wrote, which the
  panel's field then names.
- **`requests.send`** -- a copy of the served document, to be merged. The
  host keeps it and reads nothing of it; it is listed in the transaction
  log panel with who sent it, and is opened only when the owner brings it
  in. The answer has no path. It is kept in the document's transaction
  log -- a row under the sender and the bytes as one blob, saved with the
  history, so it waits across a restart -- and let go once the branch it
  was brought in to is merged or deleted, or when it is dropped unread
  (docs/TransactionLog.md sec 30.29).

Both are for a connection that may edit (sec 2.3), and both are under one
limit: **the preference `UploadLimitMB`** of `BaseApp/Preferences/SceneShare`,
16 unless set, preset for a headless serve by **`FC_SERVE_UPLOAD_MB`**. It is
read at each upload. It cannot be raised past 46 MB, which is what one
frame of the socket carries once the bytes are base64.

The dialog that starts a share and the sharing panel both have the control
("Clients may send up to"); the panel's holds from the next upload. While
the environment holds the limit the control shows it and is disabled.

**What waits is bounded too.** The copies sent to one document and not yet
merged or deleted may together be at most **`RequestsTotalMB`**, 64 unless
set, preset by **`FC_SERVE_REQUESTS_MB`**; one that would take it past is
refused, `TooMany`. A waiting copy is in the document's history and is
saved with it, so this is also the most a client can make the file grow
by. The control is beside the upload limit's.

## 3. Prior art

- **Ticket exchange** — a long-lived credential buys a short-lived, often single-use
  ticket; only the ticket rides the URL, because a URL token leaks into proxy logs and
  browser history ([websocket.org](https://websocket.org/guides/authentication/),
  [python-websockets](https://websockets.readthedocs.io/en/stable/topics/authentication.html)).
- **Jupyter** — bearer token bootstraps, cookie + XSRF carries the session, either is
  accepted ([JupyterHub](https://jupyterhub.readthedocs.io/en/latest/reference/api/services.auth.html)).
- **VS Code Live Share** — per-session non-guessable invitation, JWT claims naming the
  session, optional host approval, and **anonymous guests default to read-only**
  ([security model](https://learn.microsoft.com/en-us/visualstudio/liveshare/reference/security)).
- **Invite links** (CoCalc, MURAL, GitHub) — opaque per-invite token, optional expiry and
  use count, **individual** revocation
  ([CoCalc](https://doc.cocalc.com/howto/project-invitation-tokens.html)).

Nothing here is exotic; §2 is the conventional design.

## 4. Delegating authentication to the edge

The direction the discussion ended on, and the one that makes the rest smaller.

An authenticating front door logs the user in and asserts a **verified identity** to the
origin in a header:

- **Cloudflare Access** — login via Google/GitHub/email one-time PIN, origin receives a
  signed JWT in `Cf-Access-Jwt-Assertion` plus `cf-access-authenticated-user-email`,
  verifiable against `https://<team>.cloudflareaccess.com/cdn-cgi/access/certs` (RS256);
  **service tokens** cover non-browser clients — which is our probe suite
  ([validating JWTs](https://developers.cloudflare.com/cloudflare-one/access-controls/applications/http-apps/authorization-cookie/validating-json/)).
- **ngrok Traffic Policy** — `oauth` / `openid-connect` actions, injected headers such as
  `NGROK_AUTH_USER_EMAIL` ([docs](https://ngrok.com/docs/traffic-policy/actions/oauth)).
- **Self-hosted** — Caddy `forward_auth` to oauth2-proxy or Authelia gives the identical
  contract with only our own box in the path.

**They all reduce to one origin-side contract: a trusted front door asserts an identity
in a header.** Build to that and the front door is swappable.

**Only a door that signs people in is believed about who they are** (2026-10-05,
docs/TransactionLog.md sec 30.26). Trusting the proxy is two things: its word for the
client's address, and its word for the client's identity. A quick tunnel, or a reverse
proxy that only carries traffic, passes on whatever headers the client wrote, so a
client that sends `X-Forwarded-Email` itself would be a verified identity -- an editor
under the shared token, and whoever an identity grant names. `SceneStreamServer::
setIdentityDoor(false)` keeps the first and drops the second: no header is taken for an
identity. The Share dialog sets it from its door ("Viewers sign in at this door").

**It is off unless someone says otherwise** (user, 2026-10-05). A serve started by
script or the environment says so with `FC_SERVE_IDENTITY_DOOR=1`, or by naming the
door's header in `FC_SERVE_IDENTITY_HEADER`; `FC_SERVE_TRUST_PROXY=1` alone believes
the address and nothing else. `scripts/share-edge.sh` prints the pair for its two
doors that sign people in (`access`, `caddy`) and not for `quick`.

A door of one's own that signs people in may name its header in the dialog ("Identity
header", kept with the door): for one that uses none of the three above. Empty reads
those; a name is the only one read.

What it does to §2:

- **Grants key on verified identity**, not a self-declared name — and the wildcard
  matching already written transfers directly (`*@company.com` view-only, `lei@…` edit).
  The `client` name shrinks to a display label, which is all it was ever honest as.
- **The session token largely collapses into the edge's own session.** The browser
  attaches the edge cookie to every request *including the WebSocket upgrade*, so each
  connection arrives with a fresh signed identity. Kick/view-only then key on the identity
  subject — better than a random session token, because a banned user cannot shed the ban
  by reconnecting.
- **Bearer grants survive as the fallback** for LAN use with no domain and no IdP, and for
  machine clients — the same split Cloudflare makes with service tokens.

**Verification, cheaply.** Proper verification is RS256 + a JWKS fetch, i.e. real crypto in
the Gui layer. The weaker check we already rely on is sufficient while the front door runs
on the same machine: **trust the header only from a loopback peer**, exactly the
`X-Forwarded-For` rule of MultiDocServe.md §6.1. Start there; add signature verification if
the front door ever moves off-box.

**The cost, stated plainly.** Cloudflare and ngrok terminate TLS, so they see what the
wire carries. That is **not the CAD data**: the stream is tessellated meshes at view
tolerance, materials/textures, the object tree with names, and property values for
selected objects (the inspector card). The `.FCStd`, feature history, sketches,
constraints, and B-rep never cross the wire. So the exposure is roughly "an STL export
plus a named BOM" — the design intent is unrecoverable, but for parts whose *shape* is
the secret, view-tolerance triangles are enough to remanufacture from. Choose
self-hosted when the shape itself is sensitive; otherwise this cost is modest. The
self-hosted variant keeps even the triangles on our own host while still delegating
*identity* to Google/GitHub. And any edge auth breaks headless probes unless the token
path stays or service tokens are issued.

## 5. Relays and tunnels (the transport question, separate from auth)

Surveyed 2026-08-06 for exposing a share publicly:

- **Cloudflare Tunnel** — free, no bandwidth cap, `trycloudflare.com` quick tunnels need no
  account. ⚠️ Free/Pro **close an idle WebSocket after 100 s**
  ([docs](https://developers.cloudflare.com/network/websockets/)) — fatal for a parked
  viewer, since our stream is silent while the model is unchanged.
- **ngrok** — 1 GB/month, 3 endpoints, and a browser interstitial our viewer page would hit
  on every load.
- **Tailscale Funnel** — HTTPS only on 443/8443/10000; aimed at internal-first.
- **Self-hosted on the existing linode** — `frp`, `chisel`, or Caddy. We already own the
  box, and nothing third-party sees even the triangles (§4 on what the wire actually
  carries — the parametric model never crosses it either way). Replacing the hand-written
  `scripts/scene-proxy.py` with Caddy would also bring real TLS (so `wss://` and a secure
  context on phones).
- **Managed realtime relays** (Ably, Pusher, PartyKit, Liveblocks) are the wrong category:
  they relay pub/sub messages between clients, while our wire is a binary snapshot protocol
  plus HTTP blob/level routes. We would rewrite the transport to gain fan-out we do not need.

⚠️ **Two findings that apply whatever we choose — both since done (§7 item 1):**

1. **We need a periodic keepalive on the stream** (~30 s). An idle viewer currently sends
   and receives nothing, so any relay with an idle timeout (Cloudflare's 100 s, the common
   60 s proxy default) drops it, and the viewer looks like it disconnects at random.
2. **Accept `CF-Connecting-IP`** beside `X-Forwarded-For` — it is the header Cloudflare
   guarantees.

**Cost of the Cloudflare path, checked 2026-08-06:** $0 at our scale — Tunnel is free and
uncapped, Access is free to 50 users ($7/user/month past that), and service tokens are in
the free tier.

### 5.1 HTTP caching at the edge (surveyed 2026-08-07)

The server will serve more static content over time (the viewer bundle already, SVG icons
and similar assets next), so what a Cloudflare front door does to caching matters. The
short version: **our origin headers are the contract; Cloudflare adds a shared edge cache
only on a named-tunnel zone, and only where told to.**

- **Quick tunnels (`trycloudflare.com`) never edge-cache.** Every response passes through
  as `cf-cache-status: DYNAMIC`, and since the zone is Cloudflare's, there is nothing to
  configure. Browser caching still works normally — the origin's `Cache-Control` decides.
- *(verified live 2026-08-07 on `cad.thundereal.com`: our `no-store` on a `.js` asset
  yields `cf-cache-status: BYPASS` — the origin header wins even for extensions on the
  cacheable list.)*
- **A named tunnel on our own domain gets the real edge cache, gated by path extension.**
  Default ("Standard") caching only considers URLs whose extension is on Cloudflare's
  static list — `.svg`, `.css`, `.js`, `.png`, `.ico` qualify; **`.html`, `.wasm`, and
  extension-less paths do not**. TTL then honors the origin's `Cache-Control`;
  `no-store` / `private` are respected. Cache Rules (free tier) widen this — e.g. "cache
  `/blob`" or "cache the `.wasm` bundle".
- **WebSockets are never cached** — the scene stream passes straight through either way.
- **Under Access, the edge cache is shared across authenticated users** (the login check
  runs before the cache lookup). Fine for icons and content-addressed geometry; anything
  per-user must say `no-store`/`private` itself.

How that lands on what the server already sends:

- **`/blob` is content-addressed and says `public, max-age=31536000, immutable`** —
  ideal for any cache, but the path has no extension, so a real zone still treats it as
  DYNAMIC until a Cache Rule marks it cacheable. One rule to add the day a domain shows up.
- **The batch-fetch route says `no-store`**, necessarily: a batch is named by the request
  *body*, which no HTTP cache keys on. Edge caching it would serve one viewer's batch to
  another. Cloudflare respects the header.
- **A stale viewer bundle cannot strand anyone**: the build-stamp reload push with its
  cache-bust parameter recovers even an aggressively cached page.

**Rule for future static assets (icons included): content-hashed filename +
`public, max-age=31536000, immutable`** — the `/blob` policy generalized. That is optimal
everywhere with zero edge configuration: browser-cached through a quick tunnel,
edge-cached automatically on a named zone (`.svg` is on the default list), and never
stale because a changed asset is a new URL.

### 5.2 The front door of record: the named tunnel on cad.thundereal.com

Decided 2026-08-07: **the share's public door is the named tunnel** — Cloudflare tunnel
`fc-share` carrying `cad.thundereal.com` to the serving process (`cloudflared tunnel run
--url http://localhost:<port> fc-share`; cert in `~/.cloudflared/`, DNS CNAME on the
zone). The quick tunnel stays as the no-account fallback, and both remain presets in the
Share dialog's front-door chooser (§7.6): the bundled presets are **LAN**, **Quick
tunnel**, and **thundereal** — the last being the own-door shape, public origin
`https://cad.thundereal.com` with the identity door on. Presets are seeded lazily:
`loadDoors()` answers the bundled three while `SceneShare/Doors` is empty and
`DoorsSeeded` unset; they persist (and the flag is written) the first time the dialog
saves, so a deliberately deleted preset stays deleted. An empty stored list on a fresh
profile is therefore the working state, not a missing feature.

Through this door a share link is **tokenless** and carries only the doc group:

    https://cad.thundereal.com/fcviewer.html?doc=<DocName>

Cloudflare Access authenticates the visitor (one-time PIN, Google, or GitHub — §4), the
grant list authorizes: only an identity a grant covers gets scene bytes, everyone else is
refused after login. The backend must be launched with `FC_SERVE_TRUST_PROXY=1` and
`FC_SERVE_IDENTITY_DOOR=1` (the dialog sets both for the session from its door) and serves the viewer bundle
itself from `FC_BGFX_VIEWER_BUILD`, so the one hostname carries page, stream and blobs.
Headless serves register no doc group by themselves — `Gui.serveDocument(doc)` (a
Document object, not a name) is what puts `?doc=` on the map.

## 6. Open decisions

1. **Identity key** — verified email (readable in `user.cfg`, changes when the person's
   email does) or IdP `sub` (stable, unreadable)?
2. **First front door** — self-hosted Caddy + oauth2-proxy (even the triangles stay
   ours) or Cloudflare Access (working in five minutes; sees the display meshes, never
   the parametric model — see §4)? The exposure being mesh-level, not model-level,
   weakens the confidentiality case for self-hosting.
3. **Session token storage** if we keep one — `localStorage` (a reload rejoins as the same
   session; bans stick) or `sessionStorage` (each tab its own session)? The user's
   two-pages-from-one-browser case argues per-tab.
4. **Approval flow** — auto-accept (Live Share's default) or host approval, with anonymous
   (`*` grant, no identity) landing as **view-only** by default?
5. **Expiry / use counts** on grants — every invite-link product offers both; probably not
   needed for a desktop session, single-use possibly worth it.
6. **What else lives in the live list** besides sessions and rename easings — temporary
   bans that never touch disk?
7. **Migration** — convert the current single token into a `*` / `*` grant so links already
   handed out keep working, or start clean and reissue?

## 7. Suggested order of work

1. **Keepalive ping** (§5) — independently useful, small, unblocks any relay. **DONE**
   (with `CF-Connecting-IP` accepted beside `X-Forwarded-For`): the server pings any
   connection 30 s idle on the send side; the browser pongs on its own.
2. **The trusted-identity-header contract** (§4) — loopback-trusted header, identity on
   `SceneClientInfo`, roster shows it. **DONE**: with trust-proxy on, the scene server
   reads `Cf-Access-Authenticated-User-Email` / `X-Auth-Request-Email` /
   `X-Forwarded-Email` (or the one name `FC_SERVE_IDENTITY_HEADER` /
   `setIdentityHeader()` pins); the identity rides `SceneClientInfo`, `Gui.serveClients`,
   and the roster, where it outranks the self-declared label. `scripts/share-edge.sh`
   is the quick setup for each door — cloudflared quick tunnel, Cloudflare Access named
   tunnel, or a generated Caddy + oauth2-proxy pair — all reducing to the same two env
   knobs on our side (`FC_SERVE_TRUST_PROXY=1`, optional `FC_SERVE_TOKEN`).
3. **Grants replace client records** (§2) — persistent list keyed by identity *or* token,
   the same wildcard matching, enforcement moved into the server's authorization gate so
   refusal happens before any scene bytes. **DONE**: `SceneStreamServer::setGrants` is
   the door (most specific match wins, identity > name > address; the token is a filter,
   not a rank, so a ban cannot be outranked by the invitation it revokes); an empty list
   keeps the legacy single-token door for probe rigs. Stored under
   `SceneShare/Grants` with one-time migration of the old token + client records;
   `Gui.serveGrants` / `Gui.serveSetGrants` for scripts. The refusal code is `Refused`
   (viewer stops reconnecting, says the invite does not cover you), distinct from
   `BadToken`.
4. **Live list** (§2) — seeded from enabled grants, rename easings, ban = drop live + keep
   disabled, panel shows live-only rules distinctly. **DONE**: list changes re-judge
   every connection (the eviction moved out of the roster poll into the door); a rename
   no grant covers mints a live-only easing bounded by the invitation it eases; the
   panel shows easings as *this session* with Keep/Drop, grants with
   enable/disable/forget and a 3-state access; roster Ban adds a banned grant keyed on
   verified identity when present. All probe-verified end-to-end (admission, refusal,
   view-only by identity, 403 before bytes, easing, re-judge kick).
5. **Front door** (§4) — Caddy + oauth2-proxy on the linode, replacing
   `scripts/scene-proxy.py`. **DONE** (2026-08-07), but as the *other* door: with no
   domain available, Caddy/Access are out and the cloudflared **quick tunnel** is the
   front door — which is also the normal-user story (one static binary, no account,
   real TLS and a secure context on phones; the token stays the door). This surfaced
   a gap: the backend never actually served the viewer bundle — `FC_BGFX_VIEWER_BUILD`
   was only read for the reload stamp — so the scene server now serves the bundle
   files itself from the same gated origin (extension allowlist, `no-store`, §5.1),
   and one tunnel genuinely carries page, stream and blobs. Verified end-to-end
   through `trycloudflare.com`: 403 before bytes without the token, page + wasm with
   it, `wss://` upgrade, scene bytes flowing. The linode proxy is legacy fallback.
   The Caddy/Access variants stay in `share-edge.sh` for the day a domain exists.
6. **Front-door chooser in the Share dialog** — **DONE** (2026-08-07). The dialog offers
   named door presets (`SceneShare/Doors`, seeded once with *LAN*, *Quick tunnel* and
   the own-door *thundereal* shape): a preset is `{name, mode: lan|quick|own,
   publicOrigin, identityDoor}`, because "how do viewers reach this" is an origin, a
   tunnel and an identity door moving together. LAN keeps today's direct link (short
   `/fcviewer.html?token=…&doc=…` form when the backend can serve the page, the pasted
   viewer-page fallback otherwise). Quick spawns `cloudflared tunnel --url` as a child
   QProcess of the share — the `trycloudflare.com` origin is parsed from its output and
   the tunnel dies with the share (missing binary = download hint on a disabled Start,
   not a broken button). Own carries a public origin (named tunnel / reverse proxy) and
   the *viewers sign in* checkbox: with it on and the token cleared, links are tokenless
   and the grant list decides — the door authenticates, the grants authorize, so no
   tokenless house grant is minted. Tunnel doors force trust-proxy for the session
   without rewriting the LAN preference. The dialog states what each link is (a bearer
   link vs. sign-in + grant list), and the sharing panel grew an invite-by-identity
   quick-add (email → tokenless view/edit grant). No Cloudflare API anywhere — the §4
   identity-header contract stays the whole integration surface. GUI-smoked under xvfb
   both ways: 21-check dialog/panel/LAN pass and a 7-check quick-tunnel pass that
   fetched the page through a real `trycloudflare.com` origin and saw cloudflared die
   with the share.

7. **Probe service token** — **DONE** (2026-08-07). Headless probes were locked out the
   moment the Access app went up (the edge answers scripts with the login redirect), so
   automation authenticates with an Access **service token**: mint under Zero Trust →
   Access → Service credentials, add a **Service Auth** policy (not Allow) to the app
   including it, and the probe sends `CF-Access-Client-Id` / `CF-Access-Client-Secret`
   on every request. Diagnosis note: the login redirect's `meta` JWT carries
   `service_token_status` — `false` with the pair presented means the *token* did not
   validate (wrong team, expired, bad secret), distinct from a valid token that no
   policy admits (403). The pair lives in `~/.config/fc-probe/cf-access` (mode 600),
   never in the repo. **A service token asserts no identity to the origin** — the
   email header is absent, so the probe reaches our door identity-less and a probe
   grant keys on name or token, which is itself the negative test that the sign-in
   contract holds. 11-check suite verified live through `cad.thundereal.com`:
   anonymous 302, page 200 with the pair, `/scene.fcsd` 403 before bytes, `wss://`
   upgrade (on `/scene` — the page path 404s upgrades), ungranted hello `Refused` with
   zero scene bytes, name-granted hello admitted view-only with scene bytes flowing,
   roster row proxied with the real client address, grant list restored.

## 8. Non-goals

- **Authentication we invent ourselves.** Without a front door, a link is a bearer
  capability: "whoever holds this, calling themselves X". It is revocable and scopeable —
  genuinely stronger than one shared secret — but it never proves who is at the other end,
  and the UI must not imply otherwise.
- **Tenancy inside one backend.** Unchanged from MultiDocServe.md §7: a backend is one
  room, and isolation is a process boundary owned by the gateway layer.
