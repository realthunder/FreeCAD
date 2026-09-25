/***************************************************************************
 *   Copyright (c) 2014 Abdullah Tahiri <abdullah.tahiri.yo@gmail.com>     *
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


#ifndef GUI_TASKVIEW_TaskSketcherElements_H
#define GUI_TASKVIEW_TaskSketcherElements_H

#include <Gui/TaskView/TaskView.h>
#include <Gui/Selection.h>
#include <fastsignals/signal.h>
#include <QTreeWidget>
#include <QIcon>
#include <map>

namespace App
{
class Property;
}

namespace SketcherGui
{

class ViewProviderSketch;
class Ui_TaskSketcherElements;

class ElementView : public QTreeWidget
{
    Q_OBJECT

    typedef QTreeWidget inherited;

public:
    explicit ElementView(QWidget *parent = nullptr);
    ~ElementView() override;


    /// The rectangle of \a item's icon in column 0, in viewport coordinates.
    /// The icon is a button: it drops down the element's parts.
    QRect iconRect(QTreeWidgetItem *item) const;

Q_SIGNALS:
    /// \a item's icon was clicked; \a globalPos is where its menu goes
    void partButtonClicked(QTreeWidgetItem *item, const QPoint &globalPos);

protected:
    void contextMenuEvent (QContextMenuEvent* event);
    void mousePressEvent(QMouseEvent *event) override;
    bool viewportEvent(QEvent *event) override;

protected Q_SLOTS:
    void deleteSelectedItems();
    void convertTextToGeometry();
};

class ElementFilterList;

class TaskSketcherElements: public Gui::TaskView::TaskBox, public Gui::SelectionObserver
{
    Q_OBJECT

public:
    explicit TaskSketcherElements(ViewProviderSketch *sketchView);
    ~TaskSketcherElements() override;
    /// Editing has ended: empty the tree, so no item reads the sketch while
    /// the dialog waits for its deferred deletion
    void sketchClosed();

    /// Observer message from the Selection
    void onSelectionChanged(const Gui::SelectionChanges& msg);

private:
    void slotElementsChanged(void);
    /// rebuilds the list when the groups of the sketch changed, and only then
    void slotConstraintsChanged(void);
    /// geoId -> the constraint type it is a handle of, or None for a group member
    std::map<int, int> collectGroupRoles() const;
    /// every row's icon, from the part of its element selected last
    void updateIcons();
    /// Rebuild the scene selection from the rows' part flags
    void syncSceneSelection();
    void updatePreselection();
    /// filterState: the Mode filter as a bitmask, see filterState()
    void updateVisibility(int filterState);
    void setItemVisibility(int elementindex,int filterState);
    /// The Mode filter's ticks, one bit per entry of its list
    int filterState() const;
    void updateFilterButton();
    void clearWidget();

public Q_SLOTS:
    void on_elementsWidget_itemSelectionChanged(void); 
    void on_elementsWidget_itemEntered(QTreeWidgetItem *item);
    void onFilterItemChanged(QListWidgetItem *item);
    void onPartButtonClicked(QTreeWidgetItem *item, const QPoint &globalPos);
    void on_elementsWidget_itemChanged(QTreeWidgetItem *item, int column);

public:
    /// Move an internal geometry to a visual layer (0 shown, 2 hidden), in
    /// its own transaction; hiding it also deselects it.
    void setGeometryLayer(int geoId, int layer);

protected:
    void changeEvent(QEvent *e) override;
    void leaveEvent ( QEvent * event ) override;
    ViewProviderSketch *sketchView;
    using Connection = fastsignals::connection;
    Connection connectionElementsChanged;
    Connection connectionConstraintsChanged;

private:
    QWidget* proxy;
    QListWidget* filterList = nullptr;
    std::unique_ptr<Ui_TaskSketcherElements> ui;
    int focusItemIndex;
    int previouslySelectedItemIndex;

    std::map<int,QTreeWidgetItem*> itemMap;
    /// what the list was last built for, so a constraint change that does not touch a
    /// group does not rebuild it
    std::map<int, int> groupRoles;
    

    bool inhibitSelectionUpdate;
};

}  // namespace SketcherGui

#endif  // GUI_TASKVIEW_TASKAPPERANCE_H
