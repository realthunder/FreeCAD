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

#ifndef RENDERER_GRAPHEDITOR_GRAPHHOST_H
#define RENDERER_GRAPHEDITOR_GRAPHHOST_H

/// \file GraphHost.h
/// What the shader graph editor asks of its host, in place of the
/// MaterialX editor's own GL viewer (docs/ShaderGraphEditor.md sec
/// 4.2): a preview, thumbnails, and whether a nodedef has an
/// implementation for our target. NullGraphHost answers nothing;
/// EngineGraphHost (EngineGraphHost.h) answers on the engine, with the
/// preview itself rendered by whoever owns a scene (Gui::ShaderGraphHost).

#include <MaterialXCore/Document.h>
#include <imgui.h>

#include <string>
#include <vector>

namespace Render::GraphEditor {

class GraphHost {
public:
    virtual ~GraphHost() = default;

    /// The document changed (topology or a value, or it was loaded); a
    /// null element means everything. The host re-renders the preview
    /// when it can. \a doc is the editor's document, the one the text
    /// is serialized from.
    virtual void documentChanged(const ::MaterialX::DocumentPtr &doc,
                                 ::MaterialX::ElementPtr changed) = 0;
    /// A value moved during a drag: preview only, no property write.
    /// The document already carries the value.
    virtual void valueDragged(const ::MaterialX::DocumentPtr &doc,
                              ::MaterialX::InputPtr input,
                              ::MaterialX::ValuePtr value) = 0;
    /// Whether a nodedef has an implementation for our target
    /// (checkCanAddLink's one use of the generator context).
    virtual bool hasImplementation(const ::MaterialX::NodeDef &def) = 0;
    /// A thumbnail for an image the document names: an ImTextureID
    /// plus its size, or ImTextureID_Invalid.
    virtual ImTextureID thumbnail(const std::string &name, int &w, int &h) = 0;
    virtual const std::vector<std::string> &imageExtensions() = 0;
    /// The image names a filename input may pick from: what the
    /// program carries. Empty leaves the field typed only.
    virtual const std::vector<std::string> &imageNames() = 0;
    /// The preview: its texture at the requested size (in framebuffer
    /// pixels, the pane's logical size times the display's framebuffer
    /// scale), or ImTextureID_Invalid when there is none (then no
    /// preview pane is drawn), and its input.
    virtual ImTextureID preview(int w, int h) = 0;
    virtual void previewMouse(float x, float y, int button, bool down) = 0;
    virtual void previewScroll(float delta) = 0;
    virtual bool compiling() const = 0;
    /// The document's renderable surfaces by name (a surfacematerial's,
    /// or a bare surface shader's), the one the preview wears, and a
    /// pick from the editor's Surface menu -- which is drawn only when
    /// there are two or more. The defaults state none.
    virtual const std::vector<std::string> &surfaceNames()
    {
        static const std::vector<std::string> none;
        return none;
    }
    virtual std::string currentSurface() { return {}; }
    virtual void selectSurface(const std::string &) {}
    /// How the preview is rendered (docs/ShaderGraphEditor.md sec 15):
    /// the modes by name, the current one's index, and a pick from the
    /// editor's Preview menu -- drawn only when there are two or more.
    /// The default states none.
    virtual const std::vector<std::string> &previewModes()
    {
        static const std::vector<std::string> none;
        return none;
    }
    virtual int previewMode() { return 0; }
    virtual void setPreviewMode(int) {}
    /// The compute devices the current preview mode can render on, the
    /// one it uses, and a pick from the Device submenu of the Preview
    /// menu -- drawn only when there are two or more. The default
    /// states none, and so does a mode that has no device to choose.
    virtual const std::vector<std::string> &previewDevices()
    {
        static const std::vector<std::string> none;
        return none;
    }
    virtual int previewDevice() { return 0; }
    virtual void setPreviewDevice(int) {}
    /// What the preview's renderer is doing, shown under the pane
    /// while non-empty (a path tracer's sample count); empty when
    /// there is nothing to say.
    virtual std::string previewStatus() { return {}; }
};

/// No preview, no thumbnails, every nodedef implemented: the editor
/// is usable before the engine half exists.
class NullGraphHost : public GraphHost {
public:
    void documentChanged(const ::MaterialX::DocumentPtr &, ::MaterialX::ElementPtr) override {}
    void valueDragged(const ::MaterialX::DocumentPtr &, ::MaterialX::InputPtr,
                      ::MaterialX::ValuePtr) override
    {}
    bool hasImplementation(const ::MaterialX::NodeDef &) override { return true; }
    ImTextureID thumbnail(const std::string &, int &w, int &h) override
    {
        w = h = 0;
        return ImTextureID_Invalid;
    }
    const std::vector<std::string> &imageExtensions() override { return extensions; }
    const std::vector<std::string> &imageNames() override { return extensions; }
    ImTextureID preview(int, int) override { return ImTextureID_Invalid; }
    void previewMouse(float, float, int, bool) override {}
    void previewScroll(float) override {}
    bool compiling() const override { return false; }

private:
    std::vector<std::string> extensions;
};

} // namespace Render::GraphEditor

#endif // RENDERER_GRAPHEDITOR_GRAPHHOST_H
