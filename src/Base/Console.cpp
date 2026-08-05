/***************************************************************************
 *   Copyright (c) 2002 Jürgen Riegel <juergen.riegel@web.de>              *
 *                                                                         *
 *   This file is part of the FreeCAD CAx development system.              *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU Library General Public License (LGPL)   *
 *   as published by the Free Software Foundation; either version 2 of     *
 *   the License, or (at your option) any later version.                   *
 *   for detail see the LICENCE text file.                                 *
 *                                                                         *
 *   FreeCAD is distributed in the hope that it will be useful,            *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU Library General Public License for more details.                  *
 *                                                                         *
 *   You should have received a copy of the GNU Library General Public     *
 *   License along with FreeCAD; if not, write to the Free Software        *
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  *
 *   USA                                                                   *
 *                                                                         *
 ***************************************************************************/

#include "PreCompiled.h"

#ifndef _PreComp_
#if defined(FC_OS_WIN32)
#include <windows.h>
#elif defined(FC_OS_LINUX) || defined(FC_OS_MACOSX)
#include <unistd.h>
#endif
#include <cstring>
#include <functional>
#include <list>
#endif

#include "Interpreter.h"
#include "Console.h"
#include "Exception.h"
#include "PyObjectBase.h"
#include "Interpreter.h"
#include <QCoreApplication>

FC_LOG_LEVEL_INIT("Console");

using namespace Base;

//=========================================================================

namespace Base
{

class ConsoleEvent: public QEvent
{
public:
    ConsoleSingleton::FreeCAD_ConsoleMsgType msgtype;
    IntendedRecipient recipient;
    ContentType content;
    std::string notifier;
    std::string msg;

    ConsoleEvent(ConsoleSingleton::FreeCAD_ConsoleMsgType type,
                 IntendedRecipient recipient,
                 ContentType content,
                 const std::string& notifier,
                 const std::string& msg)
        : QEvent(QEvent::User)
        , msgtype(type)
        , recipient(recipient)
        , content(content)
        , notifier(notifier)
        , msg(msg)
    {}
};

class ConsoleOutput: public QObject  // clazy:exclude=missing-qobject-macro
{
public:
    static ConsoleOutput* getInstance()
    {
        if (!instance) {
            instance = new ConsoleOutput;
        }
        return instance;
    }
    static void destruct()
    {
        delete instance;
        instance = nullptr;
    }

    void customEvent(QEvent* ev) override
    {
        if (ev->type() == QEvent::User) {
            ConsoleEvent* ce = static_cast<ConsoleEvent*>(ev);
            switch (ce->msgtype) {
                case ConsoleSingleton::MsgType_Txt:
                    Console().notifyPrivate(LogStyle::Message,
                                            ce->recipient,
                                            ce->content,
                                            ce->notifier,
                                            ce->msg);
                    break;
                case ConsoleSingleton::MsgType_Log:
                    Console().notifyPrivate(LogStyle::Log,
                                            ce->recipient,
                                            ce->content,
                                            ce->notifier,
                                            ce->msg);
                    break;
                case ConsoleSingleton::MsgType_Wrn:
                    Console().notifyPrivate(LogStyle::Warning,
                                            ce->recipient,
                                            ce->content,
                                            ce->notifier,
                                            ce->msg);
                    break;
                case ConsoleSingleton::MsgType_Err:
                    Console().notifyPrivate(LogStyle::Error,
                                            ce->recipient,
                                            ce->content,
                                            ce->notifier,
                                            ce->msg);
                    break;
                case ConsoleSingleton::MsgType_Critical:
                    Console().notifyPrivate(LogStyle::Critical,
                                            ce->recipient,
                                            ce->content,
                                            ce->notifier,
                                            ce->msg);
                    break;
                case ConsoleSingleton::MsgType_Notification:
                    Console().notifyPrivate(LogStyle::Notification,
                                            ce->recipient,
                                            ce->content,
                                            ce->notifier,
                                            ce->msg);
                    break;
            }
        }
    }

private:
    static ConsoleOutput* instance;  // NOLINT
};

ConsoleOutput* ConsoleOutput::instance = nullptr;  // NOLINT

}  // namespace Base

//**************************************************************************
// Construction destruction


ConsoleSingleton::ConsoleSingleton()
#ifdef FC_DEBUG
    : _defaultLogLevel(FC_LOGLEVEL_LOG)
#else
    : _defaultLogLevel(FC_LOGLEVEL_MSG)
#endif
{}

ConsoleSingleton::~ConsoleSingleton()
{
    ConsoleOutput::destruct();
    std::set<ILogger*> observers;
    std::vector<ILogger*> retired;
    {
        std::lock_guard<std::mutex> guard(_observerMutex);
        observers.swap(_aclObservers);
        retired.swap(_retiredObservers);
    }
    for (ILogger* Iter : observers) {
        delete Iter;
    }
    for (ILogger* Iter : retired) {
        delete Iter;
    }
}


//**************************************************************************
// methods

/**
 *  sets the console in a special mode
 */
void ConsoleSingleton::SetConsoleMode(ConsoleMode mode)
{
    if (mode & Verbose) {
        _bVerbose = true;
    }
}

/**
 *  unsets the console from a special mode
 */
void ConsoleSingleton::UnsetConsoleMode(ConsoleMode mode)
{
    if (mode & Verbose) {
        _bVerbose = false;
    }
}

/**
 * \a type can be OR'ed with any of the FreeCAD_ConsoleMsgType flags to enable -- if \a b is true --
 * or to disable -- if \a b is false -- a console observer with name \a sObs.
 * The return value is an OR'ed value of all message types that have changed their state. For
 * example
 * @code
 * // switch off warnings and error messages
 * ConsoleMsgFlags ret = Base::Console().SetEnabledMsgType("myObs",
 *                       Base:ConsoleSingleton::MsgType_Wrn|Base::ConsoleSingleton::MsgType_Err,
 * false);
 * // do something without notifying observer myObs
 * ...
 * // restore the former configuration again
 * Base::Console().SetEnabledMsgType("myObs", ret, true);
 * @endcode
 * switches off warnings and error messages and restore the state before the modification.
 * If the observer \a sObs doesn't exist then nothing happens.
 */
ConsoleMsgFlags ConsoleSingleton::SetEnabledMsgType(const char* sObs, ConsoleMsgFlags type, bool on)
{
    ILogger* pObs = Get(sObs);
    if (pObs) {
        ConsoleMsgFlags flags = 0;

        if (type & MsgType_Err) {
            if (pObs->bErr != on) {
                flags |= MsgType_Err;
            }
            pObs->bErr = on;
        }
        if (type & MsgType_Wrn) {
            if (pObs->bWrn != on) {
                flags |= MsgType_Wrn;
            }
            pObs->bWrn = on;
        }
        if (type & MsgType_Txt) {
            if (pObs->bMsg != on) {
                flags |= MsgType_Txt;
            }
            pObs->bMsg = on;
        }
        if (type & MsgType_Log) {
            if (pObs->bLog != on) {
                flags |= MsgType_Log;
            }
            pObs->bLog = on;
        }
        if (type & MsgType_Critical) {
            if (pObs->bCritical != on) {
                flags |= MsgType_Critical;
            }
            pObs->bCritical = on;
        }
        if (type & MsgType_Notification) {
            if (pObs->bNotification != on) {
                flags |= MsgType_Notification;
            }
            pObs->bNotification = on;
        }

        return flags;
    }

    return 0;
}

bool ConsoleSingleton::IsMsgTypeEnabled(const char* sObs, FreeCAD_ConsoleMsgType type) const
{
    ILogger* pObs = Get(sObs);
    if (pObs) {
        switch (type) {
            case MsgType_Txt:
                return pObs->bMsg;
            case MsgType_Log:
                return pObs->bLog;
            case MsgType_Wrn:
                return pObs->bWrn;
            case MsgType_Err:
                return pObs->bErr;
            case MsgType_Critical:
                return pObs->bCritical;
            case MsgType_Notification:
                return pObs->bNotification;
            default:
                return false;
        }
    }

    return false;
}

void ConsoleSingleton::SetConnectionMode(ConnectionMode mode)
{
    connectionMode = mode;

    // make sure this method gets called from the main thread
    if (connectionMode == Queued) {
        ConsoleOutput::getInstance();
    }
}

//**************************************************************************
// Observer stuff

/** Attaches an Observer to Console
 *  Use this method to attach a ILogger derived class to
 *  the Console. After the observer is attached all messages will also
 *  be forwarded to it.
 *  @see ILogger
 */
void ConsoleSingleton::AttachObserver(ILogger* pcObserver)
{
    std::lock_guard<std::mutex> guard(_observerMutex);
    // double insert !!
    assert(_aclObservers.find(pcObserver) == _aclObservers.end());

    _aclObservers.insert(pcObserver);
}

/** Detaches an Observer from Console
 *  Use this method to detach a ILogger derived class.
 *  After detaching you can reinsert the Observer later.
 *
 *  This guarantees the observer receives no *further* messages. It does NOT
 *  wait for one already being delivered: messages are dispatched without
 *  holding the observer lock (see notifyPrivate), so a notification that
 *  started on another thread can still be inside SendLog when this returns.
 *  Destroying the observer right after detaching is therefore only safe when
 *  the caller knows no other thread is logging -- e.g. it has just joined the
 *  threads that were. If it cannot know that, use RetireObserver(), which
 *  takes ownership and defers destruction until no notification is in flight.
 *  @see ILogger
 *  @see RetireObserver
 */
void ConsoleSingleton::DetachObserver(ILogger* pcObserver)
{
    std::lock_guard<std::mutex> guard(_observerMutex);
    _aclObservers.erase(pcObserver);
}

void ConsoleSingleton::RetireObserver(ILogger* pcObserver)
{
    bool destroyNow = false;
    {
        std::lock_guard<std::mutex> guard(_observerMutex);
        _aclObservers.erase(pcObserver);
        if (_dispatchDepth == 0) {
            destroyNow = true;
        }
        else {
            _retiredObservers.push_back(pcObserver);
        }
    }
    // Outside the lock: ~PyConsoleObserver takes the GIL, and taking the GIL
    // while holding _observerMutex is the one ordering that can deadlock.
    if (destroyNow) {
        delete pcObserver;
    }
}

std::vector<ILogger*> ConsoleSingleton::beginDispatch()
{
    std::lock_guard<std::mutex> guard(_observerMutex);
    ++_dispatchDepth;
    return {_aclObservers.begin(), _aclObservers.end()};
}

void ConsoleSingleton::endDispatch()
{
    std::vector<ILogger*> reclaim;
    {
        std::lock_guard<std::mutex> guard(_observerMutex);
        if (--_dispatchDepth == 0) {
            reclaim.swap(_retiredObservers);
        }
    }
    for (ILogger* obs : reclaim) {
        delete obs;  // outside the lock -- see RetireObserver
    }
}

void Base::ConsoleSingleton::notifyPrivate(LogStyle category,
                                           IntendedRecipient recipient,
                                           ContentType content,
                                           const std::string& notifiername,
                                           const std::string& msg)
{
    // Snapshot under the lock, dispatch without it. Holding _observerMutex
    // across SendLog would deadlock: a Python observer blocks on the GIL, while
    // the thread holding the GIL may be attaching or detaching an observer and
    // so waiting for this very mutex.
    const std::vector<ILogger*> observers = beginDispatch();
    struct DispatchGuard
    {
        ConsoleSingleton* self;
        ~DispatchGuard()
        {
            self->endDispatch();
        }
    } dispatchGuard {this};

    for (ILogger* Iter : observers) {
        if (Iter->isActive(category)) {
            Iter->SendLog(notifiername,
                          msg,
                          category,
                          recipient,
                          content);  // send string to the listener
        }
    }
}

void ConsoleSingleton::postEvent(ConsoleSingleton::FreeCAD_ConsoleMsgType type,
                                 IntendedRecipient recipient,
                                 ContentType content,
                                 const std::string& notifiername,
                                 const std::string& msg)
{
    QCoreApplication::postEvent(ConsoleOutput::getInstance(),
                                new ConsoleEvent(type, recipient, content, notifiername, msg));
}

ILogger* ConsoleSingleton::Get(const char* Name) const
{
    std::lock_guard<std::mutex> guard(_observerMutex);
    const char* OName {};
    for (ILogger* Iter : _aclObservers) {
        OName = Iter->Name();  // get the name
        if (OName && strcmp(OName, Name) == 0) {
            return Iter;
        }
    }
    return nullptr;
}

int* ConsoleSingleton::GetLogLevel(const char* tag, bool create)
{
    if (!tag) {
        tag = "";
    }
    if (_logLevels.find(tag) != _logLevels.end()) {
        return &_logLevels[tag];
    }
    if (!create) {
        return nullptr;
    }
    int& ret = _logLevels[tag];
    ret = -1;
    return &ret;
}

void ConsoleSingleton::Refresh()
{
    if (_bCanRefresh) {
        qApp->processEvents(QEventLoop::ExcludeUserInputEvents);
    }
}

void ConsoleSingleton::EnableRefresh(bool enable)
{
    _bCanRefresh = enable;
}

//**************************************************************************
// Singleton stuff

ConsoleSingleton* ConsoleSingleton::_pcSingleton = nullptr;

void ConsoleSingleton::Destruct()
{
    // not initialized or double destructed!
    assert(_pcSingleton);
    delete _pcSingleton;
    _pcSingleton = nullptr;
}

ConsoleSingleton& ConsoleSingleton::Instance()
{
    // not initialized?
    if (!_pcSingleton) {
        _pcSingleton = new ConsoleSingleton();
    }
    return *_pcSingleton;
}

//**************************************************************************
// Python stuff

// ConsoleSingleton Methods structure
PyMethodDef ConsoleSingleton::Methods[] = {
    {"PrintMessage",
     ConsoleSingleton::sPyMessage,
     METH_VARARGS,
     "PrintMessage(obj) -> None\n\n"
     "Print a message to the output.\n\n"
     "obj : object\n    The string representation is printed."},
    {"PrintLog",
     ConsoleSingleton::sPyLog,
     METH_VARARGS,
     "PrintLog(obj) -> None\n\n"
     "Print a log message to the output.\n\n"
     "obj : object\n    The string representation is printed."},
    {"PrintError",
     ConsoleSingleton::sPyError,
     METH_VARARGS,
     "PrintError(obj) -> None\n\n"
     "Print an error message to the output.\n\n"
     "obj : object\n    The string representation is printed."},
    {"PrintDeveloperError",
     ConsoleSingleton::sPyDeveloperError,
     METH_VARARGS,
     "PrintDeveloperError(obj) -> None\n\n"
     "Print an error message intended only for Developers to the output.\n\n"
     "obj : object\n    The string representation is printed."},
    {"PrintUserError",
     ConsoleSingleton::sPyUserError,
     METH_VARARGS,
     "PrintUserError(obj) -> None\n\n"
     "Print an error message intended only for the User to the output.\n\n"
     "obj : object\n    The string representation is printed."},
    {"PrintTranslatedUserError",
     ConsoleSingleton::sPyTranslatedUserError,
     METH_VARARGS,
     "PrintTranslatedUserError(obj) -> None\n\n"
     "Print an already translated error message intended only for the User to the output.\n\n"
     "obj : object\n    The string representation is printed."},
    {"PrintWarning",
     ConsoleSingleton::sPyWarning,
     METH_VARARGS,
     "PrintWarning(obj) -> None\n\n"
     "Print a warning message to the output.\n\n"
     "obj : object\n    The string representation is printed."},
    {"PrintDeveloperWarning",
     ConsoleSingleton::sPyDeveloperWarning,
     METH_VARARGS,
     "PrintDeveloperWarning(obj) -> None\n\n"
     "Print an warning message intended only for Developers to the output.\n\n"
     "obj : object\n    The string representation is printed."},
    {"PrintUserWarning",
     ConsoleSingleton::sPyUserWarning,
     METH_VARARGS,
     "PrintUserWarning(obj) -> None\n\n"
     "Print a warning message intended only for the User to the output.\n\n"
     "obj : object\n    The string representation is printed."},
    {"PrintTranslatedUserWarning",
     ConsoleSingleton::sPyTranslatedUserWarning,
     METH_VARARGS,
     "PrintTranslatedUserWarning(obj) -> None\n\n"
     "Print an already translated warning message intended only for the User to the output.\n\n"
     "obj : object\n    The string representation is printed."},
    {"PrintCritical",
     ConsoleSingleton::sPyCritical,
     METH_VARARGS,
     "PrintCritical(obj) -> None\n\n"
     "Print a critical message to the output.\n\n"
     "obj : object\n    The string representation is printed."},
    {"PrintNotification",
     ConsoleSingleton::sPyNotification,
     METH_VARARGS,
     "PrintNotification(obj) -> None\n\n"
     "Print a user notification to the output.\n\n"
     "obj : object\n    The string representation is printed."},
    {"PrintTranslatedNotification",
     ConsoleSingleton::sPyTranslatedNotification,
     METH_VARARGS,
     "PrintTranslatedNotification(obj) -> None\n\n"
     "Print an already translated notification to the output.\n\n"
     "obj : object\n    The string representation is printed."},
    {"SetStatus",
     ConsoleSingleton::sPySetStatus,
     METH_VARARGS,
     "SetStatus(observer, type, status) -> None\n\n"
     "Set the status for either 'Log', 'Msg', 'Wrn' or 'Error' for an observer.\n\n"
     "observer : str\n    Logging interface name.\n"
     "type : str\n    Message type.\n"
     "status : bool"},
    {"GetStatus",
     ConsoleSingleton::sPyGetStatus,
     METH_VARARGS,
     "GetStatus(observer, type) -> bool or None\n\n"
     "Get the status for either 'Log', 'Msg', 'Wrn' or 'Error' for an observer.\n"
     "Returns None if the specified observer doesn't exist.\n\n"
     "observer : str\n    Logging interface name.\n"
     "type : str\n    Message type."},
    {"GetObservers",
     ConsoleSingleton::sPyGetObservers,
     METH_VARARGS,
     "GetObservers() -> list of str\n\n"
     "Get the names of the current logging interfaces."},
    {"AttachObserver",
     ConsoleSingleton::sPyAttachObserver,
     METH_VARARGS,
     "AttachObserver(callable) -> None\n\n"
     "Attach a Python observer to the console. The callable is invoked as\n"
     "callable(notifier, message, level) for every console message, where level\n"
     "is one of 'Log', 'Message', 'Warning', 'Error', 'Critical', 'Notification'.\n"
     "It may be called from any thread (with the GIL held) and must not touch\n"
     "the GUI; console output from within the callable is not re-delivered.\n\n"
     "callable : a callable taking (str, str, str)."},
    {"DetachObserver",
     ConsoleSingleton::sPyDetachObserver,
     METH_VARARGS,
     "DetachObserver(callable) -> None\n\n"
     "Detach a Python observer previously attached with AttachObserver().\n\n"
     "callable : the same callable (or one comparing equal to it)."},
    {nullptr, nullptr, 0, nullptr} /* Sentinel */
};

namespace
{
PyObject* FC_PYCONSOLE_MSG(std::function<void(const char*, const char*)> func, PyObject* args)
{
    PyObject* output {};
    PyObject* notifier {};

    const char* notifierStr = "";

    auto retrieveString = [](PyObject* pystr) {
        PyObject* unicode = nullptr;

        const char* outstr = nullptr;

        if (PyUnicode_Check(pystr)) {
            outstr = PyUnicode_AsUTF8(pystr);
        }
        else {
            unicode = PyObject_Str(pystr);
            if (unicode) {
                outstr = PyUnicode_AsUTF8(unicode);
            }
        }
        Py_XDECREF(unicode);

        return outstr;
    };


    if (!PyArg_ParseTuple(args, "OO", &notifier, &output)) {
        PyErr_Clear();
        if (!PyArg_ParseTuple(args, "O", &output)) {
            return nullptr;
        }
    }
    else {  // retrieve notifier
        PY_TRY
        {
            notifierStr = retrieveString(notifier);
        }
        PY_CATCH
    }

    PY_TRY
    {
        const char* string = retrieveString(output);

        if (string) {
            static std::string buf;
            /*process message*/
            func(notifierStr, LogLevel::checkPyFrame(FC_LOG_INSTANCE.level(), string, buf));
        }
    }
    PY_CATCH
    Py_Return;
}
}  // namespace

PyObject* ConsoleSingleton::sPyMessage(PyObject* /*self*/, PyObject* args)
{
    return FC_PYCONSOLE_MSG(
        [](const std::string& notifier, const char* msg) {
            Instance()
                .Send<Base::LogStyle::Message,
                      Base::IntendedRecipient::Developer,
                      Base::ContentType::Untranslatable>(notifier, "%s", msg);
        },
        args);
}

PyObject* ConsoleSingleton::sPyWarning(PyObject* /*self*/, PyObject* args)
{
    return FC_PYCONSOLE_MSG(
        [](const std::string& notifier, const char* msg) {
            Instance().Warning(notifier, "%s", msg);
        },
        args);
}

PyObject* ConsoleSingleton::sPyDeveloperWarning(PyObject* /*self*/, PyObject* args)
{
    return FC_PYCONSOLE_MSG(
        [](const std::string& notifier, const char* msg) {
            Instance()
                .Send<Base::LogStyle::Warning,
                      Base::IntendedRecipient::Developer,
                      Base::ContentType::Untranslatable>(notifier, "%s", msg);
        },
        args);
}

PyObject* ConsoleSingleton::sPyUserWarning(PyObject* /*self*/, PyObject* args)
{
    return FC_PYCONSOLE_MSG(
        [](const std::string& notifier, const char* msg) {
            Instance()
                .Send<Base::LogStyle::Warning,
                      Base::IntendedRecipient::User,
                      Base::ContentType::Untranslated>(notifier, "%s", msg);
        },
        args);
}

PyObject* ConsoleSingleton::sPyTranslatedUserWarning(PyObject* /*self*/, PyObject* args)
{
    return FC_PYCONSOLE_MSG(
        [](const std::string& notifier, const char* msg) {
            Instance()
                .Send<Base::LogStyle::Warning,
                      Base::IntendedRecipient::User,
                      Base::ContentType::Translated>(notifier, "%s", msg);
        },
        args);
}

PyObject* ConsoleSingleton::sPyError(PyObject* /*self*/, PyObject* args)
{
    return FC_PYCONSOLE_MSG(
        [](const std::string& notifier, const char* msg) {
            Instance()
                .Send<Base::LogStyle::Error,
                      Base::IntendedRecipient::All,
                      Base::ContentType::Untranslated>(notifier, "%s", msg);
        },
        args);
}

PyObject* ConsoleSingleton::sPyDeveloperError(PyObject* /*self*/, PyObject* args)
{
    return FC_PYCONSOLE_MSG(
        [](const std::string& notifier, const char* msg) {
            Instance()
                .Send<Base::LogStyle::Error,
                      Base::IntendedRecipient::Developer,
                      Base::ContentType::Untranslatable>(notifier, "%s", msg);
        },
        args);
}

PyObject* ConsoleSingleton::sPyUserError(PyObject* /*self*/, PyObject* args)
{
    return FC_PYCONSOLE_MSG(
        [](const std::string& notifier, const char* msg) {
            Instance()
                .Send<Base::LogStyle::Error,
                      Base::IntendedRecipient::User,
                      Base::ContentType::Untranslated>(notifier, "%s", msg);
        },
        args);
}

PyObject* ConsoleSingleton::sPyTranslatedUserError(PyObject* /*self*/, PyObject* args)
{
    return FC_PYCONSOLE_MSG(
        [](const std::string& notifier, const char* msg) {
            Instance()
                .Send<Base::LogStyle::Error,
                      Base::IntendedRecipient::User,
                      Base::ContentType::Translated>(notifier, "%s", msg);
        },
        args);
}

PyObject* ConsoleSingleton::sPyLog(PyObject* /*self*/, PyObject* args)
{
    return FC_PYCONSOLE_MSG(
        [](const std::string& notifier, const char* msg) {
            Instance()
                .Send<Base::LogStyle::Log,
                      Base::IntendedRecipient::Developer,
                      Base::ContentType::Untranslatable>(notifier, "%s", msg);
        },
        args);
}

PyObject* ConsoleSingleton::sPyCritical(PyObject* /*self*/, PyObject* args)
{
    return FC_PYCONSOLE_MSG(
        [](const std::string& notifier, const char* msg) {
            Instance()
                .Send<Base::LogStyle::Critical,
                      Base::IntendedRecipient::All,
                      Base::ContentType::Untranslated>(notifier, "%s", msg);
        },
        args);
}

PyObject* ConsoleSingleton::sPyNotification(PyObject* /*self*/, PyObject* args)
{
    return FC_PYCONSOLE_MSG(
        [](const std::string& notifier, const char* msg) {
            Instance()
                .Send<Base::LogStyle::Notification,
                      Base::IntendedRecipient::User,
                      Base::ContentType::Untranslated>(notifier, "%s", msg);
        },
        args);
}

PyObject* ConsoleSingleton::sPyTranslatedNotification(PyObject* /*self*/, PyObject* args)
{
    return FC_PYCONSOLE_MSG(
        [](const std::string& notifier, const char* msg) {
            Instance()
                .Send<Base::LogStyle::Notification,
                      Base::IntendedRecipient::User,
                      Base::ContentType::Translated>(notifier, "%s", msg);
        },
        args);
}

PyObject* ConsoleSingleton::sPyGetStatus(PyObject* /*self*/, PyObject* args)
{
    char* pstr1 {};
    char* pstr2 {};
    if (!PyArg_ParseTuple(args, "ss", &pstr1, &pstr2)) {
        return nullptr;
    }

    PY_TRY
    {
        bool b = false;
        ILogger* pObs = Instance().Get(pstr1);
        if (!pObs) {
            Py_Return;
        }

        if (strcmp(pstr2, "Log") == 0) {
            b = pObs->bLog;
        }
        else if (strcmp(pstr2, "Wrn") == 0) {
            b = pObs->bWrn;
        }
        else if (strcmp(pstr2, "Msg") == 0) {
            b = pObs->bMsg;
        }
        else if (strcmp(pstr2, "Err") == 0) {
            b = pObs->bErr;
        }
        else if (strcmp(pstr2, "Critical") == 0) {
            b = pObs->bCritical;
        }
        else if (strcmp(pstr2, "Notification") == 0) {
            b = pObs->bNotification;
        }
        else {
            Py_Error(Base::PyExc_FC_GeneralError,
                     "Unknown message type (use 'Log', 'Err', 'Wrn', 'Msg', 'Critical' or "
                     "'Notification')");
        }

        return PyBool_FromLong(b ? 1 : 0);
    }
    PY_CATCH;
}

PyObject* ConsoleSingleton::sPySetStatus(PyObject* /*self*/, PyObject* args)
{
    char* pstr1 {};
    char* pstr2 {};
    PyObject* pyStatus {};
    if (!PyArg_ParseTuple(args, "ssO!", &pstr1, &pstr2, &PyBool_Type, &pyStatus)) {
        return nullptr;
    }

    PY_TRY
    {
        bool status = asBoolean(pyStatus);
        ILogger* pObs = Instance().Get(pstr1);
        if (pObs) {
            if (strcmp(pstr2, "Log") == 0) {
                pObs->bLog = status;
            }
            else if (strcmp(pstr2, "Wrn") == 0) {
                pObs->bWrn = status;
            }
            else if (strcmp(pstr2, "Msg") == 0) {
                pObs->bMsg = status;
            }
            else if (strcmp(pstr2, "Err") == 0) {
                pObs->bErr = status;
            }
            else if (strcmp(pstr2, "Critical") == 0) {
                pObs->bCritical = status;
            }
            else if (strcmp(pstr2, "Notification") == 0) {
                pObs->bNotification = status;
            }
            else {
                Py_Error(Base::PyExc_FC_GeneralError,
                         "Unknown message type (use 'Log', 'Err', 'Wrn', 'Msg', 'Critical' or "
                         "'Notification')");
            }

            Py_Return;
        }

        Py_Error(Base::PyExc_FC_GeneralError, "Unknown logger type");
    }
    PY_CATCH;
}

PyObject* ConsoleSingleton::sPyGetObservers(PyObject* /*self*/, PyObject* args)
{
    if (!PyArg_ParseTuple(args, "")) {
        return nullptr;
    }

    PY_TRY
    {
        Py::List list;
        // Snapshot rather than iterate live: another thread may be logging, and
        // Py::String can raise, which would leave the lock held.
        std::vector<ILogger*> observers;
        {
            std::lock_guard<std::mutex> guard(Instance()._observerMutex);
            observers.assign(Instance()._aclObservers.begin(), Instance()._aclObservers.end());
        }
        for (auto i : observers) {
            list.append(Py::String(i->Name() ? i->Name() : ""));
        }

        return Py::new_reference_to(list);
    }
    PY_CATCH
}

namespace
{
/** Forwards console messages to a Python callable.
 *  SendLog may fire on any thread, so the callable is invoked under the GIL
 *  only and must not touch the GUI. A per-thread guard drops messages emitted
 *  from inside the callable itself, so an observer that prints cannot recurse.
 */
class PyConsoleObserver: public ILogger
{
public:
    explicit PyConsoleObserver(PyObject* callable)
        : callback(callable)
    {
        Py_INCREF(callback);
        // receive every category; filtering is the callable's business
        bNotification = true;
    }

    ~PyConsoleObserver() override
    {
        PyGILStateLocker lock;
        Py_DECREF(callback);
    }

    const char* Name() override
    {
        return "PythonObserver";
    }

    void SendLog(const std::string& notifiername,
                 const std::string& msg,
                 LogStyle level,
                 IntendedRecipient /*recipient*/,
                 ContentType /*content*/) override
    {
        static thread_local bool reentrant = false;
        if (reentrant) {
            return;
        }
        reentrant = true;
        PyGILStateLocker lock;
        const char* levelname = nullptr;
        switch (level) {
            case LogStyle::Log:
                levelname = "Log";
                break;
            case LogStyle::Message:
                levelname = "Message";
                break;
            case LogStyle::Warning:
                levelname = "Warning";
                break;
            case LogStyle::Error:
                levelname = "Error";
                break;
            case LogStyle::Critical:
                levelname = "Critical";
                break;
            case LogStyle::Notification:
                levelname = "Notification";
                break;
        }
        PyObject* res =
            PyObject_CallFunction(callback, "sss", notifiername.c_str(), msg.c_str(), levelname);
        if (!res) {
            // report through sys.unraisablehook, never back into the console
            PyErr_WriteUnraisable(callback);
        }
        Py_XDECREF(res);
        reentrant = false;
    }

    PyObject* callback;
};

// live Python observers; only mutated from Python calls, i.e. under the GIL
std::list<PyConsoleObserver*> _pyObservers;
}  // namespace

PyObject* ConsoleSingleton::sPyAttachObserver(PyObject* /*self*/, PyObject* args)
{
    PyObject* callable {};
    if (!PyArg_ParseTuple(args, "O", &callable)) {
        return nullptr;
    }
    if (!PyCallable_Check(callable)) {
        PyErr_SetString(PyExc_TypeError, "AttachObserver: argument must be callable");
        return nullptr;
    }
    auto observer = new PyConsoleObserver(callable);
    _pyObservers.push_back(observer);
    Instance().AttachObserver(observer);
    Py_Return;
}

PyObject* ConsoleSingleton::sPyDetachObserver(PyObject* /*self*/, PyObject* args)
{
    PyObject* callable {};
    if (!PyArg_ParseTuple(args, "O", &callable)) {
        return nullptr;
    }
    // match by equality, not identity: bound methods are recreated per access
    for (auto it = _pyObservers.begin(); it != _pyObservers.end(); ++it) {
        int eq = PyObject_RichCompareBool((*it)->callback, callable, Py_EQ);
        if (eq < 0) {
            return nullptr;
        }
        if (eq) {
            // RetireObserver, not DetachObserver + delete: a worker thread may
            // be inside SendLog on this observer right now (TechDraw logs OCCT
            // failures from QtConcurrent threads), and freeing it under them
            // calls the Python callback through a dangling pointer.
            Instance().RetireObserver(*it);
            _pyObservers.erase(it);
            Py_Return;
        }
    }
    PyErr_SetString(PyExc_ValueError, "DetachObserver: observer not attached");
    return nullptr;
}

Base::ILogger::~ILogger() = default;
