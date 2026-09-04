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

#ifndef GUI_SHADERGRAPHHOST_H
#define GUI_SHADERGRAPHHOST_H

/// \file ShaderGraphHost.h
/// The shader graph editor's host on the desktop (docs/
/// ShaderGraphEditor.md sec 4.2, phase 2): the renderer's
/// EngineGraphHost with the preview rendered -- the material icon's
/// sphere wearing the edited document, run through the render-cache
/// pipeline and handed to the document's live 3D view backend as a
/// transient capture scene, the way the TechDraw shaded underlay
/// captures a derived shape. No backend (no 3D view on the renderer
/// path) means no preview pane; the editor works without one.

#include <QObject>
#include <QTimer>
#include <map>
#include <string>
#include <vector>

#include <Gui/InventorBase.h>
#include <Gui/Renderer/GraphEditor/EngineGraphHost.h>

class SoSeparator;
class SoPerspectiveCamera;
class SoFragmentShader;
class SoShaderParameterArray1f;

namespace App {
class ShaderProgram;
}
namespace Render {
class GraphEditorWidget;
class Renderer;
}

namespace Gui {

class View3DInventorViewer;

class GuiExport ShaderGraphHost : public QObject, public Render::GraphEditor::EngineGraphHost {
    Q_OBJECT
public:
    ShaderGraphHost(App::ShaderProgram *program, Render::GraphEditorWidget *editor);
    ~ShaderGraphHost() override;

    /// The program's Images or Surface changed: thumbnails and the
    /// preview are stale.
    void programChanged();
    /// The text the editor was loaded with. The preview's shader is
    /// generated from THIS text, with the document's public input
    /// values -- the ones a drag moves -- carried as parameters the way
    /// the viewport carries Param_* properties, so a value drag
    /// changes uniforms and never regenerates or recompiles.
    void setBaseText(const std::string &xml);

    std::string resolveImage(const std::string &name) override;
    std::vector<std::string> imageNames() override;
    bool loadImage(const std::string &path, int &width, int &height,
                   std::vector<uint8_t> &rgba) override;
    void previewInvalidated() override;
    const std::vector<std::string> &surfaceNames() override;
    std::string currentSurface() override;
    /// Writes the program's Surface property, deferred to the event
    /// loop: the pick is made inside the editor's frame, and the write
    /// reloads the editor.
    void selectSurface(const std::string &name) override;

private:
    /// Render the preview from the event loop: never from inside the
    /// editor's paint, where a backend frame is being encoded.
    void renderPreview();
    /// A 3D view of the program's document with a backend, or none.
    bool findBackend(Render::Renderer *&renderer, View3DInventorViewer *&viewer) const;
    /// The document as the preview's shader node takes it: the text
    /// with the program's stored images named where they are.
    std::string documentForRender() const;
    void buildScene();

    App::ShaderProgram *const program;
    Render::GraphEditorWidget *const editor;
    CoinPtr<SoSeparator> root;
    CoinPtr<SoPerspectiveCamera> previewCamera;
    CoinPtr<SoFragmentShader> fragment;
    std::map<std::string, CoinPtr<SoShaderParameterArray1f>> paramNodes;
    /// The baseline's public input values as parameters (setBaseText).
    std::vector<Render::RenderDebugConfig::UserParam> baseParams;
    /// The text the surface names were last read from, and the names.
    std::string namesText;
    std::vector<std::string> names;
    /// The live text last inspected, and its public inputs.
    std::string inspectedText;
    std::vector<Render::RenderDebugConfig::UserParam> liveParams;
    /// Renders coalesce on this timer: a drag reports every motion.
    QTimer render;
    /// While the last preview drew a stand-in for a shader still
    /// compiling, the poll asks the backend until the compile lands.
    QTimer poll;
    int compileGeneration = 0;
};

} // namespace Gui

#endif // GUI_SHADERGRAPHHOST_H
