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

#ifndef GUI_VIEWPROVIDERLINKARRAY_H
#define GUI_VIEWPROVIDERLINKARRAY_H

#include "ViewProviderLink.h"

namespace Gui
{

/// The view of an App::LinkArray: a link, whose default edit is its pattern
class GuiExport ViewProviderLinkArray: public ViewProviderLink
{
    PROPERTY_HEADER_WITH_OVERRIDE(Gui::ViewProviderLinkArray);
    using inherited = ViewProviderLink;

public:
    ViewProviderLinkArray();

    bool doubleClicked() override;
    ViewProvider* startEditing(int ModNum) override;
    void setupContextMenu(QMenu* menu, QObject* receiver, const char* member) override;

protected:
    bool setEdit(int ModNum) override;
    void unsetEdit(int ModNum) override;
};

}  // namespace Gui

#endif  // GUI_VIEWPROVIDERLINKARRAY_H
