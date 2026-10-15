// SPDX-License-Identifier: LGPL-2.1-or-later
/****************************************************************************
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

#ifndef GUI_ONVIEWVALUE_H
#define GUI_ONVIEWVALUE_H

#include <functional>
#include <memory>
#include <vector>

#include <QObject>
#include <QPointer>
#include <QString>
#include <fastsignals/signal.h>

#include <Base/Vector3D.h>
#include <FCGlobal.h>

class QKeyEvent;
class QMenu;
class QWidget;
class SoNodeSensor;

namespace Gui
{

class EditableDatumLabel;
class Gizmo;
class QuantitySpinBox;
class ViewerContext;

/** The values of a feature in edit, shown and typed in the 3D view.
 *
 * docs/OnViewValueEditing.md, sections 3.1 and 3.2. Each value is held by a
 * QuantitySpinBox -- the panel's own, or a hidden one -- which brings its
 * unit, its limits, its expression and the code that writes the property; a
 * handle (a LinearGizmo, or a RotationGizmo for an angle) may drive the same
 * box. The set adds a label showing
 * the value, and the ACTIVE value: made so by a click on its handle or its
 * label, or by Tab in the view; its handle is highlighted, and while it is
 * active a key typed in the view goes to it:
 *   - a digit, a sign or a decimal point opens its label for typing with it;
 *   - Enter opens its label with the value selected;
 *   - Tab and Shift+Tab move to the next and the previous value;
 *   - Escape leaves no value active.
 *   - Space opens the active handle's menu, as a right click on it does.
 * In the open label (entry 136, the reporter 2026-10-15 10:07 and 10:13):
 * Enter commits (the box gets the value, and with it the property) and the
 * label stays open; Escape drops what was typed since the last Enter and
 * closes it; Tab commits, closes it and makes the next value active; the
 * focus going elsewhere commits and closes it. An open label is one undo
 * step, as a drag of a handle is; Esc or another mouse button during a drag
 * cancels it, the value back to where it began (stepBegun/stepEnded).
 *
 * A value may have several handles -- a fillet's default radius has one on
 * every edge that takes it; making one active highlights them all. A label is
 * either always shown at the top middle of the view (a feature's default
 * value, which stands for no single place) or shown beside its active handle
 * only. A right click on a handle opens the value's menu, and the pointer over
 * it shows a tool tip saying so.
 */
class GuiExport OnViewValueSet: public QObject
{
    Q_OBJECT

public:
    enum class Place
    {
        /// always shown, at the top middle of the view
        TopMiddle,
        /// shown beside its handle while that one is active
        AtHandle,
    };

    /// Fills the menu of a handle of a value
    using MenuProvider = std::function<void(QMenu& menu, Gizmo* handle)>;

    explicit OnViewValueSet(QObject* parent = nullptr);
    ~OnViewValueSet() override;

    /** Add a value; returns its index, which is also its place in the Tab
     * order. \a title leads the label's text ("Radius 2 mm"). A handle is
     * to be added before its container makes it (GizmoContainer::
     * appendGizmo): its menu and tool tip are set up here.
     */
    int addValue(QuantitySpinBox* box, const QString& title, Place place,
                 Gizmo* handle = nullptr);
    /// One more handle of the value at \a index
    void addHandle(int index, Gizmo* handle);
    /// Let go of the handles of the value at \a index, which the caller
    /// deletes after; the value stays
    void clearHandles(int index);
    /// Where a handle stands, in world coordinates: its label goes beside it
    void setHandleAnchor(Gizmo* handle, const Base::Vector3d& world);
    /// What the menu of the value at \a index offers
    void setMenu(int index, MenuProvider provider);
    /** The group of the value at \a index, 0 for none: while a value is
     * active, the handles of every value of its group are highlighted too
     * (docs/OnViewValueEditing.md 6.1)
     */
    void setGroup(int index, int group);
    /** Keep the first \a count values, drop the rest -- with their labels,
     * and with the hold on their handles, which the caller deletes after.
     */
    void truncate(int count);
    int count() const;
    /// The value \a handle belongs to, or -1
    int valueOf(const Gizmo* handle) const;

    /// Open the menu of \a handle, of the active handle when null; at the
    /// pointer, or at the handle
    void openMenu(Gizmo* handle = nullptr, bool atPointer = false);
    /// The handle made active last, if its value is still the active one
    Gizmo* getActiveHandle() const;

    /// Show the values in \a view; null takes them out of the view it had.
    void attach(ViewerContext* view);
    ViewerContext* getView() const;

    /// Restate every label from its box -- after a recompute, say.
    void refresh();

    /// Make the value at \a index the active one; -1 for none. \a handle,
    /// one of its handles, is the one its label goes beside.
    void setActive(int index, Gizmo* handle = nullptr);
    int getActive() const;

    /** Open the label of the value at \a index for typing. \a first, a key
     * typed in the view, is given to it, replacing the selected value.
     */
    bool startTyping(int index, QKeyEvent* first = nullptr);
    /// The index of the value whose label is open, or -1
    int getTyping() const;

    /// Where the default value's label is put: this far below the view's top.
    static constexpr int TopMargin = 28;
    /// Where a handle's label is put: this far right of and above the handle
    static constexpr int HandleOffset = 24;

Q_SIGNALS:
    /// The view's camera moved: a handle that depends on it may move too.
    void cameraChanged();
    void activeChanged(int index);
    /** A change that is one undo step however many writes it makes begins:
     * a drag of a handle, or a label opened for typing (entry 136). Ended
     * by stepEnded(); \a abort when a drag was cancelled, its changes to
     * be rolled back.
     */
    void stepBegun();
    void stepEnded(bool abort);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    struct Entry;

    void makeLabels();
    void dropLabels();
    void placeLabels();
    void showLabels();
    void updateLabel(int index);
    void highlight();
    void setupHandle(int index, Gizmo* handle);
    void onCamera();
    void onLabelClicked(EditableDatumLabel* label);
    void commit(int index, double value);
    void endTyping(bool keep);
    /// The keys of an open label's box, ahead of the label's own handling
    bool takeLabelKey(QKeyEvent* event);
    /// Make the next or the previous value active, as Tab does
    void moveActive(bool back);
    /// What is typed in the open label, written to its box if it is a value
    /// and not the box's already
    void writeTyped();
    void focusView();
    bool takeKey(QKeyEvent* event, bool overrideOnly);

    std::vector<std::unique_ptr<Entry>> entries;
    ViewerContext* view = nullptr;
    QPointer<QWidget> keyWidget;
    SoNodeSensor* cameraSensor = nullptr;
    fastsignals::scoped_connection connCameraReplaced;
    std::vector<QMetaObject::Connection> typingConnections;
    /// Keys taken on their press, whose release is taken too: the view acts
    /// on some releases (Escape leaves the edit, ViewProvider::eventCallback)
    std::vector<int> takenKeys;
    int active = -1;
    Gizmo* activeHandle = nullptr;
    int typing = -1;
    /// The handle being dragged, between its drag's start and end
    Gizmo* dragHandle = nullptr;
    /// The value whose handle the pointer is over, -1 for none: its handles
    /// and its group's lit as a shape under the pointer is (entry 144)
    int preselected = -1;
    /// A mouse button that cancelled a drag: its release is taken too
    int cancelButton = 0;
    /// The handle a right press was over: its release opens its menu. Taken
    /// at the view widget, ahead of the navigation, which opens the view's
    /// own menu on a right click in some styles before the scene sees it.
    Gizmo* rightPressHandle = nullptr;
};

}  // namespace Gui

#endif  // GUI_ONVIEWVALUE_H
