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
#include <vector>

#include "Renderer.h"

namespace Render {

/// A viewer click forwarded for picking: world-space ray + modifiers.
struct ScenePickRequest {
    float origin[3];
    float dir[3];
    uint32_t modifiers = 0;   ///< bit 0 = ctrl (toggle selection)
};

class RendererExport SceneStreamServer {
public:
    static SceneStreamServer &instance();

    /// Start the listener thread on port (once; further calls return
    /// whether it is running).
    bool start(int port);
    bool running() const;

    /// Replace the served payload and bump the version.
    void publish(std::vector<uint8_t> &&payload);

    /// Install the consumer of viewer pick requests. Called on a
    /// server connection thread — the handler must marshal to the GUI
    /// thread itself before touching any scene graph.
    void setPickHandler(std::function<void(const ScenePickRequest &)> handler);

private:
    SceneStreamServer() = default;
    class Private;
    Private *pimpl = nullptr;
    Private *ensure();
};

} // namespace Render

#endif // RENDERER_SCENE_SERVER_H
