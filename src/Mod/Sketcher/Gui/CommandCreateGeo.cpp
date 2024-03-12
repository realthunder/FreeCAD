/***************************************************************************
 *   Copyright (c) 2010 Jürgen Riegel <juergen.riegel@web.de>              *
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
#include <cstdlib>
#include <memory>

#include <QApplication>
#include <QString>
#endif

#include <boost/algorithm/string/predicate.hpp>

#include <App/MappedElement.h>
#include <App/OriginFeature.h>
#include <Base/Console.h>
#include <Base/Exception.h>
#include <Base/Tools.h>
#include <Gui/Action.h>
#include <Gui/Application.h>
#include <Gui/BitmapFactory.h>
#include <Gui/CommandT.h>
#include <Gui/Document.h>
#include <Gui/MainWindow.h>
#include <Gui/SelectionFilter.h>
#include <Gui/View3DInventor.h>
#include <Gui/View3DInventorViewer.h>
#include <Mod/Part/App/DatumFeature.h>
#include <Mod/Part/App/Geometry2d.h>
#include <Mod/Sketcher/App/Constraint.h>
#include <Mod/Sketcher/App/SketchObject.h>

#include "CircleEllipseConstructionMethod.h"
#include "GeometryCreationMode.h"
#include "Utils.h"
#include "ViewProviderSketch.h"

#include <Gui/View3DInventor.h>
#include <Gui/View3DInventorViewer.h>
#include <Gui/SoFCUnifiedSelection.h>
#include <Gui/ViewParams.h>
#include <Gui/ToolBarManager.h>

#include "GeometryCreationMode.h"

// DrawSketchHandler* must be last includes
#include "DrawSketchHandler.h"
#include "DrawSketchHandlerArc.h"
#include "DrawSketchHandlerArcOfEllipse.h"
#include "DrawSketchHandlerArcOfHyperbola.h"
#include "DrawSketchHandlerArcOfParabola.h"
#include "DrawSketchHandlerArcSlot.h"
#include "DrawSketchHandlerBSpline.h"
#include "DrawSketchHandlerBSplineByInterpolation.h"
#include "DrawSketchHandlerCarbonCopy.h"
#include "DrawSketchHandlerCircle.h"
#include "DrawSketchHandlerEllipse.h"
#include "DrawSketchHandlerExtend.h"
#include "DrawSketchHandlerExternal.h"
#include "DrawSketchHandlerFillet.h"
#include "DrawSketchHandlerLine.h"
#include "DrawSketchHandlerLineSet.h"
#include "DrawSketchHandlerPoint.h"
#include "DrawSketchHandlerPolygon.h"
#include "DrawSketchHandlerRectangle.h"
#include "DrawSketchHandlerSlot.h"
#include "DrawSketchHandlerSplitting.h"
#include "DrawSketchHandlerTrimming.h"


using namespace std;
using namespace SketcherGui;

#define CONSTRUCTION_UPDATE_ACTION(CLASS, ICON)                                                    \
    /* This macro creates an updateAction() function that will toggle between normal &             \
     * construction icon */                                                                        \
    void CLASS::updateAction(int mode)                                                             \
    {                                                                                              \
        auto act = getAction();                                                                    \
        if (act) {                                                                                 \
            switch (static_cast<GeometryCreationMode>(mode)) {                                     \
                case GeometryCreationMode::Normal:                                                 \
                    act->setIcon(Gui::BitmapFactory().iconFromTheme(ICON));                        \
                    break;                                                                         \
                case GeometryCreationMode::Construction:                                           \
                    act->setIcon(Gui::BitmapFactory().iconFromTheme(ICON "_Constr"));              \
                    break;                                                                         \
            }                                                                                      \
        }                                                                                          \
    }

namespace SketcherGui
{
GeometryCreationMode geometryCreationMode = GeometryCreationMode::Normal;
}

/* Sketch commands =======================================================*/

DEF_STD_CMD_AU(CmdSketcherCreateLine)

CmdSketcherCreateLine::CmdSketcherCreateLine()
    : Command("Sketcher_CreateLine")
{
    sAppModule = "Sketcher";
    sGroup = "Sketcher";
    sMenuText = QT_TR_NOOP("Create line");
    sToolTipText = QT_TR_NOOP("Create a line in the sketch");
    sWhatsThis = "Sketcher_CreateLine";
    sStatusTip = sToolTipText;
    sPixmap = "Sketcher_CreateLine";
    sAccel = "G, L";
    eType = ForEdit;
}

CONSTRUCTION_UPDATE_ACTION(CmdSketcherCreateLine, "Sketcher_CreateLine")

void CmdSketcherCreateLine::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    ActivateHandler(getActiveGuiDocument(), new DrawSketchHandlerLine());
}

bool CmdSketcherCreateLine::isActive()
{
    return isCommandActive(getActiveGuiDocument());
}


/* Create Box =======================================================*/

DEF_STD_CMD_AU(CmdSketcherCreateRectangle)

CmdSketcherCreateRectangle::CmdSketcherCreateRectangle()
    : Command("Sketcher_CreateRectangle")
{
    sAppModule = "Sketcher";
    sGroup = "Sketcher";
    sMenuText = QT_TR_NOOP("Create rectangle");
    sToolTipText = QT_TR_NOOP("Create a rectangle in the sketch");
    sWhatsThis = "Sketcher_CreateRectangle";
    sStatusTip = sToolTipText;
    sPixmap = "Sketcher_CreateRectangle";
    sAccel = "G, R";
    eType = ForEdit;
}

CONSTRUCTION_UPDATE_ACTION(CmdSketcherCreateRectangle, "Sketcher_CreateRectangle")

void CmdSketcherCreateRectangle::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    ActivateHandler(
        getActiveGuiDocument(),
        new DrawSketchHandlerRectangle(ConstructionMethods::RectangleConstructionMethod::Diagonal));
}

bool CmdSketcherCreateRectangle::isActive()
{
    return isCommandActive(getActiveGuiDocument());
}

DEF_STD_CMD_AU(CmdSketcherCreateRectangleCenter)

CmdSketcherCreateRectangleCenter::CmdSketcherCreateRectangleCenter()
    : Command("Sketcher_CreateRectangle_Center")
{
    sAppModule = "Sketcher";
    sGroup = "Sketcher";
    sMenuText = QT_TR_NOOP("Create centered rectangle");
    sToolTipText = QT_TR_NOOP("Create a centered rectangle in the sketch");
    sWhatsThis = "Sketcher_CreateRectangle_Center";
    sStatusTip = sToolTipText;
    sPixmap = "Sketcher_CreateRectangle_Center";
    sAccel = "G, V";
    eType = ForEdit;
}

CONSTRUCTION_UPDATE_ACTION(CmdSketcherCreateRectangleCenter, "Sketcher_CreateRectangle_Center")

void CmdSketcherCreateRectangleCenter::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    ActivateHandler(getActiveGuiDocument(),
                    new DrawSketchHandlerRectangle(
                        ConstructionMethods::RectangleConstructionMethod::CenterAndCorner));
}

bool CmdSketcherCreateRectangleCenter::isActive()
{
    return isCommandActive(getActiveGuiDocument());
}


/* Create rounded oblong =======================================================*/

DEF_STD_CMD_AU(CmdSketcherCreateOblong)

CmdSketcherCreateOblong::CmdSketcherCreateOblong()
    : Command("Sketcher_CreateOblong")
{
    sAppModule = "Sketcher";
    sGroup = "Sketcher";
    sMenuText = QT_TR_NOOP("Create rounded rectangle");
    sToolTipText = QT_TR_NOOP("Create a rounded rectangle in the sketch");
    sWhatsThis = "Sketcher_CreateOblong";
    sStatusTip = sToolTipText;
    sPixmap = "Sketcher_CreateOblong";
    sAccel = "G, O";
    eType = ForEdit;
}

CONSTRUCTION_UPDATE_ACTION(CmdSketcherCreateOblong, "Sketcher_CreateOblong")

void CmdSketcherCreateOblong::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    ActivateHandler(
        getActiveGuiDocument(),
        new DrawSketchHandlerRectangle(ConstructionMethods::RectangleConstructionMethod::Diagonal,
                                       true));
}

bool CmdSketcherCreateOblong::isActive()
{
    return isCommandActive(getActiveGuiDocument());
}

/* Rectangles Comp command =========================================*/

class CmdSketcherCompCreateRectangles : public Gui::GroupCommand
{
public:
    CmdSketcherCompCreateRectangles();
    virtual const char* className() const {return "CmdSketcherCompCreateRectangles";}
};

CmdSketcherCompCreateRectangles::CmdSketcherCompCreateRectangles()
    : GroupCommand("Sketcher_CompCreateRectangles")
{
    sAppModule = "Sketcher";
    sGroup = "Sketcher";
    sMenuText = QT_TR_NOOP("Create rectangle");
    sToolTipText = QT_TR_NOOP("Actions for Creating a rectangle in the sketch");
    sWhatsThis = "Sketcher_CompCreateRectangles";
    sStatusTip = sToolTipText;
    eType = ForEdit;

    addCommand(new CmdSketcherCreateRectangle());
    addCommand(new CmdSketcherCreateRectangleCenter());
    addCommand(new CmdSketcherCreateOblong());
}

// ======================================================================================

DEF_STD_CMD_AU(CmdSketcherCreatePolyline)

CmdSketcherCreatePolyline::CmdSketcherCreatePolyline()
    : Command("Sketcher_CreatePolyline")
{
    sAppModule = "Sketcher";
    sGroup = "Sketcher";
    sMenuText = QT_TR_NOOP("Create polyline");
    sToolTipText = QT_TR_NOOP("Create a polyline in the sketch. 'M' Key cycles behaviour");
    sToolTipText = QT_TR_NOOP("Create a polyline in the sketch.\n"
                              "Pressed the button (or shortcut) to cycle behaviour");
    sWhatsThis = "Sketcher_CreatePolyline";
    sStatusTip = sToolTipText;
    sPixmap = "Sketcher_CreatePolyline";
    sAccel = "M";
    eType = ForEdit;
}

CONSTRUCTION_UPDATE_ACTION(CmdSketcherCreatePolyline, "Sketcher_CreatePolyline")

void CmdSketcherCreatePolyline::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    ActivateHandler(getActiveGuiDocument(), new DrawSketchHandlerLineSet());
}

bool CmdSketcherCreatePolyline::isActive()
{
    return isCommandActive(getActiveGuiDocument());
}


// ======================================================================================

DEF_STD_CMD_AU(CmdSketcherCreateArc)

CmdSketcherCreateArc::CmdSketcherCreateArc()
    : Command("Sketcher_CreateArc")
{
    sAppModule = "Sketcher";
    sGroup = "Sketcher";
    sMenuText = QT_TR_NOOP("Create arc by center");
    sToolTipText = QT_TR_NOOP("Create an arc by its center and by its end points");
    sWhatsThis = "Sketcher_CreateArc";
    sStatusTip = sToolTipText;
    sPixmap = "Sketcher_CreateArc";
    sAccel = "G, A";
    eType = ForEdit;
}

CONSTRUCTION_UPDATE_ACTION(CmdSketcherCreateArc, "Sketcher_CreateArc")

void CmdSketcherCreateArc::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    ActivateHandler(getActiveGuiDocument(), new DrawSketchHandlerArc());
}

bool CmdSketcherCreateArc::isActive()
{
    return isCommandActive(getActiveGuiDocument());
}


// ======================================================================================

DEF_STD_CMD_AU(CmdSketcherCreate3PointArc)

CmdSketcherCreate3PointArc::CmdSketcherCreate3PointArc()
    : Command("Sketcher_Create3PointArc")
{
    sAppModule = "Sketcher";
    sGroup = "Sketcher";
    sMenuText = QT_TR_NOOP("Create arc by three points");
    sToolTipText = QT_TR_NOOP("Create an arc by its end points and a point along the arc");
    sWhatsThis = "Sketcher_Create3PointArc";
    sStatusTip = sToolTipText;
    sPixmap = "Sketcher_Create3PointArc";
    sAccel = "G, 3, A";
    eType = ForEdit;
}

CONSTRUCTION_UPDATE_ACTION(CmdSketcherCreate3PointArc, "Sketcher_Create3PointArc")

void CmdSketcherCreate3PointArc::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    ActivateHandler(
        getActiveGuiDocument(),
        new DrawSketchHandlerArc(ConstructionMethods::CircleEllipseConstructionMethod::ThreeRim));
}

bool CmdSketcherCreate3PointArc::isActive()
{
    return isCommandActive(getActiveGuiDocument());
}

class CmdSketcherCompCreateArc : public Gui::GroupCommand
{
public:
    CmdSketcherCompCreateArc();
    virtual const char* className() const {return "CmdSketcherCompCreateArc";}
};

CmdSketcherCompCreateArc::CmdSketcherCompCreateArc()
  : GroupCommand("Sketcher_CompCreateArc")
{
    sAppModule = "Sketcher";
    sGroup = "Sketcher";
    sMenuText = QT_TR_NOOP("Create arc");
    sToolTipText = QT_TR_NOOP("Actions for Creating an arc in the sketcher");
    sWhatsThis = "Sketcher_CompCreateArc";
    sStatusTip = sToolTipText;
    eType = ForEdit;
    addCommand(new CmdSketcherCreateArc());
    addCommand(new CmdSketcherCreate3PointArc());
}

// ======================================================================================

DEF_STD_CMD_AU(CmdSketcherCreateCircle)

CmdSketcherCreateCircle::CmdSketcherCreateCircle()
    : Command("Sketcher_CreateCircle")
{
    sAppModule = "Sketcher";
    sGroup = "Sketcher";
    sMenuText = QT_TR_NOOP("Create circle");
    sToolTipText = QT_TR_NOOP("Create a circle in the sketch");
    sWhatsThis = "Sketcher_CreateCircle";
    sStatusTip = sToolTipText;
    sPixmap = "Sketcher_CreateCircle";
    sAccel = "G, C";
    eType = ForEdit;
}

CONSTRUCTION_UPDATE_ACTION(CmdSketcherCreateCircle, "Sketcher_CreateCircle")

void CmdSketcherCreateCircle::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    ActivateHandler(getActiveGuiDocument(), new DrawSketchHandlerCircle());
}

bool CmdSketcherCreateCircle::isActive()
{
    return isCommandActive(getActiveGuiDocument());
}

// ======================================================================================

/// @brief Macro that declares a new sketcher command class 'CmdSketcherCreateEllipseByCenter'
DEF_STD_CMD_AU(CmdSketcherCreateEllipseByCenter)

/**
 * @brief ctor
 */
CmdSketcherCreateEllipseByCenter::CmdSketcherCreateEllipseByCenter()
    : Command("Sketcher_CreateEllipseByCenter")
{
    sAppModule = "Sketcher";
    sGroup = "Sketcher";
    sMenuText = QT_TR_NOOP("Create ellipse by center");
    sToolTipText = QT_TR_NOOP("Create an ellipse by center in the sketch");
    sWhatsThis = "Sketcher_CreateEllipseByCenter";
    sStatusTip = sToolTipText;
    sPixmap = "Sketcher_CreateEllipseByCenter";
    sAccel = "G, E, E";
    eType = ForEdit;
}

CONSTRUCTION_UPDATE_ACTION(CmdSketcherCreateEllipseByCenter, "Sketcher_CreateEllipseByCenter")

void CmdSketcherCreateEllipseByCenter::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    ActivateHandler(getActiveGuiDocument(), new DrawSketchHandlerEllipse());
}

bool CmdSketcherCreateEllipseByCenter::isActive()
{
    return isCommandActive(getActiveGuiDocument());
}

/// @brief Macro that declares a new sketcher command class 'CmdSketcherCreateEllipseBy3Points'
DEF_STD_CMD_AU(CmdSketcherCreateEllipseBy3Points)

/**
 * @brief ctor
 */
CmdSketcherCreateEllipseBy3Points::CmdSketcherCreateEllipseBy3Points()
    : Command("Sketcher_CreateEllipseBy3Points")
{
    sAppModule = "Sketcher";
    sGroup = "Sketcher";
    sMenuText = QT_TR_NOOP("Create ellipse by 3 points");
    sToolTipText = QT_TR_NOOP("Create an ellipse by 3 points in the sketch");
    sWhatsThis = "Sketcher_CreateEllipseBy3Points";
    sStatusTip = sToolTipText;
    sPixmap = "Sketcher_CreateEllipse_3points";
    sAccel = "G, 3, E";
    eType = ForEdit;
}

CONSTRUCTION_UPDATE_ACTION(CmdSketcherCreateEllipseBy3Points, "Sketcher_CreateEllipse_3points")

void CmdSketcherCreateEllipseBy3Points::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    ActivateHandler(getActiveGuiDocument(),
                    new DrawSketchHandlerEllipse(
                        ConstructionMethods::CircleEllipseConstructionMethod::ThreeRim));
}

bool CmdSketcherCreateEllipseBy3Points::isActive()
{
    return isCommandActive(getActiveGuiDocument());
}

DEF_STD_CMD_AU(CmdSketcherCreateArcOfEllipse)

CmdSketcherCreateArcOfEllipse::CmdSketcherCreateArcOfEllipse()
    : Command("Sketcher_CreateArcOfEllipse")
{
    sAppModule = "Sketcher";
    sGroup = "Sketcher";
    sMenuText = QT_TR_NOOP("Create arc of ellipse");
    sToolTipText = QT_TR_NOOP("Create an arc of ellipse in the sketch");
    sWhatsThis = "Sketcher_CreateArcOfEllipse";
    sStatusTip = sToolTipText;
    sPixmap = "Sketcher_CreateElliptical_Arc";
    sAccel = "G, E, A";
    eType = ForEdit;
}

CONSTRUCTION_UPDATE_ACTION(CmdSketcherCreateArcOfEllipse, "Sketcher_CreateElliptical_Arc")

void CmdSketcherCreateArcOfEllipse::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    ActivateHandler(getActiveGuiDocument(), new DrawSketchHandlerArcOfEllipse());
}

bool CmdSketcherCreateArcOfEllipse::isActive()
{
    return isCommandActive(getActiveGuiDocument());
}

DEF_STD_CMD_AU(CmdSketcherCreateArcOfHyperbola)

CmdSketcherCreateArcOfHyperbola::CmdSketcherCreateArcOfHyperbola()
    : Command("Sketcher_CreateArcOfHyperbola")
{
    sAppModule = "Sketcher";
    sGroup = "Sketcher";
    sMenuText = QT_TR_NOOP("Create arc of hyperbola");
    sToolTipText = QT_TR_NOOP("Create an arc of hyperbola in the sketch");
    sWhatsThis = "Sketcher_CreateArcOfHyperbola";
    sStatusTip = sToolTipText;
    sPixmap = "Sketcher_CreateHyperbolic_Arc";
    sAccel = "G, H";
    eType = ForEdit;
}

CONSTRUCTION_UPDATE_ACTION(CmdSketcherCreateArcOfHyperbola, "Sketcher_CreateHyperbolic_Arc")

void CmdSketcherCreateArcOfHyperbola::activated(int /*iMsg*/)
{
    ActivateHandler(getActiveGuiDocument(), new DrawSketchHandlerArcOfHyperbola());
}

bool CmdSketcherCreateArcOfHyperbola::isActive()
{
    return isCommandActive(getActiveGuiDocument());
}

DEF_STD_CMD_AU(CmdSketcherCreateArcOfParabola)

CmdSketcherCreateArcOfParabola::CmdSketcherCreateArcOfParabola()
    : Command("Sketcher_CreateArcOfParabola")
{
    sAppModule = "Sketcher";
    sGroup = "Sketcher";
    sMenuText = QT_TR_NOOP("Create arc of parabola");
    sToolTipText = QT_TR_NOOP("Create an arc of parabola in the sketch");
    sWhatsThis = "Sketcher_CreateArcOfParabola";
    sStatusTip = sToolTipText;
    sPixmap = "Sketcher_CreateParabolic_Arc";
    sAccel = "G, J";
    eType = ForEdit;
}

CONSTRUCTION_UPDATE_ACTION(CmdSketcherCreateArcOfParabola, "Sketcher_CreateParabolic_Arc")

void CmdSketcherCreateArcOfParabola::activated(int /*iMsg*/)
{
    ActivateHandler(getActiveGuiDocument(), new DrawSketchHandlerArcOfParabola());
}

bool CmdSketcherCreateArcOfParabola::isActive()
{
    return isCommandActive(getActiveGuiDocument());
}


class CmdSketcherCompCreateConic : public Gui::GroupCommand
{
public:
    CmdSketcherCompCreateConic();
    virtual const char* className() const {return "CmdSketcherCompCreateConic";}
};

CmdSketcherCompCreateConic::CmdSketcherCompCreateConic()
  : GroupCommand("Sketcher_CompCreateConic")
{
    sAppModule      = "Sketcher";
    sGroup          = "Sketcher";
    sMenuText       = QT_TR_NOOP("Create a conic");
    sToolTipText    = QT_TR_NOOP("Actions for Creating a conic in the sketch");
    sWhatsThis      = "Sketcher_CompCreateConic";
    sStatusTip      = sToolTipText;
    eType           = ForEdit;
    addCommand(new CmdSketcherCreateEllipseByCenter());
    addCommand(new CmdSketcherCreateEllipseBy3Points());
    addCommand(new CmdSketcherCreateArcOfEllipse());
    addCommand(new CmdSketcherCreateArcOfHyperbola());
    addCommand(new CmdSketcherCreateArcOfParabola());
}

// ======================================================================================

DEF_STD_CMD_AU(CmdSketcherCreateBSpline)

CmdSketcherCreateBSpline::CmdSketcherCreateBSpline()
    : Command("Sketcher_CreateBSpline")
{
    sAppModule = "Sketcher";
    sGroup = "Sketcher";
    sMenuText = QT_TR_NOOP("Create B-spline");
    sToolTipText = QT_TR_NOOP("Create a B-spline by control points in the sketch.");
    sWhatsThis = "Sketcher_CreateBSpline";
    sStatusTip = sToolTipText;
    sPixmap = "Sketcher_CreateBSpline";
    sAccel = "G, B, B";
    eType = ForEdit;
}

CONSTRUCTION_UPDATE_ACTION(CmdSketcherCreateBSpline, "Sketcher_CreateBSpline")

void CmdSketcherCreateBSpline::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    ActivateHandler(getActiveGuiDocument(), new DrawSketchHandlerBSpline(0));
}

bool CmdSketcherCreateBSpline::isActive()
{
    return isCommandActive(getActiveGuiDocument());
}

/// @brief Macro that declares a new sketcher command class 'CmdSketcherCreateBSpline'
DEF_STD_CMD_AU(CmdSketcherCreatePeriodicBSpline)

/**
 * @brief ctor
 */
CmdSketcherCreatePeriodicBSpline::CmdSketcherCreatePeriodicBSpline()
    : Command("Sketcher_CreatePeriodicBSpline")
{
    sAppModule = "Sketcher";
    sGroup = "Sketcher";
    sMenuText = QT_TR_NOOP("Create periodic B-spline");
    sToolTipText = QT_TR_NOOP("Create a periodic B-spline by control points in the sketch.");
    sWhatsThis = "Sketcher_CreatePeriodicBSpline";
    sStatusTip = sToolTipText;
    sPixmap = "Sketcher_Create_Periodic_BSpline";
    sAccel = "G, B, P";
    eType = ForEdit;
}

CONSTRUCTION_UPDATE_ACTION(CmdSketcherCreatePeriodicBSpline, "Sketcher_Create_Periodic_BSpline")

void CmdSketcherCreatePeriodicBSpline::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    ActivateHandler(getActiveGuiDocument(), new DrawSketchHandlerBSpline(1));
}

bool CmdSketcherCreatePeriodicBSpline::isActive()
{
    return isCommandActive(getActiveGuiDocument());
}

/// @brief Macro that declares a new sketcher command class
/// 'CmdSketcherCreateBSplineByInterpolation'
DEF_STD_CMD_AU(CmdSketcherCreateBSplineByInterpolation)

CmdSketcherCreateBSplineByInterpolation::CmdSketcherCreateBSplineByInterpolation()
    : Command("Sketcher_CreateBSplineByInterpolation")
{
    sAppModule = "Sketcher";
    sGroup = "Sketcher";
    sMenuText = QT_TR_NOOP("Create B-spline by knots");
    sToolTipText = QT_TR_NOOP("Create a B-spline by knots, i.e. by interpolation, in the sketch.");
    sWhatsThis = "Sketcher_CreateBSplineByInterpolation";
    sStatusTip = sToolTipText;
    sPixmap = "Sketcher_CreateBSplineByInterpolation";
    sAccel = "G, B, I";
    eType = ForEdit;
}

CONSTRUCTION_UPDATE_ACTION(CmdSketcherCreateBSplineByInterpolation,
                           "Sketcher_CreateBSplineByInterpolation")

void CmdSketcherCreateBSplineByInterpolation::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    ActivateHandler(getActiveGuiDocument(), new DrawSketchHandlerBSplineByInterpolation(0));
}

bool CmdSketcherCreateBSplineByInterpolation::isActive()
{
    return isCommandActive(getActiveGuiDocument());
}

/// @brief Macro that declares a new sketcher command class
/// 'CmdSketcherCreatePeriodicBSplineByInterpolation'
DEF_STD_CMD_AU(CmdSketcherCreatePeriodicBSplineByInterpolation)

CmdSketcherCreatePeriodicBSplineByInterpolation::CmdSketcherCreatePeriodicBSplineByInterpolation()
    : Command("Sketcher_CreatePeriodicBSplineByInterpolation")
{
    sAppModule = "Sketcher";
    sGroup = "Sketcher";
    sMenuText = QT_TR_NOOP("Create periodic B-spline by knots");
    sToolTipText =
        QT_TR_NOOP("Create a periodic B-spline by knots, i.e. by interpolation, in the sketch.");
    sWhatsThis = "Sketcher_Create_Periodic_BSplineByInterpolation";
    sStatusTip = sToolTipText;
    sPixmap = "Sketcher_Create_Periodic_BSplineByInterpolation";
    sAccel = "G, B, O";
    eType = ForEdit;
}

CONSTRUCTION_UPDATE_ACTION(CmdSketcherCreatePeriodicBSplineByInterpolation,
                           "Sketcher_CreatePeriodicBSplineByInterpolation")

void CmdSketcherCreatePeriodicBSplineByInterpolation::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    ActivateHandler(getActiveGuiDocument(), new DrawSketchHandlerBSplineByInterpolation(1));
}

bool CmdSketcherCreatePeriodicBSplineByInterpolation::isActive()
{
    return isCommandActive(getActiveGuiDocument());
}


class CmdSketcherCompCreateBSpline: public Gui::GroupCommand
{
public:
    CmdSketcherCompCreateBSpline();
    virtual const char* className() const {return "CmdSketcherCompCreateBSpline";}
};


/**
 * @brief ctor
 */
CmdSketcherCompCreateBSpline::CmdSketcherCompCreateBSpline()
: GroupCommand("Sketcher_CompCreateBSpline")
{
    sAppModule      = "Sketcher";
    sGroup          = "Sketcher";
    sMenuText       = QT_TR_NOOP("Create a B-spline");
    sToolTipText    = QT_TR_NOOP("Create a B-spline in the sketch");
    sWhatsThis      = "Sketcher_CompCreateBSpline";
    sStatusTip      = sToolTipText;
    eType           = ForEdit;
    addCommand(new CmdSketcherCreateBSpline());
    addCommand(new CmdSketcherCreatePeriodicBSpline());
    addCommand(new CmdSketcherCreateBSplineByInterpolation());
    addCommand(new CmdSketcherCreatePeriodicBSplineByInterpolation());
}

// ======================================================================================

DEF_STD_CMD_AU(CmdSketcherCreate3PointCircle)

CmdSketcherCreate3PointCircle::CmdSketcherCreate3PointCircle()
    : Command("Sketcher_Create3PointCircle")
{
    sAppModule = "Sketcher";
    sGroup = "Sketcher";
    sMenuText = QT_TR_NOOP("Create circle by three points");
    sToolTipText = QT_TR_NOOP("Create a circle by 3 perimeter points");
    sWhatsThis = "Sketcher_Create3PointCircle";
    sStatusTip = sToolTipText;
    sPixmap = "Sketcher_Create3PointCircle";
    sAccel = "G, 3, C";
    eType = ForEdit;
}

CONSTRUCTION_UPDATE_ACTION(CmdSketcherCreate3PointCircle, "Sketcher_Create3PointCircle")

void CmdSketcherCreate3PointCircle::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    ActivateHandler(getActiveGuiDocument(),
                    new DrawSketchHandlerCircle(
                        ConstructionMethods::CircleEllipseConstructionMethod::ThreeRim));
}

bool CmdSketcherCreate3PointCircle::isActive()
{
    return isCommandActive(getActiveGuiDocument());
}

class CmdSketcherCompCreateCircle: public Gui::GroupCommand
{
public:
    CmdSketcherCompCreateCircle();
    virtual const char* className() const {return "CmdSketcherCompCreateCircle";}
};

CmdSketcherCompCreateCircle::CmdSketcherCompCreateCircle()
  : GroupCommand("Sketcher_CompCreateCircle")
{
    sAppModule      = "Sketcher";
    sGroup          = "Sketcher";
    sMenuText       = QT_TR_NOOP("Create circle");
    sToolTipText    = QT_TR_NOOP("Create a circle in the sketcher");
    sWhatsThis      = "Sketcher_CompCreateCircle";
    sStatusTip      = sToolTipText;
    eType           = ForEdit;
    addCommand(new CmdSketcherCreateCircle());
    addCommand(new CmdSketcherCreate3PointCircle());
}


// ======================================================================================

DEF_STD_CMD_A(CmdSketcherCreatePoint)

CmdSketcherCreatePoint::CmdSketcherCreatePoint()
    : Command("Sketcher_CreatePoint")
{
    sAppModule = "Sketcher";
    sGroup = "Sketcher";
    sMenuText = QT_TR_NOOP("Create point");
    sToolTipText = QT_TR_NOOP("Create a point in the sketch");
    sWhatsThis = "Sketcher_CreatePoint";
    sStatusTip = sToolTipText;
    sPixmap = "Sketcher_CreatePoint";
    sAccel = "G, Y";
    eType = ForEdit;
}

void CmdSketcherCreatePoint::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    ActivateHandler(getActiveGuiDocument(), new DrawSketchHandlerPoint());
}

bool CmdSketcherCreatePoint::isActive()
{
    return isCommandActive(getActiveGuiDocument());
}

// ======================================================================================

DEF_STD_CMD_A(CmdSketcherCreateFillet)

CmdSketcherCreateFillet::CmdSketcherCreateFillet()
    : Command("Sketcher_CreateFillet")
{
    sAppModule = "Sketcher";
    sGroup = "Sketcher";
    sMenuText = QT_TR_NOOP("Create fillet");
    sToolTipText = QT_TR_NOOP("Create a fillet between two lines or at a coincident point");
    sWhatsThis = "Sketcher_CreateFillet";
    sStatusTip = sToolTipText;
    sPixmap = "Sketcher_CreateFillet";
    sAccel = "G, F, F";
    eType = ForEdit;
}

void CmdSketcherCreateFillet::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    ActivateHandler(
        getActiveGuiDocument(),
        new DrawSketchHandlerFillet(ConstructionMethods::FilletConstructionMethod::Fillet));
}

bool CmdSketcherCreateFillet::isActive()
{
    return isCommandActive(getActiveGuiDocument());
}

// ======================================================================================

DEF_STD_CMD_A(CmdSketcherCreateChamfer)

CmdSketcherCreateChamfer::CmdSketcherCreateChamfer()
    : Command("Sketcher_CreateChamfer")
{
    sAppModule = "Sketcher";
    sGroup = "Sketcher";
    sMenuText = QT_TR_NOOP("Create chamfer");
    sToolTipText = QT_TR_NOOP("Create a chamfer between two lines or at a coincident point");
    sWhatsThis = "Sketcher_CreateChamfer";
    sStatusTip = sToolTipText;
    sPixmap = "Sketcher_CreateChamfer";
    sAccel = "G, F, C";
    eType = ForEdit;
}

void CmdSketcherCreateChamfer::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    ActivateHandler(
        getActiveGuiDocument(),
        new DrawSketchHandlerFillet(ConstructionMethods::FilletConstructionMethod::Chamfer));
}

bool CmdSketcherCreateChamfer::isActive()
{
    return isCommandActive(getActiveGuiDocument());
}

class CmdSketcherCompCreateFillets: public Gui::GroupCommand
{
public:
    CmdSketcherCompCreateFillets()
        : GroupCommand("Sketcher_CompCreateFillets")
    {
        sAppModule = "Sketcher";
        sGroup = "Sketcher";
        sMenuText = QT_TR_NOOP("Create fillet or chamfer");
        sToolTipText = QT_TR_NOOP("Create a fillet or chamfer between two lines");
        sWhatsThis = "Sketcher_CompCreateFillets";
        sStatusTip = sToolTipText;
        eType = ForEdit;

        setCheckable(false);

        addCommand("Sketcher_CreateFillet");
        addCommand("Sketcher_CreateChamfer");
    }

    const char* className() const override
    {
        return "CmdSketcherCompCreateFillets";
    }
};


// ======================================================================================

DEF_STD_CMD_A(CmdSketcherTrimming)

CmdSketcherTrimming::CmdSketcherTrimming()
    : Command("Sketcher_Trimming")
{
    sAppModule = "Sketcher";
    sGroup = "Sketcher";
    sMenuText = QT_TR_NOOP("Trim edge");
    sToolTipText = QT_TR_NOOP("Trim an edge with respect to the picked position");
    sWhatsThis = "Sketcher_Trimming";
    sStatusTip = sToolTipText;
    sPixmap = "Sketcher_Trimming";
    sAccel = "G, T";
    eType = ForEdit;
}

void CmdSketcherTrimming::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    ActivateHandler(getActiveGuiDocument(), new DrawSketchHandlerTrimming());
}

bool CmdSketcherTrimming::isActive()
{
    return isCommandActive(getActiveGuiDocument());
}


// ======================================================================================


DEF_STD_CMD_A(CmdSketcherExtend)

// TODO: fix the translations for this
CmdSketcherExtend::CmdSketcherExtend()
    : Command("Sketcher_Extend")
{
    sAppModule = "Sketcher";
    sGroup = "Sketcher";
    sMenuText = QT_TR_NOOP("Extend edge");
    sToolTipText = QT_TR_NOOP("Extend an edge with respect to the picked position");
    sWhatsThis = "Sketcher_Extend";
    sStatusTip = sToolTipText;
    sPixmap = "Sketcher_Extend";
    sAccel = "G, Q";
    eType = ForEdit;
}

void CmdSketcherExtend::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    ActivateHandler(getActiveGuiDocument(), new DrawSketchHandlerExtend());
}

bool CmdSketcherExtend::isActive()
{
    return isCommandActive(getActiveGuiDocument());
}


// ======================================================================================

DEF_STD_CMD_A(CmdSketcherSplit)

// TODO: fix the translations for this
CmdSketcherSplit::CmdSketcherSplit()
    : Command("Sketcher_Split")
{
    sAppModule = "Sketcher";
    sGroup = "Sketcher";
    sMenuText = QT_TR_NOOP("Split edge");
    sToolTipText = QT_TR_NOOP("Splits an edge into two while preserving constraints");
    sWhatsThis = "Sketcher_Split";
    sStatusTip = sToolTipText;
    sPixmap = "Sketcher_Split";
    sAccel = "G, Z";
    eType = ForEdit;
}

void CmdSketcherSplit::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    ActivateHandler(getActiveGuiDocument(), new DrawSketchHandlerSplitting());
}

bool CmdSketcherSplit::isActive()
{
    return isCommandActive(getActiveGuiDocument());
}

// Comp for curve edition tools =======================================================

class CmdSketcherCompCurveEdition: public Gui::GroupCommand
{
public:
    CmdSketcherCompCurveEdition()
        : GroupCommand("Sketcher_CompCurveEdition")
    {
        sAppModule = "Sketcher";
        sGroup = "Sketcher";
        sMenuText = QT_TR_NOOP("Curve Edition");
        sToolTipText = QT_TR_NOOP("Curve Edition tools.");
        sWhatsThis = "Sketcher_CompCurveEdition";
        sStatusTip = sToolTipText;
        eType = ForEdit;

        setCheckable(false);

        addCommand("Sketcher_Trimming");
        addCommand("Sketcher_Split");
        addCommand("Sketcher_Extend");
    }

    const char* className() const override
    {
        return "CmdSketcherCompCurveEdition";
    }
};

// ======================================================================================

DEF_STD_CMD_A(CmdSketcherExternal)

CmdSketcherExternal::CmdSketcherExternal()
    : Command("Sketcher_External")
{
    sAppModule = "Sketcher";
    sGroup = "Sketcher";
    sMenuText = QT_TR_NOOP("Create external geometry");
    sToolTipText = QT_TR_NOOP("Create an edge linked to an external geometry");
    sWhatsThis = "Sketcher_External";
    sStatusTip = sToolTipText;
    sPixmap = "Sketcher_External";
    sAccel = "G, X";
    eType = ForEdit;
}

void CmdSketcherExternal::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    ActivateHandler(getActiveGuiDocument(), new DrawSketchHandlerExternal());
}

bool CmdSketcherExternal::isActive()
{
    return isCommandActive(getActiveGuiDocument());
}

DEF_STD_CMD_A(CmdSketcherDefining);

CmdSketcherDefining::CmdSketcherDefining()
  : Command("Sketcher_Defining")
{
    sAppModule      = "Sketcher";
    sGroup          = QT_TR_NOOP("Sketcher");
    sMenuText       = QT_TR_NOOP("Add/toggle defining geometry");
    sToolTipText    = QT_TR_NOOP("Import an external geometry as defining geometry that can be use for extrusion.\n"
                                 "Or toggle the defining status of an already imported external geometry.");
    sWhatsThis      = "Sketcher_Defining";
    sStatusTip      = sToolTipText;
    sPixmap         = "Sketcher_Defining";
    sAccel          = "X, C";
    eType           = ForEdit;
}

static Sketcher::SketchObject *getExternalSelection(std::vector<int> *sels=0, bool force=false) {
    auto doc = Gui::Application::Instance->editDocument();
    if(!doc)
        return 0;
    auto sketchgui = dynamic_cast<ViewProviderSketch*>(doc->getInEdit());
    if (!sketchgui)
        return 0;
    auto sketch = static_cast<Sketcher::SketchObject*>(sketchgui->getObject());
    for(auto &sel : Gui::Selection().getCompleteSelection()) {
        int geoId;
        if(sel.pObject != sketch
                || !sketch->geoIdFromShapeType(sel.SubName,geoId)
                || geoId>Sketcher::GeoEnum::RefExt)
            continue;
        if(!sels)
            return sketch;
        sels->push_back(geoId);
    }
    if(!force && (!sels || sels->empty()))
        return 0;
    return sketch;
}

void CmdSketcherDefining::activated(int iMsg)
{
    Q_UNUSED(iMsg);

    std::vector<int> sels;
    auto sketch = getExternalSelection(&sels);
    if(!sketch)
        ActivateHandler(getActiveGuiDocument(), new DrawSketchHandlerExternal(true));
    else {
        Gui::Selection().clearSelection();
        openCommand("Toggle defining geometry");
        sketch->toggleConstructions(sels);
        tryAutoRecomputeIfNotSolve(sketch);
        commitCommand();
    }
}

bool CmdSketcherDefining::isActive()
{
    return isCommandActive(getActiveGuiDocument());
}

DEF_STD_CMD_A(CmdSketcherIntersection)

CmdSketcherIntersection::CmdSketcherIntersection()
  : Command("Sketcher_Intersection")
{
    sAppModule      = "Sketcher";
    sGroup          = "Sketcher";
    sMenuText       = QT_TR_NOOP("Add/toggle intersection geometry");
    sToolTipText    = QT_TR_NOOP("Import an external geometry with intersection to the sketch plane.\n"
                                 "Or toggle the intersection of an already imported external geometry.");
    sWhatsThis      = "Sketcher_Intersection";
    sStatusTip      = sToolTipText;
    sPixmap         = "Sketcher_Intersection";
    sAccel          = "G, 6";
    eType           = ForEdit;
}

void CmdSketcherIntersection::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    std::vector<int> sels;
    auto sketch = getExternalSelection(&sels);
    if(!sketch)
        ActivateHandler(getActiveGuiDocument(), new DrawSketchHandlerExternal(false, true));
    else {
        Gui::Selection().clearSelection();
        openCommand("Toggle intersection geometry");
        sketch->toggleIntersection(sels);
        tryAutoRecomputeIfNotSolve(sketch);
        commitCommand();
    }
}

bool CmdSketcherIntersection::isActive()
{
    return isCommandActive(getActiveGuiDocument());
}

DEF_STD_CMD_A(CmdSketcherIntersectionDefining)

CmdSketcherIntersectionDefining::CmdSketcherIntersectionDefining()
  : Command("Sketcher_IntersectionDefining")
{
    sAppModule      = "Sketcher";
    sGroup          = "Sketcher";
    sMenuText       = QT_TR_NOOP("Add/toggle intersection defining");
    sToolTipText    = QT_TR_NOOP("Import as an defining external geometry with intersection to the sketch plane.\n"
                                 "Or toggle the intersection of an already imported external geometry and make it defining.");
    sWhatsThis      = "Sketcher_IntersectionDefining";
    sStatusTip      = sToolTipText;
    sPixmap         = "Sketcher_IntersectionDefining";
    sAccel          = "G, 5";
    eType           = ForEdit;
}

void CmdSketcherIntersectionDefining::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    std::vector<int> sels;
    auto sketch = getExternalSelection(&sels);
    if(!sketch)
        ActivateHandler(getActiveGuiDocument(), new DrawSketchHandlerExternal(true, true));
    else {
        Gui::Selection().clearSelection();
        openCommand("Toggle intersection defining");
        sketch->toggleIntersection(sels, true);
        tryAutoRecomputeIfNotSolve(sketch);
        commitCommand();
    }
}

bool CmdSketcherIntersectionDefining::isActive()
{
    return isCommandActive(getActiveGuiDocument());
}

DEF_STD_CMD_A(CmdSketcherAttach);

CmdSketcherAttach::CmdSketcherAttach()
  : Command("Sketcher_Attach")
{
    sAppModule      = "Sketcher";
    sGroup          = QT_TR_NOOP("Sketcher");
    sMenuText       = QT_TR_NOOP("Attach geometry");
    sToolTipText    = QT_TR_NOOP("Attach a missing or detached geometry to a new external geometry element");
    sWhatsThis      = "Sketcher_Attach";
    sStatusTip      = sToolTipText;
    sPixmap         = "Sketcher_Attach";
    sAccel          = "X, A";
    eType           = ForEdit;
}

void CmdSketcherAttach::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    std::vector<int> sels;
    auto sketch = getExternalSelection(&sels);
    if(!sketch)
        return;
    ActivateHandler(getActiveGuiDocument(), new DrawSketchHandlerExternal(std::move(sels)));
}

bool CmdSketcherAttach::isActive()
{
    return getExternalSelection()!=0;
}

DEF_STD_CMD_A(CmdSketcherDetach);

CmdSketcherDetach::CmdSketcherDetach()
  : Command("Sketcher_Detach")
{
    sAppModule      = "Sketcher";
    sGroup          = QT_TR_NOOP("Sketcher");
    sMenuText       = QT_TR_NOOP("Detach geometry");
    sToolTipText    = QT_TR_NOOP("Detach an external geometry");
    sWhatsThis      = "Sketcher_Detach";
    sStatusTip      = sToolTipText;
    sPixmap         = "Sketcher_Detach";
    sAccel          = "X, D";
    eType           = ForEdit;
}

void CmdSketcherDetach::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    std::vector<int> sels;
    auto sketch = getExternalSelection(&sels);
    if(!sketch)
        return;

    Gui::Selection().clearSelection();

    openCommand("Detach external geometry");
    if(sketch->detachExternal(sels)) {
        abortCommand();
        return;
    }
    tryAutoRecomputeIfNotSolve(sketch);
    commitCommand();
}

bool CmdSketcherDetach::isActive()
{
    return getExternalSelection()!=0;
}

DEF_STD_CMD_A(CmdSketcherToggleFreeze);

CmdSketcherToggleFreeze::CmdSketcherToggleFreeze()
  : Command("Sketcher_ToggleFreeze")
{
    sAppModule      = "Sketcher";
    sGroup          = QT_TR_NOOP("Sketcher");
    sMenuText       = QT_TR_NOOP("Toggle freeze");
    sToolTipText    = QT_TR_NOOP("Toggle freeze of an external geometry");
    sWhatsThis      = "Sketcher_ToggleFreeze";
    sStatusTip      = sToolTipText;
    sPixmap         = "Sketcher_ToggleFreeze";
    sAccel          = "X, F";
    eType           = ForEdit;
}

void CmdSketcherToggleFreeze::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    std::vector<int> sels;
    auto sketch = getExternalSelection(&sels);
    if(!sketch)
        return;
    Gui::Selection().clearSelection();
    openCommand("Toggle freeze geometry");
    if(sketch->toggleFreeze(sels)) {
        abortCommand();
        return;
    }
    tryAutoRecomputeIfNotSolve(sketch);
    commitCommand();
}

bool CmdSketcherToggleFreeze::isActive()
{
    return getExternalSelection(0)!=0;
}

DEF_STD_CMD_A(CmdSketcherSync);

CmdSketcherSync::CmdSketcherSync()
  : Command("Sketcher_Sync")
{
    sAppModule      = "Sketcher";
    sGroup          = QT_TR_NOOP("Sketcher");
    sMenuText       = QT_TR_NOOP("Sync geometry");
    sToolTipText    = QT_TR_NOOP("Synchronize a frozen external geometry");
    sWhatsThis      = "Sketcher_Sync";
    sStatusTip      = sToolTipText;
    sPixmap         = "Sketcher_Sync";
    sAccel          = "X, S";
    eType           = ForEdit;
}

void CmdSketcherSync::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    std::vector<int> sels;
    auto sketch = getExternalSelection(&sels);
    if(!sketch)
        return;
    Gui::Selection().clearSelection();
    openCommand("Sync frozen geometry");
    if(!sketch->syncGeometry(sels))
        tryAutoRecomputeIfNotSolve(sketch);
    commitCommand();
}

bool CmdSketcherSync::isActive()
{
    return getExternalSelection(0)!=0;
}


DEF_STD_CMD_A(CmdSketcherFixExternal);

CmdSketcherFixExternal::CmdSketcherFixExternal()
  : Command("Sketcher_FixExternal")
{
    sAppModule      = "Sketcher";
    sGroup          = QT_TR_NOOP("Sketcher");
    sMenuText       = QT_TR_NOOP("Fix reference");
    sToolTipText    = QT_TR_NOOP("Fix external geometry by guessing its geometry reference");
    sWhatsThis      = "Sketcher_FixExternal";
    sStatusTip      = sToolTipText;
    sPixmap         = "Sketcher_FixExternal";
    sAccel          = "X, R";
    eType           = ForEdit;
}

void CmdSketcherFixExternal::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    std::vector<int> sels;
    auto sketch = getExternalSelection(&sels,true);
    if(!sketch)
        return;

    openCommand("Fix external geometry");
    try {
        sketch->fixExternalGeometry(sels);
        tryAutoRecomputeIfNotSolve(sketch);
    } catch(Base::Exception &e) {
        e.ReportException();
    }
    commitCommand();
}

bool CmdSketcherFixExternal::isActive()
{
    return getExternalSelection(0)!=0;
}


class CmdSketcherExternalCmds : public Gui::GroupCommand
{
public:
    CmdSketcherExternalCmds():GroupCommand("Sketcher_ExternalCmds") {
        sGroup        = QT_TR_NOOP("Sketcher");
        sMenuText     = QT_TR_NOOP("External geometry actions");
        sToolTipText  = QT_TR_NOOP("Sketcher external geometry actions");
        sWhatsThis    = "Sketcher_ExternalCmds";
        sStatusTip    = QT_TR_NOOP("Sketcher external geometry actions");
        eType         = 0;
        bCanLog       = false;

        addCommand(new CmdSketcherExternal());
        addCommand(new CmdSketcherDefining());
        addCommand(new CmdSketcherIntersection());
        addCommand(new CmdSketcherIntersectionDefining());
        addCommand(new CmdSketcherDetach());
        addCommand(new CmdSketcherAttach());
        addCommand(new CmdSketcherToggleFreeze());
        addCommand(new CmdSketcherSync());
        addCommand(new CmdSketcherFixExternal());
    }

    virtual const char* className() const {return "CmdSketcherExternalCmds";}
};


// ======================================================================================

DEF_STD_CMD_AU(CmdSketcherCarbonCopy)

CmdSketcherCarbonCopy::CmdSketcherCarbonCopy()
    : Command("Sketcher_CarbonCopy")
{
    sAppModule = "Sketcher";
    sGroup = "Sketcher";
    sMenuText = QT_TR_NOOP("Create carbon copy");
    sToolTipText = QT_TR_NOOP("Copy the geometry of another sketch");
    sWhatsThis = "Sketcher_CarbonCopy";
    sStatusTip = sToolTipText;
    sPixmap = "Sketcher_CarbonCopy";
    sAccel = "G, W";
    eType = ForEdit;
}

CONSTRUCTION_UPDATE_ACTION(CmdSketcherCarbonCopy, "Sketcher_CarbonCopy")

void CmdSketcherCarbonCopy::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    ActivateHandler(getActiveGuiDocument(), new DrawSketchHandlerCarbonCopy());
}

bool CmdSketcherCarbonCopy::isActive()
{
    return isCommandActive(getActiveGuiDocument());
}

// Comp for slot tools =============================================

class CmdSketcherCompSlot: public Gui::GroupCommand
{
public:
    CmdSketcherCompSlot()
        : GroupCommand("Sketcher_CompSlot")
    {
        sAppModule = "Sketcher";
        sGroup = "Sketcher";
        sMenuText = QT_TR_NOOP("Slots");
        sToolTipText = QT_TR_NOOP("Slot tools.");
        sWhatsThis = "Sketcher_CompSlot";
        sStatusTip = sToolTipText;
        eType = ForEdit;

        setCheckable(false);

        addCommand("Sketcher_CreateSlot");
        addCommand("Sketcher_CreateArcSlot");
    }

    void updateAction(int mode) override
    {
        Gui::ActionGroup* pcAction = qobject_cast<Gui::ActionGroup*>(getAction());
        if (!pcAction) {
            return;
        }

        QList<QAction*> al = pcAction->actions();
        int index = pcAction->property("defaultAction").toInt();
        switch (static_cast<GeometryCreationMode>(mode)) {
            case GeometryCreationMode::Normal:
                al[0]->setIcon(Gui::BitmapFactory().iconFromTheme("Sketcher_CreateSlot"));
                al[1]->setIcon(Gui::BitmapFactory().iconFromTheme("Sketcher_CreateArcSlot"));
                getAction()->setIcon(al[index]->icon());
                break;
            case GeometryCreationMode::Construction:
                al[0]->setIcon(Gui::BitmapFactory().iconFromTheme("Sketcher_CreateSlot_Constr"));
                al[1]->setIcon(Gui::BitmapFactory().iconFromTheme("Sketcher_CreateArcSlot_Constr"));
                getAction()->setIcon(al[index]->icon());
                break;
        }
    }

    const char* className() const override
    {
        return "CmdSketcherCompSlot";
    }
};

/* Create Slot =============================================================*/

DEF_STD_CMD_AU(CmdSketcherCreateSlot)

CmdSketcherCreateSlot::CmdSketcherCreateSlot()
    : Command("Sketcher_CreateSlot")
{
    sAppModule = "Sketcher";
    sGroup = "Sketcher";
    sMenuText = QT_TR_NOOP("Create slot");
    sToolTipText = QT_TR_NOOP("Create a slot in the sketch");
    sWhatsThis = "Sketcher_CreateSlot";
    sStatusTip = sToolTipText;
    sPixmap = "Sketcher_CreateSlot";
    sAccel = "G, S";
    eType = ForEdit;
}

CONSTRUCTION_UPDATE_ACTION(CmdSketcherCreateSlot, "Sketcher_CreateSlot")

void CmdSketcherCreateSlot::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    ActivateHandler(getActiveGuiDocument(), new DrawSketchHandlerSlot());
}

bool CmdSketcherCreateSlot::isActive()
{
    return isCommandActive(getActiveGuiDocument());
}

/* Create Arc Slot =========================================================*/

DEF_STD_CMD_AU(CmdSketcherCreateArcSlot)

CmdSketcherCreateArcSlot::CmdSketcherCreateArcSlot()
    : Command("Sketcher_CreateArcSlot")
{
    sAppModule = "Sketcher";
    sGroup = "Sketcher";
    sMenuText = QT_TR_NOOP("Create arc slot");
    sToolTipText = QT_TR_NOOP("Create an arc slot in the sketch");
    sWhatsThis = "Sketcher_CreateArcSlot";
    sStatusTip = sToolTipText;
    sPixmap = "Sketcher_CreateArcSlot";
    sAccel = "G, S, 2";
    eType = ForEdit;
}

CONSTRUCTION_UPDATE_ACTION(CmdSketcherCreateArcSlot, "Sketcher_CreateArcSlot")

void CmdSketcherCreateArcSlot::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    ActivateHandler(getActiveGuiDocument(), new DrawSketchHandlerArcSlot());
}

bool CmdSketcherCreateArcSlot::isActive()
{
    return isCommandActive(getActiveGuiDocument());
}

/* Create Regular Polygon ==============================================*/

DEF_STD_CMD_AU(CmdSketcherCreateTriangle)

CmdSketcherCreateTriangle::CmdSketcherCreateTriangle()
    : Command("Sketcher_CreateTriangle")
{
    sAppModule = "Sketcher";
    sGroup = "Sketcher";
    sMenuText = QT_TR_NOOP("Create equilateral triangle");
    sToolTipText = QT_TR_NOOP("Create an equilateral triangle in the sketch");
    sWhatsThis = "Sketcher_CreateTriangle";
    sStatusTip = sToolTipText;
    sPixmap = "Sketcher_CreateTriangle";
    sAccel = "G, P, 3";
    eType = ForEdit;
}

CONSTRUCTION_UPDATE_ACTION(CmdSketcherCreateTriangle, "Sketcher_CreateTriangle")

void CmdSketcherCreateTriangle::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    ActivateHandler(getActiveGuiDocument(), new DrawSketchHandlerPolygon(3));
}

bool CmdSketcherCreateTriangle::isActive()
{
    return isCommandActive(getActiveGuiDocument());
}

DEF_STD_CMD_AU(CmdSketcherCreateSquare)

CmdSketcherCreateSquare::CmdSketcherCreateSquare()
    : Command("Sketcher_CreateSquare")
{
    sAppModule = "Sketcher";
    sGroup = "Sketcher";
    sMenuText = QT_TR_NOOP("Create square");
    sToolTipText = QT_TR_NOOP("Create a square in the sketch");
    sWhatsThis = "Sketcher_CreateSquare";
    sStatusTip = sToolTipText;
    sPixmap = "Sketcher_CreateSquare";
    sAccel = "G, P, 4";
    eType = ForEdit;
}

CONSTRUCTION_UPDATE_ACTION(CmdSketcherCreateSquare, "Sketcher_CreateSquare")

void CmdSketcherCreateSquare::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    ActivateHandler(getActiveGuiDocument(), new DrawSketchHandlerPolygon(4));
}

bool CmdSketcherCreateSquare::isActive()
{
    return isCommandActive(getActiveGuiDocument());
}

DEF_STD_CMD_AU(CmdSketcherCreatePentagon)

CmdSketcherCreatePentagon::CmdSketcherCreatePentagon()
    : Command("Sketcher_CreatePentagon")
{
    sAppModule = "Sketcher";
    sGroup = "Sketcher";
    sMenuText = QT_TR_NOOP("Create pentagon");
    sToolTipText = QT_TR_NOOP("Create a pentagon in the sketch");
    sWhatsThis = "Sketcher_CreatePentagon";
    sStatusTip = sToolTipText;
    sPixmap = "Sketcher_CreatePentagon";
    sAccel = "G, P, 5";
    eType = ForEdit;
}

CONSTRUCTION_UPDATE_ACTION(CmdSketcherCreatePentagon, "Sketcher_CreatePentagon")

void CmdSketcherCreatePentagon::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    ActivateHandler(getActiveGuiDocument(), new DrawSketchHandlerPolygon(5));
}

bool CmdSketcherCreatePentagon::isActive()
{
    return isCommandActive(getActiveGuiDocument());
}

DEF_STD_CMD_AU(CmdSketcherCreateHexagon)

CmdSketcherCreateHexagon::CmdSketcherCreateHexagon()
    : Command("Sketcher_CreateHexagon")
{
    sAppModule = "Sketcher";
    sGroup = "Sketcher";
    sMenuText = QT_TR_NOOP("Create hexagon");
    sToolTipText = QT_TR_NOOP("Create a hexagon in the sketch");
    sWhatsThis = "Sketcher_CreateHexagon";
    sStatusTip = sToolTipText;
    sPixmap = "Sketcher_CreateHexagon";
    sAccel = "G, P, 6";
    eType = ForEdit;
}

CONSTRUCTION_UPDATE_ACTION(CmdSketcherCreateHexagon, "Sketcher_CreateHexagon")

void CmdSketcherCreateHexagon::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    ActivateHandler(getActiveGuiDocument(), new DrawSketchHandlerPolygon(6));
}

bool CmdSketcherCreateHexagon::isActive()
{
    return isCommandActive(getActiveGuiDocument());
}

DEF_STD_CMD_AU(CmdSketcherCreateHeptagon)

CmdSketcherCreateHeptagon::CmdSketcherCreateHeptagon()
    : Command("Sketcher_CreateHeptagon")
{
    sAppModule = "Sketcher";
    sGroup = "Sketcher";
    sMenuText = QT_TR_NOOP("Create heptagon");
    sToolTipText = QT_TR_NOOP("Create a heptagon in the sketch");
    sWhatsThis = "Sketcher_CreateHeptagon";
    sStatusTip = sToolTipText;
    sPixmap = "Sketcher_CreateHeptagon";
    sAccel = "G, P, 7";
    eType = ForEdit;
}

CONSTRUCTION_UPDATE_ACTION(CmdSketcherCreateHeptagon, "Sketcher_CreateHeptagon")

void CmdSketcherCreateHeptagon::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    ActivateHandler(getActiveGuiDocument(), new DrawSketchHandlerPolygon(7));
}

bool CmdSketcherCreateHeptagon::isActive()
{
    return isCommandActive(getActiveGuiDocument());
}

DEF_STD_CMD_AU(CmdSketcherCreateOctagon)

CmdSketcherCreateOctagon::CmdSketcherCreateOctagon()
    : Command("Sketcher_CreateOctagon")
{
    sAppModule = "Sketcher";
    sGroup = "Sketcher";
    sMenuText = QT_TR_NOOP("Create octagon");
    sToolTipText = QT_TR_NOOP("Create an octagon in the sketch");
    sWhatsThis = "Sketcher_CreateOctagon";
    sStatusTip = sToolTipText;
    sPixmap = "Sketcher_CreateOctagon";
    sAccel = "G, P, 8";
    eType = ForEdit;
}

CONSTRUCTION_UPDATE_ACTION(CmdSketcherCreateOctagon, "Sketcher_CreateOctagon")

void CmdSketcherCreateOctagon::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    ActivateHandler(getActiveGuiDocument(), new DrawSketchHandlerPolygon(8));
}

bool CmdSketcherCreateOctagon::isActive()
{
    return isCommandActive(getActiveGuiDocument());
}

DEF_STD_CMD_AU(CmdSketcherCreateRegularPolygon)

CmdSketcherCreateRegularPolygon::CmdSketcherCreateRegularPolygon()
    : Command("Sketcher_CreateRegularPolygon")
{
    sAppModule = "Sketcher";
    sGroup = "Sketcher";
    sMenuText = QT_TR_NOOP("Create regular polygon");
    sToolTipText = QT_TR_NOOP("Create a regular polygon in the sketch");
    sWhatsThis = "Sketcher_CreateRegularPolygon";
    sStatusTip = sToolTipText;
    sPixmap = "Sketcher_CreateRegularPolygon";
    sAccel = "G, P, R";
    eType = ForEdit;
}

CONSTRUCTION_UPDATE_ACTION(CmdSketcherCreateRegularPolygon, "Sketcher_CreateRegularPolygon")

void CmdSketcherCreateRegularPolygon::activated(int iMsg)
{
    Q_UNUSED(iMsg);

    // Pop-up asking for values
    SketcherRegularPolygonDialog srpd;
    if (srpd.exec() == QDialog::Accepted) {
        ActivateHandler(getActiveGuiDocument(), new DrawSketchHandlerPolygon(srpd.sides));
    }
}

bool CmdSketcherCreateRegularPolygon::isActive()
{
    return isCommandActive(getActiveGuiDocument());
}

class CmdSketcherCompCreateRegularPolygon: public Gui::GroupCommand
{
public:
    CmdSketcherCompCreateRegularPolygon();
    virtual const char* className() const {return "CmdSketcherCompCreateRegularPolygon";}
};

CmdSketcherCompCreateRegularPolygon::CmdSketcherCompCreateRegularPolygon()
  : GroupCommand("Sketcher_CompCreateRegularPolygon")
{
    sAppModule      = "Sketcher";
    sGroup          = "Sketcher";
    sMenuText       = QT_TR_NOOP("Create regular polygon");
    sToolTipText    = QT_TR_NOOP("Create a regular polygon in the sketcher");
    sWhatsThis      = "Sketcher_CompCreateRegularPolygon";
    sStatusTip      = sToolTipText;
    sAccel          = "G, P, P";
    eType           = ForEdit;
    addCommand(new CmdSketcherCreateTriangle());
    addCommand(new CmdSketcherCreateSquare());
    addCommand(new CmdSketcherCreatePentagon());
    addCommand(new CmdSketcherCreateHexagon());
    addCommand(new CmdSketcherCreateHeptagon());
    addCommand(new CmdSketcherCreateOctagon());
    addCommand(new CmdSketcherCreateRegularPolygon());
}


void CreateSketcherCommandsCreateGeo()
{
    Gui::CommandManager& rcCmdMgr = Gui::Application::Instance->commandManager();

    rcCmdMgr.addCommand(new CmdSketcherCreatePoint());
    rcCmdMgr.addCommand(new CmdSketcherCompCreateArc());
    rcCmdMgr.addCommand(new CmdSketcherCompCreateCircle());
    rcCmdMgr.addCommand(new CmdSketcherCompCreateConic());
    rcCmdMgr.addCommand(new CmdSketcherCompCreateBSpline());
    rcCmdMgr.addCommand(new CmdSketcherCreateLine());
    rcCmdMgr.addCommand(new CmdSketcherCreatePolyline());
    rcCmdMgr.addCommand(new CmdSketcherCompCreateRegularPolygon());
    rcCmdMgr.addCommand(new CmdSketcherCompCreateRectangles());
    rcCmdMgr.addCommand(new CmdSketcherCreateSlot());
    rcCmdMgr.addCommand(new CmdSketcherCreateArcSlot());
    rcCmdMgr.addCommand(new CmdSketcherCompSlot());
    rcCmdMgr.addCommand(new CmdSketcherCreateFillet());
    rcCmdMgr.addCommand(new CmdSketcherCreateChamfer());
    rcCmdMgr.addCommand(new CmdSketcherCompCreateFillets());
    // rcCmdMgr.addCommand(new CmdSketcherCreateText());
    // rcCmdMgr.addCommand(new CmdSketcherCreateDraftLine());
    rcCmdMgr.addCommand(new CmdSketcherTrimming());
    rcCmdMgr.addCommand(new CmdSketcherExtend());
    rcCmdMgr.addCommand(new CmdSketcherExternalCmds());
    rcCmdMgr.addCommand(new CmdSketcherSplit());
    rcCmdMgr.addCommand(new CmdSketcherCompCurveEdition());
    rcCmdMgr.addCommand(new CmdSketcherCarbonCopy());
}
