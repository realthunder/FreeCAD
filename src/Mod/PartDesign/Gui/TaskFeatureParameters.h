/***************************************************************************
 *   Copyright (C) 2015 Alexander Golubev (Fat-Zer) <fatzer2@gmail.com>    *
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

#ifndef TASKFEATUREPARAMETERS_H_NAHKE2YZ
#define TASKFEATUREPARAMETERS_H_NAHKE2YZ

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <QPointer>
#include <QWidget>

#include <Gui/TaskView/TaskView.h>
#include <Gui/TaskView/TaskDialog.h>
#include <Gui/DocumentObserver.h>

#include "ViewProvider.h"

class QCheckBox;
class QComboBox;
class QTimer;
class QBoxLayout;

namespace App {
class Property;
}

namespace Gui {
class PrefCheckBox;
}

namespace PartDesignGui {

/// Convenience class to collect common methods for all SketchBased features
class TaskFeatureParameters : public Gui::TaskView::TaskBox,
                              public Gui::DocumentObserver
{
    Q_OBJECT

public:
    TaskFeatureParameters(PartDesignGui::ViewProvider* vp, QWidget *parent,
                          const std::string& pixmapname, const QString& parname);

    TaskFeatureParameters(PartDesignGui::ViewProvider* vp, QWidget *parent, const QString& parname);

    ~TaskFeatureParameters() override;

    /// save field history
    virtual void saveHistory();
    /// apply changes made in the parameters input to the model via commands
    virtual void apply() {}

    void recomputeFeature(bool delay=true);

    void addOperationCombo(QBoxLayout *layout);
    void addUpdateViewCheckBox(QBoxLayout *layout);
    virtual const char *updateViewParameter() const;

    virtual void refresh() {}

    void setupTransaction();

    int getTransactionID() const {
        return transactionID;
    }

    /** @name Every operation an undo step of its own (docs/HandsOnQueue.md
     * entry 136, the reporter 2026-10-15 09:47: "I want each operation in
     * fillet to be undoable ... but Cancel button shall restore to previous
     * state before edit")
     *
     * With fineUndo on, setupTransaction() opens a step for what the current
     * event changes and closes it when the event is done -- after the
     * recompute it schedules, if any -- so a menu choice, a mode change or a
     * value set in the panel is one step. A drag or a label open for typing
     * holds one step until it ends (beginHeldStep/endHeldStep); a cancelled
     * drag rolls its step back. OK's work is a step of its own. Cancel still
     * undoes down to the edit's first step (getTransactionID).
     */
    //@{
    bool isFineUndo() const {
        return fineUndo;
    }
    /// The first step this panel opened, 0 for none: what Cancel undoes down
    /// to when the transaction open at the edit's start was left empty
    int getFirstStepID() const {
        return firstStepTid;
    }
    /// The step OK's work goes in
    void beginAcceptStep();
    //@}

    /// Orders the panels' full recomputes of their feature; 0 for none
    unsigned long getComputedSequence() const {
        return computedSequence;
    }
    /// Whether the feature's inputs are what this panel's last full
    /// recompute of it saw, so its Shape is already theirs
    bool isInputComputed() const;

protected Q_SLOTS:
    // TODO Add update view to all dialogs (2015-12-05, Fat-Zer)
    void onUpdateView(bool on);
    void onNewSolidChanged();
    void onUpdateViewTimer();

protected:
    virtual void _refresh();

    void addBlinkWidget(QWidget *, const QString &txt=QString());
    void removeBlinkWidget(QWidget *);

    void timerEvent(QTimerEvent *);

    virtual void finishedRecomputeFeature() {}

    /// Tell the user which modifier switches a gizmo drag between coarse and
    /// fine steps (upstream 6df51cefd7).
    void showDraggerHints();
    void hideDraggerHints();

    /// One undo step per operation (entry 136); set by a panel that has it
    bool fineUndo = false;
    /// A drag, a label open for typing: one step until endHeldStep(); \a
    /// abort rolls it back (a cancelled drag)
    void beginHeldStep();
    void endHeldStep(bool abort);
    /// After a held step was rolled back (a cancelled drag): the handles of
    /// a value put back where the feature has it (entry 145)
    virtual void heldStepRolledBack() {}
    /** Whether the recompute of a drag failed for what the drag changes:
     * the pointer is then the stop sign until a recompute succeeds again
     * or the drag ends (docs/HandsOnQueue.md entry 143). By default, the
     * feature in error.
     */
    virtual bool isDragFailing() const;
    /// The stop sign shown or taken away as isDragFailing() says
    void updateFailCursor();
    /// Holds a step from the first key typed in \a box until Enter or the
    /// focus leaving it: what is typed in a panel box is one step, not one
    /// per key
    void holdStepWhileFocused(QWidget* box);
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    /** Notifies when the object is about to be removed. */
    void slotDeletedObject(const Gui::ViewProviderDocumentObject& Obj) override;
    void slotDeleteDocument(const Gui::Document& Doc) override;
    /** Notifies on undo */
    void slotUndoDocument(const Gui::Document& Doc) override;
    /** Notifies on redo */
    void slotRedoDocument(const Gui::Document& Doc) override;

protected:
    PartDesignGui::ViewProvider *vp;
    /// Lock updateUI(), applying changes to the underlying feature and calling recomputeFeature()
    bool blockUpdate;

    QCheckBox *checkBoxNewSolid = nullptr;
    QComboBox *comboAddSubType = nullptr;
    Gui::PrefCheckBox * checkBoxUpdateView = nullptr;
    QTimer *updateViewTimer = nullptr;
    int transactionID = 0;
    int blinkTimerId = 0;
    bool blink = false;
    bool draggerHintsShown = false;

    struct BlinkInfo {
        QPointer<QWidget> widget;
        QString text;
        QString altText;
        void setText(const QString &text);
    };
    std::unordered_map<void*, BlinkInfo> blinkWidgets;

private:
    void saveComputedInput();
    /// Closes the step setupTransaction() opened, once nothing holds it and
    /// no recompute is pending
    void closeStep();

    int stepTid = 0;
    int firstStepTid = 0;
    int heldSteps = 0;
    bool stepCloseQueued = false;
    /// OK's step is open: a second call (the dress-up's, then the base's)
    /// keeps it
    bool acceptStepOpen = false;
    std::vector<QPointer<QWidget>> focusHeldBoxes;
    /// The box being typed in, its step held
    QPointer<QWidget> typingBox;
    /// The stop sign is the pointer's (updateFailCursor)
    bool failCursor = false;

    /// The feature's input values at the last full recompute, by name
    std::vector<std::pair<std::string, std::unique_ptr<App::Property>>> computedInput;
    unsigned long computedSequence = 0;
};

/// A common base for sketch based, dressup and other solid parameters dialogs
class TaskDlgFeatureParameters : public Gui::TaskView::TaskDialog
{
    Q_OBJECT

public:
    explicit TaskDlgFeatureParameters(PartDesignGui::ViewProvider *vp);
    ~TaskDlgFeatureParameters() override;

public:
    /// is called by the framework if the dialog is accepted (Ok)
    bool accept() override;
    /// is called by the framework if the dialog is rejected (Cancel)
    bool reject() override;

    /// Returns the view provider dialog is runed for
     PartDesignGui::ViewProvider *viewProvider() const { return vp; }

protected:
    PartDesignGui::ViewProvider *vp;
};

} //namespace PartDesignGui


#endif /* end of include guard: TASKFEATUREPARAMETERS_H_NAHKE2YZ */
