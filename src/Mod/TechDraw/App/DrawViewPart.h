/***************************************************************************
 *   Copyright (c) 2007 Jürgen Riegel <juergen.riegel@web.de>              *
 *   Copyright (c) 2013 Luke Parry <l.parry@warwick.ac.uk>                 *
 *   Copyright (c) 2016 WandererFan <wandererfan@gmail.com>                *
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

#ifndef DrawViewPart_h_
#define DrawViewPart_h_

#include <QFuture>
#include <QFutureWatcher>

#include <TopoDS_Edge.hxx>
#include <TopoDS_Wire.hxx>
#include <gp_Trsf.hxx>

#include <App/DocumentObject.h>
#include <App/FeaturePython.h>
#include <App/PropertyFile.h>
#include <App/PropertyLinks.h>
#include <Base/BoundBox.h>
#include <Mod/Part/App/TopoShape.h>
#include <Mod/TechDraw/TechDrawGlobal.h>

#include "CosmeticExtension.h"
#include "DrawView.h"

class Message_ProgressIndicator;

class gp_Pnt;
class gp_Pln;
class gp_Ax2;
class TopoDS_Shape;

namespace Base
{
class SequencerLauncher;
}

namespace App
{
class Part;
}

namespace Part
{
class ProgressIndicator;
}

namespace TechDraw
{
class GeometryObject;
using GeometryObjectPtr = std::shared_ptr<GeometryObject>;
class Vertex;
class BaseGeom;
class Face;
}// namespace TechDraw

namespace TechDraw
{
class DrawHatch;
class DrawGeomHatch;
class DrawViewDimension;
class DrawProjectSplit;
class DrawViewSection;
class DrawViewDetail;
class DrawViewBalloon;
class CosmeticVertex;
class CosmeticEdge;
class CenterLine;
class GeomFormat;
}// namespace TechDraw

namespace TechDraw
{

class DrawViewSection;

class TechDrawExport DrawViewPart: public DrawView, public CosmeticExtension
{
    PROPERTY_HEADER_WITH_EXTENSIONS(TechDraw::DrawViewPart);

public:
    DrawViewPart();
    ~DrawViewPart() override;

    App::PropertyLinkList Source;
    App::PropertyXLinkList XSource;
    App::PropertyVector
        Direction;//TODO: Rename to YAxisDirection or whatever this actually is  (ProjectionDirection)
    App::PropertyVector XDirection;
    App::PropertyBool Perspective;
    App::PropertyDistance Focus;

    App::PropertyBool CoarseView;
    App::PropertyBool SeamVisible;
    App::PropertyBool SmoothVisible;
    //App::PropertyBool   OutlinesVisible;
    App::PropertyBool IsoVisible;

    App::PropertyBool HardHidden;
    App::PropertyBool SmoothHidden;
    App::PropertyBool SeamHidden;
    //App::PropertyBool   OutlinesHidden;
    App::PropertyBool IsoHidden;
    App::PropertyInteger IsoCount;

    App::PropertyInteger ScrubCount;

    // The shaded-view hybrid (docs/TechDrawPortAndSection.md sec 26): a
    // raster shaded underlay drawn beneath the exact-HLR edges. The
    // image and its registration rect are derived state written back by
    // the Gui capture (Prop_Output -- writing them must not re-touch
    // the view).
    App::PropertyBool Shaded;
    App::PropertyFileIncluded UnderlayImage;
    App::PropertyFloat UnderlayResolution;
    App::PropertyFloatList UnderlayRect;

    short mustExecute() const override;
    App::DocumentObjectExecReturn* execute() override;
    const char* getViewProviderName() const override { return "TechDrawGui::ViewProviderViewPart"; }
    PyObject* getPyObject() override;

    static TopoDS_Shape centerScaleRotate(DrawViewPart* dvp, TopoDS_Shape& inOutShape,
                                          Base::Vector3d centroid);

    std::vector<TechDraw::DrawHatch*> getHatches() const;
    std::vector<TechDraw::DrawGeomHatch*> getGeomHatches() const;
    std::vector<TechDraw::DrawViewDimension*> getDimensions() const;
    std::vector<TechDraw::DrawViewBalloon*> getBalloons() const;
    virtual std::vector<DrawViewSection*> getSectionRefs() const;
    virtual std::vector<DrawViewDetail*> getDetailRefs() const;

    const std::vector<TechDraw::VertexPtr> getVertexGeometry() const;
    const BaseGeomPtrVector getEdgeGeometry() const;
    const BaseGeomPtrVector getVisibleFaceEdges() const;
    const std::vector<TechDraw::FacePtr> getFaceGeometry() const;

    bool hasGeometry() const;
    TechDraw::GeometryObjectPtr getGeometryObject(bool noException=false) const;

    TechDraw::VertexPtr getVertex(std::string vertexName) const;
    TechDraw::BaseGeomPtr getEdge(std::string edgeName) const;
    TechDraw::FacePtr getFace(std::string faceName) const;

    //get existing geom for edge idx in projection
    TechDraw::BaseGeomPtr getGeomByIndex(int idx) const;
    //get existing geom for vertex idx in projection
    TechDraw::VertexPtr getProjVertexByIndex(int idx) const;
    // get existing geom for vertex by unique tag
    TechDraw::VertexPtr getProjVertexByCosTag(std::string cosTag);
    //get edges for face idx in projection
    std::vector<TechDraw::BaseGeomPtr> getFaceEdgesByIndex(int idx) const;
    // get the wires that define face idx
    virtual std::vector<TopoDS_Wire> getWireForFace(int idx) const;
    //returns a compound of all the visible projected edges
    TopoDS_Shape getEdgeCompound() const;

    // projected geometry measurements
    virtual Base::BoundBox3d getBoundingBox() const;
    double getBoxX() const;
    double getBoxY() const;
    QRectF getRect() const override;
    double getSizeAlongVector(Base::Vector3d alignmentVector);

    // ancillary projection routines
    virtual Base::Vector3d projectPoint(const Base::Vector3d& pt, bool invert = true) const;
    virtual Base::Vector3d inverseProjectPoint(const Base::Vector3d& pt, bool invert=true) const;
    virtual BaseGeomPtr projectEdge(const TopoDS_Edge& e) const;

    virtual gp_Ax2 getViewAxis(const Base::Vector3d& pt, const Base::Vector3d& direction,
                               const bool flip = true) const;
    virtual gp_Ax2 getProjectionCS(Base::Vector3d pt = Base::Vector3d(0.0, 0.0, 0.0)) const;
    virtual gp_Ax2 getRotatedCS(Base::Vector3d basePoint = Base::Vector3d(0.0, 0.0, 0.0)) const;
    virtual Base::Vector3d getXDirection() const;//don't use XDirection.getValue()
    virtual Base::Vector3d getOriginalCentroid() const;
    virtual Base::Vector3d getCurrentCentroid() const;
    virtual Base::Vector3d getLegacyX(const Base::Vector3d& pt, const Base::Vector3d& axis,
                                      const bool flip = true) const;

    gp_Ax2 localVectorToCS(const Base::Vector3d localUnit) const;
    Base::Vector3d localVectorToDirection(const Base::Vector3d localUnit) const;

    static TopoDS_Shape shapeShapeIntersect(const TopoDS_Shape& shape0,
                                            const TopoDS_Shape& shape1,
                                            Handle(Message_ProgressIndicator) pi = nullptr);
    static bool isTrulyEmpty(TopoDS_Shape inShape);

    // switches
    static bool handleFaces();
    static bool newFaceFinder();

    bool isUnsetting() { return nowUnsetting; }

    //! the compound of the source shapes, carrying their element maps
    //! (docs/TopoNamingEnhance.md section 3)
    virtual Part::TopoShape getSourceShape(bool fuse = false) const;
    virtual TopoDS_Shape getShapeForDetail() const;
    //! The exact transform mapping getShapeForDetail()'s result frame
    //! back to the global (source) frame -- the inverse of whatever
    //! rotation/centering that method applied, composed with the frame
    //! of the shape it started from (doc sec 31).  False when no rigid
    //! map exists (an aligned complex section's unfolded fiction) or
    //! the frame is not known yet.
    virtual bool getShapeForDetailFrame(gp_Trsf& frame) const;
    std::vector<App::DocumentObject*> getAllSources() const;

    // debug routines
    void dumpVerts(const std::string text);
    void dumpCosVerts(const std::string text);
    void dumpCosEdges(const std::string text);

    // routines related to landmark dimensions (obs?)
    std::string addReferenceVertex(Base::Vector3d v);
    void addReferencesToGeom();
    void removeReferenceVertex(std::string tag);
    void updateReferenceVert(std::string tag, Base::Vector3d loc2d);
    void removeAllReferencesFromGeom();
    void resetReferenceVerts();

    // routines related to multi-threading
    virtual void postHlrTasks();
    virtual void postFaceExtractionTasks();
    bool waitingForFaces() const { return m_waitingForFaces; }
    bool waitingForHlr() const { return m_waitingForHlr; }
    virtual bool waitingForResult() const;

protected:
    void onHlrFinished(GeometryObjectPtr result);
    void onFacesFinished(std::shared_ptr<std::vector<FacePtr>> faces);
    void abortMakeGeometry();
    void waitingForFaces(bool s) { m_waitingForFaces = s; }
    void waitingForHlr(bool s) { m_waitingForHlr = s; }

    bool checkXDirection() const;

    TechDraw::GeometryObjectPtr m_geometryObject;
    Base::BoundBox3d bbox;

    void onChanged(const App::Property* prop) override;
    void unsetupObject() override;

    void buildGeometryObject(TopoDS_Shape& shape, const gp_Ax2& viewAxis);
    void makeGeometryForShape(TopoDS_Shape& shape);//const??
    void partExec(TopoDS_Shape& shape);

    struct ExtractFaceParams {
        std::string featureName;
        std::shared_ptr<Base::SequencerLauncher> progress;
        std::vector<BaseGeomPtr> goEdges;
        std::shared_ptr<std::vector<FacePtr>> faces;
    };
    static void extractFaces(const ExtractFaceParams &params);

    virtual void addPoints();

    static void findFacesNew(const ExtractFaceParams &params,
                             const std::vector<TechDraw::BaseGeomPtr>& goEdges);
    static void findFacesOld(const ExtractFaceParams &params,
                             const std::vector<TechDraw::BaseGeomPtr>& goEdges);

    Base::Vector3d shapeCentroid;

    bool m_handleFaces;

    Part::TopoShape m_saveShape;  //TODO: make this a Property.  Part::TopoShapeProperty??
    Base::Vector3d m_saveCentroid;//centroid before centering shape in origin

    std::vector<TechDraw::VertexPtr> m_referenceVerts;

private:
    bool nowUnsetting = false;
    bool m_waitingForFaces = false;
    bool m_waitingForHlr = false;

    std::unique_ptr<QFutureWatcher<void>> m_hlrWatcher;
    std::unique_ptr<QFutureWatcher<void>> m_faceWatcher;
    std::shared_ptr<Base::SequencerLauncher> m_progress;
};

using DrawViewPartPython = App::FeaturePythonT<DrawViewPart>;

}//namespace TechDraw

#endif// #ifndef DrawViewPart_h_
