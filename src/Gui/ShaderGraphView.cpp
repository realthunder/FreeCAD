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

#include "PreCompiled.h"

#ifndef _PreComp_
# include <cmath>
# include <cstdio>
# include <cstring>
# include <QString>
#endif

#include <App/Document.h>
#include <App/PropertyStandard.h>
#include <App/PropertyGeo.h>
#include <App/ShaderObject.h>
#include <Base/Console.h>
#include <Base/Tools.h>

#include "ShaderGraphView.h"
#include "ShaderGraphHost.h"
#include "Application.h"
#include "Command.h"
#include "Document.h"
#include "Inventor/SoFCVertexCache.h"
#include "Inventor/SoFCRendererBridge.h"
#include "Renderer/GraphEditor/GraphEditorWidget.h"
#include "Renderer/MaterialXSupport.h"

FC_LOG_LEVEL_INIT("Gui", true, true)

using namespace Gui;
namespace sp = std::placeholders;

TYPESYSTEM_SOURCE_ABSTRACT(Gui::ShaderGraphView, Gui::MDIView)

ShaderGraphView::ShaderGraphView(App::ShaderProgram *prog, QWidget *parent)
    : MDIView(Application::Instance->getDocument(prog->getDocument()), parent)
    , program(prog)
{
    // The layout token of a split cell holding an object view is
    // derived from the widget's objectName (Document.cpp, the O:
    // token), the way MDIViewPage::setDocumentObject sets it.
    setObjectName(QString::fromUtf8(prog->getNameInDocument()));
    editor = new Render::GraphEditorWidget(this);
    setCentralWidget(editor);
    labelChanged();

    // The editor is a view over the property: it shows the text and
    // hands back the text; the document is the only state.
    editor->setCommitHandler([this](const std::string &xml) { commitText(xml); });
    host = new ShaderGraphHost(program, editor);
    editor->setHost(host);
    {
        const std::string text = documentForEditor();
        host->setBaseText(text);
        editor->setDocument(text);
    }
    changedConnection = prog->getDocument()->signalChangedObject.connect(
            std::bind(&ShaderGraphView::slotChangedObject, this, sp::_1, sp::_2));
}

ShaderGraphView::~ShaderGraphView()
{
    changedConnection.disconnect();
    // The editor outlives this body (it is a child widget); it must
    // not keep the host.
    editor->setHost(nullptr);
    delete host;
}

// The program's Param_* dynamic properties as shader parameters, the
// way the view provider binds them (docs/RenderDebug.md sec 6.4).
static std::vector<Render::RenderDebugConfig::UserParam> paramValues(App::ShaderProgram *obj)
{
    static const char prefix[] = "Param_";
    static const size_t prefixLen = sizeof(prefix) - 1;
    std::vector<Render::RenderDebugConfig::UserParam> res;
    for (const auto &name : obj->getDynamicPropertyNames()) {
        if (name.compare(0, prefixLen, prefix) != 0 || name.size() <= prefixLen)
            continue;
        auto prop = obj->getDynamicPropertyByName(name.c_str());
        std::vector<float> values;
        if (!prop || !RendererBridge::translateShaderParamValues(prop, values))
            continue;
        res.push_back({RendererBridge::shaderParamUniformName(name.c_str()),
                       std::move(values)});
    }
    return res;
}

static bool isParamProperty(App::ShaderProgram *obj, const App::Property &prop)
{
    const char *name = prop.getName();
    return name && std::strncmp(name, "Param_", 6) == 0 && name[6]
        && obj->getDynamicPropertyByName(name) == &prop;
}

std::string ShaderGraphView::documentForEditor() const
{
    std::string xml = program->FragmentProgram.getValue();
    // App::ShaderProgram::DialectEnums: 2 = MATERIALX
    if (program->Dialect.getValue() != 2 || xml.empty())
        return xml;
    auto params = paramValues(program);
    if (params.empty())
        return xml;
    return Render::MaterialX::applyInputsToDocument(xml, params, program->Surface.getValue());
}

void ShaderGraphView::slotChangedObject(const App::DocumentObject &obj,
                                        const App::Property &prop)
{
    if (&obj != program || writing)
        return;
    if (&prop == &program->FragmentProgram || isParamProperty(program, prop)) {
        const std::string text = documentForEditor();
        host->setBaseText(text);
        editor->setDocument(text);
    }
    else if (&prop == &program->Surface) {
        const std::string text = documentForEditor();
        host->programChanged();
        host->setBaseText(text);
        editor->setDocument(text);
    }
    else if (&prop == &program->Images) {
        host->programChanged();
    }
}

// A Python string literal for the text: strToPython escapes quotes and
// backslashes but leaves newlines, which end the literal.
static std::string pyLiteral(const std::string &text)
{
    std::string out;
    out.reserve(text.size() + 16);
    out += '"';
    for (char c : text) {
        switch (c) {
        case '\\': out += "\\\\"; break;
        case '"': out += "\\\""; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default: out += c;
        }
    }
    out += '"';
    return out;
}

void ShaderGraphView::commitText(const std::string &xml)
{
    if (!program || !program->getNameInDocument())
        return;
    const char *current = program->FragmentProgram.getValue();
    if (current && xml == current)
        return;
    // A Python property assignment inside one transaction, the way the
    // sync commands write: undoable, and on the macro record.
    Base::FlagToggler<bool> guard(writing);
    Gui::Document *doc = getGuiDocument();
    doc->openCommand(QT_TRANSLATE_NOOP("Command", "Edit shader graph"));
    try {
        Gui::Command::doCommand(Gui::Command::Doc,
                "App.getDocument(\"%s\").getObject(\"%s\").FragmentProgram = %s",
                program->getDocument()->getName(), program->getNameInDocument(),
                pyLiteral(xml).c_str());
        writeParams(xml);
        doc->commitCommand();
    }
    catch (Base::Exception &e) {
        doc->abortCommand();
        FC_ERR("shader graph write failed: " << e.what());
    }
}

// A Python expression that sets \a prop to \a v, by the property's
// type -- the types syncDocumentInterface materializes a public input as.
static std::string pyValueFor(const App::Property *prop, const std::vector<float> &v)
{
    auto at = [&v](size_t i) { return i < v.size() ? v[i] : 0.0f; };
    char buf[128];
    if (dynamic_cast<const App::PropertyBool*>(prop))
        return at(0) != 0.0f ? "True" : "False";
    if (dynamic_cast<const App::PropertyInteger*>(prop)) {
        std::snprintf(buf, sizeof(buf), "%ld", long(at(0)));
        return buf;
    }
    if (dynamic_cast<const App::PropertyFloat*>(prop)) {
        std::snprintf(buf, sizeof(buf), "%.9g", double(at(0)));
        return buf;
    }
    if (dynamic_cast<const App::PropertyColor*>(prop)) {
        std::snprintf(buf, sizeof(buf), "(%.9g, %.9g, %.9g, %.9g)", double(at(0)),
                      double(at(1)), double(at(2)), v.size() > 3 ? double(at(3)) : 1.0);
        return buf;
    }
    if (dynamic_cast<const App::PropertyVector*>(prop)) {
        std::snprintf(buf, sizeof(buf), "App.Vector(%.9g, %.9g, %.9g)", double(at(0)),
                      double(at(1)), double(at(2)));
        return buf;
    }
    if (dynamic_cast<const App::PropertyFloatList*>(prop)) {
        std::snprintf(buf, sizeof(buf), "[%.9g, %.9g, %.9g, %.9g]", double(at(0)),
                      double(at(1)), double(at(2)), double(at(3)));
        return buf;
    }
    return {};
}

void ShaderGraphView::writeParams(const std::string &xml)
{
    // App::ShaderProgram::DialectEnums: 2 = MATERIALX
    if (program->Dialect.getValue() != 2)
        return;
    auto info = Render::MaterialX::inspect(xml, {}, program->Surface.getValue());
    if (!info.valid)
        return;
    for (const auto &input : info.inputs) {
        const std::string name = "Param_" + input.name;
        auto prop = program->getDynamicPropertyByName(name.c_str());
        if (!prop)
            continue;   // the provider's sync materializes it from the text
        std::vector<float> current;
        if (!RendererBridge::translateShaderParamValues(prop, current))
            continue;
        bool same = true;
        for (size_t i = 0; same && i < input.value.size(); ++i)
            same = i < current.size() && std::fabs(current[i] - input.value[i]) < 1e-6f;
        if (same)
            continue;
        const std::string value = pyValueFor(prop, input.value);
        if (value.empty())
            continue;
        Gui::Command::doCommand(Gui::Command::Doc,
                "App.getDocument(\"%s\").getObject(\"%s\").%s = %s",
                program->getDocument()->getName(), program->getNameInDocument(),
                name.c_str(), value.c_str());
    }
}

void ShaderGraphView::labelChanged()
{
    const std::string label = program->Label.getValue();
    setWindowTitle(QString::fromUtf8(label.c_str()) + QStringLiteral("[*]"));
    editor->setTitle(label);
}

bool ShaderGraphView::onMsg(const char *msg, const char **)
{
    // The editor is a view over a property: its undo is the
    // document's (docs/ShaderGraphEditor.md sec 4.1).
    if (std::strcmp(msg, "Undo") == 0) {
        getGuiDocument()->undo(1);
        return true;
    }
    if (std::strcmp(msg, "Redo") == 0) {
        getGuiDocument()->redo(1);
        return true;
    }
    return false;
}

bool ShaderGraphView::onHasMsg(const char *msg) const
{
    if (std::strcmp(msg, "AllowsOverlayOnHover") == 0)
        return true;
    if (std::strcmp(msg, "Undo") == 0)
        return getGuiDocument()->getDocument()->getAvailableUndos() > 0;
    if (std::strcmp(msg, "Redo") == 0)
        return getGuiDocument()->getDocument()->getAvailableRedos() > 0;
    return false;
}

#include "moc_ShaderGraphView.cpp"
