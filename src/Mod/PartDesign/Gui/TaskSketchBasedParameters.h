/***************************************************************************
 *   Copyright (c) 2013 Jan Rheinländer                                    *
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


#ifndef GUI_TASKVIEW_TaskSketchBasedParameters_H
#define GUI_TASKVIEW_TaskSketchBasedParameters_H

#include <QStandardItemModel>
#include <QItemDelegate>
#include <QGroupBox>

#include <Gui/Selection.h>
#include <boost/signals2/connection.hpp>
#include "ViewProvider.h"

#include "ReferenceSelection.h"
#include "TaskFeatureParameters.h"
#include "EnumFlags.h"

class QComboBox;
class QCheckBox;
class QListWidget;

namespace App {
class Property;
}

namespace Gui {
class PrefQuantitySpinBox;
}

namespace PartDesignGui {

class LinkSubWidget;

/// Convenience class to collect common methods for all SketchBased features
class TaskSketchBasedParameters : public PartDesignGui::TaskFeatureParameters,
                                  public Gui::SelectionObserver
{
    Q_OBJECT

public:
    TaskSketchBasedParameters(PartDesignGui::ViewProvider* vp, QWidget *parent,
                              const std::string& pixmapname, const QString& parname);
    ~TaskSketchBasedParameters() override;

    enum class SelectionMode {
        none,
        refAdd,
        refProfile,
        refAxis,
        refSpine,
        refAuxSpine,
        refSection,
    };

    virtual void _onSelectionChanged(const Gui::SelectionChanges&) {}

    const QString onSelectUpToFace(const Gui::SelectionChanges& msg);

    void onSelectReference(QWidget *blinkWidget,
                           const AllowSelectionFlags &conf = ReferenceSelection::defaultFlags())
    {
        onSelectReference(blinkWidget, SelectionMode::refAdd, conf);
    }

    void onSelectReference(QWidget *blinkWidget,
                           SelectionMode mode,
                           const AllowSelectionFlags &conf = ReferenceSelection::defaultFlags());
    void exitSelectionMode(bool clearSelection=true);

    SelectionMode getSelectionMode() const;

    void setSelectionMode(SelectionMode mode, Gui::SelectionGate *gate);

    virtual void onSelectionModeChanged(SelectionMode oldMode) {(void)oldMode;}

    QVariant setUpToFace(const QString& text);
    /// Try to find the name of a feature with the given label.
    /// For faster access a suggested name can be tested, first.
    QVariant objectNameByLabel(const QString& label, const QVariant& suggest) const;

    QString getFaceReference(const QString& obj, const QString& sub) const;

    /// Create a label for the 2D feature: the objects name if it's already 2D,
    /// or the subelement's name if the object is a solid.
    QString make2DLabel(const App::DocumentObject* section,
                        const std::vector<std::string>& subValues);


    void saveHistory();

    void initUI(QWidget *);
    void addProfileEdit(QBoxLayout *boxLayout);
    void addFittingWidgets(QBoxLayout *parentLayout);
    void _refresh();

    boost::signals2::signal<void ()> signalSelectionModeChanged;

protected Q_SLOTS:
    void onFitChanged(double);
    void onFitJoinChanged(int);
    void onInnerFitChanged(double);
    void onInnerFitJoinChanged(int);

private:
    void onSelectionChanged(const Gui::SelectionChanges& msg) final;
    void _exitSelectionMode();

protected:
    Gui::PrefQuantitySpinBox * fitEdit = nullptr;
    QComboBox *fitJoinType = nullptr;
    Gui::PrefQuantitySpinBox * innerFitEdit = nullptr;
    QComboBox *innerFitJoinType = nullptr;
    QWidget *blinkWidget = nullptr;

    LinkSubWidget *profileWidget = nullptr;

private:
    SelectionMode selectionMode = SelectionMode::none;
    Gui::SelectionGate *selectionGate = nullptr;
};

class LinkSubWidgetDelegate : public QItemDelegate
{
    Q_OBJECT

public:
    LinkSubWidgetDelegate(QObject *parent);

    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &option,
                          const QModelIndex &index) const;

    void setEditorData(QWidget *editor, const QModelIndex &index) const;
    void setModelData(QWidget *editor, QAbstractItemModel *model,
                      const QModelIndex &index) const;
};

class LinkSubWidget: public QWidget
                   , public Gui::SelectionObserver
{
    Q_OBJECT

public:
    LinkSubWidget(TaskSketchBasedParameters *parent,
                  const QString &title,
                  App::PropertyLinkSub &prop,
                  bool singleElement=false,
                  QPushButton *_button=nullptr,
                  QListWidget *_listWidget=nullptr,
                  QPushButton *_clearButton=nullptr);

    void onSelectionChanged(const Gui::SelectionChanges& msg) override;

    void onButton(bool checked);
    void onClear();
    void onDelete();
    void refresh();

    bool setLinks(const std::vector<App::SubObjectT> &objs);
    bool addLink(const App::SubObjectT &obj);

    App::PropertyLinkSub *getProperty(App::DocumentObject **pObj = nullptr) const {
        if (auto obj = linkProp.getObject()) {
            if (pObj)
                *pObj = obj;
            return Base::freecad_dynamic_cast<App::PropertyLinkSub>(
                    obj->getPropertyByName(linkProp.getPropertyName().c_str()));
        }
        return nullptr;
    }

    void setSelectionConfig(const AllowSelectionFlags &conf);
    void setSelectionMode(TaskSketchBasedParameters::SelectionMode mode);

protected:
    bool eventFilter(QObject *, QEvent *) override;
    void setListWidgetHeight(bool expand);
    void toggleShowOnTop(bool init=false);
    void disableShowOnTop();

protected:
    friend class LinkSubWidgetDelegate;
    TaskSketchBasedParameters *parentTask;
    TaskSketchBasedParameters::SelectionMode selectionMode;
    QListWidget *listWidget;
    QPushButton *button;
    QPushButton *clearButton = nullptr;
    boost::signals2::scoped_connection conn;
    App::SubObjectT lastReference;
    App::DocumentObjectT linkProp;
    AllowSelectionFlags selectionConf;
    boost::signals2::scoped_connection connModeChange;
    bool singleElement = false;
    bool linkInited = false;
};

class LinkSubListWidget: public QGroupBox
                       , public Gui::SelectionObserver
{
    Q_OBJECT

public:
    LinkSubListWidget(TaskSketchBasedParameters *parent,
                      const QString &groupTitle,
                      const QString &title,
                      App::PropertyLinkSubList &prop);

    void onSelectionChanged(const Gui::SelectionChanges& msg) override;

    void onButton(bool checked);
    void onDelete();
    void onItemMoved();
    void refresh();
    void addItem(App::DocumentObject *obj, const std::vector<std::string> &subs, bool select=false);

    bool addLinks(const std::vector<App::SubObjectT> &objs);

    App::PropertyLinkSubList *getProperty(App::DocumentObject **pObj = nullptr) const {
        if (auto obj = linkProp.getObject()) {
            if (pObj)
                *pObj = obj;
            return Base::freecad_dynamic_cast<App::PropertyLinkSubList>(
                    obj->getPropertyByName(linkProp.getPropertyName().c_str()));
        }
        return nullptr;
    }

    void setSelectionConfig(const AllowSelectionFlags &conf);
    void setSelectionMode(TaskSketchBasedParameters::SelectionMode mode);

protected:
    bool eventFilter(QObject *, QEvent *) override;
    void toggleShowOnTop(bool init=false);
    void disableShowOnTop();

    bool addSections(const std::vector<App::SubObjectT> &objs);
    void addSectionItem(App::DocumentObject *obj,
                        const std::vector<std::string> &subs,
                        bool select);

protected:
    TaskSketchBasedParameters *parentTask;
    TaskSketchBasedParameters::SelectionMode selectionMode;
    QListWidget *listWidget;
    QPushButton *button;
    boost::signals2::scoped_connection conn;
    std::vector<App::SubObjectT> lastReferences;
    App::DocumentObjectT linkProp;
    AllowSelectionFlags selectionConf;
    boost::signals2::scoped_connection connModeChange;
    bool linkInited = false;
};


class TaskDlgSketchBasedParameters : public PartDesignGui::TaskDlgFeatureParameters
{
    Q_OBJECT

public:
    explicit TaskDlgSketchBasedParameters(PartDesignGui::ViewProvider *vp);
    ~TaskDlgSketchBasedParameters() override;

public:
    /// is called by the framework if the dialog is accepted (Ok)
    bool accept() override;
    /// is called by the framework if the dialog is rejected (Cancel)
    bool reject() override;
};

} //namespace PartDesignGui

#endif // GUI_TASKVIEW_TaskSketchBasedParameters_H
