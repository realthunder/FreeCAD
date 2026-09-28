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
#include <cstring>
#endif

#include <Base/Console.h>
#include <Base/Tools.h>

#include "Document.h"
#include "LinkArray.h"

FC_LOG_LEVEL_INIT("App::Link", true, true)

using namespace App;

PROPERTY_SOURCE_WITH_EXTENSIONS(App::LinkArray, App::Link)

LinkArray::LinkArray()
    : LinkArray(Pattern::Type::Linear)
{}

LinkArray::LinkArray(Pattern::Type type)
{
    ADD_PROPERTY_TYPE(PatternType,
                      (static_cast<long>(type)),
                      "Pattern",
                      App::Prop_None,
                      "The kind of pattern placing the elements. Its inputs change with it.");
    PatternType.setEnums(Pattern::TypeEnums);
    ADD_PROPERTY_TYPE(GeneratedOccurrences2,
                      (0),
                      "Pattern",
                      static_cast<App::PropertyType>(App::Prop_Hidden | App::Prop_ReadOnly
                                                     | App::Prop_Output),
                      "Occurrences2 of a linear pattern when its elements were generated");
    setupPatternProperties();
}

Pattern::Type LinkArray::getPatternType() const
{
    long value = PatternType.getValue();
    if (value < 0 || value > static_cast<long>(Pattern::Type::Point)) {
        return Pattern::Type::Linear;
    }
    return static_cast<Pattern::Type>(value);
}

void LinkArray::setupPatternProperties()
{
    const auto type = getPatternType();

    // Drop the inputs of the other kinds, but a property this kind has too,
    // with the same type, keeps its value
    for (int i = 0; Pattern::TypeEnums[i]; ++i) {
        const auto other = static_cast<Pattern::Type>(i);
        if (other == type) {
            continue;
        }
        for (const auto& spec : Pattern::getPropertySpecs(other)) {
            auto prop = getDynamicPropertyByName(spec.name);
            if (!prop) {
                continue;
            }
            auto wanted = Pattern::getPropertySpec(type, spec.name);
            if (wanted && std::strcmp(wanted->type, prop->getTypeId().getName()) == 0) {
                continue;
            }
            removeDynamicProperty(spec.name);
        }
    }

    for (const auto& spec : Pattern::getPropertySpecs(type)) {
        auto prop = Pattern::getProperty(*this, spec.name);
        if (prop && std::strcmp(prop->getTypeId().getName(), spec.type) == 0) {
            continue;
        }
        if (prop) {
            if (!getDynamicPropertyByName(spec.name) || !removeDynamicProperty(spec.name)) {
                FC_ERR("Cannot replace property " << spec.name << " of "
                                                  << getFullName());
                continue;
            }
        }
        prop = addDynamicProperty(spec.type, spec.name, spec.group, spec.doc);
        Pattern::initProperty(type, prop);
    }
    Pattern::setupProperties(type, *this);
    setupStatus();
}

void LinkArray::setupStatus()
{
    // The pattern decides the elements and where they go
    ElementCount.setStatus(Property::Hidden, true);
    ElementCount.setStatus(Property::Immutable, true);
    PlacementList.setStatus(Property::Hidden, true);
    PlacementList.setStatus(Property::Immutable, true);

    // A reference may lie outside the group of the array
    for (const auto& spec : Pattern::getPropertySpecs(getPatternType())) {
        if (auto link = dynamic_cast<PropertyLinkSub*>(Pattern::getProperty(*this, spec.name))) {
            link->setScope(LinkScope::Global);
        }
    }
}

void LinkArray::onChanged(const Property* prop)
{
    auto doc = getDocument();
    bool undoing = doc && doc->isPerformingTransaction();
    if (prop == &PatternType) {
        // Undo and redo bring the properties back themselves, without what
        // a file does not keep either
        if (!undoing) {
            setupPatternProperties();
        }
        else {
            setStatus(ObjectStatus::PendingTransactionUpdate, true);
        }
    }
    else if (!isRestoring() && !syncing && !undoing) {
        if (prop == &VisibilityList) {
            readSuppression();
        }
        else if (prop->getName() && std::strcmp(prop->getName(), "SuppressedPositions") == 0) {
            applySuppression();
        }
        else if (Pattern::isPatternProperty(getPatternType(), prop)) {
            Pattern::onChanged(getPatternType(), *this, prop);
        }
    }
    inherited::onChanged(prop);
}

void LinkArray::handleChangedPropertyType(Base::XMLReader& reader,
                                          const char* TypeName,
                                          Property* prop)
{
    // A file may keep the inputs of its kind before PatternType, so that one
    // meets the input of the kind the array starts as, of the same name and
    // another type: an Offset that is an angle, say. Replace it.
    if (prop->getContainer() == this && getDynamicPropertyByName(prop->getName())) {
        for (int i = 0; Pattern::TypeEnums[i]; ++i) {
            const auto type = static_cast<Pattern::Type>(i);
            auto spec = Pattern::getPropertySpec(type, prop->getName());
            if (!spec || std::strcmp(spec->type, TypeName) != 0) {
                continue;
            }
            std::string name = prop->getName();
            removeDynamicProperty(name.c_str());
            auto newProp = addDynamicProperty(TypeName, name.c_str(), spec->group, spec->doc);
            Pattern::initProperty(type, newProp);
            newProp->Restore(reader);
            return;
        }
    }
    inherited::handleChangedPropertyType(reader, TypeName, prop);
}

void LinkArray::onUndoRedoFinished()
{
    Pattern::setupProperties(getPatternType(), *this);
    setupStatus();
    inherited::onUndoRedoFinished();
}

void LinkArray::onDocumentRestored()
{
    inherited::onDocumentRestored();
    Pattern::setupProperties(getPatternType(), *this);
    setupStatus();
    restoreElementSuppression();
}

short LinkArray::mustExecute() const
{
    if (PatternType.isTouched() || Pattern::isTouched(getPatternType(), *this)) {
        return 1;
    }
    // The references are brought into the frame of the array by its placement
    if (Placement.isTouched()) {
        for (const auto& spec : Pattern::getPropertySpecs(getPatternType())) {
            auto link = dynamic_cast<PropertyLinkSub*>(Pattern::getProperty(*this, spec.name));
            if (link && link->getValue()) {
                return 1;
            }
        }
    }
    return inherited::mustExecute();
}

DocumentObjectExecReturn* LinkArray::execute()
{
    std::vector<Base::Placement> placements;
    try {
        Pattern::Context context;
        context.placement = Placement.getValue();
        context.defaultReferences = true;
        placements = Pattern::getPlacements(getPatternType(), *this, context);
    }
    catch (const Base::Exception& e) {
        return new DocumentObjectExecReturn(e.what());
    }
    syncElements(placements);
    return inherited::execute();
}

void LinkArray::syncElements(const std::vector<Base::Placement>& placements)
{
    Base::StateLocker guard(syncing);

    int count = static_cast<int>(placements.size());
    if (ElementCount.getValue() != count) {
        Base::ObjectStatusLocker<Property::Status, Property> lock(Property::Immutable,
                                                                  &ElementCount,
                                                                  false);
        ElementCount.setValue(count);
    }

    if (ShowElement.getValue()) {
        const auto& elements = ElementList.getValues();
        for (std::size_t i = 0; i < elements.size() && i < placements.size(); ++i) {
            auto element = freecad_cast<LinkElement*>(elements[i]);
            if (!element || element->Placement.getValue().isSame(placements[i])) {
                continue;
            }
            element->Placement.setValue(placements[i]);
            element->purgeTouched();
        }
    }
    else if (PlacementList.getValues() != placements) {
        Base::ObjectStatusLocker<Property::Status, Property> lock(Property::Immutable,
                                                                  &PlacementList,
                                                                  false);
        PlacementList.setValues(placements);
    }

    if (getPatternType() == Pattern::Type::Linear) {
        auto occurrences2 = dynamic_cast<PropertyInteger*>(Pattern::getProperty(*this, "Occurrences2"));
        long stride = occurrences2 ? std::max(1L, occurrences2->getValue()) : 1;
        if (GeneratedOccurrences2.getValue() != stride) {
            GeneratedOccurrences2.setValue(stride);
        }
        applySuppression();
    }
}

std::vector<std::string> LinkArray::getSubObjects(int reason) const
{
    auto res = inherited::getSubObjects(reason);
    const auto& vis = VisibilityList.getValues();
    if (vis.all()) {
        return res;
    }
    res.erase(std::remove_if(res.begin(),
                             res.end(),
                             [&](const std::string& sub) {
                                 int index = getElementIndex(sub.c_str());
                                 return index >= 0 && index < static_cast<int>(vis.size())
                                     && !vis[index];
                             }),
              res.end());
    return res;
}

bool LinkArray::isElementSuppressed(int index) const
{
    const auto& vis = VisibilityList.getValues();
    return index >= 0 && index < static_cast<int>(vis.size()) && !vis[index];
}

void LinkArray::setElementSuppressed(int index, bool suppressed)
{
    if (index >= 0) {
        setElementVisible(std::to_string(index).c_str(), !suppressed);
    }
}

void LinkArray::applySuppression()
{
    auto positionsProp =
        dynamic_cast<PropertyIntPairList*>(Pattern::getProperty(*this, "SuppressedPositions"));
    if (!positionsProp) {
        return;
    }
    const long stride = std::max(1L, GeneratedOccurrences2.getValue());
    const long count = std::max(0L, ElementCount.getValue());
    boost::dynamic_bitset<> vis(count);
    vis.set();
    for (const auto& [first, second] : positionsProp->getValues()) {
        if (first < 0 || second < 0 || second >= stride) {
            continue;
        }
        long index = first * stride + second;
        if (index < count) {
            vis[index] = false;
        }
    }

    // A list short of the elements shows the ones it misses
    const auto& current = VisibilityList.getValues();
    bool same = true;
    for (long i = 0; same && i < count; ++i) {
        bool shown = i >= static_cast<long>(current.size()) || current[i];
        same = shown == vis[i];
    }
    if (!same) {
        Base::StateLocker guard(syncing);
        VisibilityList.setValues(vis);
    }
}

void LinkArray::readSuppression()
{
    auto positionsProp =
        dynamic_cast<PropertyIntPairList*>(Pattern::getProperty(*this, "SuppressedPositions"));
    if (!positionsProp) {
        return;
    }
    const long stride = std::max(1L, GeneratedOccurrences2.getValue());
    const long count = std::max(0L, ElementCount.getValue());
    const auto& vis = VisibilityList.getValues();

    // The positions outside the elements stay, for when they come back
    std::vector<PropertyIntPairList::IntPair> positions;
    for (const auto& position : positionsProp->getValues()) {
        const auto& [first, second] = position;
        if (first < 0 || second < 0 || second >= stride || first * stride + second >= count) {
            positions.push_back(position);
        }
    }
    for (long i = 0; i < count && i < static_cast<long>(vis.size()); ++i) {
        if (!vis[i]) {
            positions.emplace_back(i / stride, i % stride);
        }
    }

    auto current = positionsProp->getValues();
    std::sort(current.begin(), current.end());
    std::sort(positions.begin(), positions.end());
    if (current != positions) {
        Base::StateLocker guard(syncing);
        positionsProp->setValues(positions);
    }
}

void LinkArray::restoreElementSuppression()
{
    // Upstream suppresses a link element by a Suppressed property of its own.
    // Those restore into VisibilityList.
    const auto& elements = ElementList.getValues();
    auto vis = VisibilityList.getValues();
    bool changed = false;
    for (std::size_t i = 0; i < elements.size(); ++i) {
        auto element = freecad_cast<LinkElement*>(elements[i]);
        if (!element || !element->wasRestoredSuppressed()) {
            continue;
        }
        if (vis.size() <= i) {
            vis.resize(i + 1, true);
        }
        if (vis[i]) {
            vis[i] = false;
            changed = true;
        }
    }

    Base::StateLocker guard(syncing);
    if (getPatternType() == Pattern::Type::Linear && GeneratedOccurrences2.getValue() == 0) {
        // A file without the generated stride made its elements with the
        // Occurrences2 it has now
        auto occurrences2 = dynamic_cast<PropertyInteger*>(Pattern::getProperty(*this, "Occurrences2"));
        GeneratedOccurrences2.setValue(occurrences2 ? std::max(1L, occurrences2->getValue()) : 1);
        GeneratedOccurrences2.purgeTouched();
    }
    if (changed) {
        VisibilityList.setValues(vis);
        VisibilityList.purgeTouched();
        if (getPatternType() == Pattern::Type::Linear) {
            syncing = false;
            readSuppression();
            if (auto positions = Pattern::getProperty(*this, "SuppressedPositions")) {
                positions->purgeTouched();
            }
        }
    }
}
