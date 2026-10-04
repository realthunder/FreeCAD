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
#include <cstring>
#include <string>
#endif

#include <Base/Console.h>

#include "Document.h"
#include "DocumentObject.h"
#include "PatternExtension.h"

FC_LOG_LEVEL_INIT("App::Pattern", true, true)

using namespace App;

EXTENSION_PROPERTY_SOURCE(App::PatternExtension, App::DocumentObjectExtension)

PatternExtension::PatternExtension()
    : PatternExtension(Pattern::Type::Linear)
{}

PatternExtension::PatternExtension(Pattern::Type type)
{
    initExtensionType(PatternExtension::getExtensionClassTypeId());
    EXTENSION_ADD_PROPERTY_TYPE(PatternType,
                                (static_cast<long>(type)),
                                "Pattern",
                                App::Prop_None,
                                "The kind of pattern. Its inputs change with it.");
    PatternType.setEnums(Pattern::TypeEnums);
}

void PatternExtension::initExtension(ExtensionContainer* obj)
{
    inherited::initExtension(obj);
    setupPatternProperties();
}

Pattern::Type PatternExtension::getPatternType() const
{
    long value = PatternType.getValue();
    if (value < 0 || value > static_cast<long>(Pattern::Type::Point)) {
        return Pattern::Type::Linear;
    }
    return static_cast<Pattern::Type>(value);
}

void PatternExtension::setPatternNames(const char* const* labels, const char* const* saveTypes)
{
    this->labels = labels;
    this->saveTypes = saveTypes;
}

Base::Type PatternExtension::getSaveType(Base::Type type) const
{
    if (saveTypes) {
        if (const char* name = saveTypes[static_cast<int>(getPatternType())]) {
            Base::Type res = Base::Type::fromName(name);
            if (!res.isBad()) {
                return res;
            }
        }
    }
    return type;
}

void PatternExtension::setupPatternProperties()
{
    auto obj = getExtendedContainer();
    if (!obj) {
        return;
    }
    const auto type = getPatternType();

    // Drop the inputs of the other kinds, but a property this kind has too,
    // with the same type, keeps its value
    for (int i = 0; Pattern::TypeEnums[i]; ++i) {
        const auto other = static_cast<Pattern::Type>(i);
        if (other == type) {
            continue;
        }
        for (const auto& spec : Pattern::getPropertySpecs(other)) {
            auto prop = obj->getDynamicPropertyByName(spec.name);
            if (!prop) {
                continue;
            }
            auto wanted = Pattern::getPropertySpec(type, spec.name);
            if (wanted && std::strcmp(wanted->type, prop->getTypeId().getName()) == 0) {
                continue;
            }
            obj->removeDynamicProperty(spec.name);
        }
    }

    for (const auto& spec : Pattern::getPropertySpecs(type)) {
        auto prop = Pattern::getProperty(*obj, spec.name);
        if (prop && std::strcmp(prop->getTypeId().getName(), spec.type) == 0) {
            continue;
        }
        if (prop) {
            if (!obj->getDynamicPropertyByName(spec.name)
                || !obj->removeDynamicProperty(spec.name)) {
                FC_ERR("Cannot replace property " << spec.name);
                continue;
            }
        }
        prop = obj->addDynamicProperty(spec.type, spec.name, spec.group, spec.doc);
        Pattern::initProperty(type, prop);
    }
    Pattern::setupProperties(type, *obj);
}

void PatternExtension::updateLabel()
{
    auto obj = getExtendedObject();
    if (!labels || !obj || !obj->isAttachedToDocument()) {
        return;
    }
    const char* wanted = labels[static_cast<int>(getPatternType())];
    if (!wanted) {
        return;
    }
    // A label the user has not changed is the label of a kind, with the
    // suffix that keeps it unique
    std::string label = obj->Label.getValue();
    label.resize(label.find_last_not_of("0123456789") + 1);
    if (label == wanted) {
        return;
    }
    for (int i = 0; Pattern::TypeEnums[i]; ++i) {
        if (labels[i] && label == labels[i]) {
            obj->Label.setValue(wanted);
            return;
        }
    }
}

void PatternExtension::extensionOnChanged(const Property* prop)
{
    auto obj = getExtendedObject();
    if (obj) {
        auto doc = obj->getDocument();
        bool undoing = doc && doc->isPerformingTransaction();
        if (prop == &PatternType) {
            // Undo and redo bring the properties back themselves, without
            // what a file does not keep either
            if (undoing) {
                obj->setStatus(ObjectStatus::PendingTransactionUpdate, true);
            }
            else {
                setupPatternProperties();
                if (!obj->isRestoring()) {
                    updateLabel();
                }
            }
        }
        else if (!obj->isRestoring() && !undoing
                 && Pattern::isPatternProperty(getPatternType(), prop)) {
            Pattern::onChanged(getPatternType(), *obj, prop);
        }
    }
    inherited::extensionOnChanged(prop);
}

bool PatternExtension::extensionHandleChangedPropertyType(Base::XMLReader& reader,
                                                          const char* TypeName,
                                                          Property* prop)
{
    // A file may keep the inputs of its kind before PatternType, so that one
    // meets the input of the kind the object starts as, of the same name and
    // another type: an Offset that is an angle, say. Replace it.
    auto obj = getExtendedContainer();
    if (!obj || prop->getContainer() != obj || !obj->getDynamicPropertyByName(prop->getName())) {
        return false;
    }
    for (int i = 0; Pattern::TypeEnums[i]; ++i) {
        const auto type = static_cast<Pattern::Type>(i);
        auto spec = Pattern::getPropertySpec(type, prop->getName());
        if (!spec || std::strcmp(spec->type, TypeName) != 0) {
            continue;
        }
        std::string name = prop->getName();
        obj->removeDynamicProperty(name.c_str());
        auto newProp = obj->addDynamicProperty(TypeName, name.c_str(), spec->group, spec->doc);
        Pattern::initProperty(type, newProp);
        newProp->Restore(reader);
        return true;
    }
    return false;
}

short PatternExtension::extensionMustExecute()
{
    auto obj = getExtendedContainer();
    if (PatternType.isTouched() || (obj && Pattern::isTouched(getPatternType(), *obj))) {
        return 1;
    }
    return inherited::extensionMustExecute();
}

void PatternExtension::onExtendedDocumentRestored()
{
    inherited::onExtendedDocumentRestored();
    // Constraints and the status the modes imply are not in the file
    if (auto obj = getExtendedContainer()) {
        Pattern::setupProperties(getPatternType(), *obj);
    }
}

void PatternExtension::onPatternUndoRedoFinished()
{
    if (auto obj = getExtendedContainer()) {
        Pattern::setupProperties(getPatternType(), *obj);
    }
}

void PatternExtension::onExtendedSetupObject()
{
    inherited::onExtendedSetupObject();
    // A new object is named after its kind, whatever its internal name
    auto obj = getExtendedObject();
    if (labels && obj) {
        if (const char* label = labels[static_cast<int>(getPatternType())]) {
            obj->Label.setValue(label);
        }
    }
}
