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

#include "GraphEditorWidget.h"
#include "EngineGraphHost.h"
#include "Graph.h"
#include "GraphHost.h"

#include <QTimer>

#include <imgui.h>
#include <imgui_node_editor.h>

namespace ed = ax::NodeEditor;

namespace Render {

struct GraphEditorWidget::Private {
    ed::EditorContext *editor = nullptr;
    GraphEditor::NullGraphHost nullHost;
    GraphEditor::EngineGraphHost *host = nullptr;
    std::unique_ptr<GraphEditor::Graph> graph;
    GraphEditor::GraphHost *activeHost()
    {
        return host ? host->graphHost() : &nullHost;
    }
    std::string title;
    std::string pendingXml;
    bool hasPending = false;
    std::string error;
    std::function<void(const std::string &)> commit;
};

GraphEditorWidget::GraphEditorWidget(QWidget *parent)
    : ImGuiSurface(parent)
    , d(new Private)
{
}

GraphEditorWidget::~GraphEditorWidget()
{
    // The graph and the editor context live in ImGui's allocator; free
    // them while the ImGui context still exists (the base destructor
    // drops that).
    if (d->editor) {
        makeImGuiCurrent();
        ed::SetCurrentEditor(d->editor);
        d->graph.reset();
        ed::SetCurrentEditor(nullptr);
        ed::DestroyEditor(d->editor);
        d->editor = nullptr;
    }
}

void GraphEditorWidget::setTitle(const std::string &title)
{
    d->title = title;
    requestFrame();
}

void GraphEditorWidget::setHost(GraphEditor::EngineGraphHost *host)
{
    if (d->host == host)
        return;
    d->host = host;
    if (d->graph) {
        makeImGuiCurrent();
        ed::SetCurrentEditor(d->editor);
        d->graph->setHost(d->activeHost());
        ed::SetCurrentEditor(nullptr);
    }
    requestFrame();
}

void GraphEditorWidget::setDocument(const std::string &xml)
{
    d->pendingXml = xml;
    d->hasPending = true;
    requestFrame();
}

const std::string &GraphEditorWidget::loadError() const
{
    return d->error;
}

void GraphEditorWidget::setCommitHandler(std::function<void(const std::string &)> handler)
{
    d->commit = std::move(handler);
}

void GraphEditorWidget::contextCreated()
{
    ed::Config config;
    // No settings file: positions belong to the document
    // (docs/ShaderGraphEditor.md sec 4.1).
    config.SettingsFile = nullptr;
    d->editor = ed::CreateEditor(&config);
    ed::SetCurrentEditor(d->editor);
    d->graph = std::make_unique<GraphEditor::Graph>(d->activeHost());
    ed::SetCurrentEditor(nullptr);
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
}

void GraphEditorWidget::drawUi()
{
    ed::SetCurrentEditor(d->editor);
    if (d->hasPending) {
        d->hasPending = false;
        d->graph->setDocument(d->pendingXml, d->error);
        d->pendingXml.clear();
    }
    d->graph->setName(d->title);
    d->graph->drawGraph(ImGui::GetMousePos());
    std::string text;
    const bool changed = d->graph->takeChange(text);
    ed::SetCurrentEditor(nullptr);

    // The write goes through the document and everything that watches
    // it; not from inside a paint.
    if (changed && d->commit) {
        QTimer::singleShot(0, this, [this, text = std::move(text)]() {
            if (d->commit)
                d->commit(text);
        });
    }
}

} // namespace Render
