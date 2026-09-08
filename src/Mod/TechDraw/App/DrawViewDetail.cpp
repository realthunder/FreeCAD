/***************************************************************************
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

#include "PreCompiled.h"

#ifndef _PreComp_
#include <BRepAlgoAPI_Common.hxx>
#include <BRepBndLib.hxx>
#include <BRepBuilderAPI_Copy.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRepProj_Projection.hxx>
#include <BRepTools.hxx>
#include <BRep_Builder.hxx>
#include <Bnd_Box.hxx>
#include <Message_ProgressIndicator.hxx>
#include <QtConcurrentRun>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>
#include <TopoDS_Shell.hxx>
#include <TopoDS_Solid.hxx>
#include <gp_Ax2.hxx>
#include <gp_Dir.hxx>
#include <gp_Pln.hxx>
#include <gp_Pnt.hxx>
#include <sstream>
#endif

#include <App/Application.h>
#include <App/Document.h>
#include <Base/Console.h>
#include <Base/Parameter.h>
#include <Base/Sequencer.h>
#include <Mod/Part/App/ProgressIndicator.h>

#include "DrawComplexSection.h"
#include "DrawUtil.h"
#include "DrawViewDetail.h"
#include "DrawViewSection.h"
#include "GeometryObject.h"
#include "Preferences.h"
#include "ShapeUtils.h"


using namespace TechDraw;

//===========================================================================
// DrawViewDetail
//===========================================================================

PROPERTY_SOURCE(TechDraw::DrawViewDetail, TechDraw::DrawViewPart)

DrawViewDetail::DrawViewDetail() : m_saveDvp(nullptr), m_saveDvs(nullptr)
{
    static const char* dgroup = "Detail";

    ADD_PROPERTY_TYPE(BaseView, (nullptr), dgroup, App::Prop_None,
                      "2D View source for this Section");
    BaseView.setScope(App::LinkScope::Global);
    ADD_PROPERTY_TYPE(AnchorPoint, (0, 0, 0), dgroup, App::Prop_None,
                      "Location of detail in BaseView");
    ADD_PROPERTY_TYPE(Radius, (10.0), dgroup, App::Prop_None, "Size of detail area");
    ADD_PROPERTY_TYPE(Reference, ("1"), dgroup, App::Prop_None, "An identifier for this detail");

    static const char* agroup{"Appearance"};
    ADD_PROPERTY_TYPE(ShowMatting, (Preferences::showDetailMatting()), agroup, App::Prop_None,
             "Show or hide the matting around the detail view");
    ADD_PROPERTY_TYPE(ShowHighlight, (Preferences::showDetailHighlight()), agroup, App::Prop_None,
             "Show or hide the detail highlight in the source view");


    getParameters();
    m_fudge = 1.01;

    //hide Properties not relevant to DVDetail
    Direction.setStatus(App::Property::ReadOnly, true);//Should be same as BaseView
    Rotation.setStatus(App::Property::ReadOnly, true); //same as BaseView
    ScaleType.setValue("Custom");                      //dvd uses scale from BaseView
}

DrawViewDetail::~DrawViewDetail()
{
    abortMakeDetail();
}

short DrawViewDetail::mustExecute() const
{
    if (isRestoring()) {
        TechDraw::DrawView::mustExecute();
    }

    if (AnchorPoint.isTouched() || Radius.isTouched() || BaseView.isTouched()) {
        return 1;
    }

    return TechDraw::DrawView::mustExecute();
}

void DrawViewDetail::onChanged(const App::Property* prop)
{
    if (isRestoring()) {
        DrawView::onChanged(prop);
        return;
    }

    if (prop == &Reference) {
        std::string lblText = "Detail " + std::string(Reference.getValue());
        Label.setValue(lblText);
    }

    DrawViewPart::onChanged(prop);
}

App::DocumentObjectExecReturn* DrawViewDetail::execute()
{
    //    Base::Console().Message("DVD::execute() - %s\n", getNameInDocument());
    if (!keepUpdated()) {
        return DrawView::execute();
    }

    App::DocumentObject* baseObj = BaseView.getValue();
    if (!baseObj) {
        return DrawView::execute();
    }

    if (!baseObj->isDerivedFrom<TechDraw::DrawViewPart>()) {
        //this can only happen via scripting?
        return DrawView::execute();
    }

    DrawViewPart* dvp = static_cast<DrawViewPart*>(baseObj);
    TopoDS_Shape shape = dvp->getShapeForDetail();
    DrawViewSection* dvs = nullptr;
    if (dvp->isDerivedFrom(TechDraw::DrawViewSection::getClassTypeId())) {
        dvs = static_cast<TechDraw::DrawViewSection*>(dvp);
    }

    if (shape.IsNull()) {
        return DrawView::execute();
    }

    bool haveX = checkXDirection();
    if (!haveX) {
        //block touch/onChanged stuff
        Base::Vector3d newX = getXDirection();
        XDirection.setValue(newX);
        XDirection.purgeTouched();//don't trigger updates!
        //unblock
    }

    detailExec(shape, dvp, dvs);

    dvp->requestPaint();//to refresh detail highlight in base view
    return DrawView::execute();
}

void DrawViewDetail::abortMakeDetail()
{
    if (m_progress) {
        m_progress->setCanceled(true);
        m_progress.reset();
    }
    m_detailWatcher.reset();
    waitingForDetail(false);
}

//try to create a detail of the solids & shells in shape
//if there are no solids/shells in shape, use the edges in shape
void DrawViewDetail::detailExec(const TopoDS_Shape& shape, DrawViewPart* dvp, DrawViewSection* dvs)
{
    abortMakeDetail();

    try {
        m_detailWatcher.reset(new QFutureWatcher<void>());
        std::shared_ptr<Output> output(new Output());

        std::string message(Label.getValue());
        message += QT_TRANSLATE_NOOP("TechDraw", " making details...");
        std::shared_ptr<Base::SequencerLauncher> progress(new Base::SequencerLauncher(message.c_str()));
        m_progress = progress;

        m_detailWatcher.reset(new QFutureWatcher<void>());
        QObject::connect(m_detailWatcher.get(), &QFutureWatcherBase::finished, m_detailWatcher.get(),
            [=] {
                if (m_progress && !m_progress->wasCanceled())
                    onMakeDetailFinished(output);
                else
                    abortMakeDetail();
            });

        m_saveShape = BRepBuilderAPI_Copy(shape).Shape();
        m_saveDvp = dvp;
        m_saveDvs = dvs;
        m_viewAxis = dvp->getProjectionCS();//save the CS for later

        DetailParams params;
        params.progress = progress;
        params.output = output;
        params.shape = m_saveShape.getShape();
        params.viewAxis = m_viewAxis;
        params.dirDetail = dvp->Direction.getValue();
        gp_Pnt gpCenter = ShapeUtils::findCentroid(params.shape, params.dirDetail);
        params.shapeCenter = Base::Vector3d(gpCenter.X(), gpCenter.Y(), gpCenter.Z());
        m_saveCentroid = params.shapeCenter;//centroid of original shape
        params.anchorPoint = AnchorPoint.getValue();
        params.radius = getFudgeRadius();
        params.rotation = Rotation.getValue();
        params.scale = getScale();
        params.featureName = getFullName();
        params.moveShape = (dvs == nullptr);

        // The exact build-time frames (doc sec 31), composed from the
        // same params the worker consumes and committed with the shapes
        // in onMakeDetailFinished.
        // m_detailShape lives in the input frame moved by -shapeCenter
        // (when the worker centers at all); the input frame is what the
        // base's getShapeForDetail applied.
        gp_Trsf inputFrame;
        m_pendingFramesValid = dvp->getShapeForDetailFrame(inputFrame);
        m_pendingDetailFrame = inputFrame;
        if (params.moveShape) {
            gp_Trsf uncenter;
            uncenter.SetTranslation(gp_Vec(params.shapeCenter.x, params.shapeCenter.y,
                                           params.shapeCenter.z));
            m_pendingDetailFrame = inputFrame.Multiplied(uncenter);
        }
        // m_scaledShape = rotate(scale(move(detailShape, -anchorR3)));
        // invert each step.
        Base::Vector3d anchorR3 = DrawUtil::toR3(params.viewAxis, params.anchorPoint);
        gp_Trsf unanchor;
        unanchor.SetTranslation(gp_Vec(anchorR3.x, anchorR3.y, anchorR3.z));
        gp_Trsf unscale;
        if (params.scale > 0.0) {
            unscale.SetScale(gp_Pnt(0.0, 0.0, 0.0), 1.0 / params.scale);
        }
        else {
            m_pendingFramesValid = false;
        }
        gp_Trsf unrotate;
        if (!DrawUtil::fpCompare(params.rotation, 0.0)) {
            unrotate.SetRotation(params.viewAxis.Axis(), -params.rotation * M_PI / 180.0);
        }
        m_pendingScaledFrame =
            m_pendingDetailFrame.Multiplied(unanchor.Multiplied(unscale.Multiplied(unrotate)));

        waitingForDetail(true);
        m_detailWatcher->setFuture(QtConcurrent::run(
            [params] {
                try {
                    makeDetailShape(params);
                    return;
                } catch (Base::Exception &e) {
                    e.ReportException();
                    Base::Console().Error("DVP::%s - fail to make detail shape - %s **\n",
                                        params.featureName.c_str(), e.what());
                } catch (Standard_Failure &e) {
                    Base::Console().Error("DVP::%s - to make detail shape - %s **\n",
                                        params.featureName.c_str(), e.GetMessageString());
                }
                params.progress->setCanceled(true);
            }));
    }
    catch (...) {
        Base::Console().Error("DVS::detailExec - failed to make detail shape");
        return;
    }
}

//this runs in a separate thread since it can sometimes take a long time
//make a common of the input shape and a cylinder (or prism depending on
//the matting style)
void DrawViewDetail::makeDetailShape(const DetailParams &params)
{
    auto output = params.output;
    double radius = params.radius;
    const Base::Vector3d &dirDetail = params.dirDetail;

    TopoDS_Shape copyShape = params.shape;

    if (params.moveShape) {
        //section cutShape should already be on origin
        copyShape = ShapeUtils::moveShape(copyShape,//centre shape on origin
                                        -params.shapeCenter);
    }

    Base::Vector3d anchor = params.anchorPoint;//this is a 2D point in base view local coords
    //    double baseRotationRad = dvp->Rotation.getValue() * M_PI / 180.0;
    //    anchor.RotateZ(baseRotationRad);

    anchor = DrawUtil::toR3(params.viewAxis, anchor);//actual anchor coords in R3

    Bnd_Box bbxSource;
    bbxSource.SetGap(0.0);
    BRepBndLib::AddOptimal(copyShape, bbxSource);
    double diag = sqrt(bbxSource.SquareExtent());

    Base::Vector3d toolPlaneOrigin = anchor + dirDetail * diag * -1.0;//center tool about anchor
    double extrudeLength = 2.0 * toolPlaneOrigin.Length();

    gp_Pnt gpnt(toolPlaneOrigin.x, toolPlaneOrigin.y, toolPlaneOrigin.z);
    gp_Dir gdir(dirDetail.x, dirDetail.y, dirDetail.z);

    TopoDS_Face extrusionFace;
    Base::Vector3d extrudeVec = dirDetail * extrudeLength;
    gp_Vec extrudeDir(extrudeVec.x, extrudeVec.y, extrudeVec.z);
    TopoDS_Shape tool;
    if (Preferences::mattingStyle()) {
        //square mat
        gp_Pln gpln(gpnt, gdir);
        BRepBuilderAPI_MakeFace mkFace(gpln, -radius, radius, -radius, radius);
        extrusionFace = mkFace.Face();
        if (extrusionFace.IsNull()) {
            Base::Console().Warning("DVD::makeDetailShape - %s - failed to create tool base face\n",
                                    params.featureName.c_str());
            return;
        }
        tool = BRepPrimAPI_MakePrism(extrusionFace, extrudeDir, false, true).Shape();
        if (tool.IsNull()) {
            Base::Console().Warning("DVD::makeDetailShape - %s - failed to create tool (prism)\n",
                                    params.featureName.c_str());
            return;
        }
    }
    else {
        //circular mat
        gp_Ax2 cs(gpnt, gdir);
        BRepPrimAPI_MakeCylinder mkTool(cs, radius, extrudeLength);
        tool = mkTool.Shape();
        if (tool.IsNull()) {
            Base::Console().Warning("DVD::detailExec - %s - failed to create tool (cylinder)\n",
                                    params.featureName.c_str());
            return;
        }
    }

    Handle(Message_ProgressIndicator) pi = new Part::ProgressIndicator(
            params.progress->numberOfSteps(), params.progress);
    try {
        TopoDS_Shape detailShape = shapeShapeIntersect(copyShape, tool, pi);
        output->detailShape = detailShape;

        if (debugDetail()) {
            BRepTools::Write(tool, "DVDTool.brep");     //debug
            BRepTools::Write(copyShape, "DVDCopy.brep");//debug
            BRepTools::Write(detailShape, "DVDCommon.brep"); //debug
        }

        gp_Pnt inputCenter;
        //centroid of result
        inputCenter = ShapeUtils::findCentroid(detailShape, dirDetail);
        Base::Vector3d centroid(inputCenter.X(), inputCenter.Y(), inputCenter.Z());
        output->centroid = centroid;//center of massaged shape

        //align shape with detail anchor
        TopoDS_Shape centeredShape = ShapeUtils::moveShape(detailShape, anchor * -1.0);
        output->shape = ShapeUtils::scaleShape(centeredShape, params.scale);
        if (debugDetail()) {
            BRepTools::Write(output->shape, "DVDScaled.brep");//debug
        }

        if (!DrawUtil::fpCompare(params.rotation, 0.0)) {
            output->shape = ShapeUtils::rotateShape(output->shape, params.viewAxis, params.rotation);
        }
    }//end try block
    catch (Base::Exception& e1) {
        Base::Console().Message("DVD::makeDetailShape - failed to create detail %s - %s **\n",
                                params.featureName.c_str(), e1.what());
        return;
    }
    catch (Standard_Failure& e1) {
        Base::Console().Message("DVD::makeDetailShape - failed to create detail %s - %s **\n",
                                params.featureName.c_str(), e1.GetMessageString());
        return;
    }
}

void DrawViewDetail::postHlrTasks()
{
    //    Base::Console().Message("DVD::postHlrTasks()\n");
    DrawViewPart::postHlrTasks();

    m_geometryObject->pruneVertexGeom(Base::Vector3d(0.0, 0.0, 0.0),
                                    Radius.getValue()
                                        * getScale());//remove vertices beyond clipradius

    //second pass if required
    if (ScaleType.isValue("Automatic") && !checkFit()) {
        double newScale = autoScale();
        Scale.setValue(newScale);
        Scale.purgeTouched();
        detailExec(m_saveShape.getShape(), m_saveDvp, m_saveDvs);
    }
    overrideKeepUpdated(false);
}

//continue processing after makeDetailShape thread is finished
void DrawViewDetail::onMakeDetailFinished(std::shared_ptr<Output> output)
{
    waitingForDetail(false);
    m_progress.reset();
    m_scaledShape = output->shape;
    m_detailShape = output->detailShape;
    m_saveCentroid += output->centroid;
    // commit the frames beside the shapes they describe (doc sec 31)
    m_detailFrame = m_pendingDetailFrame;
    m_scaledFrame = m_pendingScaledFrame;
    m_detailFrameValid = m_pendingFramesValid;
    m_scaledFrameValid = m_pendingFramesValid;

    //ancestor's buildGeometryObject will run HLR and face finding in a separate thread
    buildGeometryObject(m_scaledShape, m_viewAxis);
}

bool DrawViewDetail::waitingForResult() const
{
    if (DrawViewPart::waitingForResult() || waitingForDetail()) {
        return true;
    }
    return false;
}

TopoDS_Shape DrawViewDetail::projectEdgesOntoFace(TopoDS_Shape& edgeShape, TopoDS_Face& projFace,
                                                  gp_Dir& projDir)
{
    BRep_Builder builder;
    TopoDS_Compound edges;
    builder.MakeCompound(edges);
    TopExp_Explorer Ex(edgeShape, TopAbs_EDGE);
    while (Ex.More()) {
        TopoDS_Edge e = TopoDS::Edge(Ex.Current());
        BRepProj_Projection mkProj(e, projFace, projDir);
        if (mkProj.IsDone()) {
            builder.Add(edges, mkProj.Shape());
        }
        Ex.Next();
    }
    if (debugDetail()) {
        BRepTools::Write(edges, "DVDEdges.brep");//debug
    }

    return TopoDS_Shape(std::move(edges));
}

//we don't want to paint detail highlights on top of detail views,
//so tell the Gui that there are no details for this view
std::vector<DrawViewDetail*> DrawViewDetail::getDetailRefs() const
{
    return std::vector<DrawViewDetail*>();
}

bool DrawViewDetail::getDetailFrame(gp_Trsf& frame) const
{
    frame = m_detailFrame;
    return m_detailFrameValid;
}

bool DrawViewDetail::getScaledFrame(gp_Trsf& frame) const
{
    frame = m_scaledFrame;
    return m_scaledFrameValid;
}

double DrawViewDetail::getFudgeRadius() { return Radius.getValue() * m_fudge; }

bool DrawViewDetail::debugDetail()
{
    return Preferences::getPreferenceGroup("debug")->GetBool("debugDetail", false);
}

void DrawViewDetail::unsetupObject()
{
    //    Base::Console().Message("DVD::unsetupObject()\n");
    App::DocumentObject* baseObj = BaseView.getValue();
    DrawView* base = dynamic_cast<DrawView*>(baseObj);
    if (base) {
        base->requestPaint();
    }
}

void DrawViewDetail::getParameters() {}

// Python Drawing feature ---------------------------------------------------------

namespace App
{
/// @cond DOXERR
PROPERTY_SOURCE_TEMPLATE(TechDraw::DrawViewDetailPython, TechDraw::DrawViewDetail)
template<> const char* TechDraw::DrawViewDetailPython::getViewProviderName() const
{
    return "TechDrawGui::ViewProviderViewPart";
}
/// @endcond

// explicit template instantiation
template class TechDrawExport FeaturePythonT<TechDraw::DrawViewDetail>;
}// namespace App
