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
#include <QActionGroup>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollArea>
#include <QScrollBar>
#include <QSplitter>
#include <QStyle>
#include <QStyleOption>
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
#include <Gui/OverlayManager.h>
#include <Gui/OverlayWidgets.h>
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
/// The grip along the inner edge of an overlay (Gui::OverlaySizeGrip)
constexpr int GripSize = 6;

/// What of its place an overlay may stand in: clear of the place's own
/// chrome, and of the docks laid over the edges of the window
/// (docs/TaskPanelPerView.md sec 5.3). A dock overlay is a strip along
/// an edge; in a place it reaches it lies along one of the place's edges
/// too, and the room ends where the strip does. For a strip that touches
/// two edges of the place -- a corner of it -- the edge it reaches in from
/// the least.
QRect roomIn(const QWidget* place)
{
    QRect room = place->rect().adjusted(Margin, TopMargin, -Margin, -BottomMargin);
    const QRect all = place->rect();
    for (const QRect& taken : Gui::OverlayManager::instance()->occupied(place)) {
        const bool atLeft = taken.left() <= all.left() + 1;
        const bool atRight = taken.right() >= all.right() - 1;
        const bool atTop = taken.top() <= all.top() + 1;
        const bool atBottom = taken.bottom() >= all.bottom() - 1;
        // How far in from each edge it touches; far beyond any for one it
        // does not
        const int none = all.width() + all.height();
        int depth[4] = {atLeft ? taken.right() + 1 - all.left() : none,
                        atRight ? all.right() + 1 - taken.left() : none,
                        atTop ? taken.bottom() + 1 - all.top() : none,
                        atBottom ? all.bottom() + 1 - taken.top() : none};
        // A strip from edge to edge lies along the edge between them
        if (atLeft && atRight) {
            depth[0] = depth[1] = none;
        }
        if (atTop && atBottom) {
            depth[2] = depth[3] = none;
        }
        int edge = 0;
        for (int i = 1; i < 4; ++i) {
            if (depth[i] < depth[edge]) {
                edge = i;
            }
        }
        if (depth[edge] >= none) {
            continue;
        }
        switch (edge) {
            case 0:
                room.setLeft(qMax(room.left(), taken.right() + 1));
                break;
            case 1:
                room.setRight(qMin(room.right(), taken.left() - 1));
                break;
            case 2:
                room.setTop(qMax(room.top(), taken.bottom() + 1));
                break;
            default:
                room.setBottom(qMin(room.bottom(), taken.top() - 1));
                break;
        }
    }
    return room;
}

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

/// What the document of a view that is no 3D view was saved with for it
/// (Gui::Document::savedViewTaskState); null for a 3D view, whose own
/// properties are saved with it
const Gui::Document::ViewTaskState* savedState(const Gui::MDIView* view)
{
    if (!view || view->isDerivedFrom(Gui::View3DInventor::getClassTypeId())) {
        return nullptr;
    }
    Gui::Document* doc = view->getGuiDocument();
    return doc ? doc->savedViewTaskState(view) : nullptr;
}

/// The text \a view holds under \a name; empty when it holds none. A
/// property of its own, even an emptied one, is what it holds; without
/// one, what its document was saved with.
std::string ownText(const Gui::MDIView* view, const char* name)
{
    if (!view) {
        return {};
    }
    if (auto prop = Base::freecad_dynamic_cast<App::PropertyString>(view->getPropertyByName(name))) {
        return prop->getStrValue();
    }
    if (const Gui::Document::ViewTaskState* saved = savedState(view)) {
        if (std::strcmp(name, PropPlace) == 0) {
            return saved->place;
        }
        if (std::strcmp(name, PropMode) == 0) {
            return saved->mode;
        }
        if (std::strcmp(name, PropSide) == 0) {
            return saved->side;
        }
    }
    return {};
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
        // Somebody else's property of that name; or nothing to keep, and
        // nothing saved for the view that an emptied property would have
        // to stand in front of
        if (prop || (value.empty() && ownText(view, name).empty())) {
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

Qt::Edge edgeOf(TaskPlacement::Side side)
{
    switch (side) {
        case TaskPlacement::Side::Right:
            return Qt::RightEdge;
        case TaskPlacement::Side::Top:
            return Qt::TopEdge;
        case TaskPlacement::Side::Bottom:
            return Qt::BottomEdge;
        default:
            return Qt::LeftEdge;
    }
}

bool acrossIsWidth(TaskPlacement::Side side)
{
    return side == TaskPlacement::Side::Left || side == TaskPlacement::Side::Right;
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

/// The host's title bar: the title, the buttons, and the grip the host is
/// dragged by. The dock overlay's title bar by class, which is what its
/// style sheets style (Gui--OverlayTitleBar); its handling of the mouse
/// drags docks and is not used.
class HostHeader: public Gui::OverlayTitleBar
{
public:
    explicit HostHeader(TaskPanelHost* host)
        : Gui::OverlayTitleBar(host)
        , host(host)
    {
        // No Q_OBJECT here (the class lives in this .cpp), so the object
        // name is what tests can find it by.
        setObjectName(QStringLiteral("taskPanelHostHeader"));
        // Not the keyboard: Enter and Escape typed with it here would be
        // the dialog's
        setFocusPolicy(Qt::NoFocus);
        setCursor(Qt::OpenHandCursor);
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        // The ground a style sheet gives a title bar; the title is the
        // label's to draw
        QStyleOption opt;
        opt.initFrom(this);
        QPainter p(this);
        style()->drawPrimitive(QStyle::PE_Widget, &opt, &p, this);
    }
    void keyPressEvent(QKeyEvent* ev) override
    {
        ev->ignore();
    }
    void timerEvent(QTimerEvent*) override
    {}
    void mousePressEvent(QMouseEvent* ev) override
    {
        // Beside its view it is moved by choosing a side, not dragged
        if (ev->button() != Qt::LeftButton || host->isBeside()) {
            QWidget::mousePressEvent(ev);
            return;
        }
        pressed = true;
        dragging = false;
        pressAt = ev->globalPosition().toPoint();
        startAt = host->pos();
        ev->accept();
    }
    void mouseMoveEvent(QMouseEvent* ev) override
    {
        if (!pressed || !(ev->buttons() & Qt::LeftButton)) {
            QWidget::mouseMoveEvent(ev);
            return;
        }
        const QPoint moved = ev->globalPosition().toPoint() - pressAt;
        if (!dragging && moved.manhattanLength() < QApplication::startDragDistance()) {
            return;
        }
        dragging = true;
        host->dragTo(startAt + moved);
    }
    void mouseReleaseEvent(QMouseEvent* ev) override
    {
        const bool dragged = pressed && dragging;
        pressed = dragging = false;
        if (dragged) {
            const QPoint at = ev->globalPosition().toPoint();
            host->dragEnded(at, at - pressAt);
        }
        ev->accept();
    }

private:
    TaskPanelHost* host;
    bool pressed {false};
    bool dragging {false};
    QPoint pressAt;
    QPoint startAt;
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
    // A size is across the panel: one chosen for a panel beside the
    // picture is not one for a panel above it. Turned from the one to the
    // other, the view gives its size up and the panel asks again.
    if (view && acrossIsWidth(TaskPlacement::side(view)) != acrossIsWidth(side)
        && size(view) > 0) {
        setSize(view, 0);
    }
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
    if (auto prop = Base::freecad_dynamic_cast<App::PropertyInteger>(
            view->getPropertyByName(PropSize))) {
        return static_cast<int>(prop->getValue());
    }
    const Gui::Document::ViewTaskState* saved = savedState(view);
    return saved ? static_cast<int>(saved->size) : 0;
}

void TaskPlacement::setSize(MDIView* view, int size)
{
    if (!view) {
        return;
    }
    App::Property* prop = view->getPropertyByName(PropSize);
    auto number = Base::freecad_dynamic_cast<App::PropertyInteger>(prop);
    if (!number) {
        if (prop || (size <= 0 && TaskPlacement::size(view) <= 0)) {
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

void TaskPlacement::ownState(const MDIView* view,
                             std::string& place,
                             std::string& mode,
                             std::string& side,
                             long& size)
{
    place = ownText(view, PropPlace);
    mode = ownText(view, PropMode);
    side = ownText(view, PropSide);
    size = TaskPlacement::size(view);
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
    _layout->setContentsMargins(1, 1, 1, 1);
    _layout->setSpacing(0);

    _header = new HostHeader(this);
    auto row = new QHBoxLayout(_header);
    row->setContentsMargins(6, 1, 1, 1);
    row->setSpacing(1);
    _title = new QLabel(_header);
    _title->setObjectName(QStringLiteral("taskPanelHostTitle"));
    // The title bar is the grip: a press on the title is a press on it.
    // And a long title is cut short rather than widening the host.
    _title->setAttribute(Qt::WA_TransparentForMouseEvents);
    _title->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    row->addWidget(_title, 1);

    // The buttons of an overlaid dock's title bar, by class and by make
    // (OverlayTabWidget::createTitleButton names one after its action's
    // data)
    const int buttonSize = qMax(18, fontMetrics().height() + 4);

    // How this panel stands in its view, and on which side: its own
    // view's, kept there (TaskPlacement)
    auto menu = new QMenu(this);
    auto modes = new QActionGroup(menu);
    _actOverlay = menu->addAction(tr("Over the view"));
    _actOverlay->setObjectName(QStringLiteral("taskPanelHostOverlay"));
    _actBeside = menu->addAction(tr("Beside the view"));
    _actBeside->setObjectName(QStringLiteral("taskPanelHostBeside"));
    for (QAction* action : {_actOverlay, _actBeside}) {
        action->setCheckable(true);
        modes->addAction(action);
    }
    menu->addSeparator();
    auto sides = new QActionGroup(menu);
    const struct
    {
        TaskPlacement::Side side;
        QString text;
        const char* name;
    } entries[4] = {
        {TaskPlacement::Side::Left, tr("Left"), "taskPanelHostLeft"},
        {TaskPlacement::Side::Right, tr("Right"), "taskPanelHostRight"},
        {TaskPlacement::Side::Top, tr("Top"), "taskPanelHostTop"},
        {TaskPlacement::Side::Bottom, tr("Bottom"), "taskPanelHostBottom"},
    };
    for (int i = 0; i < 4; ++i) {
        QAction* action = menu->addAction(entries[i].text);
        action->setObjectName(QString::fromLatin1(entries[i].name));
        action->setCheckable(true);
        sides->addAction(action);
        _actSides[i] = action;
        const TaskPlacement::Side side = entries[i].side;
        connect(action, &QAction::triggered, this, [this, side] {
            if (MDIView* own = _view) {
                TaskPlacement::setSide(own, side);
            }
            // Also when the view said that already: the menu shows it
            placementChanged();
        });
    }
    connect(_actOverlay, &QAction::triggered, this, [this] {
        if (MDIView* own = _view) {
            TaskPlacement::setMode(own, TaskPlacement::Mode::Overlay);
        }
        placementChanged();
    });
    connect(_actBeside, &QAction::triggered, this, [this] {
        if (MDIView* own = _view) {
            TaskPlacement::setMode(own, TaskPlacement::Mode::Side);
        }
        placementChanged();
    });
    auto actMenu = new QAction(this);
    actMenu->setData(QStringLiteral("taskPanelHostMenu"));
    actMenu->setIcon(BitmapFactory().pixmap("qss:overlay/mode.svg"));
    actMenu->setToolTip(tr("Where this task panel stands in its view"));
    _menu = static_cast<QToolButton*>(OverlayTabWidget::createTitleButton(actMenu, buttonSize));
    _menu->setFocusPolicy(Qt::NoFocus);
    _menu->setMenu(menu);
    _menu->setPopupMode(QToolButton::InstantPopup);
    _menu->setStyleSheet(QStringLiteral("QToolButton::menu-indicator { image: none; }"));
    row->addWidget(_menu);

    // The button acts on its own panel and view (sec 15.1): the view keeps
    // the place, the task view hears it change, moves this page and lets
    // the host go.
    auto actToCombo = new QAction(this);
    actToCombo->setData(QStringLiteral("taskPanelHostToCombo"));
    actToCombo->setIcon(BitmapFactory().pixmap("qss:overlay/taskhost.svg"));
    actToCombo->setToolTip(tr("Show this task panel in the combo view"));
    _toCombo = static_cast<QToolButton*>(
        OverlayTabWidget::createTitleButton(actToCombo, buttonSize));
    _toCombo->setFocusPolicy(Qt::NoFocus);
    row->addWidget(_toCombo);
    connect(actToCombo, &QAction::triggered, this, [this] {
        if (MDIView* own = _view) {
            TaskPlacement::setPlace(own, TaskPlacement::Place::ComboView);
        }
    });

    _layout->addWidget(_header);

    // As the view has it now, and until the view changes it: what is
    // chosen in another view meanwhile is for the panels opened after
    _mode = TaskPlacement::mode(view);
    _side = TaskPlacement::side(view);
    updateMenu();

    hosts.push_back(this);
    hostCount.store(static_cast<int>(hosts.size()), std::memory_order_relaxed);

    // The docks laid over the window were laid out again: the room an
    // overlay has between them may have changed (roomIn)
    connect(OverlayManager::instance(),
            &OverlayManager::layoutChanged,
            this,
            &TaskPanelHost::placeLater);

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

bool TaskPanelHost::isBeside() const
{
    return !_besideCell.isNull();
}

int TaskPanelHost::wantedExtent() const
{
    if (!_page) {
        return 0;
    }
    if (acrossIsWidth(_side)) {
        return qMax(MinWidth,
                    _page->panel->sizeHint().width()
                        + style()->pixelMetric(QStyle::PM_ScrollBarExtent) + 6 + GripSize);
    }
    return _header->sizeHint().height() + 2 + pageHeight() + GripSize;
}

QWidget* TaskPanelHost::besidePlace(MDIView* view)
{
    ViewArea* area = ViewArea::areaOf(view);
    if (!area) {
        // A view in a tab of its own: a panel beside it is a split, and a
        // split puts such a view into a view area (sec 15.8, point 1).
        // Null for a view that is in no tab either -- a floating one.
        area = ViewArea::wrap(view);
    }
    ViewAreaCell* cell = area ? area->cellOf(view) : nullptr;
    if (!cell) {
        return nullptr;
    }
    int extent = TaskPlacement::size(view);
    if (extent <= 0 && !area->panelCellOf(cell)) {
        extent = wantedExtent();
        // Made before the page is there: fitted to it when it comes
        _fitBeside = extent <= 0;
    }
    return area->panelCell(cell, edgeOf(_side), extent);
}

void TaskPanelHost::attach()
{
    MDIView* view = _view;
    if (!view || _retired) {
        return;
    }
    QPointer<ViewAreaCell> hadCell = _besideCell;
    QWidget* where = nullptr;
    ViewAreaCell* cell = nullptr;
    if (_mode == TaskPlacement::Mode::Side) {
        where = besidePlace(view);
        if (auto panel = qobject_cast<ViewAreaPanelCell*>(where)) {
            cell = panel->cell();
        }
    }
    if (!where) {
        where = placeFor(view);
    }
    // The keyboard, when it is in the panel, goes with the panel. Left to
    // itself it would be dropped as the host changes parents and picked
    // up by whatever Qt finds next -- a neighbouring view, which would
    // then be the active one, and this panel's dialog deactivated for a
    // panel that only changed its place. So it waits in the panel's own
    // view while the host moves.
    QPointer<QWidget> focus;
    if (where != _place) {
        focus = QApplication::focusWidget();
        if (focus && !(focus == this || isAncestorOf(focus))) {
            focus = nullptr;
        }
        if (focus) {
            view->setFocus();
        }
    }
    if (where != _place) {
        if (_place && _place != view) {
            _place->removeEventFilter(this);
        }
        _place = where;
        if (cell) {
            // The one thing in the panel cell, and as large as it
            setParent(where);
            where->layout()->addWidget(this);
            if (auto pair = qobject_cast<QSplitter*>(where->parentWidget())) {
                connect(pair,
                        &QSplitter::splitterMoved,
                        this,
                        &TaskPanelHost::pairMoved,
                        Qt::UniqueConnection);
            }
        }
        else {
            if (where != view) {
                where->installEventFilter(this);
            }
            setParent(where);
        }
    }
    _besideCell = cell;
    // Out of the panel cell it stood in before, that cell goes: after the
    // host has left it, or the host would go with it
    if (hadCell && hadCell != cell) {
        if (ViewArea* area = hadCell->area()) {
            area->removePanelCell(hadCell);
        }
    }
    applyLook();
    place();
    show();
    raise();
    if (focus && focus != QApplication::focusWidget()) {
        focus->setFocus();
    }
}

void TaskPanelHost::applyLook()
{
    const bool overlay = !_besideCell;
    if (!_lookSet || overlay != _overlayLook) {
        _lookSet = true;
        _overlayLook = overlay;
        // Over the picture, an overlaid dock's look (sec 15.5): its style
        // sheet, and no ground of the host's own. In a cell of its own, a
        // plain widget's.
        setAutoFillBackground(!overlay);
        setAttribute(Qt::WA_NoSystemBackground, overlay);
        setAttribute(Qt::WA_TranslucentBackground, overlay);
        setStyleSheet(overlay ? OverlayManager::instance()->getStyleSheet() : QString());
    }
    if (_page) {
        OverlayTabWidget::applyOverlayLook(_page, overlay);
    }
    updateGrip();
    if (!overlay) {
        clearMask();
    }
}

void TaskPanelHost::updateGrip()
{
    const bool overlay = _overlayLook && !_retired;
    const bool down = acrossIsWidth(_side);
    if (_grip && _gripDown != down) {
        _grip->hide();
        _grip->deleteLater();
        _grip = nullptr;
    }
    if (overlay && !_grip) {
        // The dock overlay's own grip, which only says where it is dragged
        auto grip = new OverlaySizeGrip(this, !down);
        grip->setObjectName(QStringLiteral("taskPanelHostGrip"));
        grip->installEventFilter(this);
        connect(grip, &OverlaySizeGrip::dragMove, this, &TaskPanelHost::gripMoved);
        _grip = grip;
        _gripDown = down;
    }
    if (_grip) {
        _grip->setVisible(overlay);
    }
    // The layout leaves the grip its edge; a host with a ground of its
    // own leaves room for the frame paintEvent draws
    int left = 1;
    int top = 1;
    int right = 1;
    int bottom = 1;
    if (overlay) {
        left = top = right = bottom = 0;
        switch (_side) {
            case TaskPlacement::Side::Right:
                left = GripSize;
                break;
            case TaskPlacement::Side::Top:
                bottom = GripSize;
                break;
            case TaskPlacement::Side::Bottom:
                top = GripSize;
                break;
            default:
                right = GripSize;
                break;
        }
    }
    _layout->setContentsMargins(left, top, right, bottom);
    placeGrip();
}

void TaskPanelHost::placeGrip()
{
    if (!_grip) {
        return;
    }
    switch (_side) {
        case TaskPlacement::Side::Right:
            _grip->setGeometry(0, 0, GripSize, height());
            break;
        case TaskPlacement::Side::Top:
            _grip->setGeometry(0, height() - GripSize, width(), GripSize);
            break;
        case TaskPlacement::Side::Bottom:
            _grip->setGeometry(0, 0, width(), GripSize);
            break;
        default:
            _grip->setGeometry(width() - GripSize, 0, GripSize, height());
            break;
    }
    _grip->raise();
}

void TaskPanelHost::gripMoved(const QPoint& globalPos)
{
    QWidget* in = parentWidget();
    if (!in || !_overlayLook || _retired) {
        return;
    }
    const QRect room = roomIn(in);
    const QPoint at = in->mapFromGlobal(globalPos);
    switch (_side) {
        case TaskPlacement::Side::Right:
            _extent = room.right() + 1 - at.x();
            break;
        case TaskPlacement::Side::Top:
            _extent = at.y() - room.top();
            break;
        case TaskPlacement::Side::Bottom:
            _extent = room.bottom() + 1 - at.y();
            break;
        default:
            _extent = at.x() - room.left();
            break;
    }
    _extent = qMax(1, _extent);
    place();
}

void TaskPanelHost::updateMask()
{
    if (!_overlayLook || _retired) {
        clearMask();
        return;
    }
    // What has a ground: the title bar, the grip, and in the page the
    // boxes of the panel and what stands beside the scrolling. Between
    // and around them there is nothing, to the eye or to the pointer: a
    // press or a turn of the wheel there goes to the view beneath.
    const QPoint origin(0, 0);
    QRegion region(_header->geometry());
    if (_grip && !_grip->isHidden()) {
        region += _grip->geometry();
    }
    if (_page && !_page->isHidden()) {
        for (int i = 0; i < _page->layout->count(); ++i) {
            QWidget* w = _page->layout->itemAt(i)->widget();
            if (!w || w->isHidden()) {
                continue;
            }
            if (w != _page->scrollarea) {
                region += QRect(w->mapTo(this, origin), w->size());
                continue;
            }
            QWidget* port = _page->scrollarea->viewport();
            const QRect clip(port->mapTo(this, origin), port->size());
            for (QObject* child : _page->panel->children()) {
                auto box = qobject_cast<QWidget*>(child);
                if (!box || box->isHidden()) {
                    continue;
                }
                const QRect rect(box->mapTo(this, origin), box->size());
                region += rect.adjusted(-1, -1, 1, 1).intersected(clip);
            }
            for (QScrollBar* bar : {_page->scrollarea->verticalScrollBar(),
                                    _page->scrollarea->horizontalScrollBar()}) {
                if (bar && bar->isVisible()) {
                    region += QRect(bar->mapTo(this, origin), bar->size());
                }
            }
        }
    }
    setMask(region);
}

void TaskPanelHost::pairMoved()
{
    MDIView* view = _view;
    QWidget* panel = _besideCell ? parentWidget() : nullptr;
    if (!view || !panel || _retired) {
        return;
    }
    // The view keeps it: its next panel is given the same
    TaskPlacement::setSize(view, acrossIsWidth(_side) ? panel->width() : panel->height());
}

void TaskPanelHost::updateMenu()
{
    _actOverlay->setChecked(_mode == TaskPlacement::Mode::Overlay);
    _actBeside->setChecked(_mode == TaskPlacement::Mode::Side);
    const TaskPlacement::Side order[4] = {TaskPlacement::Side::Left,
                                          TaskPlacement::Side::Right,
                                          TaskPlacement::Side::Top,
                                          TaskPlacement::Side::Bottom};
    for (int i = 0; i < 4; ++i) {
        _actSides[i]->setChecked(order[i] == _side);
    }
}

void TaskPanelHost::setPage(TaskPage* page)
{
    if (_page == page) {
        return;
    }
    if (_page) {
        _page->panel->removeEventFilter(this);
        disconnect(_page->scrollarea->verticalScrollBar(), nullptr, this, nullptr);
        OverlayTabWidget::applyOverlayLook(_page, false);
        _layout->removeWidget(_page);
    }
    _page = page;
    if (page) {
        // What the panel holds changes under the host: a box folded or
        // opened, a widget shown, the panel scrolled. What of the host is
        // widget follows it (updateMask).
        page->panel->installEventFilter(this);
        connect(page->scrollarea->verticalScrollBar(),
                &QScrollBar::valueChanged,
                this,
                &TaskPanelHost::placeLater);
        // A page is as narrow as its host here, and scrolls what does not
        // fit: the least width it was given was the dock's
        page->scrollarea->setMinimumWidth(qMin(page->scrollarea->minimumWidth(), MinWidth - 8));
        page->setParent(this);
        _layout->addWidget(page, 1);
        if (_fitBeside && _besideCell) {
            // The panel cell was made with a third of the slot, there
            // being no page to ask: what the page asks for, now
            _fitBeside = false;
            if (ViewArea* area = _besideCell->area()) {
                area->panelCell(_besideCell, edgeOf(_side), wantedExtent());
            }
        }
    }
    applyLook();
    place();
}

TaskPage* TaskPanelHost::takePage()
{
    TaskPage* page = _page;
    _page = nullptr;
    if (page) {
        page->panel->removeEventFilter(this);
        disconnect(page->scrollarea->verticalScrollBar(), nullptr, this, nullptr);
        // As it was before it came here: where it goes next it is a plain
        // page again
        OverlayTabWidget::applyOverlayLook(page, false);
        _layout->removeWidget(page);
        page->hide();
    }
    return page;
}

TaskPage* TaskPanelHost::release()
{
    // The keyboard, when it is in the panel, goes home to the panel's view
    // rather than to whatever Qt finds next when the host is hidden
    // (attach() says why). The task view puts it back into a page it
    // moves; after a dialog that closed, the view is where it belongs.
    if (MDIView* view = _view) {
        QWidget* focus = QApplication::focusWidget();
        if (focus && (focus == this || isAncestorOf(focus))) {
            view->setFocus();
        }
    }
    TaskPage* page = takePage();
    _retired = true;
    hide();
    if (QPointer<ViewAreaCell> cell = _besideCell) {
        // The cell of its own goes with it, once the host is out of it
        _besideCell = nullptr;
        setParent(nullptr);
        if (ViewArea* area = cell->area()) {
            area->removePanelCell(cell);
        }
    }
    deleteLater();
    return page;
}

void TaskPanelHost::setTitle(const QString& title)
{
    _title->setText(title);
    _title->setToolTip(title);
}

void TaskPanelHost::placementChanged()
{
    MDIView* view = _view;
    if (!view || _retired) {
        return;
    }
    const TaskPlacement::Side was = _side;
    _mode = TaskPlacement::mode(view);
    _side = TaskPlacement::side(view);
    // The view's size, not what the grip was last dragged to
    _extent = 0;
    updateMenu();
    if (_besideCell && _mode == TaskPlacement::Mode::Side) {
        // The same panel cell, on its new side or at its new size: what
        // the view holds; with nothing held, what the cell has, or what
        // the panel asks for when it is turned from beside to above
        int extent = TaskPlacement::size(view);
        if (extent <= 0 && acrossIsWidth(was) != acrossIsWidth(_side)) {
            extent = wantedExtent();
        }
        if (ViewArea* area = _besideCell->area()) {
            area->panelCell(_besideCell, edgeOf(_side), extent);
        }
    }
    // Into the panel cell, or out of it over the picture
    attach();
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
    if (_besideCell) {
        // The panel cell's layout is this host's geometry, and the cell
        // is no smaller than a panel needs
        if (_page) {
            _page->setVisible(true);
        }
        return;
    }
    const QRect room = roomIn(in);
    if (room.width() <= 0 || room.height() <= 0) {
        return;
    }
    const int header = _header->sizeHint().height() + 2;
    // A place too small for a panel keeps the title bar alone, rather than
    // have the panel cover the view.
    const bool small = room.width() < MinWidth * 3 / 2 || room.height() < MinHeight;
    if (_page) {
        _page->setVisible(!small);
    }
    // Across: what the grip is being dragged to, else what the view holds,
    // else what the panel asks for -- that last within a third of the
    // place across the view and half of it down, which a size the user
    // gave is not held to.
    MDIView* view = _view;
    int extent = _extent;
    if (extent <= 0 && view) {
        extent = TaskPlacement::size(view);
    }
    const bool chosen = extent > 0;
    if (!chosen) {
        extent = wantedExtent();
    }
    QRect g;
    if (acrossIsWidth(_side)) {
        const int most = chosen ? qMax(MinWidth, room.width() - MinWidth / 2)
                                : qMax(MinWidth, room.width() / 3);
        extent = qMin(qBound(MinWidth, extent, most), room.width());
        // The whole length of its side, as a dock overlay is (sec 15.8)
        const int height = small ? qMin(header, room.height()) : room.height();
        const int x = isOnRight() ? room.right() + 1 - extent : room.left();
        g = QRect(x, room.top(), extent, height);
    }
    else {
        const int least = header + 40;
        const int most = chosen ? qMax(least, room.height() - MinHeight / 2)
                                : qMax(least, room.height() / 2);
        extent = small ? qMin(header, room.height())
                       : qMin(qBound(least, extent, most), room.height());
        const int y = _side == TaskPlacement::Side::Bottom ? room.bottom() + 1 - extent
                                                          : room.top();
        g = QRect(room.left(), y, room.width(), extent);
    }
    setGeometry(g);
    raise();
    placeGrip();
    updateMask();
}

void TaskPanelHost::dragTo(const QPoint& pos)
{
    QWidget* in = parentWidget();
    if (!in) {
        return;
    }
    _dragging = true;
    const int mostX = qMax(Margin, in->width() - Margin - width());
    const int mostY = qMax(Margin, in->height() - Margin - height());
    move(qBound(Margin, pos.x(), mostX), qBound(Margin, pos.y(), mostY));
}

void TaskPanelHost::dragEnded(const QPoint& globalPos, const QPoint& moved)
{
    _dragging = false;
    QWidget* in = parentWidget();
    TaskPlacement::Side side = _side;
    if (in) {
        const QPoint at = in->mapFromGlobal(globalPos);
        if (qAbs(moved.x()) >= qAbs(moved.y())) {
            side = at.x() > in->width() / 2 ? TaskPlacement::Side::Right
                                            : TaskPlacement::Side::Left;
        }
        else {
            side = at.y() > in->height() / 2 ? TaskPlacement::Side::Bottom
                                             : TaskPlacement::Side::Top;
        }
    }
    if (MDIView* view = _view) {
        // Kept in the view, and what a view with no side of its own starts
        // from. The task view hears it and tells this host, when it changed
        TaskPlacement::setSide(view, side);
    }
    // Settled where the view says, also when that is where it was
    placementChanged();
}

bool TaskPanelHost::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == _place.data() && event->type() == QEvent::Resize) {
        place();
    }
    else if (_page && watched == _page->panel
             && (event->type() == QEvent::LayoutRequest || event->type() == QEvent::Resize)) {
        // Not from inside the panel's own layout pass
        placeLater();
    }
    else if (_grip && watched == _grip && event->type() == QEvent::MouseButtonRelease) {
        // The grip let go: the view keeps the size, for this panel and
        // its next
        MDIView* view = _view;
        if (view && _extent > 0 && _overlayLook) {
            TaskPlacement::setSize(view, acrossIsWidth(_side) ? width() : height());
        }
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
    if (_overlayLook) {
        // Over the picture the host has no ground and no frame
        return;
    }
    QPainter p(this);
    p.setPen(palette().color(QPalette::Mid));
    p.drawRect(rect().adjusted(0, 0, -1, -1));
}

void TaskPanelHost::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    placeGrip();
    updateMask();
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
