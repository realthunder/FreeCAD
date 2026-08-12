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

#include <App/Application.h>
#include <Base/Console.h>
#include <Base/CrashLog.h>
#include <Base/Exception.h>

#include "GuiApplication.h"
#include "Application.h"
#include "MainWindow.h"
#include "SpaceballEvent.h"


using namespace Gui;

namespace
{

/** Record an exception that reached the event loop, in the run's crash log.
 *
 * Nothing should ever throw this far. notify() is the last boundary before Qt,
 * and Qt frames cannot be unwound through safely, so every arrival here is a
 * leak worth a record even when the application carries on afterwards.
 * Severity separates the two cases: Fatal for the ones that mean the process is
 * already damaged and is only still standing because a fault was turned into an
 * exception, Caught for a leak that was survivable.
 *
 * Deliberately no stack walk. By the time a catch body runs the stack is
 * already unwound, so walking it here would only ever describe notify() itself
 * -- never where the exception came from. What does identify the origin is the
 * exception's own file/line, which the THROWM family records at the throw site
 * for free.
 */
void logNotifyException(Base::CrashLog::Severity severity,
                        const char* type,
                        const Base::Exception* exc,
                        const char* what,
                        QObject* receiver,
                        QEvent* event)
{
    std::ostringstream headline;
    headline << type;
    if (what && *what) {
        headline << ": " << what;
    }
    Base::CrashLog::Entry entry(severity, headline.str());

    std::ostringstream where;
    where << "  event type " << (event ? static_cast<int>(event->type()) : -1) << ", receiver "
          << (receiver ? receiver->metaObject()->className() : "<null>");
    if (receiver && !receiver->objectName().isEmpty()) {
        where << " '" << receiver->objectName().toUtf8().constData() << '\'';
    }
    where << '\n';
    entry.line(where.str());

    // A bare `throw Base::Xxx(...)` leaves these empty, which is worth seeing
    // as plainly as the alternative -- it says the throw site is unrecorded.
    if (exc && !exc->getFile().empty()) {
        std::ostringstream origin;
        origin << "  thrown at " << exc->getFile() << ':' << exc->getLine() << " ("
               << exc->getFunction() << ")\n";
        entry.line(origin.str());
    }
}

}  // namespace

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
    try {
        if (event->type() == Spaceball::ButtonEvent::ButtonEventType ||
            event->type() == Spaceball::MotionEvent::MotionEventType)
            return processSpaceballEvent(receiver, event);
        else
            return QApplication::notify(receiver, event);
    }
    catch (const Base::AccessViolation &e) {
        // Fatal: the process survived, but only because the fault was turned
        // into an exception. Whatever it corrupted is still corrupted.
        logNotifyException(Base::CrashLog::Severity::Fatal, "Base::AccessViolation", &e, e.what(), receiver, event);
        QMessageBox::critical(getMainWindow(), QObject::tr("Access violation"), QObject::tr(e.what()));
    } catch (const Base::SystemExitException &e) {
        // Not a leak -- this is how a Python sys.exit() reaches the event loop.
        caughtException.reset(new Base::SystemExitException(e));
        qApp->exit(e.getExitCode());
        return true;
    }
    catch (const Base::Exception& e) {
        // typeid, not getTypeId(): a Base::ValueError logged itself as
        // "Base::Exception", because the exception classes do not all register
        // a distinct type with the Base type system. typeid gives the dynamic
        // type either way -- readable on MSVC, mangled but unambiguous on gcc.
        logNotifyException(Base::CrashLog::Severity::Caught, typeid(e).name(), &e, e.what(), receiver, event);
        e.ReportException();
        Base::Console().Error("Unhandled Base::Exception caught in GUIApplication::notify\n");
    }
    catch (Py::Exception &) {
        Base::PyGILStateLocker lock;
        Base::PyException e;
        logNotifyException(Base::CrashLog::Severity::Caught, "Py::Exception", &e, e.what(), receiver, event);
        e.ReportException();
        Base::Console().Error("Unhandled Python exception caught in GUIApplication::notify\n");
    }
    catch (const std::exception& e) {
        logNotifyException(Base::CrashLog::Severity::Caught, "std::exception", nullptr, e.what(), receiver, event);
        Base::Console().Error("Unhandled std::exception caught in GUIApplication::notify.\n"
                              "The error message is: %s\n", e.what());
#ifdef FC_DEBUG
        assert(0);
#endif
    }
    catch (...) {
        // Nothing to ask for a message, so the event type below is all the
        // context there is -- which is exactly why it is worth recording.
        logNotifyException(Base::CrashLog::Severity::Caught, "unknown exception", nullptr, "", receiver, event);
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
