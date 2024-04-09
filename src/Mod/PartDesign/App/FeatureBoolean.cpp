/******************************************************************************
 *   Copyright (c) 2013 Jan Rheinländer <jrheinlaender@users.sourceforge.net> *
 *                                                                            *
 *   This file is part of the FreeCAD CAx development system.                 *
 *                                                                            *
 *   This library is free software; you can redistribute it and/or            *
 *   modify it under the terms of the GNU Library General Public              *
 *   License as published by the Free Software Foundation; either             *
 *   version 2 of the License, or (at your option) any later version.         *
 *                                                                            *
 *   This library  is distributed in the hope that it will be useful,         *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of           *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the            *
 *   GNU Library General Public License for more details.                     *
 *                                                                            *
 *   You should have received a copy of the GNU Library General Public        *
 *   License along with this library; see the file COPYING.LIB. If not,       *
 *   write to the Free Software Foundation, Inc., 59 Temple Place,            *
 *   Suite 330, Boston, MA  02111-1307, USA                                   *
 *                                                                            *
 ******************************************************************************/


#include "PreCompiled.h"
#include "ShapeBinder.h"
#ifndef _PreComp_
# include <BRepAlgoAPI_Common.hxx>
# include <BRepAlgoAPI_Cut.hxx>
# include <BRepAlgoAPI_Fuse.hxx>
# include <Standard_Failure.hxx>
#endif

#include <App/Application.h>
#include <App/Document.h>
#include <Mod/Part/App/TopoShapeOpCode.h>
#include <Mod/PartDesign/App/ShapeBinder.h>

#include "FeatureBoolean.h"
#include "Body.h"

FC_LOG_LEVEL_INIT("PartDesign", true, true);

using namespace PartDesign;

namespace PartDesign {

PROPERTY_SOURCE_WITH_EXTENSIONS(PartDesign::Boolean, PartDesign::Feature)

const char* Boolean::TypeEnums[]= {"Fuse","Cut","Common","Compound","Section",nullptr};

Boolean::Boolean()
{
    ADD_PROPERTY(Type,((long)0));
    Type.setEnums(TypeEnums);

    ADD_PROPERTY_TYPE(Refine,(0),"Part Design",(App::PropertyType)(App::Prop_None),"Refine shape (clean up redundant edges) after adding/subtracting");
    Base::Reference<ParameterGrp> hGrp = App::GetApplication().GetUserParameter()
        .GetGroup("BaseApp")->GetGroup("Preferences")->GetGroup("Mod/PartDesign");
    this->Refine.setValue(hGrp->GetBool("RefineModel", false));
    initExtension(this);
}

short Boolean::mustExecute() const
{
    if (Group.isTouched())
        return 1;
    return PartDesign::Feature::mustExecute();
}

App::DocumentObjectExecReturn *Boolean::execute()
{
    // Get the operation type
    std::string type = Type.getValueAsString();
    std::vector<App::DocumentObject*> tools = Group.getValues();
    auto itBegin = tools.begin();
    auto itEnd = tools.end();

    // Get the base shape to operate on
    TopoShape baseShape;
    App::DocumentObject * base = getBaseObject(true);
    if (base && !NewSolid.getValue()) {
        // In case the base is referenced inside the tools, just ignore the base for now.
        bool found = false;
        for (auto tool : tools) {
            if (tool == base) {
                found = true;
                break;
            }
            if (!tool->isDerivedFrom<Part::SubShapeBinder>())
                continue;
            auto binder = static_cast<PartDesign::SubShapeBinder*>(tool);
            for (auto & link : binder->Support.getSubListValues()) {
                auto linked = link.getValue();
                if (!linked)
                    continue;
                if (base == linked) {
                    found = true;
                    break;
                }
                for (auto & sub : link.getSubValues()) {
                    auto sobj = linked->getSubObject(sub.c_str());
                    if (sobj == base) {
                        found = true;
                        break;
                    }
                }
                if (found)
                    break;
            }
            if (found)
                break;
        }
        if (!found)
            baseShape = getBaseShape();
    }

    // If not base shape, use the first tool shape as base
    if(baseShape.isNull()) {
        if (tools.empty())
            return new App::DocumentObjectExecReturn(QT_TRANSLATE_NOOP("Exception", "No tool objects"));

        App::DocumentObject *feature;
        if (type == "Cut") {
            feature = tools.front();
            ++itBegin;
        }
        else {
            feature = tools.back();
            if (tools.size() > 1)
                --itEnd;
            else
                ++itBegin;
        }
        baseShape = getTopoShape(feature);
        if (baseShape.isNull()) {
            return new App::DocumentObjectExecReturn(QT_TRANSLATE_NOOP("Exception",
                    "Cannot do boolean operation with invalid base shape"));
        }
    }
        
    std::vector<TopoShape> shapes;
    shapes.push_back(baseShape);
    for(auto it=itBegin; it<itEnd; ++it) {
        auto shape = getTopoShape(*it);
        if (shape.isNull())
            return new App::DocumentObjectExecReturn(QT_TRANSLATE_NOOP("Exception","Tool shape is null"));
        shapes.push_back(shape);
    }

    TopoShape result(0,getDocument()->getStringHasher());
    if (shapes.size() == 1) {
        if (shapes.front().getPlacement().isIdentity()) {
            this->Shape.setValue(shapes.front());
        }
        else {
            // use compound to contain the placement
            this->Shape.setValue(result.makECompound(shapes));
        }
        return App::DocumentObject::StdReturn;
    }

    const char *op = nullptr;
    if (type == "Fuse")
        op = Part::OpCodes::Fuse;
    else if(type == "Cut")
        op = Part::OpCodes::Cut;
    else if(type == "Common")
        op = Part::OpCodes::Common;
    else if(type == "Compound")
        op = Part::OpCodes::Compound;
    else if(type == "Section")
        op = Part::OpCodes::Section;
    else
        return new App::DocumentObjectExecReturn(QT_TRANSLATE_NOOP("Exception", "Unsupported boolean operation"));

    try {
        result.makEBoolean(op, shapes);
    } catch (Standard_Failure &e) {
        FC_ERR("Boolean operation failed: " << e.GetMessageString());
        return new App::DocumentObjectExecReturn(QT_TRANSLATE_NOOP("Exception", "Boolean operation failed"));
    }
    result = this->getSolid(result, false);
    // lets check if the result is a solid
    if (result.isNull())
        return new App::DocumentObjectExecReturn(QT_TRANSLATE_NOOP("Exception", "Resulting shape is null"));

    if (this->Refine.getValue())
        result = result.makERefine();

    this->Shape.setValue(result);
    return App::DocumentObject::StdReturn;
}

void Boolean::onChanged(const App::Property* prop) {

    if(strcmp(prop->getName(), "Group") == 0)
        touch();

    PartDesign::Feature::onChanged(prop);
}

void Boolean::handleChangedPropertyName(Base::XMLReader &reader, const char * TypeName, const char *PropName)
{
    // The App::PropertyLinkList property was Bodies in the past
    Base::Type type = Base::Type::fromName(TypeName);
    if (Group.getClassTypeId() == type && strcmp(PropName, "Bodies") == 0) {
        Group.Restore(reader);
    }
}

}

void Boolean::onNewSolidChanged()
{
    App::DocumentObject * base = getBaseObject(true);
    if (base && NewSolid.getValue()) {
        // Here means this feature just changed to `NewSolid`, add the current
        // base object to tools before it is nullified.
        auto findBase = [base](App::DocumentObject *tool) -> bool {
            if (tool == base)
                return true;
            if (auto binder = Base::freecad_dynamic_cast<PartDesign::SubShapeBinder>(tool)) {
                for (auto & link : binder->Support.getSubListValues()) {
                    auto linked = link.getValue();
                    if (!linked)
                        continue;
                    if (base == linked)
                        return true;
                    for (auto & sub : link.getSubValues()) {
                        auto sobj = linked->getSubObject(sub.c_str());
                        if (sobj == base)
                            return true;
                    }
                }
            }
            return false;
        };
        bool found = false;
        for (auto tool : Group.getValues()) {
            if ((found = findBase(tool)))
                break;
        }
        if (!found) {
            auto binder = static_cast<PartDesign::SubShapeBinder*>(
                    getDocument()->addObject("PartDesign::SubShapeBinder", "Reference"));
            auto grp = Group.getValues();
            binder->Support.setValue(base);
            std::string label = std::string(binder->getNameInDocument()) + "(" + base->Label.getValue() + ")";
            binder->Label.setValue(label.c_str());
            grp.insert(grp.begin(), binder);
            Group.setValue(grp);
        }
    }
    inherited::onNewSolidChanged();
}

void Boolean::unsetupObject() {
    std::vector<App::DocumentObjectT> objsT;
    for (auto obj : Group.getValues()) {
        if (!obj->isRemoving() && obj->getInList().size() <= 1) {
            objsT.emplace_back(obj);
        }
    }
    for (const auto &objT : objsT) {
        if (auto obj = objT.getObject()) {
            obj->getDocument()->removeObject(obj->getNameInDocument());
        }
    }
}
