/***************************************************************************
 *   Copyright (c) 2026 Zheng, Lei <realthunder.dev@gmail.com>             *
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
#include <algorithm>
#include <atomic>
#include <cstring>
#include <set>
#include <string>
#include <vector>
#include <QApplication>
#include <QBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollArea>
#include <QStyle>
#include <QThread>
#include <QToolButton>
#endif

#include <App/Application.h>
#include <App/Document.h>
#include <App/PropertyStandard.h>
#include <Base/Parameter.h>
#include <Gui/Application.h>
#include <Gui/BitmapFactory.h>
#include <Gui/Control.h>
#include <Gui/Document.h>
#include <Gui/MDIView.h>
#include <Gui/MainWindow.h>
#include <Gui/Selection.h>
#include <Gui/View3DInventor.h>
#include <Gui/View3DInventorViewer.h>
#include <Gui/ViewArea.h>
#include <Gui/ViewParams.h>
#include <Gui/ViewerContext.h>

#include "TaskPanelHost.h"
#include "TaskView.h"


using namespace Gui::TaskView;

namespace
{

/// Every host there is. The GUI thread's alone; \a hostCount is what any
/// thread may ask.
std::vector<TaskPanelHost*> hosts;
std::atomic<int> hostCount {0};

/// Clear of the place's own chrome: a cell's border, its menu button in
/// the top left corner, the action zones in the top right and the bottom
/// left one (Gui::ViewAreaCell).
constexpr int Margin = 4;
constexpr int TopMargin = 20;
constexpr int BottomMargin = 18;
/// The least height a place must have for a panel to be shown open
constexpr int MinHeight = 160;

/// The properties a view holds its panel's place in (TaskPlacement)
constexpr const char* PropPlace = "Task_Place";
constexpr const char* PropMode = "Task_Mode";
constexpr const char* PropSide = "Task_Side";
constexpr const char* PropSize = "Task_Size";
constexpr const char* PropGroup = "Task";

/// What was last chosen in any view, for the views that hold nothing of
/// their own: remembered across runs
ParameterGrp::handle lastChosen()
{
    return App::GetApplication().GetParameterGroupByPath(
        "User parameter:BaseApp/Preferences/TaskView/Host");
}

/// The text \a view holds under \a name; empty when it holds none
std::string ownText(const Gui::MDIView* view, const char* name)
{
    if (!view) {
        return {};
    }
    auto prop = Base::freecad_dynamic_cast<App::PropertyString>(view->getPropertyByName(name));
    return prop ? prop->getStrValue() : std::string();
}

/// Keep \a value in \a view under \a name. The property is made the
/// first time there is something to keep, and emptied, not removed, when
/// the view gives its own choice up.
void setOwnText(Gui::MDIView* view, const char* name, const char* doc, const std::string& value)
{
    if (!view) {
        return;
    }
    App::Property* prop = view->getPropertyByName(name);
    auto text = Base::freecad_dynamic_cast<App::PropertyString>(prop);
    if (!text) {
        if (prop || value.empty()) {
            // Somebody else's property of that name, or nothing to keep
            return;
        }
        text = Base::freecad_dynamic_cast<App::PropertyString>(
            view->addDynamicProperty("App::PropertyString", name, PropGroup, doc));
        if (!text) {
            return;
        }
    }
    if (text->getStrValue() != value) {
        text->setValue(value);
    }
}

TaskPlacement::Mode modeOf(const std::string& text, TaskPlacement::Mode otherwise)
{
    if (text == "Overlay") {
        return TaskPlacement::Mode::Overlay;
    }
    if (text == "Side") {
        return TaskPlacement::Mode::Side;
    }
    return otherwise;
}

const char* textOf(TaskPlacement::Mode mode)
{
    return mode == TaskPlacement::Mode::Side ? "Side" : "Overlay";
}

TaskPlacement::Side sideOf(const std::string& text, TaskPlacement::Side otherwise)
{
    if (text == "Left") {
        return TaskPlacement::Side::Left;
    }
    if (text == "Right") {
        return TaskPlacement::Side::Right;
    }
    if (text == "Top") {
        return TaskPlacement::Side::Top;
    }
    if (text == "Bottom") {
        return TaskPlacement::Side::Bottom;
    }
    return otherwise;
}

const char* textOf(TaskPlacement::Side side)
{
    switch (side) {
        case TaskPlacement::Side::Right:
            return "Right";
        case TaskPlacement::Side::Top:
            return "Top";
        case TaskPlacement::Side::Bottom:
            return "Bottom";
        default:
            return "Left";
    }
}

/// The widget the view fills: its cell when it is in a view area -- on
/// the unified canvas the view itself is hidden and the cell is what is
/// seen -- else the view.
QWidget* placeFor(Gui::MDIView* view)
{
    if (Gui::ViewArea* area = Gui::ViewArea::areaOf(view)) {
        if (Gui::ViewAreaCell* cell = area->cellOf(view)) {
            return cell;
        }
    }
    return view;
}

/// Make \a view the one being worked in, its cell with it.
void activate(Gui::MDIView* view)
{
    if (Gui::ViewArea* area = Gui::ViewArea::areaOf(view)) {
        if (area->activateCellOf(view)) {
            return;
        }
    }
    if (Gui::MainWindow* mw = Gui::getMainWindow()) {
        mw->setActiveWindow(view);
    }
}

/// The host's header: the title, the buttons, and the grip the host is
/// dragged by.
class HostHeader: public QWidget
{
public:
    explicit HostHeader(TaskPanelHost* host)
        : QWidget(host)
        , host(host)
    {
        // No Q_OBJECT here (the class lives in this .cpp), so the object
        // name is what tests and style sheets can find it by.
        setObjectName(QStringLiteral("taskPanelHostHeader"));
        setCursor(Qt::OpenHandCursor);
    }

protected:
    void mousePressEvent(QMouseEvent* ev) override
    {
        if (ev->button() != Qt::LeftButton) {
            QWidget::mousePressEvent(ev);
            return;
        }
        pressed = true;
        dragging = false;
        pressX = ev->globalPosition().toPoint().x();
        startX = host->x();
        ev->accept();
    }
    void mouseMoveEvent(QMouseEvent* ev) override
    {
        if (!pressed || !(ev->buttons() & Qt::LeftButton)) {
            QWidget::mouseMoveEvent(ev);
            return;
        }
        const int dx = ev->globalPosition().toPoint().x() - pressX;
        if (!dragging && qAbs(dx) < QApplication::startDragDistance()) {
            return;
        }
        dragging = true;
        host->dragTo(startX + dx);
    }
    void mouseReleaseEvent(QMouseEvent* ev) override
    {
        const bool dragged = pressed && dragging;
        pressed = dragging = false;
        if (dragged) {
            host->dragEnded();
        }
        ev->accept();
    }
private:
    TaskPanelHost* host;
    bool pressed {false};
    bool dragging {false};
    int pressX {0};
    int startX {0};
};

}  // namespace

// ----------------------------------------------------------------------------

TaskPlacement::Place TaskPlacement::place(const MDIView* view)
{
    const std::string text = ownText(view, PropPlace);
    if (text == "InView") {
        return Place::InView;
    }
    if (text == "ComboView") {
        return Place::ComboView;
    }
    return Place::Default;
}

bool TaskPlacement::inView(const MDIView* view)
{
    switch (place(view)) {
        case Place::InView:
            return true;
        case Place::ComboView:
            return false;
        default:
            return ViewParams::getTaskPanelInView();
    }
}

void TaskPlacement::setPlace(MDIView* view, Place place)
{
    const char* text = place == Place::InView ? "InView" : place == Place::ComboView ? "ComboView" : "";
    setOwnText(view,
               PropPlace,
               "Where this view's task panel is shown: ComboView, InView, or empty to\n"
               "follow the preference.",
               text);
}

TaskPlacement::Mode TaskPlacement::mode(const MDIView* view)
{
    const Mode last = modeOf(lastChosen()->GetASCII("Mode", "Overlay"), Mode::Overlay);
    return modeOf(ownText(view, PropMode), last);
}

TaskPlacement::Side TaskPlacement::side(const MDIView* view)
{
    const Side last = sideOf(lastChosen()->GetASCII("Side", "Left"), Side::Left);
    return sideOf(ownText(view, PropSide), last);
}

void TaskPlacement::setMode(MDIView* view, Mode mode)
{
    lastChosen()->SetASCII("Mode", textOf(mode));
    setOwnText(view,
               PropMode,
               "How this view's task panel is shown in it: Overlay, over the picture, or\n"
               "Side, in a cell of its own beside it.",
               textOf(mode));
}

void TaskPlacement::setSide(MDIView* view, Side side)
{
    lastChosen()->SetASCII("Side", textOf(side));
    setOwnText(view,
               PropSide,
               "The side of this view its task panel is on: Left, Right, Top or Bottom.",
               textOf(side));
}

int TaskPlacement::size(const MDIView* view)
{
    if (!view) {
        return 0;
    }
    auto prop = Base::freecad_dynamic_cast<App::PropertyInteger>(view->getPropertyByName(PropSize));
    return prop ? static_cast<int>(prop->getValue()) : 0;
}

void TaskPlacement::setSize(MDIView* view, int size)
{
    if (!view) {
        return;
    }
    App::Property* prop = view->getPropertyByName(PropSize);
    auto number = Base::freecad_dynamic_cast<App::PropertyInteger>(prop);
    if (!number) {
        if (prop || size <= 0) {
            return;
        }
        number = Base::freecad_dynamic_cast<App::PropertyInteger>(
            view->addDynamicProperty("App::PropertyInteger",
                                     PropSize,
                                     PropGroup,
                                     "The size of this view's task panel across, in pixels; 0 for "
                                     "what the panel asks."));
        if (!number) {
            return;
        }
    }
    if (number->getValue() != size) {
        number->setValue(size);
    }
}

bool TaskPlacement::isProperty(const App::Property& prop)
{
    const char* name = prop.getName();
    return name
        && (std::strcmp(name, PropPlace) == 0 || std::strcmp(name, PropMode) == 0
            || std::strcmp(name, PropSide) == 0 || std::strcmp(name, PropSize) == 0);
}

void TaskPlacement::applyToAll()
{
    // Every view: the ones in the main window, in a cell or a tab, and the
    // ones a document has outside it
    std::set<MDIView*> views;
    if (MainWindow* mw = getMainWindow()) {
        for (MDIView* view : mw->findChildren<MDIView*>()) {
            views.insert(view);
        }
    }
    for (App::Document* doc : App::GetApplication().getDocuments()) {
        if (Gui::Document* gui = Application::Instance->getDocument(doc)) {
            for (MDIView* view : gui->getMDIViews()) {
                views.insert(view);
            }
        }
    }
    for (MDIView* view : views) {
        if (place(view) != Place::Default) {
            setPlace(view, Place::Default);
        }
    }
    if (TaskView* taskView = Control().taskPanel()) {
        taskView->followPlacement();
    }
}

// ----------------------------------------------------------------------------

TaskPanelHost::TaskPanelHost(TaskView* taskView, MDIView* view)
    : QWidget(nullptr)
    , _taskView(taskView)
    , _view(view)
{
    setObjectName(QStringLiteral("taskPanelHost"));
    setAutoFillBackground(true);

    _layout = new QVBoxLayout(this);
    // The frame paintEvent draws
    _layout->setContentsMargins(1, 1, 1, 1);
    _layout->setSpacing(0);

    _header = new HostHeader(this);
    auto row = new QHBoxLayout(_header);
    row->setContentsMargins(6, 2, 2, 2);
    row->setSpacing(2);
    _title = new QLabel(_header);
    _title->setObjectName(QStringLiteral("taskPanelHostTitle"));
    // The header is the grip: a press on the title is a press on it. And
    // a long title is cut short rather than widening the host.
    _title->setAttribute(Qt::WA_TransparentForMouseEvents);
    _title->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    row->addWidget(_title, 1);

    _toCombo = new QToolButton(_header);
    _toCombo->setObjectName(QStringLiteral("taskPanelHostToCombo"));
    _toCombo->setAutoRaise(true);
    _toCombo->setCursor(Qt::ArrowCursor);
    _toCombo->setFocusPolicy(Qt::NoFocus);
    _toCombo->setIcon(BitmapFactory().pixmap("qss:overlay/taskhost.svg"));
    _toCombo->setToolTip(tr("Show this task panel in the combo view"));
    row->addWidget(_toCombo);

    _layout->addWidget(_header);

    // The button acts on its own panel and view (sec 15.1): the view keeps
    // the place, the task view hears it change, moves this page and lets
    // the host go.
    connect(_toCombo, &QToolButton::clicked, this, [this] {
        if (MDIView* own = _view) {
            TaskPlacement::setPlace(own, TaskPlacement::Place::ComboView);
        }
    });

    // Left or right for now: the other two sides come with the looks that
    // can stand there (sec 15.7)
    _right = TaskPlacement::side(view) == TaskPlacement::Side::Right;

    hosts.push_back(this);
    hostCount.store(static_cast<int>(hosts.size()), std::memory_order_relaxed);

    if (view) {
        view->installEventFilter(this);
    }
    attach();
}

TaskPanelHost::~TaskPanelHost()
{
    hosts.erase(std::remove(hosts.begin(), hosts.end(), this), hosts.end());
    hostCount.store(static_cast<int>(hosts.size()), std::memory_order_relaxed);

    // Still holding a page: the place was destroyed, and this host with
    // it, as its child. The page must not go the same way -- its dialog
    // owns the widgets in it and deletes them itself -- so it is handed
    // back while this object is still whole (~QWidget, which deletes the
    // children, runs after this body).
    if (TaskPage* page = takePage()) {
        if (_taskView) {
            _taskView->hostGone(this, page);
        }
        else {
            page->setParent(nullptr);
        }
    }
}

TaskPanelHost* TaskPanelHost::hostIn(const QWidget* place)
{
    for (TaskPanelHost* host : hosts) {
        if (host->parentWidget() == place) {
            return host;
        }
    }
    return nullptr;
}

TaskPanelHost* TaskPanelHost::hostAround(const QObject* object)
{
    for (const QObject* o = object; o; o = o->parent()) {
        if (!o->isWidgetType()) {
            continue;
        }
        for (TaskPanelHost* host : hosts) {
            if (host == o) {
                return host;
            }
        }
    }
    return nullptr;
}

bool TaskPanelHost::any()
{
    return hostCount.load(std::memory_order_relaxed) > 0;
}

Gui::MDIView* TaskPanelHost::view() const
{
    return _view;
}

void TaskPanelHost::attach()
{
    MDIView* view = _view;
    QWidget* where = (view && !_retired) ? placeFor(view) : nullptr;
    if (!where) {
        return;
    }
    if (where != _place) {
        if (_place && _place != view) {
            _place->removeEventFilter(this);
        }
        _place = where;
        if (where != view) {
            where->installEventFilter(this);
        }
        setParent(where);
    }
    place();
    show();
    raise();
}

void TaskPanelHost::setPage(TaskPage* page)
{
    if (_page == page) {
        return;
    }
    if (_page) {
        _page->panel->removeEventFilter(this);
        _layout->removeWidget(_page);
    }
    _page = page;
    if (page) {
        // What the panel holds changes under the host: a box folded or
        // opened, a widget shown. The host is as tall as it asks.
        page->panel->installEventFilter(this);
        // A page is as narrow as its host here, and scrolls what does not
        // fit: the least width it was given was the dock's
        page->scrollarea->setMinimumWidth(qMin(page->scrollarea->minimumWidth(), MinWidth - 8));
        page->setParent(this);
        _layout->addWidget(page, 1);
    }
    place();
}

TaskPage* TaskPanelHost::takePage()
{
    TaskPage* page = _page;
    _page = nullptr;
    if (page) {
        page->panel->removeEventFilter(this);
        _layout->removeWidget(page);
        page->hide();
    }
    return page;
}

TaskPage* TaskPanelHost::release()
{
    TaskPage* page = takePage();
    _retired = true;
    hide();
    deleteLater();
    return page;
}

void TaskPanelHost::setTitle(const QString& title)
{
    _title->setText(title);
    _title->setToolTip(title);
}

void TaskPanelHost::sideChanged()
{
    _right = TaskPlacement::side(_view) == TaskPlacement::Side::Right;
    place();
}

void TaskPanelHost::setFillsHeight(bool fill)
{
    _fill = fill;
    place();
}

int TaskPanelHost::pageHeight() const
{
    int height = 0;
    for (int i = 0; i < _page->layout->count(); ++i) {
        QWidget* w = _page->layout->itemAt(i)->widget();
        if (!w || w->isHidden()) {
            continue;
        }
        if (w == _page->scrollarea) {
            height += _page->panel->sizeHint().height() + 2 * _page->scrollarea->frameWidth();
        }
        else {
            height += w->sizeHint().height();
        }
    }
    // And room for the scroll bar a narrow host brings out
    return height + style()->pixelMetric(QStyle::PM_ScrollBarExtent);
}

void TaskPanelHost::placeLater()
{
    if (_placePending) {
        return;
    }
    _placePending = true;
    QMetaObject::invokeMethod(
        this,
        [this] {
            _placePending = false;
            place();
        },
        Qt::QueuedConnection);
}

void TaskPanelHost::place()
{
    QWidget* in = parentWidget();
    if (!in || _dragging || _retired) {
        return;
    }
    const QRect room = in->rect().adjusted(Margin, TopMargin, -Margin, -BottomMargin);
    if (room.width() <= 0 || room.height() <= 0) {
        return;
    }
    const int header = _header->sizeHint().height() + 2;
    // As wide as the panel asks for, within a third of the place. What
    // does not fit is scrolled, as it is in a narrow dock.
    int want = MinWidth;
    if (_page) {
        want = _page->panel->sizeHint().width()
            + style()->pixelMetric(QStyle::PM_ScrollBarExtent) + 6;
    }
    int width = qBound(MinWidth, want, qMax(MinWidth, room.width() / 3));
    width = qMin(width, room.width());
    // A place too small for a panel keeps the header alone, rather than
    // have the panel cover the view.
    const bool small = room.width() < MinWidth * 3 / 2 || room.height() < MinHeight;
    const bool folded = small;
    if (_page) {
        _page->setVisible(!folded);
    }
    int height = room.height();
    if (folded) {
        height = qMin(header, height);
    }
    else if (!_fill && _page) {
        // As tall as the panel needs, and no taller
        height = qMin(height, qMax(MinHeight, header + pageHeight()));
    }
    const int x = _right ? room.right() + 1 - width : room.left();
    setGeometry(x, room.top(), width, height);
    raise();
}

void TaskPanelHost::dragTo(int x)
{
    QWidget* in = parentWidget();
    if (!in) {
        return;
    }
    _dragging = true;
    const int most = qMax(Margin, in->width() - Margin - width());
    move(qBound(Margin, x, most), y());
}

void TaskPanelHost::dragEnded()
{
    _dragging = false;
    QWidget* in = parentWidget();
    const bool right = in && geometry().center().x() > in->width() / 2;
    if (MDIView* view = _view) {
        // Kept in the view, and what a view with no side of its own starts
        // from. The task view hears it and tells this host, when it changed
        TaskPlacement::setSide(view, right ? TaskPlacement::Side::Right : TaskPlacement::Side::Left);
    }
    // Settled where the view says, also when that is where it was
    sideChanged();
}

bool TaskPanelHost::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == _place.data() && event->type() == QEvent::Resize) {
        place();
    }
    else if (_page && watched == _page->panel && event->type() == QEvent::LayoutRequest) {
        // Not from inside the panel's own layout pass
        placeLater();
    }
    else if (watched == _view.data() && event->type() == QEvent::ParentChange) {
        // The view went into a cell, to another, or out on its own. Follow
        // it once it stands where it is going: this arrives from inside
        // the move.
        QMetaObject::invokeMethod(this, &TaskPanelHost::attach, Qt::QueuedConnection);
    }
    return QWidget::eventFilter(watched, event);
}

bool TaskPanelHost::event(QEvent* event)
{
    if (event->type() == QEvent::ShortcutOverride) {
        TaskView::acceptEditingKeys(event);
    }
    return QWidget::event(event);
}

void TaskPanelHost::keyPressEvent(QKeyEvent* event)
{
    // Enter and Escape are for the dialog of THIS page, whichever view is
    // the active one (sec 5.2). Any other key typed in a panel ends here,
    // as it does in the task view.
    if (_taskView && _page) {
        _taskView->pageKeyPress(_page, event);
        return;
    }
    QWidget::keyPressEvent(event);
}

void TaskPanelHost::paintEvent(QPaintEvent* event)
{
    QWidget::paintEvent(event);
    QPainter p(this);
    p.setPen(palette().color(QPalette::Mid));
    p.drawRect(rect().adjusted(0, 0, -1, -1));
}

// ----------------------------------------------------------------------------

TaskPageEventScope::TaskPageEventScope(QObject* receiver, QEvent* event)
{
    // Every event of the application comes through here: one test of a
    // counter while no view hosts a page
    if (hostCount.load(std::memory_order_relaxed) > 0) {
        open(receiver, event);
    }
}

TaskPageEventScope::~TaskPageEventScope() = default;

void TaskPageEventScope::open(QObject* receiver, QEvent* event)
{
    // What can run a panel's own code. Painting and layout cannot, and
    // they are most of what a widget is sent.
    switch (event->type()) {
        case QEvent::MouseButtonPress:
        case QEvent::MouseButtonRelease:
        case QEvent::MouseButtonDblClick:
        case QEvent::MouseMove:
        case QEvent::Wheel:
        case QEvent::KeyPress:
        case QEvent::KeyRelease:
        case QEvent::ShortcutOverride:
        case QEvent::Shortcut:
        case QEvent::FocusIn:
        case QEvent::FocusOut:
        case QEvent::Enter:
        case QEvent::Leave:
        case QEvent::HoverEnter:
        case QEvent::HoverLeave:
        case QEvent::HoverMove:
        case QEvent::ContextMenu:
        case QEvent::DragEnter:
        case QEvent::DragMove:
        case QEvent::DragLeave:
        case QEvent::Drop:
        case QEvent::InputMethod:
        case QEvent::Close:
        case QEvent::MetaCall:
        case QEvent::Timer:
            break;
        default:
            return;
    }
    // Every thread with an event loop delivers through the application
    // object; the hosts are the GUI thread's.
    if (QThread::currentThread() != qApp->thread()) {
        return;
    }
    TaskPanelHost* host = TaskPanelHost::hostAround(receiver);
    MDIView* view = host ? host->view() : nullptr;
    if (!view) {
        return;
    }
    MainWindow* mw = getMainWindow();
    if (!mw) {
        return;
    }
    // A click into a panel makes its view the active one first, which is
    // what activates the dialog (sec 5.2). Whether the widget clicked
    // takes the focus or not.
    if (event->type() == QEvent::MouseButtonPress && mw->activeWindow() != view) {
        activate(view);
    }
    if (mw->activeWindow() == view) {
        // The view's own events: it is the one being worked in already
        return;
    }
    if (auto view3d = qobject_cast<View3DInventor*>(view)) {
        ViewerContext* context = view3d->getViewer();
        if (context && ViewerContext::current() != context) {
            viewer = std::make_unique<ViewerScope>(context);
        }
    }
    else if (SelectionSingleton* own = view->selectionInstance()) {
        selection = std::make_unique<SelectionScope>(*own);
    }
}

#include "moc_TaskPanelHost.cpp"
