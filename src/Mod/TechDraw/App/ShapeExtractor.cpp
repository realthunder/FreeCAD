/***************************************************************************
 *   Copyright (c) 2019 WandererFan <wandererfan@gmail.com>                *
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
# include <sstream>
# include <BRepTools.hxx>
# include <TopoDS.hxx>
# include <TopoDS_Vertex.hxx>
# include <BRepBuilderAPI_Copy.hxx>
#endif

#include <App/Document.h>
#include <App/GroupExtension.h>
#include <App/Link.h>
#include <App/Part.h>
#include <Base/Console.h>
#include <Base/Parameter.h>
#include <Base/Placement.h>
#include <Mod/Part/App/PartFeature.h>
#include <Mod/Part/App/PrimitiveFeature.h>
#include <Mod/Part/App/FeaturePartCircle.h>
//#include <Mod/Sketcher/App/SketchObject.h>

#include "ShapeExtractor.h"
#include "DrawUtil.h"
#include "ShapeUtils.h"


using namespace TechDraw;
using DU = DrawUtil;
using SU = ShapeUtils;


//! pick out the 2d document objects objects in the list of links and return a vector of their shapes
//! Note that point objects will not make it through the hlr/projection process.
std::vector<Part::TopoShape> ShapeExtractor::getShapes2d(const std::vector<App::DocumentObject*> links)
{
//    Base::Console().Message("SE::getShapes2d() - links: %d\n", links.size());

    std::vector<Part::TopoShape> shapes2d;

    for (auto& l:links) {
        const App::GroupExtension* gex = dynamic_cast<const App::GroupExtension*>(l);
        if (gex) {
            std::vector<App::DocumentObject*> groupAll = gex->Group.getValues();
            for (auto& item : groupAll) {
                if (is2dObject(item)) {
                    if (item->getTypeId().isDerivedFrom(Part::Feature::getClassTypeId())) {
                        Part::TopoShape temp = getLocatedShape(item);
                        if (!temp.isNull()) {
                            shapes2d.push_back(temp);
                        }
                    }
                }
            }
        } else {
            if (is2dObject(l)) {
                if (l->getTypeId().isDerivedFrom(Part::Feature::getClassTypeId())) {
                    Part::TopoShape temp = getLocatedShape(l);
                    if (!temp.isNull()) {
                        shapes2d.push_back(temp);
                    }
                }  // other 2d objects would go here - Draft objects? Arch Axis?
            }
        }
    }
    return shapes2d;
}

//! get the located and oriented shapes corresponding to the the links. If the shapes are to be
//! fused, include2d should be false as 2d & 3d shapes may not fuse.
Part::TopoShape ShapeExtractor::getShapes(const std::vector<App::DocumentObject*> links, bool include2d)
{
//    Base::Console().Message("SE::getShapes() - links in: %d\n", links.size());
    std::vector<Part::TopoShape> sourceShapes;

    for (auto& l:links) {
        if (is2dObject(l->getLinkedObject()) && !include2d) {
            continue;
        }
        auto shape = Part::Feature::getTopoShape(l);
        if(!shape.isNull()) {
            sourceShapes.push_back(shape);
        } else {
            std::vector<Part::TopoShape> shapeList = getShapesFromObject(l);
            sourceShapes.insert(sourceShapes.end(),shapeList.begin(),shapeList.end());
        }
    }

    std::vector<Part::TopoShape> keepShapes;
    for (auto& s:sourceShapes) {
        if (SU::isShapeReallyNull(s.getShape())) {
            continue;
        } else if (s.getShape().ShapeType() < TopAbs_SOLID) {
            //clean up composite shapes
            Part::TopoShape cleanShape = stripInfiniteShapes(s);
            if (!cleanShape.isNull()) {
                keepShapes.push_back(cleanShape);
            }
        } else if (s.isInfinite()) {
            continue;    //simple shape is infinite
        } else {
            //a simple shape - add to compound
            keepShapes.push_back(s);
        }
    }

    // force = true (the default): a single source still comes back wrapped in a
    // compound, the way the BRep_Builder compound this replaced always did.
    // Note the parameter reads the other way round from its doc comment -- it is
    // force=false that hands back a lone shape unwrapped.
    Part::TopoShape comp;
    comp.makECompound(keepShapes);

    //it appears that an empty compound is !IsNull(), so we need to check a different way
    if (!SU::isShapeReallyNull(comp.getShape())) {
//    BRepTools::Write(comp.getShape(), "SEResult.brep");            //debug
        return comp;
    }

//    Base::Console().Error("DEVEL: ShapeExtractor failed to get any shape.\n");
    return Part::TopoShape();
}

std::vector<Part::TopoShape> ShapeExtractor::getShapesFromObject(const App::DocumentObject* docObj)
{
//    Base::Console().Message("SE::getShapesFromObject(%s)\n", docObj->getNameInDocument());
    std::vector<Part::TopoShape> result;

    const App::GroupExtension* gex = dynamic_cast<const App::GroupExtension*>(docObj);
    App::Property* gProp = docObj->getPropertyByName("Group");
    App::Property* sProp = docObj->getPropertyByName("Shape");
    if (docObj->isDerivedFrom<Part::Feature>()) {
        result.push_back(getLocatedShape(docObj));
    } else if (gex) {           //is a group extension
        std::vector<App::DocumentObject*> objs = gex->Group.getValues();
        std::vector<Part::TopoShape> shapes;
        for (auto& d: objs) {
            shapes = getShapesFromObject(d);
            if (!shapes.empty()) {
                result.insert(result.end(), shapes.begin(), shapes.end());
            }
        }
    //the next 2 bits are mostly for Arch module objects
    } else if (gProp) {       //has a Group property
        App::PropertyLinkList* list = dynamic_cast<App::PropertyLinkList*>(gProp);
        if (list) {
            std::vector<App::DocumentObject*> objs = list->getValues();
            std::vector<Part::TopoShape> shapes;
            for (auto& d: objs) {
                shapes = getShapesFromObject(d);
                if (!shapes.empty()) {
                    result.insert(result.end(), shapes.begin(), shapes.end());
                }
            }
        }
    } else if (sProp) {       //has a Shape property
        Part::PropertyPartShape* shape = dynamic_cast<Part::PropertyPartShape*>(sProp);
        if (shape) {
            result.push_back(getLocatedShape(docObj));
        }
    }
    return result;
}

Part::TopoShape ShapeExtractor::getShapesFused(const std::vector<App::DocumentObject*> links)
{
//    Base::Console().Message("SE::getShapesFused()\n");
    // get only the 3d shapes and fuse them
    Part::TopoShape baseShape = getShapes(links, false);
    if (!baseShape.isNull()) {
        // makEFuse fuses the whole list at once and carries the element names of
        // the sources through the Generated/Modified history of the fuse
        std::vector<Part::TopoShape> children = baseShape.getSubTopoShapes();
        if (children.size() > 1) {
            try {
                Part::TopoShape fusedShape;
                fusedShape.makEFuse(children);
                baseShape = fusedShape;
            }
            catch (const Standard_Failure& e) {
                Base::Console().Error("SE - Fusion failed - %s\n", e.GetMessageString());
            }
        }
        else if (children.size() == 1) {
            baseShape = children.front();
        }
    }

    // if there are 2d shapes in the links they will not fuse with the 3d shapes,
    // so instead we return a compound of the fused 3d shapes and the 2d shapes
    std::vector<Part::TopoShape> shapes2d = getShapes2d(links);
    if (!shapes2d.empty()) {
        if (!baseShape.isNull()) {
            shapes2d.push_back(baseShape);
        }
        Part::TopoShape comp;
        comp.makECompound(shapes2d);
        return comp;
    }

    return baseShape;
}

//inShape is a compound
//The shapes of datum features (Axis, Plan and CS) are infinite
//Infinite shapes can not be projected, so they need to be removed.
Part::TopoShape ShapeExtractor::stripInfiniteShapes(const Part::TopoShape& inShape)
{
//    Base::Console().Message("SE::stripInfiniteShapes()\n");
    std::vector<Part::TopoShape> keepShapes;

    for (auto& child : inShape.getSubTopoShapes()) {
        if (child.getShape().ShapeType() < TopAbs_SOLID) {
            //look inside composite shapes
            keepShapes.push_back(stripInfiniteShapes(child));
        } else if (child.isInfinite()) {
            continue;
        } else {
            //simple shape
            keepShapes.push_back(child);
        }
    }

    Part::TopoShape comp;
    comp.makECompound(keepShapes);
    return comp;
}

bool ShapeExtractor::is2dObject(App::DocumentObject* obj)
{
// TODO:: the check for an object being a sketch should be done as in the commented
// if statement below. To do this, we need to include Mod/Sketcher/SketchObject.h,
// but that makes TechDraw dependent on Eigen libraries which we don't use.  As a
// workaround we will inspect the object's class name.
//    if (obj->isDerivedFrom(Sketcher::SketchObject::getClassTypeId())) {
    std::string objTypeName = obj->getTypeId().getName();
    std::string sketcherToken("Sketcher");
    if (objTypeName.find(sketcherToken) != std::string::npos) {
        return true;
    }

    if (isEdgeType(obj) || isPointType(obj)) {
        return true;
    }
    return false;
}

// just these for now
bool ShapeExtractor::isEdgeType(App::DocumentObject* obj)
{
    bool result = false;
    Base::Type t = obj->getTypeId();
    if (t.isDerivedFrom(Part::Line::getClassTypeId()) ) {
        result = true;
    } else if (t.isDerivedFrom(Part::Circle::getClassTypeId())) {
        result = true;
    } else if (t.isDerivedFrom(Part::Ellipse::getClassTypeId())) {
        result = true;
    } else if (t.isDerivedFrom(Part::RegularPolygon::getClassTypeId())) {
        result = true;
    }
    return result;
}

bool ShapeExtractor::isPointType(App::DocumentObject* obj)
{
    // Base::Console().Message("SE::isPointType(%s)\n", obj->getNameInDocument());
    if (obj) {
        Base::Type t = obj->getTypeId();
        if (t.isDerivedFrom(Part::Vertex::getClassTypeId())) {
            return true;
        } else if (isDraftPoint(obj)) {
            return true;
        } else if (isDatumPoint(obj)) {
            return true;
        }
    }
    return false;
}

bool ShapeExtractor::isDraftPoint(App::DocumentObject* obj)
{
//    Base::Console().Message("SE::isDraftPoint()\n");
    //if the docObj doesn't have a Proxy property, it definitely isn't a Draft point
    App::PropertyPythonObject* proxy = dynamic_cast<App::PropertyPythonObject*>(obj->getPropertyByName("Proxy"));
    if (proxy) {
        std::string  pp = proxy->toString();
//        Base::Console().Message("SE::isDraftPoint - pp: %s\n", pp.c_str());
        if (pp.find("Point") != std::string::npos) {
            return true;
        }
    }
    return false;
}

bool ShapeExtractor::isDatumPoint(App::DocumentObject* obj)
{
    std::string objTypeName = obj->getTypeId().getName();
    std::string pointToken("Point");
    if (objTypeName.find(pointToken) != std::string::npos) {
        return true;
    }
    return false;
}


//! get the location of a point object
Base::Vector3d ShapeExtractor::getLocation3dFromFeat(App::DocumentObject* obj)
{
    // Base::Console().Message("SE::getLocation3dFromFeat()\n");
    if (!isPointType(obj)) {
        return Base::Vector3d(0.0, 0.0, 0.0);
    }
//    if (isDraftPoint(obj) {
//        //Draft Points are not necc. Part::PartFeature??
//        //if Draft option "use part primitives" is not set are Draft points still PartFeature?

    Part::Feature* pf = dynamic_cast<Part::Feature*>(obj);
    if (pf) {
        Part::TopoShape pts = pf->Shape.getShape();
        pts.setPlacement(pf->globalPlacement());
        TopoDS_Shape ts = pts.getShape();
        if (ts.ShapeType() == TopAbs_VERTEX)  {
            TopoDS_Vertex v = TopoDS::Vertex(ts);
            return DrawUtil::vertex2Vector(v);
        }
    }

//    Base::Console().Message("SE::getLocation3dFromFeat - returns: %s\n",
//                            DrawUtil::formatVector(result).c_str());
    return Base::Vector3d(0.0, 0.0, 0.0);
}

//! get the located and oriented version of docObj shape
Part::TopoShape ShapeExtractor::getLocatedShape(const App::DocumentObject* docObj)
{
        Part::TopoShape shape = Part::Feature::getTopoShape(docObj);
        const Part::Feature* pf = dynamic_cast<const Part::Feature*>(docObj);
        if (pf) {
            shape.setPlacement(pf->globalPlacement());
        }
        return shape;
}


bool ShapeExtractor::isSketchObject(const App::DocumentObject* obj)
{
    // use name lookup to avoid a dependency on the Sketcher module
    return obj->isDerivedFrom(Base::Type::fromName("Sketcher::SketchObject"));
}
