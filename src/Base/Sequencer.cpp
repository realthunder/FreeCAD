/***************************************************************************
 *   Copyright (c) 2004 Werner Mayer <wmayer[at]users.sourceforge.net>     *
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
# include <QMutexLocker>
# include <QThread>
#endif

#include "Sequencer.h"
#include "Mutex.h"


using namespace Base;

namespace Base {

struct SequencerP {
    // members
    static QThread *_thread;
    static std::vector<SequencerBase*> _instances; /**< A vector of all created instances */
    static std::vector<SequencerLauncher*> _launchers;
    static SequencerLauncher* _topLauncher; /**< The outermost launcher */
    static QRecursiveMutex mutex; /**< A mutex-locker for the launcher */
    /** Sets a global sequencer object.
        * Access to the last registered object is performed by @see Sequencer().
        */
    static void appendInstance (SequencerBase* s)
    {
        if (!_thread) {
            _thread = QThread::currentThread();
        }
        _instances.push_back(s);
    }
    static void removeInstance (SequencerBase* s)
    {
        std::vector<SequencerBase*>::iterator it;
        it = std::find(_instances.begin(), _instances.end(), s);
        _instances.erase(it);
    }
    static SequencerBase& getInstance ()
    {
        return *_instances.back();
    }
    static void findNextLauncher(SequencerLauncher *exclude=nullptr)
    {
        auto laucherSave = _topLauncher;
        _topLauncher = nullptr;
        for (int pass = 0; pass < 2; ++pass) {
            for (size_t i=0; i<_launchers.size(); ++i) {
                auto launcher = _launchers[_launchers.size()-1-i];
                if (launcher == exclude)
                    continue;
                // First pass, only pick blocking sequencer
                if (pass==0 && !launcher->isBlocking())
                    continue;
                if (launcher->start()) {
                    _topLauncher = launcher;
                    return;
                }
            }
        }
        if (std::find(_launchers.begin(), _launchers.end(), laucherSave) != _launchers.end())
            _topLauncher = laucherSave;
        else if (!_launchers.empty())
            _topLauncher = _launchers.back();
    }
};

/**
    * The _instances member just stores the pointer of the
    * all instantiated SequencerBase objects.
    */
std::vector<SequencerBase*> SequencerP::_instances;
std::vector<SequencerLauncher*> SequencerP::_launchers;
SequencerLauncher* SequencerP::_topLauncher = nullptr;
QRecursiveMutex SequencerP::mutex;
QThread *SequencerP::_thread = nullptr;
}  // namespace Base

SequencerBase& SequencerBase::Instance()
{
    // not initialized?
    if (SequencerP::_instances.empty()) {
        new ConsoleSequencer();
    }

    return SequencerP::getInstance();
}

SequencerBase::SequencerBase()
{
    SequencerP::appendInstance(this);
}

SequencerBase::~SequencerBase()
{
    SequencerP::removeInstance(this);
}

bool SequencerBase::start(const char* pszStr, size_t steps)
{
    // reset current state of progress (in percent)
    this->_nLastPercentage = -1;

    this->nTotalSteps = steps;
    this->nProgress = 0;
    this->_bCanceled = false;

    setText(pszStr);

    // reimplemented in sub-classes
    if (!this->_bLocked) {
        bool blocking = true;
        {
            QMutexLocker locker(&SequencerP::mutex);
            if (SequencerP::_topLauncher)
                blocking = SequencerP::_topLauncher->isBlocking();
        }
        startStep(blocking);
    }

    return true;
}

size_t SequencerBase::numberOfSteps() const
{
    return this->nTotalSteps;
}

void SequencerBase::setTotalSteps(size_t steps)
{
    this->nTotalSteps = steps;
}

void SequencerBase::startStep(bool)
{
}

bool SequencerBase::next(bool canAbort)
{
    this->nProgress++;
    float fDiv = this->nTotalSteps > 0 ? static_cast<float>(this->nTotalSteps) : 1000.0F;
    int perc = int((float(this->nProgress) * (100.0F / fDiv)));

    // do only an update if we have increased by one percent
    if (perc > this->_nLastPercentage) {
        this->_nLastPercentage = perc;

        // if not locked
        if (!this->_bLocked) {
            nextStep(canAbort);
        }
    }

    return this->nProgress < this->nTotalSteps;
}

void SequencerBase::nextStep(bool /*next*/)
{}

void SequencerBase::setProgress(size_t /*value*/)
{}

bool SequencerBase::stop()
{
    resetData();
    return true;
}

void SequencerBase::pause()
{}

void SequencerBase::resume()
{}

bool SequencerBase::isBlocking() const
{
    return true;
}

bool SequencerBase::setLocked(bool bLocked)
{
    QMutexLocker locker(&SequencerP::mutex);
    bool old = this->_bLocked;
    this->_bLocked = bLocked;
    return old;
}

bool SequencerBase::isLocked() const
{
    QMutexLocker locker(&SequencerP::mutex);
    return this->_bLocked;
}

bool SequencerBase::isRunning() const
{
    QMutexLocker locker(&SequencerP::mutex);
    return (SequencerP::_topLauncher != nullptr);
}

bool SequencerBase::wasCanceled() const
{
    QMutexLocker locker(&SequencerP::mutex);
    return this->_bCanceled;
}

void SequencerBase::tryToCancel()
{
    this->_bCanceled = true;
}

void SequencerBase::rejectCancel()
{
    this->_bCanceled = false;
}

int SequencerBase::progressInPercent() const
{
    return this->_nLastPercentage;
}

void SequencerBase::resetData()
{
    this->_bCanceled = false;
}

void SequencerBase::setText(const char* /*text*/)
{}

// ---------------------------------------------------------

using Base::ConsoleSequencer;

void ConsoleSequencer::setText(const char* pszTxt)
{
    printf("%s...\n", pszTxt);
}

void ConsoleSequencer::startStep(bool)
{
}

void ConsoleSequencer::nextStep(bool /*canAbort*/)
{
    if (this->nTotalSteps != 0) {
        printf("\t\t\t\t\t\t(%d %%)\t\r", progressInPercent());
    }
}

void ConsoleSequencer::resetData()
{
    SequencerBase::resetData();
    printf("\t\t\t\t\t\t\t\t\r");
}

// ---------------------------------------------------------

SequencerLauncher::SequencerLauncher(const char* pszStr, size_t steps)
{
    strText = pszStr ? pszStr : "";
    nTotalSteps = steps;

    QMutexLocker locker(&SequencerP::mutex);
    // Have we already an instance of SequencerLauncher created?
    SequencerP::_launchers.push_back(this);
    if (steps != 0)
        SequencerP::findNextLauncher();
}

SequencerLauncher::~SequencerLauncher()
{
    QMutexLocker locker(&SequencerP::mutex);
    auto &launchers = SequencerP::_launchers;
    auto &topLauncher = SequencerP::_topLauncher;
    auto it = std::find(launchers.begin(), launchers.end(), this);
    if (it != launchers.end())
        launchers.erase(it);
    if (topLauncher == this) {
        SequencerBase::Instance().stop();
        topLauncher = nullptr;
        SequencerP::findNextLauncher();
    }
}

bool SequencerLauncher::start(size_t steps, const char* pszTxt)
{
    QMutexLocker locker(&SequencerP::mutex);
    if (pszTxt)
        strText = pszTxt;
    if (steps)
        nTotalSteps = steps;
    if (nTotalSteps == 0)
        return false;
    if (SequencerP::_topLauncher == nullptr)
        SequencerP::_topLauncher = this;
    if (SequencerP::_topLauncher == this) {
        if (progress() == 0)
            bBlocking = (QThread::currentThread() == SequencerP::_thread);
        return SequencerBase::Instance().start(strText.c_str(), steps);
    }
    return false;
}

bool SequencerLauncher::stop()
{
    QMutexLocker locker(&SequencerP::mutex);
    if (SequencerP::_topLauncher == this) {
        SequencerBase::Instance().stop();
        SequencerP::findNextLauncher(this);
        return true;
    }
    return false;
}

void SequencerLauncher::setText(const char* pszTxt)
{
    QMutexLocker locker(&SequencerP::mutex);
    strText = pszTxt ? pszTxt : "";
    if (SequencerP::_topLauncher == this)
        SequencerBase::Instance().setText(pszTxt);
}

bool SequencerLauncher::next(bool canAbort)
{
    QMutexLocker locker(&SequencerP::mutex);
    this->nProgress++;
    if (SequencerP::_topLauncher != this) {
        if (canAbort) {
            if (bNoException) {
                if (bCanceled)
                    return false;
                try {
                    SequencerBase::Instance().checkAbort();
                } catch (Base::Exception &) {
                    return false;
                } catch (...) {
                    return false;
                }
            }
            if (bCanceled)
                throw Base::AbortException();
            SequencerBase::Instance().checkAbort();
        }
        return true; // ignore
    }
    return SequencerBase::Instance().next(canAbort);
}

void SequencerLauncher::setProgress(size_t pos)
{
    QMutexLocker locker(&SequencerP::mutex);
    if (bCanceled) {
        if (bNoException)
            return;
        throw Base::AbortException();
    }
    this->nProgress = pos;
    if (SequencerP::_topLauncher == this)
        SequencerBase::Instance().setProgress(pos);
}

void SequencerLauncher::setTotalSteps(size_t steps)
{
    QMutexLocker locker(&SequencerP::mutex);
    this->nTotalSteps = steps;
    if (SequencerP::_topLauncher == this)
        SequencerBase::Instance().setTotalSteps(steps);
}

size_t SequencerLauncher::numberOfSteps() const
{
    return nTotalSteps;
}

size_t SequencerLauncher::progress() const
{
    return this->nProgress;
}

bool SequencerLauncher::wasCanceled() const
{
    QMutexLocker locker(&SequencerP::mutex);
    return bCanceled || SequencerBase::Instance().wasCanceled();
}

void SequencerLauncher::setCanceled(bool cancel)
{
    QMutexLocker locker(&SequencerP::mutex);
    bCanceled = cancel;
}

void SequencerLauncher::setNoException(bool enable)
{
    QMutexLocker locker(&SequencerP::mutex);
    bNoException = enable;
}
