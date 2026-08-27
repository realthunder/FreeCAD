// SPDX-License-Identifier: LGPL-2.1-or-later

/***************************************************************************
 *   Copyright (c) 2017 Shai Seger <shaise at gmail>                       *
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

#pragma once

#ifdef _MSC_VER
# pragma warning(disable : 4251)
#endif

#include <queue>
#include <functional>
#include <chrono>

#include <QOpenGLWidget>
#include <QPainter>
#include <QTimer>
#include <QExposeEvent>
#include <QResizeEvent>
#include <QMouseEvent>
#include <QOpenGLContext>

#include <Base/BoundBox.h>
#include <Mod/Part/App/TopoShape.h>
#include <Gui/Renderer/Renderer.h>

class SoCamera;

namespace Render
{
class DrawSurface;
}

namespace Gui
{
class MDIView;
class Document;
class View3DInventorViewer;
}  // namespace Gui

namespace CAMSimulator
{

// use short declaration as using 'include' causes a header loop
class MillSimulation;
struct MillSimulationState;
struct Vertex;
class ViewCAMSimulator;
class GuiDisplay;
class Dummy3DViewer;

struct SimShape
{
public:
    float maxDimension() const;

public:
    std::vector<Vertex> verts;
    std::vector<uint16_t> indices;
    bool needsUpdate = false;
};

struct SimTool
{
public:
    std::vector<float> profile;
    int id;
    float diameter;
    float resolution;
};

/// The simulator's drawing, in both of the facade's flavours
/// (docs/CAMSimRenderPort.md sections 3 and 8).
///
/// ATTACHED is the normal one: the widget is hidden and drawFrame()
/// runs inside the 3D view's own frame, so the carved stock shares
/// that view's target and sorts with the document's geometry.
/// STANDALONE is the fallback for a view with no renderer to borrow
/// (render cache outside the renderer mode): the widget draws itself,
/// through its own surface and its own backbuffer, exactly as stage 1
/// left it. Only the surface differs -- drawFrame() is the same code
/// either way.
class DlgCAMSimulator: public QOpenGLWidget, public Render::FrameConsumer
{
    Q_OBJECT

    typedef std::chrono::steady_clock clock;

public:
    explicit DlgCAMSimulator(QWidget* parent = nullptr);
    ~DlgCAMSimulator() override;

    void connectTo(GuiDisplay& gui, Dummy3DViewer& dv);
    void cloneFrom(const DlgCAMSimulator& from);

    static DlgCAMSimulator* instance(Gui::Document* doc = nullptr);

    void setAnimating(bool animating);
    void startSimulation(const Part::TopoShape& stock, float quality);
    void resetSimulation();

    void addGcodeCommand(const char* cmd);
    void addTool(
        const std::vector<float>& toolProfilePoints,
        int toolNumber,
        float diameter,
        float resolution
    );

    void setStockShape(const Part::TopoShape& shape, float resolution);
    void setStockVisible(bool b);
    void setBaseShape(const Part::TopoShape& shape, float resolution);
    void setBaseVisible(bool b);

    void setRotateEnabled(bool b);

    void setBackgroundColor(const QColor& c);
    void setPathColor(const QColor& normal, const QColor& rapid);

    /// Render::FrameConsumer: one simulation step drawn into \a
    /// surface. Called by the host renderer once per frame when
    /// attached, and by paintGL through the standalone surface when
    /// not.
    unsigned framePasses() const override;
    void drawFrame(Render::DrawSurface& surface) override;

    /// Try to draw inside \a viewer's renderer instead of this
    /// widget. Does nothing (and leaves the standalone path in place)
    /// when the viewer has no renderer -- render cache outside the
    /// renderer mode, or a backend that could not start. Re-called
    /// whenever that could have changed, because a renderer swap
    /// forgets its consumer.
    void attachToHost(Gui::View3DInventorViewer* viewer);
    bool isAttached() const
    {
        return mHostRenderer != nullptr;
    }

    /// The bounds of the shapes the SIMULATOR draws itself, for a view
    /// fit. While attached the stock is deliberately absent from
    /// the viewer's scene graph (mirrorsStockToViewer), so a fit computed
    /// from that scene alone frames an empty world and leaves the
    /// camera on top of the origin -- with the stock outside the
    /// frustum, which is nothing drawn at all rather than something
    /// mis-framed. Invalid when no stock has been set yet.
    Base::BoundBox3d simulationBoundBox() const;

Q_SIGNALS:
    void simulationStarted();

protected:
    void timerEvent(QTimerEvent* event) override;

    void updateResources();
    /// \a width and \a height in device pixels of whatever is being
    /// drawn into -- the widget standalone, the host's scene target
    /// attached, which is not always the same size.
    void updateWindowScale(int width, int height);
    void updateCamera();

    void initializeGL() override;
    void paintGL() override;
    void resizeGL(int w, int h) override;

    // The STANDALONE frame boundary around drawFrame(): begin creates
    // and opens this widget's own surface, end runs the backend frame
    // and blits it into the widget -- or backs out untouched when
    // nothing was submitted. Neither runs while attached: there the
    // host owns the boundary.
    bool beginFacadeFrame();
    void endFacadeFrame();

    void updateGui();

    /// Restate the viewer's stock/base view providers from the
    /// simulator's state and the current attachment.
    void syncViewerMirrors();

    /// Whether the viewer's own view provider for each shape should
    /// follow the simulator's copy. The two shapes answer differently
    /// while attached -- see the definitions.
    bool mirrorsStockToViewer() const;
    bool mirrorsBaseToViewer() const;

    /// Ask for another frame. Attached, that is the HOST's frame --
    /// this widget is hidden and never paints, so update() on it would
    /// stop the simulation dead the moment it attached.
    void requestRedraw();

private:
    bool mNeedsInitialize = false;
    bool mNeedsClear = false;
    bool mAnimating = false;
    int mAnimatingTimer = 0;

    std::unique_ptr<MillSimulation> mMillSimulator;
    float mQuality = 10;

    std::vector<std::string> mGCode;
    std::size_t mLastGCode = 0;

    std::vector<SimTool> mTools;

    const SoCamera* mCamera = nullptr;
    SimShape mStock;
    SimShape mBase;

    std::unique_ptr<MillSimulationState> mState;
    clock::time_point mLastProcessSim = clock::time_point::min();

    GuiDisplay* mGui = nullptr;
    Dummy3DViewer* mDummyViewer = nullptr;

    // The bounds of the stock and base shapes as handed in, kept
    // because the meshes above are the simulator's own copies and the
    // viewer may not be holding the shapes at all -- see
    // simulationBoundBox().
    Base::BoundBox3d mStockBox;
    Base::BoundBox3d mBaseBox;

    /// The renderer whose frames this consumer draws in, or null when
    /// standalone. Not owned; cleared when the host goes away.
    Render::Renderer* mHostRenderer = nullptr;

    // The draw-facade frame surface; null until the backend device is
    // up (the GL path stands alone until then), dropped if it goes
    // back down.
    std::unique_ptr<Render::DrawSurface> mDrawSurface;
};

}  // namespace CAMSimulator
