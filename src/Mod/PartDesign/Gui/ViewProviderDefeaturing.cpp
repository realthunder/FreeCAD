// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 Max Wilfinger
// SPDX-FileNotice: Part of the FreeCAD project.

/******************************************************************************
 *                                                                            *
 *   FreeCAD is free software: you can redistribute it and/or modify          *
 *   it under the terms of the GNU Lesser General Public License as           *
 *   published by the Free Software Foundation, either version 2.1            *
 *   of the License, or (at your option) any later version.                   *
 *                                                                            *
 *   FreeCAD is distributed in the hope that it will be useful,               *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty              *
 *   of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.                  *
 *   See the GNU Lesser General Public License for more details.              *
 *                                                                            *
 *   You should have received a copy of the GNU Lesser General Public         *
 *   License along with FreeCAD. If not, see https://www.gnu.org/licenses     *
 *                                                                            *
 ******************************************************************************/

#include "PreCompiled.h"

#ifndef _PreComp_
# include <Standard_Failure.hxx>
#endif

#include <Base/Console.h>
#include <Base/Exception.h>
#include <Mod/PartDesign/App/FeatureDefeaturing.h>

#include "TaskDefeaturingParameters.h"
#include "ViewProviderDefeaturing.h"

using namespace PartDesignGui;

PROPERTY_SOURCE(PartDesignGui::ViewProviderDefeaturing,PartDesignGui::ViewProviderDressUp)


const std::string & ViewProviderDefeaturing::featureName() const {
    static const std::string name = "Defeaturing";
    return name;
}

void ViewProviderDefeaturing::setupContextMenu(QMenu* menu, QObject* receiver, const char* member)
{
    addDefaultAction(menu, QObject::tr("Edit defeaturing"));
    PartDesignGui::ViewProvider::setupContextMenu(menu, receiver, member);
}

TaskDlgFeatureParameters *ViewProviderDefeaturing::getEditDialog() {
    return new TaskDlgDefeaturingParameters(this);
}

bool ViewProviderDefeaturing::setEdit(int ModNum)
{
    bool res = ViewProviderDressUp::setEdit(ModNum);
    // isEditing() says so only once setEdit() returns
    previewing = res && ModNum == ViewProvider::Default;
    updateAddSubShapeIndicator();
    return res;
}

void ViewProviderDefeaturing::unsetEdit(int ModNum)
{
    previewing = false;
    ViewProviderDressUp::unsetEdit(ModNum);
    updateAddSubShapeIndicator();
}

void ViewProviderDefeaturing::updateAddSubShapeIndicator()
{
    auto feat = Base::freecad_dynamic_cast<PartDesign::Defeaturing>(getObject());
    if (!feat)
        return;
    Part::TopoShape preview;
    // Two booleans: only while the panel is open, as upstream's preview
    if (previewing && !feat->isError()) {
        try {
            Part::TopoShape shape = feat->Shape.getShape();
            shape.setPlacement(Base::Placement());
            Part::TopoShape base = feat->getBaseShape(true);
            base.move(feat->getLocation().Inverted());
            if (!shape.isNull() && !base.isNull()) {
                std::vector<Part::TopoShape> parts;
                for (auto part : {shape.makECut(base), base.makECut(shape)}) {
                    if (part.hasSubShape(TopAbs_SOLID))
                        parts.push_back(part);
                }
                if (!parts.empty())
                    preview.makECompound(parts);
            }
        }
        catch (Base::Exception &e) {
            e.reportException();
        }
        catch (Standard_Failure &e) {
            Base::Console().Error("Defeaturing preview: %s\n", e.GetMessageString());
        }
    }
    feat->DressUpShape.setValue(preview);
    getAddSubView()->updateVisual();
}
