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
#include <QAction>
#include <QMenu>
#endif

#include "Control.h"
#include "Document.h"
#include "TaskLinkArray.h"
#include "ViewProviderLinkArray.h"

using namespace Gui;

PROPERTY_SOURCE(Gui::ViewProviderLinkArray, Gui::ViewProviderLink)

ViewProviderLinkArray::ViewProviderLinkArray() = default;

bool ViewProviderLinkArray::doubleClicked()
{
    return getDocument()->setEdit(this, ViewProvider::Default);
}

ViewProvider* ViewProviderLinkArray::startEditing(int ModNum)
{
    // A link forwards the default edit to the object it links to; an
    // array's is its own pattern
    if (ModNum == ViewProvider::Default) {
        return ViewProviderDocumentObject::startEditing(ModNum);
    }
    return inherited::startEditing(ModNum);
}

void ViewProviderLinkArray::setupContextMenu(QMenu* menu, QObject* receiver, const char* member)
{
    QAction* act = menu->addAction(QObject::tr("Edit pattern"), receiver, member);
    act->setData(QVariant(static_cast<int>(ViewProvider::Default)));
    inherited::setupContextMenu(menu, receiver, member);
}

bool ViewProviderLinkArray::setEdit(int ModNum)
{
    if (ModNum != ViewProvider::Default) {
        return inherited::setEdit(ModNum);
    }
    if (auto dlg = Control().activeDialog()) {
        // Another panel is open: show it, not this one over it
        Control().showDialog(dlg);
        return false;
    }
    Control().showDialog(new TaskDlgLinkArray(this));
    return true;
}

void ViewProviderLinkArray::unsetEdit(int ModNum)
{
    if (ModNum != ViewProvider::Default) {
        inherited::unsetEdit(ModNum);
        return;
    }
    Control().closeDialog();
}
