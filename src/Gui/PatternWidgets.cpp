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

#include "PreCompiled.h"

#ifndef _PreComp_
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <sstream>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QFont>
#include <QFormLayout>
#include <QLabel>
#include <QVBoxLayout>
#include <Inventor/SoPath.h>
#include <Inventor/SoPickedPoint.h>
#include <Inventor/events/SoLocation2Event.h>
#include <Inventor/events/SoMouseButtonEvent.h>
#include <Inventor/nodes/SoAnnotation.h>
#include <Inventor/nodes/SoEventCallback.h>
#include <Inventor/nodes/SoPickStyle.h>
#include <Inventor/nodes/SoTranslation.h>
#endif

#include <App/Document.h>
#include <App/DocumentObject.h>
#include <App/Pattern.h>
#include <App/PropertyLinks.h>
#include <App/PropertyStandard.h>
#include <App/PropertyUnits.h>
#include <Base/Exception.h>
#include <Base/Quantity.h>
#include <Base/Tools.h>

#include "Command.h"
#include "EditableDatumLabel.h"
#include "Inventor/SoToggleMarker.h"
#include "PatternWidgets.h"
#include "QuantitySpinBox.h"
#include "SpinBox.h"
#include "ViewerContext.h"

using namespace Gui;

// Rows of individual spacings shown at most; the rest are in the property
// editor. A spin box each is fine for a few dozen gaps, not for thousands.
static constexpr int MaxSpacingRows = 100;

// ----------------------------------------------------------------------------

ComboLinks::ComboLinks(QComboBox& combo)
{
    setCombo(combo);
}

ComboLinks::~ComboLinks()
{
    _combo = nullptr;
    clear();
}

void ComboLinks::setCombo(QComboBox& combo)
{
    assert(!_combo);
    _combo = &combo;
    _combo->clear();
}

int ComboLinks::addLink(const App::PropertyLinkSub& lnk, const QString& itemText)
{
    if (!_combo) {
        return 0;
    }
    _combo->addItem(itemText);
    auto newItem = new App::PropertyLinkSub();
    linksInList.push_back(newItem);
    newItem->Paste(lnk);
    if (newItem->getValue() && !doc) {
        doc = newItem->getValue()->getDocument();
    }
    return static_cast<int>(linksInList.size()) - 1;
}

int ComboLinks::addLink(App::DocumentObject* linkObj,
                        const std::string& linkSubname,
                        const QString& itemText)
{
    if (!_combo) {
        return 0;
    }
    _combo->addItem(itemText);
    auto newItem = new App::PropertyLinkSub();
    linksInList.push_back(newItem);
    newItem->setValue(linkObj, std::vector<std::string>(1, linkSubname));
    if (newItem->getValue() && !doc) {
        doc = newItem->getValue()->getDocument();
    }
    return static_cast<int>(linksInList.size()) - 1;
}

int ComboLinks::addSelectItem(const QString& itemText)
{
    App::PropertyLinkSub none;
    int index = addLink(none, itemText);
    selectItems.insert(index);
    return index;
}

bool ComboLinks::isSelectItem(int index) const
{
    return selectItems.count(index) > 0;
}

bool ComboLinks::isCurrentSelectItem() const
{
    return _combo && isSelectItem(_combo->currentIndex());
}

void ComboLinks::clear()
{
    for (auto link : linksInList) {
        delete link;
    }
    // The links were deleted and never forgotten, so that a combo refilled
    // matched its new items against freed ones
    linksInList.clear();
    selectItems.clear();
    if (_combo) {
        _combo->clear();
    }
}

App::PropertyLinkSub& ComboLinks::getLink(int index) const
{
    if (index < 0 || index >= static_cast<int>(linksInList.size())) {
        THROWM(Base::IndexError, "ComboLinks::getLink:Index out of range")
    }
    if (linksInList[index]->getValue() && doc && !(doc->isIn(linksInList[index]->getValue()))) {
        THROWM(Base::ValueError, "Linked object is not in the document; it may have been deleted")
    }
    return *(linksInList[index]);
}

App::PropertyLinkSub& ComboLinks::getCurrentLink() const
{
    assert(_combo);
    return getLink(_combo->currentIndex());
}

int ComboLinks::setCurrentLink(const App::PropertyLinkSub& lnk)
{
    for (std::size_t i = 0; i < linksInList.size(); i++) {
        if (isSelectItem(static_cast<int>(i))) {
            continue;
        }
        App::PropertyLinkSub& it = *(linksInList[i]);
        // No object is the same link whatever its subs: an entry added as
        // (nullptr, "") holds one empty sub, a property never set holds none
        if (lnk.getValue() == it.getValue()
            && (!lnk.getValue() || lnk.getSubValues() == it.getSubValues())) {
            QSignalBlocker blocker(_combo);
            _combo->setCurrentIndex(static_cast<int>(i));
            return static_cast<int>(i);
        }
    }
    return -1;
}

QComboBox& ComboLinks::combo() const
{
    assert(_combo);
    return *_combo;
}

// ----------------------------------------------------------------------------

void Gui::fillPatternTypeCombo(QComboBox* combo)
{
    const QString texts[] = {
        QCoreApplication::translate("Gui::PatternWidgets", "Linear"),
        QCoreApplication::translate("Gui::PatternWidgets", "Polar"),
        QCoreApplication::translate("Gui::PatternWidgets", "Circular"),
        QCoreApplication::translate("Gui::PatternWidgets", "Along a path"),
        QCoreApplication::translate("Gui::PatternWidgets", "On points"),
    };
    int i = 0;
    for (const auto& text : texts) {
        if (i < combo->count()) {
            combo->setItemText(i, text);
        }
        else {
            combo->addItem(text);
        }
        ++i;
    }
}

QString Gui::patternReferenceText(const App::DocumentObject* obj,
                                  const std::vector<std::string>& subs)
{
    if (!obj || !obj->isAttachedToDocument()) {
        return {};
    }
    QString text = QString::fromUtf8(obj->Label.getValue());
    QStringList elements;
    for (const auto& sub : subs) {
        if (!sub.empty()) {
            elements << QString::fromUtf8(sub.c_str());
        }
    }
    if (!elements.isEmpty()) {
        text += QStringLiteral(":") + elements.join(QStringLiteral(","));
    }
    return text;
}

std::string Gui::patternReferencePython(const App::DocumentObject* obj,
                                        const std::vector<std::string>& subs)
{
    std::ostringstream ss;
    ss << "(" << (obj ? Command::getObjectCmd(obj) : std::string("None")) << ", [";
    const char* sep = "";
    for (const auto& sub : subs) {
        ss << sep << "'" << sub << "'";
        sep = ", ";
    }
    ss << "])";
    if (!obj && subs.empty()) {
        return "None";
    }
    return ss.str();
}

// ----------------------------------------------------------------------------

PatternDirectionWidget::Properties
PatternDirectionWidget::propertiesOf(const App::PropertyContainer& obj, Kind kind, bool second)
{
    const bool polar = kind == Kind::Polar;
    const char* suffix = second ? "2" : "";
    auto find = [&obj, suffix](const char* base, bool withSuffix = true) {
        std::string name = base;
        if (withSuffix) {
            name += suffix;
        }
        return App::Pattern::getProperty(obj, name.c_str());
    };
    Properties props;
    props.reference = dynamic_cast<App::PropertyLinkSub*>(polar ? find("Axis", false)
                                                                : find("Direction"));
    props.reversed = dynamic_cast<App::PropertyBool*>(find("Reversed"));
    props.mode = dynamic_cast<App::PropertyEnumeration*>(find("Mode"));
    props.extent = dynamic_cast<App::PropertyQuantity*>(polar ? find("Angle", false)
                                                              : find("Length"));
    props.spacing = dynamic_cast<App::PropertyQuantity*>(find("Offset"));
    props.occurrences = dynamic_cast<App::PropertyIntegerConstraint*>(find("Occurrences"));
    props.spacings = dynamic_cast<App::PropertyFloatList*>(find("Spacings"));
    props.spacingPattern = dynamic_cast<App::PropertyFloatList*>(find("SpacingPattern"));
    return props;
}

PatternDirectionWidget::PatternDirectionWidget(Kind kind, QWidget* parent)
    : QWidget(parent)
    , kind(kind)
{
    form = new QFormLayout(this);
    form->setContentsMargins(0, 0, 0, 0);

    labelReference = new QLabel(this);
    comboReference = new QComboBox(this);
    form->addRow(labelReference, comboReference);
    refLinks.setCombo(*comboReference);

    checkReverse = new QCheckBox(this);
    form->addRow(checkReverse);

    labelMode = new QLabel(this);
    comboMode = new QComboBox(this);
    comboMode->addItem(QString());
    comboMode->addItem(QString());
    form->addRow(labelMode, comboMode);

    Base::Unit unit = kind == Kind::Linear ? Base::Unit::Length : Base::Unit::Angle;
    auto makeSpin = [this, unit]() {
        auto spin = new Gui::QuantitySpinBox(this);
        spin->setUnit(unit);
        spin->setKeyboardTracking(false);
        if (this->kind == Kind::Polar) {
            spin->setMinimum(-360.0);
            spin->setMaximum(360.0);
        }
        else {
            spin->setMinimum(0.0);
            spin->setMaximum(std::numeric_limits<int>::max());
        }
        return spin;
    };

    labelExtent = new QLabel(this);
    spinExtent = makeSpin();
    form->addRow(labelExtent, spinExtent);

    labelSpacing = new QLabel(this);
    spinSpacing = makeSpin();
    form->addRow(labelSpacing, spinSpacing);

    checkIndividual = new QCheckBox(this);
    form->addRow(checkIndividual);

    spacingsBox = new QWidget(this);
    auto boxLayout = new QVBoxLayout(spacingsBox);
    boxLayout->setContentsMargins(0, 0, 0, 0);
    spacingsForm = new QFormLayout();
    boxLayout->addLayout(spacingsForm);
    labelMoreSpacings = new QLabel(spacingsBox);
    labelMoreSpacings->setWordWrap(true);
    boxLayout->addWidget(labelMoreSpacings);
    form->addRow(spacingsBox);

    labelOccurrences = new QLabel(this);
    spinOccurrences = new Gui::UIntSpinBox(this);
    form->addRow(labelOccurrences, spinOccurrences);

    retranslate();

    connect(comboReference, qOverload<int>(&QComboBox::activated), this, [this](int) {
        if (!blockUpdate) {
            Q_EMIT referenceActivated();
        }
    });
    connect(checkReverse, &QCheckBox::toggled, this, [this](bool on) {
        if (blockUpdate || !props.reversed) {
            return;
        }
        props.reversed->setValue(on);
        Q_EMIT changed();
    });
    connect(comboMode,
            qOverload<int>(&QComboBox::activated),
            this,
            &PatternDirectionWidget::onModeActivated);
    connect(spinExtent,
            qOverload<double>(&Gui::QuantitySpinBox::valueChanged),
            this,
            [this](double v) {
                if (blockUpdate || !props.extent) {
                    return;
                }
                props.extent->setValue(v);
                updateUI();  // the synced spacing
                Q_EMIT changed();
            });
    connect(spinSpacing,
            qOverload<double>(&Gui::QuantitySpinBox::valueChanged),
            this,
            [this](double v) {
                if (blockUpdate || !props.spacing) {
                    return;
                }
                props.spacing->setValue(v);
                updateUI();  // the synced extent, and the spacings that follow it
                Q_EMIT changed();
            });
    connect(spinOccurrences, &Gui::UIntSpinBox::unsignedChanged, this, [this](uint n) {
        if (blockUpdate || !props.occurrences) {
            return;
        }
        props.occurrences->setValue(n);
        updateUI();  // the synced value, and the rows of spacings
        Q_EMIT changed();
    });
    connect(checkIndividual,
            &QCheckBox::toggled,
            this,
            &PatternDirectionWidget::onIndividualToggled);
}

PatternDirectionWidget::~PatternDirectionWidget()
{
    clearLabels();
}

void PatternDirectionWidget::retranslate()
{
    if (kind == Kind::Linear) {
        labelReference->setText(tr("Direction"));
        comboMode->setItemText(0, tr("Overall length"));
        comboMode->setItemText(1, tr("Spacing"));
        labelExtent->setText(tr("Length"));
        labelSpacing->setText(tr("Spacing"));
    }
    else {
        labelReference->setText(tr("Axis"));
        comboMode->setItemText(0, tr("Overall angle"));
        comboMode->setItemText(1, tr("Angular spacing"));
        labelExtent->setText(tr("Angle"));
        labelSpacing->setText(tr("Spacing"));
    }
    checkReverse->setText(tr("Reverse direction"));
    labelMode->setText(tr("Mode"));
    checkIndividual->setText(tr("Individual spacings"));
    checkIndividual->setToolTip(tr("Set the spacing of each gap on its own. A gap left at the "
                                   "spacing above follows it."));
    labelOccurrences->setText(tr("Occurrences"));
    for (int i = 0; i < spacingsForm->rowCount(); ++i) {
        auto item = spacingsForm->itemAt(i, QFormLayout::LabelRole);
        if (auto label = item ? qobject_cast<QLabel*>(item->widget()) : nullptr) {
            label->setText(tr("Gap %1").arg(i + 1));
        }
    }
    labelMoreSpacings->setText(tr("The gaps after the first %1 are in the property editor, "
                                  "as Spacings.")
                                   .arg(MaxSpacingRows));
}

void PatternDirectionWidget::bind(const Properties& p)
{
    props = p;
    {
        // A range clamping the spin boxes' first value must not reach the
        // properties: the minimum of Occurrences turned 3 into 1
        Base::StateLocker lock(blockUpdate);
        spinExtent->bind(*props.extent);
        spinSpacing->bind(*props.spacing);
        spinOccurrences->bind(*props.occurrences);
        spinOccurrences->setMinimum(props.occurrences->getMinimum());
        spinOccurrences->setMaximum(props.occurrences->getMaximum());
    }
    individual = hasIndividualSpacings();
    updateUI();
}

int PatternDirectionWidget::gapCount() const
{
    return props.occurrences ? std::max(0L, props.occurrences->getValue() - 1) : 0;
}

double PatternDirectionWidget::fallbackSpacing(int index) const
{
    // As App::Pattern::getSpacing() without the individual spacing
    return App::Pattern::getSpacing({},
                                    props.spacingPattern->getValues(),
                                    props.spacing->getValue(),
                                    index);
}

bool PatternDirectionWidget::hasIndividualSpacings() const
{
    if (!props.spacings) {
        return false;
    }
    for (double v : props.spacings->getValues()) {
        if (v != -1.0) {
            return true;
        }
    }
    return false;
}

void PatternDirectionWidget::updateUI()
{
    if (!props.mode) {
        return;
    }
    Base::StateLocker lock(blockUpdate);

    if (comboReference->count()) {
        if (refLinks.setCurrentLink(*props.reference) == -1) {
            // failed to set current, because the link isn't in the list yet
            refLinks.addLink(*props.reference,
                             patternReferenceText(props.reference->getValue(),
                                                  props.reference->getSubValues()));
            refLinks.setCurrentLink(*props.reference);
        }
    }

    checkReverse->setChecked(props.reversed->getValue());
    comboMode->setCurrentIndex(props.mode->getValue());
    spinExtent->setValue(props.extent->getValue());
    spinSpacing->setValue(props.spacing->getValue());
    spinOccurrences->setValue(props.occurrences->getValue());

    if (hasIndividualSpacings()) {
        individual = true;
    }
    checkIndividual->setChecked(individual);

    rebuildSpacingRows();
    adaptVisibilityToMode();
    refreshLabels();
}

void PatternDirectionWidget::adaptVisibilityToMode()
{
    bool spacing = props.mode && props.mode->getValue() == 1;
    form->setRowVisible(spinExtent, !spacing);
    form->setRowVisible(spinSpacing, spacing);
    // No gap to set with one occurrence, or none but the one with two
    form->setRowVisible(checkIndividual, spacing && gapCount() > 1);
    form->setRowVisible(spacingsBox, spacing && individual && gapCount() > 1);
}

void PatternDirectionWidget::rebuildSpacingRows()
{
    int rows = std::min(gapCount(), MaxSpacingRows);
    if (!individual) {
        rows = 0;
    }

    while (static_cast<int>(spacingSpins.size()) > rows) {
        spacingsForm->removeRow(spacingsForm->rowCount() - 1);
        spacingSpins.pop_back();
    }
    Base::Unit unit = kind == Kind::Linear ? Base::Unit::Length : Base::Unit::Angle;
    while (static_cast<int>(spacingSpins.size()) < rows) {
        int index = static_cast<int>(spacingSpins.size());
        auto spin = new Gui::QuantitySpinBox(spacingsBox);
        spin->setUnit(unit);
        spin->setKeyboardTracking(false);
        spin->setMinimum(spinSpacing->minimum());
        spin->setMaximum(spinSpacing->maximum());
        spacingsForm->addRow(new QLabel(tr("Gap %1").arg(index + 1), spacingsBox), spin);
        spacingSpins.push_back(spin);
        connect(spin,
                qOverload<double>(&Gui::QuantitySpinBox::valueChanged),
                this,
                [this, index](double v) {
                    onSpacingEdited(index, v);
                });
    }
    labelMoreSpacings->setVisible(individual && gapCount() > MaxSpacingRows);

    const auto& spacings = props.spacings->getValues();
    for (int i = 0; i < rows; ++i) {
        bool set = i < static_cast<int>(spacings.size()) && spacings[i] != -1.0;
        QSignalBlocker blocker(spacingSpins[i]);
        spacingSpins[i]->setValue(set ? spacings[i] : fallbackSpacing(i));
        // An individual spacing does not follow the one above
        QFont font = spacingSpins[i]->font();
        font.setBold(set);
        spacingSpins[i]->setFont(font);
    }
}

void PatternDirectionWidget::onModeActivated(int index)
{
    if (blockUpdate || !props.mode) {
        return;
    }
    props.mode->setValue(index);
    updateUI();
    Q_EMIT changed();
}

void PatternDirectionWidget::onIndividualToggled(bool on)
{
    if (blockUpdate || !props.spacings) {
        return;
    }
    individual = on;
    bool cleared = false;
    if (!on && hasIndividualSpacings()) {
        props.spacings->setValues(std::vector<double>(gapCount(), -1.0));
        cleared = true;
    }
    updateUI();
    if (cleared) {
        Q_EMIT changed();
    }
}

void PatternDirectionWidget::onSpacingEdited(int index, double value)
{
    if (blockUpdate || !props.spacings) {
        return;
    }
    std::vector<double> spacings = props.spacings->getValues();
    spacings.resize(gapCount(), -1.0);
    if (index >= static_cast<int>(spacings.size())) {
        return;
    }
    // The spacing above typed back in follows it again
    double v = std::fabs(value - fallbackSpacing(index)) < 1e-9 ? -1.0 : value;
    if (spacings[index] == v) {
        return;
    }
    spacings[index] = v;
    props.spacings->setValues(spacings);
    updateUI();
    Q_EMIT changed();
}

void PatternDirectionWidget::apply(App::DocumentObject* obj) const
{
    if (!props.mode) {
        return;
    }
    FCMD_OBJ_CMD(obj,
                 props.reference->getName()
                     << " = "
                     << patternReferencePython(props.reference->getValue(),
                                               props.reference->getSubValues()));
    FCMD_OBJ_CMD(obj,
                 props.reversed->getName() << " = "
                                           << (props.reversed->getValue() ? "True" : "False"));
    FCMD_OBJ_CMD(obj, props.mode->getName() << " = " << props.mode->getValue());
    spinExtent->apply();
    spinSpacing->apply();
    spinOccurrences->apply();
    if (hasIndividualSpacings()) {
        std::ostringstream ss;
        ss.precision(std::numeric_limits<double>::max_digits10);
        ss << "[";
        const char* sep = "";
        for (double v : props.spacings->getValues()) {
            ss << sep << v;
            sep = ", ";
        }
        ss << "]";
        FCMD_OBJ_CMD(obj, props.spacings->getName() << " = " << ss.str());
    }
}

void PatternDirectionWidget::showLabels(ViewerContext* view, const PatternLabelFrame& frame)
{
    if (view != labelView) {
        clearLabels();
        labelView = view;
    }
    labelFrame = frame;
    refreshLabels();
}

void PatternDirectionWidget::clearLabels()
{
    endLabelEdit();
    onViewLabels.clear();
    labelView = nullptr;
}

void PatternDirectionWidget::refreshLabels()
{
    if (!labelView || !props.mode) {
        return;
    }
    const bool extent = props.mode->getValue() == 0;
    const bool polar = kind == Kind::Polar;
    // The extent as one, or each gap, as many as the panel has rows for
    int count = extent ? std::min(gapCount(), 1) : std::min(gapCount(), MaxSpacingRows);

    // Kept while they last, so that the one in edit keeps its box
    while (static_cast<int>(onViewLabels.size()) > count) {
        if (onViewLabels.back()->isInEdit()) {
            endLabelEdit();
        }
        onViewLabels.pop_back();
    }
    while (static_cast<int>(onViewLabels.size()) < count) {
        // A distance's dimension line keeps its offset from the view; an
        // angle's distance is its radius, which the frame gives
        auto label = std::make_unique<EditableDatumLabel>(labelView,
                                                          labelFrame.placement,
                                                          /*autoDistance=*/!polar);
        label->setLabelType(polar ? SoDatumLabel::ANGLE : SoDatumLabel::DISTANCE,
                            EditableDatumLabel::Function::Dimensioning);
        label->setPickable(true);
        connect(label.get(),
                &EditableDatumLabel::clicked,
                this,
                &PatternDirectionWidget::onLabelClicked);
        label->activate();
        onViewLabels.push_back(std::move(label));
    }

    const Base::Unit unit = polar ? Base::Unit::Angle : Base::Unit::Length;
    const auto& spacings = props.spacings->getValues();
    // Along the direction, or the angle past the original's, in radians
    double position = 0.0;
    for (int i = 0; i < count; ++i) {
        auto& label = onViewLabels[i];
        bool set = true;
        double value = props.extent->getValue();
        if (!extent) {
            set = i < static_cast<int>(spacings.size()) && spacings[i] != -1.0;
            value = set ? spacings[i] : fallbackSpacing(i);
        }
        label->setPlacement(labelFrame.placement);
        if (polar) {
            const double range = Base::toRadians(value);
            label->setPoints(Base::Vector3d(), Base::Vector3d());
            // SoDatumLabel draws an angle's arc at twice its distance
            label->setLabelDistance(labelFrame.radius / 2);
            label->setLabelStartAngle(labelFrame.startAngle + position);
            label->setLabelRange(range);
            position += range;
        }
        else {
            label->setPoints(Base::Vector3d(position, 0, 0), Base::Vector3d(position + value, 0, 0));
            position += value;
        }
        if (!label->isInEdit()) {
            label->label->string = Base::Quantity(value, unit).getUserString().c_str();
            // A gap of its own stands out from one following the spacing
            if (set) {
                label->setActivatedColor();
            }
            else {
                label->setDeactivatedColor();
            }
        }
    }
}

void PatternDirectionWidget::onLabelClicked(EditableDatumLabel* label)
{
    auto it = std::find_if(onViewLabels.begin(), onViewLabels.end(), [label](const auto& l) {
        return l.get() == label;
    });
    if (it == onViewLabels.end() || !props.mode || label->isInEdit()) {
        return;
    }
    const int index = static_cast<int>(it - onViewLabels.begin());

    // One box at a time: another one open is abandoned
    endLabelEdit();
    for (auto& other : onViewLabels) {
        if (other->isInEdit()) {
            other->stopEdit(false);
        }
    }
    refreshLabels();

    const bool extent = props.mode->getValue() == 0;
    double value = props.extent->getValue();
    if (!extent) {
        const auto& spacings = props.spacings->getValues();
        bool set = index < static_cast<int>(spacings.size()) && spacings[index] != -1.0;
        value = set ? spacings[index] : fallbackSpacing(index);
    }
    label->startEdit(value);
    label->setSpinboxValue(value, kind == Kind::Polar ? Base::Unit::Angle : Base::Unit::Length);
    // Setting the value again unselected the number, and the box reselects
    // it only where Qt gives it the focus -- never on a mirror, where the
    // digits typed would then go after the text rather than replace it
    label->setFocusToSpinbox();

    labelEdit.push_back(connect(label,
                                &EditableDatumLabel::editingFinished,
                                this,
                                [this, index](double v) {
                                    commitLabel(index, v);
                                }));
    labelEdit.push_back(connect(label, &EditableDatumLabel::parameterUnset, this, [this, index]() {
        resetLabel(index);
    }));
    // Escape: the label has already put its value back and closed
    labelEdit.push_back(connect(label, &EditableDatumLabel::editingCanceled, this, [this]() {
        endLabelEdit();
        refreshLabels();
    }));
    labelEdit.push_back(connect(label, &EditableDatumLabel::focusLost, this, [this, label]() {
        endLabelEdit();
        label->stopEdit(false);
        refreshLabels();
    }));
}

void PatternDirectionWidget::endLabelEdit()
{
    for (const auto& connection : labelEdit) {
        disconnect(connection);
    }
    labelEdit.clear();
}

void PatternDirectionWidget::commitLabel(int index, double value)
{
    endLabelEdit();
    if (index < static_cast<int>(onViewLabels.size())) {
        onViewLabels[index]->stopEdit();
    }
    if (props.mode->getValue() == 0) {
        props.extent->setValue(value);
        updateUI();
        Q_EMIT changed();
    }
    else {
        onSpacingEdited(index, value);
        refreshLabels();
    }
}

void PatternDirectionWidget::resetLabel(int index)
{
    endLabelEdit();
    if (index < static_cast<int>(onViewLabels.size())) {
        onViewLabels[index]->stopEdit(false);
    }
    if (props.mode->getValue() == 0 || !props.spacings) {
        refreshLabels();
        return;
    }
    std::vector<double> spacings = props.spacings->getValues();
    spacings.resize(gapCount(), -1.0);
    if (index >= static_cast<int>(spacings.size()) || spacings[index] == -1.0) {
        refreshLabels();
        return;
    }
    spacings[index] = -1.0;
    props.spacings->setValues(spacings);
    updateUI();
    Q_EMIT changed();
}

bool Gui::patternLabelFrame(PatternDirectionWidget::Kind kind,
                            const App::PropertyContainer& obj,
                            const App::Pattern::Context& context,
                            bool second,
                            const Base::Matrix4D& toWorld,
                            const Base::Vector3d& origin,
                            PatternLabelFrame& frame)
{
    Base::Placement local;
    try {
        if (kind == PatternDirectionWidget::Kind::Linear) {
            Base::Vector3d dir = App::Pattern::getDirection(obj, context, second);
            local = Base::Placement(origin, Base::Rotation(Base::Vector3d(1, 0, 0), dir));
            frame.radius = 0.0;
            frame.startAngle = 0.0;
        }
        else {
            auto axis = App::Pattern::getAxis(obj, context);
            Base::Vector3d dir = axis.direction;
            dir.Normalize();
            // On the axis level with the original, so that the arc passes
            // through it
            Base::Vector3d center = axis.base + dir * ((origin - axis.base) * dir);
            Base::Rotation rot(Base::Vector3d(0, 0, 1), dir);
            Base::Vector3d radial = origin - center;
            frame.radius = radial.Length();
            frame.startAngle = 0.0;
            if (frame.radius > 1e-7) {
                frame.startAngle = rot.multVec(Base::Vector3d(1, 0, 0)).GetAngleOriented(radial, dir);
            }
            else {
                // The original on the axis: an arc of some size all the same
                frame.radius = 1.0;
            }
            local = Base::Placement(center, rot);
        }
    }
    catch (const Base::Exception&) {
        return false;
    }
    frame.placement = Base::Placement(toWorld) * local;
    return true;
}

// ----------------------------------------------------------------------------

PatternParametersWidget::Kind PatternParametersWidget::kindOf(App::Pattern::Type type)
{
    switch (type) {
        case App::Pattern::Type::Circular:
            return Kind::Circular;
        case App::Pattern::Type::Path:
            return Kind::Path;
        default:
            return Kind::Point;
    }
}

PatternParametersWidget::PatternParametersWidget(Kind kind, QWidget* parent)
    : QWidget(parent)
    , kind(kind)
{
    form = new QFormLayout(this);
    form->setContentsMargins(0, 0, 0, 0);

    comboReference = new QComboBox(this);
    refLinks.setCombo(*comboReference);
    connect(comboReference, qOverload<int>(&QComboBox::activated), this, [this](int) {
        if (!blockUpdate) {
            Q_EMIT referenceActivated();
        }
    });

    auto quantity = [this](const char* name) {
        auto spin = new QuantitySpinBox(this);
        spin->setUnit(Base::Unit::Length);
        spin->setKeyboardTracking(false);
        spin->setMinimum(0.0);
        spin->setMaximum(std::numeric_limits<int>::max());
        connect(spin, qOverload<double>(&QuantitySpinBox::valueChanged), this, [this, name](double v) {
            if (blockUpdate || !object) {
                return;
            }
            if (auto prop =
                    dynamic_cast<App::PropertyFloat*>(App::Pattern::getProperty(*object, name))) {
                prop->setValue(v);
                Q_EMIT changed();
            }
        });
        addRow(name, spin);
    };
    auto count = [this](const char* name) {
        auto spin = new UIntSpinBox(this);
        connect(spin, &UIntSpinBox::unsignedChanged, this, [this, name](uint v) {
            if (blockUpdate || !object) {
                return;
            }
            if (auto prop =
                    dynamic_cast<App::PropertyInteger*>(App::Pattern::getProperty(*object, name))) {
                prop->setValue(v);
                Q_EMIT changed();
            }
        });
        addRow(name, spin);
    };
    auto check = [this](const char* name) {
        auto box = new QCheckBox(this);
        connect(box, &QCheckBox::toggled, this, [this, name](bool on) {
            if (blockUpdate || !object) {
                return;
            }
            if (auto prop =
                    dynamic_cast<App::PropertyBool*>(App::Pattern::getProperty(*object, name))) {
                prop->setValue(on);
                updateUI();  // the rows the value hides
                Q_EMIT changed();
            }
        });
        addRow(name, box, false);
    };

    switch (kind) {
        case Kind::Circular:
            addRow("Axis", comboReference);
            quantity("RadialDistance");
            quantity("TangentialDistance");
            count("NumberCircles");
            count("Symmetry");
            break;
        case Kind::Path: {
            addRow("Path", comboReference);
            auto mode = new QComboBox(this);
            for (int i = 0; i < 3; ++i) {
                mode->addItem(QString());
            }
            connect(mode, qOverload<int>(&QComboBox::activated), this, [this](int index) {
                if (blockUpdate || !object) {
                    return;
                }
                if (auto prop = dynamic_cast<App::PropertyEnumeration*>(
                        App::Pattern::getProperty(*object, "SpacingMode"))) {
                    prop->setValue(index);
                    updateUI();
                    Q_EMIT changed();
                }
            });
            addRow("SpacingMode", mode);
            count("Count");
            quantity("Spacing");
            quantity("StartOffset");
            quantity("EndOffset");
            check("ReversePath");
            check("Align");
            break;
        }
        case Kind::Point:
            addRow("PointObject", comboReference);
            break;
    }
    retranslate();
}

PatternParametersWidget::~PatternParametersWidget() = default;

void PatternParametersWidget::addRow(const char* name, QWidget* editor, bool withLabel)
{
    Row row {name};
    row.editor = editor;
    if (withLabel) {
        row.label = new QLabel(this);
        form->addRow(row.label, editor);
    }
    else {
        form->addRow(editor);
    }
    rows.push_back(row);
}

PatternParametersWidget::Row* PatternParametersWidget::findRow(const char* name)
{
    for (auto& row : rows) {
        if (std::strcmp(row.name, name) == 0) {
            return &row;
        }
    }
    return nullptr;
}

void PatternParametersWidget::retranslate()
{
    auto setLabel = [this](const char* name, const QString& text) {
        if (auto row = findRow(name)) {
            if (row->label) {
                row->label->setText(text);
            }
            else if (auto box = qobject_cast<QCheckBox*>(row->editor)) {
                box->setText(text);
            }
        }
    };
    setLabel("Axis", tr("Axis"));
    setLabel("RadialDistance", tr("Radial distance"));
    setLabel("TangentialDistance", tr("Tangential distance"));
    setLabel("NumberCircles", tr("Circles"));
    setLabel("Symmetry", tr("Symmetry"));
    setLabel("Path", tr("Path"));
    setLabel("SpacingMode", tr("Mode"));
    setLabel("Count", tr("Count"));
    setLabel("Spacing", tr("Spacing"));
    setLabel("StartOffset", tr("Start offset"));
    setLabel("EndOffset", tr("End offset"));
    setLabel("ReversePath", tr("Reverse path"));
    setLabel("Align", tr("Align to path"));
    setLabel("PointObject", tr("Points"));
    if (auto row = findRow("SpacingMode")) {
        auto combo = static_cast<QComboBox*>(row->editor);
        combo->setItemText(0, tr("Fixed count"));
        combo->setItemText(1, tr("Fixed spacing"));
        combo->setItemText(2, tr("Fixed count and spacing"));
    }
}

void PatternParametersWidget::bind(App::DocumentObject* obj)
{
    object = obj;
    reference = nullptr;
    Base::StateLocker lock(blockUpdate);
    for (auto& row : rows) {
        auto prop = App::Pattern::getProperty(*obj, row.name);
        if (!prop) {
            continue;
        }
        if (row.editor == comboReference) {
            reference = dynamic_cast<App::PropertyLinkSub*>(prop);
        }
        else if (auto spin = qobject_cast<QuantitySpinBox*>(row.editor)) {
            spin->bind(*prop);
        }
        else if (auto spin = qobject_cast<UIntSpinBox*>(row.editor)) {
            spin->bind(*prop);
            if (auto intProp = dynamic_cast<App::PropertyIntegerConstraint*>(prop)) {
                spin->setMinimum(static_cast<uint>(std::max(0L, intProp->getMinimum())));
                spin->setMaximum(static_cast<uint>(
                    std::min<long>(intProp->getMaximum(), std::numeric_limits<int>::max())));
            }
        }
    }
    updateUI();
}

void PatternParametersWidget::updateUI()
{
    if (!object) {
        return;
    }
    Base::StateLocker lock(blockUpdate);
    for (auto& row : rows) {
        auto prop = App::Pattern::getProperty(*object, row.name);
        if (!prop) {
            continue;
        }
        if (row.editor == comboReference) {
            if (reference && comboReference->count()
                && refLinks.setCurrentLink(*reference) == -1) {
                refLinks.addLink(*reference,
                                 patternReferenceText(reference->getValue(),
                                                      reference->getSubValues()));
                refLinks.setCurrentLink(*reference);
            }
        }
        else if (auto spin = qobject_cast<QuantitySpinBox*>(row.editor)) {
            if (auto p = dynamic_cast<App::PropertyFloat*>(prop)) {
                spin->setValue(p->getValue());
            }
        }
        else if (auto spin = qobject_cast<UIntSpinBox*>(row.editor)) {
            if (auto p = dynamic_cast<App::PropertyInteger*>(prop)) {
                spin->setValue(static_cast<uint>(std::max(0L, p->getValue())));
            }
        }
        else if (auto box = qobject_cast<QCheckBox*>(row.editor)) {
            if (auto p = dynamic_cast<App::PropertyBool*>(prop)) {
                box->setChecked(p->getValue());
            }
        }
        else if (auto combo = qobject_cast<QComboBox*>(row.editor)) {
            if (auto p = dynamic_cast<App::PropertyEnumeration*>(prop)) {
                combo->setCurrentIndex(p->getValue());
            }
        }
        // The rows the other inputs leave unused, as the property editor
        form->setRowVisible(row.editor, !prop->testStatus(App::Property::Hidden));
    }
}

void PatternParametersWidget::apply(App::DocumentObject* obj) const
{
    if (!object) {
        return;
    }
    for (const auto& row : rows) {
        auto prop = App::Pattern::getProperty(*object, row.name);
        if (!prop) {
            continue;
        }
        if (row.editor == comboReference) {
            if (reference) {
                FCMD_OBJ_CMD(obj,
                             row.name << " = "
                                      << patternReferencePython(reference->getValue(),
                                                                reference->getSubValues()));
            }
        }
        else if (auto spin = qobject_cast<QuantitySpinBox*>(row.editor)) {
            spin->apply();
        }
        else if (auto spin = qobject_cast<UIntSpinBox*>(row.editor)) {
            spin->apply();
        }
        else if (auto p = dynamic_cast<App::PropertyBool*>(prop)) {
            FCMD_OBJ_CMD(obj, row.name << " = " << (p->getValue() ? "True" : "False"));
        }
        else if (auto p = dynamic_cast<App::PropertyEnumeration*>(prop)) {
            FCMD_OBJ_CMD(obj, row.name << " = " << p->getValue());
        }
    }
}

// ----------------------------------------------------------------------------

PatternInstanceMarkers::PatternInstanceMarkers(QObject* parent)
    : QObject(parent)
{
    // Drawn over the model, and picked over it: a marker sits at the centre
    // of an instance, inside it
    root = new SoAnnotation;
    root->ref();
    root->setName("PatternInstanceMarkers");
    root->renderCaching = SoSeparator::OFF;
    // Held here too: the markers after them come and go
    callback = new SoEventCallback;
    callback->ref();
    callback->addEventCallback(SoMouseButtonEvent::getClassTypeId(), eventCallback, this);
    callback->addEventCallback(SoLocation2Event::getClassTypeId(), eventCallback, this);
    root->addChild(callback);
    pickStyle = new SoPickStyle;
    pickStyle->ref();
    pickStyle->style = SoPickStyle::SHAPE_ON_TOP;
    root->addChild(pickStyle);
}

PatternInstanceMarkers::~PatternInstanceMarkers()
{
    clear();
    callback->removeEventCallback(SoMouseButtonEvent::getClassTypeId(), eventCallback, this);
    callback->removeEventCallback(SoLocation2Event::getClassTypeId(), eventCallback, this);
    callback->unref();
    pickStyle->unref();
    root->unref();
}

void PatternInstanceMarkers::show(ViewerContext* view, const std::vector<Instance>& list)
{
    SoGroup* where = view ? view->getOnViewParameterRoot() : nullptr;
    if (!where || list.empty()) {
        clear();
        return;
    }
    if (where != parent) {
        clear();
        parent = where;
        parent->ref();
    }

    // Kept where they are, so a toggle turns its marker over rather than
    // making all of them anew
    constexpr int first = 2;  // after the callback and the pick style
    const int size = int(std::lround(24.0 * std::max(1.0, view->devicePixelRatio())));
    while (markers.size() > list.size()) {
        root->removeChild(first + int(markers.size()) - 1);
        markers.pop_back();
    }
    while (markers.size() < list.size()) {
        auto group = new SoSeparator;
        group->addChild(new SoTranslation);
        auto marker = new SoToggleMarker;
        group->addChild(marker);
        root->addChild(group);
        markers.push_back(marker);
    }
    for (std::size_t i = 0; i < list.size(); ++i) {
        auto group = static_cast<SoSeparator*>(root->getChild(first + int(i)));
        auto translation = static_cast<SoTranslation*>(group->getChild(0));
        const Base::Vector3d& c = list[i].center;
        SbVec3f pos(float(c.x), float(c.y), float(c.z));
        if (translation->translation.getValue() != pos) {
            translation->translation = pos;
        }
        SoToggleMarker* marker = markers[i];
        if (marker->active.getValue() == list[i].suppressed) {
            marker->active = !list[i].suppressed;
        }
        if (marker->markerSize.getValue() != size) {
            marker->markerSize = size;
        }
    }
    instances = list;
    if (highlighted >= int(markers.size())) {
        highlighted = -1;
    }
    if (pressed >= int(markers.size())) {
        pressed = -1;
    }
    if (parent->findChild(root) < 0) {
        parent->addChild(root);
    }
}

void PatternInstanceMarkers::clear()
{
    if (parent) {
        int index = parent->findChild(root);
        if (index >= 0) {
            parent->removeChild(index);
        }
        parent->unref();
        parent = nullptr;
    }
    while (root->getNumChildren() > 2) {
        root->removeChild(root->getNumChildren() - 1);
    }
    instances.clear();
    markers.clear();
    pressed = -1;
    highlighted = -1;
}

SoToggleMarker* PatternInstanceMarkers::getMarker(std::size_t i) const
{
    return i < markers.size() ? markers[i] : nullptr;
}

void PatternInstanceMarkers::eventCallback(void* data, SoEventCallback* cb)
{
    static_cast<PatternInstanceMarkers*>(data)->handleEvent(cb);
}

int PatternInstanceMarkers::markerAt(const SoPickedPoint* picked) const
{
    if (!picked) {
        return -1;
    }
    SoNode* tail = picked->getPath()->getTail();
    for (std::size_t i = 0; i < markers.size(); ++i) {
        if (markers[i] == tail) {
            return int(i);
        }
    }
    return -1;
}

void PatternInstanceMarkers::setHighlighted(int which)
{
    if (which == highlighted) {
        return;
    }
    if (highlighted >= 0) {
        markers[highlighted]->highlighted = FALSE;
    }
    highlighted = which;
    if (highlighted >= 0) {
        markers[highlighted]->highlighted = TRUE;
    }
}

void PatternInstanceMarkers::handleEvent(SoEventCallback* cb)
{
    if (markers.empty()) {
        return;
    }
    const SoEvent* event = cb->getEvent();
    const int which = markerAt(cb->getPickedPoint());
    if (event->isOfType(SoLocation2Event::getClassTypeId())) {
        // Not taken: the view goes on preselecting and navigating as before
        setHighlighted(which);
        return;
    }
    const auto* button = static_cast<const SoMouseButtonEvent*>(event);
    if (button->getButton() != SoMouseButtonEvent::BUTTON1) {
        return;
    }
    if (button->getState() == SoButtonEvent::DOWN) {
        pressed = which;
        if (which >= 0) {
            // So that nothing behind the marker takes it for a selection
            cb->setHandled();
        }
        return;
    }
    // The release over the marker the press was on is the click
    const int down = pressed;
    pressed = -1;
    if (which < 0 || which != down) {
        return;
    }
    cb->setHandled();
    const Instance instance = instances[which];
    Q_EMIT toggleRequested(instance.index, !instance.suppressed);
}

#include "moc_PatternWidgets.cpp"
