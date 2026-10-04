// SPDX-License-Identifier: LGPL-2.1-or-later

/****************************************************************************
 *   Copyright (c) 2024 Ondsel <development@ondsel.com>                     *
 *                                                                          *
 *   This file is part of the FreeCAD CAx development system.               *
 *                                                                          *
 *   This library is free software; you can redistribute it and/or          *
 *   modify it under the terms of the GNU Library General Public            *
 *   License as published by the Free Software Foundation; either           *
 *   version 2 of the License, or (at your option) any later version.       *
 *                                                                          *
 *   This library  is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of         *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the          *
 *   GNU Library General Public License for more details.                   *
 *                                                                          *
 *   You should have received a copy of the GNU Library General Public      *
 *   License along with this library; see the file COPYING.LIB. If not,     *
 *   write to the Free Software Foundation, Inc., 59 Temple Place,          *
 *   Suite 330, Boston, MA  02111-1307, USA                                 *
 *                                                                          *
 ****************************************************************************/

#include "PreCompiled.h"

#include <unordered_set>
#include <vector>

#include <App/DocumentObject.h>
#include <Base/Type.h>

#include "DlgAddProperty.h"
#include "MainWindow.h"
#include "ViewProviderVarSet.h"

using namespace Gui;

PROPERTY_SOURCE(Gui::ViewProviderVarSet, Gui::ViewProviderDocumentObject)

ViewProviderVarSet::ViewProviderVarSet()
{
    sPixmap = "VarSet";
    // Nothing to show: as for a text document
    DisplayMode.setStatus(App::Property::Hidden, true);
    OnTopWhenSelected.setStatus(App::Property::Hidden, true);
    SelectionStyle.setStatus(App::Property::Hidden, true);
    Visibility.setStatus(App::Property::Hidden, true);
    // Nor an eye in the tree (upstream 17c601eaca)
    setToggleVisibility(ToggleVisibilityMode::NoToggleVisibility);
}

bool ViewProviderVarSet::doubleClicked()
{
    // The fork's add-property dialog, as the property editor opens it;
    // upstream's dialog rework of the VarSet is not ported
    if (auto obj = getObject()) {
        Dialog::DlgAddProperty dlg(getMainWindow(), {obj});
        dlg.exec();
    }
    return true;
}
