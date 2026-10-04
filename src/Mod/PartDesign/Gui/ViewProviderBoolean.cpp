/***************************************************************************
 *   Copyright (c) 2013 Jan Rheinländer                                    *
 *                                   <jrheinlaender@users.sourceforge.net> *
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

#include <algorithm>

#ifndef _PreComp_
# include <QMenu>
# include <QMessageBox>
#endif

#include "ViewProviderBoolean.h"
#include "TaskBooleanParameters.h"
#include <Mod/PartDesign/App/FeatureBoolean.h>
#include <Gui/Application.h>
#include <Gui/Control.h>
#include <Gui/Command.h>
#include <Gui/Document.h>
#include <Gui/SoFCUnifiedSelection.h>
#include <Mod/Part/Gui/PartParams.h>


using namespace PartDesignGui;

PROPERTY_SOURCE_WITH_EXTENSIONS(PartDesignGui::ViewProviderBoolean,PartDesignGui::ViewProviderAddSub)

const char* PartDesignGui::ViewProviderBoolean::DisplayEnum[] = {"Result","Tools",nullptr};


std::vector<App::DocumentObject*> ViewProviderBoolean::claimChildren3D() const
{
    auto children = ViewProviderAddSub::claimChildren3D();
    auto boolean = Base::freecad_dynamic_cast<PartDesign::Boolean>(getObject());
    if (boolean) {
        children.erase(std::remove_if(children.begin(), children.end(),
                                      [boolean](App::DocumentObject *obj) {
                                          return !boolean->hasObject(obj);
                                      }),
                       children.end());
    }
    return children;
}

ViewProviderBoolean::ViewProviderBoolean()
{
    sPixmap = "PartDesign_Boolean.svg";
    Gui::ViewProviderGeoFeatureGroupExtension::initExtension(this);

    ADD_PROPERTY(Display,((long)0));
    Display.setEnums(DisplayEnum);

    if(pcModeSwitch->isOfType(SoFCSwitch::getClassTypeId()))
        static_cast<SoFCSwitch*>(pcModeSwitch)->defaultChild = 1;
}

ViewProviderBoolean::~ViewProviderBoolean() = default;


void ViewProviderBoolean::setupContextMenu(QMenu* menu, QObject* receiver, const char* member)
{
    addDefaultAction(menu, QObject::tr("Edit boolean"));
    PartDesignGui::ViewProvider::setupContextMenu(menu, receiver, member);
}

TaskDlgFeatureParameters *ViewProviderBoolean::getEditDialog()
{
    return new TaskDlgBooleanParameters( this );
}

void ViewProviderBoolean::attach(App::DocumentObject* obj) {
    ViewProviderAddSub::attach(obj);

    //set default display mode to override the "Group" display mode
    setDisplayMode("Flat Lines");
}

void ViewProviderBoolean::updateData(const App::Property* prop)
{
    if (auto feat = Base::freecad_dynamic_cast<PartDesign::Boolean>(getObject())) {
        if (prop == &feat->ToolShape)
            updateAddSubShapeIndicator();
        else if (prop == &feat->Type)
            checkAddSubColor();
        else if (prop == &feat->BaseFeature)
            refreshPreviewBase();

        if (prop == &feat->BaseFeature || prop == &feat->Placement || prop == &feat->ToolShape)
            updatePreviewTransform(feat->ToolShape.getShape());
    }
    ViewProviderAddSub::updateData(prop);
}

void ViewProviderBoolean::checkAddSubColor()
{
    auto feat = Base::freecad_dynamic_cast<PartDesign::Boolean>(getObject());
    if (!feat)
        return;
    const char *type = feat->Type.getValueAsString();
    uint32_t color;
    if (type && strcmp(type, "Cut") == 0)
        color = PartGui::PartParams::getPreviewSubColor();
    else if (type && strcmp(type, "Common") == 0)
        color = PartGui::PartParams::getPreviewIntersectColor();
    else
        color = PartGui::PartParams::getPreviewAddColor();
    applyPreviewColor(App::Color(color));
}

// The DisplayMode property's own default, not just the mode shown: the
// property starts at its first enum, "Group", and onChanged() would take
// that for Display = Tools (upstream a548ca698a)
const char* ViewProviderBoolean::getDefaultDisplayMode() const
{
    return "Flat Lines";
}

void ViewProviderBoolean::onChanged(const App::Property* prop) {

    PartDesignGui::ViewProvider::onChanged(prop);

    if(prop == &Display) {

        if(Display.getValue() == 0) {
            const char *mode = DisplayMode.getValueAsString();
            if (mode && strcmp(mode, "Group") == 0)
                setDisplayMode("Flat Lines");
        } else {
            setDisplayMode("Group");
        }
    } else if (prop == &DisplayMode) {
        const char *mode = DisplayMode.getValueAsString();
        if (isRestoring()) {
            if (Display.getValue() == 0) {
                // Because an old bug where DisplayMode is not synced by
                // setDisplayMode(), we shall respect Display value over
                // DisplayMode.
                if (mode && strcmp(mode, "Group") == 0)
                    setDisplayMode("Flat Lines");
            } else if (mode && strcmp(mode, "Group") != 0)
                setDisplayMode("Group");
        } else if (mode && strcmp(mode, "Group") == 0) {
            Display.setValue((long)1);
        } else
            Display.setValue((long)0);
    }
}

void ViewProviderBoolean::extensionModeSwitchChange()
{
    // Skip ViewProviderGeoFeatureExtension::extensionModeSwitchChange() so
    // that GroupExtension::extensionGetSubObjects(GS_SELECT) can work
    // TODO: find a better work around
}
