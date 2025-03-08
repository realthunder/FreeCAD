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

#include "PreCompiled.h"
#ifndef _PreComp_
# include <QCheckBox>
# include <QLabel>
# include <QLineEdit>
# include <QMenu>
# include <QTextStream>
# include <QToolButton>
# include <QVBoxLayout>
# include <QTreeWidget>
# include <QTreeWidgetItem>
# include <QApplication>
# include <QHeaderView>
# include <QFile>
#endif

#include <QHelpEvent>
#include <QToolTip>

#include <App/DocumentObserver.h>
#include <App/ComplexGeoData.h>
#include <App/Document.h>
#include <App/GeoFeature.h>
#include <Base/Console.h>
#include "Application.h"
#include "BitmapFactory.h"
#include "CommandT.h"
#include "Document.h"
#include "MetaTypes.h"
#include "MainWindow.h"
#include "PieMenu.h"
#include "SelectionView.h"
#include "Tree.h"
#include "ViewProvider.h"
#include "Widgets.h"

FC_LOG_LEVEL_INIT("Selection", true, true, true)

using namespace Gui;
using namespace Gui::DockWnd;

/* TRANSLATOR Gui::DockWnd::SelectionView */

enum ColumnIndex {
    LabelIndex,
    ElementIndex,
    PathIndex,
    LastIndex,
};

SelectionView::SelectionView(Gui::Document* pcDocument, QWidget *parent)
  : DockWindow(pcDocument,parent)
  , SelectionObserver(true, ResolveMode::NoResolve)
  , x(0.0f), y(0.0f), z(0.0f)
  , openedAutomatically(false)
{
    setWindowTitle(tr("Selection View"));

    QVBoxLayout* vLayout = new QVBoxLayout(this);
    vLayout->setSpacing(0);
    vLayout->setContentsMargins(0, 0, 0, 0);

    QLineEdit* searchBox = new QLineEdit(this);
    LineEditStyle::setup(searchBox);
    searchBox->setPlaceholderText(tr("Search"));
    searchBox->setToolTip(tr("Searches object labels"));
    QHBoxLayout* hLayout = new QHBoxLayout();
    hLayout->setSpacing(2);
    QToolButton* clearButton = new QToolButton(this);
    clearButton->setFixedSize(18, 21);
    clearButton->setCursor(Qt::ArrowCursor);
    clearButton->setStyleSheet(QString::fromUtf8("QToolButton {margin-bottom:1px}"));
    clearButton->setIcon(BitmapFactory().pixmap(":/icons/edit-cleartext.svg"));
    clearButton->setToolTip(tr("Clears the search field"));
    clearButton->setAutoRaise(true);
    countLabel = new QLabel(this);
    countLabel->setText(QString::fromUtf8("0"));
    countLabel->setToolTip(tr("The number of selected items"));
    hLayout->addWidget(searchBox);
    hLayout->addWidget(clearButton,0,Qt::AlignRight);
    hLayout->addWidget(countLabel,0,Qt::AlignRight);
    vLayout->addLayout(hLayout);

    selectionView = new QTreeWidget(this);
    selectionView->setColumnCount(LastIndex);
    selectionView->headerItem()->setText(LabelIndex, tr("Label"));
    selectionView->headerItem()->setText(ElementIndex, tr("Element"));
    selectionView->headerItem()->setText(PathIndex, tr("Path"));
    selectionView->header()->setStretchLastSection(false);

    selectionView->setContextMenuPolicy(Qt::CustomContextMenu);
    vLayout->addWidget( selectionView );

    enablePickList = new QCheckBox(this);
    enablePickList->setText(tr("Picked object list"));
    hLayout->addWidget(enablePickList);

    pickList = new QTreeWidget(this);
    pickList->header()->setSortIndicatorShown(true);
    pickList->setColumnCount(LastIndex);
    pickList->sortByColumn(ElementIndex, Qt::AscendingOrder);
    pickList->headerItem()->setText(LabelIndex, tr("Label"));
    pickList->headerItem()->setText(ElementIndex, tr("Element"));
    pickList->headerItem()->setText(PathIndex, tr("Path"));
    pickList->header()->setStretchLastSection(false);
    pickList->setVisible(false);
    vLayout->addWidget(pickList);

    for(int i=0; i<LastIndex; ++i) {
        selectionView->header()->setSectionResizeMode(i, QHeaderView::ResizeToContents);
        pickList->header()->setSectionResizeMode(i, QHeaderView::ResizeToContents);
    }

    selectionView->setMouseTracking(true); // needed for itemEntered() to work
    pickList->setMouseTracking(true);

    resize(200, 200);

    connect(clearButton, &QToolButton::clicked, searchBox, &QLineEdit::clear);
    connect(searchBox, &QLineEdit::textChanged, this, &SelectionView::search);
    // editingFinished() will fire on lost of focus, which may cause undesired
    // effect. Use returnPressed() instead for clearer user intention.
    //
    // connect(searchBox, &QLineEdit::editingFinished, this, &SelectionView::validateSearch);
    connect(searchBox, &QLineEdit::returnPressed, this, &SelectionView::validateSearch);

    connect(selectionView, &QTreeWidget::itemDoubleClicked, this, &SelectionView::toggleSelect);
    connect(selectionView, &QTreeWidget::itemEntered, this, &SelectionView::preselect);
    connect(pickList, &QTreeWidget::itemDoubleClicked, this, &SelectionView::toggleSelect);
    connect(pickList, &QTreeWidget::itemEntered, this, &SelectionView::preselect);
    connect(selectionView, &QTreeWidget::customContextMenuRequested, this, &SelectionView::onItemContextMenu);
    connect(enablePickList, &QCheckBox::stateChanged, this, &SelectionView::onEnablePickList);

    QObject::connect(selectionView, &QTreeWidget::currentItemChanged,
        [&](QTreeWidgetItem *current, QTreeWidgetItem*) {
            if (current)
                preselect(current);
        });
}

SelectionView::~SelectionView()
{
}

void SelectionView::leaveEvent(QEvent *)
{
    Selection().rmvPreselect();
}

static void addItem(QTreeWidget *tree, const App::SubObjectT &objT)
{
    auto obj = objT.getSubObject();
    if(!obj)
        return;

    auto* item = new QTreeWidgetItem(tree);
    item->setText(LabelIndex, QString::fromUtf8(obj->Label.getStrValue().c_str()));

    item->setText(ElementIndex, QString::fromUtf8(objT.getOldElementName().c_str()));

    item->setText(PathIndex, QStringLiteral("%1#%2.%3").arg(
                QString::fromUtf8(objT.getDocumentName().c_str()),
                QString::fromUtf8(objT.getObjectName().c_str()),
                QString::fromUtf8(objT.getSubName().c_str())));

    auto vp = Application::Instance->getViewProvider(obj);
    if(vp)
        item->setIcon(0, vp->getIcon());

    // save as user data
    item->setData(0, Qt::UserRole, QVariant::fromValue(objT));
}

/// @cond DOXERR
void SelectionView::onSelectionChanged(const SelectionChanges &Reason)
{
    ParameterGrp::handle hGrp = App::GetApplication().GetUserParameter().GetGroup("BaseApp")
        ->GetGroup("Preferences")->GetGroup("Selection");
    bool autoShow = hGrp->GetBool("AutoShowSelectionView", false);
    hGrp->SetBool("AutoShowSelectionView", autoShow); // Remove this line once the preferences window item is implemented

    if (autoShow) {
        if (!parentWidget()->isVisible() && Selection().hasSelection()) {
            parentWidget()->show();
            openedAutomatically = true;
        }
        else if (openedAutomatically && !Selection().hasSelection()) {
            parentWidget()->hide();
            openedAutomatically = false;
        }
    }

    QString selObject;
    QTextStream str(&selObject);
    if (Reason.Type == SelectionChanges::AddSelection) {
        addItem(selectionView, Reason.Object);
    }
    else if (Reason.Type == SelectionChanges::ClrSelection) {
        if(!Reason.pDocName[0]) {
            // remove all items
            selectionView->clear();
        }else{
            // build name
            str << QString::fromUtf8(Reason.pDocName);
            str << "#";
            // remove all items
            const auto items = selectionView->findItems(selObject,Qt::MatchStartsWith,PathIndex);
            for(auto item : items)
                delete item;
        }
    }
    else if (Reason.Type == SelectionChanges::RmvSelection) {
        // build name
        str << QString::fromUtf8(Reason.pDocName) << "#"
            << QString::fromUtf8(Reason.pObjectName) << "."
            << QString::fromUtf8(Reason.pSubName);

        // remove all items
        for(auto item : selectionView->findItems(selObject,Qt::MatchExactly,PathIndex))
            delete item;
    }
    else if (Reason.Type == SelectionChanges::SetSelection) {
        // remove all items
        selectionView->clear();
        for(auto &objT : Gui::Selection().getSelectionT("*",ResolveMode::NoResolve))
            addItem(selectionView, objT);
    }
    else if (Reason.Type == SelectionChanges::PickedListChanged) {
        bool picking = Selection().needPickedList();
        enablePickList->setChecked(picking);
        pickList->setVisible(picking);
        pickList->clear();
        if(picking) {
            pickList->setSortingEnabled(false);
            for(auto &objT : Selection().getPickedList("*"))
                addItem(pickList, objT);
            pickList->setSortingEnabled(true);
        }
    }

    countLabel->setText(QString::number(selectionView->topLevelItemCount()));
}

void SelectionView::search(const QString& text)
{
    searchList.clear();
    selectionView->clear();
    if (!text.isEmpty()) {
        App::Document* doc = App::GetApplication().getActiveDocument();
        std::vector<App::DocumentObject*> objects;
        if (doc) {
            objects = doc->getObjects();
            for (std::vector<App::DocumentObject*>::iterator it = objects.begin(); it != objects.end(); ++it) {
                QString label = QString::fromUtf8((*it)->Label.getValue());
                if (label.contains(text,Qt::CaseInsensitive)) {
                    searchList.emplace_back(*it);
                    addItem(selectionView, App::SubObjectT(*it, ""));
                }
            }
            countLabel->setText(QString::number(selectionView->topLevelItemCount()));
        }
    }
}

void SelectionView::validateSearch()
{
    if (!searchList.empty()) {
        App::Document* doc = App::GetApplication().getActiveDocument();
        if (doc) {
            selectionView->clear();
            Gui::Selection().clearSelection();
            Gui::Selection().addSelections(searchList);
        }
    }
}

void SelectionView::select(QTreeWidgetItem* item)
{
    if (!item)
        item = selectionView->currentItem();
    if (!item)
        return;

    auto objT = qvariant_cast<App::SubObjectT>(item->data(0, Qt::UserRole));
    if(!objT.getSubObject())
        return;

    try {
        doCommandT(Command::Gui, "Gui.Selection.clearSelection()");
        doCommandT(Command::Gui, "Gui.Selection.addSelection(%s, '%s')",
                Command::getObjectCmd(objT.getObject()), objT.getSubName());
    }catch(Base::Exception &e) {
        e.ReportException();
    }
}

void SelectionView::deselect()
{
    auto item = selectionView->currentItem();
    if (!item)
        return;
    auto objT = qvariant_cast<App::SubObjectT>(item->data(0, Qt::UserRole));
    if(!objT.getSubObject())
        return;

    try {
        doCommandT(Command::Gui, "Gui.Selection.removeSelection(%s, '%s')",
                Command::getObjectCmd(objT.getObject()), objT.getSubName());
    }catch(Base::Exception &e) {
        e.ReportException();
    }
}

void SelectionView::toggleSelect(QTreeWidgetItem* item)
{
    if (!item)
        return;

    auto objT = qvariant_cast<App::SubObjectT>(item->data(0, Qt::UserRole));
    if(!objT.getSubObject())
        return;
    try {
        bool selected = Gui::Selection().isSelected(objT.getDocumentName().c_str(),
                                                    objT.getObjectName().c_str(),
                                                    objT.getSubName().c_str());
        doCommandT(Command::Gui, "Gui.Selection.%s(%s,'%s')",
                selected ? "removeSelection" : "addSelection",
                Command::getObjectCmd(objT.getObject()),
                objT.getSubName());

    }catch(Base::Exception &e) {
        e.ReportException();
    }
}

void SelectionView::preselect(QTreeWidgetItem* item)
{
    if (!item) return;
    auto objT = qvariant_cast<App::SubObjectT>(item->data(0, Qt::UserRole));
    if(!objT.getSubObject())
        return;
    Gui::Selection().setPreselect(objT.getDocumentName().c_str(),
                                  objT.getObjectName().c_str(),
                                  objT.getSubName().c_str(),0,0,0,
                                  SelectionChanges::MsgSource::TreeView,true);
}

void SelectionView::zoom()
{
    select();
    try {
        Gui::Command::runCommand(Gui::Command::Gui,"Gui.SendMsgToActiveView(\"ViewSelection\")");
    }catch(Base::Exception &e) {
        e.ReportException();
    }
}

void SelectionView::treeSelect()
{
    select();
    try {
        Gui::Command::runCommand(Gui::Command::Gui,"Gui.runCommand(\"Std_TreeSelection\")");
    }catch(Base::Exception &e) {
        e.ReportException();
    }
}

void SelectionView::touch()
{
    auto item = selectionView->currentItem();
    if (!item)
        return;
    auto objT = qvariant_cast<App::SubObjectT>(item->data(0, Qt::UserRole));
    auto sobj = objT.getSubObject();
    if(!sobj)
        return;
    if(sobj) {
        try {
            cmdAppObject(sobj,"touch()");
        }catch(Base::Exception &e) {
            e.ReportException();
        }
    }
}

void SelectionView::toPython()
{
    auto item = selectionView->currentItem();
    if (!item)
        return;
    auto objT = qvariant_cast<App::SubObjectT>(item->data(0, Qt::UserRole));
    auto sobj = objT.getSubObject();
    if(!sobj)
        return;
    try {
        doCommandT(Command::Gui, "_obj, _matrix, _shp = %s.getSubObject('%s', retType=2)",
                Command::getObjectCmd(objT.getObject()), objT.getSubName());
    }
    catch (const Base::Exception& e) {
        e.ReportException();
    }
}

static std::string getModule(const char* type)
{
    // go up the inheritance tree and find the module name of the first
    // sub-class that has not the prefix "App::"
    std::string prefix;
    Base::Type typeId = Base::Type::fromName(type);

    while (!typeId.isBad()) {
        std::string temp(typeId.getName());
        std::string::size_type pos = temp.find_first_of("::");

        std::string module;
        if (pos != std::string::npos)
            module = std::string(temp,0,pos);
        if (module != "App")
            prefix = module;
        else
            break;
        typeId = typeId.getParent();
    }

    return prefix;
}

void SelectionView::showPart(void)
{
    auto *item = selectionView->currentItem();
    if (!item)
        return;
    auto objT = qvariant_cast<App::SubObjectT>(item->data(0, Qt::UserRole));
    auto sobj = objT.getSubObject();
    if(!sobj)
        return;
    std::string module = getModule(sobj->getTypeId().getName());
    if (!module.empty()) {
        try {
            doCommandT(Command::Gui, "%s.show(%s.getSubObject('%s'))",
                    module, Command::getObjectCmd(objT.getObject()), objT.getSubName());
        }
        catch (const Base::Exception& e) {
            e.ReportException();
        }
    }
}

void SelectionView::onItemContextMenu(const QPoint& point)
{
    auto item = selectionView->itemAt(point);
    if (!item)
        return;
    auto objT = qvariant_cast<App::SubObjectT>(item->data(0, Qt::UserRole));
    auto sobj = objT.getSubObject();
    if(!sobj)
        return;

    QMenu menu;
    QAction *selectAction = menu.addAction(tr("Select only"), this, [&]{
        this->select(nullptr);
    });
    selectAction->setIcon(QIcon::fromTheme(QStringLiteral("view-select")));
    selectAction->setToolTip(tr("Selects only this object"));

    QAction *deselectAction = menu.addAction(tr("Deselect"), this, &SelectionView::deselect);
    deselectAction->setIcon(QIcon::fromTheme(QStringLiteral("view-unselectable")));
    deselectAction->setToolTip(tr("Deselects this object"));

    QAction *zoomAction = menu.addAction(tr("Zoom fit"), this, &SelectionView::zoom);
    zoomAction->setIcon(QIcon::fromTheme(QStringLiteral("zoom-fit-best")));
    zoomAction->setToolTip(tr("Selects and fits this object in the 3D window"));

    QAction *gotoAction = menu.addAction(tr("Go to selection"), this, &SelectionView::treeSelect);
    gotoAction->setToolTip(tr("Selects and locates this object in the tree view"));

    QAction *touchAction = menu.addAction(tr("Mark to recompute"), this, &SelectionView::touch);
    touchAction->setIcon(QIcon::fromTheme(QStringLiteral("view-refresh")));
    touchAction->setToolTip(tr("Mark this object to be recomputed"));

    QAction *toPythonAction = menu.addAction(tr("To python console"), this, &SelectionView::toPython);
    toPythonAction->setIcon(QIcon::fromTheme(QStringLiteral("applications-python")));
    toPythonAction->setToolTip(tr("Reveals this object and its subelements in the python console."));

    if (objT.getOldElementName().size()) {
        // subshape-specific entries
        QAction *showPart = menu.addAction(tr("Duplicate subshape"), this, &SelectionView::showPart);
        showPart->setIcon(QIcon(QStringLiteral(":/icons/ClassBrowser/member.svg")));
        showPart->setToolTip(tr("Creates a standalone copy of this subshape in the document"));
    }
    menu.exec(selectionView->mapToGlobal(point));
}

void SelectionView::onUpdate()
{
}

bool SelectionView::onMsg(const char* /*pMsg*/,const char** /*ppReturn*/)
{
    return false;
}

void SelectionView::hideEvent(QHideEvent *ev) {
    FC_TRACE(this << " detaching selection observer");
    this->detachSelection();
    DockWindow::hideEvent(ev);
}

void SelectionView::showEvent(QShowEvent *ev) {
    FC_TRACE(this << " attaching selection observer");
    this->attachSelection();

    selectionView->clear();
    for(auto &objT : Gui::Selection().getSelectionT("*", ResolveMode::NoResolve))
        addItem(selectionView, objT);

    bool picking = Selection().needPickedList();
    enablePickList->setChecked(picking);
    pickList->clear();
    if(picking) {
        pickList->setSortingEnabled(false);
        for(auto &objT : Selection().getPickedList("*"))
            addItem(pickList, objT);
        pickList->setSortingEnabled(true);
    }

    Gui::DockWindow::showEvent(ev);
}

void SelectionView::onEnablePickList() {
    bool enabled = enablePickList->isChecked();
    Selection().enablePickedList(enabled);
    pickList->setVisible(enabled);
}

/// @endcond

////////////////////////////////////////////////////////////////////////

static QString _DefaultStyle = QStringLiteral("QMenu {menu-scrollable:0}");

namespace Gui {
void setupMenuStyle(QWidget *menu)
{
    LineEditStyle::setupChildren(menu);

    auto hGrp = App::GetApplication().GetParameterGroupByPath(
                    "User parameter:BaseApp/Preferences/MainWindow");
    static QString _Name;
    static QString _Stylesheet;
    QString name = QString::fromUtf8(hGrp->GetASCII("MenuStyleSheet").c_str());
    if(name.isEmpty()) {
        QString mainstyle = QString::fromUtf8(hGrp->GetASCII("StyleSheet").c_str());
        if(mainstyle.indexOf(QStringLiteral("dark"),0,Qt::CaseInsensitive)>=0)
            name = QStringLiteral("qssm:Dark.qss");
        else if(mainstyle.indexOf(QStringLiteral("light"),0,Qt::CaseInsensitive)>=0)
            name = QStringLiteral("qssm:Light.qss");
        else
            name = QStringLiteral("qssm:Default.qss");
    } else if (!QFile::exists(name))
        name = QStringLiteral("qssm:%1").arg(name);
    if(_Name != name) {
        _Name = name;
        _Stylesheet.clear();
        if(QFile::exists(name)) {
            QFile f(name);
            if(f.open(QFile::ReadOnly)) {
                QTextStream str(&f);
                _Stylesheet = str.readAll();
            }
        }
    }
    if(_Stylesheet.isEmpty())
        _Stylesheet = _DefaultStyle;

    if (menu->styleSheet() == _Stylesheet)
        return;

    menu->setStyleSheet(_Stylesheet);
    if (_Stylesheet.indexOf(QStringLiteral("background")) >= 0) {
        menu->setWindowFlags(menu->windowFlags() | Qt::FramelessWindowHint);
        menu->setAttribute(Qt::WA_NoSystemBackground, true);
        menu->setAttribute(Qt::WA_TranslucentBackground, true);
    }
}
}

SelectionMenu::SelectionMenu(QWidget *parent)
    :QMenu(parent)
{
    connect(this, SIGNAL(aboutToShow()), this, SLOT(beforeShow()));
    setupMenuStyle(this);
}

void SelectionMenu::beforeShow()
{
    for(auto child : findChildren<QMenu*>()) {
        child->setWindowFlags(child->windowFlags() | Qt::FramelessWindowHint);
        child->setAttribute(Qt::WA_NoSystemBackground, true);
        child->setAttribute(Qt::WA_TranslucentBackground, true);
    }
}

struct ElementInfo {
    QMenu *menu = nullptr;
    QIcon icon;
    std::vector<int> indices;
};

struct SubMenuInfo {
    QMenu *menu = nullptr;

    // Map from sub-object label to map from object path to element info. The
    // reason of the second map is to disambiguate sub-object with the same
    // label, but different object or object path
    std::map<std::string, std::map<std::string, ElementInfo> > items;
};

static bool _HasOverrideCursor;

App::SubObjectT SelectionMenu::doPick(const std::vector<App::SubObjectT> &sels)
{
    clear();

    std::ostringstream ss;
    typedef std::map<std::string, SubMenuInfo> MenuMap;
    MenuMap menus;
    std::map<App::DocumentObject*, QIcon> icons;

    int i=-1;
    for(auto &sel : sels) {
        ++i;
        auto sobj = sel.getSubObject();
        if(!sobj)
            continue;

        ss.str("");
        int index = -1;
        std::string element = sel.getOldElementName(&index);
        if(index < 0)
            element = "Other";
        ss << sel.getObjectName() << '.' << sel.getSubNameNoElement();
        std::string key = ss.str();

        auto &icon = icons[sobj];
        if(icon.isNull()) {
            auto vp = Application::Instance->getViewProvider(sobj);
            if(vp)
                icon = vp->getIcon();
        }

        auto &elementInfo = menus[element].items[sobj->Label.getStrValue()][key];
        elementInfo.icon = icon;
        elementInfo.indices.push_back(i);

        auto geoFeature = Base::freecad_dynamic_cast<App::GeoFeature>(
                sobj->getLinkedObject(true));
        if (geoFeature) {
            for(auto element : geoFeature->getElementTypes(true))
                menus[element];
        }
    }

    auto itOther = menus.end();
    std::vector<MenuMap::iterator> menuArray;
    menuArray.reserve(menus.size());
    for (auto it = menus.begin(); it != menus.end(); ++it) {
        if (it->first == "Other")
            itOther = it;
        else
            menuArray.push_back(it);
    }
    if (itOther != menus.end())
        menuArray.push_back(itOther);
    for(auto it : menuArray) {
        auto &v = *it;
        auto &info = v.second;
        if (info.items.empty()) {
            QAction *action = addAction(QString::fromUtf8(v.first.c_str()));
            action->setDisabled(true);
            continue;
        }

        info.menu = addMenu(QString::fromUtf8(v.first.c_str()));
        info.menu->installEventFilter(this);

        bool groupMenu = false;
        if(info.items.size() > 20)
            groupMenu = true;
        else {
            std::size_t objCount = 0;
            std::size_t count = 0;
            for(auto &vv : info.items) {
                objCount += vv.second.size();
                for(auto &vvv : vv.second) 
                    count += vvv.second.indices.size();
                if(count > 20 && objCount>1) {
                    groupMenu = true;
                    break;
                }
            }
        }

        for(auto &vv : info.items) {
            const std::string &label = vv.first;

            for(auto &vvv : vv.second) {
                auto &elementInfo = vvv.second;

                if(!groupMenu) {
                    for(int idx : elementInfo.indices) {
                        ss.str("");
                        ss << label;
                        auto element = sels[idx].getOldElementName();
                        if (element.size())
                            ss << " (" << element << ")";
                        QAction *action = info.menu->addAction(elementInfo.icon, QString::fromUtf8(ss.str().c_str()));
                        action->setData(QVariant::fromValue(sels[idx]));
                        connect(info.menu, SIGNAL(hovered(QAction*)), this, SLOT(onHover(QAction*)));
                    }
                    continue;
                }
                if(!elementInfo.menu) {
                    elementInfo.menu = info.menu->addMenu(elementInfo.icon, QString::fromUtf8(label.c_str()));
                    elementInfo.menu->installEventFilter(this);
                    connect(elementInfo.menu, SIGNAL(aboutToShow()),this,SLOT(onSubMenu()));
                    connect(elementInfo.menu, SIGNAL(hovered(QAction*)), this, SLOT(onHover(QAction*)));
                }
                for(int idx : elementInfo.indices) {
                    QAction *action = elementInfo.menu->addAction(
                            QString::fromUtf8(sels[idx].getOldElementName().c_str()));
                    action->setData(QVariant::fromValue(sels[idx]));
                }
            }
        }
    }
    bool toggle = !Gui::Selection().needPickedList();
    if(toggle)
        Gui::Selection().enablePickedList(true);

    Gui::Selection().rmvPreselect();
    QAction* picked = PieMenu::exec(this, QCursor::pos(), "Std_PickGeometry");
    if (_HasOverrideCursor) {
        _HasOverrideCursor = false;
        QApplication::restoreOverrideCursor();
    }
    if(toggle)
        Gui::Selection().enablePickedList(false);
    return onPicked(picked);
}

static bool _HasPicked;
App::SubObjectT SelectionMenu::doPick(const std::vector<App::SubObjectT> &sels,
                                      const App::SubObjectT &context)
{
    clear();

    QTreeWidgetItem *contextItem = Gui::TreeWidget::findItem(context);
    App::DocumentObject *lastObj = nullptr;
    QMenu *lastMenu = nullptr;
    QAction *lastAction = nullptr;
    for (auto sel : sels) {
        if (!sel.hasSubElement())
            continue;
        auto sobj = sel.getSubObject();
        if (!sobj)
            continue;
        if (contextItem && !Gui::TreeWidget::findItem(sel, contextItem, &sel))
            continue;
        auto vp = Gui::Application::Instance->getViewProvider(sobj);
        QAction *action;
        if (lastObj == sobj) {
            if (!lastMenu) {
                lastMenu = new QMenu(this);
                connect(lastMenu, SIGNAL(hovered(QAction*)), this, SLOT(onHover(QAction*)));
                lastMenu->installEventFilter(this);
                lastAction->setText(QString::fromUtf8(sobj->Label.getValue()));
                lastAction->setMenu(lastMenu);
                auto prev = qvariant_cast<App::SubObjectT>(lastAction->data());
                auto prevAction = lastMenu->addAction(
                        QString::fromUtf8(prev.getOldElementName().c_str()));
                prevAction->setData(QVariant::fromValue(prev));
                prev.setSubName(prev.getSubNameNoElement());
                lastAction->setData(QVariant::fromValue(prev));
            }
            action = lastMenu->addAction(QString::fromUtf8(sel.getOldElementName().c_str()));
        } else {
            lastObj = sobj;
            QString label = QString::fromUtf8(sobj->Label.getValue());
            auto element = sel.getOldElementName();
            if (element.size())
                label += QStringLiteral(" (%2)").arg(QString::fromUtf8(element.c_str()));
            action = addAction(vp ? vp->getIcon() : QIcon(), label);
            lastAction = action;
            lastMenu = nullptr;
        }
        action->setData(QVariant::fromValue(sel));
    }
    connect(this, SIGNAL(hovered(QAction*)), this, SLOT(onHover(QAction*)));
    this->installEventFilter(this);
    auto res = onPicked(this->exec(QCursor::pos()));
    _HasPicked = !res.getObjectName().empty();
    if (_HasOverrideCursor) {
        _HasOverrideCursor = false;
        QApplication::restoreOverrideCursor();
    }
    return res;
}

App::SubObjectT SelectionMenu::onPicked(QAction *picked)
{
    ToolTip::hideText();
    Gui::Selection().rmvPreselect();
    if(!picked)
        return App::SubObjectT();
    auto sel = qvariant_cast<App::SubObjectT>(picked->data());
    if (sel.getObjectName().size()) {
        auto modifier = QApplication::queryKeyboardModifiers();
        if (modifier == Qt::ShiftModifier) {
            TreeWidget::selectUp(sel);
        } else {
            if (modifier != Qt::ControlModifier) {
                Gui::Selection().selStackPush();
                Gui::Selection().clearSelection();
            }
            Gui::Selection().addSelection(sel.getDocumentName().c_str(),
                    sel.getObjectName().c_str(), sel.getSubName().c_str());
            Gui::Selection().selStackPush();
        }
    }
    return sel;
}

static bool setPreselect(QMenu *menu,
                         QAction *action,
                         bool needElement = true,
                         bool needTooltip = true)
{
    auto sel = qvariant_cast<App::SubObjectT>(action->data());
    if (sel.getObjectName().empty()) {
        if (_HasOverrideCursor) {
            _HasOverrideCursor = false;
            QApplication::restoreOverrideCursor();
        }
        Gui::Selection().rmvPreselect();
        ToolTip::hideText();
        return false;
    }

    QString element;
    if(!needElement)
        sel.setSubName(sel.getSubNameNoElement().c_str());

    auto res = Gui::Selection().setPreselect(sel.getDocumentName().c_str(),
            sel.getObjectName().c_str(), sel.getSubName().c_str(),0,0,0,
            SelectionChanges::MsgSource::TreeView);

    if (res == SelectionSingleton::PreselectResult::Blocked) {
        if (!_HasOverrideCursor) {
            _HasOverrideCursor = true;
            QApplication::setOverrideCursor(Qt::ForbiddenCursor);
        }
        return false;
    }

    if (_HasOverrideCursor) {
        _HasOverrideCursor = false;
        QApplication::restoreOverrideCursor();
    }
    if (res != SelectionSingleton::PreselectResult::OK)
        return false;

    if(!needTooltip
            ||!(QApplication::queryKeyboardModifiers() 
                & (Qt::ShiftModifier | Qt::AltModifier | Qt::ControlModifier))) {
        Gui::Selection().format(0,0,0,0,0,0,true);
        return true;
    }

    auto sobj = sel.getSubObject();
    QString tooltip = QString::fromUtf8(sel.getSubObjectFullName().c_str());

    if (!needElement && sobj && sobj->Label2.getStrValue().size())
        tooltip = QStringLiteral("%1\n\n%2").arg(
                tooltip, QString::fromUtf8(sobj->Label2.getValue()));

    tooltip = QStringLiteral("%1\n\n%2").arg(tooltip,
                QObject::tr("Left click to select the geometry.\n"
                            "CTRL + Left click for multiselection.\n"
                            "Shift + left click to edit the object.\n"
                            "Right click to bring up the hierarchy menu.\n"
                            "Shift + right click to bring up the object context menu."));
    if (sel.hasSubElement())
        tooltip = QStringLiteral("%1\n%2").arg(tooltip,
                QObject::tr("CTRL + right click to trace geometry history.\n"
                             "CTRL + Shift + right click to list derived geometries."));


    ToolTip::showText(QCursor::pos(), tooltip, menu);
    return true;
}

void SelectionMenu::onHover(QAction *action)
{
    setPreselect(this, action);
}

void SelectionMenu::leaveEvent(QEvent *event) {
    ToolTip::hideText();
    QMenu::leaveEvent(event);
}

void SelectionMenu::onSubMenu() {
    auto submenu = qobject_cast<QMenu*>(sender());
    if(!submenu) {
        Gui::Selection().rmvPreselect();
        return;
    }
    auto actions = submenu->actions();
    if(!actions.size()) {
        Gui::Selection().rmvPreselect();
        return;
    }
    setPreselect(this, actions.front(), false);
}

bool SelectionMenu::eventFilter(QObject *o, QEvent *ev)
{
    switch(ev->type()) {
    case QEvent::Show: {
        // Gui::Selection().rmvPreselect();
        break;
    }
    case QEvent::MouseButtonRelease: {
        auto me = static_cast<QMouseEvent*>(ev);
        if (me->button() == Qt::RightButton) {
            auto menu = qobject_cast<QMenu*>(o);
            if (menu) {
                auto action = menu->actionAt(me->pos());
                if (action) {
                    activeMenu = menu;
                    activeAction = action;
                    QMetaObject::invokeMethod(this, "onSelUpMenu", Qt::QueuedConnection);
                    return true;
                }
            }
        }
        break;
    }
    default:
        break;
    }
    return QMenu::eventFilter(o, ev);
}

void SelectionMenu::onSelUpMenu()
{
    ToolTip::hideText();

    QMenu *currentMenu = activeMenu;
    QAction *currentAction = activeAction;

    activeMenu = nullptr;
    activeAction = nullptr;

    if(!currentMenu || !currentAction)
        return;

    auto sel = qvariant_cast<App::SubObjectT>(currentAction->data());
    if (sel.getObjectName().empty())
        return;

    auto modifiers = QApplication::queryKeyboardModifiers();
    if (modifiers == Qt::ShiftModifier) {
        TreeWidget::selectUp(sel, this);
        return;
    }
    
    if (!(modifiers & Qt::ControlModifier)) {
        SelUpMenu menu(currentMenu);
        TreeWidget::populateSelUpMenu(&menu, &sel);
        TreeWidget::execSelUpMenu(&menu, QCursor::pos());
        return;
    }

    if (!sel.hasSubElement())
        return;

    if (setPreselect(this, currentAction, true, false)) {
        _HasPicked = false;
        PieMenu::deactivate(false);

        // ctrl + right click for geometry element history tracing
        // ctrl + shift + right click for derived geometry
        if (modifiers & Qt::ShiftModifier)
            Application::Instance->commandManager().runCommandByName("Part_GeometryDerived");
        else
            Application::Instance->commandManager().runCommandByName("Part_GeometryHistory");

        if (_HasPicked) {
            for (QWidget *w = this; w; w = qobject_cast<QMenu*>(w->parentWidget())) 
                w->hide();
        }
    }
}

// --------------------------------------------------------------------

SelUpMenu::SelUpMenu(QWidget *parent, bool trigger)
    :QMenu(parent)
{
    if (trigger)
        connect(this, SIGNAL(triggered(QAction*)), this, SLOT(onTriggered(QAction *)));
    connect(this, SIGNAL(hovered(QAction*)), this, SLOT(onHovered(QAction *)));
    setupMenuStyle(this);
}

bool SelUpMenu::event(QEvent *e)
{
    return QMenu::event(e);
}

void SelUpMenu::mouseReleaseEvent(QMouseEvent *e)
{
    if (e->button() == Qt::RightButton) {
        QAction *action = actionAt(e->pos());
        if (action) {
            TreeWidget::selectUp(action, this);
            return;
        }
    }
    QMenu::mouseReleaseEvent(e);
}

void SelUpMenu::mousePressEvent(QMouseEvent *e)
{
	for (QWidget *w = this; w; w = qobject_cast<QMenu*>(w->parentWidget())) {
        if (w->rect().contains(w->mapFromGlobal(e->globalPos()))) {
            if (w == this)
                QMenu::mousePressEvent(e);
            break;
        }
        w->hide();
    }
}

void SelUpMenu::onTriggered(QAction *action)
{
    TreeWidget::selectUp(action);
}

void SelUpMenu::onHovered(QAction *action)
{
    setPreselect(this, action);
}

#include "moc_SelectionView.cpp"
