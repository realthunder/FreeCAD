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

#include <boost/signals2.hpp>

#include <Gui/TaskView/TaskView.h>
#include <Mod/PartDesign/App/FeatureDressUp.h>

#include "TaskFeatureParameters.h"
#include "ViewProviderDressUp.h"

class QAction;
class QTreeWidget;
class QTreeWidgetItem;

namespace Part {
    class Feature;
}

namespace PartDesignGui {

class TaskDressUpParameters : public Gui::TaskView::TaskBox, public Gui::SelectionObserver
{
    Q_OBJECT

public:
    TaskDressUpParameters(ViewProviderDressUp *DressUpView, bool selectEdges, bool selectFaces, QWidget* parent = nullptr);
    ~TaskDressUpParameters() override;

    std::vector<std::string> getReferences() const;
    Part::Feature *getBase() const;

    void setupTransaction();

    void setup(QLabel *msg, QTreeWidget *widget, QCheckBox *btnAdd, bool touched=false);

    /// Apply the changes made to the object to it
    virtual void apply() {}

    int getTransactionID() const {
        return transactionID;
    }

    bool getItemElement(QTreeWidgetItem *item, std::string &subname);

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
    virtual void refresh();
    void showMessage(const char *msg=nullptr);
    virtual void onNewItem(QTreeWidgetItem *) {}

    QTreeWidgetItem *getCurrentItem() const;
    void createAddAllEdgesAction(QTreeWidget* parentList);

    void addAllEdges();

protected:
    enum selectionModes { none, refToggle, plane, line };
    virtual void clearButtons(const selectionModes notThis);
    void onSelectionChanged(const Gui::SelectionChanges& msg) override;
    virtual void changeEvent(QEvent *e) override = 0;
    bool event(QEvent *e) override;
    bool handleEvent(QEvent *e);

    ViewProviderDressUp* getDressUpView() const
    { return DressUpView; }

    bool eventFilter(QObject *o, QEvent *e) override;

protected:
    QWidget* proxy;
    ViewProviderDressUp *DressUpView;

    bool allowFaces, allowEdges;
    selectionModes selectionMode;    
    int transactionID = 0;

    QAction* deleteAction = nullptr;
    QAction* addAllEdgesAction = nullptr;
    QTreeWidget *treeWidget = nullptr;
    QCheckBox *btnAdd = nullptr;
    QLabel *messageLabel = nullptr;

    bool onTopEnabled;
    bool busy = false;

    std::vector<App::SubObjectT> onTopObjs;

    boost::signals2::scoped_connection connUndo;
    boost::signals2::scoped_connection connRedo;
    boost::signals2::scoped_connection connDelete;
    boost::signals2::scoped_connection connDeleteDoc;

    QTimer *timer = nullptr;
    QObject *enteredObject = nullptr;
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

public:
    /// is called by the framework if the dialog is accepted (Ok)
    bool accept() override;
    bool reject() override;

protected:
    TaskDressUpParameters  *parameter;
};

} //namespace PartDesignGui

#endif // GUI_TASKVIEW_TaskDressUpParameters_H
