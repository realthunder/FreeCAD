/***************************************************************************
 *   Copyright (c) 2026 realthunder <realthunder.dev@gmail.com>            *
 *                                                                         *
 *   This file is part of the FreeCAD CAx development system.              *
 *                                                                         *
 *   This library is free software; you can redistribute it and/or         *
 *   modify it under the terms of the GNU Library General Public           *
 *   License as published by the Free Software Foundation; either          *
 *   version 2 of the License, or (at your option) any later version.      *
 *                                                                         *
 *   This library  is distributed in the hope that it will be useful,      *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU Library General Public License for more details.                  *
 *                                                                         *
 *   You should have received a copy of the GNU Library General Public     *
 *   License along with this library; see the file COPYING.LIB. If not,    *
 *   write to the Free Software Foundation, Inc., 59 Temple Place,         *
 *   Suite 330, Boston, MA  02111-1307, USA                                *
 *                                                                         *
 ***************************************************************************/

#ifndef GUI_EXPRESSIONEDITORVIEW_H
#define GUI_EXPRESSIONEDITORVIEW_H

#include <string>
#include <vector>

#include <QString>
#include <QStringList>

#include <App/DocumentObserver.h>

#include "MDIView.h"

class QPlainTextEdit;
class QStackedWidget;

namespace App
{
class DocumentObject;
}

namespace Gui
{

class ExpressionTextEditor;
class TextEditor;

/** The text form of expression bindings, shared by the copy and paste
 * entries of Std_Expressions and by the expression editor.
 *
 * Each binding is a block: two header lines and the expression body.
 *
 *   ##@@ <path> <Doc>#<Obj>.<Prop> (<Label>)
 *   ##@@<comment>
 *   <body>
 *
 * A body that is a lone '#' unbinds the property.
 */
namespace ExpressionText
{

struct GuiExport Block
{
    std::string path;
    std::string docName;
    std::string objName;
    std::string propName;
    std::string comment;  ///< the second header line, still encoded
    std::string body;
    int headerLine = 0;  ///< 0-based line of the first header line
    int lastLine = 0;    ///< 0-based last line of the block, inclusive

    /// Identifies the binding: document, object, property and path.
    std::string key() const;
    /// The body is a lone '#'.
    bool isUnbind() const;
};

struct GuiExport Error
{
    int line = -1;  ///< 0-based, -1 when no line applies
    std::string message;
};

struct GuiExport Parsed
{
    std::vector<Block> blocks;
    /// Non-blank text before the first header, and headers that do not
    /// name a binding.
    std::vector<Error> errors;
};

GuiExport Parsed parse(const std::string& text);

/// The objects in the order of their documents, each document in the order
/// of its objects -- so that the same scope always dumps the same text.
GuiExport std::vector<App::DocumentObject*>
inDocumentOrder(const std::vector<App::DocumentObject*>& objs);

/// The bindings of the objects, in the given order.
GuiExport std::string dump(const std::vector<App::DocumentObject*>& objs);

struct GuiExport ApplyResult
{
    std::vector<Error> errors;
    std::vector<Error> warnings;
    int changed = 0;
};

/** Bind, rebind or unbind the blocks in one transaction.
 *
 * Every body is parsed first, and nothing changes when one fails. A block
 * whose document, object or property is missing is an error when
 * \a strict, else a warning and skipped. Bindings that would not change
 * are left alone.
 */
GuiExport ApplyResult
apply(const std::vector<Block>& blocks, bool strict, const char* transactionName);

struct GuiExport DiffLine
{
    enum Kind
    {
        Same,
        Removed,
        Added,
    };
    Kind kind;
    QString text;
};

/// A line diff (Myers) of \a from against \a to.
GuiExport std::vector<DiffLine> diffLines(const QStringList& from, const QStringList& to);

}  // namespace ExpressionText

/** A text editor over the expression bindings of some objects.
 *
 * Opened from Std_Expressions on the selection, the active document or all
 * documents. The text is the copy format; Apply binds what it says,
 * Diff shows the edit against the text last loaded, Revert discards the
 * edit, Refresh reloads from the documents, and Unbind turns the blocks
 * under the cursor into unbinding blocks. Each is a command, and their
 * tool bar is shown only while an expression editor is the active view.
 * Each block folds under its first header line, and Fold all folds or
 * unfolds them together.
 */
class GuiExport ExpressionEditorView: public MDIView
{
    Q_OBJECT
    TYPESYSTEM_HEADER_WITH_OVERRIDE();

public:
    enum class Scope
    {
        Selection,
        ActiveDocument,
        AllDocuments,
    };

    /// Open an editor on the scope, or reveal the one already open on it.
    static ExpressionEditorView* open(Scope scope);

    ~ExpressionEditorView() override;

    const char* getName() const override
    {
        return "ExpressionEditorView";
    }
    bool onMsg(const char* msg, const char** ppReturn) override;
    bool onHasMsg(const char* msg) const override;
    bool canClose() override;
    QStringList undoActions() const override;
    QStringList redoActions() const override;

    TextEditor* getEditor() const;
    bool isShowingDiff() const;

    /// The objects the editor covers, as they are now.
    std::vector<App::DocumentObject*> scopeObjects() const;

    bool apply();
    void showDiff(bool show);
    void revert();
    bool refresh(bool ask = true);
    void unbindSelected();
    /// Fold every block when one is unfolded, else unfold them all.
    void toggleFoldAll();

    static const char* toolBarName();
    /// Show the tool bar while an expression editor is the active view.
    static void updateToolBar();

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    ExpressionEditorView(Scope scope,
                         std::vector<App::DocumentObjectT> objects,
                         App::DocumentT document,
                         Gui::Document* guiDoc);

    bool covers(Scope scope,
                const std::vector<App::DocumentObjectT>& objects,
                const App::DocumentT& document) const;
    void load();
    void replaceText(const QString& text);
    QPlainTextEdit* currentEditor() const;
    void updateTitle();
    void updateDiff();
    void gotoLine(int line);
    void showErrors(const QString& title, const std::vector<ExpressionText::Error>& errors);
    std::vector<ExpressionText::Block> selectedBlocks() const;

private:
    Scope scope;
    std::vector<App::DocumentObjectT> objects;
    App::DocumentT document;
    QStackedWidget* stack;
    ExpressionTextEditor* editor;
    TextEditor* diffView;
    std::string baseline;
    bool aboutToClose = false;
};

}  // namespace Gui

#endif  // GUI_EXPRESSIONEDITORVIEW_H
