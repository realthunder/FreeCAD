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
   `scripts/scene-proxy.py`.

## 8. Non-goals

- **Authentication we invent ourselves.** Without a front door, a link is a bearer
  capability: "whoever holds this, calling themselves X". It is revocable and scopeable —
  genuinely stronger than one shared secret — but it never proves who is at the other end,
  and the UI must not imply otherwise.
- **Tenancy inside one backend.** Unchanged from MultiDocServe.md §7: a backend is one
  room, and isolation is a process boundary owned by the gateway layer.
