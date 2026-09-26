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

#ifndef PARTDESIGNGUI_PatternDirectionWidget_H
#define PARTDESIGNGUI_PatternDirectionWidget_H

#include <QWidget>

#include "TaskTransformedParameters.h"

class QCheckBox;
class QComboBox;
class QFormLayout;
class QLabel;

namespace App {
class DocumentObject;
class PropertyBool;
class PropertyEnumeration;
class PropertyFloatList;
class PropertyIntegerConstraint;
class PropertyLinkSub;
class PropertyQuantity;
}

namespace Gui {
class QuantitySpinBox;
class UIntSpinBox;
}

namespace PartDesignGui {

/**
 * The parameters of one direction of a pattern: its reference, Reversed, the
 * Extent/Spacing mode with its two values, Occurrences, and the individual
 * spacings. The linear pattern panel shows two, the polar pattern one.
 *
 * The widget sets the bound properties as they are edited, and says so with
 * changed(); picking the reference is left to the panel, which fills the
 * combo and owns the selection.
 */
class PatternDirectionWidget : public QWidget
{
    Q_OBJECT

public:
    enum class Kind { Linear, Polar };

    struct Properties {
        App::PropertyLinkSub* reference = nullptr;
        App::PropertyBool* reversed = nullptr;
        App::PropertyEnumeration* mode = nullptr;
        App::PropertyQuantity* extent = nullptr;
        App::PropertyQuantity* spacing = nullptr;
        App::PropertyIntegerConstraint* occurrences = nullptr;
        App::PropertyFloatList* spacings = nullptr;
        App::PropertyFloatList* spacingPattern = nullptr;
    };

    explicit PatternDirectionWidget(Kind kind, QWidget* parent = nullptr);
    ~PatternDirectionWidget() override;

    void bind(const Properties& props);
    const Properties& properties() const { return props; }

    /// The reference combo, for the panel to fill
    ComboLinks& links() { return refLinks; }

    /// Show the properties' values, and the reference in the combo
    void updateUI();

    /// Record the parameters as commands of \a obj, for the macro recorder
    void apply(App::DocumentObject* obj) const;

    void retranslate();

Q_SIGNALS:
    /// The user picked an item of the reference combo
    void referenceActivated();
    /// A bound property was set from the widget
    void changed();

private:
    void onModeActivated(int index);
    void onIndividualToggled(bool on);
    void onSpacingEdited(int index, double value);
    void adaptVisibilityToMode();
    void rebuildSpacingRows();
    double fallbackSpacing(int index) const;
    bool hasIndividualSpacings() const;
    int gapCount() const;

private:
    Kind kind;
    Properties props;
    ComboLinks refLinks;
    bool blockUpdate = false;
    bool individual = false;

    QFormLayout* form = nullptr;
    QLabel* labelReference = nullptr;
    QComboBox* comboReference = nullptr;
    QCheckBox* checkReverse = nullptr;
    QLabel* labelMode = nullptr;
    QComboBox* comboMode = nullptr;
    QLabel* labelExtent = nullptr;
    Gui::QuantitySpinBox* spinExtent = nullptr;
    QLabel* labelSpacing = nullptr;
    Gui::QuantitySpinBox* spinSpacing = nullptr;
    QLabel* labelOccurrences = nullptr;
    Gui::UIntSpinBox* spinOccurrences = nullptr;
    QCheckBox* checkIndividual = nullptr;
    QWidget* spacingsBox = nullptr;
    QFormLayout* spacingsForm = nullptr;
    QLabel* labelMoreSpacings = nullptr;
    std::vector<Gui::QuantitySpinBox*> spacingSpins;
};

} // namespace PartDesignGui

#endif // PARTDESIGNGUI_PatternDirectionWidget_H
