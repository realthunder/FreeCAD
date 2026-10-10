// SPDX-License-Identifier: LGPL-2.1-or-later

/***************************************************************************
 *   Copyright (c) 2023-2024 David Carter <dcarter@david.carter.ca>        *
 *                                                                         *
 *   This file is part of FreeCAD.                                         *
 *                                                                         *
 *   FreeCAD is free software: you can redistribute it and/or modify it    *
 *   under the terms of the GNU Lesser General Public License as           *
 *   published by the Free Software Foundation, either version 2.1 of the  *
 *   License, or (at your option) any later version.                       *
 *                                                                         *
 *   FreeCAD is distributed in the hope that it will be useful, but        *
 *   WITHOUT ANY WARRANTY; without even the implied warranty of            *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU      *
 *   Lesser General Public License for more details.                       *
 *                                                                         *
 *   You should have received a copy of the GNU Lesser General Public      *
 *   License along with FreeCAD. If not, see                               *
 *   <https://www.gnu.org/licenses/>.                                      *
 *                                                                         *
 **************************************************************************/

#include <Mod/Material/App/MaterialParams.h>
#include <App/Application.h>

#include "Exceptions.h"
#include "MaterialFilter.h"
#include "MaterialManager.h"
#include "Materials.h"


using namespace Materials;

TYPESYSTEM_SOURCE(Materials::MaterialFilterOptions, Base::BaseClass)

MaterialFilterOptions::MaterialFilterOptions()
{
    auto param = App::GetApplication().GetParameterGroupByPath(
        "User parameter:BaseApp/Preferences/Mod/Material/Editor");
    _includeFavorites = param->GetBool("ShowFavorites", MaterialParams::defaultEditorShowFavorites());
    _includeRecent = param->GetBool("ShowRecent", MaterialParams::defaultEditorShowRecent());
    _includeFolders = param->GetBool("ShowEmptyFolders", MaterialParams::defaultEditorShowEmptyFolders());
    _includeLibraries = param->GetBool("ShowEmptyLibraries", MaterialParams::defaultEditorShowEmptyLibraries());
    _includeLegacy = param->GetBool("ShowLegacy", MaterialParams::defaultEditorShowLegacy());
}

MaterialFilterTreeWidgetOptions::MaterialFilterTreeWidgetOptions()
{
    auto param = App::GetApplication().GetParameterGroupByPath(
        "User parameter:BaseApp/Preferences/Mod/Material/TreeWidget");
    _includeFavorites = param->GetBool("ShowFavorites", MaterialParams::defaultSelectorShowFavorites());
    _includeRecent = param->GetBool("ShowRecent", MaterialParams::defaultSelectorShowRecent());
    _includeFolders = param->GetBool("ShowEmptyFolders", MaterialParams::defaultSelectorShowEmptyFolders());
    _includeLibraries = param->GetBool("ShowEmptyLibraries", MaterialParams::defaultSelectorShowEmptyLibraries());
    _includeLegacy = param->GetBool("ShowLegacy", MaterialParams::defaultSelectorShowLegacy());
}

//===

TYPESYSTEM_SOURCE(Materials::MaterialFilter, Base::BaseClass)

MaterialFilter::MaterialFilter()
    : _required()
    , _requiredComplete()
    , _requirePhysical(false)
    , _requireAppearance(false)
{}

bool MaterialFilter::modelIncluded(const Material& material) const
{
    if (_requirePhysical) {
        if (!material.hasPhysicalProperties()) {
            return false;
        }
    }
    if (_requireAppearance) {
        if (!material.hasAppearanceProperties()) {
            return false;
        }
    }
    for (const auto& complete : _requiredComplete) {
        if (!material.isModelComplete(complete)) {
            return false;
        }
    }
    for (const auto& required : _required) {
        if (!material.hasModel(required)) {
            return false;
        }
    }

    return true;
}

bool MaterialFilter::modelIncluded(const QString& uuid) const
{
    try {
        auto material = MaterialManager::getManager().getMaterial(uuid);
        return modelIncluded(*material);
    }
    catch (const MaterialNotFound&) {
    }
    return false;
}

void MaterialFilter::addRequired(const QString& uuid)
{
    // Ignore any uuids already present
    if (!_requiredComplete.contains(uuid)) {
        _required.insert(uuid);
    }
}

void MaterialFilter::addRequiredComplete(const QString& uuid)
{
    if (_required.contains(uuid)) {
        // Completeness takes priority
        _required.remove(uuid);
    }
    _requiredComplete.insert(uuid);
}

void MaterialFilter::clear()
{
    _required.clear();
    _requiredComplete.clear();
}
