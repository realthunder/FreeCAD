/***************************************************************************
 *   Copyright (c) 2015 Stefan Tröger <stefantroeger@gmx.net>              *
 *                                                                         *
 *   This file is part of the FreeCAD CAx development system.              *
 *                                                                         *
 *   This library is free software; you can redistribute it and/or         *
 *   modify it under the terms of the GNU Library General Public           *
 *   License as published by the Free Software Foundation; either          *
 *   version 2 of the License, or (at your option) any later version.      *
 *                                                                         *
 *   This library  is distributed in the hope that it will be useful,      *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU Library General Public License for more details.                  *
 *                                                                         *
 *   You should have received a copy of the GNU Library General Public     *
 *   License along with this library; see the file COPYING.LIB. If not,    *
 *   write to the Free Software Foundation, Inc., 59 Temple Place,         *
 *   Suite 330, Boston, MA  02111-1307, USA                                *
 *                                                                         *
 ***************************************************************************/


#include "PreCompiled.h"

#ifndef _PreComp_
# include <QMenu>
#endif

#include <Gui/Application.h>
#include <Gui/BitmapFactory.h>
#include <Mod/Part/Gui/ReferenceHighlighter.h>
#include <Mod/PartDesign/App/FeatureLoft.h>

#include "ViewProviderLoft.h"
#include "TaskLoftParameters.h"

using namespace PartDesignGui;

PROPERTY_SOURCE(PartDesignGui::ViewProviderLoft,PartDesignGui::ViewProviderAddSub)

ViewProviderLoft::ViewProviderLoft()
{
    sPixmap = "PartDesign_AdditiveLoft.svg";
}

ViewProviderLoft::~ViewProviderLoft() = default;

std::vector<App::DocumentObject*> ViewProviderLoft::_claimChildren() const
{
    std::vector<App::DocumentObject*> temp;

    PartDesign::Loft* pcLoft = static_cast<PartDesign::Loft*>(getObject());

    App::DocumentObject* sketch = pcLoft->Profile.getValue();
    if (sketch && !sketch->isDerivedFrom(PartDesign::Feature::getClassTypeId()))
        temp.push_back(sketch);

    for(App::DocumentObject* obj : pcLoft->Sections.getValues()) {
        if (obj && !obj->isDerivedFrom(PartDesign::Feature::getClassTypeId())
                && std::find(temp.begin(), temp.end(), obj) == temp.end())
            temp.push_back(obj);
    }

    return temp;
}

void ViewProviderLoft::setupContextMenu(QMenu* menu, QObject* receiver, const char* member)
{
    addDefaultAction(menu, QObject::tr("Edit loft"));
    PartDesignGui::ViewProvider::setupContextMenu(menu, receiver, member);
}

TaskDlgFeatureParameters* ViewProviderLoft::getEditDialog() {
    return new TaskDlgLoftParameters(this);
}
