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


#ifndef PARTDESIGN_DATUMSHAPE_H
#define PARTDESIGN_DATUMSHAPE_H

#include <App/PropertyLinks.h>
#include <App/DocumentObserver.h>
#include <App/FeaturePython.h>
#include <Mod/Part/App/DatumFeature.h>
#include <Mod/Part/App/SubShapeBinder.h>
#include <Mod/PartDesign/PartDesignGlobal.h>

namespace PartDesign
{

/* This feature is not really a classical datum. It is a fully defined shape and not
 * infinite geometry like a plane and a line. Also it is not calculated by references and hence
 * is not "attached" to anything. Furthermore real shapes must be visualized. This makes it hard
 * to reuse the existing datum infrastructure and a special handling for this type is
 * created.
 */
// TODO Add better documentation (2015-09-11, Fat-Zer)

class PartDesignExport ShapeBinder : public Part::Feature
{
    PROPERTY_HEADER_WITH_OVERRIDE(PartDesign::ShapeBinder);

public:
    ShapeBinder();
    ~ShapeBinder() override;

    App::PropertyLinkSubListGlobal    Support;
    App::PropertyBool TraceSupport;

    static void getFilteredReferences(const App::PropertyLinkSubList* prop, App::GeoFeature*& object, std::vector< std::string >& subobjects);
    static Part::TopoShape buildShapeFromReferences(App::GeoFeature* obj, std::vector< std::string > subs);

    const char* getViewProviderName() const override {
        return "PartDesignGui::ViewProviderShapeBinder";
    }

protected:
    Part::TopoShape updatedShape() const;
    bool hasPlacementChanged() const;
    void handleChangedPropertyType(Base::XMLReader &reader, const char * TypeName, App::Property * prop) override;
    short int mustExecute() const override;
    App::DocumentObjectExecReturn* execute() override;
    void onChanged(const App::Property* prop) override;

private:
    void slotChangedObject(const App::DocumentObject& Obj, const App::Property& Prop);
    void onSettingDocument() override;

    using Connection = fastsignals::connection;
    Connection connectDocumentChangedObject;
};

class PartDesignExport SubShapeBinder : public Part::SubShapeBinder {
    PROPERTY_HEADER_WITH_OVERRIDE(PartDesign::SubShapeBinder);
public:
    using inherited = Part::SubShapeBinder;

    SubShapeBinder();
    ~SubShapeBinder() override;

    const char* getViewProviderName(void) const override {
        return "PartDesignGui::ViewProviderSubShapeBinder";
    }
};

using SubShapeBinderPython = App::FeaturePythonT<SubShapeBinder>;

} //namespace PartDesign


#endif // PARTDESIGN_DATUMSHAPE_H
