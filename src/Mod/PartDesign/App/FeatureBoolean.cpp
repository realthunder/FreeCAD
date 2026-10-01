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

#include <algorithm>
#include <set>

#include <App/Application.h>
#include <App/Document.h>
#include <App/GeoFeature.h>
#include <Base/Tools.h>
#include <Mod/Part/App/SubShapeBinder.h>
#include <Mod/Part/App/TopoShapeOpCode.h>
#include <Mod/PartDesign/App/ShapeBinder.h>

#include "FeatureAddSub.h"
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
    ADD_PROPERTY_TYPE(FuzzyTolerance, (0.0), "Part Design", App::Prop_None,
                      FeatureAddSub::fuzzyToleranceDoc);
    FuzzyTolerance.setConstraints(&FeatureAddSub::fuzzyToleranceRange);
    ADD_PROPERTY_TYPE(ToolShape, (TopoShape()), "Part Design",
                      App::PropertyType(App::Prop_Output | App::Prop_Transient | App::Prop_Hidden),
                      "The tool shapes the edit preview draws");
    ADD_PROPERTY_TYPE(UseLegacyBodyPlacement, (App::GetApplication().isRestoring()),
                      "Compatibility", App::Prop_Hidden,
                      "The Boolean owns every tool and takes its shape as it is, as\n"
                      "Booleans made before tools could be references did");
    initExtension(this);
    // A tool may be a reference, in another body or Part (upstream 9c7a761589)
    Group.setScope(App::LinkScope::Global);
}

namespace {

// The placement of an object in the document: its own and its groups'. Not
// GeoFeature::getGlobalPlacement(), whose group placement refuses to work
// for a group that is recomputing, as the Boolean's body may be.
Base::Placement placementInDocument(const App::DocumentObject *obj)
{
    Base::Placement plc = App::GeoFeature::getPlacementFromProp(
            const_cast<App::DocumentObject*>(obj), "Placement");
    std::set<const App::DocumentObject*> visited {obj};
    for (auto group = App::GeoFeatureGroupExtension::getGroupOfObject(obj);
            group && visited.insert(group).second;
            group = App::GeoFeatureGroupExtension::getGroupOfObject(group))
        plc = App::GeoFeature::getPlacementFromProp(group, "Placement") * plc;
    return plc;
}

} // namespace

bool Boolean::ownsTool(const App::DocumentObject *tool) const
{
    return UseLegacyBodyPlacement.getValue()
        || (tool && tool->isDerivedFrom<Part::SubShapeBinder>());
}

bool Boolean::hasObject(const App::DocumentObject* obj, bool recursive) const
{
    if (!ownsTool(obj))
        return false;
    return App::GeoFeatureGroupExtension::hasObject(obj, recursive);
}

std::vector<App::DocumentObject*> Boolean::addObjects(std::vector<App::DocumentObject*> objects)
{
    // The owned ones the group's way, moved in from where they were; a
    // reference only joins the list
    std::vector<App::DocumentObject*> owned, refs;
    for (auto obj : objects) {
        if (obj)
            (ownsTool(obj) ? owned : refs).push_back(obj);
    }
    auto added = App::GeoFeatureGroupExtension::addObjects(owned);
    auto tools = Group.getValues();
    for (auto obj : refs) {
        if (obj == this || std::find(tools.begin(), tools.end(), obj) != tools.end())
            continue;
        tools.push_back(obj);
        added.push_back(obj);
    }
    if (tools.size() != Group.getSize()) {
        Base::ObjectStatusLocker<App::Property::Status, App::Property> guard(
                App::Property::User3, &Group);
        Group.setValues(tools);
    }
    return added;
}

std::vector<App::DocumentObject*> Boolean::setObjects(std::vector<App::DocumentObject*> objects)
{
    removeObjects(Group.getValues());
    return addObjects(objects);
}

TopoShape Boolean::getToolShape(const App::DocumentObject *tool) const
{
    auto shape = getTopoShape(tool);
    if (ownsTool(tool) || shape.isNull())
        return shape;
    // A reference: a feature of this body is in the body's frame already;
    // anything else is where its own group puts it, brought into the body's
    auto body = getFeatureBody();
    if (!body || Body::findBodyOf(tool) == body)
        return shape;
    Base::Placement toBody = placementInDocument(body).inverse();
    if (auto group = App::GeoFeatureGroupExtension::getGroupOfObject(tool))
        toBody *= placementInDocument(group);
    if (!toBody.isIdentity())
        shape.transformShape(toBody.toMatrix(), false, true);
    return shape;
}

short Boolean::mustExecute() const
{
    if (Group.isTouched() || UseLegacyBodyPlacement.isTouched())
        return 1;
    return PartDesign::Feature::mustExecute();
}

App::DocumentObjectExecReturn *Boolean::collectOperands(std::vector<TopoShape> &shapes,
                                                        bool &hasBase) const
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
        if (!found) {
            baseShape = getBaseShape();
            hasBase = !baseShape.isNull();
        }
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
        baseShape = getToolShape(feature);
        if (baseShape.isNull()) {
            return new App::DocumentObjectExecReturn(QT_TRANSLATE_NOOP("Exception",
                    "Cannot do boolean operation with invalid base shape"));
        }
    }

    shapes.push_back(baseShape);
    for(auto it=itBegin; it<itEnd; ++it) {
        auto shape = getToolShape(*it);
        if (shape.isNull())
            return new App::DocumentObjectExecReturn(QT_TRANSLATE_NOOP("Exception","Tool shape is null"));
        shapes.push_back(shape);
    }
    return nullptr;
}

void Boolean::updateToolShape(const std::vector<TopoShape> &shapes, bool hasBase)
{
    // What the base is combined with; with no base feature, the first or
    // last tool stands in for it and is drawn as a tool too
    std::vector<TopoShape> tools(shapes.begin() + (hasBase ? 1 : 0), shapes.end());
    if (tools.empty())
        ToolShape.setValue(TopoShape());
    else
        ToolShape.setValue(TopoShape().makECompound(tools));
}

void Boolean::setPauseRecompute(bool enable)
{
    inherited::setPauseRecompute(enable);
    // ToolShape is not saved, so a Boolean first edited after a load has none
    if (enable && ToolShape.getShape().isNull()) {
        std::vector<TopoShape> shapes;
        bool hasBase = false;
        std::unique_ptr<App::DocumentObjectExecReturn> ret(collectOperands(shapes, hasBase));
        if (!ret)
            updateToolShape(shapes, hasBase);
    }
}

App::DocumentObjectExecReturn *Boolean::execute()
{
    std::vector<TopoShape> shapes;
    bool hasBase = false;
    if (auto ret = collectOperands(shapes, hasBase)) {
        ToolShape.setValue(TopoShape());
        return ret;
    }
    updateToolShape(shapes, hasBase);

    // Edited with a preview: the base and the tools are drawn as they are
    // and the boolean waits for the panel to close
    if (isRecomputePaused())
        return App::DocumentObject::StdReturn;

    std::string type = Type.getValueAsString();
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
        result.makEBoolean(op, shapes, nullptr, FuzzyTolerance.getValue());
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
        // a reference is not the Boolean's to delete
        if (!ownsTool(obj))
            continue;
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
