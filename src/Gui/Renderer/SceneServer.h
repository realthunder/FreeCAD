/****************************************************************************
 *   Copyright (c) 2026 Zheng Lei (realthunder) <realthunder.dev@gmail.com> *
 *                                                                          *
 *   This file is part of the FreeCAD CAx development system.               *
 *                                                                          *
 *   This library is free software; you can redistribute it and/or          *
 *   modify it under the terms of the GNU Library General Public            *
 *   License as published by the Free Software Foundation; either           *
 *   version 2 of the License, or (at your option) any later version.       *
 *                                                                          *
 *   This library  is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of         *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the          *
 *   GNU Library General Public License for more details.                   *
 *                                                                          *
 *   You should have received a copy of the GNU Library General Public      *
 *   License along with this library; see the file COPYING.LIB. If not,     *
 *   write to the Free Software Foundation, Inc., 59 Temple Place,          *
 *   Suite 330, Boston, MA  02111-1307, USA                                 *
 ****************************************************************************/

#ifndef RENDERER_SCENE_SERVER_H
#define RENDERER_SCENE_SERVER_H

/// Minimal scene-streaming server publishing the latest serialized
/// scene snapshot to the standalone/WebAssembly viewer. The desktop
/// bridge publishes a new payload whenever a backend feed changes
/// (FC_BGFX_SERVE_SCENE=<port>).
///
/// Primary transport: WebSocket. A GET /scene request carrying an
/// Upgrade: websocket header is answered with an RFC 6455 handshake;
/// the connection then receives a binary frame of 8-byte little-endian
/// version + SceneDump payload immediately and on every publish().
/// Client frames carry viewer events back — currently the pick request
/// ('P', flags byte, six little-endian floats: world ray origin +
/// direction) dispatched to the installed pick handler.
///
/// Fallback transport: plain HTTP polling. GET /scene?v=<last-seen>
/// answers 204 while unchanged, else 200 with the same version-prefixed
/// payload. Responses carry Access-Control-Allow-Origin: * so the page
/// can be served from any origin. POSIX sockets; start() fails
/// gracefully on Windows.

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "Renderer.h"
#include "SceneDump.h"

namespace Render {

/// A viewer click forwarded for picking: world-space ray + modifiers.
struct ScenePickRequest {
    float origin[3];
    float dir[3];
    uint32_t modifiers = 0;   ///< bit 0 = ctrl (toggle selection)
};

/// One semantic control request from a viewer (docs/ThinClient.md
/// §4.2): the raw JSON text of an `"op"` message, and a thread-safe way
/// to answer the connection it arrived on. `reply` may be called from
/// any thread, once or not at all; the answer is queued for the
/// connection's own send loop (and silently dropped if the viewer has
/// disconnected meanwhile — the request `id` correlation makes a lost
/// answer a timeout, never a mixup).
struct SceneControlRequest {
    std::string json;
    /// The connection is view-only (docs/MultiDocServe.md §8): the
    /// handler must refuse anything that mutates the document. Carried
    /// on the request rather than enforced here because only the
    /// semantic layer knows which ops write.
    bool viewOnly = false;
    /// The connection it arrived on (SceneClientInfo::id), for a
    /// handler that keeps per-connection state -- a served viewport
    /// (docs/CyclesIntegration.md sec 7.1) -- and answers it later
    /// through sendControl/sendBinary.
    uint64_t client = 0;
    std::function<void(const std::string &)> reply;
};

/// One entry of the door's grant list (docs/ShareAccess.md §2): an
/// invitation, and who may use it. Every field but the token is a
/// shell-wildcard pattern (`*` anything, `?` one character; empty =
/// `*`). A connection is admitted iff some grant matches everything it
/// presented — its token (exact; a grant with an empty token requires
/// none), its verified identity (§4), its self-declared name, its
/// address without the port — and the **most specific** match decides
/// the access: identity outranks name outranks address (the verified
/// part first, then what a person chose, then where they happen to
/// be). No match, or a best match that is banned, is refused **before
/// any scene bytes**.
struct SceneGrant {
    /// Server-assigned handle, for management and for correlating a
    /// connection with the grant that admitted it. 0 on input;
    /// setGrants/addGrant assign one.
    uint64_t id = 0;
    std::string token;     ///< invitation secret, exact; empty = none required
    std::string identity;  ///< pattern on the verified identity
    std::string client;    ///< pattern on the self-declared name
    std::string address;   ///< pattern on the address, matched portless
    int access = 0;        ///< 0 = edit, 1 = view-only, 2 = banned
    /// Exists only in this run and is never persisted — the rename
    /// easings of docs/ShareAccess.md §2, minted by the server itself
    /// so a renamed client can reconnect; the panel shows them apart,
    /// with a way to keep or drop them.
    bool liveOnly = false;
};

/// One connected viewer as the sharing UI sees it (docs/MultiDocServe.md
/// §8): identity for the roster, and the per-connection mode the host
/// may change.
struct SceneClientInfo {
    uint64_t id = 0;          ///< stable connection id, never reused
    std::string client;       ///< display label from the hello, may be empty
    std::string doc;          ///< joined document name, empty = default/none
    /// Where this client is, as well as the server can know: the
    /// forwarded address when a trusted proxy stated one (setTrustProxy),
    /// else the socket peer. \a peer always carries the socket peer, so
    /// a proxied row can still show what it came through — and every
    /// row stays unique, since the peer carries a port.
    std::string address;
    std::string peer;
    bool proxied = false;     ///< address came from a forwarded header
    /// Who this is, as verified by an authenticating front door
    /// (docs/ShareAccess.md §4): the identity header's value on the
    /// upgrade request, believed under the same loopback rule as the
    /// forwarded address. Empty when nothing asserted one — then the
    /// self-declared \a client label is all there is.
    std::string identity;
    bool viewer = false;      ///< sent a hello (a probe may not)
    bool viewOnly = false;    ///< picks and mutating ops refused
    uint64_t connectedMs = 0; ///< how long this connection has been up
    /// The grant that admitted this connection (SceneGrant::id), 0
    /// under the legacy single-token door or while unauthorized.
    uint64_t grant = 0;
};

/// One viewer's answer to a dumpFrame control request
/// (docs/RenderDebug.md §4.4): its canvas pixels read back in-page on
/// the device's real GPU, plus the viewer's own metadata JSON (canvas
/// size, WEBGL_debug_renderer_info, applied RenderDebug state).
struct ViewerFrameDump {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> rgba;   ///< tightly packed RGBA8, bottom-up rows
    std::string meta;            ///< viewer-supplied JSON (may be empty)
};

class RendererExport SceneStreamServer {
public:
    static SceneStreamServer &instance();

    /// Every entry point below takes an optional document name — the
    /// group key of docs/MultiDocServe.md §3. Empty (the default) means
    /// the *default group*: the first group anything created, which is
    /// how the single-document callers keep working unnamed. A named
    /// group is created on first use.

    /// Cap on concurrent on-demand level builds, pushed down from the
    /// LevelThreads render parameter (the renderer layer cannot read
    /// Gui parameters itself). 0 = auto-size; FC_LEVEL_THREADS still
    /// overrides. Applies to workers spawned after the call.
    static void setLevelThreadCap(int n);

    /// Start the listener thread on port (once; further calls return
    /// whether it is running).
    bool start(int port);
    bool running() const;

    /// Stop the listener and disconnect every client. Served groups
    /// keep their state — a later start() serves them again — but no
    /// connection survives and no new one is accepted. The desktop
    /// "stop sharing" path (docs/MultiDocServe.md §8).
    void stop();

    /// The shared door secret (docs/MultiDocServe.md §4/§8). Non-empty
    /// = every endpoint is gated: HTTP requests and the WebSocket
    /// upgrade must carry a matching `?token=`, and a connection whose
    /// upgrade did not may still authorize itself with the token in
    /// its hello — until then it gets no scene bytes and no verb
    /// works. Empty (the default) = open, exactly today's behavior.
    /// FC_SERVE_TOKEN presets it at first use.
    void setToken(const std::string &token);
    std::string token();

    /// Believe `X-Forwarded-For` when the connection came from a
    /// loopback peer — which is what a reverse proxy on this machine,
    /// or the local end of an ssh -R tunnel, looks like. Off by
    /// default, and deliberately so: a header is only as trustworthy
    /// as whoever set it, so a directly reachable server that honored
    /// it would show whatever address the client cared to claim.
    /// FC_SERVE_TRUST_PROXY=1 presets it.
    ///
    /// A reverse tunnel alone cannot carry the client's address (the
    /// local ssh client opens its own connection), so this is only
    /// useful with a proxy in front that sets the header.
    void setTrustProxy(bool on);
    bool trustProxy();

    /// The admitted-connection caps (docs/SceneServerPort.md sec 7.4):
    /// how many distinct users may be connected at once, and how many
    /// connections each may hold. A user is the identity a trusted
    /// front door asserted, else the grant that admitted the
    /// connection, else the judged address -- so a gateway on another
    /// host is not one user for everybody behind it, as the old
    /// per-peer cap made it. An anonymous connection judged by a
    /// loopback address (the legacy door behind a same-box tunnel,
    /// where every remote client looks the same) is not counted.
    /// Defaults 64 and 16; the pre-auth accept caps stay underneath.
    void setConnectionCaps(int users, int perUser);

    /// The request header carrying a verified identity from an
    /// authenticating front door (docs/ShareAccess.md §4). Read only
    /// under the trust rule of setTrustProxy — trust on, loopback
    /// peer — because a header is an assertion by whoever set it.
    /// Empty (the default) recognizes the well-known front doors:
    /// `Cf-Access-Authenticated-User-Email` (Cloudflare Access),
    /// `X-Auth-Request-Email` (oauth2-proxy), `X-Forwarded-Email`
    /// (ngrok and others) — which is what keeps the front door
    /// swappable. A configured name becomes the only one read.
    /// FC_SERVE_IDENTITY_HEADER presets it at first use.
    void setIdentityHeader(const std::string &name);
    std::string identityHeader();

    /// Replace the live grant list (docs/ShareAccess.md §2) — the door
    /// itself. Seeded from the enabled persistent grants when sharing
    /// starts, free to evolve at runtime, gone with the process. A
    /// non-empty list gates every endpoint by grant match and the
    /// shared token of setToken() stops mattering; empty (the default)
    /// falls back to the single-token door, which is what keeps the
    /// env-token probe setups working unchanged. Every connection is
    /// re-judged against the new list: one that no grant now admits is
    /// refused and closed, one a different grant admits gets that
    /// grant's access. Entries with id 0 are assigned one; grants()
    /// returns the stored list including the ids and any live-only
    /// easings the server has minted since.
    void setGrants(const std::vector<SceneGrant> &list);
    std::vector<SceneGrant> grants();
    /// Add one grant to the live list (a rule made in the panel while
    /// sharing runs). Returns its assigned id.
    uint64_t addGrant(SceneGrant grant);
    /// Drop one grant from the live list; connections it admitted are
    /// re-judged — which is the "ban = drop live, disable stored" move
    /// of docs/ShareAccess.md §2. False when the id is unknown.
    bool removeGrant(uint64_t id);

    /// The connected clients, for the sharing roster. Returns how many.
    int clients(std::vector<SceneClientInfo> &out);

    /// Make the identified connection view-only (or full again):
    /// view-only clients still receive every publish, but their picks
    /// are dropped and their mutating control ops answered with a
    /// ViewOnly error. False when the connection is gone.
    bool setClientViewOnly(uint64_t id, bool viewOnly);

    /// Disconnect the identified client: it is told
    /// {"cmd":"error","code":"Kicked"} and closed by its own loop. Not
    /// a ban — changing the token is — but a compliant viewer stops
    /// reconnecting when told. False when the connection is gone.
    bool kickClient(uint64_t id);

    /// Install the sharing UI's cue that the roster changed (connect,
    /// disconnect, hello, document switch, mode change). Called on a
    /// server thread — the handler must marshal itself.
    void setClientsChangedNotifier(std::function<void()> notifier);

    /// Which run of this backend the served versions belong to: a
    /// number minted once per process, carried in every payload
    /// (SceneDump.h, v35) and accepted back as `?s=` so a version from
    /// a previous run is not mistaken for one of ours.
    uint64_t sessionId(const std::string &doc = {});

    /// Claim the stream and take the version of the publish about to be
    /// serialized. The version has to be known before the payload is
    /// built, because it is written into it.
    ///
    /// \a publisher identifies the caller (its own address will do).
    /// The first one to ask owns the stream and every later caller gets
    /// 0, meaning "do not publish": two renderers alternating payloads
    /// would already be serving two different scenes down one
    /// connection, and once publishes are deltas against each other it
    /// would be incoherent rather than merely confusing.
    uint64_t beginPublish(const void *publisher,
                          const std::string &doc = {});

    /// Give the claim back when the publisher goes away, so the next
    /// renderer to come along can take the stream rather than find it
    /// held by something that no longer exists. Ignored unless
    /// \a publisher is the one holding it.
    void endPublish(const void *publisher, const std::string &doc = {});

    /// One publish as the server needs to hold it.
    struct ScenePublish {
        uint64_t version = 0;   ///< what beginPublish() returned
        /// The root, always with a complete object list. What a viewer
        /// that is behind gets instead is derived from this, not
        /// serialized separately: the producer publishes once.
        std::vector<uint8_t> payload;
        SceneSnapshot::RootSpans spans;
        /// This publish against the one before it — the entry of every
        /// object whose group manifest key moved, and the keys of the
        /// ones that went away. A viewer that missed publishes is
        /// caught up from a merge of these.
        std::vector<SceneSnapshot::ObjectEntry> changed;
        std::vector<uint64_t> removed;
        /// How many objects the payload's list carries — what the
        /// `docs` document listing reports per served document
        /// (docs/MultiDocServe.md §4).
        size_t objects = 0;
    };

    /// Replace the served payload, and remember what this publish
    /// changed so a viewer that is behind can be given the difference
    /// rather than the scene. Also rolls the out-of-band blob
    /// generations: whatever the new payload did not name (nor the one
    /// before it) is dropped.
    void publish(ScenePublish &&pub, const std::string &doc = {});

    /// Register one out-of-band payload, addressed by content key and
    /// answered by GET /blob?key= (SceneDump.h, v26). Called by the
    /// serializer's texture sink while building the payload that the
    /// following publish() installs, so the blob is servable before any
    /// viewer can learn its key.
    void publishBlob(const std::string &key, std::vector<uint8_t> &&data,
                     const std::string &doc = {});

    /// Declare a blob still in use by the publish being built, without
    /// re-sending its bytes, and report its size. False means it is no
    /// longer stored and has to be published again — that is how a
    /// publisher-side content-key memo learns it went stale
    /// (SceneDump.h, MeshBlobSink).
    bool retainBlob(const std::string &key, uint32_t *size = nullptr,
                    const std::string &doc = {});

    /// Queue the generation of a declared level (docs/SceneStreaming.md
    /// §7, phase 5c): \a source is the content key of the exact mesh
    /// chunk, \a level the rung to build, coarsest first. Idempotent —
    /// a repeat of any accepted request is free — and the answer is
    /// never returned here: the finished level is announced by the
    /// next publish naming its key. False when \a source is not a
    /// chunk this server holds, or the level is out of any declarable
    /// range. Also the seam a viewer's GET /level lands on.
    bool requestLevel(const std::string &source, uint32_t level,
                      const std::string &doc = {});

    /// The content key of a generated level, or empty while unbuilt.
    /// Retains the chunk for the publish in flight, like retainBlob —
    /// this is what the serializer's MeshBlobSink::built consults, and
    /// the manifest written from its answer is what keeps the chunk
    /// alive thereafter.
    std::string builtLevel(const std::string &source, uint32_t level,
                           uint32_t *size = nullptr,
                           const std::string &doc = {});

    /// Levels generated so far, monotonic. The publisher polls it: a
    /// change since the last publish is a reason to publish again,
    /// which is how a finished job becomes an announcement.
    size_t levelsBuilt(const std::string &doc = {});

    /// Install the consumer of viewer pick requests. Called on a
    /// server connection thread — the handler must marshal to the GUI
    /// thread itself before touching any scene graph.
    void setPickHandler(std::function<void(const ScenePickRequest &)> handler,
                        const std::string &doc = {});

    /// Install the consumer of semantic control requests — the `"op"`
    /// JSON vocabulary of the property/operation channel
    /// (docs/ThinClient.md §4.2). Called on a server connection thread;
    /// the handler must marshal to the GUI thread itself before
    /// touching the document, and answers through the request's own
    /// reply hook. No handler installed = every op answers with a
    /// structured error.
    void setControlHandler(
            std::function<void(SceneControlRequest &&)> handler,
            const std::string &doc = {});

    /// Install the publisher's cue that queued work finished (a level
    /// was generated): without it an idle backend sits on finished
    /// work, because the publish that would announce it lives in the
    /// render path and nothing else asks for a frame. Called on the
    /// worker thread — the handler must marshal itself.
    void setWorkNotifier(std::function<void()> notifier,
                         const std::string &doc = {});
    /// Install the cue that one of this document's connections closed
    /// (its SceneClientInfo::id), for a source holding per-connection
    /// state (docs/CyclesIntegration.md sec 7.1). Called on the
    /// connection's own thread as it leaves -- the handler must
    /// marshal itself.
    void setClientClosedHandler(std::function<void(uint64_t)> handler,
                                const std::string &doc = {});
    /// Queue a JSON control message to ONE connection, by id; false
    /// when it is gone. Any thread.
    bool sendControl(uint64_t client, const std::string &json);
    /// Queue a binary message to ONE connection, by id -- a streamed
    /// frame (FrameStreamWire.h); false when it is gone. Sent by the
    /// connection's own loop after the scene bytes and the queued
    /// texts. A message of the same kind still queued is replaced:
    /// a frame is a state, not an event, and a slow link should see
    /// the newest one. Any thread.
    bool sendBinary(uint64_t client, std::vector<uint8_t> &&data);

    /// Declare \a doc served: give its group the display label the
    /// `docs` document listing shows, mark it joinable by name on the
    /// wire (docs/MultiDocServe.md §4), and push the updated listing
    /// to every connected viewer — the push a viewer's document menu
    /// redraws from. Called by the document's source once it has
    /// installed its handlers; releaseGroup() is the other half.
    void setDocumentInfo(const std::string &doc, const std::string &label);

    /// A served document went away: clear the group's publisher claim
    /// and handler slots, purge its queued level jobs, and take it off
    /// the wire — the updated document listing is pushed, and each of
    /// its connections re-homes to the default document (or to
    /// nothing, with an error text, when it was the last) on its own
    /// loop's next tick. The group node itself stays — connections may
    /// still point at it — and keeps its last payload; re-serving the
    /// same document reuses it.
    void releaseGroup(const std::string &doc);

    /// Queue a JSON control message (WebSocket text frame) to every
    /// connected viewer — the browser side of the debug/capture
    /// protocol (docs/RenderDebug.md §4.4): dumpFrame, reload. Sent by
    /// each connection's own push loop (within its poll interval).
    void broadcastControl(const std::string &json);

    /// Push a dumpFrame request to every connected viewer and block
    /// until each answered or \a timeoutMs elapsed; partial results
    /// are returned on timeout. \a mode >= 0 asks the viewers to
    /// capture with that RenderDebug view mode for the dumped frame.
    /// Returns the number of dumps collected (0 when no viewer is
    /// connected, or when another collection is still in flight).
    int requestFrameDumps(int mode, int timeoutMs,
                          std::vector<ViewerFrameDump> &dumps);

    /// Pull every connected viewer's decision journal — the plan,
    /// fetch and release history it keeps locally with the reason for
    /// each move (docs/SceneStreaming.md §7). One string per viewer;
    /// partial results on timeout, same contract as the frame dumps.
    /// Also served over HTTP as GET /decisions.
    int requestDecisionLogs(int timeoutMs, std::vector<std::string> &logs);

private:
    SceneStreamServer() = default;
    class Private;
    Private *pimpl = nullptr;
    Private *ensure();
};

} // namespace Render

#endif // RENDERER_SCENE_SERVER_H
