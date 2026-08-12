// SPDX-FileCopyrightText: 2026 Benjamin Nauck
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "customtitlebarkit/FoldableMenuBar.h"

#include <QActionEvent>
#include <QHBoxLayout>
#include <QMenu>
#include <QMenuBar>
#include <QMouseEvent>
#include <QPointer>
#include <QPropertyAnimation>
#include <QTimer>

struct FoldableMenuBar::Impl {
    QHBoxLayout *layout = nullptr;
    QWidget *brandWidget = nullptr;
    QMenuBar *menuBar = nullptr;
    // Lives on the window rather than here, see ensureOverlayParent(), so it
    // can be taken down with the window before this widget is.
    QPointer<QWidget> menuContainer;
    QWidget *menuWrapper = nullptr;   // inside container, has border trick for centering
    QTimer *collapseTimer = nullptr;
    QPropertyAnimation *animation = nullptr;
    QList<QWidget *> maskTargets;
    bool foldable = false;
    bool overlayExpand = true;
    bool expanded = true;
    int revealWidth = 0;

    void updateClip() {
        if (!menuBar) return;
        if (!foldable || revealWidth >= menuBar->sizeHint().width()) {
            menuBar->clearMask();
        }
        else if (revealWidth <= 0) {
            menuBar->setMask(QRegion(-1, -1, 1, 1));
        }
        else {
            menuBar->setMask(QRegion(0, 0, revealWidth, menuBar->height()));
        }
    }

    /// Where the menu items begin, in the coordinates of the FoldableMenuBar:
    /// just right of the brand button, which is the only thing a folded bar
    /// shows.
    int menuLeft() const {
        return brandWidget ? brandWidget->geometry().right() + 1 + layout->spacing() : 0;
    }

    /*! LOCAL DIVERGENCE from FreeCAD/FreeCAD#26766: hold the menu bar at the
     * width one row of items needs, and park the folded container where the
     * unfolded one starts.
     *
     * Folding is a clip, not a teardown -- the QMenuBar stays visible to Qt,
     * so it keeps its mnemonics (Alt+F and friends) and its keyboard
     * navigation. Both of those ask the bar where an item is, and a bar the
     * zero-wide folded container has squeezed into a stack of clipped rows
     * answers with rectangles nowhere near the ones the eye expects. Held at
     * its natural width and positioned like the unfolded bar, a menu opened
     * by a mnemonic while folded lands exactly where the same menu lands once
     * the bar has unfolded around it.
     */
    void syncNaturalWidth() {
        if (!menuBar) return;
        if (!foldable) {
            menuBar->setMinimumWidth(0);
            menuBar->setMaximumWidth(QWIDGETSIZE_MAX);
            return;
        }
        // Fixed, not merely a floor: unfolding takes the container from no
        // width to the width of the row, and a menu bar that resizes with it
        // forgets which item is current -- which is the item the arrow keys
        // move from. Pinned, unfolding never resizes it.
        menuBar->setFixedWidth(menuBar->sizeHint().width());
        // QMenuBar re-grabs its mnemonic shortcuts when it recomputes its item
        // rectangles, and asking for one is the only public way to make it do
        // that. A folded bar is masked out of every repaint, so nothing else
        // would prompt it after a workbench swapped the menus.
        if (!menuBar->actions().isEmpty())
            menuBar->actionGeometry(menuBar->actions().constFirst());
    }

    /*! LOCAL DIVERGENCE from FreeCAD/FreeCAD#26766: the overlay is handed to
     * the window once and left there, rather than being reparented on every
     * unfold.
     *
     * It has to live on the window at all -- not inside this widget -- so the
     * menu items can spill out over the toolbars beside a brand button only a
     * few pixels wide. Doing that at unfold time hides the menu bar for an
     * instant, and a menu opened by a mnemonic dies with it: Alt+F used to
     * open the File menu and then close it again in the same breath. Parented
     * once, unfolding is nothing but a geometry change and a clip.
     */
    void ensureOverlayParent(FoldableMenuBar *self) {
        if (!overlayExpand || !menuContainer) return;
        QWidget *win = self->window();
        if (win && menuContainer->parentWidget() != win) {
            menuContainer->setParent(win);
            menuContainer->show();
        }
    }

    void collapseGeometry(FoldableMenuBar *self) {
        if (overlayExpand) {
            positionOverlay(self, 0);
        }
        else {
            menuContainer->setMaximumWidth(0);
        }
    }

    /*! Unfold on the next trip through the event loop.
     *
     * Every keyboard way in reaches this from inside Qt's own menu handling:
     * a mnemonic runs it from QMenu::popup(), before the menu is on screen,
     * and Alt runs it from the focus change that same popup causes. Unfolding
     * underneath either is enough for Qt to take the menu straight back down
     * -- Alt+F opened the File menu and closed it in the same breath. A hop
     * later the menu is up, and the unfold is just a geometry change beside
     * it.
     */
    void expandLater(FoldableMenuBar *self) {
        if (!foldable || expanded) return;
        collapseTimer->stop();
        QMetaObject::invokeMethod(self, [self]() { self->setExpanded(true); },
                                  Qt::QueuedConnection);
    }

    /*! Whether the menu bar is being walked with the keyboard -- reached by
     * Alt, by the menu-bar command, or by stepping out of a menu with Esc.
     * Folding it away underneath that would leave the keystrokes going
     * somewhere invisible.
     *
     * A current item, not the focus alone: Qt leaves the focus on the bar
     * after the last Esc, and a fold that waited for the focus to go would
     * stay open until something else was clicked.
     */
    bool hasKeyboard() const {
        return menuBar && menuBar->hasFocus() && menuBar->activeAction();
    }

    bool isAnyMenuVisible() const {
        if (!menuBar) return false;
        for (auto *action : menuBar->actions()) {
            if (action->menu() && action->menu()->isVisible())
                return true;
        }
        return false;
    }

    bool isMouseOverOverlay() const {
        return overlayExpand && menuContainer && menuContainer->underMouse();
    }

    void connectMenuCollapse(FoldableMenuBar *self, QMenu *menu) {
        // A menu can be opened without the bar being unfolded first: Alt+F
        // reaches the QMenuBar's mnemonic wherever the bar is. Unfold around
        // the menu that is opening, so what drops down has a menu bar over it.
        QObject::connect(menu, &QMenu::aboutToShow, self, [this, self]() {
            expandLater(self);
        });
        QObject::connect(menu, &QMenu::aboutToHide, self, [this, self]() {
            // Whether this was the last menu or the keyboard is still walking
            // the row is the collapse timer's question to answer, not this
            // one's.
            if (foldable && !self->underMouse() && !isMouseOverOverlay()) {
                collapseTimer->start();
            }
        });
    }

    /// Lay the overlay over the window at the width given, or at the width the
    /// whole row needs when none is. Zero is the folded bar: still parked
    /// where the items belong, so a menu opened while folded drops from the
    /// item it belongs to.
    void positionOverlay(FoldableMenuBar *self, int width = -1) {
        QWidget *win = self->window();
        if (!win || !menuContainer) return;
        ensureOverlayParent(self);

        QPoint origin = self->mapTo(win, QPoint(0, 0));
        if (width < 0)
            width = menuBar->sizeHint().width() + 4;

        menuContainer->setGeometry(origin.x() + menuLeft(), origin.y(), width, self->height());
    }

    /// Mask target widgets to hide their content in the overlay region.
    /// The mask grows with revealWidth so content disappears progressively.
    void updateOverlayMasks(FoldableMenuBar *self) {
        if (maskTargets.isEmpty() || !overlayExpand) return;

        if (revealWidth <= 0 || !menuContainer->isVisible()) {
            for (auto *w : maskTargets)
                w->clearMask();
            return;
        }

        QWidget *win = self->window();
        if (!win) return;

        // Overlay rect in window coordinates, width = current revealWidth
        QPoint overlayTopLeft = menuContainer->mapTo(win, QPoint(0, 0));
        QRect overlayRect(overlayTopLeft, QSize(revealWidth, menuContainer->height()));

        for (auto *target : maskTargets) {
            // Convert overlay rect to target's coordinate system
            QPoint inTarget = target->mapFrom(win, overlayRect.topLeft());
            QRect overlayInTarget(inTarget, overlayRect.size());
            QRect clipped = target->rect().intersected(overlayInTarget);

            if (clipped.isEmpty()) {
                target->clearMask();
            } else {
                target->setMask(QRegion(target->rect()) - QRegion(clipped));
            }
        }
    }

    void clearOverlayMasks() {
        for (auto *w : maskTargets)
            w->clearMask();
    }
};

FoldableMenuBar::FoldableMenuBar(QWidget *parent)
    : QWidget(parent)
    , d(std::make_unique<Impl>())
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
    setAttribute(Qt::WA_LayoutOnEntireRect, true);
#endif
    d->layout = new QHBoxLayout(this);
    d->layout->setContentsMargins(2, 0, 2, 0);
    d->layout->setSpacing(0);

    // Real QMenuBar — gets tracking, keyboard nav, mnemonics for free.
    // Wrapped in a container with VBox to force vertical centering,
    // since QMenuBar overrides size policy internally.
    d->menuBar = new QMenuBar(this);
    d->menuBar->setNativeMenuBar(false);
    d->menuBar->setProperty("foldable", true);

    // Outer container — transparent so the titlebar background (gradient, etc.)
    // shows through. Mask targets (leftArea/rightArea) clip their content
    // underneath so only menu items are visible in the overlay region.
    d->menuContainer = new QWidget(this);

    auto *containerLayout = new QVBoxLayout(d->menuContainer);
    containerLayout->setContentsMargins(0, 0, 0, 0);
    containerLayout->setSpacing(0);

    // Inner wrapper — transparent border forces Qt's styled rendering path,
    // needed for proper vertical centering of QMenuBar (platform-native ignores layout).
    d->menuWrapper = new QWidget(d->menuContainer);
    d->menuWrapper->setStyleSheet("border: 0px solid transparent;");
    auto *wrapperLayout = new QVBoxLayout(d->menuWrapper);
    wrapperLayout->setContentsMargins(0, 0, 0, 0);
    wrapperLayout->setSpacing(0);
    wrapperLayout->addWidget(d->menuBar, 0, Qt::AlignVCenter);
    containerLayout->addWidget(d->menuWrapper);

    // Overlay is default — menuContainer is NOT in the layout (floats over content).
    // In non-overlay mode it gets added to the layout in setOverlayExpand(false).

    // Track enter/leave on the overlay container for hover detection
    d->menuContainer->installEventFilter(this);

    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);

    // Animate revealWidth — only changes a clip mask, no layout involved
    d->animation = new QPropertyAnimation(this, "revealWidth", this);
    d->animation->setDuration(150);
    d->animation->setEasingCurve(QEasingCurve::OutCubic);

    connect(d->animation, &QPropertyAnimation::finished, this, [this]() {
        if (!d->expanded) {
            d->clearOverlayMasks();
            d->collapseGeometry(this);
        }
        if (d->expanded) {
            d->menuBar->clearMask();
        }
    });

    // Timer for delayed collapse when mouse leaves
    d->collapseTimer = new QTimer(this);
    d->collapseTimer->setSingleShot(true);
    d->collapseTimer->setInterval(400);
    connect(d->collapseTimer, &QTimer::timeout, this, [this]() {
        if (!d->foldable || underMouse() || d->isMouseOverOverlay()) {
            // The mouse is on it; leaveEvent() will ask again.
            return;
        }
        if (d->isAnyMenuVisible() || d->hasKeyboard()) {
            // In use by the keyboard, which ends without the mouse moving and
            // without the focus going anywhere -- neither of which would wake
            // this watch again. So keep watching rather than dropping it.
            d->collapseTimer->start();
            return;
        }
        setExpanded(false);
    });

    // The internal bar is watched for the same reasons an external one is,
    // see eventFilter().
    d->menuBar->installEventFilter(this);
}

FoldableMenuBar::~FoldableMenuBar()
{
    d->clearOverlayMasks();
    // In overlay mode, menuContainer may have been reparented to window()
    if (d->menuContainer && d->menuContainer->parent() != this)
        delete d->menuContainer;
}

int FoldableMenuBar::revealWidth() const
{
    return d->revealWidth;
}

void FoldableMenuBar::setRevealWidth(int w)
{
    d->revealWidth = w;
    d->updateClip();
    d->updateOverlayMasks(this);
}

void FoldableMenuBar::setMenuBar(QMenuBar *menuBar)
{
    if (menuBar == d->menuBar) return;

    // Remove old menuBar from wrapper
    auto *wrapperLayout = d->menuWrapper->layout();
    if (d->menuBar) {
        d->menuBar->removeEventFilter(this);
        wrapperLayout->removeWidget(d->menuBar);
        delete d->menuBar;
    }

    // Install new one
    d->menuBar = menuBar;
    d->menuBar->setNativeMenuBar(false);
    d->menuBar->setProperty("foldable", true);
    d->menuBar->setParent(d->menuWrapper);
    static_cast<QVBoxLayout *>(wrapperLayout)->addWidget(d->menuBar, 0, Qt::AlignVCenter);

    // Connect collapse for existing menus
    for (auto *action : d->menuBar->actions()) {
        if (auto *menu = action->menu())
            d->connectMenuCollapse(this, menu);
    }

    // Watch for future menus added to this QMenuBar
    d->menuBar->installEventFilter(this);

    // Apply current foldable state
    d->syncNaturalWidth();
    if (d->foldable && !d->expanded) {
        d->updateClip();
    }
}

QMenu *FoldableMenuBar::addMenu(const QString &title)
{
    auto *menu = d->menuBar->addMenu(title);
    d->connectMenuCollapse(this, menu);
    return menu;
}

void FoldableMenuBar::setBrandWidget(QWidget *brand)
{
    if (d->brandWidget) {
        d->brandWidget->removeEventFilter(this);
        d->layout->removeWidget(d->brandWidget);
        d->brandWidget->setParent(nullptr);
    }
    d->brandWidget = brand;
    if (brand) {
        brand->setParent(this);
        brand->setAttribute(Qt::WA_NoMousePropagation, true);
        brand->installEventFilter(this);
        d->layout->insertWidget(0, brand);
    }
    // The brand is what the menu items start after, and it usually arrives
    // after the fold has already been parked somewhere.
    if (d->foldable && !d->expanded) {
        d->collapseGeometry(this);
    }
}

QWidget *FoldableMenuBar::brandWidget() const
{
    return d->brandWidget;
}

void FoldableMenuBar::setFoldable(bool foldable)
{
    if (d->foldable == foldable) return;
    d->foldable = foldable;
    d->syncNaturalWidth();
    if (foldable) {
        d->expanded = false;
        d->revealWidth = 0;
        d->updateClip();
        d->collapseGeometry(this);
    }
    else {
        d->expanded = true;
        d->menuBar->clearMask();
        if (!d->overlayExpand) {
            d->menuContainer->setMaximumWidth(QWIDGETSIZE_MAX);
        }
    }
}

bool FoldableMenuBar::isFoldable() const
{
    return d->foldable;
}

void FoldableMenuBar::setOverlayExpand(bool overlay)
{
    if (d->overlayExpand == overlay) return;
    d->overlayExpand = overlay;

    if (overlay) {
        // Remove from layout — it will float as an overlay when expanded
        d->layout->removeWidget(d->menuContainer);
        d->menuContainer->setMaximumWidth(QWIDGETSIZE_MAX);
        if (d->foldable && !d->expanded) {
            d->collapseGeometry(this);
        }
    }
    else {
        d->clearOverlayMasks();
        // Ensure menuContainer is parented to this and add to layout
        if (d->menuContainer->parent() != this) {
            d->menuContainer->setParent(this);
            d->menuContainer->show();
        }
        d->layout->addWidget(d->menuContainer);
        if (d->foldable && !d->expanded) {
            d->menuContainer->setMaximumWidth(0);
        }
        else {
            d->menuContainer->setMaximumWidth(QWIDGETSIZE_MAX);
        }
    }
}

bool FoldableMenuBar::isOverlayExpand() const
{
    return d->overlayExpand;
}

void FoldableMenuBar::setOverlayMaskTargets(const QList<QWidget *> &targets)
{
    d->clearOverlayMasks();
    d->maskTargets = targets;
}

void FoldableMenuBar::setExpanded(bool expanded)
{
    if (d->expanded == expanded) return;
    d->expanded = expanded;

    if (d->foldable) {
        if (d->animation->state() == QAbstractAnimation::Running)
            d->animation->stop();

        d->syncNaturalWidth();
        int targetWidth = d->menuBar->sizeHint().width();

        if (expanded) {
            if (d->overlayExpand) {
                // Already on the window, see ensureOverlayParent(): all the
                // unfold does is give it the width of the row and put it on
                // top of the rest of the title bar.
                d->positionOverlay(this);
                d->menuContainer->show();
                d->menuContainer->raise();
            }
            else {
                d->menuContainer->setMaximumWidth(QWIDGETSIZE_MAX);
            }

            d->animation->setStartValue(0);
            d->animation->setEndValue(targetWidth);
        } else {
            d->animation->setStartValue(d->revealWidth);
            d->animation->setEndValue(0);
        }
        d->animation->start();
    }
    else {
        if (expanded) {
            d->menuBar->clearMask();
        }
        else {
            d->menuBar->setMask(QRegion(-1, -1, 1, 1));
        }
    }

    Q_EMIT expandedChanged(expanded);
}

bool FoldableMenuBar::isExpanded() const
{
    return d->expanded;
}

void FoldableMenuBar::enterEvent(QEnterEvent *event)
{
    d->collapseTimer->stop();
    QWidget::enterEvent(event);
}

void FoldableMenuBar::leaveEvent(QEvent *event)
{
    if (d->foldable) {
        d->collapseTimer->start();
    }
    QWidget::leaveEvent(event);
}

void FoldableMenuBar::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    // Update mask if needed (menuBar height may have changed)
    if (d->foldable && !d->expanded) {
        d->updateClip();
        d->collapseGeometry(this);
    }
}

bool FoldableMenuBar::eventFilter(QObject *obj, QEvent *event)
{
    if (obj == d->brandWidget && event->type() == QEvent::MouseButtonPress) {
        auto *mouseEvent = static_cast<QMouseEvent *>(event);
        if (mouseEvent->button() == Qt::LeftButton && d->foldable) {
            d->collapseTimer->stop();
            mouseEvent->accept();
            return true;
        }
    }

    if (obj == d->brandWidget && event->type() == QEvent::MouseButtonRelease) {
        auto *mouseEvent = static_cast<QMouseEvent *>(event);
        if (mouseEvent->button() == Qt::LeftButton
            && d->brandWidget->rect().contains(mouseEvent->position().toPoint())
            && d->foldable) {
            d->collapseTimer->stop();
            setExpanded(!d->expanded);
            mouseEvent->accept();
            return true;
        }
    }

    if (obj == d->menuBar) {
        switch (event->type()) {
        // Watch for menus being added to the QMenuBar
        case QEvent::ActionAdded:
            if (auto *menu = static_cast<QActionEvent *>(event)->action()->menu())
                d->connectMenuCollapse(this, menu);
            Q_FALLTHROUGH();
        case QEvent::ActionRemoved:
        case QEvent::ActionChanged:
            // A workbench swap rewrites the whole row, and the folded bar has
            // to keep answering for where its items are.
            d->syncNaturalWidth();
            break;
        // Anything that hands the menu bar the keyboard -- Alt, the menu-bar
        // command, Esc out of an open menu -- has to unfold it. A menu bar
        // taking keystrokes where it cannot be seen is worse than one that
        // never took them.
        case QEvent::FocusIn:
            d->collapseTimer->stop();
            d->expandLater(this);
            break;
        case QEvent::FocusOut:
            if (d->foldable && !d->isAnyMenuVisible())
                d->collapseTimer->start();
            break;
        default:
            break;
        }
    }

    // In overlay mode, track mouse on the floating container
    if (d->overlayExpand && obj == d->menuContainer) {
        if (event->type() == QEvent::Enter) {
            d->collapseTimer->stop();
        } else if (event->type() == QEvent::Leave) {
            if (d->foldable && !d->isAnyMenuVisible())
                d->collapseTimer->start();
        }
    }

    return QWidget::eventFilter(obj, event);
}
