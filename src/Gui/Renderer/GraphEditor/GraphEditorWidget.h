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
/// The shader graph editor's widget: an ImGuiSurface drawing the node
/// editor canvas (docs/ShaderGraphEditor.md sec 4). PHASE 0 SPIKE:
/// the canvas shows two placeholder nodes and a link, plus a text
/// field, to prove the hosting -- ImGui on bgfx inside a Qt widget in
/// a split cell beside a live 3D view. Phase 1 replaces the body with
/// the ported MaterialX Graph.

#include "ImGuiSurface.h"

#include <string>

namespace Render {

class RendererExport GraphEditorWidget : public ImGuiSurface {
public:
    explicit GraphEditorWidget(QWidget *parent = nullptr);
    ~GraphEditorWidget() override;

    /// The name shown in the canvas header (the program's label).
    void setTitle(const std::string &title);

protected:
    void contextCreated() override;
    void drawUi() override;

private:
    struct Private;
    std::unique_ptr<Private> d;
};

} // namespace Render

#endif // RENDERER_GRAPHEDITOR_GRAPHEDITORWIDGET_H
