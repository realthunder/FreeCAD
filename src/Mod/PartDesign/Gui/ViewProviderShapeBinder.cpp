/***************************************************************************
 *   Copyright (c) 2015 Stefan Tröger <stefantroeger@gmx.net>              *
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
# include <QAction>
# include <QApplication>
# include <QMessageBox>
# include <QMenu>
# include <TopExp.hxx>
# include <TopTools_IndexedMapOfShape.hxx>
#endif

#include <boost/algorithm/string/predicate.hpp>
#include <App/Document.h>
#include <Gui/ActionFunction.h>
#include <Gui/Application.h>
#include <Gui/Command.h>
#include <Gui/Control.h>
#include <Gui/Document.h>

#include <Mod/Part/Gui/PartParams.h>
#include <Mod/PartDesign/App/ShapeBinder.h>

#include "ViewProvider.h"
#include "ViewProviderShapeBinder.h"
#include "TaskShapeBinder.h"

FC_LOG_LEVEL_INIT("PartDesign",true,true)

using namespace PartDesignGui;

PROPERTY_SOURCE(PartDesignGui::ViewProviderShapeBinder,PartGui::ViewProviderPart)

ViewProviderShapeBinder::ViewProviderShapeBinder()
{
    sPixmap = "PartDesign_ShapeBinder.svg";

    //make the viewprovider more datum like
    AngularDeflection.setStatus(App::Property::Hidden, true);
    Deviation.setStatus(App::Property::Hidden, true);
    DrawStyle.setStatus(App::Property::Hidden, true);
    Lighting.setStatus(App::Property::Hidden, true);
    LineColor.setStatus(App::Property::Hidden, true);
    LineWidth.setStatus(App::Property::Hidden, true);
    PointColor.setStatus(App::Property::Hidden, true);
    PointSize.setStatus(App::Property::Hidden, true);
    DisplayMode.setStatus(App::Property::Hidden, true);

    //get the datum coloring scheme
    // set default color for datums (golden yellow with 60% transparency)
    unsigned long shcol = PartGui::PartParams::getDefaultDatumColor();
    App::Color col ( (uint32_t) shcol );
    
    MapFaceColor.setValue(false);
    MapLineColor.setValue(false);
    MapPointColor.setValue(false);
    MapTransparency.setValue(false);

    ShapeColor.setValue(col);
    LineColor.setValue(col);
    PointColor.setValue(col);
    Transparency.setValue(60);
    LineWidth.setValue(1);
}

ViewProviderShapeBinder::~ViewProviderShapeBinder() = default;

bool ViewProviderShapeBinder::setEdit(int ModNum) {
    // TODO Share code with other view providers (2015-09-11, Fat-Zer)

    if (ModNum == ViewProvider::Default || ModNum == 1) {
        // When double-clicking on the item for this pad the
        // object unsets and sets its edit mode without closing
        // the task panel
        Gui::TaskView::TaskDialog *dlg = Gui::Control().activeDialog();
        TaskDlgShapeBinder *sbDlg = qobject_cast<TaskDlgShapeBinder*>(dlg);
        if (dlg && !sbDlg && !dlg->tryClose())
            return false;

        // clear the selection (convenience)
        Gui::Selection().clearSelection();

        // start the edit dialog
        // another pad left open its task panel
        if (sbDlg)
            Gui::Control().showDialog(sbDlg);
        else
            Gui::Control().showDialog(new TaskDlgShapeBinder(this, ModNum == 1));

        return true;
    }
    else {
        return ViewProviderPart::setEdit(ModNum);
    }
}

void ViewProviderShapeBinder::unsetEdit(int ModNum) {

    PartGui::ViewProviderPart::unsetEdit(ModNum);
}

void ViewProviderShapeBinder::highlightReferences(bool on)
{
    App::GeoFeature* obj = nullptr;
    std::vector<std::string> subs;

    if (getObject()->isDerivedFrom(PartDesign::ShapeBinder::getClassTypeId()))
        PartDesign::ShapeBinder::getFilteredReferences(&static_cast<PartDesign::ShapeBinder*>(getObject())->Support, obj, subs);
    else
        return;

    // stop if not a Part feature was found
    if (!obj || !obj->isDerivedFrom<Part::Feature>())
        return;

    PartGui::ViewProviderPart* svp = dynamic_cast<PartGui::ViewProviderPart*>(
        Gui::Application::Instance->getViewProvider(obj));
    if (!svp)
        return;

    if (on) {
        if (!subs.empty() && originalLineColors.empty()) {
            TopTools_IndexedMapOfShape eMap;
            TopExp::MapShapes(static_cast<Part::Feature*>(obj)->Shape.getValue(), TopAbs_EDGE, eMap);
            originalLineColors = svp->LineColorArray.getValues();
            std::vector<App::Color> lcolors = originalLineColors;
            lcolors.resize(eMap.Extent(), svp->LineColor.getValue());

            TopExp::MapShapes(static_cast<Part::Feature*>(obj)->Shape.getValue(), TopAbs_FACE, eMap);
            originalFaceColors = svp->DiffuseColor.getValues();
            std::vector<App::Color> fcolors = originalFaceColors;
            fcolors.resize(eMap.Extent(), svp->ShapeColor.getValue());

            for (const std::string& e : subs) {
                // Note: stoi may throw, but it strictly shouldn't happen
                if (e.compare(0, 4, "Edge") == 0) {
                    int idx = std::stoi(e.substr(4)) - 1;
                    assert(idx >= 0);
                    if (idx < static_cast<int>(lcolors.size()))
                        lcolors[idx] = App::Color(1.0, 0.0, 1.0); // magenta
                }
                else if (e.compare(0, 4, "Face") == 0) {
                    int idx = std::stoi(e.substr(4)) - 1;
                    assert(idx >= 0);
                    if (idx < static_cast<int>(fcolors.size()))
                        fcolors[idx] = App::Color(1.0, 0.0, 1.0); // magenta
                }
            }
            svp->LineColorArray.setValues(lcolors);
            svp->DiffuseColor.setValues(fcolors);
        }
    }
    else {
        if (!subs.empty() && !originalLineColors.empty()) {
            svp->LineColorArray.setValues(originalLineColors);
            originalLineColors.clear();

            svp->DiffuseColor.setValues(originalFaceColors);
            originalFaceColors.clear();
        }
    }
}

void ViewProviderShapeBinder::setupContextMenu(QMenu* menu, QObject* receiver, const char* member)
{
    Q_UNUSED(receiver)
    Q_UNUSED(member)

        QAction* act;
    act = menu->addAction(QObject::tr("Edit shape binder"));
    act->setData(QVariant((int)ViewProvider::Default));

    Gui::ActionFunction* func = new Gui::ActionFunction(menu);
    func->trigger(act, [this]() {
        QString text = QObject::tr("Edit %1").arg(QString::fromUtf8(getObject()->Label.getValue()));
        Gui::Command::openCommand(text.toUtf8());

        Gui::Document* document = this->getDocument();
        if (document) {
            document->setEdit(this, ViewProvider::Default);
        }
    });
}

//=====================================================================================

PROPERTY_SOURCE(PartDesignGui::ViewProviderSubShapeBinder, PartGui::ViewProviderSubShapeBinder)

ViewProviderSubShapeBinder::ViewProviderSubShapeBinder()
{
}

////////////////////////////////////////////////////////////////////////////////////////

namespace Gui {
PROPERTY_SOURCE_TEMPLATE(PartDesignGui::ViewProviderSubShapeBinderPython,
                         PartDesignGui::ViewProviderSubShapeBinder)
template class PartDesignGuiExport ViewProviderPythonFeatureT<ViewProviderSubShapeBinder>;
}

