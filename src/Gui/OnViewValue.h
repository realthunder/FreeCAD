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

#include <memory>
#include <vector>

#include <QObject>
#include <QPointer>
#include <QString>
#include <fastsignals/signal.h>

#include <FCGlobal.h>

class QKeyEvent;
class QWidget;
class SoNodeSensor;

namespace Gui
{

class EditableDatumLabel;
class LinearGizmo;
class QuantitySpinBox;
class ViewerContext;

/** The values of a feature in edit, shown and typed in the 3D view.
 *
 * docs/OnViewValueEditing.md, sections 3.1 and 3.2. Each value is held by a
 * QuantitySpinBox -- the panel's own, or a hidden one -- which brings its
 * unit, its limits, its expression and the code that writes the property; a
 * handle (a LinearGizmo) may drive the same box. The set adds a label showing
 * the value, and the ACTIVE value: made so by a click on its handle or its
 * label, or by Tab in the view; its handle is highlighted, and while it is
 * active a key typed in the view goes to it:
 *   - a digit, a sign or a decimal point opens its label for typing with it;
 *   - Enter opens its label with the value selected;
 *   - Tab and Shift+Tab move to the next and the previous value;
 *   - Escape leaves no value active.
 * In the open label Enter commits (the box gets the value, and with it the
 * property), Escape restores, Ctrl+Enter commits and accepts the edit.
 *
 * A label is always shown at the top middle of the view: a feature's default
 * value, which stands for no single place.
 */
class GuiExport OnViewValueSet: public QObject
{
    Q_OBJECT

public:
    enum class Place
    {
        /// always shown, at the top middle of the view
        TopMiddle,
        // AtHandle -- shown beside its handle while the handle is active --
        // comes with the fillet's per-edge values (the design's step 2)
    };

    explicit OnViewValueSet(QObject* parent = nullptr);
    ~OnViewValueSet() override;

    /** Add a value; returns its index, which is also its place in the Tab
     * order. \a title leads the label's text ("Radius 2 mm").
     */
    int addValue(QuantitySpinBox* box, const QString& title, Place place,
                 LinearGizmo* handle = nullptr);
    int count() const;

    /// Show the values in \a view; null takes them out of the view it had.
    void attach(ViewerContext* view);
    ViewerContext* getView() const;

    /// Restate every label from its box -- after a recompute, say.
    void refresh();

    /// Make the value at \a index the active one; -1 for none.
    void setActive(int index);
    int getActive() const;

    /** Open the label of the value at \a index for typing. \a first, a key
     * typed in the view, is given to it, replacing the selected value.
     */
    bool startTyping(int index, QKeyEvent* first = nullptr);
    /// The index of the value whose label is open, or -1
    int getTyping() const;

    /// Where the default value's label is put: this far below the view's top.
    static constexpr int TopMargin = 28;

Q_SIGNALS:
    /// The view's camera moved: a handle that depends on it may move too.
    void cameraChanged();
    void activeChanged(int index);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    struct Entry;

    void makeLabels();
    void dropLabels();
    void placeLabels();
    void updateLabel(int index);
    void onCamera();
    void onLabelClicked(EditableDatumLabel* label);
    void commit(int index, double value);
    void endTyping(bool keep);
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
    int typing = -1;
};

}  // namespace Gui

#endif  // GUI_ONVIEWVALUE_H
