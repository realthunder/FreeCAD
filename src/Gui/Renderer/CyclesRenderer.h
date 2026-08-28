/***************************************************************************
 *   Copyright (c) 2026 Zheng Lei (realthunder) <realthunder.dev@gmail.com>*
 *                                                                         *
 *   This file is part of the FreeCAD CAx development system.              *
 *                                                                         *
 *   This library is free software; you can redistribute it and/or         *
 *   modify it under the terms of the GNU Library General Public           *
 *   License as published by the Free Software Foundation; either          *
 *   version 2 of the License, or (at your option) any later version.      *
 *                                                                         *
 *   This library  is distributed in the hope that it will be useful,      *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU Library General Public License for more details.                  *
 *                                                                         *
 *   You should have received a copy of the GNU Library General Public     *
 *   License along with this library; see the file COPYING.LIB. If not,    *
 *   write to the Free Software Foundation, Inc., 59 Temple Place,         *
 *   Suite 330, Boston, MA  02111-1307, USA                                *
 *                                                                         *
 ***************************************************************************/

#ifndef RENDER_CYCLES_RENDERER_H
#define RENDER_CYCLES_RENDERER_H

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "Renderer.h"

/// The Cycles path tracer (docs/CyclesIntegration.md), vendored at
/// src/3rdParty/cycles and built when BUILD_CYCLES is on.
///
/// Nothing here names a Cycles type: the header is the same with or
/// without the engine, and a build without it answers available()
/// false and fails the rest with a message. Nothing here names a Gui
/// type either -- the rule from the doc's section 7, so that the whole
/// unit can move to a server process later.
namespace Render::Cycles {

/// One compute device Cycles can render on.
struct DeviceInfo {
    std::string type;         ///< "CPU", "CUDA", "OPTIX", "HIP", ...
    std::string description;  ///< the device's own name
};

/// Whether this build carries the engine.
RendererExport bool available();

/// The devices the engine can see, CPU included. Empty without the
/// engine.
RendererExport std::vector<DeviceInfo> devices();

/// The camera of a render: the same two GL-layout matrices every
/// backend's render() takes (world -> eye, then eye -> clip), plus the
/// pixel size they were built for. Perspective or orthographic is read
/// off the projection.
struct CameraInput {
    float view[16];
    float proj[16];
    int width = 0;
    int height = 0;
};

/// What the translation layer reads (docs/CyclesIntegration.md sec 6
/// and 7): the backend-neutral draw list every backend is fed, the
/// per-frame configs that describe how it looks, and the camera. No
/// Gui type anywhere, so the whole thing can be produced on a server
/// and rendered elsewhere.
struct SceneInput {
    DrawCallList draws;
    PBRConfig pbr;
    OutputConfig output;
    LightConfig light;
    Background background;
    CameraInput camera;
};

/// What the translation made of a SceneInput, for the caller to
/// report (and a probe to assert on).
struct RenderReport {
    int meshes = 0;      ///< distinct Cycles meshes (shared by instances)
    int objects = 0;     ///< instances placed
    int shaders = 0;     ///< distinct surface shaders
    long triangles = 0;  ///< triangles across the distinct meshes
    int skipped = 0;     ///< draws the translation has no use for (lines,
                         ///< points, gizmos, effect volumes, wireframes)
    double seconds = 0;  ///< wall time of the render itself
};

/// Phase 3 of the plan: translate \a scene and path trace it to a PNG
/// at \a path with \a samples per pixel on the device of the given
/// type. The image is composited over the scene's background (the
/// environment where PBRConfig::envBackground asks for it, else the
/// flat or gradient Background) and encoded once for the file. Blocks
/// until the render is done. On failure returns false with \a error
/// set.
RendererExport bool renderScene(const SceneInput &scene,
                                const std::string &path,
                                int samples,
                                const std::string &deviceType,
                                std::string *error,
                                RenderReport *report = nullptr);

/// How the viewport session renders (docs/CyclesIntegration.md sec
/// 5.2 and step 11 of sec 8).
struct ViewportOptions {
    std::string device = "CPU";  ///< device type, as devices() names it
    int samples = 256;           ///< samples per pixel before it rests
    double timeLimit = 0.0;      ///< seconds per render, 0 = none
    bool denoise = false;        ///< OpenImageDenoise on the result
};

/// What the viewport session is doing, for a status line or a probe.
struct ViewportStatus {
    bool running = false;    ///< a session exists (a scene was set)
    float progress = 0.0f;   ///< 0..1 of the sample budget
    std::string status;      ///< the engine's own status text
    std::string error;       ///< non-empty when the session failed
    RenderReport report;     ///< of the last scene translation
};

/// Phase 4 of the plan: the viewport. A FrameConsumer that runs a
/// Cycles session in interactive mode against a scene it is fed, keeps
/// the progressively refined frame in a staging buffer that Cycles'
/// threads fill through its DisplayDriver, and blits that frame over
/// the host's scene target in drawFrame -- one quad, depth test and
/// write off, premultiplied, linear light handed to the host's output
/// transform (sec 5.2 and sec 6.1).
///
/// The host (a 3D view) owns the feed and the frame: it calls
/// setScene() whenever the render cache restates the scene or a
/// setting changes, setCamera() whenever the camera or the pixel size
/// moves, registers the consumer with its backend, and repaints when
/// the redraw callback asks -- which Cycles' threads invoke, so the
/// host must marshal that to its own thread. Nothing here names a Gui
/// type (sec 7).
class RendererExport Viewport : public FrameConsumer
{
public:
    /// A viewport on the device the options name, or null with \a error
    /// set (no engine in this build, unknown or absent device).
    static std::unique_ptr<Viewport> create(const ViewportOptions &options,
                                            std::string *error);
    ~Viewport() override;

    /// Restate the whole scene, camera included. Starts (or restarts)
    /// the render. The translation runs on the calling thread.
    virtual void setScene(const SceneInput &input) = 0;
    /// Move the camera or resize; the scene stays. Cheap: the session
    /// restarts sampling from the coarse resolution divider. Throttled
    /// the way Blender throttles it -- a move that arrives before the
    /// previous restart has drawn one frame is held until it has.
    virtual void setCamera(const CameraInput &camera) = 0;
    /// Hold the session (no sampling) or let it run.
    virtual void setPaused(bool paused) = 0;
    /// Called from the engine's threads whenever a newer frame is
    /// staged, so the host repaints and drawFrame picks it up.
    virtual void setRedrawCallback(std::function<void()> callback) = 0;
    virtual ViewportStatus status() const = 0;
};

/// Phase 2 of the plan: render a hard-coded scene (a cube on a floor
/// under a uniform sky) to a PNG at \a path, with \a samples per pixel
/// on the device of the given type. Proves that the engine starts a
/// session, runs its threads, hands a frame back and tears down inside
/// this process -- nothing about the document is involved. Blocks
/// until the render is done. On failure returns false with \a error
/// set.
RendererExport bool renderTestScene(const std::string &path,
                                    int width,
                                    int height,
                                    int samples,
                                    const std::string &deviceType,
                                    std::string *error);

}  // namespace Render::Cycles

#endif  // RENDER_CYCLES_RENDERER_H
