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

#include "PreCompiled.h"

#ifndef _PreComp_
#include <algorithm>
#include <climits>
#include <cmath>
#include <QApplication>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QRegularExpression>
#include <QToolButton>
#include <QVBoxLayout>
#include <Inventor/nodes/SoCamera.h>
#include <Inventor/sensors/SoNodeSensor.h>
#endif

#include <App/DocumentObject.h>
#include <App/ExpressionParser.h>

#include "DatumValueEditor.h"
#include "DlgExpressionInput.h"
#include "ExprParams.h"
#include "ExpressionCompleter.h"
#include "QuantitySpinBox.h"
#include "SoDatumLabel.h"
#include "ViewerContext.h"


using namespace Gui;

namespace
{

/// The keys the editor and its caller claim before any shortcut can: Qt
/// asks a focused widget with a ShortcutOverride first, and a line edit
/// says no to most of these.
bool isClaimedKey(const QKeyEvent* key)
{
    const auto mods = key->modifiers() & (Qt::ShiftModifier | Qt::ControlModifier
                                          | Qt::AltModifier | Qt::MetaModifier);
    switch (key->key()) {
        case Qt::Key_Return:
        case Qt::Key_Enter:
        case Qt::Key_Escape:
        case Qt::Key_Tab:
        case Qt::Key_Backtab:
        case Qt::Key_F2:
            return true;
        case Qt::Key_D:
        case Qt::Key_R:
            return mods == (Qt::ControlModifier | Qt::ShiftModifier);
        default:
            return false;
    }
}

/// The length of the number a value's text starts with, which is what is
/// selected for typing over: "50.00 mm" -> 5
int numberLength(const QString& text)
{
    static const QRegularExpression number(QStringLiteral("^\\s*[-+]?[0-9.,']*[0-9]"));
    auto match = number.match(text);
    return match.hasMatch() ? int(match.capturedLength()) : int(text.size());
}

}  // namespace

DatumValueEditor::DatumValueEditor(ViewerContext* viewer, const Base::Placement& placement)
    : viewer(viewer)
    , placement(placement)
{
    // Null on a mirror, and that is the whole of the difference between the
    // two tiers: the widgets below are built, typed into and read back
    // identically, they are simply never shown (docs/ThinClient.md 8.7).
    QWidget* parent = viewer ? viewer->datumEditorParent() : nullptr;

    frame = new QFrame(parent);
    frame->setObjectName(QStringLiteral("DatumValueEditor"));
    frame->setFrameShape(QFrame::StyledPanel);
    frame->setAutoFillBackground(true);
    auto* rows = new QVBoxLayout(frame);
    rows->setContentsMargins(2, 2, 2, 2);
    rows->setSpacing(1);

    nameEdit = new QLineEdit(frame);
    nameEdit->setObjectName(QStringLiteral("DatumValueEditorName"));
    nameEdit->setPlaceholderText(tr("Name"));
    QFont small = nameEdit->font();
    small.setPointSizeF(small.pointSizeF() * 0.85);
    nameEdit->setFont(small);
    nameEdit->setFocusPolicy(Qt::ClickFocus);

    lineRow = new QWidget(frame);
    auto* row = new QHBoxLayout(lineRow);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(2);
    toggle = new QToolButton(lineRow);
    toggle->setObjectName(QStringLiteral("DatumValueEditorDriving"));
    toggle->setCheckable(true);
    toggle->setAutoRaise(true);
    // the keys stay in the line while it is clicked
    toggle->setFocusPolicy(Qt::NoFocus);
    toggle->setToolTip(tr("Driving or reference (Ctrl+Shift+D)"));
    measureToggle = new QToolButton(lineRow);
    measureToggle->setObjectName(QStringLiteral("DatumValueEditorMeasure"));
    measureToggle->setAutoRaise(true);
    measureToggle->setFocusPolicy(Qt::NoFocus);
    measureToggle->hide();
    line = new ExpressionLineEdit(lineRow, false, '=');
    line->setObjectName(QStringLiteral("DatumValueEditorLine"));
    line->setFocusPolicy(Qt::ClickFocus);
    line->setMinimumWidth(line->fontMetrics().horizontalAdvance(QStringLiteral("000000.00 mm")));
    row->addWidget(toggle);
    row->addWidget(measureToggle);
    row->addWidget(line, 1);

    resultLabel = new QLabel(frame);
    resultLabel->setObjectName(QStringLiteral("DatumValueEditorResult"));
    resultLabel->setFont(small);

    rows->addWidget(lineRow);
    frame->hide();

    line->installEventFilter(this);
    nameEdit->installEventFilter(this);
    connect(line, &QLineEdit::textEdited, this, [this]() {
        lineEdited();
    });
    connect(line, &QLineEdit::cursorPositionChanged, this, [this]() {
        notifyChanged();
    });
    connect(nameEdit, &QLineEdit::textEdited, this, [this]() {
        notifyChanged();
    });
    connect(toggle, &QToolButton::clicked, this, [this]() {
        toggleDriving();
    });
    connect(measureToggle, &QToolButton::clicked, this, [this]() {
        toggleMeasure();
    });

    checkTimer.setSingleShot(true);
    connect(&checkTimer, &QTimer::timeout, this, [this]() {
        checkText();
    });

    parser = std::make_unique<QuantitySpinBox>();
    parser->setMinimum(-INT_MAX);
    parser->setMaximum(INT_MAX);

    if (viewer) {
        viewer->addOnViewParameter(this);
    }
}

DatumValueEditor::~DatumValueEditor()
{
    close();
    if (viewer) {
        viewer->removeOnViewParameter(this);
    }
    if (target.label) {
        target.label->unref();
    }
    // The widgets have a parent on the desktop and none on a mirror; either
    // way they go with the frame.
    delete frame;
}

void DatumValueEditor::setDrivingIcons(const QIcon& drivingFace, const QIcon& referenceFace)
{
    drivingIcon = drivingFace;
    referenceIcon = referenceFace;
    setDriving(driving);
}

void DatumValueEditor::setMeasures(const QIcon& first,
                                   const QString& firstName,
                                   const QIcon& second,
                                   const QString& secondName)
{
    measureIcons[0] = first;
    measureIcons[1] = second;
    measureNames[0] = firstName;
    measureNames[1] = secondName;
    setMeasure(measure);
}

void DatumValueEditor::edit(const Target& next)
{
    if (next.label) {
        next.label->ref();
    }
    if (target.label) {
        target.label->unref();
    }
    target = next;

    // The completer, where there is a widget to show its list. A client
    // completes names on its own side from what it knows of the document.
    App::DocumentObject* object = target.path.getDocumentObject();
    if (frame->parentWidget() && object) {
        line->setDocumentObject(object);
    }

    parser->setUnit(target.unit);
    startText = target.expression.isEmpty()
        ? parser->textFromValue(Base::Quantity(target.value, target.unit))
        : QStringLiteral("=") + target.expression;
    line->setText(startText);
    ownText = startText;
    nameEdit->setText(target.name);
    driving = target.driving;
    setDriving(driving);
    setMeasure(target.measure);
    result.clear();
    resultLevel = 0;
    if (!target.expression.isEmpty()) {
        checkText();
    }

    // Typed over, as the dialog's box has it: the number, or the whole
    // expression after its '='.
    if (target.expression.isEmpty()) {
        line->setSelection(0, numberLength(startText));
    }
    else {
        line->setSelection(1, int(startText.size()) - 1);
    }

    if (labelSensor) {
        labelSensor->detach();
    }
    else {
        labelSensor = new SoNodeSensor(&DatumValueEditor::labelMoved, this);
    }
    if (target.label) {
        labelSensor->attach(target.label);
    }
    if (frame->parentWidget() && !cameraSensor && viewer && viewer->getCamera()) {
        cameraSensor = new SoNodeSensor(&DatumValueEditor::labelMoved, this);
        cameraSensor->attach(viewer->getCamera());
    }

    open = true;
    updateRows();
    if (frame->parentWidget()) {
        frame->show();
    }
    place();
    setKeysTo(Field::Value);
}

void DatumValueEditor::follow(SoDatumLabel* label, const App::ObjectIdentifier& path)
{
    if (label) {
        label->ref();
    }
    if (target.label) {
        target.label->unref();
    }
    target.label = label;
    target.path = path;
    if (labelSensor) {
        labelSensor->detach();
        if (label) {
            labelSensor->attach(label);
        }
    }
    place();
}

void DatumValueEditor::close()
{
    checkTimer.stop();
    if (labelSensor) {
        labelSensor->detach();
        delete labelSensor;
        labelSensor = nullptr;
    }
    if (cameraSensor) {
        cameraSensor->detach();
        delete cameraSensor;
        cameraSensor = nullptr;
    }
    if (line) {
        line->hideCompleter();
    }
    if (frame) {
        frame->hide();
    }
    if (open) {
        open = false;
        notifyChanged();
    }
}

void DatumValueEditor::labelMoved(void* data, SoSensor* /*sensor*/)
{
    static_cast<DatumValueEditor*>(data)->place();
}

bool DatumValueEditor::read(Entry& entry, QString* why) const
{
    const QString text = line->text().trimmed();
    entry.driving = driving;
    entry.measure = measure;
    entry.name = nameEdit->text().trimmed();
    if (text.startsWith(QLatin1Char('='))) {
        App::ExpressionFunctionCallDisabler disabler(!ExprParams::getEvalFuncOnEdit());
        auto check = Dialog::checkExpression(target.path, text.mid(1), target.unit, nullptr, false);
        if (!check.acceptable || !check.expression) {
            if (why) {
                *why = check.message.isEmpty() ? tr("Not an expression") : check.message;
            }
            return false;
        }
        entry.isExpression = true;
        entry.expression = check.expression;
        return true;
    }
    QString copy = text;
    int pos = 0;
    if (text.isEmpty() || parser->validate(copy, pos) != QValidator::Acceptable) {
        if (why) {
            *why = tr("Not a value");
        }
        return false;
    }
    entry.isExpression = false;
    entry.value = parser->valueFromText(text);
    return true;
}

bool DatumValueEditor::isModified() const
{
    return line->text() != startText || nameEdit->text().trimmed() != target.name.trimmed()
        || driving != target.driving || measure != target.measure;
}

void DatumValueEditor::showError(const QString& message)
{
    result = message;
    resultLevel = 3;
    updateRows();
    place();
    notifyChanged();
}

void DatumValueEditor::lineEdited()
{
    // Typing into a reference makes it driving: the dialog's rule, for a
    // value as for an '='.
    if (driving == 0 && line->text() != startText) {
        setDriving(1);
        Q_EMIT drivingToggled(true);
    }
    if (line->text().startsWith(QLatin1Char('='))) {
        // as the formula editor does: judged once the typing pauses
        checkTimer.start(300);
    }
    else {
        checkTimer.stop();
        checkText();
    }
    notifyChanged();
}

void DatumValueEditor::checkText()
{
    const QString text = line->text().trimmed();
    QString message;
    int level = 0;
    if (text.startsWith(QLatin1Char('='))) {
        App::ExpressionFunctionCallDisabler disabler(!ExprParams::getEvalFuncOnEdit());
        auto check = Dialog::checkExpression(target.path,
                                             text.mid(1),
                                             target.unit,
                                             nullptr,
                                             line->completerActive());
        message = check.message;
        level = int(check.level);
    }
    else if (!text.isEmpty()) {
        QString copy = text;
        int pos = 0;
        if (parser->validate(copy, pos) != QValidator::Acceptable) {
            message = tr("Not a value");
            level = 3;
        }
    }
    if (message != result || level != resultLevel) {
        result = message;
        resultLevel = level;
        updateRows();
        place();
        notifyChanged();
    }
}

void DatumValueEditor::toggleDriving()
{
    if (driving < 0) {
        return;
    }
    setDriving(driving ? 0 : 1);
    Q_EMIT drivingToggled(driving == 1);
    notifyChanged();
}

void DatumValueEditor::setDriving(int value)
{
    driving = value;
    toggle->setVisible(driving >= 0);
    QSignalBlocker block(toggle);
    toggle->setChecked(driving == 0);
    toggle->setIcon(driving == 0 ? referenceIcon : drivingIcon);
    // a reference's number is measured, and reads that way
    QPalette palette = line->palette();
    palette.setColor(QPalette::Text,
                     driving == 0 ? palette.color(QPalette::Disabled, QPalette::Text)
                                  : QApplication::palette().color(QPalette::Text));
    line->setPalette(palette);
}

void DatumValueEditor::toggleMeasure()
{
    if (measure < 0) {
        return;
    }
    const int next = measure ? 0 : 1;

    // The size stays what it is while nothing has been typed: the number the
    // editor put in the line is restated in the other measure. A number that
    // was typed is what the user means in the measure being chosen, and an
    // expression says what it says: both are left as they are.
    const QString text = line->text();
    if (text == ownText && !text.trimmed().startsWith(QLatin1Char('='))
        && target.measureFactor > 0.0) {
        QString copy = text;
        int pos = 0;
        if (parser->validate(copy, pos) == QValidator::Acceptable) {
            const Base::Quantity now = parser->valueFromText(text);
            const double factor = next == 1 ? target.measureFactor : 1.0 / target.measureFactor;
            ownText = parser->textFromValue(Base::Quantity(now.getValue() * factor, now.getUnit()));
            line->setText(ownText);
            line->setSelection(0, numberLength(ownText));
        }
    }

    setMeasure(next);
    Q_EMIT measureToggled(measure);
    notifyChanged();
}

void DatumValueEditor::setMeasure(int value)
{
    measure = value;
    measureToggle->setVisible(measure >= 0);
    if (measure < 0) {
        return;
    }
    const int which = measure ? 1 : 0;
    measureToggle->setIcon(measureIcons[which]);
    if (measureIcons[which].isNull()) {
        measureToggle->setText(measureNames[which]);
    }
    //: %1 is the measure a value is stated in now, e.g. Radius; %2 the other one
    measureToggle->setToolTip(tr("%1: switch to %2 (Ctrl+Shift+R)")
                                  .arg(measureNames[which], measureNames[which ? 0 : 1]));
}

void DatumValueEditor::updateRows()
{
    nameEdit->setVisible(target.nameShown);
    resultLabel->setVisible(!result.isEmpty());
    resultLabel->setText(result);
    static const char* colours[] = {"", "color: palette(link)", "color: #b07000", "color: #c62b1c"};
    resultLabel->setStyleSheet(QString::fromLatin1(colours[std::clamp(resultLevel, 0, 3)]));

    // The rows on the side away from the geometry, nearest to the line the
    // result (it is about the line), then the name.
    auto* rows = static_cast<QVBoxLayout*>(frame->layout());
    rows->removeWidget(nameEdit);
    rows->removeWidget(lineRow);
    rows->removeWidget(resultLabel);
    if (rowsAbove) {
        rows->addWidget(nameEdit);
        rows->addWidget(resultLabel);
        rows->addWidget(lineRow);
    }
    else {
        rows->addWidget(lineRow);
        rows->addWidget(resultLabel);
        rows->addWidget(nameEdit);
    }
}

SbVec3f DatumValueEditor::world(const SbVec3f& onLabel) const
{
    Base::Vector3d point(onLabel[0], onLabel[1], onLabel[2]);
    placement.multVec(point, point);
    return {float(point.x), float(point.y), float(point.z)};
}

SbVec3f DatumValueEditor::anchorPoint() const
{
    return world(target.label ? target.label->getLabelTextCenter() : target.point);
}

SbVec3f DatumValueEditor::awayPoint() const
{
    if (!target.label) {
        return world(target.point + SbVec3f(0, 1, 0));
    }
    return world(target.label->getLabelTextCenter() + target.label->getLabelAwayDirection());
}

void DatumValueEditor::place()
{
    if (!open || !viewer || !frame->parentWidget()) {
        // Nowhere to put it: a client places its own (8.7), from the two
        // world points describe() gives it.
        notifyChanged();
        return;
    }

    QWidget* canvas = viewer->getWidget();
    const QSize view = canvas ? canvas->size() : frame->parentWidget()->size();
    const QPoint number = viewer->toQPoint(viewer->getPointOnViewport(anchorPoint()));
    const QPoint away = viewer->toQPoint(viewer->getPointOnViewport(awayPoint()));
    const QPoint dir = away - number;
    const double length = std::hypot(double(dir.x()), double(dir.y()));
    // Up on the screen only when the geometry is clearly below the text; a
    // text beside what it measures takes its rows below.
    bool above = length > 0.0 && double(dir.y()) < -0.3 * length;

    auto layOut = [&](bool up) {
        rowsAbove = up;
        updateRows();
        frame->layout()->activate();
        frame->adjustSize();
        const QPoint lineCentre = lineRow->geometry().center();
        return number - lineCentre;
    };
    QPoint pos = layOut(above);
    const int height = frame->height();
    if (above && pos.y() < 0 && pos.y() + height <= view.height()) {
        pos = layOut(false);
    }
    else if (!above && pos.y() + height > view.height() && pos.y() >= 0) {
        pos = layOut(true);
    }
    pos.setX(std::clamp(pos.x(), 0, std::max(0, view.width() - frame->width())));
    pos.setY(std::clamp(pos.y(), 0, std::max(0, view.height() - frame->height())));
    frame->move(pos);
    frame->raise();
}

void DatumValueEditor::setKeysTo(Field field)
{
    if (field == Field::Name && !target.nameShown) {
        field = Field::Value;
    }
    keysTo = field;
    QLineEdit* edit = field == Field::Name ? nameEdit : static_cast<QLineEdit*>(line);
    if (frame->parentWidget()) {
        edit->setFocus();
    }
    if (field == Field::Name) {
        nameEdit->selectAll();
    }
    if (viewer) {
        viewer->onViewParameterFocused(this);
    }
    notifyChanged();
}

bool DatumValueEditor::hasFocus() const
{
    QWidget* focus = QApplication::focusWidget();
    return frame->parentWidget() && focus && frame->isAncestorOf(focus);
}

bool DatumValueEditor::isCompleting() const
{
    return line->completerActive();
}

void DatumValueEditor::installKeyFilter(QObject* filter)
{
    line->installEventFilter(filter);
    nameEdit->installEventFilter(filter);
}

bool DatumValueEditor::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() == QEvent::ShortcutOverride) {
        auto* key = static_cast<QKeyEvent*>(event);
        if (isClaimedKey(key)) {
            event->accept();
            return true;
        }
    }
    else if (event->type() == QEvent::KeyPress) {
        auto* key = static_cast<QKeyEvent*>(event);
        if (key->key() == Qt::Key_D
            && (key->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier))
                == (Qt::ControlModifier | Qt::ShiftModifier)) {
            toggleDriving();
            return true;
        }
        if (key->key() == Qt::Key_R
            && (key->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier))
                == (Qt::ControlModifier | Qt::ShiftModifier)) {
            toggleMeasure();
            return true;
        }
        if (key->key() == Qt::Key_F2) {
            setKeysTo(watched == nameEdit ? Field::Value : Field::Name);
            return true;
        }
        // '=' typed at the start of a value begins an expression in place
        // of all of it: the number is what is selected for typing over,
        // and its unit would stay behind ("= mm")
        if (watched == line && key->text() == QLatin1String("=")
            && !line->text().startsWith(QLatin1Char('='))
            && (line->hasSelectedText() ? line->selectionStart() == 0
                                        : line->cursorPosition() == 0)) {
            line->setText(QStringLiteral("="));
            line->setCursorPosition(1);
            lineEdited();
            return true;
        }
    }
    return QObject::eventFilter(watched, event);
}

bool DatumValueEditor::isShownOnView() const
{
    return open;
}

void DatumValueEditor::describe(State& state) const
{
    state.kind = "datum";
    state.anchor = anchorPoint();
    state.hasAway = true;
    state.away = awayPoint();
    state.text = line->text().toStdString();
    if (line->hasSelectedText()) {
        state.selStart = line->selectionStart();
        state.selLength = int(line->selectedText().size());
    }
    else {
        state.selStart = line->cursorPosition();
        state.selLength = 0;
    }
    state.set = isModified();
    state.field = keysTo == Field::Name ? "name" : "value";
    state.expression = line->text().trimmed().startsWith(QLatin1Char('='));
    state.result = result.toStdString();
    state.resultLevel = resultLevel;
    state.driving = driving;
    state.measure = measure;
    if (measure >= 0) {
        state.measureName = measureNames[measure ? 1 : 0].toStdString();
    }
    state.nameShown = target.nameShown;
    state.name = nameEdit->text().toStdString();
    if (nameEdit->hasSelectedText()) {
        state.nameSelStart = nameEdit->selectionStart();
        state.nameSelLength = int(nameEdit->selectedText().size());
    }
    else {
        state.nameSelStart = nameEdit->cursorPosition();
        state.nameSelLength = 0;
    }
    if (App::DocumentObject* object = target.path.getDocumentObject()) {
        state.objectName = object->getNameInDocument() ? object->getNameInDocument() : "";
    }
}

bool DatumValueEditor::sendKeyEvent(QKeyEvent* event)
{
    if (!open) {
        return false;
    }
    // Straight at the field that has the keys: on a mirror nothing else
    // would take it there, an unshown widget is never focused. The filters
    // run, the caller's among them, as for a key the desktop delivers.
    QObject* field = keysTo == Field::Name ? static_cast<QObject*>(nameEdit)
                                           : static_cast<QObject*>(line);
    const bool handled = QApplication::sendEvent(field, event);
    notifyChanged();
    return handled;
}

void DatumValueEditor::takeKeys()
{
    setKeysTo(keysTo);
}

bool DatumValueEditor::act(const Action& action)
{
    if (!open) {
        return false;
    }
    if (action.name == "toggle") {
        toggleDriving();
        return driving >= 0;
    }
    if (action.name == "measure") {
        toggleMeasure();
        return measure >= 0;
    }
    if (action.name == "field") {
        setKeysTo(action.text == "name" ? Field::Name : Field::Value);
        return true;
    }
    if (action.name == "replace") {
        const QString text = line->text();
        const QString insert = QString::fromStdString(action.text);
        if (action.start < 0 || action.length < 0
            || action.start + action.length > int(text.size())) {
            return false;
        }
        line->setText(text.left(action.start) + insert
                      + text.mid(action.start + action.length));
        line->setCursorPosition(action.start + int(insert.size()));
        lineEdited();
        return true;
    }
    return false;
}

void DatumValueEditor::notifyChanged()
{
    if (viewer) {
        viewer->onViewParametersChanged();
    }
}

#include "moc_DatumValueEditor.cpp"
