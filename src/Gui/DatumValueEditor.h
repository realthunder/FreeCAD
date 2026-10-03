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

#ifndef GUI_DATUMVALUEEDITOR_H
#define GUI_DATUMVALUEEDITOR_H

#include <memory>

#include <QIcon>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QTimer>
#include <fastsignals/signal.h>

#include <App/ObjectIdentifier.h>
#include <Base/Placement.h>
#include <Base/Quantity.h>
#include <Base/Unit.h>
#include <Inventor/SbVec3f.h>

#include "OnViewEntry.h"

class QFrame;
class QLabel;
class QLineEdit;
class QToolButton;
class QWidget;
class SoNodeSensor;
class SoSensor;

namespace App
{
class Expression;
}

namespace Gui
{

class ExpressionLineEdit;
class QuantitySpinBox;
class SoDatumLabel;
class ViewerContext;

/** The editor of a value the scene already draws: a dimension's number.
 *
 * One line, which holds a value or, when its text starts with '=', an
 * expression (FreeCAD's own rule, the spreadsheet cell's), with a line
 * under it that says what the expression gives or why the text gives
 * nothing; a toggle between driving and reference; for a value that can be
 * stated in two measures, a toggle between those; and a name. It stands
 * with its line over the number it edits, and the other rows grow away from
 * what the dimension measures, so that it hides nothing it edits
 * (docs/SketcherPort.md "One editor for a constraint's value").
 *
 * It decides nothing about the document. What a key does to the text is
 * decided here; what the text then does -- applied, moved to the next
 * value, thrown away -- is the caller's, which installs itself as a key
 * filter (installKeyFilter) and reads the entry (read).
 *
 * On a view without widgets the same widgets are built, never shown, and
 * driven by the client's keys (docs/ThinClient.md section 8.7).
 */
class GuiExport DatumValueEditor: public QObject, public OnViewEntry
{
    Q_OBJECT

public:
    /// What the editor is shown for
    struct Target
    {
        /// The label whose number the line stands over
        SoDatumLabel* label = nullptr;
        /// Where the line stands when there is no label (a value drawn as
        /// an icon: Snell's law), in the label plane; the other rows go up
        SbVec3f point {0, 0, 0};
        double value = 0.0;
        Base::Unit unit;
        /// Where an expression would be bound: what it is checked against
        /// and what the completer starts from
        App::ObjectIdentifier path;
        /// The expression bound there now, without the '=', or empty
        QString expression;
        /// -1 for no toggle, 0 a reference, 1 driving
        int driving = -1;
        /// A value that can be stated in either of two measures (a circle's
        /// size as its radius or as its diameter): -1 for none, else the
        /// one `value` is stated in, 0 or 1
        int measure = -1;
        /// What a value in measure 0 is multiplied by to say the same in
        /// measure 1
        double measureFactor = 1.0;
        bool nameShown = false;
        QString name;
    };

    /// What has been typed
    struct Entry
    {
        bool isExpression = false;
        Base::Quantity value;
        std::shared_ptr<App::Expression> expression;
        int driving = -1;
        /// The measure the value or the expression is meant in
        int measure = -1;
        QString name;
    };

    enum class Field
    {
        Value,
        Name
    };

    DatumValueEditor(ViewerContext* viewer, const Base::Placement& placement);
    ~DatumValueEditor() override;

    /// The toggle's two faces
    void setDrivingIcons(const QIcon& driving, const QIcon& reference);
    /// The two measures a value can be stated in, each with its face and
    /// its name: the first is Target::measure 0
    void setMeasures(const QIcon& first,
                     const QString& firstName,
                     const QIcon& second,
                     const QString& secondName);

    /// Show the editor for `target`, or move it there: the line holds the
    /// value or "=" and the expression, the number selected
    void edit(const Target& target);
    /// Stand at another label and bind to another path, what is typed
    /// kept: the constraint was renumbered underneath (an undo)
    void follow(SoDatumLabel* label, const App::ObjectIdentifier& path);
    /// Take it off the view
    void close();
    bool isOpen() const
    {
        return open;
    }

    /** What is typed.
     * False, with the reason in `why`, when the line holds no value or an
     * expression that cannot be bound.
     */
    bool read(Entry& entry, QString* why = nullptr) const;
    /// Whether anything differs from what edit() was given
    bool isModified() const;
    /// Say under the line why what was typed was refused
    void showError(const QString& message);

    void setKeysTo(Field field);
    /// Whether a widget of it has the Qt focus. Never on a view without widgets.
    bool hasFocus() const;
    /// Whether a completion list is open over it
    bool isCompleting() const;
    /// Install `filter` on every field that takes keys, after this one's
    /// own: the caller sees Tab, Enter and Escape first
    void installKeyFilter(QObject* filter);

    /** @name OnViewEntry */
    //@{
    bool isShownOnView() const override;
    void describe(State& state) const override;
    bool sendKeyEvent(QKeyEvent* event) override;
    void takeKeys() override;
    bool act(const Action& action) override;
    void forgetViewer(bool viewAlive) override;
    //@}

Q_SIGNALS:
    /// The toggle flipped, by a click, its key, or typing into a reference
    void drivingToggled(bool driving);
    /// The value is stated in the other measure now
    void measureToggled(int measure);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void lineEdited();
    void checkText();
    void toggleDriving();
    void setDriving(int driving);
    void toggleMeasure();
    void setMeasure(int measure);
    void updateRows();
    void place();
    void notifyChanged();
    SbVec3f world(const SbVec3f& onLabel) const;
    SbVec3f anchorPoint() const;
    SbVec3f awayPoint() const;
    static void labelMoved(void* data, SoSensor* sensor);

    ViewerContext* viewer;
    Base::Placement placement;
    Target target;
    bool open = false;
    Field keysTo = Field::Value;
    int driving = -1;
    /// the rows above the line rather than below it
    bool rowsAbove = false;

    // the widgets: shown on the desktop, never on a mirror. The frame's
    // parent is the view's widget there, which may go first.
    QPointer<QFrame> frame;
    QLineEdit* nameEdit = nullptr;
    QWidget* lineRow = nullptr;
    QToolButton* toggle = nullptr;
    QToolButton* measureToggle = nullptr;
    int measure = -1;
    QIcon measureIcons[2];
    QString measureNames[2];
    ExpressionLineEdit* line = nullptr;
    QLabel* resultLabel = nullptr;
    QIcon drivingIcon;
    QIcon referenceIcon;
    /// the spin box's parser and formatter, never shown
    std::unique_ptr<QuantitySpinBox> parser;

    QString startText;
    /// The text the editor itself last put in the line: while the line still
    /// holds it, nothing has been typed
    QString ownText;
    QString result;
    int resultLevel = 0;
    QTimer checkTimer;
    SoNodeSensor* labelSensor = nullptr;
    SoNodeSensor* cameraSensor = nullptr;
    /// A change of projection replaces the camera node the sensor is on
    fastsignals::scoped_connection connCameraReplaced;
};

}  // namespace Gui

#endif  // GUI_DATUMVALUEEDITOR_H
