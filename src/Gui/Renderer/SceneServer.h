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

/// Minimal HTTP server publishing the latest serialized scene snapshot
/// to the standalone/WebAssembly viewer (the live-streaming transport).
/// The desktop bridge publishes a new payload whenever a backend feed
/// changes (FC_BGFX_SERVE_SCENE=<port>); the viewer polls
///   GET /scene?v=<last-seen-version>
/// which answers 204 while unchanged, else 200 with an 8-byte
/// little-endian version followed by the SceneDump payload. Responses
/// carry Access-Control-Allow-Origin: * so the page can be served from
/// any origin. POSIX sockets; start() fails gracefully on Windows.

#include <cstdint>
#include <vector>

#include "Renderer.h"

namespace Render {

class RendererExport SceneStreamServer {
public:
    static SceneStreamServer &instance();

    /// Start the listener thread on port (once; further calls return
    /// whether it is running).
    bool start(int port);
    bool running() const;

    /// Replace the served payload and bump the version.
    void publish(std::vector<uint8_t> &&payload);

private:
    SceneStreamServer() = default;
    class Private;
    Private *pimpl = nullptr;
};

} // namespace Render

#endif // RENDERER_SCENE_SERVER_H
