/****************************************************************************
 *   Copyright (c) 2021 Zheng Lei (realthunder) <realthunder.dev@gmail.com> *
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
#include "SavedView.h"

using namespace App;

PROPERTY_SOURCE(App::SavedView, App::DocumentObject)

SavedView::SavedView()
{
    ADD_PROPERTY_TYPE(SaveVisibilities, (true), "VisibilitySettings", App::Prop_None, "Enable saving object visibilities");
    ADD_PROPERTY_TYPE(SaveShowOnTop, (true), "VisibilitySettings", App::Prop_None, "Enable saving show on top objects");
    ADD_PROPERTY_TYPE(SaveCamera, (true), "CameraSettings", App::Prop_None, "Enable saving camera settings");
    ADD_PROPERTY_TYPE(SaveClippings, (true), "ClipSettings", App::Prop_None, "Enable saving clipping planes");
    ADD_PROPERTY_TYPE(SaveDrawStyleSettings, (true), "DrawStyleSettings", App::Prop_None, "Enable saving drawstyle settings");
    ADD_PROPERTY_TYPE(SaveRenderSettings, (true), "RenderSettings", App::Prop_None, "Enable saving the view's own render settings (Render_*, Light_*, Section_*)");
}

SavedView::~SavedView()
{
}

App::Property *SavedView::getProperty(const Base::Type &type, const char *name, const char *group, bool create)
{
    auto prop = getPropertyByName(name);
    if (!prop || !prop->isDerivedFrom(type)) {
        if (!create)
            return nullptr;
        if (prop)
            removeDynamicProperty(name);
        prop = addDynamicProperty(type.getName(), name, group);
        if (!prop)
            FC_THROWM(Base::RuntimeError,"Failed to get property " << name);
        prop->setStatus(App::Property::Hidden, true);
    }
    return prop;
}

void SavedView::handleChangedPropertyName(Base::XMLReader &reader, const char * TypeName, const char *PropName)
{
    if (strcmp(PropName, "SaveShadowSettings") == 0 && strcmp(TypeName, App::PropertyBool::getClassTypeId().getName()) == 0)
    {
        SaveDrawStyleSettings.Restore(reader);
    }
}
