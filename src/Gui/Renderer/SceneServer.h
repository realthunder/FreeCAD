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
    std::function<void(const std::string &)> reply;
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

    /// Cap on concurrent on-demand level builds, pushed down from the
    /// LevelThreads render parameter (the renderer layer cannot read
    /// Gui parameters itself). 0 = auto-size; FC_LEVEL_THREADS still
    /// overrides. Applies to workers spawned after the call.
    static void setLevelThreadCap(int n);

    /// Start the listener thread on port (once; further calls return
    /// whether it is running).
    bool start(int port);
    bool running() const;

    /// Which run of this backend the served versions belong to: a
    /// number minted once per process, carried in every payload
    /// (SceneDump.h, v35) and accepted back as `?s=` so a version from
    /// a previous run is not mistaken for one of ours.
    uint64_t sessionId();

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
    uint64_t beginPublish(const void *publisher);

    /// Give the claim back when the publisher goes away, so the next
    /// renderer to come along can take the stream rather than find it
    /// held by something that no longer exists. Ignored unless
    /// \a publisher is the one holding it.
    void endPublish(const void *publisher);

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
    };

    /// Replace the served payload, and remember what this publish
    /// changed so a viewer that is behind can be given the difference
    /// rather than the scene. Also rolls the out-of-band blob
    /// generations: whatever the new payload did not name (nor the one
    /// before it) is dropped.
    void publish(ScenePublish &&pub);

    /// Register one out-of-band payload, addressed by content key and
    /// answered by GET /blob?key= (SceneDump.h, v26). Called by the
    /// serializer's texture sink while building the payload that the
    /// following publish() installs, so the blob is servable before any
    /// viewer can learn its key.
    void publishBlob(const std::string &key, std::vector<uint8_t> &&data);

    /// Declare a blob still in use by the publish being built, without
    /// re-sending its bytes, and report its size. False means it is no
    /// longer stored and has to be published again — that is how a
    /// publisher-side content-key memo learns it went stale
    /// (SceneDump.h, MeshBlobSink).
    bool retainBlob(const std::string &key, uint32_t *size = nullptr);

    /// Queue the generation of a declared level (docs/SceneStreaming.md
    /// §7, phase 5c): \a source is the content key of the exact mesh
    /// chunk, \a level the rung to build, coarsest first. Idempotent —
    /// a repeat of any accepted request is free — and the answer is
    /// never returned here: the finished level is announced by the
    /// next publish naming its key. False when \a source is not a
    /// chunk this server holds, or the level is out of any declarable
    /// range. Also the seam a viewer's GET /level lands on.
    bool requestLevel(const std::string &source, uint32_t level);

    /// The content key of a generated level, or empty while unbuilt.
    /// Retains the chunk for the publish in flight, like retainBlob —
    /// this is what the serializer's MeshBlobSink::built consults, and
    /// the manifest written from its answer is what keeps the chunk
    /// alive thereafter.
    std::string builtLevel(const std::string &source, uint32_t level,
                           uint32_t *size = nullptr);

    /// Levels generated so far, monotonic. The publisher polls it: a
    /// change since the last publish is a reason to publish again,
    /// which is how a finished job becomes an announcement.
    size_t levelsBuilt();

    /// How many connected clients are actual viewers -- ones that
    /// completed the hello handshake, as opposed to a socket a tunnel
    /// or a probe is holding open. Zero means nothing is watching what
    /// this process publishes, which is what tells a serving process
    /// not to keep animating for nobody (docs/SceneStreaming.md).
    size_t viewerCount();

    /// Install the consumer of viewer pick requests. Called on a
    /// server connection thread — the handler must marshal to the GUI
    /// thread itself before touching any scene graph.
    void setPickHandler(std::function<void(const ScenePickRequest &)> handler);

    /// Install the consumer of semantic control requests — the `"op"`
    /// JSON vocabulary of the property/operation channel
    /// (docs/ThinClient.md §4.2). Called on a server connection thread;
    /// the handler must marshal to the GUI thread itself before
    /// touching the document, and answers through the request's own
    /// reply hook. No handler installed = every op answers with a
    /// structured error.
    void setControlHandler(
            std::function<void(SceneControlRequest &&)> handler);

    /// Install the publisher's cue that queued work finished (a level
    /// was generated): without it an idle backend sits on finished
    /// work, because the publish that would announce it lives in the
    /// render path and nothing else asks for a frame. Called on the
    /// worker thread — the handler must marshal itself.
    void setWorkNotifier(std::function<void()> notifier);

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
