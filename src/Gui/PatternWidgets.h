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

#ifndef GUI_PATTERNWIDGETS_H
#define GUI_PATTERNWIDGETS_H

#include <set>
#include <string>
#include <vector>

#include <QWidget>

#include <FCGlobal.h>

class QCheckBox;
class QComboBox;
class QFormLayout;
class QLabel;

namespace App
{
class Document;
class DocumentObject;
class PropertyBool;
class PropertyEnumeration;
class PropertyFloatList;
class PropertyIntegerConstraint;
class PropertyLinkSub;
class PropertyQuantity;
}  // namespace App

namespace Gui
{
class QuantitySpinBox;
class UIntSpinBox;

/** A combo box of references, each item holding a link
 *
 * @param combo: cleared as soon as it is bound. Don't add or remove items of
 * the combo directly, or the list of links goes out of step with it.
 */
class GuiExport ComboLinks
{
public:
    explicit ComboLinks(QComboBox& combo);
    ComboLinks() = default;
    ~ComboLinks();

    ComboLinks(const ComboLinks&) = delete;
    ComboLinks& operator=(const ComboLinks&) = delete;

    void setCombo(QComboBox& combo);

    /// Add an item, without checking for duplicates. The link may be null.
    int addLink(const App::PropertyLinkSub& lnk, const QString& itemText);
    int addLink(App::DocumentObject* linkObj, const std::string& linkSubname, const QString& itemText);
    /// Add the item that asks for a reference to be picked
    int addSelectItem(const QString& itemText);
    /// Whether \a index is an item added by addSelectItem()
    bool isSelectItem(int index) const;
    void clear();

    /// The link of item \a index; throws if its object left the document
    App::PropertyLinkSub& getLink(int index) const;
    App::PropertyLinkSub& getCurrentLink() const;
    bool isCurrentSelectItem() const;

    /** Select the item holding \a lnk, with the combo's signals blocked
     * @return its index, or -1 if there is none
     */
    int setCurrentLink(const App::PropertyLinkSub& lnk);

    QComboBox& combo() const;

private:
    QComboBox* _combo = nullptr;
    App::Document* doc = nullptr;
    std::vector<App::PropertyLinkSub*> linksInList;
    std::set<int> selectItems;
};

/// The text for a reference: the object's label, and the element
GuiExport QString patternReferenceText(const App::DocumentObject* obj,
                                       const std::vector<std::string>& subs);
/// The Python value of a reference, for a recorded command
GuiExport std::string patternReferencePython(const App::DocumentObject* obj,
                                             const std::vector<std::string>& subs);

/** The parameters of one direction of a pattern: its reference, Reversed,
 * the Extent/Spacing mode with its two values, Occurrences, and the
 * individual spacings. A linear pattern shows two, a polar pattern one.
 *
 * The widget sets the bound properties as they are edited, and says so with
 * changed(); picking the reference is left to the panel, which fills the
 * combo and owns the selection.
 */
class GuiExport PatternDirectionWidget: public QWidget
{
    Q_OBJECT

public:
    enum class Kind
    {
        Linear,
        Polar
    };

    struct Properties
    {
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
    const Properties& properties() const
    {
        return props;
    }

    /// The reference combo, for the panel to fill
    ComboLinks& links()
    {
        return refLinks;
    }

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

/** The parameters of a pattern that is not a direction -- circular, along a
 * path, on points -- each row bound to the property of its name
 *
 * A reference row is a combo the panel fills, as PatternDirectionWidget's;
 * a value row sets its property as it is edited. Which rows there are
 * follows the kind; rows whose property is hidden are hidden too.
 */
class GuiExport PatternParametersWidget: public QWidget
{
    Q_OBJECT

public:
    enum class Kind
    {
        Circular,
        Path,
        Point
    };

    explicit PatternParametersWidget(Kind kind, QWidget* parent = nullptr);
    ~PatternParametersWidget() override;

    Kind getKind() const
    {
        return kind;
    }

    /// Bind the rows to the properties of their names in \a obj
    void bind(App::DocumentObject* obj);

    /// The property the reference row edits: Axis, Path or PointObject
    App::PropertyLinkSub* referenceProperty() const
    {
        return reference;
    }
    ComboLinks& links()
    {
        return refLinks;
    }

    void updateUI();
    void apply(App::DocumentObject* obj) const;
    void retranslate();

Q_SIGNALS:
    void referenceActivated();
    void changed();

private:
    struct Row
    {
        const char* name;
        QLabel* label = nullptr;
        QWidget* editor = nullptr;
    };
    void addRow(const char* name, QWidget* editor, bool withLabel = true);
    Row* findRow(const char* name);

private:
    Kind kind;
    App::DocumentObject* object = nullptr;
    App::PropertyLinkSub* reference = nullptr;
    ComboLinks refLinks;
    bool blockUpdate = false;
    QFormLayout* form = nullptr;
    QComboBox* comboReference = nullptr;
    std::vector<Row> rows;
};

}  // namespace Gui

#endif  // GUI_PATTERNWIDGETS_H
