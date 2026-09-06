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

#ifndef DrawViewSection_h_
#define DrawViewSection_h_

#include <TopoDS_Compound.hxx>
#include <TopoDS_Shape.hxx>
#include <gp_Ax2.hxx>
#include <gp_Trsf.hxx>

#include <App/DocumentObject.h>
#include <App/FeaturePython.h>
#include <App/PropertyFile.h>
#include <App/PropertyLinks.h>
#include <Mod/TechDraw/TechDrawGlobal.h>

#include "DrawViewPart.h"

class Bnd_Box;
class BRepAlgoAPI_Cut;
class gp_Pln;
class gp_Pnt;
class TopoDS_Face;
class TopoDS_Wire;
class gp_Ax2;

namespace TechDraw
{
class Face;
class DrawProjGroupItem;
class DrawGeomHatch;
class PATLineSpec;
class LineSet;
class DashSet;

//changes in direction of complex section line. also marks at arrow positions.
class ChangePoint
{
public:
    ChangePoint(QPointF location, QPointF preDirection, QPointF postDirection);
    ChangePoint(gp_Pnt location, gp_Dir preDirection, gp_Dir postDirection);
    ~ChangePoint() = default;

    QPointF getLocation() const { return m_location; }
    void setLocation(QPointF newLocation) { m_location = newLocation; }
    QPointF getPreDirection() const { return m_preDirection; }
    void setPreDirection(QPointF newDirection) { m_preDirection = newDirection; }
    QPointF getPostDirection() const { return m_postDirection; }
    void setPostDirection(QPointF newDirection) { m_postDirection = newDirection; }
    void scale(double scaleFactor);

private:
    QPointF m_location;
    QPointF m_preDirection;
    QPointF m_postDirection;
};

using ChangePointVector = std::vector<ChangePoint>;

class TechDrawExport DrawViewSection: public DrawViewPart
{
    PROPERTY_HEADER_WITH_OVERRIDE(Part::DrawViewSection);

public:
    DrawViewSection();
    ~DrawViewSection() override;

    App::PropertyLink BaseView;
    App::PropertyVector SectionNormal;
    App::PropertyVector SectionOrigin;
    App::PropertyString SectionSymbol;


    App::PropertyEnumeration SectionDirection; //to be made obsolete eventually
    App::PropertyEnumeration CutSurfaceDisplay;//new v019
    App::PropertyFile FileHatchPattern;
    App::PropertyFile FileGeomPattern;//new v019
    App::PropertyFileIncluded SvgIncluded;
    App::PropertyFileIncluded PatIncluded;
    App::PropertyString NameGeomPattern;
    App::PropertyFloat HatchScale;
    App::PropertyFloat HatchRotation;
    App::PropertyVector HatchOffset;

    App::PropertyBool FuseBeforeCut;
    App::PropertyBool TrimAfterCut;//new v021
    App::PropertyBool UsePreviousCut;   // new v022

    App::PropertyFloat SectionLineStretch;  // new v022


    bool isReallyInBox(const Base::Vector3d v, const Base::BoundBox3d bb) const;
    bool isReallyInBox(const gp_Pnt p, const Bnd_Box& bb) const;

    App::DocumentObjectExecReturn* execute() override;
    void onChanged(const App::Property* prop) override;
    const char* getViewProviderName() const override
    {
        return "TechDrawGui::ViewProviderViewSection";
    }
    void unsetupObject() override;
    short mustExecute() const override;
    PyObject* getPyObject() override;

    void sectionExec(Part::TopoShape& s);
    virtual void makeSectionCut(const Part::TopoShape& baseShape);
    void postHlrTasks() override;
    virtual void postSectionCutTasks();
    bool waitingForCut(void) const { return m_waitingForCut; }
    bool waitingForResult() const override;

    //! The tool's faces carry fixed element names (SectionPlane and the box
    //! sides), so the faces the cut creates inherit a stable name instead of
    //! one derived from where the tool happened to meet the model.  See
    //! docs/TopoNamingEnhance.md section 8.2.
    virtual Part::TopoShape makeCuttingTool(double shapeSize);
    //! name the tool's faces by geometry: SectionPlane, SectionBack and the sides
    static void nameToolFaces(Part::TopoShape& tool, const gp_Pln& sectionPlane);
    virtual Part::TopoShape getShapeToCut();
    virtual bool isBaseValid() const;
    virtual TopoDS_Shape prepareShape(const Part::TopoShape& rawShape, double shapeSize);
    virtual Part::TopoShape getShapeToPrepare() const { return m_cutPieces; }

    //CS related methods
    gp_Ax2 getProjectionCS(Base::Vector3d pt = Base::Vector3d(0.0, 0.0, 0.0)) const override;
    void setCSFromBase(const std::string sectionName);
    void setCSFromBase(Base::Vector3d localUnit);
    void setCSFromLocalUnit(const Base::Vector3d localUnit);
    virtual gp_Ax2 getCSFromBase(const std::string sectionName) const;
    gp_Ax2 getSectionCS() const;
    Base::Vector3d getXDirection() const override;//don't use XDirection.getValue()

    TechDraw::DrawViewPart* getBaseDVP() const;

    //section face related methods
    std::vector<TechDraw::FacePtr> getTDFaceGeometry() { return m_tdSectionFaces; }
    TopoDS_Face getSectionTopoDSFace(int i);
    virtual TopoDS_Compound alignSectionFaces(TopoDS_Shape faceIntersections);
    TopoDS_Compound mapToPage(TopoDS_Shape& shapeToAlign);
    virtual std::vector<TechDraw::FacePtr> makeTDSectionFaces(TopoDS_Compound topoDSFaces);
    virtual TopoDS_Shape getShapeToIntersect() { return m_cutPieces.getShape(); }

    void makeLineSets(void);
    std::vector<LineSet> getDrawableLines(int i = 0);
    std::vector<PATLineSpec> getDecodedSpecsFromFile(std::string fileSpec, std::string myPattern);

    TopoDS_Shape getCutShape() const { return m_cutShape; }
    //! the cut result before centering/scaling/rotating, with element names
    Part::TopoShape getCutShapeRaw() const { return m_cutShapeRaw; }
    //! the cut result as it came out of the boolean, one piece per source solid
    Part::TopoShape getCutPieces() const { return m_cutPieces; }
    //! the tool the last cut used, with the names its faces carry.  Empty
    //! until a cut has run, and for an aligned complex section, which cuts
    //! with a tool of its own.
    Part::TopoShape getCuttingToolAsBuilt() const { return m_cuttingTool; }
    TopoDS_Shape getPreparedShape() const { return m_preparedShape; }

    //! The exact build-time frames (doc sec 31), committed together
    //! with the shapes they describe: m_cutShapeRaw -> global, and
    //! m_preparedShape -> global (the latter carries the 1/Scale
    //! factor).  False while no cut has finished, or when the frame is
    //! not rigid (an aligned complex section's unfolded fiction).
    bool getCutShapeFrame(gp_Trsf& frame) const;
    bool getPreparedFrame(gp_Trsf& frame) const;

    TopoDS_Shape getShapeForDetail() const override;
    bool getShapeForDetailFrame(gp_Trsf& frame) const override;

    static const char* SectionDirEnums[];
    static const char* CutSurfaceEnums[];

    virtual std::pair<Base::Vector3d, Base::Vector3d> sectionLineEnds();

    virtual void setChangePoints(const ChangePointVector &points);
    virtual ChangePointVector getChangePointsFromSectionLine();

    bool showSectionEdges(void);

    TopoDS_Shape makeFaceFromWires(std::vector<TopoDS_Wire> &inWires);

    virtual void onSectionCutFinished(std::shared_ptr<TopoDS_Shape> result);

protected:
    TopoDS_Compound m_sectionTopoDSFaces;//needed for hatching
    std::vector<LineSet> m_lineSets;
    std::vector<TechDraw::FacePtr> m_tdSectionFaces;

    //! What the cut did, kept so that the element names can be mapped on the
    //! main thread.  The boolean itself runs in a worker (QtConcurrent), and
    //! mapping names there is not safe: a shape that came out of a document
    //! carries that document's App::StringHasher, which has no locking, and
    //! the mapping hashes new names into it.  So the worker keeps the OCCT
    //! makers alive and onSectionCutFinished reads their history.
    struct CutHistory {
        std::vector<Part::TopoShape> sources;
        std::vector<std::shared_ptr<BRepAlgoAPI_Cut>> makers;
        std::shared_ptr<BRepAlgoAPI_Cut> trim;
        Part::TopoShape tool;
        //! what the worker produced, so a history left over from another cut
        //! (an aligned complex section makes its pieces its own way) is not
        //! mapped onto a shape it does not describe
        TopoDS_Shape result;
    };

    struct SectionParams {
        std::string featureName;
        std::shared_ptr<Base::SequencerLauncher> progress;
        std::shared_ptr<TopoDS_Shape> output;
        std::shared_ptr<CutHistory> history;
        Part::TopoShape baseShape;
        Part::TopoShape cuttingTool;
        bool trimAfterCut;
    };
    static void doSectionCut(const SectionParams &params);

    //! the named cut result, mapped from the history the worker recorded;
    //! the unnamed compound when there is no history to map
    Part::TopoShape nameCutPieces(const std::shared_ptr<CutHistory>& history,
                                  const TopoDS_Shape& cutPieces) const;

    virtual gp_Pln getSectionPlane() const;
    virtual TopoDS_Compound findSectionPlaneIntersections(const TopoDS_Shape& shape);
    void getParameters();
    static bool debugSection();
    static int prefCutSurface();

    void waitingForCut(bool s) { m_waitingForCut = s; }
    void abortSectionCut();

    TopoDS_Shape m_cutShape;        // centered, scaled, rotated result of cut
    Part::TopoShape m_cutShapeRaw;  // raw result of cut w/o center/scale/rotate

    //! Frame of the shape getShapeToCut() returns -> global, resolved
    //! through the BaseView chain's stored frames (a detail ancestor
    //! hands over its own centered frame).
    gp_Trsf getShapeToCutFrame(bool& valid) const;

    // The build-time frames.  Pending values are captured with the cut
    // input at launch (execute()) and committed next to the shapes they
    // describe (prepareShape), so an input recomputed mid-cut cannot
    // desynchronize them.  Transient: the shapes rebuild on
    // recompute/restore and the frames rebuild with them.
    gp_Trsf m_cutFrame;             // m_cutShapeRaw -> global
    bool m_cutFrameValid = false;
    gp_Trsf m_preparedFrame;        // m_preparedShape -> global (with 1/Scale)
    bool m_preparedFrameValid = false;
    gp_Trsf m_pendingCutFrame;
    bool m_pendingCutFrameValid = false;

    void onDocumentRestored() override;
    void setupObject() override;
    void replaceSvgIncluded(std::string newSvgFile);
    void replacePatIncluded(std::string newPatFile);

    Part::TopoShape m_cutPieces;//the shape after cutting, but before centering & scaling
    gp_Ax2 m_projectionCS;
    TopoDS_Shape m_preparedShape;//the shape after cutting, centering, scaling etc
    double m_shapeSize;

    //! set when the cut is launched, read when it lands, then released
    std::shared_ptr<CutHistory> m_cutHistory;
    Part::TopoShape m_cuttingTool;  // the tool of the last cut, kept for inspection

private:
    std::unique_ptr<QFutureWatcher<void>> m_cutWatcher;
    std::shared_ptr<Base::SequencerLauncher> m_progress;
    bool m_waitingForCut = false;
};

using DrawViewSectionPython = App::FeaturePythonT<DrawViewSection>;

}//namespace TechDraw

#endif
