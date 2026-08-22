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


#ifndef BASE_SEQUENCER_H
#define BASE_SEQUENCER_H

#include <atomic>
#include <cstddef>
#include <string>
#include <vector>

#include "Exception.h"

class QThread;

namespace Base
{

class AbortException;
class SequencerLauncher;

/**
 * \brief This class gives the user an indication of the progress of an operation and
 * it is used to reassure him that the application is still running.
 *
 * Here are some code snippets of how to use the sequencer:
 *  \code
 *
 *  #include <Base/Sequencer.h>
 *
 *  //first example
 *  Base::SequencerLauncher seq("my text", 10)
 *  for (int i=0; i<10; i++)
 *  {
 *    // do something
 *    seq.next ();
 *  }
 *
 *  //second example
 *  Base::SequencerLauncher seq("my text", 10)
 *  do
 *  {
 *    // do something
 *  }
 *  while (seq.next());
 *
 *  \endcode
 *
 * The implementation of this class also supports several nested instances
 * at a time. But note, that only the first instance has an effect. Any further
 * sequencer instance doesn't influence the total number of iteration steps. This
 * is simply because it's impossible to get the exact number of iteration steps
 * for nested instances and thus we have either too few steps estimated then the
 * sequencer may indicate 100% but the algorithm still running or we have too many
 * steps estimated so that the an algorithm may stop long before the sequencer
 * reaches 100%.
 *
 *  \code
 *  try {
 *    //start the first operation
 *    Base::SequencerLauncher seq1("my text", 10)
 *    for (int i=0; i<10, i++)
 *    {
 *      // do something
 *
 *      // start the second operation while the first one is still running
 *      Base::SequencerLauncher seq2("another text", 10);
 *      for (int j=0; j<10; j++)
 *      {
 *        // do something different
 *        seq2.next ();
 *      }
 *
 *      seq1.next ( true ); // allow to cancel
 *    }
 *  }
 *  catch(const Base::AbortException&){
 *    // cleanup your data if needed
 *  }
 *
 *  \endcode
 *
 * \note If using the sequencer with SequencerLauncher.next(\a true) then you must
 * take into account that the exception \a AbortException could be thrown, e.g. in
 * case the ESC button was pressed. So in this case it's always a good idea to use
 * the sequencer within a try-catch block.
 *
 * \note Instances of SequencerLauncher should always be created on the stack.
 * This is because if an exception somewhere is thrown the destructor is auto-
 * matically called to clean-up internal data.
 *
 * \note It's not supported to create an instance of SequencerBase or a sub-class
 * in another thread than the main thread. But you can create SequencerLauncher
 * instances in other threads.
 *
 * \author Werner Mayer
 */
class BaseExport SequencerBase
{
    friend class SequencerLauncher;

public:
    /**
     * Returns the last created sequencer instance.
     * If you create an instance of a class inheriting SequencerBase
     * this object is retrieved instead.
     *
     * This mechanism is very useful to have an own sequencer for each layer of FreeCAD.
     * For example, if FreeCAD is running in server mode you have/need no GUI layer
     * and therewith no (graphical) progress bar; in this case ConsoleSequencer is taken.
     * But in cases FreeCAD is running with GUI the @ref Gui::ProgressBar is taken instead.
     * @see Sequencer
     */
    static SequencerBase& Instance();
    /** Destruction */
    virtual ~SequencerBase();
    /**
     * Returns true if the running sequencer is blocking any user input.
     * This might be only of interest of the GUI where the progress bar or dialog
     * is used from a thread. If started from a thread this method should return
     * false, otherwise true. The default implementation always returns true.
     */
    virtual bool isBlocking() const;
    /** If \a bLock is true then the sequencer gets locked. startStep() and nextStep()
     * don't get invoked any more until the sequencer gets unlocked again.
     * This method returns the previous lock state.
     */
    bool setLocked(bool bLock);
    /** Returns true if the sequencer was locked, false otherwise. */
    bool isLocked() const;
    /** Returns true if the sequencer is running, otherwise returns false. */
    bool isRunning() const;
    /**
     * Returns true if the pending operation was canceled.
     */
    bool wasCanceled() const;

    /// Check if the  operation is aborted by user
    virtual void checkAbort()
    {}

    /**
     * Returns true if this indicator is driven by polling (e.g. the GUI status
     * bar polling SequencerManager on a timer) instead of by per-step pushes.
     * Worker-thread ticks may then skip driving the indicator altogether and
     * only bump their launcher's atomic counters.
     */
    virtual bool updatesViaPoll() const
    {
        return false;
    }

protected:
    /**
     * Starts a new operation, returns false if there is already a pending operation,
     * otherwise it returns true.
     * In this method startStep() gets invoked that can be reimplemented in sub-classes.
     */
    bool start(const char* pszStr, size_t steps);
    /** Returns the number of steps. */
    size_t numberOfSteps() const;
    /** Returns the current state of progress in percent. */
    int progressInPercent() const;
    /**
     * Performs the next step and returns true if the operation is not yet finished.
     * But note, when 0 was passed to start() as the number of total steps this method
     * always returns false.
     *
     * In this method nextStep() gets invoked that can be reimplemented in sub-classes.
     * If \a canAbort is true then the operations can be aborted, otherwise (the default)
     * the operation cannot be aborted. In case it gets aborted an exception AbortException
     * is thrown.
     */
    bool next(bool canAbort = false);
    /**
     * Stops the sequencer if all operations are finished. It returns false if
     * there are still pending operations, otherwise it returns true.
     */
    bool stop();
    /**
     * Breaks the sequencer if needed. The default implementation does nothing.
     * Every pause() must eventually be followed by a corresponding @ref resume().
     * @see Gui::ProgressBar.
     */
    virtual void pause();
    /**
     * Continues with progress. The default implementation does nothing.
     * @see pause(), @see Gui::ProgressBar.
     */
    virtual void resume();
    /**
     * Try to cancel the pending operation(s).
     * E.g. @ref Gui::ProgressBar calls this method after the ESC button was pressed.
     */
    void tryToCancel();
    /**
     * If you tried to cancel but then decided to continue the operation.
     * E.g. in @ref Gui::ProgressBar a dialog appears asking if you really want to
     * cancel. If you decide to continue this method must be called.
     */
    void rejectCancel();

protected:
    /** construction */
    SequencerBase();
    SequencerBase(const SequencerBase&) = delete;
    SequencerBase(SequencerBase&&) = delete;
    SequencerBase& operator=(const SequencerBase&) = delete;
    SequencerBase& operator=(SequencerBase&&) = delete;
    /**
     * Sets a text what the pending operation is doing. The default implementation
     * does nothing.
     */
    virtual void setText(const char* pszTxt);
    /**
     * This method can be reimplemented in sub-classes to give the user a feedback
     * when a new sequence starts. The default implementation does nothing.
     */
    virtual void startStep(bool blocking);
    /**
     * This method can be reimplemented in sub-classes to give the user a feedback
     * when the next is performed. The default implementation does nothing. If \a canAbort
     * is true then the pending operation can aborted, otherwise not. Depending on the
     * re-implementation this method can throw an AbortException if canAbort is true.
     */
    virtual void nextStep(bool canAbort);
    /**
     * Sets the progress indicator to a certain position.
     */
    virtual void setProgress(size_t);
    /**
     * Resets internal data.
     * If you want to reimplement this method, it is very important to call it in
     * the re-implemented method.
     */
    virtual void resetData();

    /**
     * Sets the total steps of indicator
     */
    virtual void setTotalSteps(size_t);

protected:
    // NOLINTBEGIN
    size_t nProgress {0};   /**< Stores the current amount of progress.*/
    size_t nTotalSteps {0}; /**< Stores the total number of steps */
    // NOLINTEND

private:
    bool _bLocked {false}; /**< Lock/unlock sequencer. */
    std::atomic<bool> _bCanceled {
        false};                /**< Is set to true if the last pending operation was canceled */
    int _nLastPercentage {-1}; /**< Progress in percent. */
};

/** This special sequencer might be useful if you want to suppress any indication
 * of the progress to the user.
 */
class BaseExport EmptySequencer: public Base::SequencerBase
{
public:
    /** construction */
    EmptySequencer() = default;
};

/**
 * \brief This class writes the progress to the console window.
 */
class BaseExport ConsoleSequencer: public SequencerBase
{
public:
    /** construction */
    ConsoleSequencer() = default;

protected:
    /** Starts the sequencer */
    void startStep(bool blocking) override;
    /** Writes the current progress to the console window. */
    void nextStep(bool canAbort) override;

private:
    /** Puts text to the console window */
    void setText(const char* pszTxt) override;
    /** Resets the sequencer */
    void resetData() override;

    std::string _lastText;   /**< last printed text, to skip repeats */
    bool _printed {false};   /**< a progress line needs clearing */
};

/** The SequencerLauncher class is provided for convenience. It allows you to run an instance of the
 * sequencer by instantiating an object of this class -- most suitable on the stack. So this
 * mechanism can be used for try-catch-blocks to destroy the object automatically if the C++
 * exception mechanism cleans up the stack.
 *
 * This class has been introduced to simplify the use with the sequencer. In the FreeCAD Gui layer
 * there is a subclass of SequencerBase called ProgressBar that grabs the keyboard and filters most
 * of the incoming events. If the programmer uses the API of SequencerBase directly to start an
 * instance without due diligence with exceptions then a not handled exception could block the whole
 * application -- the user has to kill the application then.
 *
 * Below is an example of a not correctly used sequencer.
 *
 * \code
 *
 *  #include <Base/Sequencer.h>
 *
 *  void runOperation();
 *
 *  void myTest()
 *  {
 *    try{
 *       runOperation();
 *    } catch(...) {
 *       // the programmer forgot to stop the sequencer here
 *       // Under circumstances the sequencer never gets stopped so the keyboard never gets
 * ungrabbed and
 *       // all Gui events still gets filtered.
 *    }
 *  }
 *
 *  void runOperation()
 *  {
 *    Base::Sequencer().start ("my text", 10);
 *
 *    for (int i=0; i<10; i++)
 *    {
 *      // do something where an exception be thrown
 *      ...
 *      Base::Sequencer().next ();
 *    }
 *
 *    Base::Sequencer().stop ();
 *  }
 *
 * \endcode
 *
 * To avoid such problems the SequencerLauncher class can be used as follows:
 *
 * \code
 *
 *  #include <Base/Sequencer.h>
 *
 *  void runOperation();
 *
 *  void myTest()
 *  {
 *    try{
 *       runOperation();
 *    } catch(...) {
 *       // the programmer forgot to halt the sequencer here
 *       // If SequencerLauncher leaves its scope the object gets destructed automatically and
 *       // stops the running sequencer.
 *    }
 *  }
 *
 *  void runOperation()
 *  {
 *    // create an instance on the stack (not on any terms on the heap)
 *    SequencerLauncher seq("my text", 10);
 *
 *    for (int i=0; i<10; i++)
 *    {
 *      // do something (e.g. here can be thrown an exception)
 *      ...
 *      seq.next ();
 *    }
 *  }
 *
 * \endcode
 *
 * @author Werner Mayer
 */
class BaseExport SequencerLauncher
{
public:
    /** What the indicator may do to the UI on this sequence's behalf.
     *
     * A sequence started on the main thread has always meant "this loop
     * owns the GUI thread until it ends", and the indicator answers it by
     * grabbing input: a wait cursor, the application event filter that
     * swallows clicks and keys, and the 3D viewer's own filter dropping
     * navigation (View3DInventorViewer's eventFilter tests
     * Sequencer().isBlocking() too). That is right for a loop that keeps
     * the thread.
     *
     * It is wrong for a sequence that spans event-loop turns -- a load
     * draining in budgeted slices (docs/ProgressiveLoading.md §2) reports
     * one sequence across many returns to the event loop, and the whole
     * point of the drain is that the window stays usable while it runs.
     * KeepInteractive reports such a sequence without letting the
     * indicator take the input away.
     */
    enum Blocking {
        BlockInput,       ///< the sequence owns the GUI thread (default)
        KeepInteractive,  ///< sliced on the event loop; leave the UI usable
    };
    SequencerLauncher(const char* pszStr=nullptr, size_t steps=0,
                      Blocking blocking=BlockInput);
    virtual ~SequencerLauncher();
    size_t numberOfSteps() const;
    size_t progress() const;
    void setText (const char* pszTxt);
    const std::string &text() const {
        return strText;
    }
    void setTotalSteps(size_t steps);
    bool next(bool canAbort = false);
    void setProgress(size_t);
    bool wasCanceled() const;
    void setCanceled(bool cacnel=true);
    bool isBlocking() const {
        return bBlocking;
    }
    void setNoException(bool enable);
    /** Report this sequence as a job of its own, never nested under whatever
     * happened to be running when it started.
     *
     * Nesting is inferred from registration order, which is right for a
     * sequence started inside another one's call stack and wrong for one that
     * merely overlaps a sliced sequence: a `KeepInteractive` launcher lives
     * across returns to the event loop, so a document save starting while a
     * progressive fill still drains was bucketed as the fill's child and
     * vanished from the consolidated bar -- the status text read
     * `Saving document...` over the fill's numbers, and a 30 second save
     * advertised three and a half minutes remaining.
     */
    void setStandalone(bool enable = true);
    bool start(size_t steps=0, const char *pszTxt=nullptr);
    bool stop();
private:
    std::string strText;
    std::atomic<size_t> nProgress {0};
    std::atomic<size_t> nTotalSteps {0};
    // Allow cancel by user code
    std::atomic<bool> bCanceled {false};
    bool bBlocking {false};
    bool bKeepInteractive {false};
    bool bNoException {false};
    bool bStandalone {false};
    QThread *ownerThread {nullptr};

    SequencerLauncher(const SequencerLauncher&) = delete;
    SequencerLauncher(SequencerLauncher&&) = delete;
    void operator=(const SequencerLauncher&) = delete;
    void operator=(SequencerLauncher&&) = delete;

    friend class SequencerManager;
};

/**
 * \brief Thread-aware collector of all parallel running sequences.
 *
 * Every SequencerLauncher on any thread registers itself; this class turns
 * that registry into a consolidated progress a UI can poll on its own cadence
 * (poll, not push): workers only bump their launcher's atomic counters, the
 * consolidation work happens at snapshot() time in the reader.
 *
 * Sequences are reported per thread as a hierarchy: the outermost active
 * launcher is the root (depth 0) and nested launchers follow it in nesting
 * order with increasing depth, up to \a maxLevels. Only the roots feed the
 * consolidated numbers, preserving the "nested sequences do not inflate
 * progress" rule, while launchers running on different threads sum up. A
 * launcher may also be shared by several worker threads (e.g. a parallel
 * recompute with one launcher for the whole job) — next() is safe to call
 * concurrently.
 */
class BaseExport SequencerManager
{
public:
    struct Info
    {
        std::string text;       /**< what this sequence says it is doing */
        size_t progress = 0;    /**< steps done so far */
        size_t total = 0;       /**< 0 = unknown (busy indicator) */
        size_t depth = 0;       /**< nesting level within its thread; 0 = root */
        bool mainThread = false;
        /** this sequence owns its thread until it ends (not KeepInteractive) */
        bool blocking = false;
    };
    struct Snapshot
    {
        /** grouped per thread: each root (depth 0) directly followed by its
         * nested sequences in nesting order */
        std::vector<Info> sequences;
        /** Consolidated over the roots that matter: sum of min(progress, total).
         *
         * When any root is blocking, only the blocking roots are counted. A
         * blocking sequence is what holds the thread the user is waiting on,
         * and a background sliced sequence running beside it must not be what
         * the bar reports: a save whose numbers came from a still-draining
         * progressive fill advertised three and a half minutes for thirty
         * seconds of work.
         */
        size_t progress = 0;
        size_t total = 0;    /**< consolidated the same way; 0 = indeterminate */
        /** number of root (depth 0) sequences, i.e. parallel sequences */
        size_t roots = 0;
        /** index into `sequences` of the root the bar should name, npos if none */
        size_t lead = static_cast<size_t>(-1);
    };

    /** Lock-free count of live launchers (cheap "is anything running?"). */
    static size_t activeCount();
    /** Consolidated snapshot of all running sequences (locks briefly),
     * reporting at most \a maxLevels nesting levels per thread. */
    static Snapshot snapshot(size_t maxLevels = 5);
};

/** Access to the only SequencerBase instance */
inline SequencerBase& Sequencer()
{
    return SequencerBase::Instance();
}

}  // namespace Base

#endif  // BASE_SEQUENCER_H
