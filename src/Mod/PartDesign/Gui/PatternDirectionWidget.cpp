/****************************************************************************
 *   Copyright (c) 2026 Zheng Lei <realthunder.dev@gmail.com>               *
 *                                                                          *
 *   This file is part of the FreeCAD CAx development system.               *
 *                                                                          *
 *   This library is free software; you can redistribute it and/or          *
 *   modify it under the terms of the GNU Library General Public            *
 *   License as published by the Free Software Foundation; either           *
 *   version 2 of the License, or (at your option) any later version.       *
 *                                                                          *
 *   This library  is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of         *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the          *
 *   GNU Library General Public License for more details.                   *
 *                                                                          *
 *   You should have received a copy of the GNU Library General Public      *
 *   License along with this library; see the file COPYING.LIB. If not,     *
 *   write to the Free Software Foundation, Inc., 59 Temple Place,          *
 *   Suite 330, Boston, MA  02111-1307, USA                                 *
 *                                                                          *
 ****************************************************************************/

#include "PreCompiled.h"

#ifndef _PreComp_
# include <cmath>
# include <limits>
# include <sstream>
# include <QCheckBox>
# include <QComboBox>
# include <QFont>
# include <QFormLayout>
# include <QLabel>
# include <QVBoxLayout>
#endif

#include <App/DocumentObject.h>
#include <App/PropertyLinks.h>
#include <App/PropertyStandard.h>
#include <App/PropertyUnits.h>
#include <Base/Tools.h>
#include <Gui/Command.h>
#include <Gui/QuantitySpinBox.h>
#include <Gui/SpinBox.h>

#include "PatternDirectionWidget.h"
#include "ReferenceSelection.h"

using namespace PartDesignGui;

// Rows of individual spacings shown at most; the rest are in the property
// editor. A spin box each is fine for a few dozen gaps, not for thousands.
static constexpr int MaxSpacingRows = 100;

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
        if (!blockUpdate)
            Q_EMIT referenceActivated();
    });
    connect(checkReverse, &QCheckBox::toggled, this, [this](bool on) {
        if (blockUpdate || !props.reversed)
            return;
        props.reversed->setValue(on);
        Q_EMIT changed();
    });
    connect(comboMode, qOverload<int>(&QComboBox::activated),
                  this, &PatternDirectionWidget::onModeActivated);
    connect(spinExtent, qOverload<double>(&Gui::QuantitySpinBox::valueChanged), this, [this](double v) {
        if (blockUpdate || !props.extent)
            return;
        props.extent->setValue(v);
        updateUI(); // the synced spacing
        Q_EMIT changed();
    });
    connect(spinSpacing, qOverload<double>(&Gui::QuantitySpinBox::valueChanged), this, [this](double v) {
        if (blockUpdate || !props.spacing)
            return;
        props.spacing->setValue(v);
        updateUI(); // the synced extent, and the spacings that follow it
        Q_EMIT changed();
    });
    connect(spinOccurrences, &Gui::UIntSpinBox::unsignedChanged, this, [this](uint n) {
        if (blockUpdate || !props.occurrences)
            return;
        props.occurrences->setValue(n);
        updateUI(); // the synced value, and the rows of spacings
        Q_EMIT changed();
    });
    connect(checkIndividual, &QCheckBox::toggled,
                  this, &PatternDirectionWidget::onIndividualToggled);
}

PatternDirectionWidget::~PatternDirectionWidget() = default;

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
        if (auto label = item ? qobject_cast<QLabel*>(item->widget()) : nullptr)
            label->setText(tr("Gap %1").arg(i + 1));
    }
    labelMoreSpacings->setText(tr("The gaps after the first %1 are in the property editor, "
                                  "as Spacings.").arg(MaxSpacingRows));
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
    // As the features' getSpacing() does without the individual spacing
    const auto& pattern = props.spacingPattern->getValues();
    if (pattern.size() > 1)
        return pattern[index % pattern.size()];
    return props.spacing->getValue();
}

bool PatternDirectionWidget::hasIndividualSpacings() const
{
    if (!props.spacings)
        return false;
    for (double v : props.spacings->getValues()) {
        if (v != -1.0)
            return true;
    }
    return false;
}

void PatternDirectionWidget::updateUI()
{
    if (!props.mode)
        return;
    Base::StateLocker lock(blockUpdate);

    if (comboReference->count()) {
        if (refLinks.setCurrentLink(*props.reference) == -1) {
            // failed to set current, because the link isn't in the list yet
            refLinks.addLink(*props.reference, getRefStr(props.reference->getValue(),
                                                         props.reference->getSubValues()));
            refLinks.setCurrentLink(*props.reference);
        }
    }

    checkReverse->setChecked(props.reversed->getValue());
    comboMode->setCurrentIndex(props.mode->getValue());
    spinExtent->setValue(props.extent->getValue());
    spinSpacing->setValue(props.spacing->getValue());
    spinOccurrences->setValue(props.occurrences->getValue());

    if (hasIndividualSpacings())
        individual = true;
    checkIndividual->setChecked(individual);

    rebuildSpacingRows();
    adaptVisibilityToMode();
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
    if (!individual)
        rows = 0;

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
        connect(spin, qOverload<double>(&Gui::QuantitySpinBox::valueChanged), this,
                      [this, index](double v) { onSpacingEdited(index, v); });
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
    if (blockUpdate || !props.mode)
        return;
    props.mode->setValue(index);
    updateUI();
    Q_EMIT changed();
}

void PatternDirectionWidget::onIndividualToggled(bool on)
{
    if (blockUpdate || !props.spacings)
        return;
    individual = on;
    bool cleared = false;
    if (!on && hasIndividualSpacings()) {
        props.spacings->setValues(std::vector<double>(gapCount(), -1.0));
        cleared = true;
    }
    updateUI();
    if (cleared)
        Q_EMIT changed();
}

void PatternDirectionWidget::onSpacingEdited(int index, double value)
{
    if (blockUpdate || !props.spacings)
        return;
    std::vector<double> spacings = props.spacings->getValues();
    spacings.resize(gapCount(), -1.0);
    if (index >= static_cast<int>(spacings.size()))
        return;
    // The spacing above typed back in follows it again
    double v = std::fabs(value - fallbackSpacing(index)) < 1e-9 ? -1.0 : value;
    if (spacings[index] == v)
        return;
    spacings[index] = v;
    props.spacings->setValues(spacings);
    updateUI();
    Q_EMIT changed();
}

void PatternDirectionWidget::apply(App::DocumentObject* obj) const
{
    if (!props.mode)
        return;
    FCMD_OBJ_CMD(obj, props.reference->getName() << " = "
                 << buildLinkSingleSubPythonStr(props.reference->getValue(),
                                                props.reference->getSubValues()));
    FCMD_OBJ_CMD(obj, props.reversed->getName() << " = "
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

#include "moc_PatternDirectionWidget.cpp"
