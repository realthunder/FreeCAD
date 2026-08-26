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

#ifndef DrawViewDetail_h_
#define DrawViewDetail_h_

#include <gp_Ax2.hxx>
#include <gp_Trsf.hxx>
#include <TopoDS_Shape.hxx>

#include <App/DocumentObject.h>
#include <App/FeaturePython.h>
#include <App/PropertyLinks.h>
#include <Mod/TechDraw/TechDrawGlobal.h>

#include "DrawViewPart.h"


class gp_Pln;
class gp_Ax2;
class TopoDS_Face;

namespace TechDraw
{
class Face;
}

namespace TechDraw
{


class TechDrawExport DrawViewDetail : public DrawViewPart
{
    PROPERTY_HEADER_WITH_OVERRIDE(Part::DrawViewDetail);

public:
    /// Constructor
    DrawViewDetail();
    ~DrawViewDetail() override;

    App::PropertyLink   BaseView;
    App::PropertyVector AnchorPoint;
    App::PropertyFloat  Radius;
    App::PropertyString Reference;

    App::PropertyBool   ShowMatting;
    App::PropertyBool   ShowHighlight;

    short mustExecute() const override;
    App::DocumentObjectExecReturn *execute() override;
    void onChanged(const App::Property* prop) override;
    const char* getViewProviderName() const override {
        return "TechDrawGui::ViewProviderViewPart";
    }
    void unsetupObject() override;


    void detailExec(const TopoDS_Shape& s,
                    DrawViewPart* baseView,
                    DrawViewSection* sectionAlias);
    void postHlrTasks() override;
    void waitingForDetail(bool s) { m_waitingForDetail = s; }
    bool waitingForDetail(void) const { return m_waitingForDetail; }
    bool waitingForResult() const override;

    double getFudgeRadius(void);
    static TopoDS_Shape projectEdgesOntoFace(TopoDS_Shape& edgeShape,
                                             TopoDS_Face& projFace,
                                             gp_Dir& projDir);

    std::vector<DrawViewDetail*> getDetailRefs() const override;
    TopoDS_Shape getDetailShape() const { return m_detailShape; }
    TopoDS_Shape getScaledShape() const { return m_scaledShape; }
    //! the CS the scaled shape was projected through (the base view's
    //! projection CS; the detail Rotation is baked into the shape)
    gp_Ax2 getDetailViewAxis() const { return m_viewAxis; }

    //! The exact build-time frames (doc sec 31), composed at detailExec
    //! launch and committed with the shapes: m_detailShape -> global,
    //! and m_scaledShape -> global (the latter carries the 1/Scale
    //! factor).  False while no detail has finished, or when the base
    //! chain has no rigid frame (an aligned complex section).
    bool getDetailFrame(gp_Trsf& frame) const;
    bool getScaledFrame(gp_Trsf& frame) const;

protected:
    struct Output {
        TopoDS_Shape shape;
        TopoDS_Shape detailShape;
        Base::Vector3d centroid;
    };
    void onMakeDetailFinished(std::shared_ptr<Output> output);

    struct DetailParams {
        std::string featureName;
        std::shared_ptr<Base::SequencerLauncher> progress;
        std::shared_ptr<Output> output;
        TopoDS_Shape shape;
        gp_Ax2 viewAxis;
        Base::Vector3d dirDetail;
        Base::Vector3d shapeCenter;
        Base::Vector3d anchorPoint;
        double radius;
        double rotation;
        double scale;
        bool moveShape;
    };
    static void makeDetailShape(const DetailParams &params);

    void abortMakeDetail();

    void getParameters(void);
    double m_fudge;
    static bool debugDetail();

    TopoDS_Shape m_scaledShape;
    gp_Ax2 m_viewAxis;

    // build-time frames; pendings are composed at detailExec launch
    // from the same params the worker consumes, committed in
    // onMakeDetailFinished.  Transient, like the shapes they describe.
    gp_Trsf m_detailFrame;          // m_detailShape -> global
    gp_Trsf m_scaledFrame;          // m_scaledShape -> global (with 1/Scale)
    bool m_detailFrameValid = false;
    bool m_scaledFrameValid = false;
    gp_Trsf m_pendingDetailFrame;
    gp_Trsf m_pendingScaledFrame;
    bool m_pendingFramesValid = false;

    DrawViewPart* m_saveDvp;
    DrawViewSection* m_saveDvs;

private:
    std::unique_ptr<QFutureWatcher<void>> m_detailWatcher;
    std::shared_ptr<Base::SequencerLauncher> m_progress;
    bool m_waitingForDetail = false;
    TopoDS_Shape m_detailShape;
};

using DrawViewDetailPython = App::FeaturePythonT<DrawViewDetail>;

} //namespace TechDraw

#endif
