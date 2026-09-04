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

#ifndef RENDERER_GRAPHEDITOR_GRAPHEDITORWIDGET_H
#define RENDERER_GRAPHEDITOR_GRAPHEDITORWIDGET_H

/// \file GraphEditorWidget.h
/// The shader graph editor's widget (docs/ShaderGraphEditor.md sec 4):
/// an ImGuiSurface drawing the ported MaterialX node editor over a
/// document given as MaterialX XML, and handing the text back after
/// every completed gesture. The widget holds no state the document
/// does not; its owner writes the text into the program's property
/// and reloads the widget when the property changes from outside.

#include "ImGuiSurface.h"

#include <functional>
#include <string>

namespace Render {

namespace GraphEditor {
class EngineGraphHost;
}

class RendererExport GraphEditorWidget : public ImGuiSurface {
public:
    explicit GraphEditorWidget(QWidget *parent = nullptr);
    ~GraphEditorWidget() override;

    /// The name shown at the root of the graph path (the program's label).
    void setTitle(const std::string &title);

    /// The host that answers the editor's preview, thumbnails and
    /// implementation questions (docs/ShaderGraphEditor.md sec 4.2).
    /// Null, the default, is a host with no preview and no
    /// thumbnails. Not owned; the owner unsets it before it goes.
    void setHost(GraphEditor::EngineGraphHost *host);

    /// Load, or reload, the document from MaterialX XML. Applied on
    /// the next frame, with the node editor current; a text that does
    /// not parse leaves the previous document and sets loadError().
    void setDocument(const std::string &xml);

    /// The parse error of the last setDocument, empty when it parsed.
    const std::string &loadError() const;

    /// Called from the event loop with the document text after each
    /// completed gesture that changed it.
    void setCommitHandler(std::function<void(const std::string &xml)> handler);

protected:
    void contextCreated() override;
    void drawUi() override;

private:
    struct Private;
    std::unique_ptr<Private> d;
};

} // namespace Render

#endif // RENDERER_GRAPHEDITOR_GRAPHEDITORWIDGET_H
