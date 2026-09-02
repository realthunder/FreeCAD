// SPDX-License-Identifier: LGPL-2.1-or-later

/***************************************************************************
 *   Copyright (c) 2024 Werner Mayer <wmayer[at]users.sourceforge.net>     *
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

#include "WorkbenchManipulator.h"
#include <Gui/Application.h>
#include <Gui/Command.h>
#include <Gui/MenuManager.h>
#include <Gui/ToolBarManager.h>

using namespace MatGui;

namespace
{

/** Whether a command would do something with what is selected right now.
 *
 * Asking the command itself keeps one answer in one place: the menu shows an
 * entry exactly when activating it would have an effect.
 */
bool applies(const char* command)
{
    auto found = Gui::Application::Instance->commandManager().getCommandByName(command);
    return found && found->isActive();
}

}  // namespace

void WorkbenchManipulator::modifyMenuBar([[maybe_unused]] Gui::MenuItem* menuBar)
{
    addCommands(menuBar, "Std_ToggleNavigation");
}

void WorkbenchManipulator::modifyContextMenu(const char* recipient, Gui::MenuItem* menuBar)
{
    // This fork's context menus (Gui/Workbench.cpp) are built differently from
    // upstream's: there is no Std_RandomColor, and Std_TreeSelection sits inside
    // the Selection submenu, so upstream's anchors would either miss or bury the
    // commands. Std_RenderSettings is what followed Std_SetAppearance in both
    // recipients, so going in front of it puts them back where they always were.
    if (strcmp(recipient, "View") == 0 || strcmp(recipient, "Tree") == 0) {
        addCommands(menuBar, "Std_RenderSettings", true, true);
    }
}

void WorkbenchManipulator::addCommands(Gui::MenuItem* menuBar,
                                      const char* reference,
                                      bool before,
                                      bool sync)
{
    auto par = menuBar->findParentOf(reference);
    if (par) {
        auto item = par->findItem(reference);
        if (!before) {
            item = par->afterItem(item);
        }

        auto cmd1 = new Gui::MenuItem();
        cmd1->setCommand("Std_SetMaterial");
        par->insertItem(item, cmd1);
        auto cmd2 = new Gui::MenuItem();
        cmd2->setCommand("Std_SetAppearance");
        par->insertItem(item, cmd2);

        if (!sync) {
            return;
        }
        // The way back from a look the user chose, beside the command that
        // chose it, and only while there is a card to go back to
        // (docs/MaterialStorage.md 15.5).
        if (applies("Material_ResetAppearance")) {
            auto reset = new Gui::MenuItem();
            reset->setCommand("Material_ResetAppearance");
            par->insertItem(item, reset);
        }
        // The two sync commands (docs/MaterialStorage.md sec 13) go next to
        // the command that assigned the material in the first place, but only
        // when they have something to act on: a divergence to take, or a card
        // to write back. An entry greyed out nine times in ten is clutter in a
        // menu that is long already.
        // Copy Material and Paste Material (docs/MaterialStorage.md 17.12)
        // under the same rule: Copy while the selection carries a card or a
        // look, Paste while the clipboard holds one
        for (const char* command : {"Material_UpdateFromLibrary",
                                    "Material_SaveToLibrary",
                                    "Material_Copy",
                                    "Material_Paste"}) {
            if (applies(command)) {
                auto sync = new Gui::MenuItem();
                sync->setCommand(command);
                par->insertItem(item, sync);
            }
        }
    }
}

