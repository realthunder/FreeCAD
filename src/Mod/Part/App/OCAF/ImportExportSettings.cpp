/***************************************************************************
 *   Copyright (c) 2022 Werner Mayer <wmayer[at]users.sourceforge.net>     *
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
# include <algorithm>
# include <Interface_Static.hxx>
#endif

#include "ImportExportSettings.h"
#include <Mod/Part/App/PartParams.h>
#include <Mod/Part/App/IGES/ImportExportSettings.h>
#include <Mod/Part/App/STEP/ImportExportSettings.h>
#include <App/Application.h>


namespace Part {
namespace OCAF {

void ImportExportSettings::initialize()
{
    // set the user-defined settings
    Base::Reference<ParameterGrp> hGrp = App::GetApplication().GetUserParameter()
        .GetGroup("BaseApp")->GetGroup("Preferences")->GetGroup("Mod/Part");
    initGeneral(hGrp);
    initSTEP(hGrp);
    initIGES(hGrp);
}

void ImportExportSettings::initGeneral(Base::Reference<ParameterGrp> hGrp)
{
    // General
    Base::Reference<ParameterGrp> hGenGrp = hGrp->GetGroup("General");
    // http://www.opencascade.org/org/forum/thread_20801/
    // read.surfacecurve.mode:
    // A preference for the computation of curves in an entity which has both 2D and 3D representation.
    // Each TopoDS_Edge in TopoDS_Face must have a 3D and 2D curve that references the surface.
    // If both 2D and 3D representation of the entity are present, the computation of these curves depends on
    // the following values of parameter:
    // 0: "Default" - no preference, both curves are taken
    // 3: "3DUse_Preferred" - 3D curves are used to rebuild 2D ones
    // Additional modes for IGES
    //  2: "2DUse_Preferred" - the 2D is used to rebuild the 3D in case of their inconsistency
    // -2: "2DUse_Forced" - the 2D is always used to rebuild the 3D (even if 2D is present in the file)
    // -3: "3DUse_Forced" - the 3D is always used to rebuild the 2D (even if 2D is present in the file)
    int readsurfacecurve = hGenGrp->GetInt("ReadSurfaceCurveMode", PartParams::defaultReadSurfaceCurveMode());
    Interface_Static::SetIVal("read.surfacecurve.mode", readsurfacecurve);

    // write.surfacecurve.mode (STEP-only):
    // This parameter indicates whether parametric curves (curves in parametric space of surface) should be
    // written into the STEP file. This parameter can be set to Off in order to minimize the size of the resulting
    // STEP file.
    // Off (0) : writes STEP files without pcurves. This mode decreases the size of the resulting file.
    // On (1) : (default) writes pcurves to STEP file
    int writesurfacecurve = hGenGrp->GetInt("WriteSurfaceCurveMode", PartParams::defaultWriteSurfaceCurveMode());
    Interface_Static::SetIVal("write.surfacecurve.mode", writesurfacecurve);
}

void ImportExportSettings::initIGES(Base::Reference<ParameterGrp> hGrp)
{
    //IGES handling
    Base::Reference<ParameterGrp> hIgesGrp = hGrp->GetGroup("IGES");
    bool brep = hIgesGrp->GetBool("BrepMode", PartParams::defaultIgesBrepMode());
    Interface_Static::SetIVal("write.iges.brep.mode",brep ? 1 : 0);
    Interface_Static::SetCVal("write.iges.header.company",
        hIgesGrp->GetASCII("Company", PartParams::defaultIgesCompany().c_str()).c_str());
    Interface_Static::SetCVal("write.iges.header.author",
        hIgesGrp->GetASCII("Author", PartParams::defaultIgesAuthor().c_str()).c_str());
    // empty, which is the default, leaves the kernel's own
    std::string product = hIgesGrp->GetASCII("Product", PartParams::defaultIgesProduct().c_str());
    if (!product.empty()) {
        Interface_Static::SetCVal("write.iges.header.product", product.c_str());
    }

    int unitIges = hIgesGrp->GetInt("Unit", PartParams::defaultIgesUnit());
    switch (unitIges) {
        case 1:
            Interface_Static::SetCVal("write.iges.unit","M");
            break;
        case 2:
            Interface_Static::SetCVal("write.iges.unit","INCH");
            break;
        default:
            Interface_Static::SetCVal("write.iges.unit","MM");
            break;
    }
}

void ImportExportSettings::initSTEP(Base::Reference<ParameterGrp> hGrp)
{
    //STEP handling
    Base::Reference<ParameterGrp> hStepGrp = hGrp->GetGroup("STEP");
    int unitStep = hStepGrp->GetInt("Unit", PartParams::defaultStepUnit());
    switch (unitStep) {
        case 1:
            Interface_Static::SetCVal("write.step.unit","M");
            break;
        case 2:
            Interface_Static::SetCVal("write.step.unit","INCH");
            break;
        default:
            Interface_Static::SetCVal("write.step.unit","MM");
            break;
    }

    // empty, which is the default of both, leaves the kernel's own
    std::string ap = hStepGrp->GetASCII("Scheme", PartParams::defaultStepScheme().c_str());
    if (!ap.empty()) {
        Interface_Static::SetCVal("write.step.schema", ap.c_str());
    }
    std::string product = hStepGrp->GetASCII("Product", PartParams::defaultStepProduct().c_str());
    if (!product.empty()) {
        Interface_Static::SetCVal("write.step.product.name", product.c_str());
    }
}

ImportExportSettings::ImportExportSettings()
{
    pGroup = App::GetApplication().GetParameterGroupByPath("User parameter:BaseApp/Preferences/Mod/Import");
}

STEP::ImportExportSettingsPtr ImportExportSettings::getSTEPSettings() const
{
    if (!step) {
        step = std::make_shared<STEP::ImportExportSettings>();
    }

    return step;
}

IGES::ImportExportSettingsPtr ImportExportSettings::getIGESSettings() const
{
    if (!iges) {
        iges = std::make_shared<IGES::ImportExportSettings>();
    }

    return iges;
}

void ImportExportSettings::setReadShapeCompoundMode(bool on)
{
    auto grp = pGroup->GetGroup("hSTEP");
    grp->SetBool("ReadShapeCompoundMode", on);
}

bool ImportExportSettings::getReadShapeCompoundMode() const
{
    auto grp = pGroup->GetGroup("hSTEP");
    return grp->GetBool("ReadShapeCompoundMode", PartParams::defaultReadShapeCompoundMode());
}

void ImportExportSettings::setExportHiddenObject(bool on)
{
    pGroup->SetBool("ExportHiddenObject", on);
}

bool ImportExportSettings::getExportHiddenObject() const
{
    return pGroup->GetBool("ExportHiddenObject", PartParams::defaultExportHiddenObject());
}

void ImportExportSettings::setImportHiddenObject(bool on)
{
    pGroup->SetBool("ImportHiddenObject", on);
}

bool ImportExportSettings::getImportHiddenObject() const
{
    return pGroup->GetBool("ImportHiddenObject", PartParams::defaultImportHiddenObject());
}

void ImportExportSettings::setExportKeepPlacement(bool on)
{
    pGroup->SetBool("ExportKeepPlacement", on);
}

bool ImportExportSettings::getExportKeepPlacement() const
{
    return pGroup->GetBool("ExportKeepPlacement", PartParams::defaultExportKeepPlacement());
}

void ImportExportSettings::setUseLinkGroup(bool on)
{
    pGroup->SetBool("UseAppPart", !on);
}

bool ImportExportSettings::getUseLinkGroup() const
{
    return !pGroup->GetBool("UseAppPart", PartParams::defaultUseAppPart());
}

void ImportExportSettings::setUseBaseName(bool on)
{
    pGroup->SetBool("UseBaseName", on);
}

bool ImportExportSettings::getUseBaseName() const
{
    return pGroup->GetBool("UseBaseName", PartParams::defaultUseBaseName());
}

void ImportExportSettings::setReduceObjects(bool on)
{
    pGroup->SetBool("ReduceObjects", on);
}

bool ImportExportSettings::getReduceObjects() const
{
    return pGroup->GetBool("ReduceObjects", PartParams::defaultReduceObjects());
}

void ImportExportSettings::setShowProgress(bool on)
{
    pGroup->SetBool("ShowProgress", on);
}

bool ImportExportSettings::getShowProgress() const
{
    return pGroup->GetBool("ShowProgress", PartParams::defaultShowProgress());
}

void ImportExportSettings::setProgressiveImport(bool on)
{
    pGroup->SetBool("ProgressiveImport", on);
}

bool ImportExportSettings::getProgressiveImport() const
{
    return pGroup->GetBool("ProgressiveImport", PartParams::defaultProgressiveImport());
}

int ImportExportSettings::getStreamBatchStart() const
{
    return std::max(1, static_cast<int>(pGroup->GetInt("StreamBatchStart", PartParams::defaultStreamBatchStart())));
}

int ImportExportSettings::getStreamBatchFactor() const
{
    return std::max(1, static_cast<int>(pGroup->GetInt("StreamBatchFactor", PartParams::defaultStreamBatchFactor())));
}

void ImportExportSettings::setImportMode(ImportExportSettings::ImportMode mode)
{
    pGroup->SetInt("ImportMode", static_cast<long>(mode));
}

ImportExportSettings::ImportMode ImportExportSettings::getImportMode() const
{
    return static_cast<ImportExportSettings::ImportMode>(pGroup->GetInt("ImportMode", PartParams::defaultImportMode()));
}

} // namespace OCAF
} // namespace Part
