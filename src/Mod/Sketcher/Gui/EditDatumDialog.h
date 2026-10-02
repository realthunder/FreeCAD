/***************************************************************************
 *   Copyright (c) 2011 Werner Mayer <wmayer[at]users.sourceforge.net>     *
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

#ifndef SKETCHERGUI_EditDatumDialog_H
#define SKETCHERGUI_EditDatumDialog_H

#include <QObject>
#include <functional>
#include <memory>
#include <vector>


namespace Sketcher
{
class Constraint;
class SketchObject;
}  // namespace Sketcher

namespace Base
{
class Quantity;
}
namespace Gui
{
class EditableDatumLabel;
class ViewerContext;
}  // namespace Gui

namespace SketcherGui
{
class ViewProviderSketch;
class Ui_InsertDatum;

/** Ask for the value of dimensional constraints
 *
 * In the view, at each constraint's own label, when the view the event
 * came through has no widgets (a served client's: a modal dialog on the
 * host would stop every client, with nobody there to close it) or the
 * preference Mod/Sketcher/General/EditDatumInPlace is on, which it is by
 * default. Otherwise, and for a constraint the box cannot edit -- a
 * reference, or one driven by an expression -- in the modal dialog, one
 * constraint after the other.
 *
 * In place the function returns at once and the values come later: Enter
 * applies every box as it stands, in one transaction; Escape applies none
 * and aborts the open command, as the dialog's Cancel does; a click
 * elsewhere applies them if every box holds a value. Tab moves between
 * the boxes.
 *
 * @param done: called once with whether the values were applied -- before
 * this returns when it was the dialog. May be empty.
 * @param preferDialog: the full dialog (name, reference, expression) where
 * there can be one
 */
void editDatums(Sketcher::SketchObject* sketch,
                const std::vector<int>& constraints,
                bool atCursor = true,
                std::function<void(bool)> done = {},
                bool preferDialog = false);

/** The values of dimensional constraints being typed in a view
 *
 * One entry box per constraint (Gui::EditableDatumLabel, the box of a
 * drawing tool's on-view parameter, and so what a served client is
 * already shown and types into: docs/ThinClient.md 8.7), each at its
 * constraint's label. Owned by itself; the sketch's view provider knows
 * the one in progress and ends it when the edit, or the tool that asked,
 * goes away.
 */
class DatumEditSession: public QObject
{
    Q_OBJECT

public:
    /// False, and nothing started, when a constraint has no label drawn.
    static bool start(ViewProviderSketch* vp,
                      Gui::ViewerContext* viewer,
                      const std::vector<int>& constraints,
                      std::function<void(bool)> done);
    ~DatumEditSession() override;

    /** End it
     * @param accept: apply the values; refused, and rejected, when a box
     * does not hold one
     * @param notify: call the continuation. Not when whoever would be
     * called is going away.
     */
    void finish(bool accept, bool notify = true);
    /// A button event in a view of the sketch while this runs: a press of
    /// the first button elsewhere ends it. Always true: the event is used.
    bool mouseButton(int button, bool pressed);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    DatumEditSession(ViewProviderSketch* vp, std::function<void(bool)> done);
    bool values(std::vector<std::pair<int, Base::Quantity>>& out) const;
    void focus(int index);

    struct Entry
    {
        int constraint;
        std::unique_ptr<Gui::EditableDatumLabel> label;
    };
    ViewProviderSketch* vp;
    std::vector<Entry> entries;
    std::function<void(bool)> done;
    int focused = 0;
    bool ended = false;
};

/** Whether a constraint may be given this name
 *
 * Empty (the constraint is unnamed again), or letters, digits and
 * underscores not starting with a digit: what an expression can refer to
 * without escaping. Tells the user when it is not.
 */
bool checkConstraintName(const Sketcher::SketchObject* sketch, const std::string& constraintName);

class EditDatumDialog: public QObject
{
    Q_OBJECT

public:
    EditDatumDialog(ViewProviderSketch* vp, int ConstrNbr);
    EditDatumDialog(Sketcher::SketchObject* pcSketch, int ConstrNbr);
    ~EditDatumDialog() override;

    int exec(bool atCursor = true);
    bool isSuccess();

private:
    Sketcher::SketchObject* sketch;
    Sketcher::Constraint* Constr;
    int ConstrNbr;
    bool success;
    std::unique_ptr<Ui_InsertDatum> ui_ins_datum;

private Q_SLOTS:
    void accepted();
    void rejected();
    void drivingToggled(bool);
    void datumChanged();
    void formEditorOpened(bool);

private:
    void performAutoScale(double newDatum);
};

/// When newDatum is the first value of the one constraint that fixes the
/// sketch's scale, scale the sketch to it. The constraint's number is
/// updated: constraints that cannot be scaled are dropped on the way.
void performDatumAutoScale(Sketcher::SketchObject* sketch, int& constraint, double newDatum);

}  // namespace SketcherGui
#endif  // SKETCHERGUI_DrawSketchHandler_H
