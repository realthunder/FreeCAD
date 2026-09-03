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

#include <imgui.h>
#include <imgui_node_editor.h>

#include <vector>

namespace ed = ax::NodeEditor;

namespace Render {

struct GraphEditorWidget::Private {
    ed::EditorContext *editor = nullptr;
    std::string title;
    char nameBuf[64] = "surface1";
    bool firstFrame = true;

    struct Link {
        ed::LinkId id;
        ed::PinId from;
        ed::PinId to;
    };
    std::vector<Link> links;
    int nextLinkId = 100;
};

GraphEditorWidget::GraphEditorWidget(QWidget *parent)
    : ImGuiSurface(parent)
    , d(new Private)
{
}

GraphEditorWidget::~GraphEditorWidget()
{
    // The editor context lives in ImGui's allocator; free it while the
    // ImGui context still exists (the base destructor drops that).
    if (d->editor) {
        makeImGuiCurrent();
        ed::DestroyEditor(d->editor);
        d->editor = nullptr;
    }
}

void GraphEditorWidget::setTitle(const std::string &title)
{
    d->title = title;
    requestFrame();
}

void GraphEditorWidget::contextCreated()
{
    ed::Config config;
    // No settings file: positions belong to the document
    // (docs/ShaderGraphEditor.md sec 4.1).
    config.SettingsFile = nullptr;
    d->editor = ed::CreateEditor(&config);
    d->links.push_back({ed::LinkId(d->nextLinkId++), ed::PinId(3), ed::PinId(12)});
}

void GraphEditorWidget::drawUi()
{
    const ImGuiViewport *viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration
        | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize
        | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus
        | ImGuiWindowFlags_MenuBar;
    if (!ImGui::Begin("ShaderGraph", nullptr, flags)) {
        ImGui::End();
        return;
    }

    if (ImGui::BeginMenuBar()) {
        if (ImGui::BeginMenu("Graph")) {
            if (ImGui::MenuItem("Auto Layout"))
                ed::NavigateToContent(0.0f);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Help")) {
            ImGui::MenuItem("Phase 0 spike", nullptr, false, false);
            ImGui::EndMenu();
        }
        ImGui::EndMenuBar();
    }

    ImGui::Text("%s", d->title.empty() ? "(no program)" : d->title.c_str());
    ImGui::SameLine();
    ImGui::SetNextItemWidth(160.0f);
    ImGui::InputText("Surface", d->nameBuf, sizeof(d->nameBuf));
    ImGui::Separator();

    ed::SetCurrentEditor(d->editor);
    ed::Begin("Canvas", ImVec2(0.0f, 0.0f));

    // Node ids and pin ids are one flat space in the editor.
    ed::BeginNode(1);
    ImGui::Text("Noise");
    ed::BeginPin(2, ed::PinKind::Input);
    ImGui::Text("-> scale");
    ed::EndPin();
    ImGui::SameLine();
    ed::BeginPin(3, ed::PinKind::Output);
    ImGui::Text("out ->");
    ed::EndPin();
    ed::EndNode();

    ed::BeginNode(10);
    ImGui::Text("Surface");
    ed::BeginPin(12, ed::PinKind::Input);
    ImGui::Text("-> base_color");
    ed::EndPin();
    ed::BeginPin(13, ed::PinKind::Input);
    ImGui::Text("-> roughness");
    ed::EndPin();
    ImGui::SameLine();
    ed::BeginPin(14, ed::PinKind::Output);
    ImGui::Text("out ->");
    ed::EndPin();
    ed::EndNode();

    for (const auto &link : d->links)
        ed::Link(link.id, link.from, link.to);

    if (ed::BeginCreate()) {
        ed::PinId from, to;
        if (ed::QueryNewLink(&from, &to)) {
            if (from && to && from != to && ed::AcceptNewItem()) {
                d->links.push_back({ed::LinkId(d->nextLinkId++), from, to});
                ed::Link(d->links.back().id, from, to);
            }
        }
    }
    ed::EndCreate();

    if (ed::BeginDelete()) {
        ed::LinkId deleted;
        while (ed::QueryDeletedLink(&deleted)) {
            if (ed::AcceptDeletedItem()) {
                for (auto it = d->links.begin(); it != d->links.end(); ++it) {
                    if (it->id == deleted) {
                        d->links.erase(it);
                        break;
                    }
                }
            }
        }
    }
    ed::EndDelete();

    if (d->firstFrame) {
        ed::SetNodePosition(1, ImVec2(40.0f, 60.0f));
        ed::SetNodePosition(10, ImVec2(300.0f, 60.0f));
        ed::NavigateToContent(0.0f);
        d->firstFrame = false;
    }

    ed::End();
    ed::SetCurrentEditor(nullptr);
    ImGui::End();
}

} // namespace Render
