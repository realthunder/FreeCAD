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

namespace Render {

/// A viewer click forwarded for picking: world-space ray + modifiers.
struct ScenePickRequest {
    float origin[3];
    float dir[3];
    uint32_t modifiers = 0;   ///< bit 0 = ctrl (toggle selection)
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

    /// Start the listener thread on port (once; further calls return
    /// whether it is running).
    bool start(int port);
    bool running() const;

    /// Replace the served payload and bump the version. Also rolls the
    /// out-of-band blob generations: whatever the new payload did not
    /// name (nor the one before it) is dropped.
    void publish(std::vector<uint8_t> &&payload);

    /// Register one out-of-band payload, addressed by content key and
    /// answered by GET /blob?key= (SceneDump.h, v26). Called by the
    /// serializer's texture sink while building the payload that the
    /// following publish() installs, so the blob is servable before any
    /// viewer can learn its key.
    void publishBlob(const std::string &key, std::vector<uint8_t> &&data);

    /// Install the consumer of viewer pick requests. Called on a
    /// server connection thread — the handler must marshal to the GUI
    /// thread itself before touching any scene graph.
    void setPickHandler(std::function<void(const ScenePickRequest &)> handler);

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

private:
    SceneStreamServer() = default;
    class Private;
    Private *pimpl = nullptr;
    Private *ensure();
};

} // namespace Render

#endif // RENDERER_SCENE_SERVER_H
