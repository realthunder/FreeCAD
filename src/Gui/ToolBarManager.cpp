/***************************************************************************
 *   Copyright (c) 2005 Werner Mayer <wmayer[at]users.sourceforge.net>     *
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
#include <fastsignals/signal.h>
#ifndef _PreComp_
# include <QAction>
# include <QApplication>
# include <QToolBar>
# include <QToolButton>
# include <QMenu>
# include <QHBoxLayout>
# include <QMouseEvent>
# include <QCheckBox>
# include <QPointer>
# include <QPainter>
# include <QStatusBar>
# include <QMenuBar>
#endif

#include <QWidgetAction>

#include <boost/algorithm/string/predicate.hpp>
#include <boost/algorithm/string.hpp>

#include <Base/Tools.h>
#include <Base/Console.h>

#include "ToolBarManager.h"

#include "Action.h"
#include "Application.h"
#include "Command.h"
#include "MainWindow.h"
#include "OverlayWidgets.h"
#include "WidgetFactory.h"

FC_LOG_LEVEL_INIT("ToolBar", true, 2)


using namespace Gui;

ToolBarItem::ToolBarItem() : visibilityPolicy(DefaultVisibility::Visible)
{
}

ToolBarItem::ToolBarItem(ToolBarItem* item, DefaultVisibility visibilityPolicy) : visibilityPolicy(visibilityPolicy)
{
    if (item) {
        item->appendItem(this);
    }
}

ToolBarItem::~ToolBarItem()
{
    clear();
}

void ToolBarItem::setCommand(const std::string& name)
{
    _name = name;
}

const std::string & ToolBarItem::command() const
{
    return _name;
}

void ToolBarItem::setID(const QString& name)
{
    _id = name;
}

const QString & ToolBarItem::id() const
{
    return _id;
}

bool ToolBarItem::hasItems() const
{
    return _items.count() > 0;
}

ToolBarItem* ToolBarItem::findItem(const std::string& name)
{
    if ( _name == name ) {
        return this;
    }

    for (auto it : qAsConst(_items)) {
        if (it->_name == name) {
            return it;
        }
    }

    return nullptr;
}

ToolBarItem* ToolBarItem::copy() const
{
    auto root = new ToolBarItem;
    root->setCommand( command() );
    root->setID( id() );

    QList<ToolBarItem*> items = getItems();
    for (auto it : items) {
        root->appendItem(it->copy());
    }

    return root;
}

uint ToolBarItem::count() const
{
    return _items.count();
}

void ToolBarItem::appendItem(ToolBarItem* item)
{
    _items.push_back( item );
}

bool ToolBarItem::insertItem( ToolBarItem* before, ToolBarItem* item)
{
    int pos = _items.indexOf(before);
    if (pos != -1) {
        _items.insert(pos, item);
        return true;
    }

    return false;
}

void ToolBarItem::removeItem(ToolBarItem* item)
{
    int pos = _items.indexOf(item);
    if (pos != -1) {
        _items.removeAt(pos);
    }
}

void ToolBarItem::clear()
{
    for (auto it : qAsConst(_items)) {
        delete it;
    }

    _items.clear();
}

ToolBarItem& ToolBarItem::operator << (ToolBarItem* item)
{
    appendItem(item);
    return *this;
}

ToolBarItem& ToolBarItem::operator << (const std::string& command)
{
    auto item = new ToolBarItem(this);
    item->setCommand(command);
    return *this;
}

QList<ToolBarItem*> ToolBarItem::getItems() const
{
    return _items;
}

// -----------------------------------------------------------

ToolBarManager* ToolBarManager::_instance=nullptr;

ToolBarManager* ToolBarManager::getInstance()
{
    if ( !_instance )
        _instance = new ToolBarManager;
    return _instance;
}

void ToolBarManager::destruct()
{
    delete _instance;
    _instance = nullptr;
}

namespace Gui {

class ToolBarArea : public QWidget
{
    using inherited = QWidget;
public:
    ToolBarArea(QWidget *parent,
                ParameterGrp::handle hParam,
                fastsignals::advanced_scoped_connection &conn,
                QTimer *timer = nullptr)
        : QWidget(parent)
        , _sizingTimer(timer)
        , _hParam(hParam)
        , _conn(conn)
    {
        _layout = new QHBoxLayout(this);
        _layout->setContentsMargins(0, 0, 0, 0);
    }

    void addWidget(QWidget *w)
    {
        if (_layout->indexOf(w) < 0) {
            _layout->addWidget(w);
            ToolBarManager::getInstance()->setToolBarMovable(qobject_cast<QToolBar*>(w));
            adjustParentSize();
            QString name = w->objectName();
            if (!name.isEmpty()) {
                Base::ConnectionBlocker block(_conn);
                _hParam->SetInt(w->objectName().toUtf8().constData(), _layout->count()-1);
            }
        }
    }

    void insertWidget(int idx, QWidget *w)
    {
        int index = _layout->indexOf(w);
        if (index == idx)
            return;
        if (index > 0)
            _layout->removeWidget(w);
        _layout->insertWidget(idx, w);
        ToolBarManager::getInstance()->setToolBarMovable(qobject_cast<QToolBar*>(w));
        adjustParentSize();
        saveState();
    }

    void adjustParentSize()
    {
        if (_sizingTimer) {
            _sizingTimer->start(10);
        }
    }

    void removeWidget(QWidget *w)
    {
        _layout->removeWidget(w);
        if (auto tb = qobject_cast<QToolBar*>(w)) {
            if (auto grip = tb->findChild<ToolBarGrip*>()) {
                tb->removeAction(grip->action());
                grip->deleteLater();
            }
        }
        adjustParentSize();
        QString name = w->objectName();
        if (!name.isEmpty()) {
            Base::ConnectionBlocker block(_conn);
            _hParam->RemoveInt(name.toUtf8().constData());
        }
    }

    QWidget *widgetAt(int index) const
    {
        auto item = _layout->itemAt(index);
        return item ? item->widget() : nullptr;
    }

    int count() const
    {
        return _layout->count();
    }

    int indexOf(QWidget *w) const
    {
        return _layout->indexOf(w);
    }

    template<class F>
    void foreachToolBar(F &&f)
    {
        for (int i = 0, c = _layout->count(); i < c; ++i) {
            auto toolbar = qobject_cast<QToolBar*>(widgetAt(i));
            if (!toolbar || toolbar->objectName().isEmpty()
                         || toolbar->objectName().startsWith(QStringLiteral("*")))
                continue;
            f(toolbar, i, this);
        }
    }

    void saveState()
    {
        Base::ConnectionBlocker block(_conn);
        for (auto &v : _hParam->GetIntMap()) {
            _hParam->RemoveInt(v.first.c_str());
        }
        foreachToolBar([this](QToolBar *toolbar, int idx, ToolBarArea*) {
            _hParam->SetInt(toolbar->objectName().toUtf8().constData(), idx);
        });
    };

    void restoreState(const std::map<int, QToolBar*> &toolbars)
    {
        for (auto &v : toolbars) {
            bool vis = v.second->isVisible();
            getMainWindow()->removeToolBar(v.second);
            v.second->setOrientation(Qt::Horizontal);
            insertWidget(v.first, v.second);
            ToolBarManager::getInstance()->setToolBarVisible(v.second, vis);
        }

        for (auto &v : _hParam->GetBoolMap()) {
            auto w = findChild<QWidget*>(QString::fromUtf8(v.first.c_str()));
            if (w)
                w->setVisible(v.second);
        }
    };

private:
    QHBoxLayout *_layout;
    QPointer<QTimer> _sizingTimer;
    ParameterGrp::handle _hParam;
    fastsignals::advanced_scoped_connection &_conn;
};

} // namespace Gui

ToolBar::ToolBar()
    : ToolBar(nullptr)
{}

ToolBar::ToolBar(QWidget* parent)
    : QToolBar(parent)
{}

void ToolBar::initStyleOption(QStyleOptionToolBar* option) const
{
    QToolBar::initStyleOption(option);
}

void ToolBarManager::setupWidgetProducers()
{
    // What lets a workbench ask for one by name. Registered here rather than
    // in resource.cpp because this is the file that owns the class.
    static bool registered = false;
    if (!registered) {
        registered = true;
        new WidgetProducer<Gui::ToolBar>;
    }
}


//////////////////////////////////////////////////////////////////////

ToolBarGrip::ToolBarGrip(QToolBar * parent)
    :QWidget(parent)
{
    QStyle *style = parent->style();
    QStyleOptionToolBar opt;
    static_cast<ToolBar*>(parent)->initStyleOption(&opt);
    opt.features = QStyleOptionToolBar::Movable;
    int width = style->subElementRect(QStyle::SE_ToolBarHandle, &opt, parent).width();
    this->setFixedWidth(width+4);

    setCursor(Qt::OpenHandCursor);
    setMouseTracking(true);
    auto actions = parent->actions();
    _action = parent->insertWidget(actions.isEmpty()?nullptr:actions[0], this);
    setVisible(true);
}

void ToolBarGrip::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    if (auto toolbar = qobject_cast<QToolBar*>(parentWidget())) {
        QStyle *style = toolbar->style();
        QStyleOptionToolBar opt;
        static_cast<ToolBar*>(toolbar)->initStyleOption(&opt);
        opt.features = QStyleOptionToolBar::Movable;
        // opt.rect = style->subElementRect(QStyle::SE_ToolBarHandle, &opt, toolbar);
        opt.rect = this->rect();
        style->drawPrimitive(QStyle::PE_IndicatorToolBarHandle, &opt, &painter, toolbar);
    }
    else {
        painter.setPen(Qt::transparent);
        painter.setOpacity(0.5);
        painter.setBrush(QBrush(Qt::black, Qt::Dense6Pattern));
        QRect rect(this->rect());
        painter.drawRect(rect);
    }
}

void ToolBarGrip::mouseMoveEvent(QMouseEvent *me)
{
    auto toolbar = qobject_cast<QToolBar*>(parentWidget());
    if (!toolbar) {
        return;
    }
    auto area = ToolBarManager::getInstance()->getToolBarArea(toolbar);
    if (!area)
        return;

    QPoint pos = me->globalPos();
    QRect rect(toolbar->mapToGlobal(QPoint(0,0)), toolbar->size());
    if (rect.contains(pos))
        return;

    // If mouse is out of the toolbar, remove the toolbar from area and float the
    // toolbar
    {
        QSignalBlocker blocker(toolbar);
        area->removeWidget(toolbar);
        ToolBarManager::getInstance()->addToolBarToMainWindow(toolbar);
        toolbar->setWindowFlags(Qt::Tool
                | Qt::FramelessWindowHint
                | Qt::X11BypassWindowManagerHint);
        toolbar->adjustSize();
        pos = toolbar->mapFromGlobal(pos);
        toolbar->move(pos.x()-10, pos.y()-10);
        ToolBarManager::getInstance()->setToolBarVisible(toolbar, true);
    }
    toolbar->topLevelChanged(true);

    // After removing from area, this grip will be deleted. In order to
    // continue toolbar dragging (because the mouse button is still pressed),
    // we fake mouse events and send to toolbar. For some reason,
    // send/postEvent() does not work, only timer works.
    QPointer tb(toolbar);
    QTimer::singleShot(0, [tb] {
        auto modifiers = QApplication::queryKeyboardModifiers();
        auto buttons = QApplication::mouseButtons();
        if (buttons != Qt::LeftButton
                || QWidget::mouseGrabber()
                || modifiers != Qt::NoModifier
                || !tb) {
            return;
        }
        QPoint pos(10, 10);
        QPoint globalPos(tb->mapToGlobal(pos));
        QMouseEvent mouseEvent(
                QEvent::MouseButtonPress,
                pos, globalPos, Qt::LeftButton, buttons, modifiers);
        QApplication::sendEvent(tb, &mouseEvent);

        // Mose follow the mouse press event with mouse move with some offset
        // in order to activate toolbar dragging.
        QPoint offset(30, 30);
        QMouseEvent mouseMoveEvent(
                QEvent::MouseMove,
                pos+offset, globalPos+offset,
                Qt::LeftButton, buttons, modifiers);
        QApplication::sendEvent(tb, &mouseMoveEvent);
    });
}

void ToolBarGrip::mousePressEvent(QMouseEvent *)
{
    setCursor(Qt::ClosedHandCursor);
}

void ToolBarGrip::mouseReleaseEvent(QMouseEvent *)
{
    setCursor(Qt::OpenHandCursor);
}

//////////////////////////////////////////////////////////////////////

ToolBarManager::ToolBarManager()
{
    setupWidgetProducers();

    hGeneral = App::GetApplication().GetUserParameter().GetGroup(
            "BaseApp/Preferences/General");

    hGlobal = App::GetApplication().GetUserParameter().GetGroup(
            "BaseApp/Workbench/Global/ToolBar");
    getGlobalToolBarNames();

    hStatusBar = App::GetApplication().GetUserParameter().GetGroup(
            "BaseApp/MainWindow/StatusBar");

    hMenuBarRight = App::GetApplication().GetUserParameter().GetGroup(
            "BaseApp/MainWindow/MenuBarRight");
    hMenuBarLeft = App::GetApplication().GetUserParameter().GetGroup(
            "BaseApp/MainWindow/MenuBarLeft");

    hPref = App::GetApplication().GetUserParameter().GetGroup(
            "BaseApp/MainWindow/ToolBars");

    hMovable = hPref->GetGroup("Movable");
    hWorkbenchReturn = hPref->GetGroup("WorkbenchReturn");

    if (auto sb = getMainWindow()->statusBar()) {
        sb->installEventFilter(this);
        statusBarArea = new ToolBarArea(sb, hStatusBar, connParam);
        statusBarArea->setObjectName(QStringLiteral("StatusBarArea"));
        sb->insertPermanentWidget(2, statusBarArea);
        statusBarArea->show();
    }

    if (auto mb = getMainWindow()->menuBar()) {
        mb->installEventFilter(this);
        menuBarLeftArea = new ToolBarArea(mb, hMenuBarLeft, connParam, &menuBarTimer);
        menuBarLeftArea->setObjectName(QStringLiteral("MenuBarLeftArea"));
        menuBarRightArea = new ToolBarArea(mb, hMenuBarRight, connParam, &menuBarTimer);
        menuBarRightArea->setObjectName(QStringLiteral("MenuBarRightArea"));
        // Watch the areas themselves, not just the menu bar hosting them: with a
        // custom title bar they move out of the menu bar and a right click on
        // one would otherwise reach nothing.
        menuBarLeftArea->installEventFilter(this);
        menuBarRightArea->installEventFilter(this);
        relocateMenuBarAreas();
    }

    globalArea = defaultArea = Qt::TopToolBarArea;
    hMainWindow = App::GetApplication().GetUserParameter().GetGroup("BaseApp/Preferences/MainWindow");
    std::string defarea = hMainWindow->GetASCII("DefaultToolBarArea");
    if (defarea == "Bottom")
        defaultArea = Qt::BottomToolBarArea;
    else if (defarea == "Left")
        defaultArea = Qt::LeftToolBarArea;
    else if (defarea == "Right")
        defaultArea = Qt::RightToolBarArea;
    defarea = hMainWindow->GetASCII("GlobalToolBarArea");
    if (defarea == "Bottom")
        globalArea = Qt::BottomToolBarArea;
    else if (defarea == "Left")
        globalArea = Qt::LeftToolBarArea;
    else if (defarea == "Right")
        globalArea = Qt::RightToolBarArea;

    auto refreshParams = [this](const char *name) {
        if (!name || boost::equals(name, "ToolbarIconSize"))
            _toolBarIconSize = hGeneral->GetInt("ToolbarIconSize", 24);
        if (!name || boost::equals(name, "StatusBarIconSize"))
            _statusBarIconSize = hGeneral->GetInt("StatusBarIconSize", 0);
        if (!name || boost::equals(name, "MenuBarIconSize"))
            _menuBarIconSize = hGeneral->GetInt("MenuBarIconSize", 0);
        if (!name || boost::equals(name, "WorkbenchTabIconSize"))
            _workbenchTabIconSize = hGeneral->GetInt("WorkbenchTabIconSize", 0);
        if (!name || boost::equals(name, "WorkbenchComboIconSize"))
            _workbenchComboIconSize = hGeneral->GetInt("WorkbenchComboIconSize", 0);
    };
    refreshParams(nullptr);

    connParam = App::GetApplication().GetUserParameter().signalParamChanged.connect(
        [this, refreshParams](ParameterGrp *Param, ParameterGrp::ParamType, const char *Name, const char *) {
            if (Param == hGeneral && Name) {
                refreshParams(Name);
            }
            if (Param == hPref
                    || Param == hMovable
                    || Param == hStatusBar
                    || Param == hMenuBarRight
                    || Param == hMenuBarLeft
                    || (Param == hMainWindow
                        && Name
                        && boost::equals(Name, "DefaultToolBarArea"))) {
                timer.start(100);
            }
            else if (Param == hGlobal)
                getGlobalToolBarNames();
        },
        fastsignals::advanced_tag {});
    timer.setSingleShot(true);
    connect(&timer, SIGNAL(timeout()), this, SLOT(onTimer()));

    timerChild.setSingleShot(true);
    QObject::connect(&timerChild, &QTimer::timeout, [this]() {
        toolBars();
    });

    menuBarTimer.setSingleShot(true);
    QObject::connect(&menuBarTimer, &QTimer::timeout, [this]() {
        // Only worth doing while the menu bar is what hosts the two areas. With
        // a custom title bar they sit in the title bar instead, which keeps its
        // own height, and sizing the menu bar to fit them would shrink the text
        // strip around a row it no longer contains.
        if (getMainWindow()->isCustomTitleBar()) {
            return;
        }
        if (auto menuBar = getMainWindow()->menuBar()) {
            menuBar->adjustSize();
        }
    });

    auto toolbars = toolBars();
    for (auto &v : hPref->GetBoolMap()) {
        if (v.first.empty())
            continue;
        QString name = QString::fromUtf8(v.first.c_str());
        QToolBar* toolbar = toolbars[name];
        if (!toolbar)
            toolbar = createToolBar(name);
        toolbar->toggleViewAction()->setVisible(false);
        setToolBarVisible(toolbar, false);
    }

    timerResize.setSingleShot(true);
    QObject::connect(&timerResize, &QTimer::timeout, [this]() {
        for (const auto &v : resizingToolBars) {
            if (v.second) {
                setToolBarIconSize(v.first);
            }
        }
        resizingToolBars.clear();
    });
}

ToolBarManager::~ToolBarManager() = default;

int ToolBarManager::toolBarIconSize(QWidget *widget) const
{
    int s = _toolBarIconSize;
    if (widget) {
        if (qobject_cast<WorkbenchTabWidget*>(widget)) {
            // The tabs live in a toolbar, side by side with real tool buttons,
            // so their icons match those by default rather than being shrunk to
            // 0.8 of them. WorkbenchTabIconSize still overrides it.
            if (_workbenchTabIconSize > 0)
                s = _workbenchTabIconSize;
            widget = widget->parentWidget();
        }
        else if (qobject_cast<WorkbenchComboBox*>(widget)) {
            if (_workbenchComboIconSize > 0)
                s = _workbenchComboIconSize;
            else
                s *= 0.8;
            widget = widget->parentWidget();
        }
    }

    if (widget) {
        if (widget->parentWidget() == statusBarArea) {
            if (_statusBarIconSize > 0)
                s = _statusBarIconSize;
            else
                s *= 0.6;
        }
        else if (widget->parentWidget() == menuBarLeftArea
                || widget->parentWidget() == menuBarRightArea) {
            if (_menuBarIconSize > 0)
                s = _menuBarIconSize;
            else if (!getMainWindow() || !getMainWindow()->isCustomTitleBar()) {
                // In the menu bar's corners these icons sit beside menu text
                // and are shrunk to keep the bar the height that text asks
                // for. The custom title bar hosts the two areas as a toolbar
                // row of its own, with nothing to match but the other
                // toolbars -- so there they take the toolbar icon size.
                // MenuBarIconSize still overrides both.
                s *= 0.8;
            }
        }
    }
    return std::max(s, 5);
}

void ToolBarManager::setToolBarIconSize(QToolBar *toolbar)
{
    int s = getInstance()->toolBarIconSize(toolbar);
    toolbar->setIconSize(QSize(s, s));
}

void ToolBarManager::getGlobalToolBarNames()
{
    globalToolBarNames.clear();
    globalToolBarNames.insert(QStringLiteral("File"));
    globalToolBarNames.insert(QStringLiteral("Structure"));
    globalToolBarNames.insert(QStringLiteral("Macro"));
    globalToolBarNames.insert(QStringLiteral("View"));
    globalToolBarNames.insert(QStringLiteral("Workbench"));
    for (auto &hGrp : this->hGlobal->GetGroups())
        globalToolBarNames.insert(QString::fromUtf8(hGrp->GetGroupName()));
}

bool ToolBarManager::isDefaultMovable() const
{
    return hMovable->GetBool("*", true);
}

void ToolBarManager::setDefaultMovable(bool enable)
{
    hMovable->Clear();
    hMovable->SetBool("*", enable);
}

bool ToolBarManager::areTitleToolBarsLocked() const
{
    // Upstream FreeCAD/FreeCAD#26766 defaults this to true. Here it does not:
    // the toolbars in these areas have always been movable in this fork, and a
    // parameter that has never existed should not lock them on first run.
    return hGeneral->GetBool("LockTitleToolBars", false);
}

void ToolBarManager::setTitleToolBarsLocked(bool locked)
{
    hGeneral->SetBool("LockTitleToolBars", locked);
    setTitleToolBarsMovable(!locked);
}

void ToolBarManager::setTitleToolBarsMovable(bool movable)
{
    for (auto &v : toolBars()) {
        QToolBar *toolbar = v.second;
        if (!toolbar || !getToolBarArea(toolbar))
            continue;
        // setMovable() emits movableChanged, which is what puts the grip and
        // the leading separator in or takes them out again.
        toolbar->setMovable(movable);
    }
}

struct ToolBarKey {
    Qt::ToolBarArea area;
    bool visible;
    int a;
    int b;

    ToolBarKey(QToolBar *tb)
        :area(getMainWindow()->toolBarArea(tb))
        ,visible(tb->isVisible())
    {
        int x = tb->x();
        int y = tb->y();
        switch(area) {
        case Qt::BottomToolBarArea:
            a = -y; b = x;
            break;
        case Qt::LeftToolBarArea:
            a = x; b = y;
            break;
        case Qt::RightToolBarArea:
            a = -x, b = y;
            break;
        default:
            a = y; b = x;
            break;
        }
    }

    bool operator<(const ToolBarKey &other) const {
        if (area < other.area)
            return true;
        if (area > other.area)
            return false;
        if (visible > other.visible)
            return true;
        if (visible < other.visible)
            return false;
        if (a < other.a)
            return true;
        if (a > other.a)
            return false;
        return b < other.b;
    }
};

static bool isToolBarAllowed(QToolBar *toolbar, Qt::ToolBarArea area)
{
    if (area == Qt::TopToolBarArea || area == Qt::BottomToolBarArea)
        return true;
    for (auto w : toolbar->findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly)) {
        if (qobject_cast<QToolButton*>(w)
                || qobject_cast<QMenu*>(w)
                || strstr(w->metaObject()->className(),"Separator"))
            continue;
        return false;
    }
    return true;
}

static bool isToolBarEmpty(QToolBar *toolbar)
{
    // Every QToolBar has a default tool button of private class type
    // QToolBarExtension, hence <= 1 here.
    return toolbar->findChildren<QWidget*>().size() <= 1;
}

void ToolBarManager::onTimer()
{
    Base::StateLocker guard(relocating);
    std::string defarea = hMainWindow->GetASCII("DefaultToolBarArea");
    auto area = Qt::TopToolBarArea;
    if (defarea == "Bottom")
        area = Qt::BottomToolBarArea;
    else if (defarea == "Left")
        area = Qt::LeftToolBarArea;
    else if (defarea == "Right")
        area = Qt::RightToolBarArea;
    defarea = hMainWindow->GetASCII("GlobalToolBarArea");
    auto gArea = Qt::TopToolBarArea;
    if (defarea == "Bottom")
        gArea = Qt::BottomToolBarArea;
    else if (defarea == "Left")
        gArea = Qt::LeftToolBarArea;
    else if (defarea == "Right")
        gArea = Qt::RightToolBarArea;

    auto mw = getMainWindow();
    bool movable = isDefaultMovable();

    std::map<int, QToolBar*> sbToolBars;
    std::map<int, QToolBar*> mbRightToolBars;
    std::map<int, QToolBar*> mbLeftToolBars;
    std::multimap<ToolBarKey, QToolBar*> lines;
    for (auto &v : toolBars()) {
        if (!v.second)
            continue;
        QToolBar *tb = v.second;
        bool isGlobal = globalToolBarNames.count(v.first);
        auto defArea = isGlobal ? globalArea : defaultArea;
        auto curArea = isGlobal ? gArea : area;
        QByteArray name = v.first.toUtf8();
        int idx = hStatusBar->GetInt(name, -1);
        tb->setMovable(hMovable->GetBool(name, movable));

        if (idx >= 0) {
            sbToolBars[idx] = tb;
            continue;
        }

        idx = hMenuBarLeft->GetInt(name, -1);
        if (idx >= 0) {
            mbLeftToolBars[idx] = tb;
            continue;
        }

        idx = hMenuBarRight->GetInt(name, -1);
        if (idx >= 0) {
            mbRightToolBars[idx] = tb;
            continue;
        }

        if (tb->parentWidget() != getMainWindow()) {
            addToolBarToMainWindow(tb);
        }

        if (defArea != curArea && mw->toolBarArea(tb) == defArea)
            lines.emplace(ToolBarKey(tb),tb);
        if (tb->toggleViewAction()->isVisible())
            setToolBarVisible(tb, hPref->GetBool(name, tb->isVisible()));
    }

    bool first = true;
    int a;
    for (auto &v : lines) {
        auto toolbarArea = globalToolBarNames.count(
                v.second->objectName()) ? gArea : area;
        if (!isToolBarAllowed(v.second, toolbarArea))
            continue;
        Base::StateLocker guard(adding);
        if (first)
            first = false;
        else if (a != v.first.a)
            mw->addToolBarBreak(toolbarArea);
        a = v.first.a;
        mw->addToolBar(toolbarArea, v.second);
    }

    if (defaultArea != area || globalArea != gArea) {
        defaultArea = area;
        globalArea = gArea;
        mw->saveWindowSettings(true);
    }

    statusBarArea->restoreState(sbToolBars);
    menuBarRightArea->restoreState(mbRightToolBars);
    menuBarLeftArea->restoreState(mbLeftToolBars);

    // Last, because the loop above gave every toolbar its own stored movability
    // and the toolbars only reached their areas just now.
    setTitleToolBarsMovable(!areTitleToolBarsLocked());
}

QToolBar *ToolBarManager::createToolBar(const QString &name)
{
    Base::StateLocker guard(adding);
    auto toolbar = new QToolBar(getMainWindow());
    getMainWindow()->addToolBar(
            globalToolBarNames.count(name) ? globalArea : defaultArea, toolbar);
    toolbar->setObjectName(name);
    connectToolBar(toolbar);
    return toolbar;
}

void ToolBarManager::connectToolBar(QToolBar *toolbar)
{
    auto &p = connectedToolBars[toolbar];
    if (p == toolbar)
        return;
    p = toolbar;
    toolbar->installEventFilter(this);
    connect(toolbar, SIGNAL(visibilityChanged(bool)), this, SLOT(onToggleToolBar(bool)));
    connect(toolbar, SIGNAL(movableChanged(bool)), this, SLOT(onMovableChanged(bool)));
    connect(toolbar, &QToolBar::topLevelChanged, this, [this, toolbar] {
        if (this->restored)
            getMainWindow()->saveWindowSettings(true);
        checkToolBarIconSize(toolbar);
    });
    connect(toolbar, &QToolBar::iconSizeChanged, this, [this, toolbar] {
        if (toolbar->parentWidget() == menuBarLeftArea) {
            menuBarLeftArea->adjustParentSize();
        }
        else if (toolbar->parentWidget() == menuBarRightArea) {
            menuBarRightArea->adjustParentSize();
        }
    });
    QByteArray name = p->objectName().toUtf8();
    p->setMovable(hMovable->GetBool(name, isDefaultMovable()));
    FC_TRACE("connect toolbar " << name.constData() << ' ' << p);
}

void ToolBarManager::removeToolBar(const QString &id)
{
    auto tb = getMainWindow()->findChild<QToolBar*>(id);
    if (tb) {
        getMainWindow()->removeToolBar(tb);
        delete tb;
        connectedToolBars.erase(tb);
        hPref->RemoveBool(id.toUtf8().constData());
    }
}

bool ToolBarManager::isCustomToolBarName(const char *name)
{
    int dummy;
    return boost::equals(name, "Custom") || sscanf(name, "Custom%d", &dummy)==1;
}

QString ToolBarManager::generateToolBarID(const char *groupName, const char *name)
{
    return QStringLiteral("Custom_%1_%2").arg(QString::fromUtf8(groupName),
                                              QString::fromUtf8(name));
}

void ToolBarManager::setup(ToolBarItem* toolBarItems)
{
    static QPointer<QWidget> _ActionWidget;
    if (!_ActionWidget) {
        _ActionWidget = new QWidget(getMainWindow());
        _ActionWidget->setObjectName(QStringLiteral("_fc_action_widget_"));
		// Although _ActionWidget has zero size, on MacOS it somehow has a
		// 'phantom' size without any visible content and will block the top
		// left tool buttons of the application main window. So we move it out
		// of the way.
		_ActionWidget->move(QPoint(-100,-100));

    } else {
        for (auto action : _ActionWidget->actions())
            _ActionWidget->removeAction(action);
    }

    this->toolbarNames.clear();

    std::map<int,int> widths;

    QList<ToolBarItem*> items = toolBarItems->getItems();
    auto toolbars = toolBars();
    for (auto item : items) {
        // search for the toolbar
        QString name;
        if (item->id().size())
            name = item->id();
        else
            name = QString::fromUtf8(item->command().c_str());

        this->toolbarNames << name;
        std::string toolbarName = item->command();

        QToolBar *toolbar = nullptr;
        auto it = toolbars.find(name);
        if (it != toolbars.end()) {
            toolbar = it->second;
            toolbars.erase(it);
        }
        bool newToolBar = false;
        if (!toolbar) {
            newToolBar = true;
            toolbar = createToolBar(name);
        }

        bool visible;
        // If visibility policy is custom, the toolbar is initialised as not visible, and the
        // toggleViewAction to control its visibility is not visible either.
        //
        // Both are managed under the responsibility of the client code
        if(item->visibilityPolicy == ToolBarItem::DefaultVisibility::Unavailable) {
            visible = false;
            // Prevent that the action to show/hide a toolbar appears on the (contextual) menus.
            // This is also managed by the client code for a toolbar with custom policy
            toolbar->toggleViewAction()->setVisible(false);
        }
        else {
            visible = hPref->GetBool(toolbarName.c_str(),
                    item->visibilityPolicy == ToolBarItem::DefaultVisibility::Visible);
            if (item->id().size()) {
                // Migrate to use toolbar ID instead of title for identification to
                // avoid name conflict when using custom toolbar
                bool v = hPref->GetBool(name.toUtf8().constData(), true);
                if (v == hPref->GetBool(name.toUtf8().constData(), false)) {
                    visible = v;
                }
            }
            // Enable automatic handling of visibility via, for example, (contextual) menu
            toolbar->toggleViewAction()->setVisible(true);

            QByteArray n(name.toUtf8());
            if(newToolBar && hPref->GetBool(n, true) != hPref->GetBool(n, false)) {
                // Make sure we remember the toolbar so that we can pre-create
                // it the next time the application is launched
                Base::ConnectionBlocker block(connParam);
                hPref->SetBool(n, visible);
            }
        }
        // Store item visibility policy within the action
        toolbar->toggleViewAction()->setProperty("DefaultVisibility", static_cast<int>(item->visibilityPolicy));

        bool toolbarAdded = toolbar->windowTitle().isEmpty();

        // setup the toolbar
        setup(item, toolbar);
        if (isToolBarEmpty(toolbar)) {
            toolbar->toggleViewAction()->setVisible(false);
            FC_TRACE("Empty toolbar " << name.toUtf8().constData());
            continue;
        }

        toolbar->setWindowTitle(QApplication::translate("Workbench", toolbarName.c_str())); // i18n
        setToolBarVisible(toolbar, visible);

        auto actions = toolbar->actions();
        for (auto action : actions) {
            _ActionWidget->addAction(action);
        }

        // try to add some breaks to avoid to have all toolbars in one line
        if (toolbarAdded) {
            auto area = getMainWindow()->toolBarArea(toolbar);
            if (!isToolBarAllowed(toolbar, area)) {
                addToolBarToMainWindow(toolbar);
            }

            int &top_width = widths[area];
            if (top_width > 0 && getMainWindow()->toolBarBreak(toolbar))
                top_width = 0;
            // the width() of a toolbar doesn't return useful results so we estimate
            // its size by the number of buttons and the icon size
            QList<QToolButton*> btns = toolbar->findChildren<QToolButton*>();
            top_width += (btns.size() * toolbar->iconSize().width());
            int max_width = toolbar->orientation() == Qt::Vertical
                ? getMainWindow()->height() : getMainWindow()->width();
            if (newToolBar && top_width > max_width) {
                top_width = 0;
                getMainWindow()->insertToolBarBreak(toolbar);
            }
        }
    }

    // hide all unneeded toolbars
    for (auto &v : toolbars) {
        QToolBar *toolbar = v.second;
        if (!toolbar)
            continue;
        // ignore toolbars which do not belong to the previously active workbench
        if (!toolbar->toggleViewAction()->isVisible())
            continue;
        toolbar->toggleViewAction()->setVisible(false);
        setToolBarVisible(toolbar, false);
    }
}

void ToolBarManager::setToolBarVisible(QToolBar *toolbar, bool show)
{
    QSignalBlocker blocker(toolbar);
    if (!show) {
        // make sure that the main window has the focus when hiding the toolbar with
        // the combo box inside
        QWidget *fw = QApplication::focusWidget();
        while (fw &&  !fw->isWindow()) {
            if (fw == toolbar) {
                getMainWindow()->setFocus();
                break;
            }
            fw = fw->parentWidget();
        }
    }
    toolbar->setVisible(show);
}

void ToolBarManager::setup(ToolBarItem* item, QToolBar* toolbar) const
{
    CommandManager& mgr = Application::Instance->commandManager();
    QList<ToolBarItem*> items = item->getItems();
    if (items.empty())
        return;
    QList<QAction*> actions = toolbar->actions();
    for (QList<ToolBarItem*>::ConstIterator it = items.begin(); it != items.end(); ++it) {
        // search for the action item.
        QString cmdName = QString::fromUtf8((*it)->command().c_str());
        QAction* action = findAction(actions, cmdName);
        if (!action) {
            int size = toolbar->actions().size(); 
            if ((*it)->command() == "Separator")
                toolbar->addSeparator();
            else 
                mgr.addTo((*it)->command().c_str(), toolbar);
            auto actions = toolbar->actions();

            // We now support single command adding multiple actions
            if (actions.size()) {
                for (auto i=std::min<qsizetype>(actions.size()-1, size); i<actions.size(); ++i)
                    actions[i]->setData(cmdName);
            }
        } else {
            do {
                // Note: For toolbars we do not remove and re-add the actions
                // because this causes flicker effects. So, it could happen that the order of
                // buttons doesn't match with the order of commands in the workbench.
                int index = actions.indexOf(action);
                actions.removeAt(index);
            } while ((*it)->command() != "Separator"
                    && (action = findAction(actions, cmdName)));
        }
    }

    // remove all tool buttons which we don't need for the moment
    for (QList<QAction*>::Iterator it = actions.begin(); it != actions.end(); ++it) {
        toolbar->removeAction(*it);
    }
}

void ToolBarManager::saveState() const
{
    // Base::ConnectionBlocker block(const_cast<ToolBarManager*>(this)->connParam);

    // ToolBar visibility is now synced using Qt signals
}

void ToolBarManager::restoreState()
{
    Base::StateLocker guard(relocating);
    std::map<int, QToolBar*> sbToolBars;
    std::map<int, QToolBar*> mbRightToolBars;
    std::map<int, QToolBar*> mbLeftToolBars;
    for (auto &v : toolBars()) {
        QToolBar *toolbar = v.second;
        if (!toolbar)
            continue;
        QByteArray toolbarName = toolbar->objectName().toUtf8();
        if (toolbar->windowTitle().isEmpty() && isToolBarEmpty(toolbar)) {
            toolbar->toggleViewAction()->setVisible(false);
            setToolBarVisible(toolbar, false);
        }
        else if (this->toolbarNames.indexOf(toolbar->objectName()) >= 0)
            setToolBarVisible(toolbar, hPref->GetBool(toolbarName, toolbar->isVisible()));
        else
            setToolBarVisible(toolbar, false);
        int idx = hStatusBar->GetInt(toolbarName, -1);
        if (idx >= 0) {
            sbToolBars[idx] = toolbar;
            continue;
        }
        idx = hMenuBarLeft->GetInt(toolbarName, -1);
        if (idx >= 0) {
            mbLeftToolBars[idx] = toolbar;
            continue;
        }
        idx = hMenuBarRight->GetInt(toolbarName, -1);
        if (idx >= 0) {
            mbRightToolBars[idx] = toolbar;
            continue;
        }
        if (toolbar->parentWidget() != getMainWindow()) {
            addToolBarToMainWindow(toolbar);
        }
    }

    statusBarArea->restoreState(sbToolBars);
    menuBarRightArea->restoreState(mbRightToolBars);
    menuBarLeftArea->restoreState(mbLeftToolBars);
    setTitleToolBarsMovable(!areTitleToolBarsLocked());
    restored = true;
}

void ToolBarManager::addToolBarToMainWindow(QToolBar *toolbar)
{
    Base::StateLocker guard(adding);
    getMainWindow()->addToolBar(toolbar);
}

void ToolBarManager::detachMenuBarAreas()
{
    auto mw = getMainWindow();
    if (auto mb = mw->menuBar()) {
        if (mb->cornerWidget(Qt::TopLeftCorner) == menuBarLeftArea) {
            mb->setCornerWidget(nullptr, Qt::TopLeftCorner);
        }
        if (mb->cornerWidget(Qt::TopRightCorner) == menuBarRightArea) {
            mb->setCornerWidget(nullptr, Qt::TopRightCorner);
        }
    }
    for (auto area : {menuBarLeftArea, menuBarRightArea}) {
        if (!area) {
            continue;
        }
        if (auto layout = area->parentWidget() ? area->parentWidget()->layout() : nullptr) {
            layout->removeWidget(area);
        }
        area->setParent(mw);
        area->hide();
    }
}

void ToolBarManager::relocateMenuBarAreas()
{
    if (!menuBarLeftArea || !menuBarRightArea) {
        return;
    }

    auto mw = getMainWindow();
    auto mb = mw->menuBar();

    // Always take them out of the menu bar's corners first. Leaving a corner
    // widget behind while reparenting it elsewhere leaves QMenuBar with a
    // dangling corner and a gap where it used to reserve space.
    if (mb) {
        if (mb->cornerWidget(Qt::TopLeftCorner) == menuBarLeftArea) {
            mb->setCornerWidget(nullptr, Qt::TopLeftCorner);
        }
        if (mb->cornerWidget(Qt::TopRightCorner) == menuBarRightArea) {
            mb->setCornerWidget(nullptr, Qt::TopRightCorner);
        }
    }

    if (mw->isCustomTitleBar()) {
        const auto place = [](QWidget *host, ToolBarArea *area) {
            if (!host) {
                return;
            }
            auto layout = qobject_cast<QHBoxLayout*>(host->layout());
            if (!layout) {
                layout = new QHBoxLayout(host);
                layout->setContentsMargins(0, 0, 0, 0);
                layout->setSpacing(0);
            }
            area->setParent(host);
            layout->addWidget(area);
            area->show();
        };
        place(mw->leftArea(), menuBarLeftArea);
        place(mw->rightArea(), menuBarRightArea);
    }
    else if (mb) {
        // setCornerWidget reparents for us.
        mb->setCornerWidget(menuBarLeftArea, Qt::TopLeftCorner);
        mb->setCornerWidget(menuBarRightArea, Qt::TopRightCorner);
        menuBarLeftArea->show();
        menuBarRightArea->show();
    }
}

QToolBar *ToolBarManager::nextDockToolBar(QToolBar *toolbar)
{
    // QMainWindow does not hand out its dock order, but it has laid the
    // toolbars out by the time anyone asks, and ToolBarKey already turns that
    // geometry into the order the dock is in -- rows for a horizontal area,
    // columns for a vertical one. Read for one toolbar only, and only while
    // \a toolbar is still docked, so the layout is the one on screen.
    auto mw = getMainWindow();
    const auto area = mw->toolBarArea(toolbar);
    std::map<ToolBarKey, QToolBar*> docked;
    for (auto &v : toolBars()) {
        QToolBar *tb = v.second;
        if (tb && !tb->isHidden() && tb->parentWidget() == mw
                && mw->toolBarArea(tb) == area) {
            docked.emplace(ToolBarKey(tb), tb);
        }
    }
    auto it = docked.find(ToolBarKey(toolbar));
    if (it == docked.end() || ++it == docked.end()) {
        return nullptr;
    }
    return it->second;
}

/*! The dock slot the workbench toolbar goes back to, under
 * MainWindow/ToolBars/WorkbenchReturn -- a subgroup for the same reason
 * Movable is one, so that writing it does not look like a toolbar preference
 * change and restart the 100 ms re-placement pass.
 *
 * Absent exactly when the toolbar is not in the title bar. Four entries,
 * because between them they are the whole slot: which dock area it was in,
 * which toolbar it stood in front of there, and where its row began and ended.
 * QMainWindow::removeToolBar() erases all of that from Qt's own
 * MainWindowState the moment the title bar takes the toolbar.
 *
 * The two breaks are read in different cases, and only one of them ever
 * matters at a time. Inserting in front of the anchor already puts the toolbar
 * at the head of that row if that is where it was, so BreakBefore is only
 * needed where there is no anchor to insert in front of -- the toolbar was
 * last in its area, and the break has to be appended before it. BreakAfter is
 * the opposite case: the anchor started the next row, so the toolbar had a row
 * that ended at it, and the anchor needs its break back or the two arrive as
 * one row.
 */
static const char *returnArea = "Area";
static const char *returnAnchor = "Anchor";
static const char *returnBreakBefore = "BreakBefore";
static const char *returnBreakAfter = "BreakAfter";

static const char *toolBarAreaName(Qt::ToolBarArea area)
{
    switch (area) {
    case Qt::BottomToolBarArea:
        return "Bottom";
    case Qt::LeftToolBarArea:
        return "Left";
    case Qt::RightToolBarArea:
        return "Right";
    default:
        return "Top";
    }
}

static Qt::ToolBarArea toolBarAreaFromName(const std::string &name)
{
    if (name == "Bottom") {
        return Qt::BottomToolBarArea;
    }
    if (name == "Left") {
        return Qt::LeftToolBarArea;
    }
    if (name == "Right") {
        return Qt::RightToolBarArea;
    }
    return Qt::TopToolBarArea;
}

void ToolBarManager::setTitleBarToolBars(bool enable)
{
    auto mw = getMainWindow();
    if (!mw || !menuBarLeftArea || !menuBarRightArea) {
        return;
    }

    // One named toolbar, not a measured row. The workbench selector is the same
    // toolbar in every workbench and the one the title bar has room for; taking
    // whatever happened to be laid out on the first row moved a different set
    // per workbench, and moved toolbars the user had put there themselves.
    // Asked of toolBars() rather than findChild(): recorded toolbars are
    // pre-created empty so Qt can restore their position, so the window can
    // hold two of this name and only that map knows which one is the real one.
    auto bars = toolBars();
    auto it = bars.find(QStringLiteral("Workbench"));
    QToolBar *toolbar = it == bars.end() ? nullptr : it->second.data();
    if (!toolbar) {
        return;
    }

    auto area = getToolBarArea(toolbar);
    if (enable ? (area != nullptr) : (area != menuBarLeftArea && area != menuBarRightArea)) {
        // Already where it is being asked to go, or somewhere this did not put
        // it -- the status bar, or an area the user dragged it into.
        return;
    }

    // Nothing below is the user's doing. Every move here hides the toolbar on
    // the way -- removeToolBar() and reparenting both do -- and onToggleToolBar
    // would otherwise write that into the toolbar's visibility preference,
    // where it outlives the session.
    Base::StateLocker guard(relocating);

    // isHidden(), not isVisible(): the latter is false whenever an ancestor is
    // hidden, and the area above this one is hidden for part of a title bar
    // swap. What has to be kept is whether the toolbar was hidden in its own
    // right.
    const bool visible = !toolbar->isHidden();

    Base::ConnectionBlocker block(connParam);

    if (enable) {
        // The dock slot as it stands, so the toolbar can go back into it.
        // addToolBar() appends to the end of the area instead, which turns every
        // trip into the title bar and out again into a move to the bottom of the
        // dock, and merges a row of its own into the one above.
        //
        // Written to the configuration rather than kept in a member: the trip
        // out is usually not in the session that made the trip in. A start with
        // the toolbar already parked -- which is what MainWindow/MenuBarLeft
        // arranges -- has nothing in memory to go back to.
        auto anchor = nextDockToolBar(toolbar);
        hWorkbenchReturn->SetASCII(returnArea, toolBarAreaName(mw->toolBarArea(toolbar)));
        hWorkbenchReturn->SetBool(returnBreakBefore, mw->toolBarBreak(toolbar));
        hWorkbenchReturn->SetBool(returnBreakAfter, anchor && mw->toolBarBreak(anchor));
        if (anchor) {
            hWorkbenchReturn->SetASCII(returnAnchor, anchor->objectName().toUtf8());
        }
        else {
            hWorkbenchReturn->RemoveASCII(returnAnchor);
        }
        mw->removeToolBar(toolbar);
        toolbar->setOrientation(Qt::Horizontal);
        // addWidget() gives it the grip to be dragged back out by, and records
        // the area in MainWindow/MenuBarLeft so the next start puts it here.
        menuBarLeftArea->addWidget(toolbar);
    }
    else {
        // removeWidget() is the one that takes the grip off and drops the
        // area's parameter entry; without it the toolbar re-docks wearing a
        // grip it has no area to be dragged around by, and the parameter would
        // send it back to the area on the next start.
        area->removeWidget(toolbar);

        // The anchor is a name, so it has to be looked up again: the workbench
        // in charge now need not be the one that was there when it was
        // recorded. Gone -- a workbench-specific toolbar, or a toolbar the user
        // has since moved elsewhere -- and the end of the area is as good an
        // answer as any.
        QToolBar *anchor = nullptr;
        const QString name = QString::fromUtf8(hWorkbenchReturn->GetASCII(returnAnchor).c_str());
        if (!name.isEmpty()) {
            auto found = bars.find(name);
            if (found != bars.end() && found->second && found->second->parentWidget() == mw) {
                anchor = found->second;
            }
        }
        const auto dockArea = toolBarAreaFromName(hWorkbenchReturn->GetASCII(returnArea, "Top"));

        Base::StateLocker adder(adding);
        if (anchor) {
            // Inserting in front of the anchor lands the toolbar back on the
            // anchor's row, at the head of it if that is where it was -- so the
            // break before it needs nothing done. The break after it does: the
            // anchor gave its own up when the two rows merged into one.
            mw->insertToolBar(anchor, toolbar);
            if (hWorkbenchReturn->GetBool(returnBreakAfter, false)
                    && !mw->toolBarBreak(anchor)) {
                mw->insertToolBarBreak(anchor);
            }
        }
        else {
            // Last in its area: appending is the right place, and the break has
            // to go first because there is nothing to insert in front of.
            if (hWorkbenchReturn->GetBool(returnBreakBefore, false)) {
                mw->addToolBarBreak(dockArea);
            }
            mw->addToolBar(dockArea, toolbar);
        }
        hWorkbenchReturn->Clear();
    }
    setToolBarVisible(toolbar, visible);
}

ToolBarArea *ToolBarManager::getToolBarArea(QToolBar *toolbar)
{
    auto parent = toolbar->parentWidget();
    if (parent == statusBarArea)
        return statusBarArea;
    if (parent == menuBarLeftArea)
        return menuBarLeftArea;
    if (parent == menuBarRightArea)
        return menuBarRightArea;
    return nullptr;
}

void ToolBarManager::retranslate()
{
    for (auto &v : toolBars()) {
        QToolBar *toolbar = v.second;
        if (!toolbar) continue;
        if (toolbar->objectName().startsWith(QStringLiteral("Custom_")))
            continue;
        QByteArray toolbarName = toolbar->objectName().toUtf8();
        if (toolbar->windowTitle().size())
            toolbar->setWindowTitle(QApplication::translate("Workbench", (const char*)toolbarName));
    }
}

QAction* ToolBarManager::findAction(const QList<QAction*>& acts, const QString& item) const
{
    for (QList<QAction*>::ConstIterator it = acts.begin(); it != acts.end(); ++it) {
        if ((*it)->data().toString() == item)
            return *it;
    }

    return 0; // no item with the user data found
}

std::map<QString, QPointer<QToolBar>> ToolBarManager::toolBars()
{
    std::map<QString, QPointer<QToolBar>> res;
    auto mw = getMainWindow();
    for (auto tb : mw->findChildren<QToolBar*>()) {
        auto parent = tb->parentWidget();
        if (!parent)
            continue;
        if (parent != mw
                && parent != mw->statusBar()
                && parent != statusBarArea
                && parent != menuBarLeftArea
                && parent != menuBarRightArea)
            continue;
        QString name = tb->objectName();
        if (name.isEmpty() || name.startsWith(QStringLiteral("*")))
            continue;
        auto &p = res[name];
        if (!p) {
            p = tb;
            connectToolBar(tb);
            continue;
        }
        if (p->windowTitle().isEmpty() || tb->windowTitle().isEmpty()) {
            // We now pre-create all recorded toolbars (with an empty title)
            // so that when application starts, Qt can restore the toolbar
            // position properly. However, some user code may later create the
            // toolbar with the same name without checking existence (e.g. Draft
            // tray). So we will replace our toolbar here.
            //
            // Also, because findChildren() may return object in any order, we
            // will only replace the toolbar having an empty title with the
            // other that has a title.

            QToolBar *a = p, *b = tb;
            if (a->windowTitle().isEmpty() && isToolBarEmpty(a))
                std::swap(a, b);
            if (!a->windowTitle().isEmpty() || !isToolBarEmpty(a)) {
                // replace toolbar and insert it at the same location
                FC_TRACE("replacing " << name.toUtf8().constData() << ' ' << b << " -> " << a);
                getMainWindow()->insertToolBar(b, a);
                p = a;
                connectToolBar(a);
                if (connectedToolBars.erase(b)) {
                    b->setObjectName(QStringLiteral("__scrapped"));
                    b->deleteLater();
                }
                continue;
            }
        }

        if (FC_LOG_INSTANCE.isEnabled(FC_LOGLEVEL_LOG))
            FC_WARN("duplicate toolbar " << name.toUtf8().constData());
    }
    return res;
}

void ToolBarManager::onToggleToolBar(bool visible)
{
    auto toolbar = qobject_cast<QToolBar*>(sender());
    if (!toolbar)
        return;
    if (visible && toolbar->windowTitle().isEmpty() && isToolBarEmpty(toolbar)) {
        setToolBarVisible(toolbar, false);
        return;
    }
    if (!restored || relocating || getMainWindow()->isRestoringWindowState())
        return;

    bool enabled = visible;
    if (!visible && !toolbar->toggleViewAction()->isVisible()) {
        // This usually means the toolbar is hidden as a result of
        // workbench switch. The parameter entry however means whether
        // the toolbar is enabled while at its owner workbench
        enabled = true;
    }
    Base::ConnectionBlocker block(connParam);
    hPref->SetBool(toolbar->objectName().toUtf8(), enabled);
    auto parent = toolbar->parentWidget();
    if (parent == menuBarLeftArea || parent == menuBarRightArea) {
        menuBarTimer.start(10);
    }
}

void ToolBarManager::onToggleStatusBarWidget(QWidget *w, bool visible)
{
    Base::ConnectionBlocker block(connParam);
    w->setVisible(visible);
    hStatusBar->SetBool(w->objectName().toUtf8().constData(), w->isVisible());
}

void ToolBarManager::onMovableChanged(bool)
{
    setToolBarMovable(qobject_cast<QToolBar*>(sender()));
}

void ToolBarManager::setToolBarMovable(QToolBar *toolbar)
{
    if (!toolbar)
        return;

    bool movable = toolbar->isMovable() && isDefaultMovable();

    if (restored && !toolbar->objectName().isEmpty()) {
        Base::ConnectionBlocker block(connParam);
        hMovable->SetBool(toolbar->objectName().toUtf8(), movable);
    }
    if (auto area = getToolBarArea(toolbar)) {
        if (auto grip = toolbar->findChild<ToolBarGrip*>()) {
            if (!movable) {
                toolbar->removeAction(grip->action());
                grip->deleteLater();
            }
        }
        else {
            new ToolBarGrip(toolbar);
        }
        area->adjustParentSize();
    }
    QString name = QStringLiteral("_fc_toolbar_sep_");
    auto sep = toolbar->findChild<QAction*>(name);
    if (sep) {
        if (movable) {
            toolbar->removeAction(sep);
            sep->deleteLater();
        }
    }
    else if (!movable) {
        auto actions = toolbar->actions();
        if (actions.size()) {
            sep = toolbar->insertSeparator(actions[0]);
            sep->setObjectName(name);
        }
    }
}

void ToolBarManager::checkToolBar()
{
    if (_instance && !_instance->adding) {
        // In case some user code creates its own toolbar without going through
        // the toolbar manager, we shall call toolBars() using a timer to try
        // to hook it up. One example is 'Draft Snap'. See comment in
        // toolBars() on how we deal with this and move the toolBar to previous
        // saved position.
        _instance->timerChild.start();
    }
}

QRect ToolBarManager::menuBarDropRect() const
{
    auto mw = getMainWindow();

    if (mw->isCustomTitleBar()) {
        // The two areas live in the title bar now, and the menu bar is only the
        // text strip beside them -- a fraction of the row's width. Drop against
        // the whole title bar, or most of it would not accept a toolbar at all.
        auto left = mw->leftArea();
        auto titleBar = left ? left->parentWidget() : nullptr;
        if (!titleBar || !titleBar->isVisible()) {
            return {};
        }
        return {titleBar->mapToGlobal(QPoint(0, 0)), titleBar->size()};
    }

    auto menuBar = mw->menuBar();
    if (!menuBar || !menuBar->isVisible()) {
        return {};
    }
    return {menuBar->mapToGlobal(QPoint(0, 0)), menuBar->size()};
}

bool ToolBarManager::addToolBarToArea(QObject *o, QMouseEvent *ev)
{
    auto statusBar = getMainWindow()->statusBar();
    if (!statusBar || !statusBar->isVisible())
        statusBar = nullptr;

    const QRect menuBarRect = menuBarDropRect();
    if (menuBarRect.isEmpty() && !statusBar)
        return false;

    auto tb = qobject_cast<QToolBar*>(o);
    if (!tb || !tb->isFloating())
        return false;

    static QPointer<OverlayDragFrame> tbPlaceholder;
    static QPointer<ToolBarArea> lastArea;
    static int tbIndex = -1;
    if (ev->type() == QEvent::MouseMove) {
        if (tb->orientation() != Qt::Horizontal
            || ev->buttons() != Qt::LeftButton) {
            if (tbIndex >= 0) {
                if (lastArea) {
                    lastArea->removeWidget(tbPlaceholder);
                    lastArea = nullptr;
                }
                tbPlaceholder->hide();
                tbIndex = -1;
            }
            return false;
        }
    }

    if (ev->type() == QEvent::MouseButtonRelease && ev->button() != Qt::LeftButton)
        return false;

    QPoint pos = QCursor::pos();
    ToolBarArea *area = nullptr;
    if (statusBar) {
        QRect rect(statusBar->mapToGlobal(QPoint(0,0)), statusBar->size());
        if (rect.contains(pos))
            area = statusBarArea;
    }
    if (!area) {
        if (menuBarRect.isEmpty()) {
            return false;
        }
        const QRect &rect = menuBarRect;
        if (rect.contains(pos)) {
            if (pos.x() - rect.left() < rect.width()/2) {
                area = menuBarLeftArea;
            }
            else {
                area = menuBarRightArea;
            }
        }
        else {
            if (tbPlaceholder) {
                if (lastArea) {
                    lastArea->removeWidget(tbPlaceholder);
                    lastArea = nullptr;
                }
                tbPlaceholder->hide();
                tbIndex = -1;
            }
            return false;
        }
    }

    int idx = 0;
    for (int c = area->count(); idx < c ;++idx) {
        auto w = area->widgetAt(idx);
        if (!w || w->isHidden())
            continue;
        int p = w->mapToGlobal(w->rect().center()).x();
        if (pos.x() < p)
            break;
    }
    if (tbIndex >= 0 && tbIndex == idx-1)
        idx = tbIndex;
    if (ev->type() == QEvent::MouseMove) {
        if (!tbPlaceholder) {
            tbPlaceholder = new OverlayDragFrame(getMainWindow());
            tbPlaceholder->hide();
            tbIndex = -1;
        }
        if (tbIndex != idx) {
            tbIndex = idx;
            tbPlaceholder->setSizePolicy(tb->sizePolicy());
            tbPlaceholder->setMinimumWidth(tb->minimumWidth());
            tbPlaceholder->resize(tb->size());
            area->insertWidget(idx, tbPlaceholder);
            lastArea = area;
            tbPlaceholder->adjustSize();
            tbPlaceholder->show();
        }
    } else {
        tbIndex = idx;
        QTimer::singleShot(10, tb, [tb,this]() {
            if (!lastArea)
                return;
            else {
                tbPlaceholder->hide();
                QSignalBlocker block(tb);
                lastArea->removeWidget(tbPlaceholder);
                getMainWindow()->removeToolBar(tb);
                tb->setOrientation(Qt::Horizontal);
                lastArea->insertWidget(tbIndex, tb);
                ToolBarManager::getInstance()->setToolBarVisible(tb, true);
                lastArea = nullptr;
            }
            tb->topLevelChanged(false);
            tbIndex = -1;
        });
    }
    return false;
}

bool ToolBarManager::showContextMenu(QObject *source)
{
    QMenu menu;
    QHBoxLayout *layout = nullptr;
    ToolBarArea *area;
    if (getMainWindow()->statusBar() == source) {
        // The status bar carries two kinds of thing: toolbars parked in its
        // area, which the rest of this function lists, and the widgets
        // registered through MainWindow::addStatusBarItem(). Both belong in
        // the one menu the user gets from a right-click.
        getMainWindow()->buildStatusBarContextMenu(menu);
        menu.addSeparator();
        area = statusBarArea;
        for (auto l : source->findChildren<QHBoxLayout*>()) {
            if(l->indexOf(area) >= 0) {
                layout = l;
                break;
            }
        }
    }
    else if (source == menuBarLeftArea) {
        area = menuBarLeftArea;
    }
    else if (source == menuBarRightArea) {
        area = menuBarRightArea;
    }
    else if (getMainWindow()->menuBar() == source) {
        QPoint pos = QCursor::pos();
        QRect rect(menuBarLeftArea->mapToGlobal(QPoint(0,0)), menuBarLeftArea->size());
        if (rect.contains(pos)) {
            area = menuBarLeftArea;
        }
        else {
            rect = QRect(menuBarRightArea->mapToGlobal(QPoint(0,0)), menuBarRightArea->size());
            if (rect.contains(pos)) {
                area = menuBarRightArea;
            }
            else {
                return false;
            }
        }
    }
    else {
        return false;
    }
    auto tooltip = QObject::tr("Toggles visibility");
    QCheckBox *checkbox;

    auto addMenuVisibleItem = [&](QToolBar *toolbar, int, ToolBarArea *) {
        auto action = toolbar->toggleViewAction();
        if ((action->isVisible() || toolbar->isVisible()) && action->text().size()) {
            action->setVisible(true);
            auto wa = Action::addCheckBox(&menu, action->text(),
                    tooltip, action->icon(), action->isChecked(), &checkbox);
            QObject::connect(checkbox, SIGNAL(toggled(bool)), action, SIGNAL(triggered(bool)));
            QObject::connect(wa, SIGNAL(triggered(bool)), action, SIGNAL(triggered(bool)));
        }
    };

    if (layout) {
        for (int i = 0, c = layout->count(); i < c; ++i) {
            auto w = layout->itemAt(i)->widget();
            if (!w || w == area
                || w->objectName().isEmpty()
                || w->objectName().startsWith(QStringLiteral("*")))
            {
                continue;
            }
            QString name = w->windowTitle();
            if (name.isEmpty()) {
                name = w->objectName();
                name.replace(QLatin1Char('_'), QLatin1Char(' '));
                name = name.simplified();
            }
            auto wa = Action::addCheckBox(&menu, name,
                    tooltip, QIcon(), w->isVisible(), &checkbox);
            auto onToggle = [w, this](bool visible) {onToggleStatusBarWidget(w, visible);};
            QObject::connect(checkbox, &QCheckBox::toggled, onToggle);
            QObject::connect(wa, &QAction::triggered, onToggle);
        }
    }

    area->foreachToolBar(addMenuVisibleItem);
    if (menu.isEmpty()) {
        // An empty area has nothing to offer; let the caller fall back.
        return false;
    }
    menu.exec(QCursor::pos());
    return true;
}

bool ToolBarManager::eventFilter(QObject *o, QEvent *e)
{
    bool res = false;
    switch(e->type()) {
    case QEvent::MouseButtonRelease: {
        auto mev = static_cast<QMouseEvent*>(e);
        if (mev->button() == Qt::RightButton) {
            if (showContextMenu(o)) {
                return true;
            }
            if (o == menuBarLeftArea || o == menuBarRightArea) {
                // Nothing is parked here yet. The empty stretch of a title bar
                // is still a sensible place to ask for the window's own toolbar
                // and dock menu, which is where the toolbars come from.
                if (auto menu = getMainWindow()->createPopupMenu()) {
                    menu->setAttribute(Qt::WA_DeleteOnClose);
                    menu->exec(QCursor::pos());
                    return true;
                }
            }
        }
    }
    // fall through
    case QEvent::MouseMove:
        res = addToolBarToArea(o, static_cast<QMouseEvent*>(e));
        break;
    case QEvent::ChildAdded:
        if (auto tb = qobject_cast<QToolBar*>(o)) {
            if (tb->toggleViewAction()->isVisible()
                || !globalToolBarNames.count(tb->objectName()))
                break;
            QByteArray name = tb->objectName().toUtf8();
            if (!hGlobal->HasGroup(name.constData()))
                break;
            std::string toolbarName = hGlobal->GetGroup(name.constData())->GetASCII("Name");

            tb->toggleViewAction()->setVisible(true);
            if (tb->windowTitle().isEmpty())
                tb->setWindowTitle(QApplication::translate("Workbench", toolbarName.c_str()));
            if (!tb->isVisible() && hPref->GetBool(toolbarName.c_str(), true))
                tb->setVisible(true);
        }
        break;
    default:
        break;
    }
    return res;
}

void ToolBarManager::setToolBarVisibility(bool show, const QList<QString>& names)
{
    auto toolbars = toolBars();
    for (auto& name : names) {
        auto it = toolbars.find(name);
        if (it == toolbars.end() || !it->second)
            continue;
        QToolBar *tb = it->second;
        if (show) {
            if(hPref->GetBool(name.toStdString().c_str(), true))
                tb->show();
            tb->toggleViewAction()->setVisible(true);
        }
        else {
            tb->hide();
            tb->toggleViewAction()->setVisible(false);
        }
    }
}

QSize ToolBarManager::actionsIconSize(const QList<QAction*> &actions, QWidget *widget)
{
    int s = getInstance()->toolBarIconSize(widget);
    QSize iconSize(s, s);
    for (auto action : actions) {
        if (!action->isVisible())
            continue;
        auto icon = action->icon();
        if (icon.isNull())
            continue;
        auto size = icon.actualSize(iconSize);
        if (size.height() < iconSize.height()) {
            iconSize.setWidth(static_cast<float>(iconSize.height())/size.height()*size.width());
        }
    }
    return iconSize;
}

void ToolBarManager::checkToolBarIconSize(QToolBar *tb)
{
    if (!tb)
        return;
    auto &v = resizingToolBars[tb];
    if (!v) {
        timerResize.start(100);
        v = tb;
    }
}

void ToolBarManager::checkToolBarIconSize(QAction *action)
{
    for (auto w : action->associatedWidgets()) {
        if (auto tb = qobject_cast<QToolBar*>(w))
            checkToolBarIconSize(tb);
    }
}

ToolBarItem::DefaultVisibility ToolBarManager::getToolbarPolicy(const QToolBar* toolbar) const
{
    auto* action = toolbar->toggleViewAction();

    QVariant property = action->property("DefaultVisibility");
    if (property.isNull()) {
        return ToolBarItem::DefaultVisibility::Visible;
    }

    return static_cast<ToolBarItem::DefaultVisibility>(property.toInt());
}

void ToolBarManager::setState(const QList<QString>& names, State state)
{
    auto toolbars = this->toolBars();
    for (auto& name : names) {
        auto it = toolbars.find(name);
        if (it == toolbars.end())
            continue;

        QToolBar *tb = it->second;
        bool visible = true;
        if (state == State::RestoreDefault) {

            auto policy = getToolbarPolicy(tb);
            if(policy == ToolBarItem::DefaultVisibility::Unavailable) {
                visible = false;
                tb->toggleViewAction()->setVisible(false);
            }
            else {
                visible = hPref->GetBool(name.toStdString().c_str(),
                        policy == ToolBarItem::DefaultVisibility::Visible);
                tb->toggleViewAction()->setVisible(true);
            }
        }
        else if (state == State::ForceAvailable) {

            auto policy = getToolbarPolicy(tb);

            tb->toggleViewAction()->setVisible(true);

            // Unavailable policy defaults to a Visible toolbars when made available
            visible = hPref->GetBool(name.toStdString().c_str(),
                    policy == ToolBarItem::DefaultVisibility::Visible
                    || policy == ToolBarItem::DefaultVisibility::Unavailable);
        }
        else if (state == State::ForceHidden) {
            tb->toggleViewAction()->setVisible(false); // not visible in context menus
            visible = false;

        }
        else if (state == State::SaveState) {
            auto show = tb->isVisible();
            hPref->SetBool(name.toStdString().c_str(), show);
            continue;
        }
        setToolBarVisible(tb, visible);
    }
}

void ToolBarManager::setupToolBarIconSize()
{
    int s = toolBarIconSize();
    getMainWindow()->setIconSize(QSize(s, s));
    // Most of the the toolbar will have explicit icon size, so the above call
    // to QMainWindow::setIconSize() will have no effect. We need to explicitly
    // change the icon size.
    for (auto toolbar : getMainWindow()->findChildren<QToolBar*>()) {
        setToolBarIconSize(toolbar);
    }
}

#include "moc_ToolBarManager.cpp"
