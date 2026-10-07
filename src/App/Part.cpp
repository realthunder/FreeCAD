/***************************************************************************
 *   Copyright (c) 2014 Jürgen Riegel <juergen.riegel@web.de>              *
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

#include <App/DocumentObject.h>

#include "Application.h"
#include "LinkAppearance.h"
#include "Part.h"
#include "PartPy.h"


using namespace App;


PROPERTY_SOURCE_WITH_EXTENSIONS(App::Part, App::GeoFeature)


//===========================================================================
// Part
//===========================================================================


Part::Part()
{
    ADD_PROPERTY_TYPE(Type,(""),"Part", App::Prop_None,"");
    ADD_PROPERTY_TYPE(Material, (nullptr), 0, App::Prop_None, "The Material for this Part");
    ADD_PROPERTY_TYPE(Meta, (), "Part", App::Prop_None, "Map with additional meta information");

    // create the uuid for the document
    Base::Uuid id;
    ADD_PROPERTY_TYPE(Id, (""), "Part", App::Prop_None, "ID (Part-Number) of the Item");
    ADD_PROPERTY_TYPE(Uid, (id), "Part", App::Prop_None, "UUID of the Item");

    // license stuff (leave them empty to avoid confusion with imported 3rd party STEP/IGES files)
    ADD_PROPERTY_TYPE(License, (""), "Part", App::Prop_None, "License string of the Item");
    ADD_PROPERTY_TYPE(LicenseURL, (""), "Part", App::Prop_None, "URL to the license text/contract");

    ADD_PROPERTY_TYPE(ColoredElements, (0), 0, (PropertyType)(Prop_ReadOnly|Prop_Hidden), "");

    // What the part lays over what it holds, as a link does
    // (docs/ShapeAppearanceDesign.md sec 14.6.4): the store, and the names
    // over it, which are in no file
    static const char *appearances = "Appearances";
    ADD_PROPERTY_TYPE(ElementAppearance, (0), appearances,
            (PropertyType)(Prop_Hidden|Prop_Output),
            "The looks the part gives what it holds: its own, and those of the\n"
            "elements it names");
    ElementAppearance.setPathNames(true);
    ColoredElements.setStatus(Property::Legacy, true);
    const auto name = (PropertyType)(Prop_NoPersist|Prop_Output|Prop_NoRecompute);
    ADD_PROPERTY_TYPE(OverrideMaterial, (false), appearances, name,
            "Give what the part holds a look of the part's own");
    App::MaterialAppearance mat(App::MaterialAppearance::DEFAULT);
    mat.diffuseColor.setPackedValue(static_cast<uint32_t>(
        GetApplication().GetParameterGroupByPath("User parameter:BaseApp/Preferences/View")
            ->GetUnsigned("DefaultShapeColor", 0xCCCCE6FFUL)));
    ADD_PROPERTY_TYPE(ShapeAppearance, (mat), appearances, name,
            "The look the part gives what it holds, where it gives one");
    ShapeAppearance.setStatus(Property::MaterialEdit, true);
    ShapeAppearance.setWriter([this](const AppearanceList &, const AppearanceList &after, int) {
        LinkAppearance::writeAppearance(LinkAppearance::namesOf(this), after, mirroringLooks);
    });

    OriginGroupExtension::initExtension(this);

    ExportMode.setStatus(Property::Hidden,false);
    ExportMode.setValue(ExportByVisibility);
}

Part::~Part() = default;

void Part::onChanged(const Property *prop)
{
    if (prop == &ElementAppearance || prop == &ColoredElements || prop == &OverrideMaterial) {
        LinkAppearance::onChanged(LinkAppearance::namesOf(this), prop, mirroringLooks);
    }
    GeoFeature::onChanged(prop);
}

void Part::onDocumentRestored()
{
    LinkAppearance::onRestored(LinkAppearance::namesOf(this), mirroringLooks);
    GeoFeature::onDocumentRestored();
}

static App::Part *_getPartOfObject(const DocumentObject *obj,
                                   std::set<const DocumentObject*> *objset)
{
    if (!obj || !obj->getNameInDocument())
        return nullptr;
    // as a Part is a geofeaturegroup it must directly link to all
    // objects it contains, even if they are in additional groups etc.
    // But we still must call 'hasObject()' to exclude link brought in by
    // expressions.
    for (auto inObj : obj->getInList()) {
        if (objset && !objset->insert(inObj).second)
            continue;
        auto group = inObj->getExtensionByType<GeoFeatureGroupExtension>(true);
        if(group && group->hasObject(obj)) {
            if(inObj->isDerivedFrom(App::Part::getClassTypeId()))
                return static_cast<App::Part*>(inObj);
            else if (objset)
                return _getPartOfObject(inObj, objset);
            // Only one parent geofeature group per object, so break
            break;
        }
    }

    return nullptr;
}

App::Part *Part::getPartOfObject (const DocumentObject* obj, bool recursive) {
    if (!recursive)
        return _getPartOfObject(obj, nullptr);
    std::set<const DocumentObject *> objset;
    objset.insert(obj);
    return _getPartOfObject(obj, &objset);
}


PyObject *Part::getPyObject()
{
    if (PythonObject.is(Py::_None())){
        // ref counter is set to 1
        PythonObject = Py::Object(new PartPy(this),true);
    }
    return Py::new_reference_to(PythonObject);
}

void Part::handleChangedPropertyType(Base::XMLReader &reader, const char *TypeName, App::Property *prop)
{
    // Migrate Material from App::PropertyMap to App::PropertyLink
    if (!strcmp(TypeName, "App::PropertyMap")) {
        App::PropertyMap oldvalue;
        oldvalue.Restore(reader);
        if (oldvalue.getSize()) {
            auto oldprop = static_cast<App::PropertyMap*>(addDynamicProperty("App::PropertyMap", "Material_old", "Base"));
            oldprop->setValues(oldvalue.getValues());
        }
    } else {
        App::GeoFeature::handleChangedPropertyType(reader, TypeName, prop);
    }
}

// Python feature ---------------------------------------------------------

// Not quite sure yet making Part derivable in Python is good Idea!
// JR 2014

//namespace App {
///// @cond DOXERR
//PROPERTY_SOURCE_TEMPLATE(App::PartPython, App::Part)
//template<> const char* App::PartPython::getViewProviderName(void) const {
//    return "Gui::ViewProviderPartPython";
//}
//template<> PyObject* App::PartPython::getPyObject(void) {
//    if (PythonObject.is(Py::_None())) {
//        // ref counter is set to 1
//        PythonObject = Py::Object(new FeaturePythonPyT<App::PartPy>(this),true);
//    }
//    return Py::new_reference_to(PythonObject);
//}
///// @endcond
//
//// explicit template instantiation
//template class AppExport FeaturePythonT<App::Part>;
//}
