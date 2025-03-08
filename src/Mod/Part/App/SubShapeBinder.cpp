/***************************************************************************
 *   Copyright (c) 2021 Zheng Lei (realthunder) <realthunder.dev@gmail.com>*
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
# include <cfloat>
# include <gp_Lin.hxx>
# include <gp_Pln.hxx>
# include <BRep_Builder.hxx>
# include <BRepBuilderAPI_MakeEdge.hxx>
# include <BRepBuilderAPI_MakeFace.hxx>
# include <BRepBuilderAPI_MakeWire.hxx>
# include <BRep_Tool.hxx>
# include <TopExp_Explorer.hxx>
# include <TopoDS.hxx>
# include <Precision.hxx>
#endif

#include <unordered_map>
#include <unordered_set>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/algorithm/string/find.hpp>
#include <boost/range.hpp>
#include <boost/iostreams/device/array.hpp>
#include <boost/iostreams/stream.hpp>

typedef boost::iterator_range<const char*> CharRange;

#include <Base/Console.h>
#include <Base/Tools.h>
#include <App/Application.h>
#include <App/Document.h>
#include <App/Document.h>
#include <App/GeoFeatureGroupExtension.h>
#include <App/OriginFeature.h>
#include <App/Link.h>
#include <App/MappedElement.h>
#include <App/Part.h>
#include <App/Origin.h>
#include <Mod/Part/App/TopoShape.h>
#include <Mod/Part/App/TopoShapeOpCode.h>
#include <Mod/Part/App/FaceMakerBullseye.h>
#include "SubShapeBinder.h"
#include "BodyBase.h"
#include "PartParams.h"
#include "WireJoiner.h"

FC_LOG_LEVEL_INIT("Part",true,true)

#ifndef M_PI
#define M_PI       3.14159265358979323846
#endif

using namespace Part;
namespace bp = boost::placeholders;
namespace bio = boost::iostreams;

// ============================================================================

PROPERTY_SOURCE(Part::SubShapeBinder, Part::Feature)

SubShapeBinder::SubShapeBinder()
{
    ADD_PROPERTY_TYPE(Support, (0), "",(App::PropertyType)(App::Prop_None), "Support of the geometry");
    // Support.setStatus(App::Property::ReadOnly, true);
    ADD_PROPERTY_TYPE(Fuse, (false), "Base",App::Prop_None,"Fuse solids from bound shapes");
    ADD_PROPERTY_TYPE(Refine, (true),"Base",(App::PropertyType)(App::Prop_None),"Refine shape (clean up redundant edges)");
    ADD_PROPERTY_TYPE(MakeFace, (true), "Base",App::Prop_None,"Create face using wires from bound shapes");
    ADD_PROPERTY_TYPE(FaceMaker, (""), "Base",App::Prop_None,"Face maker class name. Default to Part::FaceMakerBullseye.");
    ADD_PROPERTY_TYPE(FillStyle, (static_cast<long>(0)), "Base",App::Prop_None,
            "Face filling type when making curved face.\n\n"
            "Stretch: the style with the flattest patches.\n"
            "Coons: a rounded style of patch with less depth than those of Curved.\n"
            "Curved: the style with the most rounded patches.");
    static const char *FillStyleEnum[] = {"Stretch", "Coons", "Curved", 0};
    FillStyle.setEnums(FillStyleEnum);

    ADD_PROPERTY_TYPE(Offset, (0.0), "Offsetting", App::Prop_None, "2D offset face or wires, 0.0 = no offset");
    ADD_PROPERTY_TYPE(OffsetJoinType, ((long)0), "Offsetting", App::Prop_None, "Arcs, Tangent, Intersection");
    static const char* JoinTypeEnum[] = { "Arcs", "Tangent", "Intersection", nullptr };
    OffsetJoinType.setEnums(JoinTypeEnum);
    ADD_PROPERTY_TYPE(OffsetFill, (false), "Offsetting", App::Prop_None, "True = make face between original wire and offset.");
    ADD_PROPERTY_TYPE(OffsetOpenResult, (false), "Offsetting", App::Prop_None, "False = make closed offset from open wire.");
    ADD_PROPERTY_TYPE(OffsetIntersection, (false), "Offsetting", App::Prop_None, "False = offset child wires independently.");
    ADD_PROPERTY_TYPE(ClaimChildren, (false), "Base",App::Prop_Output,"Claim linked object as children");
    ADD_PROPERTY_TYPE(Relative, (true), "Base",App::Prop_None,"Enable relative sub-object binding");
    ADD_PROPERTY_TYPE(BindMode, ((long)BindModeEnum::Synchronized), "Base", App::Prop_None, 
            "Synchronized: auto update binder shape on changed of bound object.\n"
            "Frozen: disable auto update, but can be updated manually using context menu.\n"
            "Detached: copy the shape of bound object and then remove the binding immediately.\n"
            "Float: same as 'Synchronized' but with independent placement to the bound object.\n"
            "FloatFirst: same as 'Float' but with independent placement to the first bound object.\n"
            "            The rest bound objects (if any) will maintain the same relative placement\n"
            "            to the first bound object.");
    ADD_PROPERTY_TYPE(PartialLoad, (false), "Base", App::Prop_None,
            "Enable partial loading, which disables auto loading of external document\n"
            "of the external bound object.");
    PartialLoad.setStatus(App::Property::PartialTrigger,true);
    static const char *BindModeEnum[] = {"Synchronized", "Frozen", "Detached", "Float", "FloatFirst", 0};
    BindMode.setEnums(BindModeEnum);

    static const char *BindCopyOnChangeEnum[] = {"Disabled", "Enabled", "Mutated", 0};
    BindCopyOnChange.setEnums(BindCopyOnChangeEnum);
    ADD_PROPERTY_TYPE(BindCopyOnChange, ((long)0), "Base", App::Prop_None,
            "Disabled: disable copy on change.\n"
            "Enabled: duplicate properties from binding object that are marked with 'CopyOnChange'.\n"
            "         Make internal copy of the object with any changed properties to obtain the\n"
            "         shape of an alternative configuration\n"
            "Mutated: indicate the binder has already mutated by changing any properties marked with\n"
            "         'CopyOnChange'. Those properties will no longer be kept in sync between the\n"
            "         binder and the binding object");

    ADD_PROPERTY_TYPE(Context, (0), "Base", App::Prop_Hidden,
            "Stores the context of this binder. It is used for monitoring and auto updating\n"
            "the relative placement of the bound shape");
    Context.setScope(App::LinkScope::Hidden);
    Context.setSilentRestore(true);

    ADD_PROPERTY_TYPE(_Version,(0),"Base",(App::PropertyType)(
                App::Prop_Hidden|App::Prop_ReadOnly), "");

    _CopiedLink.setScope(App::LinkScope::Hidden);
    ADD_PROPERTY_TYPE(_CopiedLink,(0),"Base",(App::PropertyType)(
                App::Prop_Hidden|App::Prop_ReadOnly|App::Prop_NoPersist), "");

    ADD_PROPERTY_TYPE(SplitEdges, (false), "Wire Construction",  App::Prop_None,
            "Whether to split edge if it intersects with other edges");
    ADD_PROPERTY_TYPE(MergeEdges, (true), "Wire Construction",  App::Prop_None,
            "Merge connected edges without branches into wire before searching for closed wires.");
    ADD_PROPERTY_TYPE(TightBound, (false), "Wire Construction",  App::Prop_None,
            "Create only wires that cannot be further splitted into smaller wires");
    ADD_PROPERTY_TYPE(Outline, (false), "Wire Construction",  App::Prop_None,
            "Whether to remove any edges that are shared by more than one wire, effectively creating an outline.");
    ADD_PROPERTY_TYPE(EdgeTolerance, (1e-6), "Wire Construction",  App::Prop_None,
            "Wire construction tolerance");
}

SubShapeBinder::~SubShapeBinder() {
    clearCopiedObjects();
}

void SubShapeBinder::setupObject() {
    _Version.setValue(8);
    Refine.setValue(PartParams::getRefineModel());
    checkPropertyStatus();
}

void SubShapeBinder::setupCopyOnChange() {
    copyOnChangeConns.clear();

    const auto &support = Support.getSubListValues();
    if(BindCopyOnChange.getValue()==0 || support.size()!=1) {
        if(hasCopyOnChange) {
            hasCopyOnChange = false;
            std::vector<App::Property*> props;
            getPropertyList(props);
            for(auto prop : props) {
                if(App::LinkBaseExtension::isCopyOnChangeProperty(this,*prop)) {
                    try {
                        removeDynamicProperty(prop->getName());
                    } catch (Base::Exception &e) {
                        e.ReportException();
                    } catch (...) {
                    }
                }
            }
        }
        return;
    }

    auto linked = support.front().getValue();
    hasCopyOnChange = App::LinkBaseExtension::setupCopyOnChange(this,linked,
        BindCopyOnChange.getValue()==1?&copyOnChangeConns:nullptr,hasCopyOnChange);
    if(hasCopyOnChange) {
        copyOnChangeConns.push_back(linked->signalChanged.connect(
            [this](const App::DocumentObject &, const App::Property &prop) {
                if(!prop.testStatus(App::Property::Output)
                        && !prop.testStatus(App::Property::PropOutput))
                {
                    if(this->_CopiedObjs.size()) {
                        FC_LOG("Clear binder " << getFullName() << " cache on change of "
                                << prop.getFullName());
                        this->clearCopiedObjects();
                    }
                }
            }
        ));
    }
}

void SubShapeBinder::clearCopiedObjects() {
    std::vector<App::DocumentObjectT> objs;
    objs.swap(_CopiedObjs);
    for(auto &o : objs) {
        auto obj = o.getObject();
        if(obj)
            obj->getDocument()->removeObject(obj->getNameInDocument());
    }
    _CopiedLink.setValue(0);
}

App::DocumentObject *SubShapeBinder::getSubObject(const char *subname, PyObject **pyObj,
        Base::Matrix4D *mat, bool transform, int depth) const
{
    while(subname && *subname=='.') ++subname; // skip leading .
    auto sobj = Part::Feature::getSubObject(subname,pyObj,mat,transform,depth);
    if(sobj)
        return sobj;
    if(Data::ComplexGeoData::findElementName(subname)==subname)
        return nullptr;

    const char *dot = strchr(subname, '.');
    if(!dot)
        return nullptr;

    if (!App::GetApplication().checkLinkDepth(depth))
        return nullptr;

    std::string name(subname,dot-subname);
    for(auto &l : Support.getSubListValues()) {
        auto obj = l.getValue();
        if(!obj || !obj->getNameInDocument())
            continue;
        for(auto &sub : l.getSubValues()) {
            auto sobj = obj->getSubObject(sub.c_str());
            if(!sobj || !sobj->getNameInDocument())
                continue;
            if(subname[0] == '$') {
                if(sobj->Label.getStrValue() != name.c_str()+1)
                    continue;
            } else if(!boost::equals(sobj->getNameInDocument(), name))
                continue;
            name = Data::ComplexGeoData::noElementName(sub.c_str());
            name += dot+1;
            if(mat && transform)
                *mat *= Placement.getValue().toMatrix();
            return obj->getSubObject(name.c_str(),pyObj,mat,true,depth+1);
        }
    }
    return nullptr;
}

void SubShapeBinder::update(SubShapeBinder::UpdateOption options) {
    Part::TopoShape result;
    if(_Version.getValue()>2 && getDocument())
        result = Part::TopoShape(0, getDocument()->getStringHasher());

    std::vector<Part ::TopoShape> shapes;
    std::vector<std::pair<int,int> > shapeOwners;
    std::vector<const Base::Matrix4D*> shapeMats;

    bool forced = (Shape.getValue().IsNull() || (options & UpdateForced)) ? true : false;
    bool init = (!forced && (options & UpdateForced)) ? true : false;

    std::string errMsg;
    auto parent = Context.getValue();
    std::string parentSub  = Context.getSubName(false);
    if(!Relative.getValue()) 
        parent = 0;
    else {
        if(parent && parent->getSubObject(parentSub.c_str())==this) {
            auto parents = parent->getParents();
            if(parents.size()) {
                parent = parents.begin()->first;
                parentSub = parents.begin()->second + parentSub;
            }
        } else
            parent = 0;
        if(!parent && parentSub.empty()) {
            auto parents = getParents();
            if(parents.size()) {
                parent = parents.begin()->first;
                parentSub = parents.begin()->second;
            }
        }
        if(parent && (parent!=Context.getValue() || parentSub!=Context.getSubName(false)))
            Context.setValue(parent,parentSub.c_str());
    }

    bool first = true;
    Base::Matrix4D matFirst;
    std::unordered_map<const App::DocumentObject*, Base::Matrix4D> mats;
    int idx = -1;
    for(auto &l : Support.getSubListValues()) {
        ++idx;
        auto obj = l.getValue();
        if(!obj || !obj->getNameInDocument()) {
            if (isRecomputing())
                FC_THROWM(Base::RuntimeError,"Missing support object");
            continue;
        }
        auto res = mats.emplace(obj,Base::Matrix4D());
        auto link = obj;
        std::string linkSub = l.getSubName();
        if(parent && res.second) {
            std::string resolvedSub = parentSub;
            auto resolved = parent->resolveRelativeLink(resolvedSub,link,linkSub, RelativeLinkOption::TopParent);
            if(!resolved) {
                if(!link) {
                    FC_WARN(getFullName() << " cannot resolve relative link of "
                            << parent->getFullName() << '.' << parentSub
                            << " -> " << obj->getFullName());
                }
            }else{
                Base::Matrix4D mat;
                auto sobj = resolved->getSubObject(resolvedSub.c_str(),0,&mat);
                if (sobj != this) {
                    FC_WARN(getFullName() << " skip invalid parent " << resolved->getFullName() 
                            << '.' << resolvedSub);
                }else if(_Version.getValue()==0) {
                    // For existing legacy SubShapeBinder, we use its Placement
                    // to store the position adjustment of the first Support
                    if(first) {
                        auto pla = Placement.getValue()*Base::Placement(mat).inverse();
                        Placement.setValue(pla);
                        first = false;
                    } else {
                        // The remaining support will cancel the Placement
                        mat.inverseGauss();
                        res.first->second = mat;
                    }
                }else{
                    // For newer SubShapeBinder, the Placement property is free
                    // to use by the user to add additional offset to the
                    // binding object
                    mat.inverseGauss();
                    res.first->second = Placement.getValue().toMatrix()*mat;
                }
            }
        }

        if (res.second && (BindMode.getValue() == BindModeEnum::Float
                            || BindMode.getValue() == BindModeEnum::FloatFirst)) {
            Base::Matrix4D mat;
            if (first || BindMode.getValue() == BindModeEnum::Float) {
                first = false;
                link->getSubObject(linkSub.c_str(),0,&mat);
                Base::Vector3d t;
                Base::Rotation r;
                Base::Vector3d s;
                Base::Rotation so;
                mat.getTransform(t, r, s, so);
                mat.setTransform(t, r, Base::Vector3d(1, 1, 1), Base::Rotation());
                mat.inverse();
                matFirst = mat;
            }
            res.first->second *= matFirst;
        }

        if(init)
            continue;

        App::DocumentObject *copied = 0;

        if(BindCopyOnChange.getValue() == 2 && Support.getSubListValues().size()==1) {
            if(_CopiedObjs.size())
               copied = _CopiedObjs.front().getObject();

            bool recomputeCopy = false;
            int copyerror = 0;
            if(!copied || !copied->isValid()) {
                recomputeCopy = true;
                clearCopiedObjects();

                auto tmpDoc = App::GetApplication().newDocument(
                                "_tmp_binder", 0, false, true);
                auto objs = tmpDoc->copyObject({obj},true,true);
                if(objs.size()) {
                    for(auto it=objs.rbegin(); it!=objs.rend(); ++it)
                        _CopiedObjs.emplace_back(*it);
                    copied = objs.back();
                    // IMPORTANT! must make a recomputation first before any
                    // further change so that we can generate the correct
                    // geometry element map.
                    if (!copied->recomputeFeature(true))
                        copyerror = 1;
                }
            }

            if(copied) {
                if (!copyerror) {
                    std::vector<App::Property*> props;
                    getPropertyList(props);
                    for(auto prop : props) {
                        if(!App::LinkBaseExtension::isCopyOnChangeProperty(this,*prop))
                            continue;
                        auto p = copied->getPropertyByName(prop->getName());
                        if(p && p->getContainer()==copied
                                && p->getTypeId()==prop->getTypeId()
                                && !p->isSame(*prop)) 
                        {
                            recomputeCopy = true;
                            std::unique_ptr<App::Property> pcopy(prop->Copy());
                            p->Paste(*pcopy);
                        }
                    }
                    if(recomputeCopy && !copied->recomputeFeature(true))
                        copyerror = 2;
                }
                obj = copied;
                _CopiedLink.setValue(copied,l.getSubValues(false));
                if (copyerror) {
                    FC_THROWM(Base::RuntimeError,
                            (copyerror == 1 ? "Initial copy failed." : "Copy on change failed.")
                             << " Please check report view for more details.\n"
                             "You can show temporary document to reveal the failed objects using\n"
                             "tree view context menu.");
                }
            }
        }

        const auto &subvals = copied?_CopiedLink.getSubValues():l.getSubValues();
        int sidx = copied?-1:idx;
        int subidx = -1;
        std::set<std::string> subs(subvals.begin(),subvals.end());
        static std::string none("");
        if(subs.empty())
            subs.insert(none);
        else if(subs.size()>1)
            subs.erase(none);
        for(const auto &sub : subs) {
            ++subidx;
            try {
                auto shape = Part::Feature::getTopoShape(obj,sub.c_str(),true);
                if(shape.isNull())
                    throw Part::NullShapeException("Null shape");
                shapes.push_back(shape);
                shapeOwners.emplace_back(sidx, subidx);
                shapeMats.push_back(&res.first->second);
            } catch(Base::Exception &e) {
                if (isRecomputing()) {
                    e.ReportException();
                    FC_ERR(getFullName() << " failed to obtain shape from " 
                            << obj->getFullName() << '.' << sub);
                }
                if(errMsg.empty()) {
                    std::ostringstream ss;
                    ss << "Failed to obtain shape " <<
                        obj->getFullName() << '.' 
                        << Data::ComplexGeoData::oldElementName(sub.c_str());
                    errMsg = ss.str();
                }
            }
        }
    }

    std::string objName;
    // lambda function for generating matrix cache property
    auto cacheName = [this,&objName](const App::DocumentObject *obj) {
        objName = "Cache_";
        objName += obj->getNameInDocument();
        if(obj->getDocument() != getDocument()) {
            objName += "_";
            objName += obj->getDocument()->getName();
        }
        return objName.c_str();
    };

    if(!init) {
        if(errMsg.size()) {
            // Notify user about restore error
            if (!isRecomputing()) {
                if (getDocument())
                    getDocument()->setErrorDescription(this, errMsg.c_str());
                return;
            }
            FC_THROWM(Base::RuntimeError, errMsg);
        }

        // If not forced, only rebuild when there is any change in
        // transformation matrix
        if(!forced) {
            bool hit = true;
            for(auto &v : mats) {
                auto prop = Base::freecad_dynamic_cast<App::PropertyMatrix>(
                        getDynamicPropertyByName(cacheName(v.first)));
                if(!prop || prop->getValue()!=v.second) {
                    hit = false;
                    break;
                }
            }
            if(hit)
                return;
        }

        std::ostringstream ss;
        idx = -1;
        for(auto &shape : shapes) {
            ++idx;
            if(shape.Hasher
                    && shape.getElementMapSize()
                    && shape.Hasher != getDocument()->getStringHasher())
            {
                ss.str("");
                ss << Data::ComplexGeoData::externalTagPostfix()
                   << Data::ComplexGeoData::elementMapPrefix()
                   << Part::OpCodes::Shapebinder << ':' << shapeOwners[idx].first
                   << ':' << shapeOwners[idx].second;
                shape.reTagElementMap(-getID(),
                        getDocument()->getStringHasher(),ss.str().c_str());
            }
            if (!shape.hasSubShape(TopAbs_FACE) && shape.hasSubShape(TopAbs_EDGE))
                shape = shape.makECopy();
        }
        
        if(shapes.size()==1 && !Relative.getValue())
            shapes.back().setPlacement(Base::Placement());
        else {
            for(size_t i=0;i<shapes.size();++i) {
                auto &shape = shapes[i];
                shape = shape.makETransform(*shapeMats[i]);
            }
        }

        if(shapes.empty()) {
            Shape.resetElementMapVersion();
            return;
        }

        result.makECompound(shapes);
        if (needUpdate())
            SupportShape.setValue(TopoShape());
        else
            SupportShape.setValue(result);
        buildShape(result);
    }

    // collect transformation matrix cache entries
    std::unordered_set<std::string> caches;
    for(const auto &name : getDynamicPropertyNames()) {
        if(boost::starts_with(name,"Cache_"))
            caches.emplace(name);
    }

    // update transformation matrix cache
    for(auto &v : mats) {
        const char *name = cacheName(v.first);
        auto prop = getDynamicPropertyByName(name);
        if(!prop || !prop->isDerivedFrom(App::PropertyMatrix::getClassTypeId())) {
            if(prop)
                removeDynamicProperty(name);
            prop = addDynamicProperty("App::PropertyMatrix",name,"Cache",0,0,false,true);
        }
        caches.erase(name);
        static_cast<App::PropertyMatrix*>(prop)->setValue(v.second);
    }

    // remove any non-used cache entries.
    for(const auto &name : caches) {
        try {
            removeDynamicProperty(name.c_str());
        } catch(...) {}
    }
}

void SubShapeBinder::buildShape(TopoShape &result)
{
    bool makeWire = true;
    if (!result.hasSubShape(TopAbs_FACE)
            && result.hasSubShape(TopAbs_EDGE)
            && (SplitEdges.getValue()
                || Outline.getValue()
                || TightBound.getValue()))
    {
        WireJoiner joiner;
        joiner.setTolerance(EdgeTolerance.getValue());
        joiner.setTightBound(TightBound.getValue());
        joiner.setMergeEdges(MergeEdges.getValue());
        joiner.setOutline(Outline.getValue());
        joiner.setSplitEdges(SplitEdges.getValue());
        joiner.addShape(result);
        joiner.getResultWires(result);
        makeWire = false;
        this->fixShape(result);
    }

    if(MakeFace.getValue() && !result.hasSubShape(TopAbs_FACE)) {
        try {
            if (_Version.getValue()>4
                && !result.hasSubShape(TopAbs_EDGE)
                && result.countSubShapes(TopAbs_VERTEX) > 1) {
                std::vector<Part::TopoShape> edges;
                Part::TopoShape first, prev;
                for (auto &vertex : result.getSubTopoShapes(TopAbs_VERTEX)) {
                    if (prev.isNull()) {
                        first = vertex;
                        prev = vertex;
                        continue;
                    }
                    BRepBuilderAPI_MakeEdge builder(
                            BRep_Tool::Pnt(TopoDS::Vertex(prev.getShape())),
                            BRep_Tool::Pnt(TopoDS::Vertex(vertex.getShape())));
                    Part::TopoShape edge(builder.Shape());
                    edge.mapSubElement({prev, vertex});
                    edges.push_back(edge);
                    prev = vertex;
                }
                auto p1 = BRep_Tool::Pnt(TopoDS::Vertex(prev.getShape()));
                auto p2 = BRep_Tool::Pnt(TopoDS::Vertex(first.getShape()));
                if (result.countSubShapes(TopAbs_VERTEX) > 2
                        && p1.SquareDistance(p2) > Precision::SquareConfusion()) {
                    BRepBuilderAPI_MakeEdge builder(p1, p2);
                    Part::TopoShape edge(builder.Shape());
                    edge.mapSubElement({prev, first});
                    edges.push_back(edge);
                }
                result.makEWires(edges);
                this->fixShape(result);
            }
            else if (makeWire && result.hasSubShape(TopAbs_EDGE)) {
                result = result.makEWires();
                this->fixShape(result);
            }
        } catch(Base::Exception & e) {
            FC_LOG(getFullName() << " Failed to make wire: " << e.what());
        } catch(Standard_Failure & e) {
            FC_LOG(getFullName() << " Failed to make wire: " << e.GetMessageString());
        } catch(...) {
            FC_LOG(getFullName() << " Failed to make wire");
        }

        if (result.hasSubShape(TopAbs_WIRE)) {
            bool done = false;
            gp_Pln pln;
            if (_Version.getValue() > 4 && !result.findPlane(pln)) {
                bool closedWire = false;
                for (const auto &wire :result.getSubShapes(TopAbs_WIRE)) {
                    if (BRep_Tool::IsClosed(TopoDS::Wire(wire))) {
                        closedWire = true;
                        break;
                    }
                }
                if (closedWire && result.countSubShapes(TopAbs_WIRE) == 1) {
                    auto wire = result.getSubTopoShape(TopAbs_WIRE, 1);
                    unsigned edgeCount = result.countSubShapes(TopAbs_EDGE);
                    if (edgeCount >= 1 && edgeCount <= 4
                                        && edgeCount == wire.countSubShapes(TopAbs_EDGE)
                                        && result.countSubShapes(TopAbs_VERTEX) == wire.countSubShapes(TopAbs_VERTEX))
                    {
                        try {
                            result = result.makEBSplineFace(
                                    static_cast<Part::TopoShape::FillingStyle>(FillStyle.getValue()));
                            this->fixShape(result);
                            done = true;
                        } catch(Base::Exception & e) {
                            FC_LOG(getFullName() << " Failed to make bspline face: " << e.what());
                        } catch(Standard_Failure & e) {
                            FC_LOG(getFullName() << " Failed to make bspline face: " << e.GetMessageString());
                        } catch(...) {
                            FC_LOG(getFullName() << " Failed to make bspline face");
                        }
                    }
                }
                if (closedWire && !done) {
                    try {
                        Part::TopoShape filledFace(-getID(), getDocument()->getStringHasher());
                        filledFace.makEFilledFace({result}, TopoShape::BRepFillingParams());
                        if (filledFace.hasSubShape(TopAbs_FACE)) {
                            done = true;
                            result = filledFace;
                            this->fixShape(result);
                        }
                    } catch(Base::Exception & e) {
                        FC_LOG(getFullName() << " Failed to make filled face: " << e.what());
                    } catch(Standard_Failure & e) {
                        FC_LOG(getFullName() << " Failed to make filled face: " << e.GetMessageString());
                    } catch(...) {
                        FC_LOG(getFullName() << " Failed to make filled face");
                    }
                }
            }
            if (!done) {
                try {
                    result = result.makEFace(nullptr, FaceMaker.getValue());
                    this->fixShape(result);
                    done = true;
                } catch(Base::Exception & e) {
                    FC_LOG(getFullName() << " Failed to make face: " << e.what());
                } catch(Standard_Failure & e) {
                    FC_LOG(getFullName() << " Failed to make face: " << e.GetMessageString());
                } catch(...) {
                    FC_LOG(getFullName() << " Failed to make face");
                }

                if (!done && _Version.getValue() > 7) {
                    try {
                        result = result.makERuledSurface(result.getSubTopoShapes(TopAbs_WIRE));
                        this->fixShape(result);
                    } catch(Base::Exception & e) {
                        FC_LOG(getFullName() << " Failed to make ruled face: " << e.what());
                    } catch(Standard_Failure & e) {
                        FC_LOG(getFullName() << " Failed to make ruled face: " << e.GetMessageString());
                    } catch(...) {
                        FC_LOG(getFullName() << " Failed to make ruled face");
                    }
                }
            }
        }
    }

    auto doFuse = [&]() {
        if(Fuse.getValue()) {
            if (_Version.getValue() > 4
                    && !result.hasSubShape(TopAbs_SOLID)
                    && result.hasSubShape(TopAbs_FACE))
            {
                TopoShape tmp(result);
                if (!tmp.hasSubShape(TopAbs_SHELL)) {
                    try {
                        tmp = tmp.makEShell();
                        this->fixShape(tmp);
                        if (!tmp.isClosed() || !tmp.isValid())
                            tmp = TopoShape();
                    } catch(Base::Exception & e) {
                        FC_LOG(getFullName() << " Failed to make shell: " << e.what());
                    } catch(Standard_Failure & e) {
                        FC_LOG(getFullName() << " Failed to make shell: " << e.GetMessageString());
                    } catch(...) {
                        FC_LOG(getFullName() << " Failed to make shell");
                    }
                }

                if (tmp.hasSubShape(TopAbs_SHELL) && tmp.isClosed()) {
                    try {
                        tmp = tmp.makESolid();
                        this->fixShape(tmp);
                        if (!tmp.isClosed() || !tmp.isValid())
                            tmp = TopoShape();
                        result = tmp;
                    } catch(Base::Exception & e) {
                        FC_LOG(getFullName() << " Failed to make solid: " << e.what());
                    } catch(Standard_Failure & e) {
                        FC_LOG(getFullName() << " Failed to make solid: " << e.GetMessageString());
                    } catch(...) {
                        FC_LOG(getFullName() << " Failed to make solid");
                    }
                }
            }
            if (result.hasSubShape(TopAbs_SOLID)) {
                // If the compound has solid, fuse them together, and ignore other type of
                // shapes
                auto solids = result.getSubTopoShapes(TopAbs_SOLID);
                if(solids.size() > 1) {
                    try {
                        result.makEFuse(solids);
                        this->fixShape(result);
                    } catch(Base::Exception & e) {
                        FC_LOG(getFullName() << " Failed to fuse: " << e.what());
                    } catch(Standard_Failure & e) {
                        FC_LOG(getFullName() << " Failed to fuse: " << e.GetMessageString());
                    } catch(...) {
                        FC_LOG(getFullName() << " Failed to fuse");
                    }
                } else {
                    // wrap the single solid in compound to keep its placement
                    auto solid = solids.front();
                    result.makECompound({solid});
                }
            }
        } 
        if (Refine.getValue()) {
            result = result.makERefine();
            this->fixShape(result);
        }
    };
    
    doFuse();

    if (fabs(Offset.getValue()) > Precision::Confusion()) {
        try {
            std::vector<TopoShape> results;
            auto makeOffset = [&](const TopoShape &s, bool is2D) {
                if (is2D)
                    results.push_back(s.makEOffset2D(Offset.getValue(),
                                                        static_cast<TopoShape::JoinType>(OffsetJoinType.getValue()),
                                                        OffsetFill.getValue(),
                                                        OffsetOpenResult.getValue(),
                                                        OffsetIntersection.getValue()));
                else
                    results.push_back(s.makEOffset(Offset.getValue(),
                                                    Precision::Confusion(),
                                                    OffsetIntersection.getValue(),
                                                    false,
                                                    OffsetOpenResult.getValue() ? 0 : 1,
                                                    static_cast<TopoShape::JoinType>(OffsetJoinType.getValue()),
                                                    OffsetFill.getValue()));
                this->fixShape(results.back());
            };
            for (const auto &s : result.getSubTopoShapes(TopAbs_SOLID))
                makeOffset(s, false);
            for (const auto &s : result.getSubTopoShapes(TopAbs_SHELL, TopAbs_SOLID))
                makeOffset(s, false);
            for (auto s : result.getSubTopoShapes(TopAbs_FACE, TopAbs_SHELL)) {
                if (s.isPlanarFace()) {
                    s.linearize(true, false);
                    makeOffset(s, true);
                } else
                    makeOffset(s, false);
            }
            for (auto s : result.getSubTopoShapes(TopAbs_EDGE, TopAbs_FACE)) {
                if (s.isLinearEdge()) {
                    s.linearize(false, true);
                    makeOffset(s, true);
                } else
                    makeOffset(s, false);
            }

            result.makECompound(results);

            doFuse();

        } catch(Base::Exception & e) {
            e.ReportException();
            throw;
        } catch(Standard_Failure & e) {
            FC_THROWM(Base::CADKernelError, "Failed to make offset: " << e.GetMessageString());
        }
    }

    result.setPlacement(Placement.getValue());
    Shape.setValue(result);
    auto res = executeExtensions();
    if (res) {
        FC_ERR(res->Why);
        delete res;
    }
}

App::DocumentObject *SubShapeBinder::getElementOwner(const Data::MappedName & name) const
{
    if(!name)
        return nullptr;

    static std::string op(Part::OpCodes::Shapebinder);
    op += ":";
    int offset = name.rfind(op);
    if (offset < 0)
        return nullptr;
    
    int idx, subidx;
    char sep;
    int size;
    const char * s = name.toConstString(offset+op.size(), size);
    bio::stream<bio::array_source> iss(s, size);
    if (!(iss >> idx >> sep >> subidx) || sep!=':' || subidx<0)
        return nullptr;

    const App::PropertyXLink *link = nullptr;
    if(idx < 0)
        link = &_CopiedLink;
    else if (idx < Support.getSize()) {
        int i=0;
        for(auto &l : Support.getSubListValues()) {
            if(i++ == idx)
                link = &l;
        }
    }
    if(!link || !link->getValue())
        return nullptr;

    const auto &subs = link->getSubValues();
    if(subidx < (int)subs.size())
        return link->getValue()->getSubObject(subs[subidx].c_str());

    if(subidx == 0 && subs.empty())
        return link->getValue();

    return nullptr;
}

void SubShapeBinder::slotRecomputedObject(const App::DocumentObject& Obj) {
    if(Context.getValue() == &Obj
        && !this->testStatus(App::ObjectStatus::Recompute2))
    {
        try {
            update();
        } catch (Base::Exception &e) {
            e.ReportException();
        }
    }
}

static const char _GroupPrefix[] = "Configuration (";

bool SubShapeBinder::needUpdate() const
{
    switch(BindMode.getValue()) {
    case BindModeEnum::Synchronized:
    case BindModeEnum::Float:
    case BindModeEnum::FloatFirst:
        return true;
    default:
        return false;
    }
}

App::DocumentObjectExecReturn* SubShapeBinder::execute(void) {

    setupCopyOnChange();

    if (needUpdate())
        update(UpdateForced);
    else {
        TopoShape result(SupportShape.getShape());
        if (!result.isNull())
            buildShape(result);
    }

    return inherited::execute();
}

void SubShapeBinder::onDocumentRestored() {
    if(_Version.getValue()<2)
        update(UpdateInit);
    else if (_Version.getValue()<4
            && needUpdate()
            && !testStatus(App::ObjectStatus::PartialObject))
    {
        // Older version SubShapeBinder does not treat missing sub object as
        // error, which may cause noticeable error to user. We'll perform an
        // explicit check here, and raise exception if necessary.
        for(auto &l : Support.getSubListValues()) {
            auto obj = l.getValue();
            if(!obj || !obj->getNameInDocument())
                continue;
            const auto &subvals = l.getSubValues();
            std::set<std::string> subs(subvals.begin(),subvals.end());
            static std::string none("");
            if(subs.empty())
                subs.insert(none);
            else if(subs.size()>1)
                subs.erase(none);
            for(const auto &sub : subs) {
                if(!obj->getSubObject(sub.c_str())) {
                    if (obj->getDocument() != getDocument())
                        FC_THROWM(Base::RuntimeError,
                                "Failed to get sub-object " << obj->getFullName() << "." << sub);
                    else
                        FC_THROWM(Base::RuntimeError,
                                "Failed to get sub-object " << obj->getNameInDocument() << "." << sub);
                }
            }
        }
    }
    if (_Version.getValue() < 6)
        collapseGeoChildren();
    inherited::onDocumentRestored();
}

void SubShapeBinder::collapseGeoChildren()
{
    // Geo children, i.e. children of GeoFeatureGroup may group some tool
    // features under itself but does not function as a container. In addition,
    // its parent group can directly reference the tool feature grouped without
    // referencing the child. The purpose of this function is to remove any
    // intermediate Non group features in the object path to avoid unnecessary
    // dependencies.
    if (Support.testStatus(App::Property::User3))
        return;

    Base::ObjectStatusLocker<App::Property::Status, App::Property>
        guard(App::Property::User3, &Support);
    App::PropertyXLinkSubList::atomic_change guard2(Support, false);

    std::vector<App::DocumentObject*> removes;
    std::map<App::DocumentObject*, std::vector<std::string> > newVals;
    std::ostringstream ss;
    for(auto &l : Support.getSubListValues()) {
        auto obj = l.getValue();
        if(!obj || !obj->getNameInDocument())
            continue;
        auto subvals = l.getSubValues();
        if (subvals.empty())
            continue;
        bool touched = false;
        for (auto itSub=subvals.begin(); itSub!=subvals.end();) {
            auto &sub = *itSub;
            App::SubObjectT sobjT(obj, sub.c_str());
            if (sobjT.normalize(App::SubObjectT::NormalizeOption::KeepSubName)) {
                touched = true;
                auto newobj = sobjT.getObject();
                sub = sobjT.getSubName();
                if (newobj != obj) {
                    newVals[newobj].push_back(std::move(sub));
                    itSub = subvals.erase(itSub);
                    continue;
                }
            }
            ++itSub;
        }
        if (touched)
            removes.push_back(obj);
        if (!subvals.empty() && touched) {
            auto &newSubs = newVals[obj];
            if (newSubs.empty())
                newSubs = std::move(subvals);
            else
                newSubs.insert(newSubs.end(),
                                std::make_move_iterator(subvals.begin()),
                                std::make_move_iterator(subvals.end()));
        }
    }

    if (removes.size() || newVals.size())
        guard2.aboutToChange();
    for (auto obj : removes)
        Support.removeValue(obj);
    if (newVals.size())
        setLinks(std::move(newVals));
}

void SubShapeBinder::onChanged(const App::Property *prop) {

    if(prop == &Context || prop == &Relative) {
        if(!Context.getValue() || !Relative.getValue()) {
            connRecomputedObj.disconnect();
        } else if(contextDoc != Context.getValue()->getDocument() 
                || !connRecomputedObj.connected()) 
        {
            contextDoc = Context.getValue()->getDocument();
            connRecomputedObj = contextDoc->signalRecomputedObject.connect(
                    boost::bind(&SubShapeBinder::slotRecomputedObject, this, bp::_1));
        }
    }else if(!App::GetApplication().isRestoring() && !getDocument()->isPerformingTransaction()) {
        if(prop == &Support) {
            if (!Support.testStatus(App::Property::User3)) {
                collapseGeoChildren();
                clearCopiedObjects();
                setupCopyOnChange();
                if(Support.getSubListValues().size())
                    update(); 
            }
        }else if(prop == &BindCopyOnChange) {
            setupCopyOnChange();
        }else if(prop == &BindMode) {
            update(UpdateForced);
            checkPropertyStatus();
        }else if(prop == &PartialLoad) {
           checkPropertyStatus();
        }else if(prop && !prop->testStatus(App::Property::User3)) {
            checkCopyOnChange(*prop);
        }

        if(prop == &BindMode || prop == &Support) {
            if(BindMode.getValue()==BindModeEnum::Detached && Support.getSubListValues().size())
                Support.setValue(0);
        }

        if (prop == &Label)
            slotLabelChanged();
    }

    // Regardless of restoring or undo, we'll check the support for label
    // synchronization
    if (prop == &Support && _Version.getValue() >= 7) {
        connLabelChange.disconnect();
        if (Support.getValue()) {
            auto &xlink = Support.getSubListValues().front();
            const auto &subs = xlink.getSubValues();
            auto linked = xlink.getValue();
            if (linked) {
                linked = linked->getSubObject(subs.empty() ? "" : subs[0].c_str());
                if (linked) {
                    connLabelChange = linked->Label.signalChanged.connect(
                            boost::bind(&SubShapeBinder::slotLabelChanged, this));
                    slotLabelChanged();
                }
            }
        }
    }

    inherited::onChanged(prop);
}

void SubShapeBinder::slotLabelChanged()
{
    if (!getDocument()
            || !getNameInDocument()
            || isRestoring()
            || getDocument()->isPerformingTransaction()
            || _Version.getValue() < 7)
        return;

    std::string prefix = getNameInDocument();
    prefix += "(";
    if (Label.getStrValue() != getNameInDocument()
            && !boost::starts_with(Label.getStrValue(), prefix))
        return;

    if (Support.getSize()) {
        auto &xlink = Support.getSubListValues().front();
        const auto &subs = xlink.getSubValues();
        auto linked = xlink.getValue()->getSubObject(subs.empty() ? "" : subs[0].c_str());
        if (linked) {
            std::string label;
            if (linked->isDerivedFrom(SubShapeBinder::getClassTypeId())
                    || linked->isDerivedFrom(App::Link::getClassTypeId())) {
                std::string p = linked->getNameInDocument();
                p += "(";
                if (boost::starts_with(linked->Label.getValue(), p)) {
                    const char *linkedLabel = linked->Label.getValue() + p.size();
                    while (*linkedLabel == '*')
                        ++linkedLabel;
                    label = prefix + "*" + linkedLabel;
                    if (boost::ends_with(label, ")"))
                        label.resize(label.size()-1);
                    else if (boost::ends_with(label, ")...")) {
                        label.resize(label.size()-4);
                        label += "...";
                    }
                }
            }
            if (label.empty())
                label = prefix + linked->Label.getValue();
            if (Support.getSize() > 1 || subs.size() > 1)
                label += ")...";
            else
                label += ")";
            Label.setValue(label);
            return;
        }
    }
    Label.setValue(prefix + "?)");
}

void SubShapeBinder::checkCopyOnChange(const App::Property &prop) {
    if(BindCopyOnChange.getValue()!=1
            || getDocument()->isPerformingTransaction()
            || !App::LinkBaseExtension::isCopyOnChangeProperty(this,prop)
            || Support.getSubListValues().size()!=1)
        return;

    auto linked = Support.getSubListValues().front().getValue();
    if(!linked)
        return;
    auto linkedProp = linked->getPropertyByName(prop.getName());
    if(linkedProp && linkedProp->getTypeId()==prop.getTypeId() && !linkedProp->isSame(prop))
        BindCopyOnChange.setValue(2);
}

void SubShapeBinder::checkPropertyStatus() {
    Support.setAllowPartial(PartialLoad.getValue());

    // Make Shape transient can reduce some file size, and maybe reduce file
    // loading time as well. But there maybe complication arise when doing
    // TopoShape version upgrade. So we DO NOT set trasient at the moment.
    //
    // Shape.setStatus(App::Property::Transient, !PartialLoad.getValue() && BindMode.getValue()==BindModeEnum::Synchronized);
}

void SubShapeBinder::setLinks(std::map<App::DocumentObject *, std::vector<std::string> >&&values, bool reset)
{
    if(values.empty()) {
        if(reset) {
            Shape.setValue(Part::TopoShape());
            Support.setValue(0);
        }
        return;
    }
    auto inSet = getInListEx(true);
    inSet.insert(this);

    for(auto &v : values) {
        if(!v.first || !v.first->getNameInDocument())
            FC_THROWM(Base::ValueError,"Invalid document object");
        if(inSet.find(v.first)!=inSet.end())
            FC_THROWM(Base::ValueError, "Cyclic reference to " << v.first->getFullName());

        if(v.second.empty()) {
            v.second.push_back("");
            continue;
        }

        std::vector<std::string> wholeSubs;
        for(auto &sub : v.second) {
            if(sub.empty()) {
                wholeSubs.clear();
                v.second.resize(1);
                v.second[0].clear();
                break;
            }else if(sub[sub.size()-1] == '.')
                wholeSubs.push_back(sub);
        }
        for(auto &whole : wholeSubs) {
            for(auto it=v.second.begin();it!=v.second.end();) {
                auto &sub = *it;
                if(!boost::starts_with(sub,whole) || sub.size()==whole.size())
                    ++it;
                else {
                    FC_LOG("Remove subname " << sub <<" because of whole selection " << whole);
                    it = v.second.erase(it);
                }
            }
        }
    }

    if(!reset) {
        for(auto &link : Support.getSubListValues()) {
            auto linkedObj = link.getValue();
            if (!linkedObj || !linkedObj->getNameInDocument()) {
                FC_WARN("Discard missing support: " 
                        << link.getObjectName() << " " << link.getDocumentPath());
                continue;
            }
            auto subs = link.getSubValues();
            auto &s = values[linkedObj];
            if(s.empty()) {
                s = std::move(subs);
                continue;
            }else if(subs.empty() || s[0].empty())
                continue;

            for(auto &sub : s) {
                for(auto it=subs.begin();it!=subs.end();) {
                    if(sub[sub.size()-1] == '.') {
                        if(boost::starts_with(*it,sub)) {
                            FC_LOG("Remove subname " << *it <<" because of whole selection " << sub);
                            it = subs.erase(it);
                        } else
                            ++it;
                    }else if(it->empty() 
                            || (it->back()=='.' && boost::starts_with(sub,*it)))
                    {
                        FC_LOG("Remove whole subname " << *it <<" because of " << sub);
                        it = subs.erase(it);
                    } else
                        ++it;
                }
            }
            subs.insert(subs.end(),s.begin(),s.end());
            s = std::move(subs);
        }
    }
    Support.setValues(std::move(values));
}
    
void SubShapeBinder::handleChangedPropertyType(
        Base::XMLReader &reader, const char * TypeName, App::Property * prop) 
{
   if(prop == &Support) {
       Support.upgrade(reader,TypeName);
       return;
   }
   inherited::handleChangedPropertyType(reader,TypeName,prop);
}

App::DocumentObject *SubShapeBinder::_getLinkedObject(
        bool recurse, Base::Matrix4D *mat, bool transform, int depth) const
{
    if (mat && transform) 
        *mat *= Placement.getValue().toMatrix();

    auto self = const_cast<SubShapeBinder*>(this);

    const auto &supports = Support.getSubListValues();
    if (supports.empty())
        return self;

    auto &link = supports.front();
    if (!link.getValue())
        return self;

    auto sobj = link.getValue()->getSubObject(link.getSubName(), nullptr, mat, true, depth+1);
    if (!sobj)
        return self;
    if (!recurse || sobj == link.getValue())
        return sobj;

    // set transform to false, because the above getSubObject() already include
    // the transform of sobj
    sobj = sobj->getLinkedObject(true, mat, false, depth+1);

    auto binder = Base::freecad_dynamic_cast<SubShapeBinder>(sobj);
    if (!binder)
        return sobj;

    return binder->_getLinkedObject(true, mat, false, depth+1);
}

App::SubObjectT
SubShapeBinder::import(const App::SubObjectT &_feature, 
                       const App::SubObjectT &_editObjT,
                       bool importWholeObject,
                       bool noSubElement,
                       bool compatible,
                       bool noSubObject)
{
    // Normalize to 1) simplify hierarchy, 2) make sure to use old style index
    // based element, because e.g. TaskDraftParameter stores neutral plane in
    // editor like <objname>:<element>, and it split on ':' to get the element
    // from the second entry, while the new style mapped element actually
    // contains ':'.
    auto feature = _feature.normalized();
    auto editObjT = _editObjT.normalized();

    App::DocumentObject *editObj = nullptr;
    App::DocumentObject *container = nullptr;
    App::DocumentObject *topParent = nullptr;
    std::string subname = editObjT.getSubNameNoElement();

    if (noSubElement) {
        noSubObject = true;
        importWholeObject = false;
    }

    editObj = editObjT.getSubObject();
    if (!editObj)
        FC_THROWM(Base::RuntimeError, "No editing object");
    else {
        auto objs = editObjT.getSubObjectList();
        topParent = objs.front();
        for (auto rit = objs.rbegin(); rit != objs.rend(); ++rit) {
            auto obj = (*rit)->getLinkedObject();
            if (obj->hasExtension(App::GeoFeatureGroupExtension::getExtensionClassTypeId())) {
                container = obj;
                break;
            }
        }
    }
    if (!container && !feature.hasSubObject()) {
        container = Part::BodyBase::findBodyOf(editObj);
        if (!container) {
            container = App::Part::getPartOfObject(editObj);
            if (!container
                    && editObjT.getDocumentName() == feature.getDocumentName())
                return feature;
        }
    }
    auto sobj = feature.getSubObject();
    if (!sobj)
        FC_THROWM(Base::RuntimeError,
                "Sub object not found: " << feature.getSubObjectFullName());
    if (sobj == editObj || editObj->getInListEx(true).count(sobj)) {
        // Do not throw. Let caller deal with it.
        //
        // FC_THROWM(Base::RuntimeError,
        //         "Cyclic reference to: " << feature.getSubObjectFullName());
        return feature;
    }
    auto link = feature.getObject();

    const char *featName = "Import";
    App::SubObjectT resolved;
    if (!container)
        resolved = feature;
    else {
        std::string linkSub = feature.getSubName();
        topParent->resolveRelativeLink(subname, link, linkSub, RelativeLinkOption::Flatten);
        if (!link)
            FC_THROWM(Base::RuntimeError,
                    "Failed to resolve relative link: "
                    << editObjT.getSubObjectFullName() << " -> "
                    << feature.getSubObjectFullName());

        if (link->isDerivedFrom(App::Origin::getClassTypeId())) {
            auto feat = link->getSubObject(linkSub.c_str());
            if (feat) {
                link = feat;
                linkSub.clear();
            }
        }

        resolved = App::SubObjectT(link, linkSub.c_str());
        if (link == container
                || Part::BodyBase::findBodyOf(link) == container
                || App::GeoFeatureGroupExtension::getGroupOfObject(link) == container) {
            if ((!noSubElement || !resolved.hasSubElement())
                    && (!noSubObject || !resolved.hasSubObject()))
                return App::SubObjectT(sobj, feature.getElementName());
            featName = "Binder";
        }
    }

    std::string resolvedSub = resolved.getSubNameNoElement();
    if (!importWholeObject)
        resolvedSub += resolved.getOldElementName();
    std::string element;
    if (importWholeObject)
        element = feature.getNewElementName();

    App::Document *doc;
    std::vector<App::DocumentObject*> objs;

    // Try to find an unused import of the same object
    App::GeoFeatureGroupExtension *group = nullptr;
    if (container) {
        doc = container->getDocument();
        group = container->getExtensionByType<App::GeoFeatureGroupExtension>(true);
        if (!group)
            FC_THROWM(Base::RuntimeError, "Invalid container: " << container->getFullName());
        objs = group->Group.getValue();
    } else {
        doc = editObj->getDocument();
        for (auto obj : doc->getObjectsOfType(SubShapeBinder::getClassTypeId())) {
            if (obj != editObj && !App::GeoFeatureGroupExtension::getGroupOfObject(obj))
                objs.push_back(obj);
        }
    }

    for (auto o : objs) {
        auto binder = Base::freecad_dynamic_cast<Part::SubShapeBinder>(o);
        if (!binder || (!boost::starts_with(o->getNameInDocument(), "Import")
                        && !boost::starts_with(o->getNameInDocument(), "BaseFeature")))
            continue;
        if (binder->Support.getSize() != 1)
            continue;
        auto &binderSupport = binder->Support.getSubListValues().front();
        const auto &subs = binderSupport.getSubValues(false);
        if (subs.size() > 1
                || binderSupport.getValue() != resolved.getObject()
                || (resolvedSub.size() && subs.empty())
                || (!subs.empty() && resolvedSub != subs[0]))
            continue;
        if (element.size()) {
            try {
                auto res = Part::Feature::getElementFromSource(
                        binder, "", binderSupport.getValue(),
                        (resolvedSub + element).c_str(), true);
                if (res.size()) {
                    std::string tmp;
                    return App::SubObjectT(binder, res.front().index.toString(tmp));
                }
            } catch (Base::Exception &e) {
                if (FC_LOG_INSTANCE.isEnabled(FC_LOGLEVEL_LOG))
                    e.ReportException();
            }
            if (FC_LOG_INSTANCE.isEnabled(FC_LOGLEVEL_LOG))
                FC_WARN("Failed to deduce bound geometry from existing import: " << binder->getFullName());
        } else
            return binder;
    }

    struct Cleaner {
        App::DocumentObjectT objT;
        Cleaner(App::DocumentObject *obj)
            :objT(obj)
        {}
        ~Cleaner()
        {
            auto doc = objT.getDocument();
            if (doc)
                doc->removeObject(objT.getObjectName().c_str());
        }
        void release()
        {
            objT = App::DocumentObjectT();
        }
    };

    auto binder = static_cast<Part::SubShapeBinder*>(
            doc->addObject(compatible ?
                "PartDesign::SubShapeBinder" : "Part::SubShapeBinder", featName));
    Cleaner guard(binder);
    binder->Visibility.setValue(false);
    if (group)
        group->addObject(binder);
    std::map<App::DocumentObject*, std::vector<std::string> > support;
    auto &supportSubs = support[link];
    if (resolvedSub.size())
        supportSubs.push_back(resolvedSub);
    binder->setLinks(std::move(support));
    if (element.size()) {
        doc->recomputeFeature(binder);
        try {
            auto res = Part::Feature::getElementFromSource(
                    binder, "", link, (resolvedSub + element).c_str(), true);
            if (res.size()) {
                std::string tmp;
                guard.release();
                return App::SubObjectT(binder, res.front().index.toString(tmp));
            }
        } catch (Base::Exception &e) {
            if (FC_LOG_INSTANCE.isEnabled(FC_LOGLEVEL_LOG))
                e.ReportException();
        }
        // Cannot deduce bound geometry, ignore 'importWholeObject' and fall
        // back to single element binding
        support.clear();
        support[link].push_back(resolvedSub + element);
        binder->setLinks(std::move(support), true);
        doc->recomputeFeature(binder);
    }
    guard.release();
    return binder;
}
