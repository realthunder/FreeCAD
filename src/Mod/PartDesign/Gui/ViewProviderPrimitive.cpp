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
# include <QMessageBox>
#endif

#include <Gui/BitmapFactory.h>
#include <Gui/Command.h>
#include <Gui/Control.h>
#include <Gui/TaskView/TaskDialog.h>
#include <Mod/PartDesign/App/FeaturePrimitive.h>

#include "ViewProviderPrimitive.h"
#include "TaskPrimitiveParameters.h"


using namespace PartDesignGui;

PROPERTY_SOURCE(PartDesignGui::ViewProviderPrimitive,PartDesignGui::ViewProviderAddSub)

ViewProviderPrimitive::ViewProviderPrimitive() = default;

ViewProviderPrimitive::~ViewProviderPrimitive() = default;

void ViewProviderPrimitive::attach(App::DocumentObject* obj) {
    auto* prim = static_cast<PartDesign::FeaturePrimitive*>(obj);
    switch(prim->getPrimitiveType()) {
    case PartDesign::FeaturePrimitive::Box:
        sPixmap = "PartDesign_AdditiveBox.svg";
        break;
    case PartDesign::FeaturePrimitive::Cylinder:
        sPixmap = "PartDesign_AdditiveCylinder.svg";
        break;
    case PartDesign::FeaturePrimitive::Sphere:
        sPixmap = "PartDesign_AdditiveSphere.svg";
        break;
    case PartDesign::FeaturePrimitive::Cone:
        sPixmap = "PartDesign_AdditiveCone.svg";
        break;
    case PartDesign::FeaturePrimitive::Ellipsoid:
        sPixmap = "PartDesign_AdditiveEllipsoid.svg";
        break;
    case PartDesign::FeaturePrimitive::Torus:
        sPixmap = "PartDesign_AdditiveTorus.svg";
        break;
    case PartDesign::FeaturePrimitive::Prism:
        sPixmap = "PartDesign_AdditivePrism.svg";
        break;
    case PartDesign::FeaturePrimitive::Wedge:
        sPixmap = "PartDesign_AdditiveWedge.svg";
        break;
    }

    ViewProviderAddSub::attach(obj);
}

void ViewProviderPrimitive::setupContextMenu(QMenu* menu, QObject* receiver, const char* member)
{
    addDefaultAction(menu, QObject::tr("Edit primitive"));
    inherited::setupContextMenu(menu, receiver, member);
}


TaskDlgFeatureParameters* ViewProviderPrimitive::getEditDialog() {
    return new TaskPrimitiveParameters(this);
}

void ViewProviderPrimitive::updateData(const App::Property* p) {
    PartDesignGui::ViewProviderAddSub::updateData(p);
}
