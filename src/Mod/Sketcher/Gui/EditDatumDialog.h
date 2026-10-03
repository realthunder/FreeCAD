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
#include <Inventor/SbVec3f.h>
#include <boost/uuid/uuid.hpp>
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
class DatumValueEditor;
class ViewerContext;
}  // namespace Gui

namespace SketcherGui
{
class ViewProviderSketch;
class Ui_InsertDatum;

/** Ask for the value of dimensional constraints
 *
 * In the view, in one editor that stands over the constraint's own label
 * (Gui::DatumValueEditor): the value or an expression, driving or
 * reference, the name. Always so for a view without widgets (a served
 * client's: a modal dialog on the host would stop every client, with
 * nobody there to close it), and on the desktop unless the preference
 * Mod/Sketcher/General/EditDatumInPlace is off, when it is the modal
 * dialog, one constraint after the other.
 *
 * In place the function returns at once and the values come later (see
 * DatumEditSession).
 *
 * @param done: called once with whether the entry was taken -- before
 * this returns when it was the dialog. May be empty.
 */
void editDatums(Sketcher::SketchObject* sketch,
                const std::vector<int>& constraints,
                bool atCursor = true,
                std::function<void(bool)> done = {});

/** A dimension's value edited in place: one editor, which Tab moves
 *
 * Ruled 2026-10-03 (docs/SketcherPort.md "One editor for a constraint's
 * value"). Tab applies what is typed for the constraint the editor is on
 * and moves it to the next one: the set it was opened for when that is
 * several (the Dimension tool's two), every dimension the view shows when
 * it was one. Everything applied is one transaction: Enter commits it,
 * Escape aborts it (a new constraint goes with it), a click elsewhere is
 * Enter. Std_Undo or Std_Redo while it runs is Escape and nothing older
 * (ViewProviderSketch::undoRedoInEdit); an undo from elsewhere is followed
 * by tag (documentRewound).
 *
 * Owned by itself; the sketch's view provider knows the one in progress
 * and ends it when the edit, or the tool that asked, goes away.
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
     * @param accept: apply what is typed and commit; a cancel when it is
     * no value
     * @param notify: call the continuation. Not when whoever would be
     * called is going away.
     */
    void finish(bool accept, bool notify = true);
    /// A button event in a view of the sketch while this runs: a press of
    /// the first button elsewhere ends it. Always true: the event is used.
    bool mouseButton(int button, bool pressed);
    /** The document was undone or redone underneath, by somebody this
     * could not answer first: a Python undo, another client. When that
     * took the transaction this runs in, the document has moved past the
     * entry and it ends. Otherwise the editor follows its constraint by
     * tag to wherever the list now has it; gone, the entry ends.
     */
    void documentRewound();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    DatumEditSession(ViewProviderSketch* vp,
                     Gui::ViewerContext* viewer,
                     std::function<void(bool)> done);
    /// A dimensional constraint with its label drawn (or Snell's law)
    static bool canEdit(ViewProviderSketch* vp, int constraint);
    /// Where the editor stands for it, in the sketch's plane
    SbVec3f anchorOf(int index) const;
    /// canEdit, in the virtual space shown, visible, its number on the screen
    bool isShown(int index) const;
    int indexOf(const boost::uuids::uuid& tag) const;
    /// The constraint the editor is on, by its place in the list now, or -1
    int current() const;
    /// The editor on constraint `index`
    void show(int index);
    /// What is typed, into the document. False, and said in the editor,
    /// when it cannot be.
    bool applyCurrent();
    /// The editor to the next (1) or previous (-1) one to visit
    void move(int step);

    ViewProviderSketch* vp;
    Gui::ViewerContext* viewer;
    std::unique_ptr<Gui::DatumValueEditor> editor;
    /// What Tab visits; empty for every dimension the view shows
    std::vector<boost::uuids::uuid> cycle;
    boost::uuids::uuid editing {};
    std::function<void(bool)> done;
    /// A document transaction was open when this started: the command's
    /// that made the constraint, which the entry is the end of
    bool ownTransaction = false;
    /// Something went into the document
    bool applied = false;
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
