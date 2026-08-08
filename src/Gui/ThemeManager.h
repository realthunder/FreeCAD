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

#ifndef GUI_THEMEMANAGER_H
#define GUI_THEMEMANAGER_H

#include <string>
#include <vector>

#include <Base/Parameter.h>

#include "FCGlobal.h"


namespace Gui
{

/**
 * \brief Who owns the application's appearance.
 *
 * Appearance is spread over several parameters, and a preference pack writes
 * only the ones its .cfg happens to contain. Left alone that makes a theme a
 * partial description: switching from a theme that sets an overlay stylesheet
 * to one that does not used to leave the first theme's overlay in place.
 *
 * ThemeManager names the set once and makes a theme own all of it. The one
 * exception is the icon set, which users curate independently of the theme --
 * see IconSetPolicy.
 */
class GuiExport ThemeManager
{
public:
    /**
     * The keys under BaseApp/Preferences/MainWindow that a theme owns outright.
     * Applying a theme clears every one of them first, so a key the theme omits
     * falls back to its coded default rather than surviving from the theme
     * before it.
     *
     * "IconSet" is deliberately absent: it is governed by iconSetPolicy().
     */
    static const std::vector<std::string>& appearanceKeys();

    /// The preference pack whose appearance is currently in effect, or empty.
    static std::string currentTheme();
    static void setCurrentTheme(const std::string& name);

    /**
     * True when an appearance key no longer holds what the recorded theme
     * declares -- the state the UI calls "modified".
     *
     * IconSet counts only while the policy is Reset. Under Merge and Keep it is
     * meant to differ from the theme, so counting it would leave every theme
     * permanently modified. Accent colors are not compared either: no pack
     * declares them, so they belong to the user rather than the theme.
     */
    static bool isCustomised();

    /// What applying a theme does to MainWindow/IconSet.
    enum class IconSetPolicy
    {
        /// The theme owns the icon set: its value wins, and if it names none the
        /// icon set is cleared. The default.
        Reset,
        /// The theme's icon set layers on top of the user's. Icon sets are read
        /// in order and a later entry wins, so the theme overrides the icons it
        /// names and everything else the user chose survives.
        Merge,
        /// The theme never touches the icon set.
        Keep,
    };

    static IconSetPolicy iconSetPolicy();
    static void setIconSetPolicy(IconSetPolicy policy);

    /// State carried across the insertion of a pack's parameters.
    struct Transition
    {
        bool active = false;
        IconSetPolicy policy = IconSetPolicy::Reset;
        /// MainWindow/IconSet exactly as it was.
        std::string originalIconSet;
        /// The entries of it the user owns, i.e. minus what the last theme added.
        std::string userIconSet;
        /// What the incoming theme asks for.
        std::string packIconSet;
        bool packDeclaresIconSet = false;
    };

    /**
     * Clear the appearance keys ahead of inserting a theme pack's parameters.
     * \param packParameters the pack's loaded .cfg, read to see what it declares
     */
    static Transition beginThemeChange(ParameterGrp& packParameters);

    /// Settle MainWindow/IconSet per the policy, once the pack has been inserted.
    static void endThemeChange(const Transition& transition);

    /**
     * A pack that is not a theme but still moves appearance keys leaves the
     * recorded theme name describing something that is no longer on screen.
     * Forget it, so the UI says "custom" rather than lying.
     */
    static void forgetThemeIfAppearanceChanged(ParameterGrp& packParameters);
};

}  // namespace Gui

#endif  // GUI_THEMEMANAGER_H
