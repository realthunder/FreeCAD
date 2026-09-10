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

#ifndef GUI_SHADERGRAPHVIEW_H
#define GUI_SHADERGRAPHVIEW_H

/// \file ShaderGraphView.h
/// The view half of "Edit Shader Graph..." (docs/ShaderGraphEditor.md
/// sec 4.4): an MDIView over an App::ShaderProgram whose central
/// widget is the renderer's graph editor. It is placed by
/// Gui::ViewPlacement like a TechDraw page or a spreadsheet -- never
/// by itself -- and its Qt objectName is the program's internal name,
/// which is what the split-view area keys a cell's layout token on.

#include <Gui/MDIView.h>

namespace App {
class ShaderProgram;
}
namespace Render {
class GraphEditorWidget;
}

namespace Gui {

class ShaderGraphHost;

class GuiExport ShaderGraphView : public MDIView {
    Q_OBJECT
    TYPESYSTEM_HEADER_WITH_OVERRIDE();

public:
    ShaderGraphView(App::ShaderProgram *program, QWidget *parent);
    ~ShaderGraphView() override;

    const char *getName() const override { return "ShaderGraphView"; }
    bool onMsg(const char *msg, const char **) override;
    bool onHasMsg(const char *msg) const override;

    App::ShaderProgram *getProgram() const { return program; }
    /// The program's label changed: retitle.
    void labelChanged();

private:
    /// The document's text after a completed gesture in the editor:
    /// one undoable property write (docs/ShaderGraphEditor.md sec 4.1),
    /// with the Param_* properties whose input the text moved written
    /// in the same transaction.
    void commitText(const std::string &xml);
    /// The text the editor shows: the property's document with the
    /// Param_* property values written into its public inputs, which
    /// is what the viewport renders.
    std::string documentForEditor() const;
    /// Write into the open transaction every Param_* property whose
    /// public input \a xml states differently.
    void writeParams(const std::string &xml);
    /// An object of the document changed: the program's text from
    /// outside (undo, Python) reloads the editor.
    void slotChangedObject(const App::DocumentObject &obj, const App::Property &prop);

    App::ShaderProgram *const program;
    Render::GraphEditorWidget *editor = nullptr;
    ShaderGraphHost *host = nullptr;
    fastsignals::connection changedConnection;
    /// Set around this view's own property write, which must not
    /// reload the editor that made it.
    bool writing = false;
};

} // namespace Gui

#endif // GUI_SHADERGRAPHVIEW_H
