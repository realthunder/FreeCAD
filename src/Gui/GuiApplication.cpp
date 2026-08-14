/***************************************************************************
 *   Copyright (c) 2015 Werner Mayer <wmayer[at]users.sourceforge.net>     *
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
# include <chrono>
# include <sstream>
# include <QAbstractSpinBox>
# include <QByteArray>
# include <QComboBox>
# include <QDataStream>
# include <QFileInfo>
# include <QFileOpenEvent>
# include <QSessionManager>
# include <QTimer>
# include <QMessageBox>
#endif

#include <QLocalServer>
#include <QLocalSocket>

#if defined(Q_OS_UNIX)
# include <sys/types.h>
# include <ctime>
# include <unistd.h>
#endif

#if defined(__linux__)
# include <atomic>
# include <chrono>
# include <thread>
# include <csignal>
# include <execinfo.h>
# include <pthread.h>
#endif

#include <App/Application.h>
#include <Base/Console.h>
#include <Base/Exception.h>

#include "GuiApplication.h"
#include "Application.h"
#include "MainWindow.h"
#include "RenderParams.h"
#include "SpaceballEvent.h"


using namespace Gui;

namespace {
#if defined(__linux__)
/// A mid-flight stack sampler for slow paint dispatches.
///
/// Every bracket inside the paint path -- renderScene's FrameOutside
/// spans, the publish stage timers, the backend's own frame account --
/// reports single-digit milliseconds while the paint dispatch above
/// them reports hundreds, so the cost sits in code nobody bracketed,
/// and only a stack taken WHILE the paint runs can name it. A watchdog
/// thread watches the dispatch the tracer below armed; once the paint
/// outlives the slow threshold the GUI thread is signalled and the
/// handler writes a backtrace to stderr. Samples repeat once per
/// threshold until the dispatch ends (capped), so a long paint yields
/// a small profile rather than one guess.
///
/// Signal-context rules: the handler calls only write(),
/// ::backtrace() and ::backtrace_symbols_fd() (both async-signal-safe
/// in glibc once primed); the constructor primes the unwinder on the
/// GUI thread first, so the handler never takes glibc's one-time init
/// lock. The watchdog reads no RenderParams -- the arming dispatch
/// snapshots the threshold into an atomic, so the worker touches only
/// this struct's atomics.
struct PaintSampler {
    std::atomic<long long> startNs {0};
    std::atomic<long long> thresholdMs {0};
    std::atomic<int> taken {0};
    pthread_t guiThread {};
    static constexpr int maxSamples = 4;

    static PaintSampler& instance()
    {
        static PaintSampler self;
        return self;
    }

    static void onSignal(int)
    {
        static const char head[] = "slow paint sample:\n";
        const ssize_t w = ::write(2, head, sizeof(head) - 1);
        (void)w;
        void* frames[48];
        const int n = ::backtrace(frames, 48);
        ::backtrace_symbols_fd(frames, n, 2);
    }

    PaintSampler()
        : guiThread(::pthread_self())
    {
        // Prime the unwinder outside signal context (its first call
        // initializes libgcc's unwind tables under a lock).
        void* frames[2];
        ::backtrace(frames, 2);
        struct sigaction sa {};
        sa.sa_handler = &onSignal;
        ::sigemptyset(&sa.sa_mask);
        sa.sa_flags = SA_RESTART;
        ::sigaction(SIGRTMIN + 7, &sa, nullptr);
        std::thread([this]() { run(); }).detach();
    }

    void run()
    {
        for (;;) {
            std::this_thread::sleep_for(std::chrono::milliseconds(25));
            const long long t0 = startNs.load(std::memory_order_acquire);
            if (!t0)
                continue;
            const long long limit = thresholdMs.load(std::memory_order_relaxed);
            if (limit <= 0)
                continue;
            const long long elapsedMs =
                (std::chrono::duration_cast<std::chrono::nanoseconds>(
                     std::chrono::steady_clock::now().time_since_epoch())
                     .count()
                 - t0)
                / 1000000;
            int had = taken.load(std::memory_order_relaxed);
            if (had >= maxSamples || elapsedMs < limit * (had + 1))
                continue;
            // The CAS keeps one signal per due sample if the paint ends
            // (and a new one arms) between the check and the kill.
            if (taken.compare_exchange_strong(had, had + 1))
                ::pthread_kill(guiThread, SIGRTMIN + 7);
        }
    }

    /// Returns false when another paint already holds the sampler (a
    /// nested paint keeps the outer clock running rather than resetting
    /// it); the caller only disarms what it armed.
    bool arm(long long limitMs)
    {
        long long expected = 0;
        const long long now =
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now().time_since_epoch())
                .count();
        // Threshold and count are only touched once this arm owns the
        // sampler, or a nested paint would reset the outer one's cap.
        thresholdMs.store(limitMs, std::memory_order_relaxed);
        if (!startNs.compare_exchange_strong(expected, now,
                                             std::memory_order_release,
                                             std::memory_order_relaxed))
            return false;
        taken.store(0, std::memory_order_relaxed);
        return true;
    }

    void disarm()
    {
        startNs.store(0, std::memory_order_release);
    }
};
#endif

/// Which single event-loop dispatch a GUI stall IS (armed by LevelDebug
/// with Render_LevelSlowBuildMS as the threshold). The interactivity
/// gate measures gaps between timer firings, and the landing pump and
/// the visual builds time themselves -- but a measured gap larger than
/// every measured turn means the stall is a dispatch nobody bracketed,
/// and this is the one place every dispatch passes through.
///
/// Reported at EVERY nesting depth, depth on the line: a blocking
/// script pumps the loop from inside its own dispatch (the measured
/// case: a whole 180s harness ran as ONE outermost meta-call, and an
/// outermost-only trace saw nothing inside it), so the innermost slow
/// line is the owner and its ancestors are the pumps it ran under.
///
/// The receiver's identity is captured up front: an event handler may
/// destroy its own receiver (a close, a deleteLater drain), so nothing
/// may touch the pointer after the dispatch. The class name is a
/// pointer into the static meta object and outlives the instance.
struct SlowDispatchTrace {
    static thread_local int depth;
    const bool armed;
    int myDepth = 0;
    int eventType = 0;
    bool paintArmed = false;
    const char *className = nullptr;
    QString objectName;
    std::chrono::steady_clock::time_point start;
    SlowDispatchTrace(QObject *receiver, QEvent *event)
        : armed(Gui::RenderParams::getLevelDebug()
                && Gui::RenderParams::getLevelSlowBuildMS() > 0)
    {
        myDepth = depth++;
        if (!armed)
            return;
        eventType = int(event->type());
        className = receiver->metaObject()->className();
        objectName = receiver->objectName();
        start = std::chrono::steady_clock::now();
#if defined(__linux__)
        // Paint dispatches get the stack sampler: their cost has no
        // in-code bracket left to name it (widget paints only run on
        // the GUI thread, so pthread_self() in the ctor is the right
        // thread to signal).
        if (event->type() == QEvent::Paint)
            paintArmed = PaintSampler::instance().arm(
                Gui::RenderParams::getLevelSlowBuildMS());
#endif
    }
    ~SlowDispatchTrace()
    {
        --depth;
        if (!armed)
            return;
#if defined(__linux__)
        if (paintArmed)
            PaintSampler::instance().disarm();
#endif
        const double ms = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - start).count();
        if (ms < double(Gui::RenderParams::getLevelSlowBuildMS()))
            return;
        Base::Console().Message(
            "slow dispatch: depth %d %.0fms event %d to %s (%s)\n",
            myDepth, ms, eventType, className,
            objectName.isEmpty() ? "-" : qPrintable(objectName));
    }
};
thread_local int SlowDispatchTrace::depth = 0;
}

GUIApplication::GUIApplication(int & argc, char ** argv)
    : GUIApplicationNativeEventAware(argc, argv)
{
    connect(this, &GUIApplication::commitDataRequest,
            this, &GUIApplication::commitData, Qt::DirectConnection);
#if QT_VERSION < QT_VERSION_CHECK(6,0,0)
    setFallbackSessionManagementEnabled(false);
#endif
}

GUIApplication::~GUIApplication() = default;

bool GUIApplication::notify (QObject * receiver, QEvent * event)
{
    if (!receiver) {
        Base::Console().Log("GUIApplication::notify: Unexpected null receiver, event type: %d\n",
            (int)event->type());
        return false;
    }
    SlowDispatchTrace slowTrace(receiver, event);
    try {
        if (event->type() == Spaceball::ButtonEvent::ButtonEventType ||
            event->type() == Spaceball::MotionEvent::MotionEventType)
            return processSpaceballEvent(receiver, event);
        else
            return QApplication::notify(receiver, event);
    }
    catch (const Base::AccessViolation &e) {
        QMessageBox::critical(getMainWindow(), QObject::tr("Access violation"), QObject::tr(e.what()));
    } catch (const Base::SystemExitException &e) {
        caughtException.reset(new Base::SystemExitException(e));
        qApp->exit(e.getExitCode());
        return true;
    }
    catch (const Base::Exception& e) {
        e.ReportException();
        Base::Console().Error("Unhandled Base::Exception caught in GUIApplication::notify\n");
    }
    catch (Py::Exception &) {
        Base::PyGILStateLocker lock;
        Base::PyException e;
        e.ReportException();
        Base::Console().Error("Unhandled Python exception caught in GUIApplication::notify\n");
    }
    catch (const std::exception& e) {
        Base::Console().Error("Unhandled std::exception caught in GUIApplication::notify.\n"
                              "The error message is: %s\n", e.what());
#ifdef FC_DEBUG
        assert(0);
#endif
    }
    catch (...) {
        Base::Console().Error("Unhandled unknown exception caught in GUIApplication::notify.\n");
#ifdef FC_DEBUG
        assert(0);
#endif
    }

    // Print some more information to the log file (if active) to ease bug fixing
    try {
        std::stringstream dump;
        dump << "The event type " << (int)event->type() << " was sent to "
             << receiver->metaObject()->className() << "\n";
        dump << "Object tree:\n";
        if (receiver->isWidgetType()) {
            QWidget* w = qobject_cast<QWidget*>(receiver);
            while (w) {
                dump << "\t";
                dump << w->metaObject()->className();
                QString name = w->objectName();
                if (!name.isEmpty())
                    dump << " (" << (const char*)name.toUtf8() << ")";
                w = w->parentWidget();
                if (w)
                    dump << " is child of\n";
            }
            std::string str = dump.str();
            Base::Console().Log("%s",str.c_str());
        }
    }
    catch (...) {
        Base::Console().Log("Invalid recipient and/or event in GUIApplication::notify\n");
    }

    return true;
}

void GUIApplication::commitData(QSessionManager &manager)
{
    if (manager.allowsInteraction()) {
        if (!Gui::getMainWindow()->close()) {
            // cancel the shutdown
            manager.release();
            manager.cancel();
        }
    }
    else {
        // no user interaction allowed, thus close all documents and
        // the main window
        App::GetApplication().closeAllDocuments();
        Gui::getMainWindow()->close();
    }
}

bool GUIApplication::event(QEvent * ev)
{
    if (ev->type() == QEvent::FileOpen) {
        QString file = static_cast<QFileOpenEvent*>(ev)->file();
        QFileInfo fi(file);
        if (fi.suffix().toLower() == QStringLiteral("fcstd")) {
            QByteArray fn = file.toUtf8();
            Application::Instance->open(fn, "FreeCAD");
            return true;
        }
    }

    return GUIApplicationNativeEventAware::event(ev);
}

// ----------------------------------------------------------------------------

class GUISingleApplication::Private {
public:
    explicit Private(GUISingleApplication *q_ptr)
      : q_ptr(q_ptr)
      , timer(new QTimer(q_ptr))
    {
        timer->setSingleShot(true);
        std::string exeName = App::Application::getExecutableName();
        serverName = QString::fromStdString(exeName);
    }

    ~Private()
    {
        if (server)
            server->close();
        delete server;
    }

    void setupConnection()
    {
        QLocalSocket socket;
        socket.connectToServer(serverName);
        if (socket.waitForConnected(1000)) {
            this->running = true;
        }
        else {
            startServer();
        }
    }

    void startServer()
    {
        // Start a QLocalServer to listen for connections
        server = new QLocalServer();
        QObject::connect(server, &QLocalServer::newConnection,
                         q_ptr, &GUISingleApplication::receiveConnection);
        // first attempt
        if (!server->listen(serverName)) {
            if (server->serverError() == QAbstractSocket::AddressInUseError) {
                // second attempt
                server->removeServer(serverName);
                server->listen(serverName);
            }
        }
        if (server->isListening()) {
            Base::Console().Log("Local server '%s' started\n", qPrintable(serverName));
        }
        else {
            Base::Console().Log("Local server '%s' failed to start\n", qPrintable(serverName));
        }
    }

    GUISingleApplication *q_ptr;
    QTimer *timer;
    QLocalServer *server{nullptr};
    QString serverName;
    QList<QByteArray> messages;
    bool running{false};
};

GUISingleApplication::GUISingleApplication(int & argc, char ** argv)
    : GUIApplication(argc, argv),
      d_ptr(new Private(this))
{
    d_ptr->setupConnection();
    connect(d_ptr->timer, &QTimer::timeout, this, &GUISingleApplication::processMessages);
}

GUISingleApplication::~GUISingleApplication() = default;

bool GUISingleApplication::isRunning() const
{
    return d_ptr->running;
}

bool GUISingleApplication::sendMessage(const QByteArray &message, int timeout)
{
    QLocalSocket socket;
    bool connected = false;
    for(int i = 0; i < 2; i++) {
        socket.connectToServer(d_ptr->serverName);
        connected = socket.waitForConnected(timeout/2);
        if (connected || i > 0)
            break;
        int ms = 250;
#if defined(Q_OS_WIN)
        Sleep(DWORD(ms));
#else
        usleep(ms*1000);
#endif
    }
    if (!connected)
        return false;

    QDataStream ds(&socket);
    ds << message;
    socket.waitForBytesWritten(timeout);
    return true;
}

void GUISingleApplication::receiveConnection()
{
    QLocalSocket *socket = d_ptr->server->nextPendingConnection();
    if (!socket)
        return;

    connect(socket, &QLocalSocket::disconnected,
            socket, &QLocalSocket::deleteLater);
    if (socket->waitForReadyRead()) {
        QDataStream in(socket);
        if (!in.atEnd()) {
            d_ptr->timer->stop();
            QByteArray message;
            in >> message;
            Base::Console().Log("Received message: %s\n", message.constData());
            d_ptr->messages.push_back(message);
            d_ptr->timer->start(1000);
        }
    }

    socket->disconnectFromServer();
}

void GUISingleApplication::processMessages()
{
    QList<QByteArray> msg = d_ptr->messages;
    d_ptr->messages.clear();
    Q_EMIT messageReceived(msg);
}

// ----------------------------------------------------------------------------

WheelEventFilter::WheelEventFilter(QObject* parent)
  : QObject(parent)
{
}

bool WheelEventFilter::eventFilter(QObject* obj, QEvent* ev)
{
    if (qobject_cast<QComboBox*>(obj) && ev->type() == QEvent::Wheel)
        return true;
    auto sb = qobject_cast<QAbstractSpinBox*>(obj);
    if (sb) {
        if (ev->type() == QEvent::Show) {
            sb->setFocusPolicy(Qt::StrongFocus);
        }
        else if (ev->type() == QEvent::Wheel) {
            return !sb->hasFocus();
        }
    }
    return false;
}

#include "moc_GuiApplication.cpp"
