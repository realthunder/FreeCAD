/***************************************************************************
 *   Copyright (c) 2002 Jürgen Riegel <juergen.riegel@web.de>              *
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

#ifndef GUI_DOCKWND_SELECTIONVIEW_H
#define GUI_DOCKWND_SELECTIONVIEW_H


#include <QMenu>
#include <QTimer>
#include <QPointer>

class QTreeWidget;
class QTreeWidgetItem;

// Not upstream's quoted "DockWindow.h": this header has consumers outside
// src/Gui (six of them), and a quoted include resolves relative to THIS
// file's directory, which is no longer where DockWindow.h lives.
#include <Gui/DockWindow.h>
#include "Selection.h"

class QCheckBox;
class QLabel;

namespace App {
class DocumentObject;
class SubObjectT;
}

namespace Gui {
namespace DockWnd {

/** A test class. A more elaborate class description.
 */
class SelectionView : public Gui::DockWindow,
                      public Gui::SelectionObserver
{
    Q_OBJECT

public:
    /**
     * A constructor.
     * A more elaborate description of the constructor.
     */
    explicit SelectionView(Gui::Document* pcDocument, QWidget *parent=nullptr);

    /**
     * A destructor.
     * A more elaborate description of the destructor.
    */
    ~SelectionView() override;

    /// Observer message from the Selection
    void onSelectionChanged(const SelectionChanges& msg) override;

    void leaveEvent(QEvent*) override;

    bool onMsg(const char* pMsg,const char** ppReturn) override;

    const char *getName() const override {return "SelectionView";}

    /// get called when the document is changed or updated
    void onUpdate() override;

    QTreeWidget* selectionView;
    QLabel*      countLabel;

    QCheckBox *enablePickList;
    QTreeWidget *pickList;

public Q_SLOTS:
    /// get called when text is entered in the search box
    void search(const QString& text);
    /// get called when enter is pressed in the search box
    void validateSearch();
    /// get called when the list is right-clicked
    void onItemContextMenu(const QPoint& point);
    /// different actions
    void select(QTreeWidgetItem* item=nullptr);
    void deselect();
    void zoom();
    void treeSelect();
    void toPython();
    void touch();
    void showPart();
    void onEnablePickList();
    void toggleSelect(QTreeWidgetItem* item=nullptr);
    void preselect(QTreeWidgetItem* item=nullptr);

protected:
    void showEvent(QShowEvent *) override;
    void hideEvent(QHideEvent *) override;

private:
    float x,y,z;
    std::vector<App::SubObjectT> searchList;
    bool openedAutomatically;
};

} // namespace DockWnd

class GuiExport SelectionMenu : public QMenu {
    Q_OBJECT
public:
    SelectionMenu(QWidget *parent=nullptr);

    /** Populate and show the menu for picking geometry elements.
     *
     * @param sels: a list of geometry element references
     * @return Return the picked geometry reference
     *
     * The menu will be dividied into submenus that are grouped by element type.
     */
    App::SubObjectT doPick(const std::vector<App::SubObjectT> &sels);

    /** Populate and show the menu for geometry history tracing,
     *
     * @param sels: a list of geometry element references
     * @return Return the picked geometry reference
     *
     * The menu will be not be grouped using element type, but may group by
     * objects if there are more than one element per object.
     */
    App::SubObjectT doPick(const std::vector<App::SubObjectT> &sels,
                           const App::SubObjectT &context);

public Q_SLOTS:
    void onHover(QAction *);
    void onSubMenu();
    void beforeShow();
    void onSelUpMenu();

protected:
    bool eventFilter(QObject *, QEvent *) override;
    void leaveEvent(QEvent *e) override;
    App::SubObjectT onPicked(QAction *);

private:
    QPointer<QMenu> activeMenu;
    QPointer<QAction> activeAction;
};


class GuiExport SelUpMenu : public QMenu
{
    Q_OBJECT
public:
    SelUpMenu(QWidget *parent, bool trigger=true);

protected:
    void mouseReleaseEvent(QMouseEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    bool event(QEvent *) override;

public Q_SLOTS:
    void onTriggered(QAction *action);
    void onHovered(QAction *action);
};

} // namespace Gui

#endif // GUI_DOCKWND_SELECTIONVIEW_H
