/***************************************************************************
 *   Copyright (c) 2014 Werner Mayer <wmayer[at]users.sourceforge.net>     *
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
# include <algorithm>
# include <sstream>
# include <QStyledItemDelegate>
# include <QTreeWidgetItem>
# include <QVBoxLayout>
#endif

#include <App/Document.h>
#include <App/DocumentObject.h>
#include <App/GeoFeature.h>
#include <App/ObjectIdentifier.h>
#include <App/PropertyPythonObject.h>
#include <Base/Interpreter.h>
#include <Base/Tools.h>

#include "DlgPropertyLink.h"
#include "ui_DlgPropertyLink.h"
#include "Application.h"
#include "Document.h"
#include "BitmapFactory.h"
#include "PropertyView.h"
#include "Selection.h"
#include "Tree.h"
#include "TreeParams.h"
#include "View3DInventor.h"
#include "ViewProviderDocumentObject.h"
#include "MetaTypes.h"
#include "Tools.h"


using namespace Gui::Dialog;

class ItemDelegate: public QStyledItemDelegate {
public:
    explicit ItemDelegate(QObject* parent=nullptr): QStyledItemDelegate(parent) {}

    QWidget* createEditor(QWidget *parent,
            const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        if(index.column() != 1)
            return nullptr;
        return QStyledItemDelegate::createEditor(parent, option, index);
    }
};

/* TRANSLATOR Gui::Dialog::DlgPropertyLink */

DlgPropertyLink::DlgPropertyLink(QWidget* parent, int flags)
  : QWidget(parent)
  , ui(new Ui_DlgPropertyLink)
  , flags(flags)
{
    ui->setupUi(this);

    selContext = Gui::Selection().getExtendedContext(nullptr);

    unsigned long col = TreeParams::getTreeActiveColor();
    selBrush = QColor((col >> 24) & 0xff,(col >> 16) & 0xff,(col >> 8) & 0xff);
    bgBrush = QColor(255, 255, 0, 100);
    selFont = ui->treeWidget->font();
    selFont.setBold(true);

    connect(ui->checkObjectType, &QCheckBox::toggled,
            this, &DlgPropertyLink::onObjectTypeToggled);
    connect(ui->typeTree, &QTreeWidget::itemSelectionChanged,
            this, &DlgPropertyLink::onTypeTreeItemSelectionChanged);
    connect(ui->searchBox, &ExpressionLineEdit::textChanged,
            this, &DlgPropertyLink::onSearchBoxTextChanged);

    ui->typeTree->hide();
    if(flags & NoTypeFilter) {
        QSignalBlocker blocker(ui->checkObjectType);
        ui->checkObjectType->setChecked(true);
        ui->checkObjectType->hide();
    }

    if(flags & NoSearchBox) {
        ui->searchBox->hide();
        ui->labelSearch->hide();
    } else {
        ui->searchBox->installEventFilter(this);
        ui->searchBox->setNoProperty(true);
        connect(ui->searchBox, SIGNAL(returnPressed()), this, SLOT(onItemSearch()));
    }

    ui->checkSubObject->setChecked(false);
    if(flags & NoSyncSubObject)
        ui->checkSubObject->hide();
    else if(flags & AlwaysSyncSubObject) {
        ui->checkSubObject->hide();
        ui->checkSubObject->setChecked(true);
    }

    timer = new QTimer(this);
    timer->setSingleShot(true);

    connect(timer, &QTimer::timeout, this, &DlgPropertyLink::onTimer);

    ui->treeWidget->setEditTriggers(QAbstractItemView::DoubleClicked);
    ui->treeWidget->setItemDelegate(new ItemDelegate(this));
    ui->treeWidget->setMouseTracking(true);

    ui->treeWidget->viewport()->installEventFilter(this);

    connect(ui->treeWidget, &QTreeWidget::itemExpanded,
            this, &DlgPropertyLink::onItemExpanded);

    connect(ui->treeWidget, SIGNAL(currentItemChanged(QTreeWidgetItem*,QTreeWidgetItem*)),
            this, SLOT(onCurrentItemChanged(QTreeWidgetItem*,QTreeWidgetItem*)));
    connect(ui->treeWidget, SIGNAL(itemChanged(QTreeWidgetItem*,int)),
            this, SLOT(onItemChanged(QTreeWidgetItem*,int)));

    if(flags & NoButton)
        ui->buttonBox->hide();
    else {
        connect(ui->buttonBox, SIGNAL(accepted()), this, SIGNAL(accepted()));
        connect(ui->buttonBox, SIGNAL(rejected()), this, SIGNAL(rejected()));
        connect(ui->buttonBox, SIGNAL(clicked(QAbstractButton*)), this, SLOT(onClicked(QAbstractButton*)));
        refreshButton = ui->buttonBox->addButton(tr("Reset"), QDialogButtonBox::ActionRole);
        resetButton = ui->buttonBox->addButton(tr("Clear"), QDialogButtonBox::ResetRole);
    }
}

/**
 *  Destroys the object and frees any allocated resources
 */
DlgPropertyLink::~DlgPropertyLink()
{
    // no need to delete child widgets, Qt does it all for us
    delete ui;
}

QTreeWidget *DlgPropertyLink::treeWidget() {
    return ui->treeWidget;
}

QList<App::SubObjectT> DlgPropertyLink::getLinksFromProperty(const App::PropertyLinkBase *prop)
{
    QList<App::SubObjectT> res;
    if(!prop)
        return res;

    std::vector<App::DocumentObject*> objs;
    std::vector<std::string> subs;
    prop->getLinks(objs,true,&subs,false);
    if(subs.empty()) {
        for(auto obj : objs)
            res.push_back(App::SubObjectT(obj,nullptr));
    } else if (objs.size()==1) {
        for(auto &sub : subs)
            res.push_back(App::SubObjectT(objs.front(),sub.c_str()));
    } else {
        int i=0;
        for(auto obj : objs)
            res.push_back(App::SubObjectT(obj,subs[i++].c_str()));
    }
    return res;
}

QString DlgPropertyLink::formatObject(App::Document *ownerDoc, App::DocumentObject *obj, const char *sub)
{
    if(!obj || !obj->isAttachedToDocument())
        return QStringLiteral("?");

    const char *objName = obj->getNameInDocument();
    std::string _objName;
    if(ownerDoc && ownerDoc!=obj->getDocument()) {
        _objName = obj->getFullName();
        objName = _objName.c_str();
    }

    if(!sub || !sub[0]) {
        if(obj->Label.getStrValue() == obj->getNameInDocument())
            return QString::fromUtf8(objName);
        return QStringLiteral("%1 (%2)").arg(QString::fromUtf8(objName),
                                                  QString::fromUtf8(obj->Label.getValue()));
    }

    auto sobj = obj->getSubObject(sub);
    if(!sobj || sobj->Label.getStrValue() == sobj->getNameInDocument())
        return QStringLiteral("%1.%2").arg(QString::fromUtf8(objName),
                                                QString::fromUtf8(sub));

    return QStringLiteral("%1.%2 (%3)").arg(QString::fromUtf8(objName),
                                                 QString::fromUtf8(sub),
                                                 QString::fromUtf8(sobj->Label.getValue()));
}

static inline bool isLinkSub(const QList<App::SubObjectT>& links)
{
    for(const auto &link : links) {
        if(&link == &links.front())
            continue;
        if(link.getDocumentName() != links.front().getDocumentName()
                || link.getObjectName() != links.front().getObjectName())
        {
            return false;
        }
    }
    return true;
}

void DlgPropertyLink::setItemLabel(QTreeWidgetItem *item, std::size_t idx) {

    item->setCheckState(0, idx ? Qt::Checked : Qt::Unchecked);

    if(singleSelect)
        return;

    QString label = item->data(0, Qt::UserRole+4).toString();
    if(!idx) {
        item->setText(0, label);
        item->setText(1, QString());
        if (item != searchItem)
            item->setBackground(0, QBrush());
        item->setFont(0, ui->treeWidget->font());
    } else {
        item->setText(0, QStringLiteral("%1: %2").arg(idx).arg(label));
        if (item != searchItem)
            item->setBackground(0, selBrush);
        item->setFont(0, selFont);
    }
}

QString DlgPropertyLink::formatLinks(App::Document *ownerDoc, QList<App::SubObjectT> links)
{
    if(!ownerDoc || links.empty())
        return {};

    auto obj = links.front().getObject();
    if(!obj)
        return QStringLiteral("?");

    if(links.size() == 1 && links.front().getSubName().empty())
        return formatObject(ownerDoc, links.front());

    QStringList list;
    if(isLinkSub(links)) {
        int i = 0;
        for(auto &link : links) {
            list << QString::fromUtf8(link.getSubName().c_str());
            if( ++i >= 3)
                break;
        }
        return QStringLiteral("%1 [%2%3]").arg(formatObject(ownerDoc,obj,0),
                                               list.join(QStringLiteral(", ")),
                    links.size() > 3 ? QStringLiteral(" ...") : QStringLiteral(""));
    }

    int i = 0;
    for(auto &link : links) {
        list << formatObject(ownerDoc,link);
        if( ++i >= 3)
            break;
    }
    return QStringLiteral("[%1%2]").arg(list.join(QStringLiteral(", ")),
                    links.size() > 3 ? QStringLiteral(" ...") : QStringLiteral(""));
}

void DlgPropertyLink::setTypeFilter(std::set<QByteArray> &&filter)
{
    selectedTypes = std::move(filter);
}

void DlgPropertyLink::setTypeFilter(const std::vector<Base::Type> &types)
{
    selectedTypes.clear();
    for(auto &type : types) {
        const char *name = type.getName();
        selectedTypes.insert(QByteArray::fromRawData(name,strlen(name)+1));
    }
}

void DlgPropertyLink::setTypeFilter(Base::Type type)
{
    selectedTypes.clear();
    const char *name = type.getName();
    selectedTypes.insert(QByteArray::fromRawData(name,strlen(name)+1));
}

void DlgPropertyLink::setContext(App::SubObjectT &&ctx)
{
    selContext = std::move(ctx);
}

void DlgPropertyLink::setInitObjects(std::vector<App::DocumentObjectT> &&objs)
{
    initObjs = std::move(objs);
}

static std::pair<App::DocumentObject *, const char *> resolveContext(
        const App::SubObjectT &ctx, std::string &subname, const App::SubObjectT &_sobjT)
{
    auto sobjT = _sobjT;
    auto obj = ctx.getObject();
    if (obj) {
        auto sobj = sobjT.getObject();
        if (!sobj)
            return {nullptr, nullptr};

        auto inList = sobj->getInListEx(true);
        auto ctxObjs = ctx.getSubObjectList();
        // Try to find a (grand)parent object of sobjT from the bottom up the ctx
        // (i.e. current context) object path.
        for (int i = static_cast<int>(ctxObjs.size()-1); i>=0 ;--i) {
            if (!inList.count(ctxObjs[i]))
                continue;
            auto parents = sobj->getParents(ctxObjs[i]);
            if (parents.empty())
                continue;

            sobjT = App::SubObjectT(ctxObjs.begin(), ctxObjs.begin()+i+1, parents.front().second.c_str());
            sobjT.setSubName(sobjT.getSubName() + _sobjT.getObjectName() + "." + _sobjT.getSubName());
            break;
        }
    }

    sobjT.normalize();
    subname = sobjT.getSubName();
    return {sobjT.getObject(), subname.c_str()};
}

void DlgPropertyLink::init(const App::DocumentObjectT &prop, bool tryFilter)
{
    {
        QSignalBlocker blocker(ui->typeTree);
        ui->typeTree->clear();
    }

    {
        QSignalBlocker blocker(ui->treeWidget);
        ui->treeWidget->clear();
    }
    oldLinks.clear();
    docItems.clear();
    typeItems.clear();
    itemMap.clear();
    inList.clear();
    if(tryFilter)
        selectedTypes.clear();
    searchItem = nullptr;
    checkedItems.clear();

    objProp  = prop;
    auto owner = objProp.getObject();
    if(!owner || !owner->isAttachedToDocument())
        return;

    ui->searchBox->setDocumentObject(owner);

    if (selContext.getObjectName().empty()) {
        for(auto &sel : Gui::Selection().getSelectionT("*", ResolveMode::NoResolve)) {
            if (sel.getSubObject() == owner) {
                if (sel.getObject() != owner) 
                    selContext = sel.getParent();
                break;
            }
        }
    }

    auto propLink = Base::freecad_dynamic_cast<App::PropertyLinkBase>(objProp.getProperty());
    if(!propLink)
        return;

    oldLinks = getLinksFromProperty(propLink);

    if(propLink->getScope() != App::LinkScope::Hidden) {
        // populate inList to filter out any objects that contains the owner object
        // of the editing link property
        inList = owner->getInListEx(true);
        inList.insert(owner);
    }

    std::vector<App::Document*> docs;

    singleSelect = false;
    if(propLink->isDerivedFrom(App::PropertyXLinkSub::getClassTypeId())
            || propLink->isDerivedFrom(App::PropertyLinkSub::getClassTypeId()))
    {
        allowSubObject = true;
        singleParent = true;
    } else if (propLink->isDerivedFrom(App::PropertyLink::getClassTypeId())) {
        singleSelect = true;
    } else if (propLink->isDerivedFrom(App::PropertyLinkSubList::getClassTypeId())) {
        allowSubObject = true;
    }

    std::vector<App::DocumentObject*> objs;

    isXLink = false;

    if(App::PropertyXLink::supportXLink(propLink)) {
        allowSubObject = true;
        docs = App::GetApplication().getDocuments();
        isXLink = true;
    } else if (initObjs.empty()) {
        objs = owner->getDocument()->getObjects();
    } else {
        objs.reserve(initObjs.size());
        for(auto &o : initObjs) 
            objs.push_back(o.getObject());
    }

    bool isLinkList = false;
    if (propLink->isDerivedFrom(App::PropertyXLinkList::getClassTypeId())
            || propLink->isDerivedFrom(App::PropertyLinkList::getClassTypeId()))
    {
        isLinkList = true;
        allowSubObject = false;
    }

    if(flags & NoSubObject)
        allowSubObject = false;

    // We use checkbox to track multiple selection, so SingleSelection mode
    // works just fine and probably has less confusion.
    ui->treeWidget->setSelectionMode(QAbstractItemView::SingleSelection);

    if(singleSelect)
        singleParent = true;

    if(!(flags & (NoSyncSubObject|AlwaysSyncSubObject)))
        ui->checkSubObject->setVisible(allowSubObject);

    if(!allowSubObject && !(flags & AllowSubElement)) {
        ui->treeWidget->setHeaderHidden(true);
        ui->treeWidget->setColumnCount(1);
        ui->treeWidget->setSelectionBehavior(QTreeWidget::SelectRows);
    } else {
        ui->treeWidget->setSelectionBehavior(QTreeWidget::SelectItems);
        ui->treeWidget->setHeaderHidden(false);
        ui->treeWidget->headerItem()->setText(0, tr("Object"));
        ui->treeWidget->headerItem()->setText(1, tr("Element"));
        ui->treeWidget->setColumnCount(2);

        // make sure to show a horizontal scrollbar if needed
        ui->treeWidget->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    }

    std::set<App::Document*> expandDocs;

    if(oldLinks.empty()) {
        expandDocs.insert(owner->getDocument());
    } else {
        for(auto &link : oldLinks) {
            auto doc = link.getDocument();
            if(doc)
                expandDocs.insert(doc);
        }
    }

    if(objs.size()) {
        for(auto obj : objs) {
            if(!obj || itemMap.count(obj))
                continue;
            auto item = createItem(obj,nullptr);
            if(item)
                itemMap[obj] = item;
        }
    } else {
        QPixmap docIcon(Gui::BitmapFactory().pixmap("Document"));
        for(auto d : docs) {
            auto item = new QTreeWidgetItem(ui->treeWidget);
            item->setIcon(0, docIcon);
            item->setText(0, QString::fromUtf8(d->Label.getValue()));
            item->setData(0, Qt::UserRole, QByteArray(""));
            item->setData(0, Qt::UserRole+1, QByteArray(d->getName()));
            item->setFlags(Qt::ItemIsEnabled);
            item->setChildIndicatorPolicy(QTreeWidgetItem::ShowIndicator);
            if(expandDocs.count(d))
                item->setExpanded(true);
            docItems[d] = item;
        }
    }

    if(allowSubObject && !(flags & (NoSyncSubObject|AlwaysSyncSubObject))) {
        if (propLink->testFlag(App::PropertyLinkBase::LinkSyncSubObject))
            ui->checkSubObject->setChecked(true);
        else {
            for(auto &link : oldLinks) {
                auto sobj = link.getSubObject();
                if(sobj && sobj!=link.getObject()) {
                    ui->checkSubObject->setChecked(true);
                    break;
                }
            }
        }
    }

    if(oldLinks.isEmpty())
        return;

    // Try to select items corresponding to the current links inside the
    // property
    std::string subname;
    QTreeWidgetItem * item = nullptr;
    for(auto &link : oldLinks) {
        auto res = resolveContext(selContext, subname, link);
        if(!res.first)
            continue;
        auto sel = selectionChanged(Gui::SelectionChanges(SelectionChanges::AddSelection,
                                               res.first->getDocument()->getName(),
                                               res.first->getNameInDocument(),
                                               res.second), true);
        if (sel)
            item = sel;
    }
    if (item)
        ui->treeWidget->scrollToItem(item);

    // For link list type property, try to auto filter type
    if(tryFilter && isLinkList) {
        Base::Type objType;
        for(const auto& link : qAsConst(oldLinks)) {
            auto obj = link.getSubObject();
            if(!obj)
                continue;
            if(objType.isBad()) {
                objType = obj->getTypeId();
                continue;
            }
            for(;objType != App::DocumentObject::getClassTypeId();
                 objType = objType.getParent())
            {
                if(obj->isDerivedFrom(objType))
                    break;
            }
        }

        Base::Type baseType;
        // get only geometric types
        if (objType.isDerivedFrom(App::GeoFeature::getClassTypeId()))
            baseType = App::GeoFeature::getClassTypeId();
        else
            baseType = App::DocumentObject::getClassTypeId();

        // get the direct base class of App::DocumentObject which 'obj' is derived from
        while (!objType.isBad()) {
            Base::Type parType = objType.getParent();
            if (parType == baseType) {
                baseType = objType;
                break;
            }
            objType = parType;
        }

        if(!baseType.isBad()) {
            const char *name = baseType.getName();
            auto it = typeItems.find(QByteArray::fromRawData(name,strlen(name)+1));
            if(it != typeItems.end())
                it->second->setSelected(true);
            ui->checkObjectType->setChecked(true);
        }
    }
}

void DlgPropertyLink::onItemChanged(QTreeWidgetItem *item, int column)
{
    if (column != 0)
        return;

    auto it = std::find(checkedItems.begin(), checkedItems.end(), item);
    bool oldState = it != checkedItems.end();
    if (oldState == (item->checkState(0) == Qt::Checked))
        return;

    QSignalBlocker blocker(ui->treeWidget);
    if (oldState) {
        // Uncheck
        setItemLabel(item);
        it = checkedItems.erase(it);
        for (; it != checkedItems.end(); ++it)
            setItemLabel(*it, it - checkedItems.begin() + 1);
    } else {
        // Newly checked item
        bool doClear = false;
        if (singleSelect)
            doClear = true;
        else if (singleParent) {
            auto parent = item->parent();
            for (auto treeitem : checkedItems) {
                if (parent != treeitem->parent()) {
                    doClear = true;
                    break;
                }
            }
        }
        if (doClear) {
            for (auto treeitem : checkedItems)
                setItemLabel(treeitem);
            checkedItems.clear();
        }
        checkedItems.push_back(item);
        setItemLabel(item, (int)checkedItems.size());
    }
    linkChanged();
}

void DlgPropertyLink::onClicked(QAbstractButton *button) {
    if(button == resetButton) {
        QSignalBlocker blocker(ui->treeWidget);
        clearSelection(nullptr);
        Gui::Selection().clearSelection();
    } else if (button == refreshButton) {
        init(objProp);
    }
}

void DlgPropertyLink::attachObserver(Gui::SelectionObserver *observer) {
    if(observer->isSelectionAttached())
        return;

    Gui::Selection().selStackPush();
    observer->attachSelection();

    if(!parentView) {
        for(auto p=parent(); p; p=p->parent()) {
            auto view = qobject_cast<Gui::PropertyView*>(p);
            if(view) {
                parentView = view;
                for(auto &sel : Gui::Selection().getCompleteSelection(ResolveMode::NoResolve))
                    savedSelections.emplace_back(sel.DocName, sel.FeatName, sel.SubName);
                break;
            }
        }
    }
    auto view = qobject_cast<Gui::PropertyView*>(parentView.data());
    if(view)
        view->blockSelection(true);
}

void DlgPropertyLink::onItemEntered(QTreeWidgetItem *) {
}

void DlgPropertyLink::leaveEvent(QEvent *ev) {
    timer->stop();
    Gui::Selection().rmvPreselect();
    QWidget::leaveEvent(ev);
}

void DlgPropertyLink::detachObserver(Gui::SelectionObserver *observer) {
    if(observer->isSelectionAttached())
        observer->detachSelection();

    auto view = qobject_cast<Gui::PropertyView*>(parentView.data());
    if(view && !savedSelections.empty()) {
        try {
            Gui::Selection().clearSelection();
        }
        catch (Py::Exception& e) {
            e.clear();
        }
        for(auto &sel : savedSelections) {
            if(sel.getSubObject())
                Gui::Selection().addSelection(sel.getDocumentName().c_str(),
                                              sel.getObjectName().c_str(),
                                              sel.getSubName().c_str());
        }
        savedSelections.clear();
    }
    if(view)
        view->blockSelection(false);

    parentView = nullptr;
}

void DlgPropertyLink::onCurrentItemChanged(QTreeWidgetItem *item, QTreeWidgetItem*)
{
    if(!item)
        return;
    int column = ui->treeWidget->currentColumn();

    auto sobjs = getLinkFromItem(item, column==1);
    App::DocumentObject *obj = !sobjs.empty()?sobjs.front().getObject():nullptr;
    if(!obj) {
        Gui::Selection().clearSelection();
        return;
    }

    bool focus = false;
    // Do auto view switch if tree view does not do it
    if(!TreeParams::getSyncView() && selContext.getObjectName().empty()) {
        focus = ui->treeWidget->hasFocus();
        auto doc = Gui::Application::Instance->getDocument(sobjs.front().getDocumentName().c_str());
        if(doc) {
            auto vp = Base::freecad_dynamic_cast<Gui::ViewProviderDocumentObject>(
                    doc->getViewProvider(obj));
            if(vp) {
                doc->setActiveView(vp, Gui::View3DInventor::getClassTypeId());
            }
        }
    }

    {
        Base::StateLocker locker(busy);
        Gui::Selection().clearSelection();

        std::string subname;
        for(auto &sobj : sobjs) {
            auto res = resolveContext(selContext, subname, sobj);
            if(!res.first) continue;
            Gui::Selection().addSelection(res.first->getDocument()->getName(),
                    res.first->getNameInDocument(), res.second);
        }
    }

    if(focus) {
        // FIXME: does not work, why?
        ui->treeWidget->setFocus();
    }
}

QTreeWidgetItem *DlgPropertyLink::findItem(
        App::DocumentObject *obj, const char *subname, bool *pfound)
{
    if(pfound)
        *pfound = false;

    if(!obj || !obj->isAttachedToDocument() || !obj->getSubObject(subname))
        return nullptr;

    std::vector<App::DocumentObject *> sobjs;
    if(subname && subname[0]) {
        if(!allowSubObject) {
            obj = obj->getSubObject(subname);
            if(!obj)
                return nullptr;
            sobjs.push_back(obj);
        } else {
            bool checking = true;
            for(auto sobj : obj->getSubObjectList(subname)) {
                if(checking && inList.count(sobj))
                    continue;
                checking = false;
                sobjs.push_back(sobj);
            }
        }
    } else
        sobjs.push_back(obj);

    if(sobjs.empty())
        return nullptr;

    if(docItems.size()) {
        auto itDoc = docItems.find(sobjs.front()->getDocument());
        if(itDoc == docItems.end())
            return nullptr;
        onItemExpanded(itDoc->second);
    }

    auto it = itemMap.find(sobjs.front());
    if(it == itemMap.end() || it->second->isHidden())
        return nullptr;

    if(!allowSubObject) {
        if(pfound)
            *pfound = true;
        return it->second;
    }

    QTreeWidgetItem *item = it->second;

    bool first = true;
    for(auto o : sobjs) {
        if(first) {
            first = false;
            continue;
        }
        onItemExpanded(item);
        bool found = false;
        for(int i=0,count=item->childCount();i<count;++i) {
            auto child = item->child(i);
            if(child->isHidden())
                break;
            if(strcmp(o->getNameInDocument(),
                        child->data(0, Qt::UserRole).toByteArray().constData())==0)
            {
                item = child;
                found = true;
                break;
            }
        }
        if(!found)
            return item;
    }
    if(pfound)
        *pfound = true;
    return item;
}

QTreeWidgetItem *DlgPropertyLink::selectionChanged(const Gui::SelectionChanges& msg, bool setCurrent)
{
    if(busy)
        return nullptr;

    if(msg.pOriginalMsg)
        return selectionChanged(*msg.pOriginalMsg, setCurrent);

    switch(msg.Type) {
    case SelectionChanges::AddSelection:
        break;
    case SelectionChanges::ClrSelection:
    case SelectionChanges::RmvSelection:
        if (!Gui::Selection().hasSelection())
            ui->treeWidget->selectionModel()->clear();
        // fall through
    default:
        return nullptr;
    }

    bool found = false;
    auto selObj = msg.Object.getObject();

    std::pair<std::string,std::string> elementName;
    const char *subname = msg.pSubName;
    if(!ui->checkSubObject->isChecked()) {
        selObj = App::GeoFeature::resolveElement(selObj,subname,elementName);
        if(!selObj)
            return nullptr;
        subname = elementName.second.c_str();
    }

    auto item = findItem(selObj, subname, &found);
    if(!item || !found)
        return nullptr;

    std::string element = msg.Object.getOldElementName();
    if(elementFilter && elementFilter(msg.Object,element))
        return nullptr;

    bool checked = item->checkState(0) == Qt::Checked;

    if(allowSubObject || (flags & AllowSubElement)) {
        if(element.size()) {
            QString e = QString::fromUtf8(element.c_str());
            QStringList list;
            QString text = item->text(1);
            if(text.size())
                list = text.split(QLatin1Char(','));
            if(list.indexOf(e)<0) {
                list << e;
                item->setText(1, list.join(QStringLiteral(",")));
                if (checked)
                    linkChanged();
            }
        } else if (item->text(1).size()) {
            item->setText(1, QString());
            if (checked)
                linkChanged();
        }
    }

    QSignalBlocker blocker(ui->treeWidget);
    if (!checked) {
        item->setCheckState(0, Qt::Checked);
        onItemChanged(item,0);
    }
    if (setCurrent) {
        ui->treeWidget->scrollToItem(item);
        ui->treeWidget->setCurrentItem(item);
    }
    return item;
}

void DlgPropertyLink::clearSelection(QTreeWidgetItem *itemKeep) {
    ui->treeWidget->selectionModel()->clearSelection();
    for(auto item : checkedItems)
        setItemLabel(item, item==itemKeep?1:0);
    checkedItems.clear();
    if(itemKeep)
        checkedItems.push_back(itemKeep);
}

static QTreeWidgetItem *_getLinkFromItem(std::ostringstream &ss, QTreeWidgetItem *item, const char *objName) {
    auto parent = item->parent();
    if(!parent)
        return item;
    const char *nextName = parent->data(0, Qt::UserRole).toByteArray().constData();
    if(!nextName[0])
        return item;

    item = _getLinkFromItem(ss, parent, nextName);
    ss << objName << '.';
    return item;
}

static App::SubObjectT subObjectFromItem(QTreeWidgetItem *item)
{
    App::SubObjectT sobj;
    auto parent = item->parent();
    if(!parent) {
        return App::SubObjectT(item->data(0, Qt::UserRole+1).toByteArray().constData(),
                               item->data(0, Qt::UserRole).toByteArray().constData(), 0);
    }
    
    std::ostringstream ss;
    auto parentItem = _getLinkFromItem(ss, item,
            item->data(0,Qt::UserRole).toByteArray().constData());

    return App::SubObjectT(parentItem->data(0, Qt::UserRole+1).toByteArray().constData(),
                           parentItem->data(0, Qt::UserRole).toByteArray().constData(),
                           ss.str().c_str());
}

QList<App::SubObjectT>
DlgPropertyLink::getLinkFromItem(QTreeWidgetItem *item, bool needElement) const
{
    QList<App::SubObjectT> res;

    auto sobj = subObjectFromItem(item);
    if(sobj.getObjectName().empty())
        return res;

    QString elements;
    if(needElement && (allowSubObject || (flags & AllowSubElement)))
        elements = item->text(1);

    if(elements.isEmpty()) {
        res.append(App::SubObjectT());
        res.last() = std::move(sobj);
        return res;
    }

    const auto split = elements.split(QLatin1Char(','));
    for(const QString &element : split) {
        res.append(App::SubObjectT());
        res.last() = App::SubObjectT(sobj.getDocumentName().c_str(),
                                     sobj.getObjectName().c_str(),
                                     (sobj.getSubName() + element.toUtf8().constData()).c_str());
    }
    return res;
}

void DlgPropertyLink::onTimer() {
    auto pos = ui->treeWidget->viewport()->mapFromGlobal(QCursor::pos());
    auto item = ui->treeWidget->itemAt(pos);
    if(!item) {
        Selection().rmvPreselect();
        return;
    }

    std::string element;
    if(ui->treeWidget->columnCount() > 1) {
        int offset = ui->treeWidget->header()->sectionPosition(1);
        if (pos.x() >= offset) {
            QString txt = item->text(1);
            if (txt.size()) {
                const auto &fm = ui->treeWidget->fontMetrics();
                int sepWidth = QtTools::horizontalAdvance(fm, QStringLiteral(","));
                for (auto &e : txt.split(QLatin1Char(','))) {
                    int w = QtTools::horizontalAdvance(fm, e);
                    if (pos.x() < offset + w) {
                        element = e.toUtf8().constData();
                        break;
                    }
                    offset += w + sepWidth;
                }
            }
        }
    }
    auto sobjs = getLinkFromItem(item, false);
    if(sobjs.isEmpty())
        return;
    const auto &sobj = sobjs.front();

    std::string subname;
    auto res = resolveContext(selContext, subname, sobj);
    if(res.first) {
        if (element.size()) {
            if (res.second)
                element = std::string(res.second) + element;
            res.second = element.c_str();
        }
        Gui::Selection().setPreselect(res.first->getDocument()->getName(),
                                      res.first->getNameInDocument(),
                                      res.second,
                                      0, 0, 0, Gui::SelectionChanges::MsgSource::TreeView);
    }
    enterTime.start();
}

QList<App::SubObjectT> DlgPropertyLink::currentLinks() const
{
    QList<App::SubObjectT> res;
    for(auto item : checkedItems)
        res.append(getLinkFromItem(item));
    return res;
}

QList<App::SubObjectT> DlgPropertyLink::originalLinks() const
{
    return oldLinks;
}

QString DlgPropertyLink::linksToPython(const QList<App::SubObjectT>& links) {
    if(links.isEmpty())
        return QStringLiteral("None");

    if(links.size() == 1)
        return QString::fromUtf8(links.front().getSubObjectPython(false).c_str());

    std::ostringstream ss;

    if(isLinkSub(links)) {
        ss << '(' << links.front().getObjectPython() << ", [";
        for(const auto& link : links) {
            const auto &sub = link.getSubName();
            if(!sub.empty())
                ss << "u'" << Base::Tools::escapeEncodeString(sub) << "',";
        }
        ss << "])";
    } else {
        ss << '[';
        for(const auto& link : links)
            ss << link.getSubObjectPython(false) << ',';
        ss << ']';
    }

    return QString::fromUtf8(ss.str().c_str());
}

void DlgPropertyLink::filterObjects()
{
    for(int i=0, count=ui->treeWidget->topLevelItemCount(); i<count; ++i) {
        auto item = ui->treeWidget->topLevelItem(i);
        if(!isXLink) {
            filterItem(item);
            continue;
        }
        for(int j=0, c=item->childCount(); j<c; ++j)
            filterItem(item->child(j));
    }
}

void DlgPropertyLink::filterItem(QTreeWidgetItem *item)
{
    if(filterType(item)) {
        item->setHidden(true);
        return;
    }
    if(objFilter && objFilter(subObjectFromItem(item))) {
        item->setHidden(true);
        return;
    }
    item->setHidden(false);
    for(int i=0, count=item->childCount(); i<count; ++i)
        filterItem(item->child(i));
}

bool DlgPropertyLink::eventFilter(QObject *obj, QEvent *e) {
    if(obj == ui->searchBox) {
        if(e->type() == QEvent::KeyPress
            && static_cast<QKeyEvent*>(e)->key() == Qt::Key_Escape)
        {
            ui->searchBox->setText(QString());
            return true;
        }
    } else if (obj == ui->treeWidget->viewport()) {
        if (e->type() == QEvent::MouseMove) {
            if(!timer->isActive() && enterTime.elapsed() < Gui::TreeParams::getPreSelectionDelay()) {
                onTimer();
            } else {
                int timeout = Gui::TreeParams::getPreSelectionDelay()/2;
                if(timeout < 0)
                    timeout = 1;
                timer->start(timeout);
            }
        }
    }
    return QWidget::eventFilter(obj,e);
}

void DlgPropertyLink::onItemSearch() {
    itemSearch(ui->searchBox->text(), true);
}

void DlgPropertyLink::keyPressEvent(QKeyEvent *ev)
{
    if(ev->key() == Qt::Key_Enter || ev->key() == Qt::Key_Return) {
        if(ui->searchBox->hasFocus())
            return;
    }
    QWidget::keyPressEvent(ev);
}

void DlgPropertyLink::itemSearch(const QString &text, bool select) {
    if(searchItem) {
        if (searchItem->checkState(0) == Qt::Checked)
            searchItem->setBackground(0, selBrush);
        else
            searchItem->setBackground(0, QBrush());
    }

    auto owner = objProp.getObject();
    if(!owner)
        return;

    std::string txt(text.toUtf8().constData());
    try {
        if(txt.empty())
            return;
        if(txt.find("<<") == std::string::npos) {
            auto pos = txt.find('.');
            if(pos==std::string::npos)
                txt += '.';
            else if(pos!=txt.size()-1) {
                txt.insert(pos+1,"<<");
                if(txt.back()!='.')
                    txt += '.';
                txt += ">>.";
            }
        }else if(txt.back() != '.')
            txt += '.';
        txt += "_self";
        auto path = App::ObjectIdentifier::parse(owner,txt);
        if(path.getPropertyName() != "_self")
            return;

        App::DocumentObject *obj = path.getDocumentObject();
        if(!obj)
            return;

        bool found;
        const char *subname = path.getSubObjectName().c_str();
        QTreeWidgetItem *item = findItem(obj, subname, &found);
        if(!item)
            return;

        if(select) {
            if(!found)
                return;
            std::string s;
            auto res = resolveContext(selContext, s, App::SubObjectT(obj,subname));
            if(res.first)
                Gui::Selection().addSelection(res.first->getDocument()->getName(),
                        res.first->getNameInDocument(),res.second);
        }else{
            std::string s;
            auto res = resolveContext(selContext, s, App::SubObjectT(obj,subname));
            if(res.first)
                Selection().setPreselect(res.first->getDocument()->getName(),
                    res.first->getNameInDocument(), res.second, 0, 0, 0,
                    Gui::SelectionChanges::MsgSource::TreeView, true);
            searchItem = item;
            ui->treeWidget->scrollToItem(searchItem);
            searchItem->setBackground(0, bgBrush);
        }
    } catch(...)
    {
    }
}

QTreeWidgetItem *DlgPropertyLink::createItem(
        App::DocumentObject *obj, QTreeWidgetItem *parent)
{
    if(!obj || !obj->isAttachedToDocument())
        return nullptr;

    if(inList.count(obj))
        return nullptr;

    auto vp = Base::freecad_dynamic_cast<ViewProviderDocumentObject>(
            Application::Instance->getViewProvider(obj));
    if(!vp)
        return nullptr;

    QTreeWidgetItem* item;
    if(parent)
        item = new QTreeWidgetItem(parent);
    else
        item = new QTreeWidgetItem(ui->treeWidget);
    item->setIcon(0, vp->getIcon());
    QString label = QString::fromUtf8((obj)->Label.getValue());
    item->setText(0, label);
    item->setData(0, Qt::UserRole+4, label);
    item->setData(0, Qt::UserRole, QByteArray(obj->getNameInDocument()));
    item->setData(0, Qt::UserRole+1, QByteArray(obj->getDocument()->getName()));
    item->setCheckState(0, Qt::Unchecked);

    if(allowSubObject || (flags & AllowSubElement)) {
        if(allowSubObject)
            item->setChildIndicatorPolicy(!obj->getLinkedObject(true)->getOutList().empty()?
                    QTreeWidgetItem::ShowIndicator:QTreeWidgetItem::DontShowIndicator);
        item->setFlags(item->flags() | Qt::ItemIsEditable | Qt::ItemIsUserCheckable);
    } else
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);

    const char *typeName = obj->getTypeId().getName();
    QByteArray typeData = QByteArray::fromRawData(typeName, strlen(typeName)+1);
    item->setData(0, Qt::UserRole+2, typeData);

    QByteArray proxyType;
    auto prop = Base::freecad_dynamic_cast<App::PropertyPythonObject>(
            obj->getPropertyByName("Proxy"));
    if(prop) {
        Base::PyGILStateLocker lock;
        Py::Object proxy = prop->getValue();
        if(!proxy.isNone() && !proxy.isString()) {
            const char *name = nullptr;
            if (proxy.hasAttr("__class__"))
                proxyType = QByteArray(proxy.getAttr("__class__").as_string().c_str());
            else {
                name = proxy.ptr()->ob_type->tp_name;
                proxyType = QByteArray::fromRawData(name, strlen(name)+1);
            }
            auto it = typeItems.find(proxyType);
            if(it != typeItems.end())
                proxyType = it->first;
            else if (name)
                proxyType = QByteArray(name, proxyType.size());
        }
    }
    item->setData(0, Qt::UserRole+3, proxyType);

    filterItem(item);
    return item;
}

QTreeWidgetItem *DlgPropertyLink::createTypeItem(Base::Type type) {
    if(type.isBad())
        return nullptr;

    QTreeWidgetItem *item = nullptr;
    if(!type.isBad() && type!=App::DocumentObject::getClassTypeId()) {
        Base::Type parentType = type.getParent();
        if(!parentType.isBad()) {
            const char *name = parentType.getName();
            auto typeData = QByteArray::fromRawData(name,strlen(name)+1);
            auto &typeItem = typeItems[typeData];
            if(!typeItem) {
                typeItem = createTypeItem(parentType);
                typeItem->setData(0, Qt::UserRole, typeData);
            }
            item = typeItem;
        }
    }

    if(!item)
        item = new QTreeWidgetItem(ui->typeTree);
    else
        item = new QTreeWidgetItem(item);
    item->setExpanded(true);
    item->setText(0, QString::fromUtf8(type.getName()));
    if(type == App::DocumentObject::getClassTypeId())
        item->setFlags(Qt::ItemIsEnabled);
    return item;
}

bool DlgPropertyLink::filterType(QTreeWidgetItem *item) {
    auto proxyType = item->data(0, Qt::UserRole+3).toByteArray();
    QTreeWidgetItem *proxyItem = nullptr;
    if(proxyType.size()) {
        auto &pitem = typeItems[proxyType];
        if(!pitem) {
            pitem = new QTreeWidgetItem(ui->typeTree);
            pitem->setText(0,QString::fromUtf8(proxyType));
            pitem->setIcon(0,item->icon(0));
            pitem->setData(0,Qt::UserRole,proxyType);
        }
        proxyItem = pitem;
    }

    auto typeData = item->data(0, Qt::UserRole+2).toByteArray();
    Base::Type type = Base::Type::fromName(typeData.constData());
    if(type.isBad())
        return false;

    QTreeWidgetItem *&typeItem = typeItems[typeData];
    if(!typeItem)  {
        typeItem = createTypeItem(type);
        typeItem->setData(0, Qt::UserRole, typeData);
    }

    if(!proxyType.size()) {
        QIcon icon = typeItem->icon(0);
        if(icon.isNull())
            typeItem->setIcon(0, item->icon(0));
    }

    if(!ui->checkObjectType->isChecked() || selectedTypes.empty())
        return false;

    if(proxyItem && selectedTypes.count(proxyType))
        return false;

    for(auto t=type; !t.isBad() && t!=App::DocumentObject::getClassTypeId(); t=t.getParent()) {
        const char *name = t.getName();
        if(selectedTypes.count(QByteArray::fromRawData(name, strlen(name)+1)))
            return false;
    }

    return true;
}

void DlgPropertyLink::onItemExpanded(QTreeWidgetItem * item) {
    if(item->childCount())
        return;

    QByteArray docName = item->data(0, Qt::UserRole+1).toByteArray();
    auto doc = App::GetApplication().getDocument(docName);
    if (!doc)
        return;

    QByteArray objName = item->data(0, Qt::UserRole).toByteArray();
    if (objName.isEmpty()) {
        for(auto obj : doc->getObjects()) {
            auto newItem = createItem(obj,item);
            if(newItem)
                itemMap[obj] = newItem;
        }
    } else if(allowSubObject) {
        auto obj = doc->getObject(objName);
        if(!obj)
            return;
        std::set<App::DocumentObject*> childSet;
        std::string sub;
        for(auto child : obj->getLinkedObject(true)->getOutList()) {
            if(!childSet.insert(child).second)
                continue;
            sub = child->getNameInDocument();
            sub += ".";
            if(obj->getSubObject(sub.c_str()))
                createItem(child,item);
        }
    }
}

void DlgPropertyLink::onObjectTypeToggled(bool on)
{
    ui->typeTree->setVisible(on);
    filterObjects();
}

void DlgPropertyLink::onTypeTreeItemSelectionChanged() {

    selectedTypes.clear();
    const auto items = ui->typeTree->selectedItems();
    for(auto item : items)
        selectedTypes.insert(item->data(0, Qt::UserRole).toByteArray());

    if(ui->checkObjectType->isChecked())
        filterObjects();
}

void DlgPropertyLink::onSearchBoxTextChanged(const QString& text)
{
    itemSearch(text,false);
}

///////////////////////////////////////////////////////////////////////////////

PropertyLinkEditor::PropertyLinkEditor(QWidget *parent)
    :QDialog(parent), SelectionObserver(false)
{
    proxy = new DlgPropertyLink(this);
    auto layout = new QVBoxLayout(this);
    layout->addWidget(proxy);
    connect(proxy, SIGNAL(accepted()), this, SLOT(accept()));
    connect(proxy, SIGNAL(rejected()), this, SLOT(reject()));
}

PropertyLinkEditor::~PropertyLinkEditor()
{
    proxy->detachObserver(this);
}

void PropertyLinkEditor::showEvent(QShowEvent *ev)
{
    proxy->attachObserver(this);
    QDialog::showEvent(ev);
}

void PropertyLinkEditor::hideEvent(QHideEvent *ev)
{
    proxy->detachObserver(this);
    QDialog::hideEvent(ev);
}

void PropertyLinkEditor::closeEvent(QCloseEvent *ev)
{
    proxy->detachObserver(this);
    QDialog::closeEvent(ev);
}

void PropertyLinkEditor::onSelectionChanged(const Gui::SelectionChanges& msg)
{
    proxy->selectionChanged(msg, true);
}

#include "moc_DlgPropertyLink.cpp"
