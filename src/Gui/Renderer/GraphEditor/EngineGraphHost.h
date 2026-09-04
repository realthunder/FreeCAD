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
 *                                                                          *
 ****************************************************************************/

#ifndef RENDERER_GRAPHEDITOR_ENGINEGRAPHHOST_H
#define RENDERER_GRAPHEDITOR_ENGINEGRAPHHOST_H

/// \file EngineGraphHost.h
/// The engine half of the shader graph editor's host (docs/
/// ShaderGraphEditor.md sec 4.2, phase 2): everything the editor asks
/// that the renderer can answer by itself -- thumbnails decoded and
/// uploaded, the preview texture owned and uploaded, the orbit camera
/// the preview pane drives, whether a nodedef has an implementation
/// for our generator, and the "compiling" state -- with the one thing
/// it cannot do, RENDER the preview, left to a platform subclass that
/// owns a scene and a backend (Gui::ShaderGraphHost renders the
/// material icon's sphere through the document's 3D view).
///
/// Names no MaterialX type: the subclass lives in a library without
/// MaterialX headers. The GraphHost the editor talks to is an adapter
/// this class owns (graphHost()).

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "../Renderer.h"

namespace Render::GraphEditor {

class GraphHost;

class RendererExport EngineGraphHost {
public:
    EngineGraphHost();
    virtual ~EngineGraphHost();
    EngineGraphHost(const EngineGraphHost &) = delete;
    EngineGraphHost &operator=(const EngineGraphHost &) = delete;

    /// The host the editor is given (GraphEditorWidget::setHost).
    GraphHost *graphHost() const;

    // ---- what the platform half overrides

    /// An image name as the document states it, resolved to a file to
    /// read; empty when there is none. The default answers a name that
    /// is a readable file as it is, else the data library's search path.
    virtual std::string resolveImage(const std::string &name);
    /// The names a filename input may pick from; the default has none.
    virtual std::vector<std::string> imageNames() { return {}; }
    /// Decode one file into RGBA8 rows, TOP-DOWN, no side over
    /// kThumbnailSide. The default reads what the engine's decoder
    /// reads (PNG, JPEG); a subclass may add formats.
    virtual bool loadImage(const std::string &path, int &width, int &height,
                           std::vector<uint8_t> &rgba);
    /// The preview is out of date -- the document, the camera or the
    /// pane size changed -- render one when you can and hand it to
    /// setPreviewImage. May be called several times per frame; the
    /// default renders nothing (no preview pane).
    virtual void previewInvalidated() {}
    /// The document's renderable surfaces, the one the preview wears,
    /// and a pick made in the editor's Surface menu (GraphHost has the
    /// contract). The defaults state none, so no menu is drawn.
    virtual const std::vector<std::string> &surfaceNames()
    {
        static const std::vector<std::string> none;
        return none;
    }
    virtual std::string currentSurface() { return {}; }
    virtual void selectSurface(const std::string &) {}
    /// How the preview is rendered, and a pick made in the editor's
    /// Preview menu (GraphHost has the contract). The defaults state
    /// one way of rendering with no name, so no menu is drawn.
    virtual const std::vector<std::string> &previewModes()
    {
        static const std::vector<std::string> none;
        return none;
    }
    virtual int previewMode() { return 0; }
    virtual void setPreviewMode(int) {}
    /// Which device the current preview mode renders on, and a pick
    /// made in the Device submenu of that menu (GraphHost has the
    /// contract). The defaults state none, so no submenu is drawn.
    virtual const std::vector<std::string> &previewDevices()
    {
        static const std::vector<std::string> none;
        return none;
    }
    virtual int previewDevice() { return 0; }
    virtual void setPreviewDevice(int) {}

    // ---- what the platform half reads and writes

    /// The document as the preview should render it, serialized after
    /// every change the editor reports.
    const std::string &documentText() const;
    /// The pane size the editor last asked a preview for; zero until it
    /// has.
    int previewWidth() const;
    int previewHeight() const;
    /// The orbit camera the preview pane drives: the eye circles the
    /// origin at \a distance, \a yaw about +Z from +Y, \a pitch above
    /// the XY plane, both in radians. Z is up, as in the viewport.
    struct Camera {
        float yaw = 0.6f;
        float pitch = 0.45f;
        float distance = 3.2f;
    };
    const Camera &camera() const;
    /// The eye position and the unit vectors the camera looks with
    /// (forward, up), from camera().
    void cameraFrame(float eye[3], float forward[3], float up[3]) const;

    /// Hand over a rendered preview: \a width x \a height RGBA8 texels,
    /// rows BOTTOM-UP the way GL reads a framebuffer back. Copied
    /// before this returns. The preview pane draws it on the next
    /// frame the editor paints.
    void setPreviewImage(int width, int height, const uint8_t *rgba);
    /// No preview until the next setPreviewImage: the pane goes.
    void clearPreview();
    /// Whether a shader the preview needs is still compiling -- the
    /// editor shows "Compiling Shaders" while true.
    void setCompiling(bool compiling);
    bool compiling() const;
    /// What the preview's renderer is doing, shown under the pane while
    /// non-empty (a path tracer's "Sample 64/256"); empty says nothing.
    void setPreviewStatus(const std::string &status);
    const std::string &previewStatus() const;
    /// Forget every thumbnail (the image set changed).
    void invalidateThumbnails();

    /// The longest side a thumbnail is decoded to.
    static constexpr int kThumbnailSide = 256;

private:
    struct Private;
    std::unique_ptr<Private> d;
    friend class EngineGraphHostAdapter;
};

} // namespace Render::GraphEditor

#endif // RENDERER_GRAPHEDITOR_ENGINEGRAPHHOST_H
