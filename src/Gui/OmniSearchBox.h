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

#ifndef GUI_OMNI_SEARCH_BOX_H
#define GUI_OMNI_SEARCH_BOX_H

#include <QFrame>
#include <QLineEdit>
#include <QPointer>
#include <QWidget>

#include <FCGlobal.h>
#include <App/DocumentObserver.h>

#include "OmniSearch.h"

class QCompleter;
class QVBoxLayout;

namespace App {
struct ParamInfo;
}

namespace Gui {

class CommandListModel;
class ExpressionCompleter;
class KeywordFilterModel;
class OmniParamPanel;
class OmniPropertyPanel;
class ParamListModel;
class TreeWidget;

/** The line edit of the omni search box.
 *
 * One edit, four completers: the mode chooser, the expression completer
 * for objects and properties, and keyword-filtered lists of commands and
 * parameters. The text's prefix picks which one answers a keystroke
 * (OmniSearch::parseInput); at most one popup is up at a time.
 */
class GuiExport OmniSearchEdit : public QLineEdit
{
    Q_OBJECT
public:
    explicit OmniSearchEdit(QWidget *parent = nullptr);
    ~OmniSearchEdit() override;

    /// The object an object query is parsed against; null disables that mode
    void setOwner(App::DocumentObject *owner);
    App::DocumentObject *owner() const;

    OmniSearch::Mode mode() const { return input.mode; }
    const OmniSearch::Input &currentInput() const { return input; }

    /// Set the text and act on it as if typed
    void setInputText(const QString &text);

    /// Show the mode chooser once the edit has focus (or now, if it has)
    void requestChooser();

    bool popupVisible() const;
    void hidePopups();

    /// Rows of the current mode's filtered list, for a lone-match Enter
    int filteredRowCount() const;
    QModelIndex filteredRow(int row) const;

Q_SIGNALS:
    void modeChanged(OmniSearch::Mode mode);
    /// The object query names an object or a property
    void objectResolved(const OmniSearch::ObjectMatch &match);
    /// The object query names nothing
    void objectUnresolved();
    /// A command row was chosen
    void commandChosen(const QByteArray &name);
    /// The arrow of a group command row was hit; rect is in global coordinates
    void groupExpandRequested(const QByteArray &name, const QRect &rect);
    /// A parameter row was chosen
    void paramChosen(const App::ParamInfo *info);
    /// Return pressed with no popup row to take it
    void enterPressed();

public Q_SLOTS:
    void onTextEdited(const QString &text);

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void focusInEvent(QFocusEvent *event) override;
    bool eventFilter(QObject *obj, QEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;

private:
    void setupChooser();
    void setupCommands();
    void setupParams();
    void runObjectQuery();
    void resolveObjectQuery();
    void completeObject(const QString &completion);
    QRect popupRect() const;
    QCompleter *activeCompleter() const;
    bool expandGroupAt(const QModelIndex &index);

    OmniSearch::Input input;
    App::DocumentObjectT ownerObj;

    QCompleter *chooser = nullptr;
    ExpressionCompleter *objCompleter = nullptr;
    QCompleter *cmdCompleter = nullptr;
    CommandListModel *cmdModel = nullptr;
    KeywordFilterModel *cmdFilter = nullptr;
    QCompleter *paramCompleter = nullptr;
    ParamListModel *paramModel = nullptr;
    KeywordFilterModel *paramFilter = nullptr;

    bool completing = false;
    bool justActivated = false;
    bool chooserPending = false;
    App::Property *resolvedProp = nullptr;
    App::SubObjectT resolvedObj;
};

/** The floating omni search box, Std_OmniSearch.
 *
 * A frameless tool window at the top centre of the main window holding
 * an OmniSearchEdit and, below it, whichever panel the resolved item
 * calls for: the property editor of a resolved property, or the editor
 * of a chosen parameter. Objects are shown in the tree as they resolve
 * and selected, with the hierarchy menu, on Enter; commands run on Enter.
 *
 * See docs/OmniSearch.md.
 */
class GuiExport OmniSearchBox : public QFrame
{
    Q_OBJECT
public:
    static OmniSearchBox *instance();

    /// Show the box, anchored to the main window, ready for a query
    void open();
    /// Hide the box and undo its transient state (tree highlight, preselection)
    void dismiss();

    OmniSearchEdit *edit() const { return lineEdit; }

    bool eventFilter(QObject *obj, QEvent *event) override;

protected:
    bool event(QEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private Q_SLOTS:
    void onModeChanged(OmniSearch::Mode mode);
    void onObjectResolved(const OmniSearch::ObjectMatch &match);
    void onObjectUnresolved();
    void onCommandChosen(const QByteArray &name);
    void onGroupExpandRequested(const QByteArray &name, const QRect &rect);
    void onParamChosen(const App::ParamInfo *info);
    void onEnterPressed();

private:
    explicit OmniSearchBox(QWidget *parent);
    ~OmniSearchBox() override;

    void place();
    void showPanel(QWidget *panel);
    void hidePanels();
    void selectObject(const OmniSearch::ObjectMatch &match);
    TreeWidget *tree() const;

    OmniSearchEdit *lineEdit = nullptr;
    QWidget *panelHost = nullptr;
    QVBoxLayout *panelLayout = nullptr;
    OmniPropertyPanel *propertyPanel = nullptr;
    OmniParamPanel *paramPanel = nullptr;
    bool menuRunning = false;
};

} // namespace Gui

#endif // GUI_OMNI_SEARCH_BOX_H
