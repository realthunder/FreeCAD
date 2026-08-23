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
# include <atomic>
# include <climits>
# include <QApplication>
# include <QElapsedTimer>
# include <QGridLayout>
# include <QKeyEvent>
# include <QLabel>
# include <QMessageBox>
# include <QMetaObject>
# include <QThread>
# include <QTime>
# include <QTimer>
# include <QWindow>
#endif

#include <App/Application.h>
#include <Base/Parameter.h>

#include "ProgressBar.h"
#include "LiveViewInteraction.h"
#include "MainWindow.h"
#include "ProgressDialog.h"
#include "WaitCursor.h"


using namespace Gui;


namespace Gui {

/** How many nesting levels per thread the detail popup shows. */
static size_t progressDetailLevels()
{
    ParameterGrp::handle hGrp = App::GetApplication().GetParameterGroupByPath(
        "User parameter:BaseApp/Preferences/General");
    long levels = hGrp->GetInt("ProgressDetailLevels", 5);
    return levels < 1 ? 1 : (size_t)levels;
}

/** Frameless tool-tip style popup showing one live progress bar per running
 * sequence — nested sequences indented under their root, one hierarchy per
 * thread — plus a consolidated total row. It is refreshed by the owning
 * ProgressBar's aggregate poll while visible, so the bars keep ticking.
 */
class ProgressDetailPopup: public QWidget
{
public:
    explicit ProgressDetailPopup(QWidget* anchor)
        : QWidget(anchor->window(), Qt::ToolTip)
        , anchor(anchor)
    {
        setAttribute(Qt::WA_ShowWithoutActivating);
        setAttribute(Qt::WA_TransparentForMouseEvents);
        auto grid = new QGridLayout(this);
        grid->setContentsMargins(10, 8, 10, 8);
        grid->setHorizontalSpacing(10);
        grid->setVerticalSpacing(4);
    }

    void updateSnapshot(const Base::SequencerManager::Snapshot& snap)
    {
        int rows = (int)snap.sequences.size();
        // a consolidated row only carries information with parallel roots
        bool totalRow = snap.roots > 1 && snap.total > 0;
        ensureRows(rows + (totalRow ? 1 : 0));
        for (int i = 0; i < rows; ++i) {
            const auto& info = snap.sequences[i];
            setRow(i, QString::fromUtf8(info.text.c_str()), info.progress, info.total,
                   info.depth);
        }
        if (totalRow)
            setRow(rows, ProgressBar::tr("Total"), snap.progress, snap.total, 0);
        adjustSize();
        // anchored above the status-bar progress bar, right aligned
        QPoint corner = anchor->mapToGlobal(QPoint(anchor->width(), 0));
        move(std::max(0, corner.x() - width()), corner.y() - height() - 8);
    }

private:
    void ensureRows(int count)
    {
        auto grid = static_cast<QGridLayout*>(layout());
        while ((int)labels.size() < count) {
            int row = (int)labels.size();
            auto label = new QLabel(this);
            auto bar = new QProgressBar(this);
            bar->setFixedWidth(140);
            bar->setAlignment(Qt::AlignHCenter);
            grid->addWidget(label, row, 0);
            grid->addWidget(bar, row, 1);
            labels.push_back(label);
            bars.push_back(bar);
        }
        while ((int)labels.size() > count) {
            delete labels.back(); labels.pop_back();
            delete bars.back(); bars.pop_back();
        }
    }

    void setRow(int row, const QString& text, size_t progress, size_t total, size_t depth)
    {
        QLabel* label = labels[row];
        label->setIndent((int)depth * 14);
        QFontMetrics fm(label->font());
        label->setText(fm.elidedText(text, Qt::ElideMiddle, 260));
        QProgressBar* bar = bars[row];
        if (total > 0) {
            int t = (int)std::min<size_t>(total, INT_MAX);
            int p = (int)std::min<size_t>(std::min(progress, total), INT_MAX);
            if (bar->maximum() != t || bar->minimum() != 0)
                bar->setRange(0, t);
            bar->setValue(p);
            bar->setFormat(QStringLiteral("%v / %m"));
        }
        else if (bar->maximum() != 0 || bar->minimum() != 0) {
            bar->setRange(0, 0); // unknown total: busy indicator
        }
    }

    QWidget* anchor;
    std::vector<QLabel*> labels;
    std::vector<QProgressBar*> bars;
};

struct SequencerBarPrivate
{
    ProgressBar* bar;
    WaitCursor* waitCursor;
    QElapsedTimer measureTime;
    QElapsedTimer progressTime;
    QElapsedTimer checkAbortTime;
    QString text;
    bool guiThread;
    /** true while the aggregate poll owns the bar's value/range */
    std::atomic<bool> aggregateDriven {false};
    /** true between the first startStep and the deferred teardown */
    std::atomic<bool> engaged {false};
    /** a stop happened; teardown runs when the grace period passes */
    std::atomic<bool> teardownPending {false};
    /** the app event filter is installed and must be removed at teardown */
    std::atomic<bool> filterHeld {false};
    /** pause() pushed an override cursor that resume() must pop */
    bool cursorPaused {false};
};

struct ProgressBarPrivate
{
    QTimer* delayShowTimer;
    QTimer* pollTimer = nullptr;
    QTimer* teardownTimer = nullptr;
    QElapsedTimer pollActive;
    ProgressDetailPopup* detailPopup = nullptr;
    QString statusText;
    int minimumDuration;
    int observeEventFilter;

    bool isModalDialog(QObject* o) const
    {
        QWidget* parent = qobject_cast<QWidget*>(o);
        if (!parent) {
            QWindow* window = qobject_cast<QWindow*>(o);
            if (window)
                parent = QWidget::find(window->winId());
        }
        while (parent) {
            auto* dlg = qobject_cast<QMessageBox*>(parent);
            if (dlg && dlg->isModal())
                return true;
            auto* pd = qobject_cast<QProgressDialog*>(parent);
            if (pd)
                return true;
            parent = parent->parentWidget();
        }

        return false;
    }
};
}

SequencerBar* SequencerBar::_pclSingleton = nullptr;

SequencerBar* SequencerBar::instance()
{
    // not initialized?
    if (!_pclSingleton)
    {
        _pclSingleton = new SequencerBar();
    }

    return _pclSingleton;
}

SequencerBar::SequencerBar()
{
    d = new SequencerBarPrivate;
    d->bar = nullptr;
    d->waitCursor = nullptr;
    d->guiThread = true;
}

SequencerBar::~SequencerBar()
{
    delete d;
}

void SequencerBar::pause()
{
    QThread *currentThread = QThread::currentThread();
    QThread *thr = d->bar->thread(); // this is the main thread
    d->bar->leaveControlEvents(d->guiThread);
    if (thr != currentThread)
        return;

    // allow key handling of dialog and restore cursor. There is a wait
    // cursor only if the running sequence asked the indicator to take the
    // UI (a worker-thread sequence, or a KeepInteractive one, never does),
    // and pause/resume must push and pop the override cursor in pairs --
    // hence the flag rather than a second test of the pointer, which the
    // dialog's own event pumping can invalidate in between.
    if (d->waitCursor) {
        d->waitCursor->restoreCursor();
        QApplication::setOverrideCursor(Qt::ArrowCursor);
        d->cursorPaused = true;
    }
}

void SequencerBar::resume()
{
    QThread *currentThread = QThread::currentThread();
    QThread *thr = d->bar->thread(); // this is the main thread
    if (thr == currentThread && d->cursorPaused) {
        d->cursorPaused = false;
        QApplication::restoreOverrideCursor();
        if (d->waitCursor)
            d->waitCursor->setWaitCursor();
    }

    // must be called as last to get control before WaitCursor
    d->bar->enterControlEvents(d->guiThread); // grab again
}

void SequencerBar::startStep(bool blocking)
{
    QThread *currentThread = QThread::currentThread();
    QThread *thr = d->bar->thread(); // this is the main thread

    // A start within the teardown grace period (or while the poll owns the
    // bar) reuses the still-engaged indicator: no cursor / event-filter /
    // status-bar churn. This is what makes a launcher per work item cheap.
    bool cheap = d->engaged.load(std::memory_order_relaxed)
        && (d->teardownPending.exchange(false, std::memory_order_relaxed)
            || d->aggregateDriven.load(std::memory_order_relaxed));

    if (thr != currentThread) {
        d->guiThread = blocking;
        d->progressTime.start();
        d->checkAbortTime.start();
        d->measureTime.start();
        if (!cheap) {
            QMetaObject::invokeMethod(d->bar, "setRangeEx", Qt::QueuedConnection,
                Q_ARG(int, 0), Q_ARG(int, (int)nTotalSteps));
            QMetaObject::invokeMethod(d->bar, "aboutToShow", Qt::QueuedConnection);
            d->bar->enterControlEvents(d->guiThread);
            if (d->guiThread)
                d->filterHeld.store(true, std::memory_order_relaxed);
            d->engaged.store(true, std::memory_order_relaxed);
        }
    }
    else {
        // Not "the main thread is running this" but "this sequence owns the
        // main thread": a sequence sliced across event-loop turns
        // (SequencerLauncher::KeepInteractive) reports from here without the
        // wait cursor and without the input grab, because the UI it reports
        // for is meant to stay usable. isBlocking() carries the same answer
        // on to the 3D viewer's own filter.
        d->guiThread = blocking;
        d->progressTime.start();
        d->checkAbortTime.start();
        d->measureTime.start();
        if (!cheap) {
            d->bar->setRangeEx(0, (int)nTotalSteps);
            d->engaged.store(true, std::memory_order_relaxed);
            showRemainingTime();
            d->bar->aboutToShow();
        }
        // The engaged indicator is reusable; a claim on the input is not
        // inheritable. A KeepInteractive sequence makes none, so a blocking
        // sequence starting behind one has to make its own -- and one that
        // follows another blocking sequence finds the claim already held.
        if (blocking && !d->filterHeld.exchange(true, std::memory_order_relaxed)) {
            if (!d->waitCursor)
                d->waitCursor = new Gui::WaitCursor;
            d->bar->enterControlEvents(true);
        }
    }
    // From now on the aggregate poll owns the bar; it stops itself (and
    // clears aggregateDriven) once no sequence is left running.
    QMetaObject::invokeMethod(d->bar, "startAggregatePoll", Qt::QueuedConnection);
}

void SequencerBar::checkAbort()
{
    if (d->bar->thread() != QThread::currentThread())
        return;
    if (!wasCanceled()) {
        if(d->checkAbortTime.elapsed() < 500)
            return;
        d->checkAbortTime.restart();
        qApp->processEvents();
        return;
    }
    // restore cursor
    pause();
    bool ok = d->bar->canAbort();
    // continue and show up wait cursor if needed
    resume();

    // force to abort the operation
    if ( ok ) {
        throw Base::AbortException();
    } else {
        rejectCancel();
    }
}

void SequencerBar::nextStep(bool canAbort)
{
    QThread *currentThread = QThread::currentThread();
    QThread *thr = d->bar->thread(); // this is the main thread
    if (thr != currentThread) {
        if (wasCanceled() && canAbort) {
            abort();
        }
        else {
            setValue((int)nProgress + 1);
        }
    }
    else {
        if (wasCanceled() && canAbort) {
            // restore cursor
            pause();
            bool ok = d->bar->canAbort();
            // continue and show up wait cursor if needed
            resume();

            // force to abort the operation
            if ( ok ) {
                abort();
            } else {
                rejectCancel();
                setValue((int)nProgress+1);
            }
        }
        else {
            setValue((int)nProgress+1);
        }
    }
}

void SequencerBar::setProgress(size_t step)
{
    // While the poll owns the bar, its visibility follows the poll's own
    // minimum-duration rule instead of being forced per call (OCCT
    // indicators call this on every Show()).
    if (!d->aggregateDriven.load(std::memory_order_relaxed)) {
        QThread* currentThread = QThread::currentThread();
        QThread* thr = d->bar->thread(); // this is the main thread
        if (thr != currentThread) {
            QMetaObject::invokeMethod(d->bar, "show", Qt::QueuedConnection);
        }
        else {
            d->bar->show();
        }
    }

    setValue((int)step);
}

void SequencerBar::setValue(int step)
{
    if (d->aggregateDriven.load(std::memory_order_relaxed)) {
        // The aggregate poll owns the bar's value; here only keep the event
        // pumping alive so a blocking main-thread sequence stays responsive
        // (which is also what fires the poll timer).
        if (QThread::currentThread() == d->bar->thread()) {
            int elapsed = d->progressTime.elapsed();
            if (elapsed > 200) {
                d->progressTime.restart();
                if (d->bar->isVisible())
                    showRemainingTime();
                d->bar->resetObserveEventFilter();
                qApp->processEvents();
            }
        }
        return;
    }

    QThread *currentThread = QThread::currentThread();
    QThread *thr = d->bar->thread(); // this is the main thread
    // if number of total steps is unknown then increment only by one
    if (nTotalSteps == 0) {
        int elapsed = d->progressTime.elapsed();
        // allow an update every 200 milliseconds only
        if (elapsed > 200) {
            d->progressTime.restart();
            if (thr != currentThread) {
                QMetaObject::invokeMethod(d->bar, "setValueEx", Qt::/*Blocking*/QueuedConnection,
                    Q_ARG(int,d->bar->value()+1));
            }
            else {
                d->bar->setValueEx(d->bar->value()+1);
                qApp->processEvents();
            }
        }
    }
    else {
        int elapsed = d->progressTime.elapsed();
        // allow an update every 200 milliseconds only
        if (elapsed > 200) {
            d->progressTime.restart();
            if (thr != currentThread) {
                QMetaObject::invokeMethod(d->bar, "setValueEx", Qt::/*Blocking*/QueuedConnection,
                Q_ARG(int,step));
                if (d->bar->isVisible())
                    showRemainingTime();
            }
            else {
                d->bar->setValueEx(step);
                if (d->bar->isVisible())
                    showRemainingTime();
                d->bar->resetObserveEventFilter();
                qApp->processEvents();
            }
        }
    }
}

void SequencerBar::setTotalSteps(size_t steps)
{
    SequencerBase::setTotalSteps(steps);
    if (d->aggregateDriven.load(std::memory_order_relaxed))
        return; // the aggregate poll owns the bar's range
    QThread *currentThread = QThread::currentThread();
    QThread *thr = d->bar->thread(); // this is the main thread
    if (thr != currentThread) {
        QMetaObject::invokeMethod(d->bar, "setRangeEx", Qt::QueuedConnection,
            Q_ARG(int, 0), Q_ARG(int, (int)nTotalSteps));
    }
    else {
        d->bar->setRangeEx(0, (int)nTotalSteps);
    }
}

void SequencerBar::showRemainingTime()
{
    QThread *currentThread = QThread::currentThread();
    QThread *thr = d->bar->thread(); // this is the main thread

    int elapsed = d->measureTime.elapsed();
    int progress = d->bar->value();
    int totalSteps = d->bar->maximum() - d->bar->minimum();

    QString txt = d->text;
    // More than 5 percent complete or more than 5 secs have elapsed.
    if (progress * 20 > totalSteps || elapsed > 5000) {
        int rest = (int) ( (double) totalSteps/progress * elapsed ) - elapsed;

        // more than 1 secs have elapsed and at least 100 ms are remaining
        if (elapsed > 1000 && rest > 100) {
            QTime time( 0,0, 0);
            time = time.addSecs( rest/1000 );
            QString remain = Gui::ProgressBar::tr("Remaining: %1").arg(time.toString());
            QString status = QStringLiteral("%1\t[%2]").arg(txt, remain);

            if (thr != currentThread) {
                QMetaObject::invokeMethod(getMainWindow(), "showMessage",
                    Qt::/*Blocking*/QueuedConnection,
                    Q_ARG(QString,status));
            }
            else {
                getMainWindow()->showMessage(status);
                d->bar->setToolTip(status);
            }
        }
    }
}

void SequencerBar::resetData()
{
    // The UI teardown is deferred behind a grace period so that per-item
    // start/stop cycles (e.g. one brep-import indicator per shape) reuse
    // the engaged indicator instead of thrashing the wait cursor, the app
    // event filter and the status bar. finishAggregate() runs it once
    // nothing has restarted within the grace period.
    if (d->engaged.load(std::memory_order_relaxed)) {
        d->teardownPending.store(true, std::memory_order_relaxed);
        QMetaObject::invokeMethod(d->bar, "armAggregateTeardown",
            Qt::QueuedConnection);
    }
    SequencerBase::resetData();
}

void SequencerBar::finishAggregate(bool force)
{
    if (!d->engaged.load(std::memory_order_relaxed))
        return;
    if (!force) {
        if (!d->teardownPending.load(std::memory_order_relaxed))
            return;
        if (Base::SequencerManager::activeCount() > 0)
            return; // something restarted; its stop re-arms the grace timer
    }
    d->teardownPending.store(false, std::memory_order_relaxed);
    d->engaged.store(false, std::memory_order_relaxed);
    d->bar->resetEx();
    d->bar->aboutToHide();
    delete d->waitCursor;
    d->waitCursor = nullptr;
    if (d->filterHeld.exchange(false, std::memory_order_relaxed))
        d->bar->leaveControlEvents(true);
    if (getMainWindow()) {
        getMainWindow()->setPaneText(1, QString());
        getMainWindow()->showMessage(QString());
    }
}

void SequencerBar::abort()
{
    //resets
    resetData();
    Base::AbortException exc("User aborted");
    throw exc;
}

void SequencerBar::setText (const char* pszTxt)
{
    QThread *currentThread = QThread::currentThread();
    QThread *thr = d->bar->thread(); // this is the main thread

    // print message to the statusbar
    d->text = pszTxt ? QString::fromUtf8(pszTxt) : QStringLiteral("");
    if (d->aggregateDriven.load(std::memory_order_relaxed))
        return; // the aggregate poll mirrors sequence texts on change
    if (thr != currentThread) {
        QMetaObject::invokeMethod(getMainWindow(), "showMessage",
            Qt::/*Blocking*/QueuedConnection,
            Q_ARG(QString,d->text));
    }
    else {
        getMainWindow()->showMessage(d->text);
    }
}

bool SequencerBar::isBlocking() const
{
    return d->guiThread;
}

bool SequencerBar::updatesViaPoll() const
{
    return d->aggregateDriven.load(std::memory_order_relaxed);
}

QProgressBar* SequencerBar::getProgressBar(QWidget* parent)
{
    if (!d->bar)
        d->bar = new ProgressBar(this, parent);
    return d->bar;
}

// -------------------------------------------------------

/* TRANSLATOR Gui::ProgressBar */

ProgressBar::ProgressBar (SequencerBar* s, QWidget * parent)
    : QProgressBar(parent), sequencer(s)
{
#ifdef QT_WINEXTRAS_LIB
  m_taskbarButton = nullptr;
  m_taskbarButton = nullptr;
#endif
    d = new Gui::ProgressBarPrivate;
    d->minimumDuration = 2000; // 2 seconds
    d->delayShowTimer = new QTimer(this);
    d->delayShowTimer->setSingleShot(true);
    connect(d->delayShowTimer, &QTimer::timeout, this, &ProgressBar::delayedShow);
    d->pollTimer = new QTimer(this);
    d->pollTimer->setInterval(200); // matches the push path's update throttle
    connect(d->pollTimer, &QTimer::timeout, this, &ProgressBar::aggregatePoll);
    d->teardownTimer = new QTimer(this);
    d->teardownTimer->setSingleShot(true);
    d->teardownTimer->setInterval(200); // teardown grace period
    connect(d->teardownTimer, &QTimer::timeout, this,
            [this]() { sequencer->finishAggregate(); });
    d->observeEventFilter = 0;

    setAttribute(Qt::WA_Hover);
    setFixedWidth(120);

    // write percentage to the center
    setAlignment(Qt::AlignHCenter);
    hide();
}

ProgressBar::~ProgressBar ()
{
    disconnect(d->delayShowTimer, &QTimer::timeout, this, &ProgressBar::delayedShow);
    delete d->delayShowTimer;
    delete d;
}

int ProgressBar::minimumDuration() const
{
    return d->minimumDuration;
}

void ProgressBar::resetEx()
{
  QProgressBar::reset();
#ifdef QT_WINEXTRAS_LIB
  setupTaskBarProgress();
  m_taskbarProgress->reset();
#endif
}

void ProgressBar::setRangeEx(int minimum, int maximum)
{
  QProgressBar::setRange(minimum, maximum);
#ifdef QT_WINEXTRAS_LIB
  setupTaskBarProgress();
  m_taskbarProgress->setRange(minimum, maximum);
#endif
}

void ProgressBar::setValueEx(int value)
{
  QProgressBar::setValue(value);
#ifdef QT_WINEXTRAS_LIB
  setupTaskBarProgress();
  m_taskbarProgress->setValue(value);
#endif
}

void ProgressBar::setMinimumDuration (int ms)
{
    if (value() == 0)
    {
        d->delayShowTimer->stop();
        d->delayShowTimer->start(ms);
    }

    d->minimumDuration = ms;
}

void ProgressBar::aboutToShow()
{
    // delay showing the bar
    d->delayShowTimer->start(d->minimumDuration);
#ifdef QT_WINEXTRAS_LIB
    setupTaskBarProgress();
    m_taskbarProgress->show();
#endif
}

void ProgressBar::delayedShow()
{
    if (!isVisible() && !sequencer->wasCanceled() && sequencer->isRunning()) {
        show();
    }
}

void ProgressBar::startAggregatePoll()
{
    if (!d->pollTimer->isActive()) {
        d->pollActive.start();
        d->pollTimer->start();
    }
}

void ProgressBar::armAggregateTeardown()
{
    // restarting pushes the grace period out past the latest stop
    d->teardownTimer->start();
}

void ProgressBar::aggregatePoll()
{
    auto snap = Base::SequencerManager::snapshot(progressDetailLevels());
    if (snap.sequences.empty()) {
        d->pollTimer->stop();
        sequencer->d->aggregateDriven.store(false, std::memory_order_relaxed);
        d->statusText.clear();
        hideDetailPopup();
        // the deferred UI teardown stays with the grace timer, which a
        // quickly following sequence can still cancel
        return;
    }
    sequencer->d->aggregateDriven.store(true, std::memory_order_relaxed);

    // The push path no longer forces the bar visible; apply the
    // minimum-duration rule from here so chained short sequences
    // (per-item indicators) still surface the bar once they add up.
    if (isHidden() && d->pollActive.elapsed() > d->minimumDuration)
        show();

    if (snap.total > 0) {
        int total = (int)std::min<size_t>(snap.total, INT_MAX);
        int progress = (int)std::min<size_t>(std::min(snap.progress, snap.total), INT_MAX);
        if (maximum() != total || minimum() != 0)
            setRangeEx(0, total);
        setValueEx(progress);
    }
    else if (maximum() != 0 || minimum() != 0) {
        setRangeEx(0, 0); // no sequence knows its total: busy indicator
    }

    // Keep the status message in sync: worker-thread setText() doesn't push
    // while poll-driven, so mirror the leading root sequence's text here.
    // The snapshot names its own lead: the root whose numbers the bar is
    // showing. Picking one here again would let the text describe one sequence
    // while the bar counted another.
    const auto* lead = snap.lead < snap.sequences.size()
        ? &snap.sequences[snap.lead]
        : &snap.sequences.front();
    QString text = QString::fromUtf8(lead->text.c_str());
    if (snap.roots > 1)
        text += tr(" (+%1 more)").arg(snap.roots - 1);
    if (text != d->statusText) {
        d->statusText = text;
        sequencer->d->text = QString::fromUtf8(lead->text.c_str());
        getMainWindow()->showMessage(text);
    }

    if (d->detailPopup && d->detailPopup->isVisible())
        d->detailPopup->updateSnapshot(snap);
}

void ProgressBar::showDetailPopup()
{
    auto snap = Base::SequencerManager::snapshot(progressDetailLevels());
    if (snap.sequences.empty())
        return;
    if (!d->detailPopup)
        d->detailPopup = new ProgressDetailPopup(this);
    d->detailPopup->updateSnapshot(snap);
    d->detailPopup->show();
}

void ProgressBar::hideDetailPopup()
{
    if (d->detailPopup)
        d->detailPopup->hide();
}

bool ProgressBar::event(QEvent* e)
{
    switch (e->type()) {
    case QEvent::HoverEnter:
        showDetailPopup();
        break;
    case QEvent::HoverLeave:
        hideDetailPopup();
        break;
    case QEvent::ToolTip:
        if (d->detailPopup && d->detailPopup->isVisible())
            return true; // the live popup replaces the plain tooltip
        break;
    default:
        break;
    }
    return QProgressBar::event(e);
}

void ProgressBar::aboutToHide()
{
    hide();
    setToolTip(QString());
#ifdef QT_WINEXTRAS_LIB
    setupTaskBarProgress();
    m_taskbarProgress->hide();
#endif
}

bool ProgressBar::canAbort() const
{
    auto ret = QMessageBox::question(getMainWindow(),tr("Aborting"),
    tr("Do you really want to abort the operation?"),  QMessageBox::Yes | QMessageBox::No,
    QMessageBox::No);

    return (ret == QMessageBox::Yes) ? true : false;
}

void ProgressBar::showEvent(QShowEvent* e)
{
    QProgressBar::showEvent(e);
    d->delayShowTimer->stop();
}

void ProgressBar::hideEvent(QHideEvent* e)
{
    QProgressBar::hideEvent(e);
    d->delayShowTimer->stop();
    hideDetailPopup();
}

void ProgressBar::resetObserveEventFilter()
{
    d->observeEventFilter = 0;
}

void ProgressBar::enterControlEvents(bool grab)
{
    // Change behavior to not block on background sequence, i.e. calling
    // startStep() from a different thread.
    if (grab)
        qApp->installEventFilter(this);

    // Make sure that we get the key events, otherwise the Inventor viewer usurps the key events
    // This also disables accelerators.
#if defined(Q_OS_LINUX)
    Q_UNUSED(grab)
#else
    if (grab)
        grabKeyboard();
#endif
}

void ProgressBar::leaveControlEvents(bool release)
{
    if (release)
        qApp->removeEventFilter(this);

#if defined(Q_OS_LINUX)
    Q_UNUSED(release)
#else
    // release the keyboard again
    if (release)
        releaseKeyboard();
#endif
}

#ifdef QT_WINEXTRAS_LIB
void ProgressBar::setupTaskBarProgress()
{
  if (!m_taskbarButton || !m_taskbarProgress)
  {
    m_taskbarButton = new QWinTaskbarButton(this);
    m_taskbarButton->setWindow(MainWindow::getInstance()->windowHandle());
    //m_myButton->setOverlayIcon(QIcon(""));

    m_taskbarProgress = m_taskbarButton->progress();
  }
}
#endif

bool ProgressBar::eventFilter(QObject* o, QEvent* e)
{
    if (sequencer->isRunning() && e) {
        QThread* currentThread = QThread::currentThread();
        QThread* thr = this->thread(); // this is the main thread
        if (thr != currentThread) {
            if (e->type() == QEvent::KeyPress) {
                auto ke = static_cast<QKeyEvent*>(e);
                if (ke->key() == Qt::Key_Escape) {
                    // cancel the operation
                    sequencer->tryToCancel();
                    return true;
                }
            }
            return QProgressBar::eventFilter(o, e);
        }

        // main thread
        // Hovering the bar itself shows the live detail popup. Enter/Leave
        // events are swallowed below while a blocking sequence runs, so they
        // must be handled here in the filter.
        if (o == this) {
            if (e->type() == QEvent::Enter)
                showDetailPopup();
            else if (e->type() == QEvent::Leave)
                hideDetailPopup();
        }
        switch ( e->type() )
        {
        // check for ESC
        case QEvent::KeyPress:
            {
                auto ke = static_cast<QKeyEvent*>(e);
                if (ke->key() == Qt::Key_Escape) {
                    // eventFilter() was called from the application 50 times without performing a new step (app could hang)
                    if (d->observeEventFilter > 50) {
                        // tries to unlock the application if it hangs (probably due to incorrect usage of Base::Sequencer)
                        if (ke->modifiers() & (Qt::ControlModifier | Qt::AltModifier)) {
                            sequencer->resetData();
                            // emergency unlock: no grace period, and ignore
                            // launchers that may still be registered
                            sequencer->finishAggregate(true);
                            return true;
                        }
                    }

                    // cancel the operation
                    sequencer->tryToCancel();
                }

                return true;
            }   break;

        // ignore all these events
        case QEvent::KeyRelease:
        case QEvent::Enter:
        case QEvent::Leave:
        case QEvent::MouseButtonDblClick:
        case QEvent::MouseButtonRelease:
        case QEvent::MouseMove:
        case QEvent::NativeGesture:
        case QEvent::ContextMenu:
            {
                if (!d->isModalDialog(o) && !LiveViewInteraction::passes(o, e))
                    return true;
            }   break;

        // special case if the main window's close button was pressed
        case QEvent::Close:
            {
                // avoid to exit while app is working
                // note: all other widget types are allowed to be closed anyway
                if (o == getMainWindow()) {
                    e->ignore();
                    return true;
                }
            }   break;

        // do a system beep and ignore the event
        case QEvent::MouseButtonPress:
            {
                if (!d->isModalDialog(o) && !LiveViewInteraction::passes(o, e)) {
                    QApplication::beep();
                    return true;
                }
            }   break;

        default:
            {
            }   break;
        }

        d->observeEventFilter++;
    }

    return QProgressBar::eventFilter(o, e);
}


#include "moc_ProgressBar.cpp"
