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


#pragma once

#include <Gui/WorkbenchManipulator.h>

namespace MatGui {

class WorkbenchManipulator: public Gui::WorkbenchManipulator
{
protected:
    /*!
     * \brief modifyMenuBar
     * Adds the commands Std_SetMaterial and Std_SetAppearance to the View menu
     */
    void modifyMenuBar(Gui::MenuItem* menuBar) override;
    /*!
     * \brief modifyContextMenu
     * Adds the commands Std_SetMaterial and Std_SetAppearance to the contex-menu,
     * plus the library sync commands when the selection has something to sync
     */
    void modifyContextMenu(const char* recipient, Gui::MenuItem* menuBar) override;

private:
    /** \a sync adds the library sync commands, and only where they apply.
     *
     * A context menu is built afresh for every popup, so "where they apply"
     * is a question that can be asked there. A menu bar is built once when
     * the workbench is activated, and the answer would be stale by the time
     * anyone looked, so it does not get them.
     */
    static void addCommands(Gui::MenuItem* menuBar,
                            const char* reference,
                            bool before = false,
                            bool sync = false);
};

} // namespace MatGui