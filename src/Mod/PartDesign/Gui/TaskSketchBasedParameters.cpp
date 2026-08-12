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


#include "PreCompiled.h"

#ifndef _PreComp_
# include <sstream>
# include <QAction>
# include <QApplication>
# include <QRegularExpression>
# include <QRegularExpressionMatch>
# include <QTextStream>
# include <QListWidget>
# include <QMessageBox>
# include <QScrollBar>
# include <Precision.hxx>
#endif

#include <boost/algorithm/string/predicate.hpp>

#include <Base/Tools.h>
#include <Base/Console.h>
#include <App/Application.h>
#include <App/Document.h>
#include <App/Origin.h>
#include <App/MappedElement.h>
#include <Gui/Application.h>
#include <Gui/CommandT.h>
#include <Gui/Document.h>
#include <Gui/Selection.h>
#include <Gui/Command.h>
#include <Gui/MainWindow.h>
#include <Gui/MetaTypes.h>
#include <Gui/PrefWidgets.h>

#include <Mod/Part/App/DatumFeature.h>
#include <Mod/Part/App/FeatureOffset.h>
#include <Mod/PartDesign/App/Body.h>
#include <Mod/PartDesign/App/FeatureSketchBased.h>
#include <Mod/PartDesign/App/FeatureExtrusion.h>
#include <Mod/Sketcher/App/SketchObject.h>

#include "TaskSketchBasedParameters.h"
#include "ReferenceSelection.h"
#include "Utils.h"

FC_LOG_LEVEL_INIT("PartDesignGui",true,true)

using namespace PartDesignGui;
using namespace Gui;

namespace {
struct SubInfo
{
    App::SubObjectT objT;
    std::vector<std::string> subs;
    SubInfo(){}
    SubInfo(const App::SubObjectT &objT,
            const std::vector<std::string> &subs)
        :objT(objT)
        ,subs(subs)
    {}
};
} //anonymous namespace
Q_DECLARE_METATYPE(SubInfo)

/* TRANSLATOR PartDesignGui::TaskSketchBasedParameters */

LinkSubWidgetDelegate::LinkSubWidgetDelegate(QObject *parent) : QItemDelegate(parent)
{
}

QWidget *LinkSubWidgetDelegate::createEditor(QWidget *parent, const QStyleOptionViewItem &/* option */,
                                             const QModelIndex & index) const
{
    auto owner = qobject_cast<LinkSubWidget*>(this->parent());
    if (!owner)
        return nullptr;
    App::DocumentObject *obj;
    auto prop = owner->getProperty(&obj);
    if (!prop)
        return nullptr;
    App::ObjectIdentifier path(*prop);
    if (obj->getExpression(path).expression && index.row() != 0)
        return nullptr;
    if (index.row() != 0)
        return new QLineEdit(parent);
    auto editor = new Gui::ExpLineEdit(parent);
    editor->bind(path);
    return editor;
}

void LinkSubWidgetDelegate::setEditorData(QWidget *editor, const QModelIndex &index) const
{
    auto lineEdit = qobject_cast<QLineEdit*>(editor);
    if (!lineEdit)
        return;
    auto owner = qobject_cast<LinkSubWidget*>(this->parent());
    if (!owner)
        return;
    App::DocumentObject *obj;
    auto prop = owner->getProperty(&obj);
    if (!prop)
        return;
    auto link = prop->getValue();
    if (!link)
        return;

    if (index.row() == 0) {
        lineEdit->setText(QString::fromUtf8(link->getNameInDocument()));
        return;
    }
    const auto &subs = prop->getSubValues(false);
    if (index.row()-1 < (int)subs.size())
        lineEdit->setText(QString::fromUtf8(subs[index.row()-1].c_str()));
}

void LinkSubWidgetDelegate::setModelData(QWidget *editor, QAbstractItemModel *model,
                                        const QModelIndex &index) const
{
    (void)model;
    auto lineEdit = qobject_cast<QLineEdit*>(editor);
    if (!lineEdit)
        return;
    auto owner = qobject_cast<LinkSubWidget*>(this->parent());
    if (!owner)
        return;
    App::DocumentObject *obj;
    auto prop = owner->getProperty(&obj);
    if (!prop)
        return;
    App::ObjectIdentifier path(*prop);
    if (obj->getExpression(path).expression)
        return;

    auto item = owner->listWidget->item(0);
    if (!item)
        return;
    App::DocumentObject *link = qvariant_cast<App::SubObjectT>(item->data(Qt::UserRole)).getSubObject();
    std::vector<std::string> subs;
    for (int i=1;;++i) {
        auto item = owner->listWidget->item(i);
        if (!item)
            break;
        subs.push_back(item->text().toUtf8().constData());
    }
    if (index.row() == 0) {
        auto newLink = obj->getDocument()->getObject(lineEdit->text().toUtf8().constData());
        if (!newLink)
            QMessageBox::critical(Gui::getMainWindow(),
                    QObject::tr("Error"), QObject::tr("Object not found"));
        link = newLink;
    } else if (index.row() - 1 < (int)subs.size())
        subs[index.row()-1] = lineEdit->text().toUtf8().constData();

    if (!link)
        return;

    owner->parentTask->setupTransaction();
    prop->setValue(link, std::move(subs));
    owner->refresh();
    owner->parentTask->recomputeFeature();
}

LinkSubWidget::LinkSubWidget(TaskSketchBasedParameters *parent,
                             const QString &title,
                             App::PropertyLinkSub &prop,
                             bool singleElement,
                             QPushButton *_button,
                             QListWidget *_listWidget,
                             QPushButton *_clearButton)
    :QWidget(parent)
    ,parentTask(parent)
    ,selectionMode(TaskSketchBasedParameters::SelectionMode::refAdd)
    ,linkProp(&prop)
    ,singleElement(singleElement)
{


    selectionConf.setFlag(AllowSelection::EDGE);
    selectionConf.setFlag(AllowSelection::FACE);
    selectionConf.setFlag(AllowSelection::PLANAR, false);
    selectionConf.setFlag(AllowSelection::WHOLE);
    selectionConf.setFlag(AllowSelection::WIRE);
    selectionConf.setFlag(AllowSelection::POINT);

    if (_button && _listWidget) {
        button = _button;
        listWidget = _listWidget;
        clearButton = _clearButton;
        setVisible(false);
    }
    else {
        QHBoxLayout *hlayout = new QHBoxLayout();
        hlayout->setSpacing(2);
        setLayout(hlayout);
        hlayout->setContentsMargins(0,0,0,0);
        setContentsMargins(0,0,0,0);
        button = new QPushButton(this);
        hlayout->addWidget(button);

        listWidget = new QListWidget(this);
        hlayout->addWidget(listWidget);

        clearButton = new QPushButton(this);
        hlayout->addWidget(clearButton);
    }
    button->setText(title);
    button->setCheckable(true);
    QObject::connect(button, &QPushButton::clicked, [this](bool checked) {onButton(checked);});
    if (button->toolTip().isEmpty())
        button->setToolTip(tr("Click to enter selection mode"));

    listWidget->setItemDelegate(new LinkSubWidgetDelegate(this));
    listWidget->setViewMode(QListView::IconMode);
    listWidget->setWrapping(false);
    listWidget->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    listWidget->setMouseTracking(true);
    listWidget->setSpacing(5);
    listWidget->horizontalScrollBar()->installEventFilter(this);
    QObject::connect(listWidget, &QListWidget::itemEntered, [this](QListWidgetItem *item) {
        App::DocumentObject *obj;
        auto prop = getProperty(&obj);
        if (!prop)
            return;
        if (auto link = prop->getValue()) {
            if (item == listWidget->item(0))
                PartDesignGui::highlightObjectOnTop(link);
            else
                PartDesignGui::highlightObjectOnTop(App::SubObjectT(link, item->text().toUtf8().constData()));
        }
    });
    listWidget->installEventFilter(this);
    setListWidgetHeight(listWidget->horizontalScrollBar()->isVisible());

    QAction* remove = new QAction(tr("Remove"), this);
    remove->setShortcut(QKeySequence::Delete);
    listWidget->addAction(remove);
    listWidget->setContextMenuPolicy(Qt::ActionsContextMenu);
    QObject::connect(remove, &QAction::triggered, [this](){onDelete();});

    if (clearButton) {
        clearButton->setIcon(Gui::BitmapFactory().pixmap("edit-cleartext"));
        if (clearButton->toolTip().isEmpty())
            clearButton->setToolTip(tr("Temporary clear link references for new selection"));
        QObject::connect(clearButton, &QPushButton::clicked, [this]() {onClear();});
    }

    if (auto prop = getProperty()) {
        conn = prop->signalChanged.connect([this](const App::Property &) {toggleShowOnTop();});
    }

    connModeChange = parentTask->signalSelectionModeChanged.connect([this]() {
        if (parentTask->getSelectionMode() == selectionMode) {
            toggleShowOnTop(true);
            button->setChecked(true);
        } else if (button->isChecked()) {
            refresh();
            disableShowOnTop();
            button->setChecked(false);
        }
    });
}

void LinkSubWidget::onSelectionChanged(const Gui::SelectionChanges& msg)
{
    if (parentTask->getSelectionMode() != selectionMode)
        return;
    if (msg.Type == Gui::SelectionChanges::AddSelection) {
        App::SubObjectT ref(msg.pOriginalMsg ? msg.pOriginalMsg->Object : msg.Object);
        if (addLink(ref) && singleElement)
            parentTask->exitSelectionMode();
    }
}

void LinkSubWidget::setSelectionConfig(const AllowSelectionFlags &conf)
{
    selectionConf = conf;
}

void LinkSubWidget::onDelete()
{
    auto item = listWidget->currentItem();
    if (!item)
        return;
    auto firstItem = listWidget->item(0);
    if (item == firstItem) {
        onClear();
        return;
    }
    if (auto link = qvariant_cast<App::SubObjectT>(firstItem->data(Qt::UserRole)).getSubObject()) {
        delete item;
        std::vector<App::SubObjectT> sobjs;
        for (int i=1;;++i) {
            item = listWidget->item(i);
            if (!item)
                break;
            sobjs.emplace_back(link, item->text().toUtf8().constData());
        }
        if (sobjs.empty())
            sobjs.emplace_back(link, "");
        setLinks(sobjs);
    }
}

void LinkSubWidget::onButton(bool checked)
{
    if (checked) {
        if (parentTask->getSelectionMode() == TaskSketchBasedParameters::SelectionMode::none) {
            auto sels = Gui::Selection().getSelectionT("*", Gui::ResolveMode::NoResolve);
            if (sels.size()) {
                if (setLinks(sels)) {
                    button->setChecked(false);
                    return;
                }
            }
        }
        parentTask->onSelectReference(button, selectionMode, selectionConf);
    } else {
        parentTask->exitSelectionMode();
        refresh();
    }
}

void LinkSubWidget::toggleShowOnTop(bool init)
{
    auto vp = Base::freecad_dynamic_cast<Gui::ViewProviderDocumentObject>(
                Gui::Application::Instance->getViewProvider(linkProp.getObject()));
    PartDesignGui::toggleShowOnTop(vp, lastReference, linkProp.getPropertyName().c_str(), init);
}

void LinkSubWidget::disableShowOnTop()
{
    auto vp = Base::freecad_dynamic_cast<Gui::ViewProviderDocumentObject>(
                Gui::Application::Instance->getViewProvider(linkProp.getObject()));
    PartDesignGui::toggleShowOnTop(vp, lastReference, nullptr);
}

void LinkSubWidget::onClear()
{
    if (!listWidget)
        return;

    Gui::Selection().clearSelection();
    listWidget->clear();
    if (parentTask->getSelectionMode() != selectionMode)
        onButton(true);
}

void LinkSubWidget::setListWidgetHeight(bool expand)
{
    auto scrollbar = listWidget->horizontalScrollBar();
    int height = button->sizeHint().height() + listWidget->frameWidth() + (expand?scrollbar->height():0);
    listWidget->setMinimumHeight(height);
    listWidget->setMaximumHeight(height);
}

bool LinkSubWidget::eventFilter(QObject *o, QEvent *ev)
{
    switch(ev->type()) {
    case QEvent::Show:
    case QEvent::Hide:
        if (listWidget && o == listWidget->horizontalScrollBar())
            setListWidgetHeight(ev->type() == QEvent::Show);
        break;
    case QEvent::Leave:
        if (o == listWidget)
            Gui::Selection().rmvPreselect();
        break;
    case QEvent::ShortcutOverride:
    case QEvent::KeyPress: {
        QKeyEvent * kevent = static_cast<QKeyEvent*>(ev);
        if (o == listWidget && kevent->modifiers() == Qt::NoModifier) {
            if (kevent->key() == Qt::Key_Delete) {
                kevent->accept();
                if (ev->type() == QEvent::KeyPress)
                    onDelete();
            }
        }
        break;
    }
    default:
        break;
    }
    return false;
}

void LinkSubWidget::refresh()
{
    App::DocumentObject *obj;
    auto prop = getProperty(&obj);
    if (!prop) {
        linkInited = true;
        return;
    }
    QSignalBlocker guard(listWidget);
    listWidget->clear();
    App::ObjectIdentifier path(*prop);
    bool hasExpression = !!obj->getExpression(path).expression;
    auto linkColor = QVariant::fromValue(QApplication::palette().color(QPalette::Link));
    if (auto link = prop->getValue()) {
        App::SubObjectT linkT(link);
        auto item = new QListWidgetItem(QString::fromUtf8(
                    linkT.getObjectFullName(obj->getDocument()->getName()).c_str()));
        listWidget->addItem(item);
        item->setFlags(item->flags() | Qt::ItemIsEditable);
        if (hasExpression)
            item->setData(Qt::ForegroundRole, linkColor);
        item->setData(Qt::UserRole, QVariant::fromValue(linkT));

        std::vector<std::string> subnames;
        for (const auto &sub : prop->getSubValues(false)) {
            if (linkInited || !App::GeoFeature::hasMissingElement(sub.c_str()))
                subnames.push_back(sub);
        }
        bool touched = false;
        if (subnames.size() != prop->getSubValues().size()) {
            subnames.clear();
            const auto &subs = prop->getShadowSubs();
            std::set<std::string> subSet;
            std::string indexedName;
            for (const auto &sub : subs) {
                subSet.insert(sub.second);
                subSet.insert(sub.first);
            }
            for (const auto &sub : subs) {
                subnames.push_back(sub.second);
                if (App::GeoFeature::hasMissingElement(sub.second.c_str())) {
                    auto related = Part::Feature::getRelatedElements(link, sub.first.c_str());
                    if (!related.empty()) {
                        indexedName.clear();
                        const auto &element = related.front();
                        element.index.appendToStringBuffer(indexedName);
                        FC_WARN("guess element reference in " << prop->getFullName() 
                                << ": " << sub.second << " -> " << indexedName);
                        if (subSet.insert(indexedName).second)
                            subnames.back() = indexedName;
                        else
                            subnames.pop_back();
                        touched = true;
                    }
                }
            }
        }
        if (subnames.size() != 1 || !subnames.front().empty()) {
            for (const auto &sub : subnames) {
                auto item = new QListWidgetItem(QString::fromUtf8(sub.c_str()));
                listWidget->addItem(item);
                item->setFlags(item->flags() | Qt::ItemIsEditable);
                if (hasExpression)
                    item->setData(Qt::ForegroundRole, linkColor);
            }
        }
        if (touched) {
            try {
                parentTask->setupTransaction();
                prop->setValue(link, subnames);
                parentTask->recomputeFeature();
            }
            catch (Base::Exception &e) {
                e.ReportException();
            }
        }
    }
    linkInited = true;
}

bool LinkSubWidget::setLinks(const std::vector<App::SubObjectT> &objs)
{
    App::DocumentObject *obj;
    auto prop = getProperty(&obj);
    if (!prop || objs.empty())
        return false;
    try {
        parentTask->setupTransaction();
        if (singleElement) {
            auto ref = PartDesignGui::importExternalElement(objs.front());
            prop->setValue(ref.getObject(), {ref.getSubName()});
        } else if (!PartDesignGui::importExternalElements(*prop, objs))
            return false;
        parentTask->recomputeFeature();
        if (auto o = prop->getValue()) {
            if (!o->isDerivedFrom(PartDesign::Feature::getClassTypeId()))
                o->Visibility.setValue(false);
        }
        refresh();
        return true;
    } catch (Base::Exception &e) {
        e.ReportException();
    }
    return false;
}

bool LinkSubWidget::addLink(const App::SubObjectT &objT)
{
    if (!listWidget)
        return false;
    App::DocumentObject *obj;
    auto prop = getProperty(&obj);
    std::vector<App::SubObjectT> links;
    if (listWidget->count() == 0 || !prop->getValue() || singleElement)
        links.push_back(objT);
    else {
        if (auto linked = prop->getValue()) {
            if (prop->getSubValues().empty()) {
                links.emplace_back(linked, "");
            }
            else {
                for (const auto &sub : prop->getSubValues()) {
                    if (!App::GeoFeature::hasMissingElement(sub.c_str())) {
                        links.emplace_back(linked, sub.c_str());
                    }
                }
            }
        }
        links.push_back(objT);
    }
    if (!setLinks(links))
        return false;
    listWidget->setCurrentItem(listWidget->item(listWidget->count()-1));
    return true;
}

void LinkSubWidget::setSelectionMode(TaskSketchBasedParameters::SelectionMode mode)
{
    selectionMode = mode;
}

//////////////////////////////////////////////////////////////////////////////////////

LinkSubListWidget::LinkSubListWidget(TaskSketchBasedParameters *parent,
                                     const QString &groupTitle,
                                     const QString &title,
                                     App::PropertyLinkSubList &prop)
    :QGroupBox(groupTitle, parent)
    ,parentTask(parent)
    ,selectionMode(TaskSketchBasedParameters::SelectionMode::refSection)
    ,linkProp(&prop)
{
    selectionConf.setFlag(AllowSelection::EDGE);
    selectionConf.setFlag(AllowSelection::FACE);
    selectionConf.setFlag(AllowSelection::PLANAR, false);
    selectionConf.setFlag(AllowSelection::WHOLE);
    selectionConf.setFlag(AllowSelection::WIRE);
    selectionConf.setFlag(AllowSelection::POINT);

    auto layout = new QVBoxLayout();
    setLayout(layout);
    button = new QPushButton(this);
    button->setToolTip(tr("Click to enter selection mode"));
    button->setText(title);
    button->setCheckable(true);
    QObject::connect(button, &QPushButton::clicked, [this](bool checked) {onButton(checked);});
    layout->addWidget(button);

    listWidget = new QListWidget(this);
    listWidget->setMinimumHeight(200);
    listWidget->setMouseTracking(true);
    listWidget->installEventFilter(this);
    layout->addWidget(listWidget);
    QObject::connect(listWidget, &QListWidget::itemEntered, [this](QListWidgetItem *item) {
        PartDesignGui::highlightObjectOnTop(qvariant_cast<SubInfo>(item->data(Qt::UserRole)).objT);
    });
    QObject::connect(listWidget->model(), &QAbstractItemModel::rowsMoved, [this](){onItemMoved();});

    QAction* remove = new QAction(tr("Remove"), this);
    remove->setShortcut(QKeySequence::Delete);
    listWidget->addAction(remove);
    listWidget->setContextMenuPolicy(Qt::ActionsContextMenu);
    QObject::connect(remove, &QAction::triggered, [this](){onDelete();});

    if (auto prop = getProperty()) {
        conn = prop->signalChanged.connect([this](const App::Property &) {toggleShowOnTop();});
    }

    connModeChange = parentTask->signalSelectionModeChanged.connect([this]() {
        if (parentTask->getSelectionMode() == selectionMode) {
            toggleShowOnTop(true);
            button->setChecked(true);
        } else if (button->isChecked()) {
            disableShowOnTop();
            button->setChecked(false);
            refresh();
        }
    });
}

void LinkSubListWidget::onSelectionChanged(const Gui::SelectionChanges& msg)
{
    if (parentTask->getSelectionMode() != selectionMode)
        return;
    if (msg.Type == Gui::SelectionChanges::AddSelection) {
        App::SubObjectT ref(msg.pOriginalMsg ? msg.pOriginalMsg->Object : msg.Object);
        addLinks({ref});
    }
}

void LinkSubListWidget::setSelectionConfig(const AllowSelectionFlags &conf)
{
    selectionConf = conf;
}

void LinkSubListWidget::setSelectionMode(TaskSketchBasedParameters::SelectionMode mode)
{
    selectionMode = mode;
}

bool LinkSubListWidget::addLinks(const std::vector<App::SubObjectT> &objs)
{
    auto prop = getProperty();
    if (!prop)
        return false;
    try {
        auto links = prop->getSubListValues();
        bool touched = false;
        std::set<App::DocumentObjectT> newSections;
        for (auto ref : objs) {
            ref = PartDesignGui::importExternalObject(ref);
            auto refObj = ref.getObject();
            if (!refObj)
                continue;
            bool found = false;
            for (auto &v : links) {
                if (v.first != refObj)
                    continue;
                if (ref.getSubName().empty()) {
                    if (v.second.size() != 1 || v.second.front().size()) {
                        v.second.clear();
                        v.second.emplace_back();
                        touched = true;
                    }
                    found = true;
                    break;
                }
                for (auto it=v.second.begin(); it!=v.second.end();) {
                    if (it->empty()) {
                        it = v.second.erase(it);
                        touched = true;
                    }
                    else if (*it == ref.getSubName()) {
                        found = true;
                        break;
                    } else
                        ++it;
                }
                if (!found) {
                    v.second.push_back(ref.getSubName());
                    touched = found = true;
                }
                break;
            }
            if (!found) {
                newSections.emplace(refObj);
                std::vector<std::string> subs;
                if (!ref.getSubName().empty())
                    subs.push_back(ref.getSubName());
                links.emplace_back(refObj, std::move(subs));
                touched = true;
            }
        }
        if (!touched)
            return false;
        for (auto it = links.begin(); it!=links.end(); ) {
            if (it->second.empty()) {
                ++it;
                continue;
            }
            for (auto itSub = it->second.begin(); itSub!=it->second.end(); ) {
                if (App::GeoFeature::hasMissingElement(itSub->c_str()))
                    itSub = it->second.erase(itSub);
                else
                    ++itSub;
            }
            if (it->second.empty())
                it = links.erase(it);
            else
                ++it;
        }
        parentTask->setupTransaction();
        prop->setSubListValues(links);
        parentTask->recomputeFeature();
        for (const auto &o : newSections) {
            if (auto obj = o.getObject())
                obj->Visibility.setValue(false);
        }
        refresh();
        return true;
    } catch (Base::Exception &e) {
        e.ReportException();
    }
    return false;
}

void LinkSubListWidget::addItem(App::DocumentObject *obj,
                                const std::vector<std::string> &subs,
                                bool select)
{
    auto prop = getProperty();
    if (!prop)
        return;

    App::SubObjectT objT(obj);
    QString label = QString::fromUtf8(objT.getObjectFullName(objT.getDocumentName().c_str()).c_str());
    if (subs.size() && !subs[0].empty()) {
        bool first = true;
        for (const auto &sub : subs) {
            if (first) {
                first = false;
                label += QStringLiteral("(");
            }
            else
                label += QStringLiteral(", ");
            label += QString::fromUtf8(sub.c_str());
        }
        label += QStringLiteral(")");
    }

    QListWidgetItem *item = nullptr;
    for (int i=0; i<listWidget->count(); ++i) {
        auto listItem = listWidget->item(i);
        if (qvariant_cast<SubInfo>(listItem->data(Qt::UserRole)).objT == objT) {
            item = listItem;
            break;
        }
    }
    if (!item) {
        item = new QListWidgetItem(listWidget);
        auto vp = Gui::Application::Instance->getViewProvider(obj);
        if (vp)
            item->setIcon(vp->getIcon());
    }
    item->setData(Qt::UserRole, QVariant::fromValue(SubInfo(objT, subs)));
    item->setText(label);
    if (select) {
        QSignalBlocker blocker(listWidget);
        item->setSelected(true);
        listWidget->scrollToItem(item);
    }
}

void LinkSubListWidget::onDelete()
{
    auto prop = getProperty();
    if (!prop)
        return;

    // Delete the selected profile
    for(auto item : listWidget->selectedItems())
        delete item;

    std::vector<App::PropertyLinkSubList::SubSet> subset;
    for (int i=0; i<listWidget->count(); ++i) {
        auto data = qvariant_cast<SubInfo>(listWidget->item(i)->data(Qt::UserRole));
        if (auto obj = data.objT.getObject())
            subset.emplace_back(obj, data.subs);
    }
    try {
        parentTask->setupTransaction();
        prop->setSubListValues(subset);
        parentTask->recomputeFeature();
    } catch (Base::Exception &e) {
        e.ReportException();
    }
}

void LinkSubListWidget::onItemMoved()
{
    QAbstractItemModel* model = qobject_cast<QAbstractItemModel*>(sender());
    if (!model)
        return;

    auto prop = getProperty();
    if (!prop)
        return;

    std::vector<App::PropertyLinkSubList::SubSet> subset;
    int rows = model->rowCount();
    for (int i = 0; i < rows; i++) {
        QModelIndex index = model->index(i, 0);
        auto data = qvariant_cast<SubInfo>(index.data(Qt::UserRole));
        if (auto obj = data.objT.getObject())
            subset.emplace_back(obj, data.subs);
    }
    try {
        parentTask->setupTransaction();
        prop->setSubListValues(subset);
        parentTask->recomputeFeature();
    } catch (Base::Exception &e) {
        e.ReportException();
    }
}

void LinkSubListWidget::onButton(bool checked) {
    if (checked) {
        if (parentTask->getSelectionMode() == TaskSketchBasedParameters::SelectionMode::none) {
            auto sels = Gui::Selection().getSelectionT("*", Gui::ResolveMode::NoResolve);
            if (sels.size()) {
                if (addLinks(sels)) {
                    button->setChecked(false);
                    return;
                }
            }
        }
        parentTask->onSelectReference(button, selectionMode, selectionConf);
    } else {
        parentTask->exitSelectionMode();
        refresh();
    }
}

bool LinkSubListWidget::eventFilter(QObject *o, QEvent *ev)
{
    switch(ev->type()) {
    case QEvent::Leave:
        Gui::Selection().rmvPreselect();
        break;
    case QEvent::ShortcutOverride:
    case QEvent::KeyPress: {
        QKeyEvent * kevent = static_cast<QKeyEvent*>(ev);
        if (o == listWidget && kevent->modifiers() == Qt::NoModifier) {
            if (kevent->key() == Qt::Key_Delete) {
                kevent->accept();
                if (ev->type() == QEvent::KeyPress)
                    onDelete();
            }
        }
        break;
    }
    default:
        break;
    }
    return false;
}

void LinkSubListWidget::toggleShowOnTop(bool init)
{
    auto vp = Base::freecad_dynamic_cast<Gui::ViewProviderDocumentObject>(
                Gui::Application::Instance->getViewProvider(linkProp.getObject()));
    PartDesignGui::toggleShowOnTop(vp, lastReferences, linkProp.getPropertyName().c_str(), init);
}

void LinkSubListWidget::disableShowOnTop()
{
    auto vp = Base::freecad_dynamic_cast<Gui::ViewProviderDocumentObject>(
                Gui::Application::Instance->getViewProvider(linkProp.getObject()));
    PartDesignGui::toggleShowOnTop(vp, lastReferences, nullptr);
}

void LinkSubListWidget::refresh()
{
    auto prop = getProperty();
    if (!prop)
        return;

    auto subset = prop->getSubListValues();

    if (!linkInited && prop->getShadowSubs().size() == prop->getValues().size()) {
        std::map<App::DocumentObject*, std::set<std::string>> resolveMap;
        size_t i = 0;
        std::string indexedName;
        bool hasResolved = false;
        for (const auto &shadow : prop->getShadowSubs()) {
            auto link = prop->getValues()[i++];

            if (App::GeoFeature::hasMissingElement(shadow.second.c_str())) {
                auto related = Part::Feature::getRelatedElements(link, shadow.first.c_str());
                if (!related.empty()) {
                    hasResolved = true;
                    indexedName.clear();
                    const auto &element = related.front();
                    element.index.appendToStringBuffer(indexedName);
                    FC_WARN("guess element reference in " << prop->getFullName()
                            << ": " << shadow.second << " -> " << element.index);
                    resolveMap[link].insert(indexedName);
                    break;
                }
            }
        }
        if (hasResolved) {
            for (auto &v : subset) {
                auto it = resolveMap.find(v.first);
                if (it == resolveMap.end())
                    continue;
                for (auto itSub = v.second.begin(); itSub != v.second.end();) {
                    if (it->second.count(*itSub))
                        itSub = v.second.erase(itSub);
                    else
                        ++itSub;
                }
                v.second.insert(v.second.end(), it->second.begin(), it->second.end());
            }
            try {
                parentTask->setupTransaction();
                prop->setSubListValues(subset);
                parentTask->recomputeFeature();
            } catch (Base::Exception &e) {
                e.ReportException();
            }
        }
    }

    QSignalBlocker guard(listWidget);
    listWidget->clear();
    for (const auto &v : subset)
        addItem(v.first, v.second);

    linkInited = true;
}


//////////////////////////////////////////////////////////////////////////////////////////

TaskSketchBasedParameters::TaskSketchBasedParameters(PartDesignGui::ViewProvider *vp, QWidget *parent,
                                                     const std::string& pixmapname, const QString& parname)
    : TaskFeatureParameters(vp, parent, pixmapname, parname)
{
}

void TaskSketchBasedParameters::addProfileEdit(QBoxLayout *boxLayout)
{
    if (!vp)
        return;
    PartDesign::ProfileBased* pcSketchBased = static_cast<PartDesign::ProfileBased*>(vp->getObject());
    profileWidget = new LinkSubWidget(this, tr("Profile"), pcSketchBased->Profile);
    profileWidget->setSelectionMode(SelectionMode::refProfile);
    boxLayout->insertWidget(0, profileWidget);
}

void TaskSketchBasedParameters::initUI(QWidget *widget) {
    if(!vp)
        return;

    QBoxLayout * boxLayout = qobject_cast<QBoxLayout*>(widget->layout());
    if (!boxLayout)
        return;

    addProfileEdit(boxLayout);
    PartDesignGui::addTaskCheckBox(vp, widget);
    addOperationCombo(boxLayout);
    addUpdateViewCheckBox(boxLayout);
    addFittingWidgets(boxLayout);
    _refresh();
}

void TaskSketchBasedParameters::addFittingWidgets(QBoxLayout *parentLayout)
{
    if (!vp)
        return;
    PartDesign::ProfileBased* pcSketchBased = static_cast<PartDesign::ProfileBased*>(vp->getObject());
    if (pcSketchBased->Fit.testStatus(App::Property::Hidden))
        return;
    auto groupBox = new QGroupBox(tr("Fitting"));
    auto boxLayout = new QVBoxLayout();
    groupBox->setLayout(boxLayout);

    QHBoxLayout *layout = new QHBoxLayout();
    layout->addWidget(new QLabel(tr("Tolerance"), this));
    fitEdit = new Gui::PrefQuantitySpinBox(this);
    fitEdit->setParamGrpPath(QByteArray("User parameter:BaseApp/History/ProfileFit"));
    fitEdit->bind(pcSketchBased->Fit);
    fitEdit->setUnit(Base::Unit::Length);
    fitEdit->setKeyboardTracking(false);
    fitEdit->setToolTip(QApplication::translate("Property", pcSketchBased->Fit.getDocumentation()));
    layout->addWidget(fitEdit);
    connect(fitEdit, SIGNAL(valueChanged(double)), this, SLOT(onFitChanged(double)));
    boxLayout->addLayout(layout);

    layout = new QHBoxLayout();
    layout->addWidget(new QLabel(tr("Join type")));
    fitJoinType = new QComboBox(this);
    for (int i=0;;++i) {
        const char * type = Part::Offset::JoinEnums[i];
        if (!type)
            break;
        fitJoinType->addItem(tr(type));
    }
    connect(fitJoinType, SIGNAL(currentIndexChanged(int)), this, SLOT(onFitJoinChanged(int)));
    layout->addWidget(fitJoinType);
    boxLayout->addLayout(layout);

    layout = new QHBoxLayout();
    layout->addWidget(new QLabel(tr("Inner fit"), this));
    innerFitEdit = new Gui::PrefQuantitySpinBox(this);
    innerFitEdit->setParamGrpPath(QByteArray("User parameter:BaseApp/History/ProfileInnerFit"));
    innerFitEdit->bind(pcSketchBased->InnerFit);
    innerFitEdit->setUnit(Base::Unit::Length);
    innerFitEdit->setKeyboardTracking(false);
    innerFitEdit->setToolTip(QApplication::translate(
                "Property", pcSketchBased->InnerFit.getDocumentation()));
    layout->addWidget(innerFitEdit);
    connect(innerFitEdit, SIGNAL(valueChanged(double)), this, SLOT(onInnerFitChanged(double)));
    boxLayout->addLayout(layout);

    layout = new QHBoxLayout();
    layout->addWidget(new QLabel(tr("Inner join type")));
    innerFitJoinType = new QComboBox(this);
    for (int i=0;;++i) {
        const char * type = Part::Offset::JoinEnums[i];
        if (!type)
            break;
        innerFitJoinType->addItem(tr(type));
    }
    connect(innerFitJoinType, SIGNAL(currentIndexChanged(int)), this, SLOT(onInnerFitJoinChanged(int)));
    layout->addWidget(innerFitJoinType);
    boxLayout->addLayout(layout);

    parentLayout->addWidget(groupBox);
}

void TaskSketchBasedParameters::_refresh()
{
    if (!vp || !vp->getObject())
        return;

    PartDesign::ProfileBased* pcSketchBased = static_cast<PartDesign::ProfileBased*>(vp->getObject());
    if (fitEdit) {
        QSignalBlocker guard(fitEdit);
        fitEdit->setValue(pcSketchBased->Fit.getValue());
    }

    if (fitJoinType) {
        QSignalBlocker guard(fitJoinType);
        fitJoinType->setCurrentIndex(pcSketchBased->FitJoin.getValue());
    }

    if (innerFitEdit) {
        QSignalBlocker guard(innerFitEdit);
        innerFitEdit->setValue(pcSketchBased->InnerFit.getValue());
    }

    if (innerFitJoinType) {
        QSignalBlocker guard(innerFitJoinType);
        innerFitJoinType->setCurrentIndex(pcSketchBased->InnerFitJoin.getValue());
    }

    if (profileWidget)
        profileWidget->refresh();

    TaskFeatureParameters::_refresh();
}

void TaskSketchBasedParameters::saveHistory(void)
{
    if (fitEdit)
        fitEdit->pushToHistory();
    if (innerFitEdit)
        innerFitEdit->pushToHistory();
    TaskFeatureParameters::saveHistory();
}

void TaskSketchBasedParameters::onFitChanged(double v)
{
    PartDesign::ProfileBased* pcSketchBased = static_cast<PartDesign::ProfileBased*>(vp->getObject());
    pcSketchBased->Fit.setValue(v);
    recomputeFeature();
}

void TaskSketchBasedParameters::onFitJoinChanged(int v)
{
    PartDesign::ProfileBased* pcSketchBased = static_cast<PartDesign::ProfileBased*>(vp->getObject());
    pcSketchBased->FitJoin.setValue((long)v);
    recomputeFeature();
}

void TaskSketchBasedParameters::onInnerFitChanged(double v)
{
    PartDesign::ProfileBased* pcSketchBased = static_cast<PartDesign::ProfileBased*>(vp->getObject());
    pcSketchBased->InnerFit.setValue(v);
    recomputeFeature();
}

void TaskSketchBasedParameters::onInnerFitJoinChanged(int v)
{
    PartDesign::ProfileBased* pcSketchBased = static_cast<PartDesign::ProfileBased*>(vp->getObject());
    pcSketchBased->InnerFitJoin.setValue((long)v);
    recomputeFeature();
}

const QString TaskSketchBasedParameters::onSelectUpToFace(const Gui::SelectionChanges& msg)
{
    // Note: The validity checking has already been done in ReferenceSelection.cpp
    PartDesign::ProfileBased* pcSketchBased = static_cast<PartDesign::ProfileBased*>(vp->getObject());
    App::DocumentObject* selObj = pcSketchBased->getDocument()->getObject(msg.pObjectName);
    if (selObj == pcSketchBased) {
        // The feature itself is selected, trace the selected element back to
        // its base
        auto baseShape = pcSketchBased->getBaseShape(true);
        auto base = pcSketchBased->getBaseObject();
        if (baseShape.isNull() || !base)
            return QString();
        auto history = Part::Feature::getElementHistory(pcSketchBased,msg.pSubName,true,true);
        const char *element = 0;
        std::string tmp;
        for(auto &hist : history) {
            if (hist.obj != base)
                continue;
            tmp.clear();
            element = hist.element.appendToBufferWithPrefix(tmp);
            if (!baseShape.getSubShape(element, true).IsNull())
                break;
            element = nullptr;
        }
        if(element) {
            if(msg.pOriginalMsg) {
                // We are about change the sketched base object shape, meaning
                // that this selected element may be gone soon. So remove it
                // from the selection to avoid warning.
                Gui::Selection().rmvSelection(msg.pOriginalMsg->pDocName,
                                              msg.pOriginalMsg->pObjectName,
                                              msg.pOriginalMsg->pSubName);
            }

            App::SubObjectT sel = (msg.pOriginalMsg ? msg.pOriginalMsg->Object : msg.Object).getParent();
            auto objs = sel.getSubObjectList();
            int i=0, idx = -1;
            for (auto obj : objs) {
                ++i;
                if (obj->getLinkedObject()->isDerivedFrom(PartDesign::Body::getClassTypeId()))
                    idx = i;
            }
            if (idx < 0)
                return QString();
            objs.resize(idx);
            objs.push_back(base);
            sel = App::SubObjectT(objs);
            sel.setSubName((sel.getSubName() + element).c_str());
            Gui::Selection().addSelection(sel);
        }
        return QString();
    }

    App::SubObjectT objT = msg.pOriginalMsg ? msg.pOriginalMsg->Object : msg.Object;

    // Remove subname for planes and datum features
    if (PartDesign::Feature::isDatum(selObj))
        objT.setSubName(objT.getSubNameNoElement());

    objT = PartDesignGui::importExternalObject(objT, false, false);
    if (auto sobj = objT.getSubObject()) {
        pcSketchBased->UpToFace.setValue(sobj, {objT.getOldElementName()});
        recomputeFeature();
        auto subElement = objT.getOldElementName();
        if (subElement.size()) {
            return QStringLiteral("%1:%2").arg(
                    QString::fromUtf8(sobj->getNameInDocument()),
                    QString::fromUtf8(subElement.c_str()));
        } else
            return QString::fromUtf8(sobj->getNameInDocument());
    }

    return QString();
}

void TaskSketchBasedParameters::onSelectReference(QWidget *blinkWidget,
                                                  SelectionMode mode,
                                                  const AllowSelectionFlags &conf)
{
    exitSelectionMode(false);
    if (!vp || mode == SelectionMode::none)
        return;
    PartDesign::ProfileBased* pcSketchBased = dynamic_cast<PartDesign::ProfileBased*>(vp->getObject());
    // The solid this feature will be fused to
    App::DocumentObject* prevSolid = pcSketchBased->getBaseObject( /* silent =*/ true );
    if (blinkWidget) {
        QString altText = tr("Selecting");
        auto prop = blinkWidget->property("blinkText");
        if (prop.isValid())
            altText = prop.toString();
        addBlinkWidget(blinkWidget, altText);
        this->blinkWidget = blinkWidget;
    }
    std::unique_ptr<Gui::SelectionFilterGate> gateRefPtr(new ReferenceSelection(prevSolid, conf));
    std::unique_ptr<Gui::SelectionFilterGate> gateDepPtr(new NoDependentsSelection(pcSketchBased));
    setSelectionMode(mode, new CombineSelectionFilterGates(gateRefPtr, gateDepPtr));
}


void TaskSketchBasedParameters::_exitSelectionMode()
{
    if (selectionMode == SelectionMode::none)
        return;

    if (blinkWidget) {
        removeBlinkWidget(this->blinkWidget);
        blinkWidget = nullptr;
    }
    if (Gui::Selection().currentSelectionGate() == selectionGate)
        Gui::Selection().rmvSelectionGate();
    selectionMode = SelectionMode::none;
    selectionGate = nullptr;
}

void TaskSketchBasedParameters::exitSelectionMode(bool clearSelection)
{
    if (selectionMode != SelectionMode::none) {
        auto oldMode = selectionMode;
        _exitSelectionMode();
        onSelectionModeChanged(oldMode);
        signalSelectionModeChanged();
        if (clearSelection)
            Gui::Selection().clearSelection();
    }
}

void TaskSketchBasedParameters::setSelectionMode(SelectionMode mode, Gui::SelectionGate *gate)
{
    if (mode == selectionMode)
        return;

    exitSelectionMode(false);
    if (mode == SelectionMode::none)
        return;

    auto oldMode = selectionMode;
    Gui::Selection().clearSelection();
    selectionMode = mode;
    selectionGate = gate;
    if (gate)
        Gui::Selection().addSelectionGate(gate);
    onSelectionModeChanged(oldMode);
    signalSelectionModeChanged();
}

TaskSketchBasedParameters::SelectionMode TaskSketchBasedParameters::getSelectionMode() const
{
    return selectionMode;
}

void TaskSketchBasedParameters::onSelectionChanged(const Gui::SelectionChanges& msg)
{
    if (selectionGate && Gui::Selection().currentSelectionGate() != selectionGate)
        exitSelectionMode(false);
    else if (vp && selectionMode != SelectionMode::none)
        _onSelectionChanged(msg);
}

QVariant TaskSketchBasedParameters::setUpToFace(const QString& text)
{
    if (text.isEmpty())
        return {};

    QStringList parts = text.split(QChar::fromLatin1(':'));
    if (parts.length() < 2)
        parts.push_back(QStringLiteral(""));

    // Check whether this is the name of an App::Plane or Part::Datum feature
    App::DocumentObject* obj = vp->getObject()->getDocument()->getObject(parts[0].toUtf8());
    if (!obj)
        return {};

    if (obj->isDerivedFrom<App::Plane>()) {
        // everything is OK (we assume a Part can only have exactly 3 App::Plane objects located at the base of the feature tree)
        return {};
    }
    else if (obj->isDerivedFrom<Part::Datum>()) {
        // it's up to the document to check that the datum plane is in the same body
        return {};
    }
    else {
        // We must expect that "parts[1]" is the translation of "Face" followed by an ID.
        QString name;
        QTextStream str(&name);
        str << "^" << tr("Face") << "(\\d+)$";

        std::string upToFace;
        QRegularExpression rx(name);
        QRegularExpressionMatch match;
        if (parts[1].indexOf(rx, 0, &match) < 0)
            upToFace = parts[1].toUtf8().constData();
        else {
            int faceId = match.captured(1).toInt();
            std::stringstream ss;
            ss << "Face" << faceId;
            upToFace = ss.str();
        }

        PartDesign::ProfileBased* pcSketchBased = static_cast<PartDesign::ProfileBased*>(vp->getObject());
        pcSketchBased->UpToFace.setValue(obj, {upToFace});
        recomputeFeature();

        return QByteArray(upToFace.c_str());
    }
}

QVariant TaskSketchBasedParameters::objectNameByLabel(const QString& label,
                                                      const QVariant& suggest) const
{
    // search for an object with the given label
    App::Document* doc = this->vp->getObject()->getDocument();
    // for faster access try the suggestion
    if (suggest.isValid()) {
        App::DocumentObject* obj = doc->getObject(suggest.toByteArray());
        if (obj && QString::fromUtf8(obj->Label.getValue()) == label) {
            return QVariant(QByteArray(obj->getNameInDocument()));
        }
    }

    // go through all objects and check the labels
    std::string name = label.toUtf8().data();
    std::vector<App::DocumentObject*> objs = doc->getObjects();
    for (auto obj : objs) {
        if (name == obj->Label.getValue()) {
            return QVariant(QByteArray(obj->getNameInDocument()));
        }
    }

    return {}; // no such feature found
}

QString TaskSketchBasedParameters::getFaceReference(const QString& obj, const QString& sub) const
{
    App::Document* doc = this->vp->getObject()->getDocument();
    QString o = obj.left(obj.indexOf(QStringLiteral(":")));

    if (o.isEmpty())
        return {};

    return QStringLiteral(R"((App.getDocument("%1").%2, ["%3"]))")
            .arg(QString::fromUtf8(doc->getName()), o, sub);
}

QString TaskSketchBasedParameters::make2DLabel(const App::DocumentObject* section,
                                               const std::vector<std::string>& subValues)
{
    if (section->isDerivedFrom(Part::Part2DObject::getClassTypeId())) {
        return QString::fromUtf8(section->Label.getValue());
    }
    else if (subValues.empty()) {
        Base::Console().Error("No valid subelement linked in %s\n", section->Label.getValue());
        return {};
    }
    else {
        return QString::fromStdString((std::string(section->getNameInDocument()) + ":" + subValues[0]));
    }
}

TaskSketchBasedParameters::~TaskSketchBasedParameters()
{
    _exitSelectionMode();
    Gui::Selection().clearSelection();
}


//**************************************************************************
//**************************************************************************
// TaskDialog
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

TaskDlgSketchBasedParameters::TaskDlgSketchBasedParameters(PartDesignGui::ViewProvider *vp)
    : TaskDlgFeatureParameters(vp)
{
}

TaskDlgSketchBasedParameters::~TaskDlgSketchBasedParameters() = default;

//==== calls from the TaskView ===============================================================


bool TaskDlgSketchBasedParameters::accept() {
    if (!vp)
        return true;

    App::DocumentObject* feature = vp->getObject();

    // Make sure the feature is what we are expecting
    // Should be fine but you never know...
    if (!feature->isDerivedFrom<PartDesign::ProfileBased>()) {
        THROWM(Base::TypeError, "Bad object processed in the sketch based dialog.")
    }

    // First verify that the feature can be built and then hide the profile as otherwise
    // it will remain hidden if the feature's recompute fails
    if (TaskDlgFeatureParameters::accept()) {
        App::DocumentObject* sketch = static_cast<PartDesign::ProfileBased*>(feature)->Profile.getValue();
        Gui::cmdAppObjectHide(sketch);
        return true;
    }

    return false;
}

bool TaskDlgSketchBasedParameters::reject()
{
    return TaskDlgFeatureParameters::reject();
}

#include "moc_TaskSketchBasedParameters.cpp"
