/***************************************************************************
 *   Copyright (c) 2011 Juergen Riegel <FreeCAD@juergen-riegel.net>        *
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
# include <BRep_Tool.hxx>
# include <BRepBuilderAPI_MakeFace.hxx>
# include <gp_Pln.hxx>
# include <gp_Pnt.hxx>
# include <Standard_Failure.hxx>
# include <TopExp_Explorer.hxx>
# include <TopoDS.hxx>
#endif

#include <App/Document.h>
#include <App/DocumentObject.h>
#include <App/FeaturePythonPyImp.h>
#include <Mod/Part/App/PartParams.h>
#include <App/ElementNamingUtils.h>
#include "App/OriginFeature.h"
#include <Base/Console.h>

#include "Feature.h"
#include "FeaturePy.h"
#include "Body.h"
#include "ShapeBinder.h"

FC_LOG_LEVEL_INIT("PartDesign", true, true)


namespace PartDesign {


PROPERTY_SOURCE(PartDesign::Feature,Part::Feature)

Feature::Feature()
{
    ADD_PROPERTY(BaseFeature,(nullptr));
    ADD_PROPERTY_TYPE(_Body,(nullptr),"Base",(App::PropertyType)(
                App::Prop_ReadOnly|App::Prop_Hidden|App::Prop_Output|App::Prop_Transient),0);
    ADD_PROPERTY_TYPE(Suppress,(false),"Base",App::Prop_None, "Suppress the current feature");
    ADD_PROPERTY(SuppressedShape,(TopoShape()));

    ADD_PROPERTY_TYPE(_Siblings,(0),"Base",(App::PropertyType)(
                App::Prop_ReadOnly|App::Prop_Hidden|App::Prop_Output),0);

    Placement.setStatus(App::Property::Hidden, true);
    BaseFeature.setStatus(App::Property::Hidden, true);

    ValidateShape.setValue(Part::PartParams::getAutoValidateShape());

    ADD_PROPERTY_TYPE(NewSolid,(false),"Base",App::Prop_None,
                     "Create a new separated solid from this feature");
}

short Feature::mustExecute() const
{
    if (BaseFeature.isTouched())
        return 1;
    return Part::Feature::mustExecute();
}

bool Feature::allowMultiSolid() const
{
    auto body = getFeatureBody();
    return body && !body->SingleSolid.getValue();
}

TopoShape Feature::getSolid(const TopoShape& shape, bool force)
{
    if (shape.isNull())
        throw Part::NullShapeException("Null shape");
    int count = shape.countSubShapes(TopAbs_SOLID);
    if(count>1) {
        if(allowMultiSolid()) {
            auto res = shape;
            res.fixSolidOrientation();
            return res;
        }
        throw Base::RuntimeError("Result has multiple solids.\n"
                "To allow multiple solids, please set 'SingleSolid' property of the body to false");
    }
    if(count) {
        auto res = shape.getSubTopoShape(TopAbs_SOLID,1);
        res.fixSolidOrientation();
        return res;
    }
    if (force)
        return TopoShape();
    return shape;
}

void Feature::onChanged(const App::Property *prop)
{
    if (!this->isRestoring() 
            && this->getDocument()
            && !this->getDocument()->isPerformingTransaction()) {
        if (prop == &NewSolid) 
            onNewSolidChanged();
        else if (prop == &Visibility && Visibility.getValue()) {
            if (auto body = Body::findBodyOf(this)) {
                auto siblings = body->getSiblings(this);
                for (auto feat : siblings) {
                    if (feat != this && feat->Visibility.getValue())
                        feat->Visibility.setValue(false);
                }
                body->signalSiblingVisibilityChanged(siblings);
            }
        }
    }
    Part::Feature::onChanged(prop);
}

void Feature::onNewSolidChanged()
{
    if (!NewSolid.getValue() && !BaseFeature.getValue()) {
        auto body = getFeatureBody();
        if (body) {
            auto prev = body->getPrevSolidFeature(this);
            if (prev)
                BaseFeature.setValue(prev);
        }
    }
    else if(NewSolid.getValue() && BaseFeature.getValue())
        BaseFeature.setValue(nullptr);
}

const gp_Pnt Feature::getPointFromFace(const TopoDS_Face& f)
{
    if (!f.Infinite()) {
        TopExp_Explorer exp;
        exp.Init(f, TopAbs_VERTEX);
        if (exp.More())
            return BRep_Tool::Pnt(TopoDS::Vertex(exp.Current()));
        // Else try the other method
    }

    // TODO: Other method, e.g. intersect X,Y,Z axis with the (unlimited?) face?
    // Or get a "corner" point if the face is limited?
    THROWM(Base::NotImplementedError, "getPointFromFace(): Not implemented yet for this case")
}

Part::Feature* Feature::getBaseObject(bool silent) const {
    App::DocumentObject* BaseLink = BaseFeature.getValue();
    Part::Feature* BaseObject = nullptr;
    const char *err = nullptr;

    if (BaseLink) {
        if (BaseLink->isDerivedFrom<Part::Feature>()) {
            BaseObject = static_cast<Part::Feature*>(BaseLink);
        }
        if (!BaseObject) {
            err =  "No base feature linked";
        }
    } else {
        err = "Base property not set";
    }

    // If the function not in silent mode throw the exception describing the error
    if (!silent && err) {
        THROWM(Base::RuntimeError, err)
    }

    return BaseObject;
}

TopoShape Feature::getBaseShape(bool silent, bool force, bool checkSolid) const {
    Part::TopoShape result;

    if (NewSolid.getValue() && !force) {
        if (silent)
            return result;
        THROWM(Base::ValueError, "No need base shape when creating new solid")
    }

    const Part::Feature* BaseObject = getBaseObject(silent);
    if (!BaseObject)
        return result;

    if(BaseObject != BaseFeature.getValue()) {
        // A feature with no body is legal: files from 0.15 and before made
        // them, and a dress-up of a plain Part object is one (upstream
        // a76adf48a9). Only the shape binder rule needs a body to ask.
        auto body = getFeatureBody();
        if ((!body || body->BaseFeature.getValue() != BaseObject)
                && (BaseObject->isDerivedFrom<PartDesign::ShapeBinder>()
                    || BaseObject->isDerivedFrom<Part::SubShapeBinder>()))
        {
            if(silent)
                return result;
            THROWM(Base::ValueError, "Base shape of shape binder cannot be used")
        }
    }

    result = BaseObject->Shape.getShape();

    if (result.isNull()) {
        if (!silent)
            throw Part::NullShapeException("Base feature's TopoShape is invalid");
    }

    if (checkSolid && !result.hasSubShape(TopAbs_SOLID)) {
        if (silent)
            result = TopoShape();
        else
            THROWM(Base::ValueError, "Base feature's shape is not a solid")
    }
    return result;
}

const TopoDS_Shape& Feature::getBaseShapeOld() const {
    const Part::Feature* BaseObject = getBaseObject();

    if(BaseObject != BaseFeature.getValue()) {
        if (BaseObject->isDerivedFrom<PartDesign::ShapeBinder>() ||
            BaseObject->isDerivedFrom<Part::SubShapeBinder>())
        {
            THROWM(Base::ValueError, "Base shape of shape binder cannot be used")
        }
    }

    const TopoDS_Shape& result = BaseObject->Shape.getValue();
    if (result.IsNull())
        throw Part::NullShapeException("Base feature's shape is invalid");
    TopExp_Explorer xp (result, TopAbs_SOLID);
    if (!xp.More())
        THROWM(Base::ValueError, "Base feature's shape is not a solid")

    return result;
}


PyObject* Feature::getPyObject()
{
    if (PythonObject.is(Py::_None())){
        // ref counter is set to 1
        PythonObject = Py::Object(new FeaturePy(this),true);
    }
    return Py::new_reference_to(PythonObject);
}

bool Feature::isDatum(const App::DocumentObject* feature)
{
    return feature->isDerivedFrom<App::OriginFeature>() ||
           feature->isDerivedFrom<Part::Datum>();
}

gp_Pln Feature::makePlnFromPlane(const App::DocumentObject* obj)
{
    if (!obj || !obj->getNameInDocument())
        THROWM(Base::ValueError, "Feature: Null object")
    // A plane of a coordinate system: its own Placement is relative to the
    // LCS, so a plane of a moved or turned LCS read as the global one --
    // an up-to-face or a neutral plane landed at the origin (upstream
    // 194ec0820c). getBasePoint()/getDirection() carry the LCS.
    if (auto datum = dynamic_cast<const App::DatumElement*>(obj)) {
        Base::Vector3d pos = datum->getBasePoint();
        Base::Vector3d normal = datum->getDirection();
        return gp_Pln(gp_Pnt(pos.x, pos.y, pos.z), gp_Dir(normal.x, normal.y, normal.z));
    }
    auto propPlacement = Base::freecad_dynamic_cast<App::PropertyPlacement>(obj->getPropertyByName("Placement"));
    if (!propPlacement)
        THROWM(Base::ValueError, "Feature: no placement found")

    Base::Vector3d pos = propPlacement->getValue().getPosition();
    Base::Rotation rot = propPlacement->getValue().getRotation();
    Base::Vector3d normal(0,0,1);
    rot.multVec(normal, normal);
    return gp_Pln(gp_Pnt(pos.x,pos.y,pos.z), gp_Dir(normal.x,normal.y,normal.z));
}

TopoShape Feature::makeShapeFromPlane(const App::DocumentObject* obj)
{
    BRepBuilderAPI_MakeFace builder(makePlnFromPlane(obj));
    if (!builder.IsDone())
        THROWM(Base::CADKernelError, "Feature: Could not create shape from base plane")

    return TopoShape(obj->getID(), nullptr, builder.Shape());
}

Body* Feature::getFeatureBody() const {

    auto body = Base::freecad_dynamic_cast<Body>(_Body.getValue());
    if(body) {
        if (body->Group.find(getNameInDocument()) != this)
            return nullptr;
        return body;
    }

    for (auto in : this->getInList()) {
        if (auto body = Base::freecad_dynamic_cast<Body>(in)) {
            if (body->Group.find(getNameInDocument())) {
                if (!_Body.getValue())
                    _Body.setValue(body);
                return body;
            }
        }
    }

    return nullptr;
}

void Feature::getGeneratedIndices(std::vector<int> &faces,
                                  std::vector<int> &edges,
                                  std::vector<int> &vertices) const
{
    Part::TopoShape shape = Shape.getShape();
    std::set<int> edgeSet;
    std::set<int> vertexSet;
    unsigned count = shape.countSubShapes(TopAbs_FACE);
    for(unsigned i=1; i<=count; ++i) {
        Data::MappedName mapped = shape.getMappedName(
                Data::IndexedName::fromConst("Face", i));
        if(mapped && isElementGenerated(shape, mapped)) {
            faces.push_back(i-1);
            Part::TopoShape face = shape.getSubTopoShape(TopAbs_FACE, i);
            for(auto &s : face.getSubShapes(TopAbs_EDGE)) {
                int idx = shape.findShape(s)-1;
                if(idx >= 0 && edgeSet.insert(idx).second)
                    edges.push_back(idx);
            }
            for(auto &s : face.getSubShapes(TopAbs_VERTEX)) {
                int idx = shape.findShape(s)-1;
                if(idx >= 0 && vertexSet.insert(idx).second)
                    vertices.push_back(idx);
            }
        }
    }
}

bool Feature::isElementGenerated(const TopoShape &shape, const Data::MappedName &name) const
{
    return shape.isElementGenerated(name);
}

App::DocumentObjectExecReturn *Feature::recompute(void)
{
    SuppressedShape.setValue(TopoShape());

    if(!Suppress.getValue())
        return Part::Feature::recompute();

    bool failed = false;
    try {
        std::unique_ptr<App::DocumentObjectExecReturn> ret(Part::Feature::recompute());
        if(ret)
            THROWM(Base::RuntimeError, ret->Why)
    } catch (Base::AbortException &) {
        throw;
    } catch (Base::Exception &e) {
        failed = true;
        e.ReportException();
        FC_ERR("Failed to recompute suppressed feature " << getFullName());
    }

    if(!failed)
        updateSuppressedShape();
    else
        Shape.setValue(getPlacedBaseShape());
    return  App::DocumentObject::StdReturn;
}

void Feature::onBaseFeatureRerouted(App::DocumentObject*, App::DocumentObject*)
{
}

bool Feature::relinkToMatchingSubElements(App::PropertyLinkSub& link,
                                          App::DocumentObject* oldBase,
                                          App::DocumentObject* newBase)
{
    if (!oldBase || !newBase || link.getValue() != oldBase)
        return false;
    auto oldFeature = Base::freecad_dynamic_cast<Part::Feature>(oldBase);
    auto newFeature = Base::freecad_dynamic_cast<Part::Feature>(newBase);
    if (!oldFeature || !newFeature)
        return false;
    const TopoShape& oldShape = oldFeature->Shape.getShape();
    const TopoShape& newShape = newFeature->Shape.getShape();
    if (oldShape.isNull() || newShape.isNull())
        return false;

    // The mapped names belong to the old base's element map, so look each
    // element up there and find its geometry in the new base.
    const auto& shadows = link.getShadowSubs();
    if (shadows.size() != link.getSubValues().size())
        return false;
    std::vector<std::string> subs;
    for (const auto& shadow : shadows) {
        const auto& ref = shadow.first.empty() ? shadow.second : shadow.first;
        if (ref.empty()) {
            subs.emplace_back();
            continue;
        }
        TopoShape sub = oldShape.getSubTopoShape(ref.c_str(), /*silent*/true);
        if (sub.isNull())
            return false;
        std::vector<std::string> names;
        auto found = newShape.searchSubShape(sub, &names);
        if (found.size() != 1 || names.size() != 1)
            return false;
        subs.push_back(std::move(names.front()));
    }
    link.setValue(newBase, std::move(subs));
    return true;
}

TopoShape Feature::wrapLocated(const TopoShape& shape) const
{
    if (shape.isNull() || shape.getShape().Location().IsIdentity())
        return shape;
    TopoShape res(0, getDocument()->getStringHasher());
    res.makECompound({shape});
    return res;
}

TopoShape Feature::getPlacedBaseShape() const
{
    // Setting the base shape as it is would hand this feature the base's
    // Placement, and a suppressed primitive would come back misplaced.
    TopoShape shape = getBaseShape(true);
    if (shape.isNull())
        return shape;
    shape.move(getLocation().Inverted());
    shape = wrapLocated(shape);
    shape.setPlacement(Placement.getValue());
    return shape;
}

void Feature::updateSuppressedShape()
{
    auto baseShape = getPlacedBaseShape();
    TopoShape res(getID());
    TopoShape shape = Shape.getShape();
    shape.setPlacement(Base::Placement());
    std::vector<TopoShape> generated;
    if(!shape.isNull()) {
        unsigned count = shape.countSubShapes(TopAbs_FACE);
        for(unsigned i=1; i<=count; ++i) {
            Data::MappedName mapped = shape.getMappedName(
                    Data::IndexedName::fromConst("Face", i));
            if(mapped && isElementGenerated(shape, mapped))
                generated.push_back(shape.getSubTopoShape(TopAbs_FACE, i));
        }
    }
    if(!generated.empty()) {
        res.makECompound(generated);
        res.setPlacement(Placement.getValue());
    }
    Shape.setValue(baseShape);
    SuppressedShape.setValue(res);
}

App::DocumentObject *Feature::getSubObject(const char *subname, 
        PyObject **pyObj, Base::Matrix4D *pmat, bool transform, int depth) const
{
    if (subname && subname != Data::findElementName(subname)) {
        const char * dot = strchr(subname,'.');
        if (dot) {
            auto body = PartDesign::Body::findBodyOf(this);
            if (body) {
                auto feat = body->Group.find(std::string(subname, dot));
                if (feat) {
                    Base::Matrix4D _mat;
                    if (!transform) {
                        // Normally the parent object is supposed to transform
                        // the sub-object using its own placement. So, if no
                        // transform is requested, (i.e. no parent
                        // transformation), we just need to NOT apply the
                        // transformation.
                        //
                        // But PartDesign features (including sketch) are
                        // supposed to be contained inside a body. It makes
                        // little sense to transform its sub-object. So if 'no
                        // transform' is requested, we need to actively apply
                        // an inverse transform.
                        _mat = Placement.getValue().inverse().toMatrix();
                        if (pmat)
                            *pmat *= _mat; 
                        else
                            pmat = &_mat;
                    }
                    return feat->getSubObject(dot+1, pyObj, pmat, true, depth+1);
                }
            }
        }
    }
    return Part::Feature::getSubObject(subname, pyObj, pmat, transform, depth);
}


}//namespace PartDesign

namespace App {
/// @cond DOXERR
PROPERTY_SOURCE_TEMPLATE(PartDesign::FeaturePython, PartDesign::Feature)
template<> const char* PartDesign::FeaturePython::getViewProviderName() const {
    return "PartDesignGui::ViewProviderPython";
}
template<> PyObject* PartDesign::FeaturePython::getPyObject() {
    if (PythonObject.is(Py::_None())) {
        // ref counter is set to 1
        PythonObject = Py::Object(new FeaturePythonPyT<PartDesign::FeaturePy>(this),true);
    }
    return Py::new_reference_to(PythonObject);
}
/// @endcond

// explicit template instantiation
template class PartDesignExport FeaturePythonT<PartDesign::Feature>;
}

