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

#ifndef APP_SAVED_VIEW_H
#define APP_SAVED_VIEW_H

#include "DocumentObject.h"
#include "PropertyStandard.h"
#include "PropertyGeo.h"

namespace App
{

class AppExport SavedView : public App::DocumentObject
{
    PROPERTY_HEADER_WITH_OVERRIDE(App::SavedView);
    typedef App::DocumentObject inherited;

public:
    App::PropertyBool SaveVisibilities;
    App::PropertyBool SaveShowOnTop;
    App::PropertyBool SaveCamera;
    App::PropertyBool SaveClippings;
    App::PropertyBool SaveDrawStyleSettings;
    App::PropertyBool SaveRenderSettings;

    SavedView();
    virtual ~SavedView();

    virtual const char* getViewProviderName(void) const override{
        return "Gui::ViewProviderSavedView";
    }

    virtual void handleChangedPropertyName(Base::XMLReader &reader, const char * TypeName, const char *PropName) override;

    template<class PropT>
    PropT *getVisibilityProperty(const char *name, bool create=false) {
        return Base::freecad_dynamic_cast<PropT>(getProperty(PropT::getClassTypeId(), name, "VisibilitySettings", create));
    }
    template<class PropT>
    PropT *getClippingProperty(const char *name, bool create=false) {
        return Base::freecad_dynamic_cast<PropT>(getProperty(PropT::getClassTypeId(), name, "ClipSettings", create));
    }
    /// The per-view render settings a saved view restores: the Render_*,
    /// Light_* and Section_* properties the view carries. Only what the
    /// view overrides is captured -- a knob still following the
    /// preference is somebody's own setting, not this document's.
    template<class PropT>
    PropT *getRenderProperty(const char *name, bool create=false) {
        return Base::freecad_dynamic_cast<PropT>(getProperty(PropT::getClassTypeId(), name, "RenderSettings", create));
    }
    template<class PropT>
    PropT *getCameraProperty(const char *name, bool create=false) {
        return Base::freecad_dynamic_cast<PropT>(getProperty(PropT::getClassTypeId(), name, "CameraSettings", create));
    }

    App::Property *getProperty(const Base::Type &type, const char *name, const char *group, bool create);
};

}

#endif //APP_SAVED_VIEW_H
