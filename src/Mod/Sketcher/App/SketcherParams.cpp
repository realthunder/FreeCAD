/****************************************************************************
 *   Copyright (c) 2026 Zheng Lei (realthunder) <realthunder.dev@gmail.com> *
 *                                                                          *
 *   This file is part of the FreeCAD CAx development system.               *
 *                                                                          *
 *   This library is free software; you can redistribute it and/or          *
 *   modify it under the terms of the GNU Library General Public            *
 *   License as published by the Free Software Foundation; either           *
 *   version 2 of the License, or (at your option) any later version.       *
 *                                                                          *
 *   This library  is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of         *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the          *
 *   GNU Library General Public License for more details.                   *
 *                                                                          *
 *   You should have received a copy of the GNU Library General Public      *
 *   License along with this library; see the file COPYING.LIB. If not,     *
 *   write to the Free Software Foundation, Inc., 59 Temple Place,          *
 *   Suite 330, Boston, MA  02111-1307, USA                                 *
 *                                                                          *
 ****************************************************************************/

#include "PreCompiled.h"

/*[[[cog
import SketcherParams
SketcherParams.define()
]]]*/

// Auto generated code (Tools/params_utils.py:198)
#include <unordered_map>
#include <App/Application.h>
#include <App/DynamicProperty.h>
#include <App/ParamRegistry.h>
#include "SketcherParams.h"
using namespace Sketcher;

// Auto generated code (Tools/params_utils.py:210)
namespace {
class SketcherParamsP: public ParameterGrp::ObserverType {
public:
    ParameterGrp::handle handle;
    std::unordered_map<const char *,void(*)(SketcherParamsP*),App::CStringHasher,App::CStringHasher> funcs;
    bool AutoRecompute;
    bool AutoRemoveRedundants;
    bool ContinuousCreationMode;
    bool ContinuousConstraintMode;
    bool ShowDialogOnDistanceConstraint;
    double RadiusDiameterConstraintDisplayBaseAngle;
    double RadiusDiameterConstraintDisplayAngleRandomness;
    long GeometryHistoryLevel;
    double ArcFitTolerance;
    long ExternalBSplineMaxDegree;
    double ExternalBSplineTolerance;
    bool MakeInternals;
    bool LeaveSketchWithEscape;
    bool ShowSolverAdvancedWidget;
    bool RecalculateInitialSolutionWhileDragging;
    bool ExtendedConstraintInformation;
    bool HideInternalAlignment;
    bool VisualisationTrackingFilter;
    bool HideUnits;
    bool ShowDimensionalName;
    std::string DimensionalStringFormat;
    bool ShowCursorCoords;
    bool UseSystemDecimals;

    // Auto generated code (Tools/params_utils.py:254)
    SketcherParamsP() {
        handle = App::GetApplication().GetParameterGroupByPath("User parameter:BaseApp/Preferences/Mod/Sketcher");
        handle->Attach(this);

        AutoRecompute = this->handle->GetBool("AutoRecompute", false);
        funcs["AutoRecompute"] = &SketcherParamsP::updateAutoRecompute;
        AutoRemoveRedundants = this->handle->GetBool("AutoRemoveRedundants", false);
        funcs["AutoRemoveRedundants"] = &SketcherParamsP::updateAutoRemoveRedundants;
        ContinuousCreationMode = this->handle->GetBool("ContinuousCreationMode", true);
        funcs["ContinuousCreationMode"] = &SketcherParamsP::updateContinuousCreationMode;
        ContinuousConstraintMode = this->handle->GetBool("ContinuousConstraintMode", true);
        funcs["ContinuousConstraintMode"] = &SketcherParamsP::updateContinuousConstraintMode;
        ShowDialogOnDistanceConstraint = this->handle->GetBool("ShowDialogOnDistanceConstraint", true);
        funcs["ShowDialogOnDistanceConstraint"] = &SketcherParamsP::updateShowDialogOnDistanceConstraint;
        RadiusDiameterConstraintDisplayBaseAngle = this->handle->GetFloat("RadiusDiameterConstraintDisplayBaseAngle", 15.0);
        funcs["RadiusDiameterConstraintDisplayBaseAngle"] = &SketcherParamsP::updateRadiusDiameterConstraintDisplayBaseAngle;
        RadiusDiameterConstraintDisplayAngleRandomness = this->handle->GetFloat("RadiusDiameterConstraintDisplayAngleRandomness", 0.0);
        funcs["RadiusDiameterConstraintDisplayAngleRandomness"] = &SketcherParamsP::updateRadiusDiameterConstraintDisplayAngleRandomness;
        GeometryHistoryLevel = this->handle->GetInt("GeometryHistoryLevel", 1);
        funcs["GeometryHistoryLevel"] = &SketcherParamsP::updateGeometryHistoryLevel;
        ArcFitTolerance = this->handle->GetFloat("ArcFitTolerance", 1e-06);
        funcs["ArcFitTolerance"] = &SketcherParamsP::updateArcFitTolerance;
        ExternalBSplineMaxDegree = this->handle->GetInt("ExternalBSplineMaxDegree", 5);
        funcs["ExternalBSplineMaxDegree"] = &SketcherParamsP::updateExternalBSplineMaxDegree;
        ExternalBSplineTolerance = this->handle->GetFloat("ExternalBSplineTolerance", 0.0001);
        funcs["ExternalBSplineTolerance"] = &SketcherParamsP::updateExternalBSplineTolerance;
        MakeInternals = this->handle->GetBool("MakeInternals", true);
        funcs["MakeInternals"] = &SketcherParamsP::updateMakeInternals;
        LeaveSketchWithEscape = this->handle->GetBool("LeaveSketchWithEscape", true);
        funcs["LeaveSketchWithEscape"] = &SketcherParamsP::updateLeaveSketchWithEscape;
        ShowSolverAdvancedWidget = this->handle->GetBool("ShowSolverAdvancedWidget", false);
        funcs["ShowSolverAdvancedWidget"] = &SketcherParamsP::updateShowSolverAdvancedWidget;
        RecalculateInitialSolutionWhileDragging = this->handle->GetBool("RecalculateInitialSolutionWhileDragging", true);
        funcs["RecalculateInitialSolutionWhileDragging"] = &SketcherParamsP::updateRecalculateInitialSolutionWhileDragging;
        ExtendedConstraintInformation = this->handle->GetBool("ExtendedConstraintInformation", false);
        funcs["ExtendedConstraintInformation"] = &SketcherParamsP::updateExtendedConstraintInformation;
        HideInternalAlignment = this->handle->GetBool("HideInternalAlignment", false);
        funcs["HideInternalAlignment"] = &SketcherParamsP::updateHideInternalAlignment;
        VisualisationTrackingFilter = this->handle->GetBool("VisualisationTrackingFilter", false);
        funcs["VisualisationTrackingFilter"] = &SketcherParamsP::updateVisualisationTrackingFilter;
        HideUnits = this->handle->GetBool("HideUnits", false);
        funcs["HideUnits"] = &SketcherParamsP::updateHideUnits;
        ShowDimensionalName = this->handle->GetBool("ShowDimensionalName", true);
        funcs["ShowDimensionalName"] = &SketcherParamsP::updateShowDimensionalName;
        DimensionalStringFormat = this->handle->GetASCII("DimensionalStringFormat", "%N = %V");
        funcs["DimensionalStringFormat"] = &SketcherParamsP::updateDimensionalStringFormat;
        ShowCursorCoords = this->handle->GetBool("ShowCursorCoords", true);
        funcs["ShowCursorCoords"] = &SketcherParamsP::updateShowCursorCoords;
        UseSystemDecimals = this->handle->GetBool("UseSystemDecimals", true);
        funcs["UseSystemDecimals"] = &SketcherParamsP::updateUseSystemDecimals;
    }

    // Auto generated code (Tools/params_utils.py:284)
    ~SketcherParamsP() override = default;

    // Auto generated code (Tools/params_utils.py:297)
    void OnChange(Base::Subject<const char*> &, const char* sReason) override {
        if(!sReason)
            return;
        auto it = funcs.find(sReason);
        if(it == funcs.end())
            return;
        it->second(this);
    }


    // Auto generated code (Tools/params_utils.py:314)
    static void updateAutoRecompute(SketcherParamsP *self) {
        self->AutoRecompute = self->handle->GetBool("AutoRecompute", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateAutoRemoveRedundants(SketcherParamsP *self) {
        self->AutoRemoveRedundants = self->handle->GetBool("AutoRemoveRedundants", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateContinuousCreationMode(SketcherParamsP *self) {
        self->ContinuousCreationMode = self->handle->GetBool("ContinuousCreationMode", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateContinuousConstraintMode(SketcherParamsP *self) {
        self->ContinuousConstraintMode = self->handle->GetBool("ContinuousConstraintMode", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateShowDialogOnDistanceConstraint(SketcherParamsP *self) {
        self->ShowDialogOnDistanceConstraint = self->handle->GetBool("ShowDialogOnDistanceConstraint", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateRadiusDiameterConstraintDisplayBaseAngle(SketcherParamsP *self) {
        self->RadiusDiameterConstraintDisplayBaseAngle = self->handle->GetFloat("RadiusDiameterConstraintDisplayBaseAngle", 15.0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateRadiusDiameterConstraintDisplayAngleRandomness(SketcherParamsP *self) {
        self->RadiusDiameterConstraintDisplayAngleRandomness = self->handle->GetFloat("RadiusDiameterConstraintDisplayAngleRandomness", 0.0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateGeometryHistoryLevel(SketcherParamsP *self) {
        self->GeometryHistoryLevel = self->handle->GetInt("GeometryHistoryLevel", 1);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateArcFitTolerance(SketcherParamsP *self) {
        self->ArcFitTolerance = self->handle->GetFloat("ArcFitTolerance", 1e-06);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateExternalBSplineMaxDegree(SketcherParamsP *self) {
        self->ExternalBSplineMaxDegree = self->handle->GetInt("ExternalBSplineMaxDegree", 5);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateExternalBSplineTolerance(SketcherParamsP *self) {
        self->ExternalBSplineTolerance = self->handle->GetFloat("ExternalBSplineTolerance", 0.0001);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateMakeInternals(SketcherParamsP *self) {
        self->MakeInternals = self->handle->GetBool("MakeInternals", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateLeaveSketchWithEscape(SketcherParamsP *self) {
        self->LeaveSketchWithEscape = self->handle->GetBool("LeaveSketchWithEscape", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateShowSolverAdvancedWidget(SketcherParamsP *self) {
        self->ShowSolverAdvancedWidget = self->handle->GetBool("ShowSolverAdvancedWidget", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateRecalculateInitialSolutionWhileDragging(SketcherParamsP *self) {
        self->RecalculateInitialSolutionWhileDragging = self->handle->GetBool("RecalculateInitialSolutionWhileDragging", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateExtendedConstraintInformation(SketcherParamsP *self) {
        self->ExtendedConstraintInformation = self->handle->GetBool("ExtendedConstraintInformation", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateHideInternalAlignment(SketcherParamsP *self) {
        self->HideInternalAlignment = self->handle->GetBool("HideInternalAlignment", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateVisualisationTrackingFilter(SketcherParamsP *self) {
        self->VisualisationTrackingFilter = self->handle->GetBool("VisualisationTrackingFilter", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateHideUnits(SketcherParamsP *self) {
        self->HideUnits = self->handle->GetBool("HideUnits", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateShowDimensionalName(SketcherParamsP *self) {
        self->ShowDimensionalName = self->handle->GetBool("ShowDimensionalName", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateDimensionalStringFormat(SketcherParamsP *self) {
        self->DimensionalStringFormat = self->handle->GetASCII("DimensionalStringFormat", "%N = %V");
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateShowCursorCoords(SketcherParamsP *self) {
        self->ShowCursorCoords = self->handle->GetBool("ShowCursorCoords", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateUseSystemDecimals(SketcherParamsP *self) {
        self->UseSystemDecimals = self->handle->GetBool("UseSystemDecimals", true);
    }
};

// Auto generated code (Tools/params_utils.py:336)
SketcherParamsP *instance() {
    static SketcherParamsP *inst = new SketcherParamsP;
    return inst;
}

} // Anonymous namespace

// Auto generated code (Tools/params_utils.py:352)
static const App::ParamRegistry::Registrar _SketcherParamsRegistrar({
    App::ParamInfo("Sketcher", "SketcherParams", "User parameter:BaseApp/Preferences/Mod/Sketcher", "AutoRecompute", "AutoRecompute", App::ParamInfo::Bool, false)
        .setTitle("Auto update")
        .setDoc("Recompute the active document after every sketch action. Set from\n"
"the Auto-update box of the solver messages panel; applies at once."),
    App::ParamInfo("Sketcher", "SketcherParams", "User parameter:BaseApp/Preferences/Mod/Sketcher", "AutoRemoveRedundants", "AutoRemoveRedundants", App::ParamInfo::Bool, false)
        .setTitle("Auto remove redundant constraints")
        .setDoc("Remove redundant constraints automatically when they are detected.\n"
"Works only while Auto-update is on. Applies at once."),
    App::ParamInfo("Sketcher", "SketcherParams", "User parameter:BaseApp/Preferences/Mod/Sketcher", "ContinuousCreationMode", "ContinuousCreationMode", App::ParamInfo::Bool, true)
        .setTitle("Geometry creation continue mode")
        .setDoc("Keep a geometry tool active after it creates an element, so the\n"
"next one can be drawn at once. Applies to the next element."),
    App::ParamInfo("Sketcher", "SketcherParams", "User parameter:BaseApp/Preferences/Mod/Sketcher", "ContinuousConstraintMode", "ContinuousConstraintMode", App::ParamInfo::Bool, true)
        .setTitle("Constraint creation continue mode")
        .setDoc("Keep a constraint tool active after it creates a constraint.\n"
"Applies the next time a constraint tool is started."),
    App::ParamInfo("Sketcher", "SketcherParams", "User parameter:BaseApp/Preferences/Mod/Sketcher", "ShowDialogOnDistanceConstraint", "ShowDialogOnDistanceConstraint", App::ParamInfo::Bool, true)
        .setTitle("Ask for value after creating a dimensional constraint")
        .setDoc("Ask for the value right after a dimensional constraint is created.\n"
"Applies to the next constraint."),
    App::ParamInfo("Sketcher", "SketcherParams", "User parameter:BaseApp/Preferences/Mod/Sketcher", "RadiusDiameterConstraintDisplayBaseAngle", "RadiusDiameterConstraintDisplayBaseAngle", App::ParamInfo::Float, 15.0)
        .setTitle("Radius and diameter label angle")
        .setDoc("Angle in degrees at which the label of a new radius or diameter\n"
"constraint is placed. Applies to new constraints."),
    App::ParamInfo("Sketcher", "SketcherParams", "User parameter:BaseApp/Preferences/Mod/Sketcher", "RadiusDiameterConstraintDisplayAngleRandomness", "RadiusDiameterConstraintDisplayAngleRandomness", App::ParamInfo::Float, 0.0)
        .setTitle("Radius and diameter label angle spread")
        .setDoc("Random spread in degrees added around the base angle of new radius\n"
"or diameter labels, so labels overlap less."),
    App::ParamInfo("Sketcher", "SketcherParams", "User parameter:BaseApp/Preferences/Mod/Sketcher", "GeometryHistoryLevel", "GeometryHistoryLevel", App::ParamInfo::Int, 1)
        .setTitle("Geometry history level")
        .setDoc("How much geometry history a sketch keeps for stable element names:\n"
"0 none, 1 default, above 1 keeps more. Read when a sketch object\n"
"is created or loaded."),
    App::ParamInfo("Sketcher", "SketcherParams", "User parameter:BaseApp/Preferences/Mod/Sketcher", "ArcFitTolerance", "ArcFitTolerance", App::ParamInfo::Float, 1e-06)
        .setTitle("Arc fit tolerance")
        .setDoc("Tolerance used to fit arcs for a new sketch; copied into the\n"
"sketch's ArcFitTolerance property when it is created."),
    App::ParamInfo("Sketcher", "SketcherParams", "User parameter:BaseApp/Preferences/Mod/Sketcher", "ExternalBSplineMaxDegree", "ExternalBSplineMaxDegree", App::ParamInfo::Int, 5)
        .setTitle("External B-spline maximum degree")
        .setDoc("Maximum degree of B-splines made from external geometry in a new\n"
"sketch. Copied into the sketch when it is created."),
    App::ParamInfo("Sketcher", "SketcherParams", "User parameter:BaseApp/Preferences/Mod/Sketcher", "ExternalBSplineTolerance", "ExternalBSplineTolerance", App::ParamInfo::Float, 0.0001)
        .setTitle("External B-spline tolerance")
        .setDoc("Tolerance of B-splines made from external geometry in a new\n"
"sketch.0001. Copied into the sketch when it is created."),
    App::ParamInfo("Sketcher", "SketcherParams", "User parameter:BaseApp/Preferences/Mod/Sketcher", "MakeInternals", "MakeInternals", App::ParamInfo::Bool, true)
        .setTitle("Generate internal faces")
        .setDoc("Generate internal faces from the closed regions of a new sketch.\n"
"Copied into the sketch's MakeInternals property when it is\n"
"created."),
    App::ParamInfo("Sketcher", "SketcherParams", "User parameter:BaseApp/Preferences/Mod/Sketcher", "LeaveSketchWithEscape", "LeaveSketchWithEscape", App::ParamInfo::Bool, true)
        .setTitle("Esc can leave sketch edit mode")
        .setDoc("Let the Esc key leave sketch edit mode. Takes effect the next time\n"
"a sketch is edited."),
    App::ParamInfo("Sketcher", "SketcherParams", "User parameter:BaseApp/Preferences/Mod/Sketcher", "ShowSolverAdvancedWidget", "ShowSolverAdvancedWidget", App::ParamInfo::Bool, false)
        .setTitle("Show section 'Advanced solver control'")
        .setDoc("Show the Advanced solver control section in the sketch task panel.\n"
"Takes effect the next time a sketch is edited."),
    App::ParamInfo("Sketcher", "SketcherParams", "User parameter:BaseApp/Preferences/Mod/Sketcher", "RecalculateInitialSolutionWhileDragging", "RecalculateInitialSolutionWhileDragging", App::ParamInfo::Bool, true)
        .setTitle("Improve solving while dragging")
        .setDoc("Recalculate the solver's starting point while a point is dragged,\n"
"which improves solving. Takes effect the next time a sketch is\n"
"edited."),
    App::ParamInfo("Sketcher", "SketcherParams", "User parameter:BaseApp/Preferences/Mod/Sketcher", "ExtendedConstraintInformation", "ExtendedConstraintInformation", App::ParamInfo::Bool, false)
        .setTitle("Extended constraint information")
        .setDoc("Show extended information for each entry of the constraint list.\n"
"Applies at once."),
    App::ParamInfo("Sketcher", "SketcherParams", "User parameter:BaseApp/Preferences/Mod/Sketcher", "HideInternalAlignment", "HideInternalAlignment", App::ParamInfo::Bool, false)
        .setTitle("Hide internal alignment constraints")
        .setDoc("Hide internal alignment constraints in the constraint list.\n"
"Applies at once."),
    App::ParamInfo("Sketcher", "SketcherParams", "User parameter:BaseApp/Preferences/Mod/Sketcher", "VisualisationTrackingFilter", "VisualisationTrackingFilter", App::ParamInfo::Bool, false)
        .setTitle("Show only filtered constraints in the 3D view")
        .setDoc("Show in the 3D view only the constraints that pass the constraint\n"
"list filter. Applies at once."),
    App::ParamInfo("Sketcher", "SketcherParams", "User parameter:BaseApp/Preferences/Mod/Sketcher", "HideUnits", "HideUnits", App::ParamInfo::Bool, false)
        .setTitle("Hide base length units")
        .setDoc("Hide the base length unit in dimension labels and cursor\n"
"coordinates for unit systems that support it. Shown at the next\n"
"redraw."),
    App::ParamInfo("Sketcher", "SketcherParams", "User parameter:BaseApp/Preferences/Mod/Sketcher", "ShowDimensionalName", "ShowDimensionalName", App::ParamInfo::Bool, true)
        .setTitle("Show dimensional constraint name")
        .setDoc("Show the name of a named dimensional constraint in its label,\n"
"using the format string. Shown at the next redraw."),
    App::ParamInfo("Sketcher", "SketcherParams", "User parameter:BaseApp/Preferences/Mod/Sketcher", "DimensionalStringFormat", "DimensionalStringFormat", App::ParamInfo::String, "%N = %V")
        .setTitle("Dimensional constraint label format")
        .setDoc("Format of a named dimension's label: %N is the constraint name, %V\n"
"its value. Shown at the next redraw."),
    App::ParamInfo("Sketcher", "SketcherParams", "User parameter:BaseApp/Preferences/Mod/Sketcher", "ShowCursorCoords", "ShowCursorCoords", App::ParamInfo::Bool, true)
        .setTitle("Show coordinates beside cursor")
        .setDoc("Show the coordinates next to the cursor while drawing in a sketch.\n"
"Applies at once."),
    App::ParamInfo("Sketcher", "SketcherParams", "User parameter:BaseApp/Preferences/Mod/Sketcher", "UseSystemDecimals", "UseSystemDecimals", App::ParamInfo::Bool, true)
        .setTitle("Use system decimals for cursor coordinates")
        .setDoc("Show cursor coordinates with the number of decimals of the unit\n"
"settings instead of a short form. Applies at once."),
});

// Auto generated code (Tools/params_utils.py:368)
ParameterGrp::handle SketcherParams::getHandle() {
    return instance()->handle;
}

// Auto generated code (Tools/params_utils.py:397)
const char *SketcherParams::docAutoRecompute() {
    return QT_TRANSLATE_NOOP("SketcherParams",
"Recompute the active document after every sketch action. Set from\n"
"the Auto-update box of the solver messages panel; applies at once.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & SketcherParams::getAutoRecompute() {
    return instance()->AutoRecompute;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & SketcherParams::defaultAutoRecompute() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SketcherParams::setAutoRecompute(const bool &v) {
    instance()->handle->SetBool("AutoRecompute",v);
    instance()->AutoRecompute = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SketcherParams::removeAutoRecompute() {
    instance()->handle->RemoveBool("AutoRecompute");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SketcherParams::docAutoRemoveRedundants() {
    return QT_TRANSLATE_NOOP("SketcherParams",
"Remove redundant constraints automatically when they are detected.\n"
"Works only while Auto-update is on. Applies at once.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & SketcherParams::getAutoRemoveRedundants() {
    return instance()->AutoRemoveRedundants;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & SketcherParams::defaultAutoRemoveRedundants() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SketcherParams::setAutoRemoveRedundants(const bool &v) {
    instance()->handle->SetBool("AutoRemoveRedundants",v);
    instance()->AutoRemoveRedundants = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SketcherParams::removeAutoRemoveRedundants() {
    instance()->handle->RemoveBool("AutoRemoveRedundants");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SketcherParams::docContinuousCreationMode() {
    return QT_TRANSLATE_NOOP("SketcherParams",
"Keep a geometry tool active after it creates an element, so the\n"
"next one can be drawn at once. Applies to the next element.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & SketcherParams::getContinuousCreationMode() {
    return instance()->ContinuousCreationMode;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & SketcherParams::defaultContinuousCreationMode() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SketcherParams::setContinuousCreationMode(const bool &v) {
    instance()->handle->SetBool("ContinuousCreationMode",v);
    instance()->ContinuousCreationMode = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SketcherParams::removeContinuousCreationMode() {
    instance()->handle->RemoveBool("ContinuousCreationMode");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SketcherParams::docContinuousConstraintMode() {
    return QT_TRANSLATE_NOOP("SketcherParams",
"Keep a constraint tool active after it creates a constraint.\n"
"Applies the next time a constraint tool is started.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & SketcherParams::getContinuousConstraintMode() {
    return instance()->ContinuousConstraintMode;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & SketcherParams::defaultContinuousConstraintMode() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SketcherParams::setContinuousConstraintMode(const bool &v) {
    instance()->handle->SetBool("ContinuousConstraintMode",v);
    instance()->ContinuousConstraintMode = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SketcherParams::removeContinuousConstraintMode() {
    instance()->handle->RemoveBool("ContinuousConstraintMode");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SketcherParams::docShowDialogOnDistanceConstraint() {
    return QT_TRANSLATE_NOOP("SketcherParams",
"Ask for the value right after a dimensional constraint is created.\n"
"Applies to the next constraint.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & SketcherParams::getShowDialogOnDistanceConstraint() {
    return instance()->ShowDialogOnDistanceConstraint;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & SketcherParams::defaultShowDialogOnDistanceConstraint() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SketcherParams::setShowDialogOnDistanceConstraint(const bool &v) {
    instance()->handle->SetBool("ShowDialogOnDistanceConstraint",v);
    instance()->ShowDialogOnDistanceConstraint = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SketcherParams::removeShowDialogOnDistanceConstraint() {
    instance()->handle->RemoveBool("ShowDialogOnDistanceConstraint");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SketcherParams::docRadiusDiameterConstraintDisplayBaseAngle() {
    return QT_TRANSLATE_NOOP("SketcherParams",
"Angle in degrees at which the label of a new radius or diameter\n"
"constraint is placed. Applies to new constraints.");
}

// Auto generated code (Tools/params_utils.py:405)
const double & SketcherParams::getRadiusDiameterConstraintDisplayBaseAngle() {
    return instance()->RadiusDiameterConstraintDisplayBaseAngle;
}

// Auto generated code (Tools/params_utils.py:413)
const double & SketcherParams::defaultRadiusDiameterConstraintDisplayBaseAngle() {
    const static double def = 15.0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SketcherParams::setRadiusDiameterConstraintDisplayBaseAngle(const double &v) {
    instance()->handle->SetFloat("RadiusDiameterConstraintDisplayBaseAngle",v);
    instance()->RadiusDiameterConstraintDisplayBaseAngle = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SketcherParams::removeRadiusDiameterConstraintDisplayBaseAngle() {
    instance()->handle->RemoveFloat("RadiusDiameterConstraintDisplayBaseAngle");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SketcherParams::docRadiusDiameterConstraintDisplayAngleRandomness() {
    return QT_TRANSLATE_NOOP("SketcherParams",
"Random spread in degrees added around the base angle of new radius\n"
"or diameter labels, so labels overlap less.");
}

// Auto generated code (Tools/params_utils.py:405)
const double & SketcherParams::getRadiusDiameterConstraintDisplayAngleRandomness() {
    return instance()->RadiusDiameterConstraintDisplayAngleRandomness;
}

// Auto generated code (Tools/params_utils.py:413)
const double & SketcherParams::defaultRadiusDiameterConstraintDisplayAngleRandomness() {
    const static double def = 0.0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SketcherParams::setRadiusDiameterConstraintDisplayAngleRandomness(const double &v) {
    instance()->handle->SetFloat("RadiusDiameterConstraintDisplayAngleRandomness",v);
    instance()->RadiusDiameterConstraintDisplayAngleRandomness = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SketcherParams::removeRadiusDiameterConstraintDisplayAngleRandomness() {
    instance()->handle->RemoveFloat("RadiusDiameterConstraintDisplayAngleRandomness");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SketcherParams::docGeometryHistoryLevel() {
    return QT_TRANSLATE_NOOP("SketcherParams",
"How much geometry history a sketch keeps for stable element names:\n"
"0 none, 1 default, above 1 keeps more. Read when a sketch object\n"
"is created or loaded.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & SketcherParams::getGeometryHistoryLevel() {
    return instance()->GeometryHistoryLevel;
}

// Auto generated code (Tools/params_utils.py:413)
const long & SketcherParams::defaultGeometryHistoryLevel() {
    const static long def = 1;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SketcherParams::setGeometryHistoryLevel(const long &v) {
    instance()->handle->SetInt("GeometryHistoryLevel",v);
    instance()->GeometryHistoryLevel = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SketcherParams::removeGeometryHistoryLevel() {
    instance()->handle->RemoveInt("GeometryHistoryLevel");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SketcherParams::docArcFitTolerance() {
    return QT_TRANSLATE_NOOP("SketcherParams",
"Tolerance used to fit arcs for a new sketch; copied into the\n"
"sketch's ArcFitTolerance property when it is created.");
}

// Auto generated code (Tools/params_utils.py:405)
const double & SketcherParams::getArcFitTolerance() {
    return instance()->ArcFitTolerance;
}

// Auto generated code (Tools/params_utils.py:413)
const double & SketcherParams::defaultArcFitTolerance() {
    const static double def = 1e-06;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SketcherParams::setArcFitTolerance(const double &v) {
    instance()->handle->SetFloat("ArcFitTolerance",v);
    instance()->ArcFitTolerance = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SketcherParams::removeArcFitTolerance() {
    instance()->handle->RemoveFloat("ArcFitTolerance");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SketcherParams::docExternalBSplineMaxDegree() {
    return QT_TRANSLATE_NOOP("SketcherParams",
"Maximum degree of B-splines made from external geometry in a new\n"
"sketch. Copied into the sketch when it is created.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & SketcherParams::getExternalBSplineMaxDegree() {
    return instance()->ExternalBSplineMaxDegree;
}

// Auto generated code (Tools/params_utils.py:413)
const long & SketcherParams::defaultExternalBSplineMaxDegree() {
    const static long def = 5;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SketcherParams::setExternalBSplineMaxDegree(const long &v) {
    instance()->handle->SetInt("ExternalBSplineMaxDegree",v);
    instance()->ExternalBSplineMaxDegree = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SketcherParams::removeExternalBSplineMaxDegree() {
    instance()->handle->RemoveInt("ExternalBSplineMaxDegree");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SketcherParams::docExternalBSplineTolerance() {
    return QT_TRANSLATE_NOOP("SketcherParams",
"Tolerance of B-splines made from external geometry in a new\n"
"sketch.0001. Copied into the sketch when it is created.");
}

// Auto generated code (Tools/params_utils.py:405)
const double & SketcherParams::getExternalBSplineTolerance() {
    return instance()->ExternalBSplineTolerance;
}

// Auto generated code (Tools/params_utils.py:413)
const double & SketcherParams::defaultExternalBSplineTolerance() {
    const static double def = 0.0001;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SketcherParams::setExternalBSplineTolerance(const double &v) {
    instance()->handle->SetFloat("ExternalBSplineTolerance",v);
    instance()->ExternalBSplineTolerance = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SketcherParams::removeExternalBSplineTolerance() {
    instance()->handle->RemoveFloat("ExternalBSplineTolerance");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SketcherParams::docMakeInternals() {
    return QT_TRANSLATE_NOOP("SketcherParams",
"Generate internal faces from the closed regions of a new sketch.\n"
"Copied into the sketch's MakeInternals property when it is\n"
"created.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & SketcherParams::getMakeInternals() {
    return instance()->MakeInternals;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & SketcherParams::defaultMakeInternals() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SketcherParams::setMakeInternals(const bool &v) {
    instance()->handle->SetBool("MakeInternals",v);
    instance()->MakeInternals = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SketcherParams::removeMakeInternals() {
    instance()->handle->RemoveBool("MakeInternals");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SketcherParams::docLeaveSketchWithEscape() {
    return QT_TRANSLATE_NOOP("SketcherParams",
"Let the Esc key leave sketch edit mode. Takes effect the next time\n"
"a sketch is edited.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & SketcherParams::getLeaveSketchWithEscape() {
    return instance()->LeaveSketchWithEscape;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & SketcherParams::defaultLeaveSketchWithEscape() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SketcherParams::setLeaveSketchWithEscape(const bool &v) {
    instance()->handle->SetBool("LeaveSketchWithEscape",v);
    instance()->LeaveSketchWithEscape = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SketcherParams::removeLeaveSketchWithEscape() {
    instance()->handle->RemoveBool("LeaveSketchWithEscape");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SketcherParams::docShowSolverAdvancedWidget() {
    return QT_TRANSLATE_NOOP("SketcherParams",
"Show the Advanced solver control section in the sketch task panel.\n"
"Takes effect the next time a sketch is edited.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & SketcherParams::getShowSolverAdvancedWidget() {
    return instance()->ShowSolverAdvancedWidget;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & SketcherParams::defaultShowSolverAdvancedWidget() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SketcherParams::setShowSolverAdvancedWidget(const bool &v) {
    instance()->handle->SetBool("ShowSolverAdvancedWidget",v);
    instance()->ShowSolverAdvancedWidget = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SketcherParams::removeShowSolverAdvancedWidget() {
    instance()->handle->RemoveBool("ShowSolverAdvancedWidget");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SketcherParams::docRecalculateInitialSolutionWhileDragging() {
    return QT_TRANSLATE_NOOP("SketcherParams",
"Recalculate the solver's starting point while a point is dragged,\n"
"which improves solving. Takes effect the next time a sketch is\n"
"edited.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & SketcherParams::getRecalculateInitialSolutionWhileDragging() {
    return instance()->RecalculateInitialSolutionWhileDragging;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & SketcherParams::defaultRecalculateInitialSolutionWhileDragging() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SketcherParams::setRecalculateInitialSolutionWhileDragging(const bool &v) {
    instance()->handle->SetBool("RecalculateInitialSolutionWhileDragging",v);
    instance()->RecalculateInitialSolutionWhileDragging = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SketcherParams::removeRecalculateInitialSolutionWhileDragging() {
    instance()->handle->RemoveBool("RecalculateInitialSolutionWhileDragging");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SketcherParams::docExtendedConstraintInformation() {
    return QT_TRANSLATE_NOOP("SketcherParams",
"Show extended information for each entry of the constraint list.\n"
"Applies at once.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & SketcherParams::getExtendedConstraintInformation() {
    return instance()->ExtendedConstraintInformation;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & SketcherParams::defaultExtendedConstraintInformation() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SketcherParams::setExtendedConstraintInformation(const bool &v) {
    instance()->handle->SetBool("ExtendedConstraintInformation",v);
    instance()->ExtendedConstraintInformation = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SketcherParams::removeExtendedConstraintInformation() {
    instance()->handle->RemoveBool("ExtendedConstraintInformation");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SketcherParams::docHideInternalAlignment() {
    return QT_TRANSLATE_NOOP("SketcherParams",
"Hide internal alignment constraints in the constraint list.\n"
"Applies at once.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & SketcherParams::getHideInternalAlignment() {
    return instance()->HideInternalAlignment;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & SketcherParams::defaultHideInternalAlignment() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SketcherParams::setHideInternalAlignment(const bool &v) {
    instance()->handle->SetBool("HideInternalAlignment",v);
    instance()->HideInternalAlignment = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SketcherParams::removeHideInternalAlignment() {
    instance()->handle->RemoveBool("HideInternalAlignment");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SketcherParams::docVisualisationTrackingFilter() {
    return QT_TRANSLATE_NOOP("SketcherParams",
"Show in the 3D view only the constraints that pass the constraint\n"
"list filter. Applies at once.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & SketcherParams::getVisualisationTrackingFilter() {
    return instance()->VisualisationTrackingFilter;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & SketcherParams::defaultVisualisationTrackingFilter() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SketcherParams::setVisualisationTrackingFilter(const bool &v) {
    instance()->handle->SetBool("VisualisationTrackingFilter",v);
    instance()->VisualisationTrackingFilter = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SketcherParams::removeVisualisationTrackingFilter() {
    instance()->handle->RemoveBool("VisualisationTrackingFilter");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SketcherParams::docHideUnits() {
    return QT_TRANSLATE_NOOP("SketcherParams",
"Hide the base length unit in dimension labels and cursor\n"
"coordinates for unit systems that support it. Shown at the next\n"
"redraw.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & SketcherParams::getHideUnits() {
    return instance()->HideUnits;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & SketcherParams::defaultHideUnits() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SketcherParams::setHideUnits(const bool &v) {
    instance()->handle->SetBool("HideUnits",v);
    instance()->HideUnits = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SketcherParams::removeHideUnits() {
    instance()->handle->RemoveBool("HideUnits");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SketcherParams::docShowDimensionalName() {
    return QT_TRANSLATE_NOOP("SketcherParams",
"Show the name of a named dimensional constraint in its label,\n"
"using the format string. Shown at the next redraw.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & SketcherParams::getShowDimensionalName() {
    return instance()->ShowDimensionalName;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & SketcherParams::defaultShowDimensionalName() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SketcherParams::setShowDimensionalName(const bool &v) {
    instance()->handle->SetBool("ShowDimensionalName",v);
    instance()->ShowDimensionalName = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SketcherParams::removeShowDimensionalName() {
    instance()->handle->RemoveBool("ShowDimensionalName");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SketcherParams::docDimensionalStringFormat() {
    return QT_TRANSLATE_NOOP("SketcherParams",
"Format of a named dimension's label: %N is the constraint name, %V\n"
"its value. Shown at the next redraw.");
}

// Auto generated code (Tools/params_utils.py:405)
const std::string & SketcherParams::getDimensionalStringFormat() {
    return instance()->DimensionalStringFormat;
}

// Auto generated code (Tools/params_utils.py:413)
const std::string & SketcherParams::defaultDimensionalStringFormat() {
    const static std::string def = "%N = %V";
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SketcherParams::setDimensionalStringFormat(const std::string &v) {
    instance()->handle->SetASCII("DimensionalStringFormat",v);
    instance()->DimensionalStringFormat = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SketcherParams::removeDimensionalStringFormat() {
    instance()->handle->RemoveASCII("DimensionalStringFormat");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SketcherParams::docShowCursorCoords() {
    return QT_TRANSLATE_NOOP("SketcherParams",
"Show the coordinates next to the cursor while drawing in a sketch.\n"
"Applies at once.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & SketcherParams::getShowCursorCoords() {
    return instance()->ShowCursorCoords;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & SketcherParams::defaultShowCursorCoords() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SketcherParams::setShowCursorCoords(const bool &v) {
    instance()->handle->SetBool("ShowCursorCoords",v);
    instance()->ShowCursorCoords = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SketcherParams::removeShowCursorCoords() {
    instance()->handle->RemoveBool("ShowCursorCoords");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SketcherParams::docUseSystemDecimals() {
    return QT_TRANSLATE_NOOP("SketcherParams",
"Show cursor coordinates with the number of decimals of the unit\n"
"settings instead of a short form. Applies at once.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & SketcherParams::getUseSystemDecimals() {
    return instance()->UseSystemDecimals;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & SketcherParams::defaultUseSystemDecimals() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SketcherParams::setUseSystemDecimals(const bool &v) {
    instance()->handle->SetBool("UseSystemDecimals",v);
    instance()->UseSystemDecimals = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SketcherParams::removeUseSystemDecimals() {
    instance()->handle->RemoveBool("UseSystemDecimals");
}
//[[[end]]]
