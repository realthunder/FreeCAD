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
# include <algorithm>
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
    static std::atomic<SequencerLauncher*> _topLauncher; /**< The outermost launcher */
    static std::atomic<size_t> _activeCount; /**< Lock-free count of live launchers */
    static QRecursiveMutex mutex; /**< A mutex-locker for the launcher */
    /** True if the active indicator is poll-driven, so worker-thread ticks
     * need not push into it. Read without the mutex: _instances only changes
     * at application start/shutdown.
     */
    static bool updatesViaPoll()
    {
        return !_instances.empty() && _instances.back()->updatesViaPoll();
    }
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
        auto laucherSave = _topLauncher.load();
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
std::atomic<SequencerLauncher*> SequencerP::_topLauncher {nullptr};
std::atomic<size_t> SequencerP::_activeCount {0};
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
            if (auto top = SequencerP::_topLauncher.load())
                blocking = top->isBlocking();
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
    return (SequencerP::_topLauncher.load(std::memory_order_relaxed) != nullptr);
}

bool SequencerBase::wasCanceled() const
{
    return this->_bCanceled.load(std::memory_order_relaxed);
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
    // Per-item indicators (one launcher per shape read, say) restart the
    // console sequencer with the same text thousands of times; only a
    // change is worth a line.
    std::string text = pszTxt ? pszTxt : "";
    if (text.empty() || text == _lastText)
        return;
    _lastText = std::move(text);
    printf("%s...\n", _lastText.c_str());
}

void ConsoleSequencer::startStep(bool)
{
}

void ConsoleSequencer::nextStep(bool /*canAbort*/)
{
    if (this->nTotalSteps != 0) {
        printf("\t\t\t\t\t\t(%d %%)\t\r", progressInPercent());
        _printed = true;
    }
}

void ConsoleSequencer::resetData()
{
    SequencerBase::resetData();
    if (_printed) {
        _printed = false;
        printf("\t\t\t\t\t\t\t\t\r");
    }
}

// ---------------------------------------------------------

SequencerLauncher::SequencerLauncher(const char* pszStr, size_t steps)
{
    strText = pszStr ? pszStr : "";
    nTotalSteps = steps;
    ownerThread = QThread::currentThread();

    QMutexLocker locker(&SequencerP::mutex);
    // Have we already an instance of SequencerLauncher created?
    SequencerP::_launchers.push_back(this);
    SequencerP::_activeCount.fetch_add(1, std::memory_order_relaxed);
    if (steps != 0) {
        // Re-probing the top on every construction restarts the running
        // indicator each time — per-item churn when code stacks a launcher
        // per work item. Only look for a new top when this launcher could
        // actually take over: no top yet, or a blocking (main-thread)
        // launcher arriving over a non-blocking top.
        auto top = SequencerP::_topLauncher.load();
        if (!top
                || (!top->isBlocking() && ownerThread == SequencerP::_thread))
            SequencerP::findNextLauncher();
    }
}

SequencerLauncher::~SequencerLauncher()
{
    QMutexLocker locker(&SequencerP::mutex);
    auto &launchers = SequencerP::_launchers;
    auto &topLauncher = SequencerP::_topLauncher;
    auto it = std::find(launchers.begin(), launchers.end(), this);
    if (it != launchers.end()) {
        launchers.erase(it);
        SequencerP::_activeCount.fetch_sub(1, std::memory_order_relaxed);
    }
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
        return SequencerBase::Instance().start(strText.c_str(), nTotalSteps);
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
    // When poll-driven, worker-thread text changes are picked up by the next
    // snapshot; don't push them into the indicator (a blocking queued call).
    if (SequencerP::_topLauncher == this
            && (QThread::currentThread() == SequencerP::_thread
                || !SequencerP::updatesViaPoll()))
        SequencerBase::Instance().setText(pszTxt);
}

bool SequencerLauncher::next(bool canAbort)
{
    this->nProgress.fetch_add(1, std::memory_order_relaxed);

    // Worker-thread ticks stay lock-free whenever they don't have to drive
    // the indicator: either another launcher is on top, or the indicator is
    // poll-driven (SequencerManager) and reads the atomic counters itself.
    // This also makes it safe and cheap to share one launcher between many
    // worker threads (e.g. a parallel recompute).
    if (QThread::currentThread() != SequencerP::_thread
            && (SequencerP::_topLauncher.load(std::memory_order_acquire) != this
                || SequencerP::updatesViaPoll())) {
        if (canAbort && bCanceled.load(std::memory_order_relaxed)) {
            if (bNoException)
                return false;
            throw Base::AbortException();
        }
        return true;
    }

    QMutexLocker locker(&SequencerP::mutex);
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
    if (bCanceled.load(std::memory_order_relaxed)) {
        if (bNoException)
            return;
        throw Base::AbortException();
    }
    this->nProgress.store(pos, std::memory_order_relaxed);

    // Same lock-free rule as in next()
    if (QThread::currentThread() != SequencerP::_thread
            && (SequencerP::_topLauncher.load(std::memory_order_acquire) != this
                || SequencerP::updatesViaPoll()))
        return;

    QMutexLocker locker(&SequencerP::mutex);
    if (SequencerP::_topLauncher == this)
        SequencerBase::Instance().setProgress(pos);
}

void SequencerLauncher::setTotalSteps(size_t steps)
{
    this->nTotalSteps.store(steps, std::memory_order_relaxed);

    if (QThread::currentThread() != SequencerP::_thread
            && (SequencerP::_topLauncher.load(std::memory_order_acquire) != this
                || SequencerP::updatesViaPoll()))
        return;

    QMutexLocker locker(&SequencerP::mutex);
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
    // Lock-free: cheap enough for worker loops to poll every iteration
    return bCanceled.load(std::memory_order_relaxed)
        || SequencerBase::Instance().wasCanceled();
}

void SequencerLauncher::setCanceled(bool cancel)
{
    bCanceled.store(cancel, std::memory_order_relaxed);
}

void SequencerLauncher::setNoException(bool enable)
{
    QMutexLocker locker(&SequencerP::mutex);
    bNoException = enable;
}

// ---------------------------------------------------------

size_t SequencerManager::activeCount()
{
    return SequencerP::_activeCount.load(std::memory_order_relaxed);
}

SequencerManager::Snapshot SequencerManager::snapshot(size_t maxLevels)
{
    Snapshot snap;
    if (maxLevels == 0)
        maxLevels = 1;

    // Launchers are stack objects, so per thread their registration order in
    // _launchers is their nesting order. Bucket them per thread (preserving
    // the threads' first-appearance order) so each root is directly followed
    // by its nested sequences.
    struct ThreadEntry {
        QThread* thread;
        std::vector<Info> infos;
    };
    std::vector<ThreadEntry> threads;

    QMutexLocker locker(&SequencerP::mutex);
    for (auto *launcher : SequencerP::_launchers) {
        Info info;
        info.progress = launcher->nProgress.load(std::memory_order_relaxed);
        info.total = launcher->nTotalSteps.load(std::memory_order_relaxed);
        if (info.total == 0 && info.progress == 0)
            continue; // registered but not started: invisible, doesn't consume a level
        auto it = std::find_if(threads.begin(), threads.end(),
            [launcher](const ThreadEntry& entry) {
                return entry.thread == launcher->ownerThread;
            });
        if (it == threads.end()) {
            threads.push_back({launcher->ownerThread, {}});
            it = threads.end() - 1;
        }
        if (it->infos.size() >= maxLevels)
            continue;
        info.depth = it->infos.size();
        info.text = launcher->strText;
        info.mainThread = (launcher->ownerThread == SequencerP::_thread);
        it->infos.push_back(std::move(info));
    }

    for (auto& entry : threads) {
        const Info& root = entry.infos.front();
        ++snap.roots;
        if (root.total > 0) {
            snap.total += root.total;
            snap.progress += std::min(root.progress, root.total);
        }
        for (auto& info : entry.infos)
            snap.sequences.push_back(std::move(info));
    }
    return snap;
}
