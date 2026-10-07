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
# include <Interface_Static.hxx>
#endif

#include "ImportExportSettings.h"
#include <Mod/Part/App/PartParams.h>
#include <App/Application.h>


namespace Part {
namespace STEP {

ImportExportSettings::ImportExportSettings()
{
    pGroup = App::GetApplication().GetParameterGroupByPath("User parameter:BaseApp/Preferences/Mod/Part/STEP");
}

void ImportExportSettings::setVisibleExportDialog(bool on)
{
    pGroup->SetBool("VisibleExportDialog", on);
}

bool ImportExportSettings::isVisibleExportDialog() const
{
    return pGroup->GetBool("VisibleExportDialog", PartParams::defaultVisibleExportDialog());
}


void ImportExportSettings::setWriteSurfaceCurveMode(bool on)
{
    ParameterGrp::handle grp = App::GetApplication().GetParameterGroupByPath("User parameter:BaseApp/Preferences/Mod/Part/General");
    grp->SetInt("WriteSurfaceCurveMode", on ? 1 : 0);
    Interface_Static::SetIVal("write.surfacecurve.mode", on ? 1 : 0);
}

bool ImportExportSettings::getWriteSurfaceCurveMode() const
{
    ParameterGrp::handle grp = App::GetApplication().GetParameterGroupByPath("User parameter:BaseApp/Preferences/Mod/Part/General");
    int writesurfacecurve = grp->GetInt("WriteSurfaceCurveMode", PartParams::defaultWriteSurfaceCurveMode());
    return (writesurfacecurve == 0 ? false : true);
}

std::string ImportExportSettings::getScheme() const
{
    // empty, which is the default, means the kernel's own
    std::string scheme = pGroup->GetASCII("Scheme", PartParams::defaultStepScheme().c_str());
    return scheme.empty() ? std::string(Interface_Static::CVal("write.step.schema")) : scheme;
}

void ImportExportSettings::setScheme(const char* scheme)
{
    pGroup->SetASCII("Scheme", scheme);
    Interface_Static::SetCVal("write.step.schema", scheme);
}

Interface::Unit ImportExportSettings::getUnit() const
{
    return static_cast<Interface::Unit>(pGroup->GetInt("Unit", PartParams::defaultStepUnit()));
}

void ImportExportSettings::setUnit(Interface::Unit unit)
{
    pGroup->SetInt("Unit", static_cast<long>(unit));
    Part::Interface::writeStepUnit(unit);
}

std::string ImportExportSettings::getCompany() const
{
    return pGroup->GetASCII("Company", PartParams::defaultStepCompany().c_str());
}

void ImportExportSettings::setCompany(const char* name)
{
    pGroup->SetASCII("Company", name);
}

std::string ImportExportSettings::getAuthor() const
{
    return pGroup->GetASCII("Author", PartParams::defaultStepAuthor().c_str());
}

void ImportExportSettings::setAuthor(const char* name)
{
    pGroup->SetASCII("Author", name);
}

std::string ImportExportSettings::getProductName() const
{
    return Part::Interface::writeStepHeaderProduct();
}

void ImportExportSettings::setProductName(const char* name)
{
    Part::Interface::writeStepHeaderProduct(name);
}

} // namespace STEP
} // namespace Part
