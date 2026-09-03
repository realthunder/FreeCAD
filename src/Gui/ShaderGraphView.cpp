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
# include <cstring>
# include <QString>
#endif

#include <App/Document.h>
#include <App/ShaderObject.h>
#include <Base/Console.h>
#include <Base/Tools.h>

#include "ShaderGraphView.h"
#include "Application.h"
#include "Command.h"
#include "Document.h"
#include "Renderer/GraphEditor/GraphEditorWidget.h"

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
    editor->setDocument(program->FragmentProgram.getValue());
    changedConnection = prog->getDocument()->signalChangedObject.connect(
            std::bind(&ShaderGraphView::slotChangedObject, this, sp::_1, sp::_2));
}

ShaderGraphView::~ShaderGraphView()
{
    changedConnection.disconnect();
}

void ShaderGraphView::slotChangedObject(const App::DocumentObject &obj,
                                        const App::Property &prop)
{
    if (&obj != program || writing)
        return;
    if (&prop == &program->FragmentProgram)
        editor->setDocument(program->FragmentProgram.getValue());
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
        doc->commitCommand();
    }
    catch (Base::Exception &e) {
        doc->abortCommand();
        FC_ERR("shader graph write failed: " << e.what());
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
