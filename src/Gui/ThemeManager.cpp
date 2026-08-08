// SPDX-License-Identifier: LGPL-2.1-or-later

/****************************************************************************
 *   Copyright (c) 2026 The FreeCAD project                                 *
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
#endif

#include <App/Application.h>

#include "ThemeManager.h"


using namespace Gui;

namespace
{

ParameterGrp::handle userMainWindow()
{
    return App::GetApplication().GetParameterGroupByPath(
        "User parameter:BaseApp/Preferences/MainWindow");
}

/**
 * Walk a parameter tree without creating anything. GetGroup() materialises the
 * groups it walks through, which for a pack's parameters would mean inserting
 * empty groups into the user's config a moment later.
 */
Base::Reference<ParameterGrp> findGroup(Base::Reference<ParameterGrp> group,
                                        const std::vector<const char*>& path)
{
    for (const char* name : path) {
        if (!group.isValid() || !group->HasGroup(name)) {
            return {};
        }
        group = group->GetGroup(name);
    }
    return group;
}

bool declares(const Base::Reference<ParameterGrp>& group, const std::string& key)
{
    if (!group.isValid()) {
        return false;
    }
    const auto names = group->GetParameterNames();
    return std::any_of(names.begin(), names.end(), [&key](const auto& entry) {
        return entry.second == key;
    });
}

/// MainWindow/IconSet holds a ';'-separated list, read left to right.
std::vector<std::string> splitIconSet(const std::string& value)
{
    std::vector<std::string> entries;
    std::string::size_type pos = 0;
    while (pos <= value.size()) {
        auto next = value.find(';', pos);
        if (next == std::string::npos) {
            next = value.size();
        }
        auto entry = value.substr(pos, next - pos);
        const auto first = entry.find_first_not_of(" \t");
        if (first != std::string::npos) {
            const auto last = entry.find_last_not_of(" \t");
            entries.push_back(entry.substr(first, last - first + 1));
        }
        pos = next + 1;
    }
    return entries;
}

std::string joinIconSet(const std::vector<std::string>& entries)
{
    std::string joined;
    for (const auto& entry : entries) {
        if (!joined.empty()) {
            joined += ';';
        }
        joined += entry;
    }
    return joined;
}

/// Keep the user's config free of empty strings that mean nothing.
void setOrRemove(const ParameterGrp::handle& group, const char* key, const std::string& value)
{
    if (value.empty()) {
        group->RemoveASCII(key);
    }
    else {
        group->SetASCII(key, value.c_str());
    }
}

/// Bookkeeping: which entries of IconSet the last applied theme contributed.
const char* ThemeIconSetKey = "ThemeIconSet";

}  // namespace


const std::vector<std::string>& ThemeManager::appearanceKeys()
{
    static const std::vector<std::string> keys {
        "ColorScheme",
        "StyleSheet",
        "OverlayActiveStyleSheet",
        "MenuStyleSheet",
        "TiledBackground",
    };
    return keys;
}

std::string ThemeManager::currentTheme()
{
    return userMainWindow()->GetASCII("Theme");
}

void ThemeManager::setCurrentTheme(const std::string& name)
{
    setOrRemove(userMainWindow(), "Theme", name);
}

ThemeManager::IconSetPolicy ThemeManager::iconSetPolicy()
{
    const std::string policy = userMainWindow()->GetASCII("ThemeIconSetPolicy", "Reset");
    if (policy == "Merge") {
        return IconSetPolicy::Merge;
    }
    if (policy == "Keep") {
        return IconSetPolicy::Keep;
    }
    return IconSetPolicy::Reset;
}

void ThemeManager::setIconSetPolicy(IconSetPolicy policy)
{
    const char* value = "Reset";
    if (policy == IconSetPolicy::Merge) {
        value = "Merge";
    }
    else if (policy == IconSetPolicy::Keep) {
        value = "Keep";
    }
    userMainWindow()->SetASCII("ThemeIconSetPolicy", value);
}

ThemeManager::Transition ThemeManager::beginThemeChange(ParameterGrp& packParameters)
{
    Transition transition;
    transition.active = true;
    transition.policy = iconSetPolicy();

    auto hMain = userMainWindow();

    const auto packMain = findGroup(Base::Reference<ParameterGrp>(&packParameters),
                                    {"BaseApp", "Preferences", "MainWindow"});
    transition.packDeclaresIconSet = declares(packMain, "IconSet");
    if (transition.packDeclaresIconSet) {
        transition.packIconSet = packMain->GetASCII("IconSet");
    }

    transition.originalIconSet = hMain->GetASCII("IconSet");

    // The user's own icon sets are whatever is set now minus what the last theme
    // contributed. Without that subtraction Merge would accrete one entry per
    // theme switch and never converge.
    auto entries = splitIconSet(transition.originalIconSet);
    for (const auto& fromTheme : splitIconSet(hMain->GetASCII(ThemeIconSetKey))) {
        entries.erase(std::remove(entries.begin(), entries.end(), fromTheme), entries.end());
    }
    transition.userIconSet = joinIconSet(entries);

    // Clearing rather than overwriting is the point: a key the theme omits has
    // to fall back to its coded default, and only a key that is absent does.
    const auto& keys = appearanceKeys();
    for (const auto& entry : hMain->GetParameterNames()) {
        if (std::find(keys.begin(), keys.end(), entry.second) != keys.end()) {
            hMain->RemoveAttribute(entry.first, entry.second.c_str());
        }
    }

    return transition;
}

void ThemeManager::endThemeChange(const Transition& transition)
{
    if (!transition.active) {
        return;
    }

    auto hMain = userMainWindow();

    switch (transition.policy) {
        case IconSetPolicy::Keep:
            // The insert may have written the theme's icon set over the top.
            setOrRemove(hMain, "IconSet", transition.originalIconSet);
            break;

        case IconSetPolicy::Reset:
            setOrRemove(hMain, "IconSet", transition.packIconSet);
            setOrRemove(hMain, ThemeIconSetKey, transition.packIconSet);
            break;

        case IconSetPolicy::Merge: {
            const auto packEntries = splitIconSet(transition.packIconSet);
            auto merged = splitIconSet(transition.userIconSet);
            // An entry the theme also names would be loaded twice; drop the
            // user's copy and let the theme's, which comes last, apply.
            merged.erase(std::remove_if(merged.begin(),
                                        merged.end(),
                                        [&packEntries](const std::string& entry) {
                                            return std::find(packEntries.begin(),
                                                             packEntries.end(),
                                                             entry)
                                                != packEntries.end();
                                        }),
                         merged.end());
            merged.insert(merged.end(), packEntries.begin(), packEntries.end());
            setOrRemove(hMain, "IconSet", joinIconSet(merged));
            setOrRemove(hMain, ThemeIconSetKey, transition.packIconSet);
            break;
        }
    }
}

void ThemeManager::forgetThemeIfAppearanceChanged(ParameterGrp& packParameters)
{
    if (currentTheme().empty()) {
        return;
    }

    const auto packMain = findGroup(Base::Reference<ParameterGrp>(&packParameters),
                                    {"BaseApp", "Preferences", "MainWindow"});
    if (!packMain.isValid()) {
        return;
    }

    const auto& keys = appearanceKeys();
    for (const auto& entry : packMain->GetParameterNames()) {
        if (entry.second == "IconSet"
            || std::find(keys.begin(), keys.end(), entry.second) != keys.end()) {
            setCurrentTheme({});
            return;
        }
    }
}
