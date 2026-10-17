/***************************************************************************
 *   Copyright (c) 2012 Jan Rheinländer                                    *
 *                                   <jrheinlaender@users.sourceforge.net> *
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

#ifndef GUI_TASKVIEW_TaskDressUpParameters_H
#define GUI_TASKVIEW_TaskDressUpParameters_H

#include <fastsignals/signal.h>

#include <QBrush>

#include <Base/Color.h>
#include <Gui/TaskView/TaskView.h>
#include <Mod/PartDesign/App/FeatureDressUp.h>

#include "TaskFeatureParameters.h"
#include "ViewProviderDressUp.h"

class QAction;
class QIcon;
class QTreeWidget;
class QTreeWidgetItem;

namespace Part {
    class Feature;
}

namespace PartDesignGui {

class TaskDressUpParameters : public TaskFeatureParameters, public Gui::SelectionObserver
{
    Q_OBJECT

public:
    TaskDressUpParameters(ViewProviderDressUp *DressUpView, bool selectEdges, bool selectFaces, QWidget* parent = nullptr);
    ~TaskDressUpParameters() override;

    std::vector<std::string> getReferences() const;
    Part::Feature *getBase() const;

    void setup(QLabel *msg, QTreeWidget *widget, QCheckBox *btnAdd, bool touched=false);

    /// Apply the changes made to the object to it
    virtual void apply() override {}

    bool getItemElement(QTreeWidgetItem *item, std::string &subname);
    void lightPreviewFace(const std::string &subname);
    /** The row of the reference whose dress-up made \a element of the
     * preview ("Face2", as the preview names it): the reverse of
     * getItemElement(). Null for none, or more than one.
     */
    QTreeWidgetItem *getPreviewItem(const char *element);
    /** The base's edge (face) a present reference names, for \a element of
     * the preview: what a pick of it is taken for in the 3D view
     * (ViewProviderDressUp::redirectEditingPick). False for a missing one,
     * which has none.
     */
    bool getPreviewTarget(const char *element, App::SubObjectT &target);

protected Q_SLOTS:
    void onButtonRefAdd(const bool checked);
    void onItemEntered(QTreeWidgetItem* current, int);
    void onItemSelectionChanged();
    virtual void onTimer();
    virtual void onRefDeleted();

protected:
    void exitSelectionMode();
    App::DocumentObject *getInEdit(std::string &subname, App::DocumentObject *base=nullptr);
    App::SubObjectT getInEdit(App::DocumentObject *base=nullptr, const char *sub=nullptr);
    bool showOnTop(bool enable, std::vector<App::SubObjectT> &&objs = {});
    bool syncItems(const std::vector<App::SubObjectT> &sels = {});
    void recompute();
    bool populate(bool refresh=false);
    void refresh() override;
    void showMessage(const char *msg=nullptr);
    virtual void onNewItem(QTreeWidgetItem *) {}
    /// Whether a top-level row is a reference of Base (a fillet's corner row
    /// is not: Corners keeps it)
    virtual bool isBaseItem(QTreeWidgetItem *) const { return true; }
    /// Brings what keeps the rows that are not Base's in step with them
    virtual void syncOtherItems() {}
    /// Adds the rows that are not Base's, after the tree is built from Base
    virtual void populateOtherItems() {}
    void finishedRecomputeFeature() override; 

    QTreeWidgetItem *getCurrentItem() const;
    void createAddAllEdgesAction(QTreeWidget* parentList);

    /** The colour of an edge group's handles (docs/OnViewValueEditing.md 3.3,
     * the reporter 2026-10-14 13:54: "each group show its own color. choose
     * color from a set of distinct ones (auto generated)"), a fillet's and a
     * chamfer's alike
     */
    static Base::Color groupColor(int group);
    /// A swatch of a group's colour, for its menu item and its panel row
    static QIcon groupIcon(int group);

    void addAllEdges();

protected:
    enum selectionModes { none, refToggle, plane, line };
    virtual void clearButtons(const selectionModes notThis);
    void onSelectionChanged(const Gui::SelectionChanges& msg) override;
    /// A selection of the preview, taken as one of the row its element
    /// names; false when it is not one
    bool onPreviewSelected(const Gui::SelectionChanges& msg);
    /** A preselection in the 3D view -- the base's edge, or the preview's
     * face of a missing one -- lights its row as hovered and its preview
     * face; the reverse of a hover of the row (onTimer()), entry 169
     */
    void onPreselectChanged(const Gui::SelectionChanges& msg);
    /// Shows \a item as hovered, scrolled into view; null for none
    void setHoverItem(QTreeWidgetItem *item);
    virtual void changeEvent(QEvent *e) override = 0;
    bool event(QEvent *e) override;
    bool handleEvent(QEvent *e);

    ViewProviderDressUp* getDressUpView() const
    { return static_cast<ViewProviderDressUp*>(vp); }

    bool eventFilter(QObject *o, QEvent *e) override;

protected:
    QWidget* proxy;

    bool allowFaces, allowEdges;
    /// Whether a vertex can be picked, for a fillet's corners
    bool allowVertexes = false;
    selectionModes selectionMode;    

    QAction* deleteAction = nullptr;
    QAction* addAllEdgesAction = nullptr;
    QTreeWidget *treeWidget = nullptr;
    QCheckBox *btnAdd = nullptr;
    QLabel *messageLabel = nullptr;

    bool onTopEnabled;
    bool busy = false;

    std::vector<App::SubObjectT> onTopObjs;

    QTimer *timer = nullptr;
    QObject *enteredObject = nullptr;
    /// The row shown as hovered for a preselection in the 3D view, and the
    /// backgrounds it had
    QTreeWidgetItem *hoverItem = nullptr;
    std::vector<QBrush> hoverBrushes;
    /// The panel itself preselects, for a hover of its list
    bool hoverFromList = false;
};

/// simulation dialog for the TaskView
class TaskDlgDressUpParameters : public TaskDlgFeatureParameters
{
    Q_OBJECT

public:
    explicit TaskDlgDressUpParameters(ViewProviderDressUp *DressUpView);
    ~TaskDlgDressUpParameters() override;

    ViewProviderDressUp* getDressUpView() const
    { return static_cast<ViewProviderDressUp*>(vp); }
    TaskDressUpParameters* getDressUpParameters() const
    { return parameter; }

public:
    /// is called by the framework if the dialog is accepted (Ok)
    bool accept() override;
    bool reject() override;

protected:
    TaskDressUpParameters  *parameter;
};

} //namespace PartDesignGui

#endif // GUI_TASKVIEW_TaskDressUpParameters_H
