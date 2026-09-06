/***************************************************************************
 *   Copyright (c) 2002 Jürgen Riegel <juergen.riegel@web.de>              *
 *   Copyright (c) 2013 Luke Parry <l.parry@warwick.ac.uk>                 *
 *   Copyright (c) 2016, 2022 WandererFan <wandererfan@gmail.com>          *
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

// DrawViewSection processing overview

// execute
//     sectionExec(getShapeToCut())

// sectionExec
//     makeSectionCut(baseShape)

// makeSectionCut (separate thread)
//     m_cuttingTool = makeCuttingTool (DVSTool.brep)
//     m_cutPieces = (baseShape - m_cuttingTool) (DVSCutPieces.brep)

// onSectionCutFinished
//     m_preparedShape = prepareShape(m_cutPieces) - centered, scaled, rotated
//     geometryObject = DVP::buildGeometryObject(m_preparedShape)  (HLR)

// postHlrTasks
//     faceIntersections = findSectionPlaneIntersections
//     m_sectionTopoDSFaces = alignSectionFaces(faceIntersections)
//     m_tdSectionFaces = makeTDSectionFaces(m_sectionTopoDSFaces)

#include "PreCompiled.h"

#ifndef _PreComp_
#include <BRepAdaptor_Surface.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepBndLib.hxx>
#include <BRepBuilderAPI_Copy.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRepTools.hxx>
#include <BRep_Builder.hxx>
#include <Bnd_Box.hxx>
#include <QtConcurrentRun>
#include <ShapeAnalysis.hxx>
#include <ShapeFix_Shape.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>
#include <chrono>
#include <gp_Ax2.hxx>
#include <gp_Ax3.hxx>
#include <gp_Dir.hxx>
#include <gp_Pln.hxx>
#include <gp_Pnt.hxx>
#include <sstream>
#endif

#include <App/Document.h>
#include <Base/BoundBox.h>
#include <Base/Console.h>
#include <Base/FileInfo.h>
#include <Base/Parameter.h>

#include <Mod/Part/App/PartFeature.h>
#include <Mod/Part/App/ProgressIndicator.h>
#include <Mod/Part/App/TopoShapeOpCode.h>

#include "DrawGeomHatch.h"
#include "DrawHatch.h"
#include "DrawUtil.h"
#include "DrawViewDetail.h"
#include "EdgeWalker.h"
#include "GeometryObject.h"
#include "Preferences.h"

#include "DrawViewSection.h"
// inclusion of the generated files (generated out of DrawViewSectionPy.xml)
#include <Mod/TechDraw/App/DrawViewSectionPy.h>

using namespace TechDraw;

using DU = DrawUtil;

// class to store geometry of points where the section line changes direction
ChangePoint::ChangePoint(QPointF location, QPointF preDirection, QPointF postDirection)
{
    m_location = location;
    m_preDirection = preDirection;
    m_postDirection = postDirection;
}

ChangePoint::ChangePoint(gp_Pnt location, gp_Dir preDirection, gp_Dir postDirection)
{
    m_location.setX(location.X());
    m_location.setY(location.Y());
    m_preDirection.setX(preDirection.X());
    m_preDirection.setY(preDirection.Y());
    m_postDirection.setX(postDirection.X());
    m_postDirection.setY(postDirection.Y());
}

void ChangePoint::scale(double scaleFactor)
{
    m_location = m_location * scaleFactor;
}

const char* DrawViewSection::SectionDirEnums[] =
    {"Right", "Left", "Up", "Down", "Aligned", nullptr};

const char* DrawViewSection::CutSurfaceEnums[] = {"Hide", "Color", "SvgHatch", "PatHatch", nullptr};

//===========================================================================
// DrawViewSection
//===========================================================================

PROPERTY_SOURCE(TechDraw::DrawViewSection, TechDraw::DrawViewPart)

DrawViewSection::DrawViewSection()
    : m_shapeSize(0.0)
    , m_waitingForCut(false)
{
    static const char* sgroup = "Section";
    static const char* fgroup = "Cut Surface Format";
    static const char* ggroup = "Cut Operation";
    static const char* agroup = "Appearance";

    // general section properties
    ADD_PROPERTY_TYPE(SectionSymbol,
                      (""),
                      sgroup,
                      App::Prop_None,
                      "The identifier for this section");
    ADD_PROPERTY_TYPE(BaseView,
                      (nullptr),
                      sgroup,
                      App::Prop_None,
                      "2D View source for this Section");
    BaseView.setScope(App::LinkScope::Global);
    ADD_PROPERTY_TYPE(SectionNormal,
                      (0, 0, 1.0),
                      sgroup,
                      App::Prop_None,
                      "Section Plane normal direction");// direction of extrusion
                                                        // of cutting prism
    ADD_PROPERTY_TYPE(SectionOrigin, (0, 0, 0), sgroup, App::Prop_None, "Section Plane Origin");

    // TODO: SectionDirection is a legacy from when SectionViews were only
    // available along cardinal directions.  It should be made obsolete and
    // replaced with Aligned sections and local unit vectors.
    SectionDirection.setEnums(SectionDirEnums);
    ADD_PROPERTY_TYPE(SectionDirection,
                      ((long)0),
                      sgroup,
                      App::Prop_None,
                      "Orientation of this Section in the Base View");

    // properties related to the cut operation
    ADD_PROPERTY_TYPE(FuseBeforeCut,
                      (false),
                      ggroup,
                      App::Prop_None,
                      "Merge Source(s) into a single shape before cutting");
    ADD_PROPERTY_TYPE(TrimAfterCut,
                      (false),
                      ggroup,
                      App::Prop_None,
                      "Trim the resulting shape after the section cut");
    ADD_PROPERTY_TYPE(UsePreviousCut,
                      (Preferences::SectionUsePreviousCut()),
                      ggroup,
                      App::Prop_None,
                      "Use the cut shape from the base view instead of the original object");

    // properties related to the display of the cut surface
    CutSurfaceDisplay.setEnums(CutSurfaceEnums);
    ADD_PROPERTY_TYPE(CutSurfaceDisplay,
                      (prefCutSurface()),
                      fgroup,
                      App::Prop_None,
                      "Appearance of Cut Surface");
    ADD_PROPERTY_TYPE(FileHatchPattern,
                      (DrawHatch::prefSvgHatch()),
                      fgroup,
                      App::Prop_None,
                      "The hatch pattern file for the cut surface");
    ADD_PROPERTY_TYPE(FileGeomPattern,
                      (DrawGeomHatch::prefGeomHatchFile()),
                      fgroup,
                      App::Prop_None,
                      "The PAT pattern file for geometric hatching");

    ADD_PROPERTY_TYPE(SvgIncluded,
                      (""),
                      fgroup,
                      App::Prop_None,
                      "Embedded Svg hatch file. System use only.");// n/a to end users
    ADD_PROPERTY_TYPE(PatIncluded,
                      (""),
                      fgroup,
                      App::Prop_None,
                      "Embedded Pat pattern file. System use only.");// n/a to end users
    ADD_PROPERTY_TYPE(NameGeomPattern,
                      (DrawGeomHatch::prefGeomHatchName()),
                      fgroup,
                      App::Prop_None,
                      "The pattern name for geometric hatching");
    ADD_PROPERTY_TYPE(HatchScale, (1.0), fgroup, App::Prop_None, "Hatch pattern size adjustment");
    ADD_PROPERTY_TYPE(HatchRotation,
                      (0.0),
                      fgroup,
                      App::Prop_None,
                      "Rotation of hatch pattern in degrees anti-clockwise");
    ADD_PROPERTY_TYPE(HatchOffset, (0.0, 0.0, 0.0), fgroup, App::Prop_None, "Hatch pattern offset");

    ADD_PROPERTY_TYPE(SectionLineStretch, (1.0), agroup, App::Prop_None,
                      "Adjusts the length of the section line.  1.0 is normal length.  1.1 would be 10% longer, 0.9 would be 10% shorter.");

    getParameters();

    std::string hatchFilter("Svg files (*.svg *.SVG);;All files (*)");
    FileHatchPattern.setFilter(hatchFilter);
    hatchFilter = ("PAT files (*.pat *.PAT);;All files (*)");
    FileGeomPattern.setFilter(hatchFilter);

    SvgIncluded.setStatus(App::Property::ReadOnly, true);
    PatIncluded.setStatus(App::Property::ReadOnly, true);
    // SectionNormal is used instead to Direction
    Direction.setStatus(App::Property::ReadOnly, true);
    SectionDirection.setStatus(App::Property::Hidden, true);
    SectionDirection.setStatus(App::Property::ReadOnly, true);
}

DrawViewSection::~DrawViewSection()
{
    abortSectionCut();
}

short DrawViewSection::mustExecute() const
{
    if (isRestoring()) {
        return TechDraw::DrawView::mustExecute();
    }

    if (Scale.isTouched() || Direction.isTouched() || BaseView.isTouched()
        || SectionNormal.isTouched() || SectionOrigin.isTouched() || Rotation.isTouched()) {
        return 1;
    }

    return TechDraw::DrawView::mustExecute();
}

void DrawViewSection::onChanged(const App::Property* prop)
{
    if (isRestoring()) {
        DrawViewPart::onChanged(prop);
        return;
    }

    App::Document* doc = getDocument();
    if (!doc) {
        // tarfu
        DrawViewPart::onChanged(prop);
        return;
    }

    if (prop == &SectionNormal) {
        Direction.setValue(SectionNormal.getValue());
        return;
    }
    else if (prop == &SectionSymbol) {
        if (getBaseDVP()) {
            getBaseDVP()->requestPaint();
        }
        return;
    }
    else if (prop == &CutSurfaceDisplay) {
        if (CutSurfaceDisplay.isValue("PatHatch")) {
            makeLineSets();
        }
        requestPaint();
        return;
    }
    else if (prop == &FileHatchPattern) {
        replaceSvgIncluded(FileHatchPattern.getValue());
        requestPaint();
        return;
    }
    else if (prop == &FileGeomPattern) {
        replacePatIncluded(FileGeomPattern.getValue());
        makeLineSets();
        requestPaint();
        return;
    }
    else if (prop == &NameGeomPattern) {
        makeLineSets();
        requestPaint();
        return;
    }
    else if (prop == &BaseView) {
        // if the BaseView is a Section, then the option of using UsePreviousCut is
        // valid.
        if (BaseView.getValue() && BaseView.getValue()->isDerivedFrom<TechDraw::DrawViewSection>()) {
            UsePreviousCut.setStatus(App::Property::ReadOnly, false);
        }
        else {
            UsePreviousCut.setStatus(App::Property::ReadOnly, true);
        }
    } else if (prop == &SectionLineStretch) {
        BaseView.getValue()->touch();
    }

    DrawView::onChanged(prop);
}

Part::TopoShape DrawViewSection::getShapeToCut()
{
    //    Base::Console().Message("DVS::getShapeToCut() - %s\n",
    //    getNameInDocument());
    App::DocumentObject* base = BaseView.getValue();
    TechDraw::DrawViewPart* dvp = nullptr;
    TechDraw::DrawViewSection* dvs = nullptr;
    TechDraw::DrawViewDetail* dvd = nullptr;
    if (!base) {
        return Part::TopoShape();
    }

    Part::TopoShape shapeToCut;
    if (base->isDerivedFrom<TechDraw::DrawViewSection>()) {
        dvs = static_cast<TechDraw::DrawViewSection*>(base);
        shapeToCut = dvs->getShapeToCut();
        if (UsePreviousCut.getValue()) {
            shapeToCut = dvs->getCutShapeRaw();
        }
    }
    else if (base->isDerivedFrom<TechDraw::DrawViewDetail>()) {
        dvd = static_cast<TechDraw::DrawViewDetail*>(base);
        shapeToCut = dvd->getDetailShape();
    }
    else if (base->isDerivedFrom<TechDraw::DrawViewPart>()) {
        dvp = static_cast<TechDraw::DrawViewPart*>(base);
        shapeToCut = dvp->getSourceShape();
        if (FuseBeforeCut.getValue()) {
            shapeToCut = dvp->getSourceShape(true);
        }
    }
    else {
        Base::Console().Message("DVS::getShapeToCut - base is weird\n");
        return Part::TopoShape();
    }
    return shapeToCut;
}

TopoDS_Shape DrawViewSection::getShapeForDetail() const
{
    return ShapeUtils::rotateShape(getCutShape(), getProjectionCS(), Rotation.getValue());
}

gp_Trsf DrawViewSection::getShapeToCutFrame(bool& valid) const
{
    // Mirrors getShapeToCut()'s branches: the frame of the shape it
    // would return, read off the BaseView chain's stored frames.
    App::DocumentObject* base = BaseView.getValue();
    if (base && base->isDerivedFrom<TechDraw::DrawViewSection>()) {
        auto* dvs = static_cast<TechDraw::DrawViewSection*>(base);
        if (UsePreviousCut.getValue()) {
            gp_Trsf frame;
            valid = dvs->getCutShapeFrame(frame);
            return frame;
        }
        return dvs->getShapeToCutFrame(valid);
    }
    if (base && base->isDerivedFrom<TechDraw::DrawViewDetail>()) {
        auto* dvd = static_cast<TechDraw::DrawViewDetail*>(base);
        gp_Trsf frame;
        valid = dvd->getDetailFrame(frame);
        return frame;
    }
    // a plain base view's source shape is in the global frame
    valid = base && base->isDerivedFrom<TechDraw::DrawViewPart>();
    return gp_Trsf();
}

bool DrawViewSection::getCutShapeFrame(gp_Trsf& frame) const
{
    frame = m_cutFrame;
    return m_cutFrameValid;
}

bool DrawViewSection::getPreparedFrame(gp_Trsf& frame) const
{
    frame = m_preparedFrame;
    return m_preparedFrameValid;
}

bool DrawViewSection::getShapeForDetailFrame(gp_Trsf& frame) const
{
    // getShapeForDetail rotates the CENTERED cut shape (m_cutShape =
    // raw moved by -m_saveCentroid) by +Rotation about the projection
    // CS axis; invert both steps, then map raw -> global.
    gp_Trsf unrotate;
    if (!DrawUtil::fpCompare(Rotation.getValue(), 0.0)) {
        unrotate.SetRotation(getProjectionCS().Axis(), -Rotation.getValue() * M_PI / 180.0);
    }
    gp_Trsf uncenter;
    uncenter.SetTranslation(gp_Vec(m_saveCentroid.x, m_saveCentroid.y, m_saveCentroid.z));
    frame = m_cutFrame.Multiplied(uncenter.Multiplied(unrotate));
    return m_cutFrameValid;
}

App::DocumentObjectExecReturn* DrawViewSection::execute()
{
    //    Base::Console().Message("DVS::execute() - %s\n", getNameInDocument());
    if (!keepUpdated()) {
        return App::DocumentObject::StdReturn;
    }

    if (!isBaseValid()) {
        return new App::DocumentObjectExecReturn("BaseView object not found");
    }

    Part::TopoShape baseShape = getShapeToCut();

    if (baseShape.isNull()) {
        return DrawView::execute();
    }

    // is SectionOrigin valid?
    Bnd_Box centerBox;
    BRepBndLib::AddOptimal(baseShape.getShape(), centerBox);
    centerBox.SetGap(0.0);
    Base::Vector3d orgPnt = SectionOrigin.getValue();

    if (!isReallyInBox(gp_Pnt(orgPnt.x, orgPnt.y, orgPnt.z), centerBox)) {
        Base::Console().Warning("DVS: SectionOrigin doesn't intersect part in %s\n",
                                getNameInDocument());
    }

    // save important info for later use
    m_shapeSize = sqrt(centerBox.SquareExtent());
    m_saveShape = baseShape;
    // The frame of the cut input, captured with the shape itself (doc
    // sec 31); committed to m_cutFrame beside m_cutShapeRaw when the
    // async cut lands in prepareShape.
    m_pendingCutFrame = getShapeToCutFrame(m_pendingCutFrameValid);

    bool haveX = checkXDirection();
    if (!haveX) {
        // block touch/onChanged stuff
        Base::Vector3d newX = getXDirection();
        XDirection.setValue(newX);
        XDirection.purgeTouched();// don't trigger updates!
                                  // unblock
    }

    sectionExec(baseShape);

    return DrawView::execute();
}

bool DrawViewSection::isBaseValid() const
{
    App::DocumentObject* base = BaseView.getValue();
    if (base && base->isDerivedFrom<TechDraw::DrawViewPart>()) {
        return true;
    }
    return false;
}

void DrawViewSection::sectionExec(Part::TopoShape& baseShape)
{
    if (baseShape.isNull()) {
        // should be caught before this
        return;
    }

    makeSectionCut(baseShape);
}

void DrawViewSection::abortSectionCut()
{
    if (m_progress) {
        m_progress->setCanceled(true);
        m_progress.reset();
    }
    m_cutWatcher.reset();
    waitingForCut(false);
}

void DrawViewSection::makeSectionCut(const Part::TopoShape &baseShape)
{
    abortSectionCut();

    try {
        m_cutWatcher.reset(new QFutureWatcher<void>());
        std::shared_ptr<TopoDS_Shape> cutPieces(new TopoDS_Shape());

        std::string message(Label.getValue());
        message += QT_TRANSLATE_NOOP("TechDraw", " making section cut...");
        std::shared_ptr<Base::SequencerLauncher> progress(new Base::SequencerLauncher(message.c_str()));
        m_progress = progress;

        m_cutWatcher.reset(new QFutureWatcher<void>());
        QObject::connect(m_cutWatcher.get(), &QFutureWatcherBase::finished, m_cutWatcher.get(),
            [=] {
                if (m_progress && !m_progress->wasCanceled())
                    onSectionCutFinished(cutPieces);
                else
                    abortSectionCut();
            });

        SectionParams params;
        params.progress = progress;
        params.featureName = getFullName();
        params.output = cutPieces;
        params.history = std::make_shared<CutHistory>();
        m_cutHistory = params.history;
        // the copy keeps the element map: makECopy, not BRepBuilderAPI_Copy
        params.baseShape = baseShape.makECopy();
        m_saveShape = params.baseShape;//save shape for 2nd pass
        params.cuttingTool = makeCuttingTool(m_shapeSize);
        params.history->tool = params.cuttingTool;
        m_cuttingTool = params.cuttingTool;
        params.trimAfterCut = TrimAfterCut.getValue();

        waitingForCut(true);
        m_cutWatcher->setFuture(QtConcurrent::run(
            [params] {
                try {
                    doSectionCut(params);
                    return;
                } catch (Base::Exception &e) {
                    e.ReportException();
                    Base::Console().Error("DVP::%s - section cut failed - %s **\n",
                                        params.featureName.c_str(), e.what());
                } catch (Standard_Failure &e) {
                    Base::Console().Error("DVP::%s - section cut failed - %s **\n",
                                        params.featureName.c_str(), e.GetMessageString());
                }
                params.progress->setCanceled(true);
            }));
    }
    catch (...) {
        Base::Console().Error("DVS::sectionExec - failed to make section cut");
        return;
    }
}

void DrawViewSection::doSectionCut(const SectionParams &params)
{
    if (debugSection()) {
        BRepTools::Write(params.baseShape.getShape(), "DVSCopy.brep");//debug
    }

    if (debugSection()) {
        BRepTools::Write(params.cuttingTool.getShape(), "DVSTool.brep");//debug
    }

    auto progress = params.progress;
    Handle(Message_ProgressIndicator) pi = new Part::ProgressIndicator(
            progress->numberOfSteps(), progress);

    //perform the cut. We cut each solid in baseShape individually to avoid issues where
    //a compound BaseShape does not cut correctly.
    BRep_Builder builder;
    TopoDS_Compound cutPieces;
    builder.MakeCompound(cutPieces);
    // The makers are kept in params.history: the element names are mapped from
    // them on the main thread, since the hasher they would write to is shared
    // with the document and has no locking (CutHistory in the header).
    const TopoDS_Shape& tool = params.cuttingTool.getShape();
    for (auto& solid : params.baseShape.getSubTopoShapes(TopAbs_SOLID)) {
        const TopoDS_Shape& s = solid.getShape();
#if OCC_VERSION_HEX < 0x070600
        auto mkCut = std::make_shared<BRepAlgoAPI_Cut>(s, tool);
#   if OCC_VERSION_HEX < 0x070500
        mkCut->SetProgressIndicator(pi);
        pi->NewScope(100, progress->text().c_str());
        pi->Show();
#   else
        Message_ProgressScope scope(pi->Start(), progress->text().c_str(), 100);
        mkCut->SetProgressIndicator(scope);
#   endif
#else
        auto mkCut = std::make_shared<BRepAlgoAPI_Cut>(s, tool, pi->Start());
#endif
        if (!mkCut->IsDone()) {
            Base::Console().Warning("DVS: Section cut of some solid has failed in %s\n", params.featureName.c_str());
        }
        else {
            builder.Add(cutPieces, mkCut->Shape());
            if (params.history) {
                params.history->sources.push_back(solid);
                params.history->makers.push_back(mkCut);
            }
        }
#if OCC_VERSION_HEX < 0x070500
        pi->EndScope();
#endif
    }

    // cutPieces contains result of cutting each subshape in baseShape with tool
    *params.output = cutPieces;
    if (debugSection()) {
        BRepTools::Write(cutPieces, "DVSCutPieces1.brep");// debug
    }

    //second cut if requested.  Sometimes the first cut includes extra uncut pieces.
    if (params.trimAfterCut) {
        progress->setText((progress->text() + QT_TRANSLATE_NOOP("TechDraw", " (second pass)")).c_str());

#if OCC_VERSION_HEX < 0x070600
        auto mkCut2 = std::make_shared<BRepAlgoAPI_Cut>(cutPieces, tool);
#   if OCC_VERSION_HEX < 0x070500
        mkCut2->SetProgressIndicator(pi);
        pi->NewScope(100, progress->text().c_str());
        pi->Show();
#   else
        Message_ProgressScope scope(pi->Start(), progress->text().c_str(), 100);
        mkCut2->SetProgressIndicator(scope);
#   endif
#else
        auto mkCut2 = std::make_shared<BRepAlgoAPI_Cut>(cutPieces, tool, pi->Start());
#endif
        if (mkCut2->IsDone()) {
            *params.output = mkCut2->Shape();
            if (params.history) {
                params.history->trim = mkCut2;
            }
            if (debugSection()) {
                BRepTools::Write(*params.output, "DVSCutPieces2.brep");//debug
            }
        }

#if OCC_VERSION_HEX < 0x070500
        pi->EndScope();
#endif
    }

    if (params.history) {
        params.history->result = *params.output;
    }

    // check for error in cut
    Bnd_Box testBox;
    BRepBndLib::AddOptimal(*params.output, testBox);
    testBox.SetGap(0.0);

    if (testBox.IsVoid()) {//prism & input don't intersect.  rawShape is garbage, don't bother.
        Base::Console().Warning("DVS::makeSectionCut - prism & input don't intersect - %s\n",
                                params.featureName.c_str());
        return;
    }
}

//! position, scale and rotate shape for  buildGeometryObject
//! save the cut shape for further processing
Part::TopoShape DrawViewSection::prepareShape(const Part::TopoShape& rawShape, double shapeSize)
{
    //    Base::Console().Message("DVS::prepareShape - %s - rawShape.IsNull: %d
    //    shapeSize: %.3f\n",
    //                            getNameInDocument(), rawShape.IsNull(),
    //                            shapeSize);
    (void)shapeSize;// shapeSize is not used in this base class, but is
                    // interesting for derived classes
    // build display geometry as in DVP, with minor mods
    Part::TopoShape preparedShape;
    try {
        Base::Vector3d origin(0.0, 0.0, 0.0);
        m_projectionCS = getProjectionCS(origin);
        gp_Pnt inputCenter;
        inputCenter = ShapeUtils::findCentroid(rawShape.getShape(), m_projectionCS);
        Base::Vector3d centroid(inputCenter.X(), inputCenter.Y(), inputCenter.Z());

        m_cutShapeRaw = rawShape;
        // commit the frame beside the shape it describes (doc sec 31)
        m_cutFrame = m_pendingCutFrame;
        m_cutFrameValid = m_pendingCutFrameValid;
        preparedShape = ShapeUtils::moveShape(rawShape, centroid * -1.0);
        m_cutShape = preparedShape.getShape();
        m_saveCentroid = centroid;

        preparedShape = ShapeUtils::scaleShape(preparedShape, getScale());

        if (!DrawUtil::fpCompare(Rotation.getValue(), 0.0)) {
            preparedShape =
                ShapeUtils::rotateShape(preparedShape, m_projectionCS, Rotation.getValue());
        }

        // m_preparedShape -> global: invert rotate, then scale, then
        // centering, then map the raw frame out.
        gp_Trsf unrotate;
        if (!DrawUtil::fpCompare(Rotation.getValue(), 0.0)) {
            unrotate.SetRotation(m_projectionCS.Axis(), -Rotation.getValue() * M_PI / 180.0);
        }
        gp_Trsf unscale;
        if (getScale() > 0.0) {
            unscale.SetScale(gp_Pnt(0.0, 0.0, 0.0), 1.0 / getScale());
        }
        gp_Trsf uncenter;
        uncenter.SetTranslation(gp_Vec(centroid.x, centroid.y, centroid.z));
        m_preparedFrame = m_cutFrame.Multiplied(uncenter.Multiplied(unscale.Multiplied(unrotate)));
        m_preparedFrameValid = m_cutFrameValid && getScale() > 0.0;
        if (debugSection()) {
            BRepTools::Write(m_cutShape, "DVSCutShape.brep");// debug
            //            DrawUtil::dumpCS("DVS::makeSectionCut - CS to GO",
            //            viewAxis);
        }
    }
    catch (Standard_Failure& e1) {
        Base::Console().Error("DVS::prepareShape - failed to build shape %s - %s **\n",
                                getNameInDocument(),
                                e1.GetMessageString());
    }
    return preparedShape;
}

Part::TopoShape DrawViewSection::makeCuttingTool(double shapeSize)
{
    //    Base::Console().Message("DVS::makeCuttingTool(%.3f) - %s\n", shapeSize,
    //    getNameInDocument());
    // Make the extrusion face
    gp_Pln pln = getSectionPlane();
    gp_Dir gpNormal = pln.Axis().Direction();
    BRepBuilderAPI_MakeFace mkFace(pln, -shapeSize, shapeSize, -shapeSize, shapeSize);
    TopoDS_Face aProjFace = mkFace.Face();
    if (aProjFace.IsNull()) {
        return Part::TopoShape();
    }
    if (debugSection()) {
        BRepTools::Write(aProjFace, "DVSSectionFace.brep");// debug
    }
    gp_Vec extrudeDir = shapeSize * gp_Vec(gpNormal);
    Part::TopoShape tool(BRepPrimAPI_MakePrism(aProjFace, extrudeDir, false, true).Shape());
    nameToolFaces(tool, pln);
    return tool;
}

//! Give the prism's faces fixed names, so that the faces the cut creates
//! inherit one.  The names are decided by geometry rather than by the order
//! OCCT built the prism in: the face lying in the section plane is
//! SectionPlane -- the one users hatch, colour and dimension against -- the
//! parallel face behind it is SectionBack, and the four sides are named by
//! the axis of the section plane's own coordinate system they face along.
//! Nothing here depends on shapeSize or on where the plane sits, so moving
//! SectionOrigin leaves every name unchanged.
void DrawViewSection::nameToolFaces(Part::TopoShape& tool, const gp_Pln& sectionPlane)
{
    if (tool.isNull()) {
        return;
    }

    const gp_Ax3& cs = sectionPlane.Position();
    const gp_Dir& normal = cs.Direction();
    const gp_Dir& xDir = cs.XDirection();
    const gp_Dir& yDir = cs.YDirection();
    constexpr double parallel = 0.999;   // ~2.5 degrees, and the faces are axis aligned

    int index = 0;
    for (const auto& face : tool.getSubTopoShapes(TopAbs_FACE)) {
        ++index;
        BRepAdaptor_Surface surface(TopoDS::Face(face.getShape()));
        if (surface.GetType() != GeomAbs_Plane) {
            continue;
        }
        const gp_Pln facePln = surface.Plane();
        const gp_Dir faceDir = facePln.Axis().Direction();
        const gp_Vec offset(sectionPlane.Location(), facePln.Location());

        const char* name = nullptr;
        if (std::fabs(faceDir.Dot(normal)) > parallel) {
            name = std::fabs(offset.Dot(gp_Vec(normal))) < Precision::Confusion()
                       ? "SectionPlane" : "SectionBack";
        }
        else if (std::fabs(faceDir.Dot(xDir)) > parallel) {
            name = offset.Dot(gp_Vec(xDir)) > 0.0 ? "SectionSideXMax" : "SectionSideXMin";
        }
        else if (std::fabs(faceDir.Dot(yDir)) > parallel) {
            name = offset.Dot(gp_Vec(yDir)) > 0.0 ? "SectionSideYMax" : "SectionSideYMin";
        }
        if (!name) {
            continue;
        }

        tool.setElementName(Data::IndexedName::fromConst("Face", index),
                            Data::MappedName(name));
    }
}

//! Map the element names of the cut onto its result.  Called on the main
//! thread with the history the worker recorded: the sources and the OCCT
//! makers, whose Generated/Modified history is what makEShape reads.  Falls
//! back to the unnamed compound whenever there is no history to map -- an
//! aborted cut, a failed boolean, or a derived class that cuts its own way.
Part::TopoShape DrawViewSection::nameCutPieces(const std::shared_ptr<CutHistory>& history,
                                              const TopoDS_Shape& cutPieces) const
{
    if (!history || history->makers.empty() || !history->result.IsSame(cutPieces)) {
        return Part::TopoShape(cutPieces);
    }

    try {
        std::vector<Part::TopoShape> pieces;
        for (size_t i = 0; i < history->makers.size(); ++i) {
            Part::TopoShape piece;
            piece.makEShape(*history->makers[i],
                            {history->sources[i], history->tool},
                            Part::OpCodes::Cut);
            pieces.push_back(piece);
        }

        Part::TopoShape named;
        named.makECompound(pieces);

        if (history->trim) {
            Part::TopoShape trimmed;
            trimmed.makEShape(*history->trim, {named, history->tool}, Part::OpCodes::Cut);
            named = trimmed;
        }

        return named;
    }
    catch (const Base::Exception& e) {
        Base::Console().Warning("DVS::nameCutPieces - %s - naming failed - %s\n",
                                getNameInDocument(), e.what());
    }
    catch (const Standard_Failure& e) {
        Base::Console().Warning("DVS::nameCutPieces - %s - naming failed - %s\n",
                                getNameInDocument(), e.GetMessageString());
    }
    return Part::TopoShape(cutPieces);
}

void DrawViewSection::onSectionCutFinished(std::shared_ptr<TopoDS_Shape> cutPieces)
{
    waitingForCut(false);
    m_progress.reset();
    m_cutPieces = nameCutPieces(m_cutHistory, *cutPieces);
    m_cutHistory.reset();

    m_preparedShape = prepareShape(getShapeToPrepare(), m_shapeSize);
    if (debugSection()) {
        BRepTools::Write(m_preparedShape.getShape(), "DVSPreparedShape.brep");// debug
    }

    postSectionCutTasks();

    //display geometry for cut shape is in geometryObject as in DVP
    buildGeometryObject(m_preparedShape, getProjectionCS());
}

//activities that depend on updated geometry object
void DrawViewSection::postHlrTasks()
{
    //    Base::Console().Message("DVS::postHlrTasks() - %s\n",
    //    getNameInDocument());

    DrawViewPart::postHlrTasks();

    // second pass if required
    if (ScaleType.isValue("Automatic") && !checkFit()) {
        double newScale = autoScale();
        Scale.setValue(newScale);
        Scale.purgeTouched();
        sectionExec(m_saveShape);
    }
    overrideKeepUpdated(false);


    // build section face geometry
    TopoDS_Compound faceIntersections = findSectionPlaneIntersections(getShapeToIntersect());
    if (faceIntersections.IsNull()) {
        requestPaint();
        return;
    }
    if (debugSection()) {
        BRepTools::Write(faceIntersections, "DVSFaceIntersections.brep");// debug
    }

    TopoDS_Shape centeredFaces = ShapeUtils::moveShape(faceIntersections, m_saveCentroid * -1.0);

    TopoDS_Shape scaledSection = ShapeUtils::scaleShape(centeredFaces, getScale());
    if (!DrawUtil::fpCompare(Rotation.getValue(), 0.0)) {
        scaledSection =
            ShapeUtils::rotateShape(scaledSection, getProjectionCS(), Rotation.getValue());
    }

    m_sectionTopoDSFaces = alignSectionFaces(faceIntersections);
    if (debugSection()) {
        BRepTools::Write(m_sectionTopoDSFaces, "DVSTopoSectionFaces.brep");// debug
    }
    m_tdSectionFaces = makeTDSectionFaces(m_sectionTopoDSFaces);

    TechDraw::DrawViewPart* dvp = dynamic_cast<TechDraw::DrawViewPart*>(BaseView.getValue());
    if (dvp) {
        dvp->requestPaint();// to refresh section line
    }
    requestPaint();// this will be a duplicate paint if we are making a
                   // standalone ComplexSection
}

// activities that depend on a valid section cut
void DrawViewSection::postSectionCutTasks()
{
    //    Base::Console().Message("DVS::postSectionCutTasks()\n");
    std::vector<App::DocumentObject*> children = getInList();
    for (auto& c : children) {
        if (c->isDerivedFrom<DrawViewPart>()) {
            // details or sections of this need cut shape
            c->recomputeFeature();
        }
    }
}

bool DrawViewSection::waitingForResult() const
{
    if (DrawViewPart::waitingForResult() || waitingForCut()) {
        return true;
    }
    return false;
}

gp_Pln DrawViewSection::getSectionPlane() const
{
    gp_Ax2 viewAxis = getSectionCS();
    gp_Ax3 viewAxis3(viewAxis);

    return gp_Pln(viewAxis3);
}

//! tries to find the intersection of the section plane with the shape giving a
//! collection of planar faces the original algo finds the intersections first
//! then transforms them to match the centered, rotated and scaled cut shape.
//! Aligned complex sections need to intersect the final cut shape (which in
//! this case is a compound of individual cuts) with the "effective" (flattened)
//! section plane.
TopoDS_Compound DrawViewSection::findSectionPlaneIntersections(const TopoDS_Shape& shape)
{
    //    Base::Console().Message("DVS::findSectionPlaneIntersections() - %s\n",
    //    getNameInDocument());
    if (shape.IsNull()) {
        // this shouldn't happen
        Base::Console().Warning(
            "DrawViewSection::findSectionPlaneInter - %s - input shape is Null\n",
            getNameInDocument());
        return TopoDS_Compound();
    }

    gp_Pln plnSection = getSectionPlane();
    if (debugSection()) {
        BRepBuilderAPI_MakeFace mkFace(plnSection,
                                       -m_shapeSize,
                                       m_shapeSize,
                                       -m_shapeSize,
                                       m_shapeSize);
        BRepTools::Write(mkFace.Face(), "DVSSectionPlane.brep");// debug
        BRepTools::Write(shape, "DVSShapeToIntersect.brep)");
    }
    BRep_Builder builder;
    TopoDS_Compound result;
    builder.MakeCompound(result);

    TopExp_Explorer expFaces(shape, TopAbs_FACE);
    for (; expFaces.More(); expFaces.Next()) {
        const TopoDS_Face& face = TopoDS::Face(expFaces.Current());
        BRepAdaptor_Surface adapt(face);
        if (adapt.GetType() == GeomAbs_Plane) {
            gp_Pln plnFace = adapt.Plane();
            if (plnSection.Contains(plnFace.Location(), Precision::Confusion())
                && plnFace.Axis().IsParallel(plnSection.Axis(), Precision::Angular())) {
                builder.Add(result, face);
            }
        }
    }
    return result;
}

// move section faces to line up with cut shape
TopoDS_Compound DrawViewSection::alignSectionFaces(TopoDS_Shape faceIntersections)
{
    //    Base::Console().Message("DVS::alignSectionFaces() - %s -
    //    faceIntersection.isnull: %d\n",
    //                            getNameInDocument(),
    //                            faceIntersections.IsNull());
    TopoDS_Compound sectionFaces;
    TopoDS_Shape centeredShape =
        ShapeUtils::moveShape(faceIntersections, getOriginalCentroid() * -1.0);

    TopoDS_Shape scaledSection = ShapeUtils::scaleShape(centeredShape, getScale());
    if (!DrawUtil::fpCompare(Rotation.getValue(), 0.0)) {
        scaledSection =
            ShapeUtils::rotateShape(scaledSection, getProjectionCS(), Rotation.getValue());
    }

    return mapToPage(scaledSection);
}

TopoDS_Compound DrawViewSection::mapToPage(TopoDS_Shape& shapeToAlign)
{
    // shapeToAlign is compound of TopoDS_Face intersections, but aligned to
    // pln(origin, sectionNormal) needs to be aligned to paper plane (origin,
    // stdZ);
    // project the faces in the shapeToAlign, build new faces from the resulting
    // wires and combine everything into a compound of faces
    //    Base::Console().Message("DVS::mapToPage() - shapeToAlign.null: %d\n",
    //    shapeToAlign.IsNull());
    if (debugSection()) {
        BRepTools::Write(shapeToAlign, "DVSShapeToAlign.brep");// debug
    }

    BRep_Builder builder;
    TopoDS_Compound result;
    builder.MakeCompound(result);

    TopExp_Explorer expFace(shapeToAlign, TopAbs_FACE);
    for (int iFace = 1; expFace.More(); expFace.Next(), iFace++) {
        const TopoDS_Face& face = TopoDS::Face(expFace.Current());
        std::vector<TopoDS_Wire> faceWires;
        TopExp_Explorer expWires(face, TopAbs_WIRE);
        for (; expWires.More(); expWires.Next()) {
            const TopoDS_Wire& wire = TopoDS::Wire(expWires.Current());
            TopoDS_Shape projectedShape =
                GeometryObject::projectSimpleShape(wire, getProjectionCS());
            std::vector<TopoDS_Edge> wireEdges;
            // projectedShape is just a bunch of edges. we have to rebuild the wire.
            TopExp_Explorer expEdges(projectedShape, TopAbs_EDGE);
            for (; expEdges.More(); expEdges.Next()) {
                const TopoDS_Edge& edge = TopoDS::Edge(expEdges.Current());
                wireEdges.push_back(edge);
            }
            TopoDS_Wire cleanWire = EdgeWalker::makeCleanWire(wireEdges, 2.0 * EWTOLERANCE);
            faceWires.push_back(cleanWire);
        }

        // validate section face wires
        std::vector<TopoDS_Wire> goodWires;
        constexpr double minWireArea = 0.000001;// arbitrary very small face size
        for (auto& wire : faceWires) {
            if (wire.IsNull()) {
                continue;
            }
            if (!BRep_Tool::IsClosed(wire)) {
                continue;// can not make a face from open wire
            }
            double area = ShapeAnalysis::ContourArea(wire);
            if (area <= minWireArea) {
                continue;// can not make a face from wire with no area
            }
            goodWires.push_back(wire);
        }

        if (goodWires.empty()) {
            // this may or may not be significant.  In the offset or noparallel
            // strategies, a profile segment that is parallel to the SectionNormal
            // will not generate a face.
            Base::Console().Log("DVS::mapToPage - %s - section face has no valid wires.\n",
                                getNameInDocument());
            continue;
        }

        TopoDS_Shape holeyShape = makeFaceFromWires(goodWires);
        if (holeyShape.IsNull()) {
            continue;
        }

        builder.Add(result, TopoDS::Face(holeyShape));
        if (debugSection()) {
            std::stringstream ss;
            ss << "DVSFaceFromWires" << iFace << ".brep";
            BRepTools::Write(holeyShape, ss.str().c_str());// debug
        }
    }

    return result;
}

// makes a [perforated] face from an outer wire and wires describing the holes.
// Open wires and wires with zero area are assumed to already have been removed.
TopoDS_Shape DrawViewSection::makeFaceFromWires(std::vector<TopoDS_Wire>& inWires)
{
    // make sure the largest wire is the first
    EdgeWalker eWalker;
    std::vector<TopoDS_Wire> goodWires = eWalker.sortWiresBySize(inWires);

    // make a face from the good wires
    // first good wire should be the outer boundary of the face
    TopoDS_Face faceToFix;
    TopoDS_Shape orientedShape = goodWires.at(0).Oriented(TopAbs_FORWARD);
    TopoDS_Wire orientedWire = TopoDS::Wire(orientedShape);
    orientedWire.Orientation(TopAbs_FORWARD);
    TopoDS_Face blankFace = BRepBuilderAPI_MakeFace(orientedWire);
    int wireCount = goodWires.size();
    if (wireCount < 2) {
        faceToFix = blankFace;
    }
    else {
        // add the holes
        BRepBuilderAPI_MakeFace mkFace(blankFace);
        for (int iWire = 1; iWire < wireCount; iWire++) {
            // make holes in the face with the rest of the wires
            orientedShape = goodWires.at(iWire).Oriented(TopAbs_REVERSED);
            orientedWire = TopoDS::Wire(orientedShape);
            mkFace.Add(orientedWire);
        }

        if (!mkFace.IsDone()) {
            Base::Console().Error("DVS::makeFaceFromWires - %s - failed to make section face.\n",
                                    getNameInDocument());
            return TopoDS_Shape();
        }
        faceToFix = mkFace.Face();
    }

    // setting the wire orientation above should generate a valid face, but
    // sometimes does not, so we fix the shape to resolve any issues
    Handle(ShapeFix_Shape) sfs = new ShapeFix_Shape;
    sfs->Init(faceToFix);
    sfs->Perform();
    return sfs->Shape();
}

// turn OCC section faces into TD geometry
std::vector<TechDraw::FacePtr> DrawViewSection::makeTDSectionFaces(TopoDS_Compound topoDSFaces)
{
    //    Base::Console().Message("DVS::makeTDSectionFaces()\n");
    std::vector<TechDraw::FacePtr> tdSectionFaces;
    TopExp_Explorer sectionExpl(topoDSFaces, TopAbs_FACE);
    for (; sectionExpl.More(); sectionExpl.Next()) {
        const TopoDS_Face& face = TopoDS::Face(sectionExpl.Current());
        TechDraw::FacePtr sectionFace(std::make_shared<TechDraw::Face>());
        TopExp_Explorer expFace(face, TopAbs_WIRE);
        for (; expFace.More(); expFace.Next()) {
            TechDraw::Wire* w = new TechDraw::Wire();
            const TopoDS_Wire& wire = TopoDS::Wire(expFace.Current());
            TopExp_Explorer expWire(wire, TopAbs_EDGE);
            for (; expWire.More(); expWire.Next()) {
                const TopoDS_Edge& edge = TopoDS::Edge(expWire.Current());
                TechDraw::BaseGeomPtr e = BaseGeom::baseFactory(edge);
                if (e) {
                    w->geoms.push_back(e);
                }
            }
            sectionFace->wires.push_back(w);
        }
        tdSectionFaces.push_back(sectionFace);
    }

    return tdSectionFaces;
}

// calculate the ends of the section line in BaseView's coords
std::pair<Base::Vector3d, Base::Vector3d> DrawViewSection::sectionLineEnds()
{
    std::pair<Base::Vector3d, Base::Vector3d> result;
    DrawViewPart *baseDvp = getBaseDVP();
    if (!baseDvp)
        return result;

    Base::Vector3d stdZ(0.0, 0.0, 1.0);
    double baseRotation = baseDvp->Rotation.getValue();//Qt degrees are clockwise
    Base::Rotation rotator(stdZ, baseRotation * M_PI / 180.0);
    Base::Rotation unrotator(stdZ, -baseRotation * M_PI / 180.0);

    auto sNorm = SectionNormal.getValue();
    auto axis = baseDvp->Direction.getValue();
    Base::Vector3d stdOrg(0.0, 0.0, 0.0);
    Base::Vector3d sectionLineDir = -axis.Cross(sNorm);
    sectionLineDir.Normalize();

    sectionLineDir = baseDvp->projectPoint(sectionLineDir);//convert to base view CS
    sectionLineDir.Normalize();


    Base::Vector3d sectionOrg = SectionOrigin.getValue() - baseDvp->getOriginalCentroid();
    sectionOrg = baseDvp->projectPoint(sectionOrg);//convert to base view CS
    double halfSize = (getBaseDVP()->getSizeAlongVector(sectionLineDir) / 2.0) * SectionLineStretch.getValue();

    result.first = sectionOrg + sectionLineDir * halfSize;
    result.second = sectionOrg - sectionLineDir * halfSize;

    return result;
}

// find the points and directions to make the change point marks.
ChangePointVector DrawViewSection::getChangePointsFromSectionLine()
{
    ChangePointVector result;
    DrawViewPart *baseDvp = getBaseDVP();
    if (!baseDvp)
        return result;

    std::vector<gp_Pnt> allPoints;
    std::pair<Base::Vector3d, Base::Vector3d> lineEnds = sectionLineEnds();
    //make start and end marks
    gp_Pnt location0 = DU::togp_Pnt(lineEnds.first);
    gp_Pnt location1 = DU::togp_Pnt(lineEnds.second);
    gp_Dir postDir = gp_Dir(location1.XYZ() - location0.XYZ());
    gp_Dir preDir = postDir.Reversed();
    ChangePoint startPoint(location0, preDir, postDir);
    result.push_back(startPoint);
    preDir = gp_Dir(location0.XYZ() - location1.XYZ());
    postDir = preDir.Reversed();
    ChangePoint endPoint(location1, preDir, postDir);
    result.push_back(endPoint);
    return result;
}

void DrawViewSection::setChangePoints(const ChangePointVector &points)
{
    auto baseDvp = getBaseDVP();
    if (!baseDvp)
        return;

    auto lineEnds = sectionLineEnds();

    // current section line mid point in proj cs
    Base::Vector3d oldMid = baseDvp->projectPoint((lineEnds.first+lineEnds.second)/2);

    Base::Vector3d p0 = DU::toVector3d(points.front().getLocation());
    Base::Vector3d p1 = DU::toVector3d(points.back().getLocation());
    Base::Vector3d newMid = (p1+p0)/2;

    if (!DU::vectorEqual(oldMid, newMid)) {
        Base::Vector3d centroid = baseDvp->getOriginalCentroid();

        newMid = baseDvp->inverseProjectPoint(newMid) + centroid; // new mid point in view cs
        SectionOrigin.setValue(newMid);
    }

    Base::Vector3d oldDir = lineEnds.second - lineEnds.first;
    oldDir.Normalize();
    Base::Vector3d newDir = p1 - p0;
    newDir.Normalize();
    if (!DU::vectorEqual(oldDir, newDir))
        setCSFromBase(Base::Vector3d(-newDir.y, -newDir.x, 0));
}

bool DrawViewSection::isReallyInBox(const Base::Vector3d v, const Base::BoundBox3d bb) const
{
    if (v.x <= bb.MinX || v.x >= bb.MaxX) {
        return false;
    }
    if (v.y <= bb.MinY || v.y >= bb.MaxY) {
        return false;
    }
    if (v.z <= bb.MinZ || v.z >= bb.MaxZ) {
        return false;
    }
    return true;
}

bool DrawViewSection::isReallyInBox(const gp_Pnt p, const Bnd_Box& bb) const
{
    return !bb.IsOut(p);
}

Base::Vector3d DrawViewSection::getXDirection() const
{
    //    Base::Console().Message("DVS::getXDirection() - %s\n",
    //    Label.getValue());
    App::Property* prop = getPropertyByName("XDirection");
    if (!prop) {
        // No XDirection property.  can this happen?
        gp_Ax2 cs = getCSFromBase(SectionDirection.getValueAsString());
        gp_Dir gXDir = cs.XDirection();
        return Base::Vector3d(gXDir.X(), gXDir.Y(), gXDir.Z());
    }

    // we have an XDirection property
    if (DrawUtil::fpCompare(XDirection.getValue().Length(), 0.0)) {
        // but it has no value, so we make a value
        if (BaseView.getValue()) {
            gp_Ax2 cs = getCSFromBase(SectionDirection.getValueAsString());
            gp_Dir gXDir = cs.XDirection();
            return Base::Vector3d(gXDir.X(), gXDir.Y(), gXDir.Z());
        }
    }

    // XDirection is good, so we use it
    return XDirection.getValue();
}

void DrawViewSection::setCSFromBase(const std::string sectionName)
{
    //    Base::Console().Message("DVS::setCSFromBase(%s)\n",
    //    sectionName.c_str());
    gp_Dir gDir = getCSFromBase(sectionName).Direction();
    Base::Vector3d vDir(gDir.X(), gDir.Y(), gDir.Z());
    Direction.setValue(vDir);
    SectionNormal.setValue(vDir);
    gp_Dir gxDir = getCSFromBase(sectionName).XDirection();
    Base::Vector3d vXDir(gxDir.X(), gxDir.Y(), gxDir.Z());
    XDirection.setValue(vXDir);
}

// set the section CS based on an XY vector in BaseViews CS
void DrawViewSection::setCSFromBase(const Base::Vector3d localUnit)
{
    //    Base::Console().Message("DVS::setCSFromBase(%s)\n",
    //    DrawUtil::formatVector(localUnit).c_str());
    gp_Ax2 newSectionCS = getBaseDVP()->localVectorToCS(localUnit);

    Base::Vector3d vDir(newSectionCS.Direction().X(),
                        newSectionCS.Direction().Y(),
                        newSectionCS.Direction().Z());
    Direction.setValue(vDir);
    SectionNormal.setValue(vDir);
    Base::Vector3d vXDir(newSectionCS.XDirection().X(),
                         newSectionCS.XDirection().Y(),
                         newSectionCS.XDirection().Z());
    XDirection.setValue(vXDir);// XDir is for projection
}

// reset the section CS based on an XY vector in current section CS
void DrawViewSection::setCSFromLocalUnit(const Base::Vector3d localUnit)
{
    //    Base::Console().Message("DVS::setCSFromLocalUnit(%s)\n",
    //    DrawUtil::formatVector(localUnit).c_str());
    gp_Dir verticalDir = getSectionCS().YDirection();
    gp_Ax1 verticalAxis(DrawUtil::togp_Pnt(SectionOrigin.getValue()), verticalDir);
    gp_Dir oldNormal = getSectionCS().Direction();
    gp_Dir newNormal = DrawUtil::togp_Dir(projectPoint(localUnit));
    double angle = oldNormal.AngleWithRef(newNormal, verticalDir);
    gp_Ax2 newCS = getSectionCS().Rotated(verticalAxis, angle);
    SectionNormal.setValue(DrawUtil::toVector3d(newCS.Direction()));
    XDirection.setValue(DrawUtil::toVector3d(newCS.XDirection()));
}

gp_Ax2 DrawViewSection::getCSFromBase(const std::string sectionName) const
{
    //    Base::Console().Message("DVS::getCSFromBase(%s)\n",
    //    sectionName.c_str());
    Base::Vector3d origin(0.0, 0.0, 0.0);
    Base::Vector3d sectOrigin = SectionOrigin.getValue();

    gp_Ax2 dvpCS = getBaseDVP()->getProjectionCS(sectOrigin);

    if (debugSection()) {
        DrawUtil::dumpCS("DVS::getCSFromBase - dvp CS", dvpCS);
    }
    gp_Dir dvpDir = dvpCS.Direction();
    gp_Dir dvpUp = dvpCS.YDirection();
    gp_Dir dvpRight = dvpCS.XDirection();
    gp_Pnt dvsLoc(sectOrigin.x, sectOrigin.y, sectOrigin.z);
    gp_Dir dvsDir;
    gp_Dir dvsXDir;

    if (sectionName == "Up") {// looking up
        dvsDir = dvpUp.Reversed();
        dvsXDir = dvpRight;
    }
    else if (sectionName == "Down") {
        dvsDir = dvpUp;
        dvsXDir = dvpRight;
    }
    else if (sectionName == "Left") {
        dvsDir = dvpRight;          // dvpX
        dvsXDir = dvpDir.Reversed();//-dvpZ
    }
    else if (sectionName == "Right") {
        dvsDir = dvpRight.Reversed();
        dvsXDir = dvpDir;
    }
    else if (sectionName == "Aligned") {
        // if aligned, we don't get our direction from the base view
        Base::Vector3d sectionNormal = SectionNormal.getValue();
        dvsDir = gp_Dir(sectionNormal.x, sectionNormal.y, sectionNormal.z);
        Base::Vector3d sectionXDir = XDirection.getValue();
        dvsXDir = gp_Dir(sectionXDir.x, sectionXDir.y, sectionXDir.z);
    }
    else {
        dvsDir = dvpRight;
        dvsXDir = dvpDir;
    }

    gp_Ax2 CS(dvsLoc, dvsDir, dvsXDir);

    if (debugSection()) {
        DrawUtil::dumpCS("DVS::getCSFromBase - sectionCS out", CS);
    }

    return CS;
}

// returns current section cs
gp_Ax2 DrawViewSection::getSectionCS() const
{
    //    Base::Console().Message("DVS::getSectionCS()\n");
    Base::Vector3d vNormal = SectionNormal.getValue();
    gp_Dir gNormal(vNormal.x, vNormal.y, vNormal.z);
    Base::Vector3d vXDir = getXDirection();
    gp_Dir gXDir(vXDir.x, vXDir.y, vXDir.z);
    Base::Vector3d vOrigin = SectionOrigin.getValue();
    gp_Pnt gOrigin(vOrigin.x, vOrigin.y, vOrigin.z);
    gp_Ax2 sectionCS(gOrigin, gNormal);
    try {
        sectionCS = gp_Ax2(gOrigin, gNormal, gXDir);
    }
    catch (...) {
        Base::Console().Error("DVS::getSectionCS - %s - failed to create section CS\n",
                              getNameInDocument());
    }
    return sectionCS;
}

gp_Ax2 DrawViewSection::getProjectionCS(const Base::Vector3d pt) const
{
    Base::Vector3d vNormal = SectionNormal.getValue();
    gp_Dir gNormal(vNormal.x, vNormal.y, vNormal.z);
    Base::Vector3d vXDir = getXDirection();
    gp_Dir gXDir(vXDir.x, vXDir.y, vXDir.z);
    if (DrawUtil::fpCompare(fabs(gNormal.Dot(gXDir)), 1.0)) {
        // can not build a gp_Ax2 from these values
        throw Base::RuntimeError(
            "DVS::getProjectionCS - SectionNormal and XDirection are parallel");
    }
    gp_Pnt gOrigin(pt.x, pt.y, pt.z);
    return {gOrigin, gNormal, gXDir};
}

std::vector<LineSet> DrawViewSection::getDrawableLines(int i)
{
    //    Base::Console().Message("DVS::getDrawableLines(%d) - lineSets: %d\n", i,
    //    m_lineSets.size());
    std::vector<LineSet> result;
    return DrawGeomHatch::getTrimmedLinesSection(this,
                                                 m_lineSets,
                                                 getSectionTopoDSFace(i),
                                                 HatchScale.getValue(),
                                                 HatchRotation.getValue(),
                                                 HatchOffset.getValue());
}

TopoDS_Face DrawViewSection::getSectionTopoDSFace(int i)
{
    TopExp_Explorer expl(m_sectionTopoDSFaces, TopAbs_FACE);
    int count = 1;
    for (; expl.More(); expl.Next(), count++) {
        if (count == i + 1) {
            return TopoDS::Face(expl.Current());
        }
    }
    return TopoDS_Face();
}

TechDraw::DrawViewPart* DrawViewSection::getBaseDVP() const
{
    return Base::freecad_dynamic_cast<DrawViewPart>(BaseView.getValue());
}

// setup / tear down routines

void DrawViewSection::unsetupObject()
{
    TechDraw::DrawViewPart* base = getBaseDVP();
    if (base) {
        base->touch();
    }
    DrawViewPart::unsetupObject();
}

void DrawViewSection::onDocumentRestored()
{
    makeLineSets();
    DrawViewPart::onDocumentRestored();
}

void DrawViewSection::setupObject()
{
    // by this point DVS should have a name and belong to a document
    replaceSvgIncluded(FileHatchPattern.getValue());
    replacePatIncluded(FileGeomPattern.getValue());

    DrawViewPart::setupObject();
}

// hatch file routines

// create geometric hatch lines
void DrawViewSection::makeLineSets(void)
{
    //    Base::Console().Message("DVS::makeLineSets()\n");
    if (PatIncluded.isEmpty()) {
        return;
    }

    std::string fileSpec = PatIncluded.getValue();
    Base::FileInfo fi(fileSpec);
    if (!fi.isReadable()) {
        Base::Console().Message("%s can not read hatch file: %s\n",
                                getNameInDocument(),
                                fileSpec.c_str());
        return;
    }

    if (fi.hasExtension("pat")) {
        if (!fileSpec.empty() && !NameGeomPattern.isEmpty()) {
            m_lineSets.clear();
            m_lineSets = DrawGeomHatch::makeLineSets(fileSpec, NameGeomPattern.getValue());
        }
    }
}

void DrawViewSection::replaceSvgIncluded(std::string newSvgFile)
{
    //    Base::Console().Message("DVS::replaceSvgIncluded(%s)\n",
    //    newSvgFile.c_str());
    if (newSvgFile.empty()) {
        return;
    }

    Base::FileInfo tfi(newSvgFile);
    if (tfi.isReadable()) {
        SvgIncluded.setValue(newSvgFile.c_str());
    }
    else {
        THROWM(Base::RuntimeError, "Could not read the new Svg file")
    }
}

void DrawViewSection::replacePatIncluded(std::string newPatFile)
{
    //    Base::Console().Message("DVS::replacePatIncluded(%s)\n",
    //    newPatFile.c_str());
    if (newPatFile.empty()) {
        return;
    }

    Base::FileInfo tfi(newPatFile);
    if (tfi.isReadable()) {
        PatIncluded.setValue(newPatFile.c_str());
    }
    else {
        THROWM(Base::RuntimeError, "Could not read the new Pat file")
    }
}

// Parameter fetching routines

void DrawViewSection::getParameters()
{
    //    Base::Console().Message("DVS::getParameters()\n");
    bool fuseFirst = Preferences::getPreferenceGroup("General")->GetBool("SectionFuseFirst", false);
    FuseBeforeCut.setValue(fuseFirst);
}

bool DrawViewSection::debugSection(void)
{
    return Preferences::getPreferenceGroup("debug")->GetBool("debugSection", false);
}

int DrawViewSection::prefCutSurface(void)
{
    //    Base::Console().Message("DVS::prefCutSurface()\n");

    return Preferences::getPreferenceGroup("Decorations")
        ->GetInt("CutSurfaceDisplay", 2);// default to SvgHatch
}

bool DrawViewSection::showSectionEdges(void)
{
    return Preferences::getPreferenceGroup("General")->GetBool("ShowSectionEdges", true);
}

PyObject* DrawViewSection::getPyObject()
{
    if (PythonObject.is(Py::_None())) {
        // ref counter is set to 1
        PythonObject = Py::Object(new DrawViewSectionPy(this), true);
    }
    return Py::new_reference_to(PythonObject);
}

// Python Drawing feature
// ---------------------------------------------------------

namespace App
{
/// @cond DOXERR
PROPERTY_SOURCE_TEMPLATE(TechDraw::DrawViewSectionPython, TechDraw::DrawViewSection)
template<>
const char* TechDraw::DrawViewSectionPython::getViewProviderName() const
{
    return "TechDrawGui::ViewProviderDrawingView";
}
/// @endcond

// explicit template instantiation
template class TechDrawExport FeaturePythonT<TechDraw::DrawViewSection>;
}// namespace App
