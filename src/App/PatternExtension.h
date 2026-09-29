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

#ifndef APP_PATTERNEXTENSION_H
#define APP_PATTERNEXTENSION_H

#include "DocumentObjectExtension.h"
#include "Pattern.h"
#include "PropertyStandard.h"

namespace App
{

/** A pattern whose kind can be changed at any time
 *
 * PatternType picks the kind -- linear, polar, circular, along a path, on
 * points. The inputs of the active kind are dynamic properties with
 * upstream's names, swapped when PatternType changes; a property two kinds
 * share with the same type (Occurrences, Reversed, Axis, ...) keeps its
 * value across the change.
 *
 * App::LinkArray and PartDesign's patterns carry it. Part's pattern
 * extensions are the fixed-kind form, with static members, kept for
 * upstream's API.
 *
 * Beside the inputs, the owner may give each kind
 *  - a label, which the object's Label follows while it is one of these
 *    labels (with a numeric suffix): as the object is created, and when the
 *    kind changes, unless the user has renamed it;
 *  - a class to be saved as (getSaveType()), so that a file names the class
 *    of the kind the object has now -- one upstream reads as that kind.
 *
 * The extension itself is not recorded in the file: all it holds are
 * properties of the object, and a reader without it would only complain.
 */
class AppExport PatternExtension: public DocumentObjectExtension
{
    EXTENSION_PROPERTY_HEADER_WITH_OVERRIDE(App::PatternExtension);
    using inherited = DocumentObjectExtension;

public:
    PatternExtension();
    explicit PatternExtension(Pattern::Type type);

    PropertyEnumeration PatternType;

    Pattern::Type getPatternType() const;

    /** The labels and the classes of the kinds, indexed by Pattern::Type
     *
     * Either may be null; the arrays must outlive the object.
     */
    void setPatternNames(const char* const* labels, const char* const* saveTypes);

    /// The class to save the object as: that of its kind, else \a type
    Base::Type getSaveType(Base::Type type) const;

    /// The owner calls it after an undo or a redo
    void onPatternUndoRedoFinished();

    void initExtension(ExtensionContainer* obj) override;
    bool isExtensionSaved() const override
    {
        return false;
    }

protected:
    short extensionMustExecute() override;
    void extensionOnChanged(const Property* prop) override;
    bool extensionHandleChangedPropertyType(Base::XMLReader& reader,
                                            const char* TypeName,
                                            Property* prop) override;
    void onExtendedDocumentRestored() override;
    void onExtendedSetupObject() override;

private:
    void setupPatternProperties();
    void updateLabel();

    const char* const* labels = nullptr;
    const char* const* saveTypes = nullptr;
};

}  // namespace App

#endif  // APP_PATTERNEXTENSION_H
