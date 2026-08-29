// SPDX-FileCopyrightText: 2026 Benjamin Nauck
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "customtitlebarkit/CustomTitleBarWindow.h"
#include "customtitlebarkit/TitleBarWidget.h"
#include "customtitlebarkit/MenuIntegration.h"
#include "platform/PlatformTitleBarBackend.h"

#include <QChildEvent>
#include <QEvent>
#include <QMenu>
#include <QMenuBar>
#include <QPointer>
#include <QResizeEvent>
#include <QShowEvent>
#include <QVariantAnimation>
#include <QWindow>

struct CustomTitleBarWindow::Impl {
    CustomTitleBarWindow::Mode mode = Mode::Custom;
    QPointer<TitleBarWidget> titleBar;
    QPointer<QWidget> menuSpacer;   // plain spacer used as setMenuWidget()
    std::unique_ptr<PlatformTitleBarBackend> backend;
    QPointer<QMenuBar> appMenuBar;
    QPointer<MenuIntegration> menuIntegration;
    int titleBarHeight = 0;
    bool titleBarVisible = true;
    bool attached = false;
    bool safeAreaConnected = false;
    bool menuGuardQueued = false;
    int cachedSpacerWidth = 0;
    QVariantAnimation *spacerAnim = nullptr;

    void ensureMenuSpacer(CustomTitleBarWindow* window)
    {
        if (menuSpacer) {
            return;
        }

        menuSpacer = new QWidget(window);
        window->setMenuWidget(menuSpacer);
    }

    void updateNativeControlsSpacer() {
        if (!backend || !titleBar) {
            return;
        }

        int w = backend->nativeControlsAreaSize().width();
        // Only grow, never shrink — button positions can fluctuate during layout
        if (w > cachedSpacerWidth)
            cachedSpacerWidth = w;
        titleBar->setNativeControlsSpacerSize({cachedSpacerWidth, 0});
    }

    void layoutOverlay(CustomTitleBarWindow* window)
    {
        if (!backend || !titleBar) {
            return;
        }

        ensureMenuSpacer(window);
        if (!menuSpacer) {
            return;
        }

        int overlayH = backend->snapTitleBarHeight(titleBar->minimumHeight());
        int spacerH = titleBarVisible ? overlayH : 0;
        // Spacer reserves space in QMainWindow's internal layout
        menuSpacer->setFixedHeight(spacerH);
        // Overlay renders on top at (0,0)
        titleBar->setGeometry(0, 0, window->width(), overlayH);
        titleBar->raise();
    }

    void animateSpacerWidth(int targetWidth) {
        if (!spacerAnim || !titleBar) {
            return;
        }

        if (spacerAnim->state() == QAbstractAnimation::Running)
            spacerAnim->stop();
        spacerAnim->setStartValue(titleBar->nativeControlsSpacer()->width());
        spacerAnim->setEndValue(targetWidth);
        spacerAnim->start();
    }

    // LOCAL DIVERGENCE: put the menu spacer back when something else takes
    // its place in QMainWindow's menu-widget slot. The usurper is plain
    // QMainWindow::menuBar(): the kit shadows menuBar() non-virtually, so
    // every call through a QMainWindow* -- which is all of Python, PySide
    // binds the base class -- reaches Qt's, and Qt's lazily creates an empty
    // QMenuBar INTO the layout when the menu widget is not already one. The
    // empty bar has height zero, so the space the spacer was reserving
    // collapses and the title bar overlay lands on the first toolbar row:
    // still painted, but on top of the toolbars, taking their clicks as
    // window-drag. One innocent Gui.getMainWindow().menuBar() from any
    // addon breaks every toolbar on the row.
    void guardMenuWidget(CustomTitleBarWindow *window)
    {
        menuGuardQueued = false;
        if (mode != Mode::Custom || !attached) {
            return;
        }
        QWidget *current = window->QMainWindow::menuWidget();
        if (!menuSpacer || current == menuSpacer) {
            return;
        }
        if (auto *bar = qobject_cast<QMenuBar *>(current)) {
            if (bar == appMenuBar) {
                // The application's own menu bar, in the slot on purpose --
                // nothing in the kit or in Qt's lazy path puts it there.
                // Evicting it through setMenuWidget() would delete it, so
                // whoever arranged this keeps it.
                return;
            }
            if (!bar->actions().isEmpty()) {
                // The caller managed to put menus on it before the guard
                // fired -- a script's menuBar().addMenu(...). What they
                // wanted was the application's menu bar, so carry the
                // entries over to it instead of deleting them.
                QMenuBar *appBar = window->menuBar();
                const auto actions = bar->actions();
                for (QAction *action : actions) {
                    if (QMenu *menu = action->menu()) {
                        menu->setParent(appBar, menu->windowFlags());
                    }
                    else if (action->parent() == bar) {
                        action->setParent(appBar);
                    }
                    appBar->addAction(action);
                }
            }
        }
        // Qt deletes the widget this replaces, which is the point: the
        // imposter reserves no height and shows nothing.
        window->setMenuWidget(menuSpacer);
        layoutOverlay(window);
    }

    // LOCAL DIVERGENCE from FreeCAD/FreeCAD#26766: upstream builds all of this
    // in the constructor and never takes it down. Both halves live here instead
    // so setMode() can run them on a window that already exists.
    void setupCustom(CustomTitleBarWindow *window)
    {
        // A window that has been running native already owns the application's
        // menu bar, and setMenuWidget() below would delete it as the outgoing
        // menu widget. Take it out of QMainWindow's layout first and hand it to
        // the menu integration at the end. On the constructor path there is no
        // menu bar yet and this finds nothing, which is why upstream never
        // needed it.
        QMenuBar *existingMenuBar = appMenuBar;
        if (!existingMenuBar)
            existingMenuBar = qobject_cast<QMenuBar *>(window->QMainWindow::menuWidget());
        if (existingMenuBar) {
            existingMenuBar->hide();
            existingMenuBar->setParent(nullptr);
        }

        backend = PlatformTitleBarBackend::create();

        titleBar = new TitleBarWidget(window);
        titleBar->setMinimumHeight(backend->snapTitleBarHeight(0));

        // Show window controls if backend is frameless (no native titlebar)
        titleBar->setWindowControlsVisible(backend->needsWindowControls());

        // Replace the native controls spacer with a custom widget if the backend provides one
        if (auto *ctrlWidget = backend->createNativeControlsWidget(titleBar)) {
            if (backend->nativeControlsPosition() == PlatformTitleBarBackend::RightSide)
                titleBar->setNativeControlsWidgetRight(ctrlWidget);
            else
                titleBar->setNativeControlsWidget(ctrlWidget);
        }

        // Plain spacer as menu widget — reserves space in QMainWindow's layout
        // without containing QPushButtons that would affect native titlebar sizing.
        menuSpacer = new QWidget(window);
        menuSpacer->setFixedHeight(titleBar->minimumHeight());
        window->setMenuWidget(menuSpacer);

        // Animate native controls spacer width on fullscreen transitions
        spacerAnim = new QVariantAnimation(window);
        spacerAnim->setDuration(200);
        spacerAnim->setEasingCurve(QEasingCurve::OutCubic);
        QObject::connect(spacerAnim, &QVariantAnimation::valueChanged, window,
                         [this](const QVariant &value) {
            if (titleBar)
                titleBar->setNativeControlsSpacerSize({value.toInt(), 0});
        });

        // Watch for titlebar resize to auto-notify the backend (e.g. Mac NSToolbar)
        titleBar->installEventFilter(window);

        // Force native window creation and apply styling eagerly
        window->winId();
        backend->attach(window);
        attached = true;

        // Position overlay and set content margins AFTER attach() so that
        // setWindowFlags/WA_LayoutOnEntireRect are already in effect.
        layoutOverlay(window);

        // Show it explicitly rather than relying on the parent to bring its
        // children up. A widget created as the child of an already-visible
        // parent starts hidden, which is the run-time switch case -- and there
        // is no "is the window visible" test to gate this on, because attach()
        // just changed the window flags and Qt hides a window when it does.
        // Harmless on the constructor path: the window is not on screen yet, so
        // this only marks the title bar to appear when the window does.
        titleBar->setVisible(titleBarVisible);

        // Create default menu integration strategy from the platform backend
        menuIntegration = backend->createDefaultMenuIntegration(window);

        QObject::connect(backend.get(), &PlatformTitleBarBackend::fullscreenChanged, window,
                         [this, window](bool fullscreen) {
            animateSpacerWidth(fullscreen ? 0 : cachedSpacerWidth);
            layoutOverlay(window);
        });

        if (existingMenuBar) {
            appMenuBar = existingMenuBar;
            menuIntegration->install(appMenuBar, window);
        }
    }

    void teardownCustom(CustomTitleBarWindow *window)
    {
        // uninstall() reparents the menu bar back onto the window and undoes
        // what foldable mode did to it, so it has to run before anything it
        // put the menu bar inside is destroyed.
        if (menuIntegration) {
            if (appMenuBar)
                menuIntegration->uninstall(window);
            delete menuIntegration;
            menuIntegration = nullptr;
        }

        if (backend) {
            backend->detach();
            backend.reset();
        }
        attached = false;

        delete titleBar;
        titleBar = nullptr;

        delete spacerAnim;
        spacerAnim = nullptr;

        // Handing the menu bar back to QMainWindow replaces the spacer standing
        // in for it, and Qt deletes the widget it replaces.
        if (appMenuBar) {
            appMenuBar->show();
            window->QMainWindow::setMenuBar(appMenuBar);
        }
        else {
            window->setMenuWidget(nullptr);
        }
        menuSpacer = nullptr;
        cachedSpacerWidth = 0;
        titleBarHeight = 0;
    }
};

CustomTitleBarWindow::CustomTitleBarWindow(Mode mode, QWidget *parent)
    : QMainWindow(parent)
    , d(std::make_unique<Impl>())
{
    d->mode = mode;

    // Check env var override
    QByteArray envMode = qgetenv("CUSTOMTITLEBARKIT_MODE");
    if (envMode.toLower() == "native")
        d->mode = Mode::Native;

    if (d->mode == Mode::Native)
        return;  // plain QMainWindow — nothing to set up

    d->setupCustom(this);
}

CustomTitleBarWindow::~CustomTitleBarWindow() = default;

void CustomTitleBarWindow::setMode(Mode newMode)
{
    // LOCAL DIVERGENCE from FreeCAD/FreeCAD#26766 -- see the header.
    if (d->mode == newMode)
        return;

    const bool wasVisible = isVisible();

    if (newMode == Mode::Custom) {
        d->mode = Mode::Custom;
        d->setupCustom(this);
    }
    else {
        d->teardownCustom(this);
        d->mode = Mode::Native;
    }

    // Both directions run through setWindowFlags in the platform backend, and
    // Qt hides a window whose flags change. Put it back the way it was found.
    if (wasVisible && !isVisible())
        show();

    Q_EMIT modeChanged(d->mode);
}

CustomTitleBarWindow::Mode CustomTitleBarWindow::mode() const
{
    return d->mode;
}

QString CustomTitleBarWindow::backendName() const
{
    return d->backend ? d->backend->backendName() : QString();
}

int CustomTitleBarWindow::titleBarHeight() const
{
    return d->titleBarHeight;
}

void CustomTitleBarWindow::setTitleBarHeight(int height)
{
    if (d->mode == Mode::Native) return;
    if (!d->backend || !d->titleBar) return;
    if (d->titleBarHeight == height)
        return;

    d->titleBarHeight = height;
    d->titleBar->setMinimumHeight(d->backend->snapTitleBarHeight(height));
    d->layoutOverlay(this);
    Q_EMIT titleBarHeightChanged(height);
}

QWidget *CustomTitleBarWindow::leftArea() const
{
    if (d->mode == Mode::Native) return nullptr;
    if (!d->titleBar) return nullptr;
    return d->titleBar->leftArea();
}

QWidget *CustomTitleBarWindow::rightArea() const
{
    if (d->mode == Mode::Native) return nullptr;
    if (!d->titleBar) return nullptr;
    return d->titleBar->rightArea();
}

QWidget *CustomTitleBarWindow::nativeControlsWidget() const
{
    if (d->mode == Mode::Native) return nullptr;
    if (!d->titleBar) return nullptr;
    return d->titleBar->nativeControlsSpacer();
}

QMenuBar *CustomTitleBarWindow::menuBar()
{
    if (d->mode == Mode::Native)
        return QMainWindow::menuBar();
    if (!d->menuIntegration)
        return QMainWindow::menuBar();
    if (!d->appMenuBar) {
        d->appMenuBar = new QMenuBar(this);
        d->menuIntegration->install(d->appMenuBar, this);
    }
    return d->appMenuBar;
}

void CustomTitleBarWindow::setMenuBar(QMenuBar *mb)
{
    if (d->mode == Mode::Native) {
        QMainWindow::setMenuBar(mb);
        return;
    }
    if (!d->menuIntegration)
        return;
    if (d->appMenuBar) {
        d->menuIntegration->uninstall(this);
        if (d->appMenuBar->parent() == this)
            delete d->appMenuBar;
    }
    d->appMenuBar = mb;
    if (mb)
        d->menuIntegration->install(mb, this);
}

void CustomTitleBarWindow::setMenuIntegration(MenuIntegration *integration)
{
    if (d->mode == Mode::Native) {
        delete integration;
        return;
    }
    if (!d->backend) {
        delete integration;
        return;
    }
    if (d->appMenuBar && d->menuIntegration)
        d->menuIntegration->uninstall(this);
    delete d->menuIntegration;
    // nullptr resets to the platform default
    d->menuIntegration = integration ? integration
                                     : d->backend->createDefaultMenuIntegration(this);
    if (d->appMenuBar)
        d->menuIntegration->install(d->appMenuBar, this);
}

bool CustomTitleBarWindow::isTitleBarVisible() const
{
    return d->titleBarVisible;
}

void CustomTitleBarWindow::setTitleBarVisible(bool visible)
{
    if (d->mode == Mode::Native) return;
    if (!d->backend || !d->titleBar) return;
    if (d->titleBarVisible == visible)
        return;

    d->titleBarVisible = visible;
    if (visible) {
        d->titleBar->setMinimumHeight(d->backend->snapTitleBarHeight(d->titleBarHeight));
        d->titleBar->setVisible(true);
    } else {
        d->titleBar->setMinimumHeight(0);
        d->titleBar->setVisible(false);
    }
    d->layoutOverlay(this);
    Q_EMIT titleBarVisibleChanged(visible);
}

void CustomTitleBarWindow::showEvent(QShowEvent *event)
{
    QMainWindow::showEvent(event);
    if (d->mode == Mode::Native) return;

    d->layoutOverlay(this);

    d->updateNativeControlsSpacer();

    // Connect to safeAreaMarginsChanged for dynamic spacer updates (once)
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
    if (!d->safeAreaConnected) {
        if (auto *wh = windowHandle()) {
            connect(wh, &QWindow::safeAreaMarginsChanged, this, [this]() {
                if (d->backend && !d->backend->isFullscreen())
                    d->updateNativeControlsSpacer();
            });
            d->safeAreaConnected = true;
        }
    }
#endif
}

void CustomTitleBarWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);
    if (d->mode == Mode::Native) return;
    d->layoutOverlay(this);
}

void CustomTitleBarWindow::childEvent(QChildEvent *event)
{
    QMainWindow::childEvent(event);
    // The menu bar QMainWindow::menuBar() creates arrives here as a
    // ChildAdded -- but mid-construction, when a cast to QMenuBar cannot
    // succeed yet. So any added widget queues one check of the menu-widget
    // slot instead; the check is a pointer compare, and the flag folds a
    // construction storm into a single visit.
    if (event->added() && event->child()->isWidgetType()
        && d->mode == Mode::Custom && d->attached && !d->menuGuardQueued) {
        d->menuGuardQueued = true;
        QMetaObject::invokeMethod(this, [this]() { d->guardMenuWidget(this); },
                                  Qt::QueuedConnection);
    }
}

bool CustomTitleBarWindow::nativeEvent(const QByteArray &eventType, void *message, qintptr *result)
{
    if (d->mode != Mode::Native && d->backend) {
        if (d->backend->handleNativeEvent(eventType, message, result))
            return true;
    }
    return QMainWindow::nativeEvent(eventType, message, result);
}

bool CustomTitleBarWindow::eventFilter(QObject *obj, QEvent *event)
{
    if (d->mode == Mode::Native)
        return QMainWindow::eventFilter(obj, event);
    if (obj == d->titleBar && d->backend && event->type() == QEvent::Resize
        && !d->backend->isFullscreen()) {
        d->backend->setTitleBarHeight(d->titleBar->height());
        d->updateNativeControlsSpacer();
    }
    return QMainWindow::eventFilter(obj, event);
}
