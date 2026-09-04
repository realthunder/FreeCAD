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

/// Wait for every session retired by a destroyed or restarted Viewport
/// to finish being destroyed. A session is not torn down where it is
/// released -- `~Session` joins its own thread, which may be inside a
/// first-ever GPU kernel compile that nothing interrupts, and that
/// would be the GUI thread waiting minutes -- so a worker does it. The
/// application calls this once on its way out, where the alternative
/// is a thread still in the engine while the process unloads it.
/// Returns at once without the engine, or with nothing retired.
RendererExport void waitForRetiredSessions();

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
    /// The bump map treatment of the raster path (Render_BumpScale).
    /// Parallax has no meaning to a path tracer and is not read.
    BumpConfig bump;
    OutputConfig output;
    LightConfig light;
    Background background;
    CameraInput camera;
    /// How a section is filled (docs/CyclesIntegration.md sec 6.4).
    /// The planes themselves ride the draws' materials; this is the
    /// frame-level style the caps are built to.
    SectionConfig section;
    /// The raster path's buffer visualization (RenderParams
    /// DebugViewMode, docs/RenderDebug.md sec 2.3), honoured here where
    /// a path tracer has the same quantity to show: 2 = the view-space
    /// shading normal as n * 0.5 + 0.5, written raw, over black -- the
    /// one picture in which the two engines can be compared exactly,
    /// which is what the finish and map probes assert on. Any other
    /// value renders normally.
    int debugView = 0;
};

/// What the translation made of a SceneInput, for the caller to
/// report (and a probe to assert on).
struct RenderReport {
    int meshes = 0;      ///< distinct Cycles meshes (shared by instances)
    int objects = 0;     ///< instances placed
    int shaders = 0;     ///< distinct surface shaders
    long triangles = 0;  ///< triangles across the distinct meshes
    int images = 0;      ///< image texture nodes in the shaders (the maps
                         ///< of docs/CyclesIntegration.md sec 6.5)
    int skipped = 0;     ///< draws the translation has no use for (lines,
                         ///< points, gizmos, effect volumes, wireframes)
    double seconds = 0;  ///< wall time of the render itself
    /// What the translation DID to get there (docs/CyclesIntegration.md
    /// sec 5.10): a fresh scene counts everything as added and built,
    /// a restate counts only the difference.
    int added = 0;       ///< objects created
    int removed = 0;     ///< objects deleted (no draw claimed them)
    int restated = 0;    ///< kept objects whose transform or colour changed
    int built = 0;       ///< meshes translated
    int released = 0;    ///< meshes deleted (no object references them)
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
    bool denoise = true;         ///< OpenImageDenoise (fast quality) on
                                 ///< the refining result
    int pixelSize = 1;           ///< render at 1/n resolution and scale
                                 ///< up (Blender's preview pixel size)
};

/// What the viewport session is doing, for a status line or a probe.
struct ViewportStatus {
    bool running = false;    ///< a session exists (a scene was set)
    float progress = 0.0f;   ///< 0..1 of the sample budget
    std::string status;      ///< the engine's own status text
    std::string error;       ///< non-empty when the session failed
    RenderReport report;     ///< of the scene as last translated
    int sessions = 0;        ///< sessions started (device set up)
    int updates = 0;         ///< scenes restated in place under one
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

    /// State the whole scene, camera included. The first call starts
    /// the session; later ones restate the running session's scene in
    /// place and restart its sampling only if something changed
    /// (docs/CyclesIntegration.md sec 5.10). The translation runs on
    /// the calling thread.
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

    /// For a host that draws nothing itself (a server streaming the
    /// frame to a viewer, sec 7.1): hand the staged frame to \a fn if
    /// a newer one arrived since the last take -- premultiplied linear
    /// half4, bottom-up, \a width as the pitch -- and do the session's
    /// draw accounting drawFrame() would have done (the reset throttle
    /// and a held camera move). Returns whether \a fn was called. Any
    /// thread, serialized by the caller against setScene/setCamera.
    virtual bool takeFrame(
            const std::function<void(const void *half4, int width, int height)> &fn) = 0;
};

/// How a served viewport renders and how its frames travel
/// (docs/CyclesIntegration.md sec 7.1).
struct StreamOptions {
    ViewportOptions viewport;
    int cell = 0;                ///< the viewer's sub-view the frames are
                                 ///< for (0 = its full canvas); rides
                                 ///< every frame's header
    int quality = 85;            ///< JPEG quality, 1..100
    int minIntervalMs = 100;     ///< least time between two frames sent
    long maxPixels = 1920L * 1080L;  ///< the render size cap (aspect kept)
    int maxStreams = 0;          ///< how many streams this process may
                                 ///< have alive at once, this one
                                 ///< included; 0 = no cap. The server's
                                 ///< policy, stated at every start.
};

/// Phase 5 of the plan: a Viewport whose frames go to a remote viewer
/// instead of a blit. Owns the Viewport, the viewer's camera, and an
/// encoder thread that the session's updates wake: it takes the newest
/// staged frame, composites it over the scene's background (the film
/// is transparent where the environment is not seen, and the wire
/// carries no alpha), encodes it, and hands the message to \a send.
/// The scene comes from the publisher's thread (setScene), the camera
/// from wherever the viewer's message lands (setCamera); both are
/// serialized here. Nothing here names a Gui type (sec 7).
class RendererExport FrameStream
{
protected:
    FrameStream();

public:
    /// \a send delivers one wire message (FrameStreamWire.h) to the
    /// viewer from the encoder thread and answers false once the
    /// viewer is gone -- the stream then stops encoding and reports
    /// lost(). \a notify carries unsolicited JSON events (an engine
    /// error) the same way. Null with \a error set when the engine or
    /// the device is absent.
    static std::unique_ptr<FrameStream> create(
            const StreamOptions &options,
            std::function<bool(std::vector<uint8_t> &&)> send,
            std::function<void(const std::string &)> notify,
            std::string *error);
    /// Streams alive in this process, across every source and every
    /// connection: what StreamOptions::maxStreams caps. A stream
    /// counts from its construction to its destruction, and the
    /// session it hands to the reaper on the way out (sec 5.12) is
    /// already uncounted while that session still holds its device.
    static int liveCount();
    virtual ~FrameStream();

    /// State the scene; its camera is ignored once the viewer has
    /// stated one. Publisher's thread.
    virtual void setScene(const SceneInput &input) = 0;
    /// The viewer's camera and canvas size (device pixels); the render
    /// size is this capped to StreamOptions::maxPixels. Any thread.
    virtual void setCamera(const CameraInput &camera) = 0;
    virtual ViewportStatus status() const = 0;
    /// The viewer is gone (send answered false) or the session failed.
    virtual bool lost() const = 0;
};

/// A staged viewport frame -- premultiplied linear half4, bottom-up,
/// \a width the pitch, as Viewport::takeFrame hands it -- composited
/// over \a background where the film was transparent (the same flat
/// colour or vertical ramp the offline render writes), encoded to sRGB
/// exactly when \a managed (the scene is colour managed), and packed
/// to 8-bit rows of \a channels (3 = RGB, 4 = RGBA with alpha 255),
/// top-down when \a topDown else bottom-up. Shared by the served
/// stream (sec 7.1) and the shader graph editor's preview
/// (docs/ShaderGraphEditor.md sec 15). Does nothing without the engine.
RendererExport void compositeFrame(const void *half4, int width, int height,
                                   const Background &background, bool managed,
                                   int channels, bool topDown,
                                   std::vector<uint8_t> &out);

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
