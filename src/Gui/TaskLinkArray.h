// SPDX-License-Identifier: LGPL-2.1-or-later
/****************************************************************************
 *   Copyright (c) 2026 Zheng Lei <realthunder.dev@gmail.com>               *
 *                                                                          *
 *   This file is part of FreeCAD.                                          *
 *                                                                          *
 *   FreeCAD is free software: you can redistribute it and/or modify it     *
 *   under the terms of the GNU Lesser General Public License as            *
 *   published by the Free Software Foundation, either version 2.1 of the   *
 *   License, or (at your option) any later version.                        *
 *                                                                          *
 *   FreeCAD is distributed in the hope that it will be useful, but         *
 *   WITHOUT ANY WARRANTY; without even the implied warranty of             *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU       *
 *   Lesser General Public License for more details.                        *
 *                                                                          *
 *   You should have received a copy of the GNU Lesser General Public       *
 *   License along with FreeCAD. If not, see                                *
 *   <https://www.gnu.org/licenses/>.                                       *
 *                                                                          *
 ***************************************************************************/

#ifndef GUI_TASKLINKARRAY_H
#define GUI_TASKLINKARRAY_H

#include <App/DocumentObserver.h>

#include "DocumentObserver.h"
#include "Selection.h"
#include "TaskView/TaskDialog.h"
#include "TaskView/TaskView.h"

class QCheckBox;
class QComboBox;
class QGroupBox;
class QLabel;
class QPushButton;
class QVBoxLayout;

namespace App
{
class LinkArray;
class PropertyLinkSub;
}  // namespace App

namespace Gui
{
class ComboLinks;
class PatternDirectionWidget;
class PatternParametersWidget;
class ViewProviderLinkArray;

/** The panel of an App::LinkArray: what it links, the kind of pattern, and
 * the editors of the kind, which change with it
 */
class GuiExport TaskLinkArray: public TaskView::TaskBox,
                               public SelectionObserver,
                               public DocumentObserver
{
    Q_OBJECT

public:
    explicit TaskLinkArray(ViewProviderLinkArray* vp, QWidget* parent = nullptr);
    ~TaskLinkArray() override;

    /// Record the parameters as commands, for the macro recorder
    void apply();
    /// Leave the selection modes
    void exitSelection();

protected:
    void onSelectionChanged(const SelectionChanges& msg) override;
    void changeEvent(QEvent* e) override;
    void slotUndoDocument(const Document& doc) override;
    void slotRedoDocument(const Document& doc) override;

private:
    App::LinkArray* getArray() const;
    void retranslate();
    void buildPatternWidgets();
    void updateUI();
    void updateLinkedLabel();
    void fillReferenceCombo(ComboLinks& links, bool second);
    void onTypeActivated(int index);
    void onReferenceActivated(ComboLinks& links, App::PropertyLinkSub* prop);
    void onLinkedButtonToggled(bool on);
    void onShowElementToggled(bool on);
    void onDirection2Toggled(bool on);
    void onChanged();
    void recompute();
    /// Put the directions' on-view labels where the array is now
    void updateLabels();

private:
    App::DocumentObjectT arrayT;
    bool blockUpdate = false;

    QLabel* labelLinked = nullptr;
    QPushButton* buttonLinked = nullptr;
    QLabel* labelType = nullptr;
    QComboBox* comboType = nullptr;
    QCheckBox* checkShowElement = nullptr;
    QWidget* patternBox = nullptr;
    QVBoxLayout* patternLayout = nullptr;

    QGroupBox* groupDirection1 = nullptr;
    QGroupBox* groupDirection2 = nullptr;
    PatternDirectionWidget* direction1 = nullptr;
    PatternDirectionWidget* direction2 = nullptr;
    PatternParametersWidget* parameters = nullptr;

    // The reference being picked, or the linked object
    App::PropertyLinkSub* picking = nullptr;
    ComboLinks* pickingLinks = nullptr;
    bool pickingLinked = false;
};

class GuiExport TaskDlgLinkArray: public TaskView::TaskDialog
{
    Q_OBJECT

public:
    explicit TaskDlgLinkArray(ViewProviderLinkArray* vp);
    ~TaskDlgLinkArray() override;

    bool accept() override;
    bool reject() override;

    QDialogButtonBox::StandardButtons getStandardButtons() const override
    {
        return QDialogButtonBox::Ok | QDialogButtonBox::Cancel;
    }

private:
    TaskLinkArray* panel = nullptr;
};

}  // namespace Gui

#endif  // GUI_TASKLINKARRAY_H
