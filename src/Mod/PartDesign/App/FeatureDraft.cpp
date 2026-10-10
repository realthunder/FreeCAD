/***************************************************************************
 *   Copyright (c) 2012 Jan Rheinländer                                    *
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
#ifndef _PreComp_
# include <BRepOffsetAPI_DraftAngle.hxx>
# include <BRepBuilderAPI_MakeEdge.hxx>
# include <TopTools_IndexedMapOfShape.hxx>
# include <TopExp.hxx>
# include <TopoDS.hxx>
# include <TopoDS_Face.hxx>
# include <BRepAdaptor_Curve.hxx>
# include <BRepAdaptor_Surface.hxx>
# include <BRep_Tool.hxx>
# include <Geom2d_Curve.hxx>
# include <Geom_Curve.hxx>
# include <Geom_Line.hxx>
# include <Geom_Plane.hxx>
# include <GeomAPI_IntSS.hxx>
# include <gp_Circ.hxx>
# include <gp_Dir.hxx>
# include <gp_Lin.hxx>
# include <gp_Pln.hxx>
#endif

#include <App/OriginFeature.h>
#include <App/Document.h>
#include <Base/Console.h>
#include <Base/Exception.h>
#include <Base/Tools.h>
#include <Mod/Part/App/Part2DObject.h>
#include <Mod/Part/App/TopoShape.h>

#include "FeatureDraft.h"
#include "DatumLine.h"
#include "DatumPlane.h"


using namespace PartDesign;

FC_LOG_LEVEL_INIT("PartDesign", true,true)


PROPERTY_SOURCE(PartDesign::Draft, PartDesign::DressUp)

const App::PropertyAngle::Constraints Draft::floatAngle = { -90.0,90.0 - Base::toDegrees<double>(Precision::Angular()), 0.1 };

Draft::Draft()
{
    ADD_PROPERTY(Angle,(1.5));
    Angle.setConstraints(&floatAngle);
    ADD_PROPERTY_TYPE(NeutralPlane,(nullptr),"Draft",(App::PropertyType)(App::Prop_None),"NeutralPlane");
    ADD_PROPERTY_TYPE(PullDirection,(nullptr),"Draft",(App::PropertyType)(App::Prop_None),"PullDirection");
    ADD_PROPERTY(Reversed,(0));
    ADD_PROPERTY_TYPE(_NeutralEdge,(nullptr),"Draft",
            (App::PropertyType)(App::Prop_Hidden|App::Prop_Output),
            "The edge a guessed neutral plane is taken from");
    ADD_PROPERTY_TYPE(_NeutralSense,(0),"Draft",
            (App::PropertyType)(App::Prop_Hidden|App::Prop_Output),
            "The side of that edge the guessed plane faces");
}

namespace
{
// The plane a draft is neutral about when none is given, from one edge of the
// drafted face. False for an edge that offers none.
bool neutralPlaneFromEdge(const TopoDS_Face &face, const TopoDS_Edge &edge, gp_Pln &plane)
{
    // Note: What happens if the edge is the degenerated edge of a cone?
    // But in that case the draft is not possible anyway!
    BRepAdaptor_Curve c(edge);
    gp_Pnt p1 = c.Value(c.FirstParameter());
    gp_Pnt p2 = c.Value(c.LastParameter());

    if (c.IsClosed()) {
        // Edge is a circle or a circular arc (other types are not allowed for drafting)
        if (c.GetType() != GeomAbs_Circle)
            return false;
        plane = gp_Pln(p1, c.Circle().Axis().Direction());
        return true;
    }

    // Edge is linear
    // Find midpoint of edge and create auxiliary plane through midpoint normal to edge
    gp_Pnt pm = c.Value((c.FirstParameter() + c.LastParameter()) / 2.0);
    Handle(Geom_Plane) aux = new Geom_Plane(pm, gp_Dir(p2.X() - p1.X(), p2.Y() - p1.Y(), p2.Z() - p1.Z()));
    // Intersect plane with face. Is there no easier way?
    BRepAdaptor_Surface adapt(face, Standard_False);
    Handle(Geom_Surface) sf = adapt.Surface().Surface();
    GeomAPI_IntSS intersector(aux, sf, Precision::Confusion());
    if (!intersector.IsDone() || intersector.NbLines() < 1)
        return false;
    Handle(Geom_Curve) icurve = intersector.Line(1);
    if (!icurve->IsKind(STANDARD_TYPE(Geom_Line)))
        return false;
    // TODO: How to extract the line from icurve without creating an edge first?
    TopoDS_Edge line = BRepBuilderAPI_MakeEdge(icurve);
    BRepAdaptor_Curve lc(line);
    plane = gp_Pln(pm, lc.Line().Direction());
    return true;
}

// Which way a direction at an edge of a face points, told by the face and not
// by how the shape is written down: 1 into the face from the edge, or with the
// face's outward normal if the direction is across the face rather than along
// it; -1 the other way; 0 where that cannot be told. The edge has to carry
// the orientation it has IN the face.
int senseAtEdge(const TopoDS_Face &face, const TopoDS_Edge &edge, const gp_Dir &dir)
{
    double first = 0.0, last = 0.0;
    Handle(Geom2d_Curve) pcurve = BRep_Tool::CurveOnSurface(edge, face, first, last);
    if (pcurve.IsNull())
        return 0;
    const double mid = (first + last) / 2.0;
    BRepAdaptor_Curve c(edge);
    gp_Pnt pm;
    gp_Vec tangent;
    c.D1(mid, pm, tangent);
    if (edge.Orientation() == TopAbs_REVERSED)
        tangent.Reverse();
    gp_Pnt2d uv = pcurve->Value(mid);
    BRepAdaptor_Surface surface(face, Standard_False);
    gp_Pnt ps;
    gp_Vec du, dv;
    surface.D1(uv.X(), uv.Y(), ps, du, dv);
    gp_Vec normal = du.Crossed(dv);
    if (face.Orientation() == TopAbs_REVERSED)
        normal.Reverse();
    if (normal.Magnitude() < Precision::Confusion()
            || tangent.Magnitude() < Precision::Confusion())
        return 0;
    normal.Normalize();
    tangent.Normalize();
    // to the left of the edge as the wire runs, seen from outside: the face
    const gp_Vec inward = normal.Crossed(tangent);
    const gp_Vec d(dir);
    const double along = d.Dot(inward);
    const double across = d.Dot(normal);
    const double pick = fabs(along) >= fabs(across) ? along : across;
    if (fabs(pick) < Precision::Angular())
        return 0;
    return pick > 0 ? 1 : -1;
}
}  // namespace

bool Draft::guessNeutralPlane(const Part::TopoShape &baseShape,
                              const Part::TopoShape &faceShape,
                              gp_Pln &plane)
{
    const TopoDS_Face face = TopoDS::Face(faceShape.getShape());
    TopTools_IndexedMapOfShape mapOfEdges;
    TopExp::MapShapes(face, TopAbs_EDGE, mapOfEdges);

    // What the guess says for one edge, with the pull direction on the side
    // that was written down, or written down now if none was.
    auto planeAt = [&](const TopoDS_Edge &edge, bool record) {
        if (!neutralPlaneFromEdge(face, edge, plane))
            return false;
        int sense = senseAtEdge(face, edge, plane.Axis().Direction());
        int wanted = record ? 0 : static_cast<int>(_NeutralSense.getValue());
        if (sense != 0 && wanted != 0 && sense != wanted) {
            plane = gp_Pln(plane.Location(), plane.Axis().Direction().Reversed());
            sense = wanted;
        }
        if ((record || wanted == 0) && _NeutralSense.getValue() != sense)
            _NeutralSense.setValue(sense);
        return true;
    };

    // The edge an earlier guess settled on, for as long as the face has it.
    App::DocumentObject *base = Base.getValue();
    if (base && _NeutralEdge.getValue() == base && !_NeutralEdge.getSubValues().empty()) {
        const auto &subs = _NeutralEdge.getSubValues();
        const auto &shadows = _NeutralEdge.getShadowSubs();
        const std::string &ref = (!shadows.empty() && !shadows.front().first.empty())
            ? shadows.front().first : subs.front();
        Part::TopoShape edge;
        try {
            edge = baseShape.getSubTopoShape(ref.c_str(), true);
        }
        catch (...) {
        }
        // as the face has it, for the orientation
        const int found = (!edge.isNull() && edge.shapeType() == TopAbs_EDGE)
            ? mapOfEdges.FindIndex(edge.getShape()) : 0;
        if (found > 0 && planeAt(TopoDS::Edge(mapOfEdges(found)), false))
            return true;
    }

    for (int i = 1; i <= mapOfEdges.Extent(); i++) {
        const TopoDS_Edge &edge = TopoDS::Edge(mapOfEdges(i));
        if (!planeAt(edge, true))
            continue;
        FC_LOG("guess draft neutral plane using Edge" << i);
        int index = baseShape.findShape(edge);
        if (base && index > 0) {
            std::vector<std::string> sub {"Edge" + std::to_string(index)};
            if (_NeutralEdge.getValue() != base || _NeutralEdge.getSubValues() != sub)
                _NeutralEdge.setValue(base, std::move(sub));
        }
        return true;
    }
    return false;
}

void Draft::onDocumentRestored()
{
    // A file from before _NeutralEdge: the base's shape is still the one the
    // file stored, with its edges in the order the guess was made in, so this
    // is the last moment the edge it took can be told.
    if (!NeutralPlane.getValue() && Base.getValue()
            && (!_NeutralEdge.getValue() || _NeutralSense.getValue() == 0)) {
        try {
            Part::TopoShape baseShape = getBaseShape();
            baseShape.setTransform(Base::Matrix4D());
            auto faces = getFaces(baseShape);
            gp_Pln plane;
            if (!faces.empty())
                guessNeutralPlane(baseShape, faces[0], plane);
        }
        catch (Base::Exception &) {
        }
        catch (Standard_Failure &) {
        }
    }
    DressUp::onDocumentRestored();
}

void Draft::handleChangedPropertyType(Base::XMLReader &reader,
                                      const char * TypeName,
                                      App::Property * prop)
{
    Base::Type inputType = Base::Type::fromName(TypeName);
    if (prop == &Angle && inputType == App::PropertyFloatConstraint::getClassTypeId()) {
        App::PropertyFloatConstraint v;
        v.Restore(reader);
        Angle.setValue(v.getValue());
    }
    else {
        DressUp::handleChangedPropertyType(reader, TypeName, prop);
    }
}

short Draft::mustExecute() const
{
    if (Placement.isTouched() ||
        Angle.isTouched() ||
        NeutralPlane.isTouched() ||
        PullDirection.isTouched() ||
        Reversed.isTouched())
        return 1;
    return DressUp::mustExecute();
}

App::DocumentObjectExecReturn *Draft::execute()
{
    // Get parameters
    // Base shape
    Part::TopoShape baseShape;
    try {
        baseShape = getBaseShape();
    } catch (Base::Exception& e) {
        return new App::DocumentObjectExecReturn(e.what());
    }
    baseShape.setTransform(Base::Matrix4D());

    // Faces where draft should be applied
    auto faces = getFaces(baseShape);
    if (faces.empty())
        return new App::DocumentObjectExecReturn(QT_TRANSLATE_NOOP("Exception", "No faces specified"));

    // Draft angle
    double angle = Base::toRadians(Angle.getValue());

    // Pull direction
    gp_Dir pullDirection;
    App::DocumentObject* refDirection = PullDirection.getValue();
    if (refDirection) {
        if (refDirection->isDerivedFrom<PartDesign::Line>()) {
            PartDesign::Line* line = static_cast<PartDesign::Line*>(refDirection);
            Base::Vector3d d = line->getDirection();
            pullDirection = gp_Dir(d.x, d.y, d.z);
        } else if (refDirection->isDerivedFrom<App::Line>()) {
            App::Line* line = static_cast<App::Line*>(refDirection);
            Base::Vector3d d = line->getDirection();
            pullDirection = gp_Dir(d.x, d.y, d.z);
        } else if (refDirection->isDerivedFrom<Part::Feature>()) {
            std::vector<std::string> subStrings = PullDirection.getSubValues(true);
            if (subStrings.empty() || subStrings[0].empty())
                THROWM(Base::ValueError, "No pull direction reference specified")

            Part::Feature* refFeature = static_cast<Part::Feature*>(refDirection);
            Part::TopoShape refShape = refFeature->Shape.getShape();
            TopoDS_Shape ref = refShape.getSubShape(subStrings[0].c_str());

            if (ref.ShapeType() == TopAbs_EDGE) {
                TopoDS_Edge refEdge = TopoDS::Edge(ref);
                if (refEdge.IsNull())
                    THROWM(Base::ValueError, "Failed to extract pull direction reference edge")
                BRepAdaptor_Curve adapt(refEdge);
                if (adapt.GetType() != GeomAbs_Line)
                    THROWM(Base::TypeError, "Pull direction reference edge must be linear")

                pullDirection = adapt.Line().Direction();
            } else {
                THROWM(Base::TypeError, "Pull direction reference must be an edge or a datum line")
            }
        } else {
            THROWM(Base::TypeError, "Pull direction reference must be an edge of a feature or a datum line")
        }

        TopLoc_Location invObjLoc = this->getLocation().Inverted();
        pullDirection.Transform(invObjLoc.Transformation());
    }

    // Neutral plane
    gp_Pln neutralPlane;
    App::DocumentObject* refPlane = NeutralPlane.getValue();
    if (!refPlane) {
        // Try to guess a neutral plane from the first selected face
        if (!guessNeutralPlane(baseShape, faces[0], neutralPlane))
            THROWM(Base::RuntimeError, "No neutral plane specified and none can be guessed")
    } else {
        if (refPlane->isDerivedFrom<PartDesign::Plane>()) {
            PartDesign::Plane* plane = static_cast<PartDesign::Plane*>(refPlane);
            Base::Vector3d b = plane->getBasePoint();
            Base::Vector3d n = plane->getNormal();
            neutralPlane = gp_Pln(gp_Pnt(b.x, b.y, b.z), gp_Dir(n.x, n.y, n.z));
        } else if (refPlane->isDerivedFrom<App::Plane>()
                   || (refPlane->isDerivedFrom<Part::Part2DObject>()
                       && (NeutralPlane.getSubValues().empty()
                           || NeutralPlane.getSubValues().front().empty()))) {
            // a whole sketch is its plane (upstream 51f4ad7432); an edge of
            // it goes the way of any shape's below
            neutralPlane = Feature::makePlnFromPlane(refPlane);
        } else if (refPlane->isDerivedFrom<Part::Feature>()) {
            std::vector<std::string> subStrings = NeutralPlane.getSubValues(true);
            if (subStrings.empty() || subStrings[0].empty())
                THROWM(Base::ValueError, "No neutral plane reference specified")

            Part::Feature* refFeature = static_cast<Part::Feature*>(refPlane);
            Part::TopoShape refShape = refFeature->Shape.getShape();
            TopoDS_Shape ref = refShape.getSubShape(subStrings[0].c_str());

            if (ref.ShapeType() == TopAbs_FACE) {
                TopoDS_Face refFace = TopoDS::Face(ref);
                if (refFace.IsNull())
                    THROWM(Base::ValueError, "Failed to extract neutral plane reference face")
                BRepAdaptor_Surface adapt(refFace);
                if (adapt.GetType() != GeomAbs_Plane)
                    THROWM(Base::TypeError, "Neutral plane reference face must be planar")

                neutralPlane = adapt.Plane();
            } else if (ref.ShapeType() == TopAbs_EDGE) {
                if (refDirection) {
                    // Create neutral plane through edge normal to pull direction
                    TopoDS_Edge refEdge = TopoDS::Edge(ref);
                    if (refEdge.IsNull())
                        THROWM(Base::ValueError, "Failed to extract neutral plane reference edge")
                    BRepAdaptor_Curve c(refEdge);
                    if (c.GetType() != GeomAbs_Line)
                        THROWM(Base::TypeError, "Neutral plane reference edge must be linear")
                    double a = c.Line().Angle(gp_Lin(c.Value(c.FirstParameter()), pullDirection));
                    if (std::fabs(a - M_PI_2) > Precision::Confusion())
                        THROWM(Base::ValueError, "Neutral plane reference edge must be normal to pull direction")
                    neutralPlane = gp_Pln(c.Value(c.FirstParameter()), pullDirection);
                } else {
                    THROWM(Base::TypeError, "Neutral plane reference can only be an edge if pull direction is defined")
                }
            } else {
                THROWM(Base::TypeError, "Neutral plane reference must be a face")
            }
        } else {
            THROWM(Base::TypeError, "Neutral plane reference must be face of a feature or a datum plane")
        }

        TopLoc_Location invObjLoc = this->getLocation().Inverted();
        neutralPlane.Transform(invObjLoc.Transformation());
    }

    if (!refDirection) {
        // Choose pull direction normal to neutral plane
        pullDirection = neutralPlane.Axis().Direction();
    }

    // Reversed pull direction
    bool reversed = Reversed.getValue();
    if (reversed)
        angle *= -1.0;

    computeProps = {pullDirection, neutralPlane};

    this->positionByBaseFeature();
    try {
        // Note:
        // LocOpe_SplitDrafts can split a face with a wire and apply draft to both parts
        //       Not clear though whether the face must have free boundaries
        // LocOpe_DPrism can create a stand-alone draft prism. The sketch can only have a single
        //       wire, though.
        // BRepFeat_MakeDPrism requires a support for the operation but will probably support multiple
        //       wires in the sketch
        TopoShape shape(0,getDocument()->getStringHasher());
        try {
            shape.makEDraft(baseShape,faces,pullDirection,angle,neutralPlane);
        }catch(Standard_Failure &e) {
            std::ostringstream ss;
            ss << "Failed to create draft: " << e.GetMessageString();
            return new App::DocumentObjectExecReturn(ss.str().c_str());
        }
        if (shape.isNull())
            return new App::DocumentObjectExecReturn("Resulting shape is null");

        this->Shape.setValue(getSolid(shape));
        return App::DocumentObject::StdReturn;
    }
    catch (Standard_Failure& e) {

        return new App::DocumentObjectExecReturn(e.GetMessageString());
    }
}
