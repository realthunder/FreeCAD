/***************************************************************************
 *   Copyright (c) 2011 Juergen Riegel <FreeCAD@juergen-riegel.net>        *
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
# include <QMessageBox>
# include <QAction>
# include <QApplication>
# include <QMenu>
# include <QMouseEvent>
# include <Inventor/details/SoFaceDetail.h>
# include <BRepAdaptor_Surface.hxx>
# include <gp_Lin.hxx>
# include <TopExp.hxx>
# include <TopoDS.hxx>
# include <TopTools_IndexedMapOfShape.hxx>
#endif

#include <Base/Exception.h>
#include <App/Document.h>
#include <Gui/ActionFunction.h>
#include <Gui/Application.h>
#include <Gui/BitmapFactory.h>
#include <Gui/Command.h>
#include <Gui/Control.h>
#include <Gui/Document.h>
#include <Gui/BitmapFactory.h>
#include <Gui/SoFCUnifiedSelection.h>
#include <Gui/CommandT.h>
#include <Gui/ViewParams.h>
#include <Gui/Tree.h>
#include <Base/Exception.h>
#include <Base/Tools.h>
#include <Mod/Part/App/PartParams.h>
#include <Mod/Part/Gui/SoBrepFaceSet.h>
#include <Mod/Part/Gui/SoBrepEdgeSet.h>
#include <Mod/Part/Gui/SoBrepPointSet.h>
#include <Mod/PartDesign/App/Body.h>
#include <Mod/PartDesign/App/Feature.h>
#include <Mod/PartDesign/App/FeatureExtrusion.h>
#include <Mod/PartDesign/App/FeatureHole.h>
#include <Mod/PartDesign/App/FeatureTransformed.h>
#include <Mod/Sketcher/App/SketchObject.h>

#include "TaskFeatureParameters.h"

#include "ViewProvider.h"
#include "ViewProviderPy.h"
#include "ViewProviderBody.h"
#include "Utils.h"

using namespace PartDesignGui;

PROPERTY_SOURCE_WITH_EXTENSIONS(PartDesignGui::ViewProvider, PartGui::ViewProviderPart)

ViewProvider::ViewProvider()
: oldWb(""), isSetTipIcon(false)
{
    PartGui::ViewProviderAttachExtension::initExtension(this);
    ADD_PROPERTY(IconColor,((long)0));

    if (Gui::ViewParams::getRandomColor()){
        MapFaceColor.setValue(false);
        MapLineColor.setValue(false);
        MapPointColor.setValue(false);
        MapTransparency.setValue(false);
    }
}

ViewProvider::~ViewProvider() = default;

App::DocumentObject *ViewProvider::getPickedVisibilityTarget() const
{
    // What is picked in the view is the body's shape, whichever feature the
    // face came from: toggling that feature hid nothing, or showed it in
    // place of the Tip (upstream bd03414893, 20c01000a1)
    auto obj = getObject();
    auto body = PartDesign::Body::findBodyOf(obj);
    if (body && body->isSolidFeature(obj))
        return body;
    return nullptr;
}

bool ViewProvider::doubleClicked()
{
    std::string Msg("Edit ");
    Msg += this->pcObject->Label.getValue();
    App::AutoTransaction committer(Msg.c_str());
    try {
	    PartDesign::Body* body = PartDesign::Body::findBodyOf(getObject());
        // the edit mode the user picked, as a Part feature's double click
        // does (upstream f34f15dc60)
        PartDesignGui::setEdit(pcObject, body, PDBODYKEY,
                               Gui::Application::Instance->getUserEditMode());
    }
    catch (const Base::Exception &) {
        committer.close(true);
    }
    return true;
}

enum PartDesignEditMode {
    EditToggleSupress = ViewProvider::UserEditMode+1,
    EditSelectSiblings = ViewProvider::UserEditMode+2,
    EditExpandSiblings = ViewProvider::UserEditMode+3,
    EditExpandAll = ViewProvider::UserEditMode+4,
    EditCollapseSiblings = ViewProvider::UserEditMode+5,
    EditCollapseAll = ViewProvider::UserEditMode+6,
    EditSetTip = ViewProvider::UserEditMode+7,
};

void ViewProvider::startDefaultEditMode()
{
    QString text = QObject::tr("Edit %1").arg(QString::fromUtf8(getObject()->Label.getValue()));
    Gui::Command::openCommand(text.toUtf8());

    Gui::Document* document = this->getDocument();
    if (document) {
        document->setEdit(this, ViewProvider::Default);
    }
}

void ViewProvider::addDefaultAction(QMenu* menu, const QString& text)
{
    QAction* act = menu->addAction(text);
    act->setData(QVariant((int)ViewProvider::Default));
    Gui::ActionFunction* func = new Gui::ActionFunction(menu);
    func->trigger(act, std::bind(&ViewProvider::startDefaultEditMode, this));
}

void ViewProvider::setupContextMenu(QMenu* menu, QObject* receiver, const char* member)
{
    auto feat = Base::freecad_dynamic_cast<PartDesign::Feature>(getObject());
    auto body = PartDesign::Body::findBodyOf(feat);
    if (body) {
        auto act = menu->addAction(QObject::tr("Toggle export"), receiver, member);
        act->setData(QVariant((int)Gui::ViewProvider::ExportInGroup));
    }

    if(body && body->isSolidFeature(feat)) {
        QAction* act;
        act = menu->addAction(QObject::tr(
                    feat->Suppress.getValue()?"Unsuppress":"Suppress"),
                    receiver, member);
        act->setData(QVariant((int)EditToggleSupress));

        if (!body->AutoGroupSolids.getValue() && feat->_Siblings.getSize()) {
            act = menu->addAction(QObject::tr("Ungroup solid feature"), receiver, member);
            act->setData(QVariant((int)EditExpandSiblings));
        }

        auto siblings = body->getSiblings(feat);
        if ( siblings.size() > 1) {
            if (!body->AutoGroupSolids.getValue()) {
                auto it = std::find(siblings.begin(), siblings.end(), feat);
                if (it != siblings.end()) {
                    int pos = it - siblings.begin();
                    if (pos != feat->_Siblings.getSize()) {
                        act = menu->addAction(QObject::tr("Group solid feature"), receiver, member);
                        act->setData(QVariant((int)EditCollapseSiblings));
                    }
                    int grouped = 0;
                    int groupCount = 0;
                    for (auto o : siblings) {
                        auto sibling = Base::freecad_dynamic_cast<PartDesign::Feature>(o);
                        if (sibling && sibling->_Siblings.getSize()) {
                            grouped += 1 + sibling->_Siblings.getSize();
                            ++groupCount;
                        }
                    }
                    if (groupCount > 1 || grouped != (int)siblings.size()) {
                        act = menu->addAction(QObject::tr("Group all"), receiver, member);
                        act->setData(QVariant((int)EditCollapseAll));
                    }
                    if (groupCount && grouped != 1 + feat->_Siblings.getSize()) {
                        act = menu->addAction(QObject::tr("Ungroup all"), receiver, member);
                        act->setData(QVariant((int)EditExpandAll));
                    }
                }
            }
            act = menu->addAction(QObject::tr("Select solid features"), receiver, member);
            act->setData(QVariant((int)EditSelectSiblings));
        }

        if (body->Tip.getValue() != feat) {
            if (Gui::Application::Instance->commandManager().addTo("PartDesign_MoveTip", menu)) {
                menu->actions().back()->setData(QVariant((int)EditSetTip));
            }
        }
    }
    QAction* act = menu->addAction(QObject::tr("Set colors..."), receiver, member);
    act->setData(QVariant((int)ViewProvider::Color));
    // Call the extensions
    Gui::ViewProvider::setupContextMenu(menu, receiver, member);
}

bool ViewProvider::setEdit(int ModNum)
{
    auto feat = Base::freecad_dynamic_cast<PartDesign::Feature>(getObject());
    if (!feat)
        return false;

    switch(ModNum) {
    case ViewProvider::Default: {
        // When double-clicking on the item for this feature the
        // object unsets and sets its edit mode without closing
        // the task panel
        Gui::TaskView::TaskDialog *dlg = Gui::Control().activeDialog();
        TaskDlgFeatureParameters *featureDlg = qobject_cast<TaskDlgFeatureParameters *>(dlg);
        // NOTE: if the dialog is not partDesigan dialog the featureDlg will be NULL
        if (featureDlg && featureDlg->viewProvider() != this) {
            featureDlg = nullptr; // another feature left open its task panel
        }
        if (dlg && !featureDlg && !dlg->tryClose())
            return false;

        // clear the selection (convenience)
        Gui::Selection().clearSelection();

        // always change to PartDesign WB, remember where we come from
        oldWb = Gui::Command::assureWorkbench("PartDesignWorkbench");

        // start the edit dialog if
        if (!featureDlg) {
            featureDlg = this->getEditDialog();
            if (!featureDlg) // Shouldn't generally happen
                return false;
        }

        PartDesignGui::beforeEdit(getObject());
        Gui::Control().showDialog(featureDlg);
        return true;
    }
    case EditToggleSupress: {
        std::ostringstream ss;
        ss << (feat->Suppress.getValue()?"Unsuppress":"Suppress") << " " << feat->getNameInDocument();
        App::AutoTransaction committer(ss.str().c_str());
        try {
            if(feat->Suppress.getValue())
                Gui::cmdAppObject(feat, "Suppress = False");
            else
                Gui::cmdAppObject(feat, "Suppress = True");
            Gui::cmdAppDocument(App::GetApplication().getActiveDocument(), "recompute()");
        } catch (Base::Exception &e) {
            e.ReportException();
        }
        return false;
    }
    case EditCollapseSiblings:
    case EditCollapseAll:
    case EditExpandSiblings:
    case EditExpandAll:
    {
        auto bodyVp = Base::freecad_dynamic_cast<ViewProviderBody>(
                Gui::Application::Instance->getViewProvider(
                    PartDesign::Body::findBodyOf(getObject())));
        if (bodyVp)
            bodyVp->groupSiblings(feat,
                    ModNum == EditCollapseSiblings || ModNum == EditCollapseAll,
                    ModNum == EditCollapseAll || ModNum == EditExpandAll);
        return false;
    }
    case EditSetTip: {
        return false;
    }
    case EditSelectSiblings: {
        auto body = PartDesign::Body::findBodyOf(getObject());
        if (body) {
            auto ctx = Gui::Selection().getExtendedContext();
            if (ctx.getSubObject() != getObject())
                return false;
            do {
                ctx = ctx.getParent();
                if (ctx.getObjectName().empty())
                    break;
                if (ctx.getSubObject() == body) {
                    for (auto obj : body->getSiblings(getObject()))
                        Gui::Selection().addSelection(ctx.getChild(obj));
                    break;
                }
            } while(1);
        }
        return false;
    }
    default:
        return PartGui::ViewProviderPart::setEdit(ModNum);
    }
}


TaskDlgFeatureParameters *ViewProvider::getEditDialog() {
    return nullptr;
}


void ViewProvider::unsetEdit(int ModNum)
{
    if (ModNum == ViewProvider::Default) {
        // return to the WB we were in before editing the PartDesign feature
        if (!oldWb.empty()) {
            Gui::Command::assureWorkbench(oldWb.c_str());
            oldWb.clear();
        }

        // when pressing ESC make sure to close the dialog
        Gui::Control().closeDialog();
    }
    else {
        PartGui::ViewProviderPart::unsetEdit(ModNum);
    }
}

void ViewProvider::updateData(const App::Property* prop)
{
    auto feature = Base::freecad_dynamic_cast<PartDesign::Feature>(getObject());
    if(!feature) {
        inherited::updateData(prop);
        return;
    }

    if(prop == &feature->SuppressedShape) {
        if (feature->SuppressedShape.getShape().isNull()) {
            enableFullSelectionHighlight();
        } else {
            auto node = getDisplayMaskMode("Flat Lines");
            if (!pSuppressedView && node && node->isOfType(SoGroup::getClassTypeId())) {
                pSuppressedView.reset(new PartGui::ViewProviderPart);
                pSuppressedView->setShapePropertyName("SuppressedShape");
                pSuppressedView->forceUpdate();
                pSuppressedView->MapFaceColor.setValue(false);
                pSuppressedView->MapLineColor.setValue(false);
                pSuppressedView->MapPointColor.setValue(false);
                pSuppressedView->MapTransparency.setValue(false);
                pSuppressedView->ForceMapColors.setValue(false);
                pSuppressedView->ShapeColor.setValue(App::Color(1.0f));
                pSuppressedView->LineColor.setValue(App::Color(1.0f));
                pSuppressedView->PointColor.setValue(App::Color(1.0f));
                pSuppressedView->Selectable.setValue(false);
                pSuppressedView->enableFullSelectionHighlight(false, false, false);
                pSuppressedView->setStatus(Gui::SecondaryView,true);

                auto switchNode = getModeSwitch();
                if(switchNode->isOfType(SoFCSwitch::getClassTypeId()))
                    static_cast<SoFCSwitch*>(switchNode)->overrideSwitch = SoFCSwitch::OverrideVisible;

                pSuppressedView->attach(feature);

                static_cast<SoGroup*>(node)->addChild(pSuppressedView->getRoot());
            }

            if(pSuppressedView)
                enableFullSelectionHighlight(false, false, false);
        }

        if(pSuppressedView)
            pSuppressedView->updateData(prop);

    } else if (prop == &feature->Suppress) {
        signalChangeIcon();
    } else if (prop == &feature->_Siblings) {
        pxTipIcon = QPixmap();
        signalChangeIcon();
    } else if (prop == &feature->Shape) {
        if (!getObject()->getDocument()->isPerformingTransaction()
                && !getObject()->getDocument()->testStatus(App::Document::Restoring)
                && !IconColor.getValue().getPackedValue())
        {
            auto body = Base::freecad_dynamic_cast<ViewProviderBody>(
                    Gui::Application::Instance->getViewProvider(
                        PartDesign::Body::findBodyOf(getObject())));
            if (body) {
                unsigned long color = body->generateIconColor(getObject());
                if (color)
                    IconColor.setValue(color);
            }
        }
    } else if (prop == &feature->BaseFeature && !prop->testStatus(App::Property::User3)) {
        PartDesign::Body* body = PartDesign::Body::findBodyOf(getObject());
        auto bodyVp = Base::freecad_dynamic_cast<ViewProviderBody>(
                Gui::Application::Instance->getViewProvider(body));
        if (!getObject()->getDocument()->isPerformingTransaction()
                && !getObject()->getDocument()->testStatus(App::Document::Restoring)
                && bodyVp)
        {
            unsigned long color = 0;
            if (IconColor.getValue().getPackedValue()) {
                if (!body->isSolidFeature(getObject()))
                    this->IconColor.setValue(0);
                else {
                    if (!feature->BaseFeature.getValue())
                        color = bodyVp->generateIconColor();

                    bool first = true;
                    for (auto obj : body->getSiblings(feature)) {
                        auto vp = Base::freecad_dynamic_cast<ViewProvider>(
                                Gui::Application::Instance->getViewProvider(obj));
                        if (!vp)
                            continue;
                        if (first) {
                            first = false;
                            if (!color) {
                                color = vp->IconColor.getValue().getPackedValue();
                                if (!color) {
                                    color = IconColor.getValue().getPackedValue();
                                    if (!color) {
                                        color = bodyVp->generateIconColor();
                                        if (!color)
                                            break;
                                    }
                                }
                            }
                        }
                        vp->IconColor.setValue(color);
                    }
                }
            }

            if (!feature->BaseFeature.getValue()) {
                // Assign tag icon color (if none) of the initial solid that we are forked from
                std::set<App::DocumentObject*> checked;
                for (auto obj : body->Group.getValue()) {
                    if (!body->isSolidFeature(obj))
                        continue;
                    if (checked.count(obj))
                        continue;
                    auto vp = Base::freecad_dynamic_cast<ViewProvider>(
                            Gui::Application::Instance->getViewProvider(obj));
                    if (!vp || vp->IconColor.getValue().getPackedValue())
                        continue;
                    uint32_t color = 0;
                    auto siblings = body->getSiblings(obj, true, true);
                    for (auto sibling : siblings) {
                        auto vp = Base::freecad_dynamic_cast<ViewProvider>(
                                Gui::Application::Instance->getViewProvider(sibling));
                        if (!vp)
                            continue;
                        if (vp->IconColor.getValue().getPackedValue())
                            color = vp->IconColor.getValue().getPackedValue();
                        else {
                            if (!color)
                                color = bodyVp->generateIconColor();
                            vp->IconColor.setValue(color);
                        }
                    }
                }
            }
            bodyVp->checkSiblings();
        }
    }

    inherited::updateData(prop);

    // A new shape has new faces, and the holes behind it may have changed
    // what they thread: re-derive the cosmetic threads, but only where
    // there are some to state or some to take away -- the render material
    // is not rebuilt for every feature of every body on every recompute.
    if (prop == &feature->Shape) {
        const bool threads = hasCosmeticThreads(feature);
        if (threads || impliedThreads) {
            impliedThreads = threads;
            updateRenderMaterial();
        }
    }
}

namespace {

using Bore = PartDesign::Hole::CosmeticThreadBore;

/// A bore moved by a placement: its origin as a point, its direction as a
/// direction
Bore placed(Bore bore, const Base::Placement &pla)
{
    pla.multVec(bore.origin, bore.origin);
    pla.getRotation().multVec(bore.direction, bore.direction);
    return bore;
}

/// The threads of the holes up to and including \a last, in the body's
/// coordinates, stopping at the first one when \a first
std::vector<Bore> threadsReaching(const PartDesign::Feature *last, bool first)
{
    std::vector<Bore> bores;
    auto body = PartDesign::Body::findBodyOf(last);
    if (!body)
        return bores;
    const auto &group = body->Group.getValues();
    auto end = std::find(group.begin(), group.end(), last);
    if (end == group.end())
        return bores;
    ++end;
    for (auto it = group.begin(); it != end; ++it) {
        if (auto hole = Base::freecad_dynamic_cast<PartDesign::Hole>(*it)) {
            for (const Bore &bore : hole->getCosmeticThreads()) {
                bores.push_back(placed(bore, hole->Placement.getValue()));
                if (first)
                    return bores;
            }
            continue;
        }
        auto pattern = Base::freecad_dynamic_cast<PartDesign::Transformed>(*it);
        if (!pattern || pattern->Suppress.getValue())
            continue;
        // The holes the pattern copies. A MultiTransform's steps have no
        // originals of their own; the MultiTransform does.
        std::vector<App::DocumentObject *> originals = pattern->Originals.getValues();
        for (auto obj : pattern->OriginalSubs.getValues())
            originals.push_back(obj);
        std::vector<Bore> sources;
        std::set<App::DocumentObject *> seen;
        const Base::Placement toPattern = pattern->Placement.getValue().inverse();
        for (auto obj : originals) {
            auto hole = Base::freecad_dynamic_cast<PartDesign::Hole>(obj);
            if (!hole || !seen.insert(obj).second)
                continue;
            for (const Bore &bore : hole->getCosmeticThreads())
                sources.push_back(placed(bore, toPattern * hole->Placement.getValue()));
        }
        if (sources.empty())
            continue;
        std::list<gp_Trsf> transforms;
        try {
            // Only Scaled needs the original shapes, and a thread scaled
            // is no longer the thread its hole states anyway
            transforms = pattern->getTransformations({});
        }
        catch (const Base::Exception &) {
            continue;
        }
        catch (const Standard_Failure &) {
            continue;
        }
        for (const gp_Trsf &trsf : transforms) {
            if (std::fabs(std::fabs(trsf.ScaleFactor()) - 1.0) > 1e-9)
                continue;
            for (Bore bore : sources) {
                gp_Pnt o(bore.origin.x, bore.origin.y, bore.origin.z);
                gp_Vec d(bore.direction.x, bore.direction.y, bore.direction.z);
                o.Transform(trsf);
                d.Transform(trsf);
                bore.origin = Base::Vector3d(o.X(), o.Y(), o.Z());
                // A mirror copies the hole, not the tap: the thread keeps
                // the hand its hole states, as the part it drills would
                bore.direction = Base::Vector3d(d.X(), d.Y(), d.Z());
                bores.push_back(placed(bore, pattern->Placement.getValue()));
                if (first)
                    return bores;
            }
        }
    }
    return bores;
}

} // namespace

bool ViewProvider::hasCosmeticThreads(const PartDesign::Feature *last)
{
    return last && !threadsReaching(last, true).empty();
}

void ViewProvider::cosmeticThreadFinishes(const PartDesign::Feature *last,
                                          const Base::Placement &toLocal,
                                          const TopoDS_Shape &shape,
                                          std::vector<ImpliedFinish> &finishes)
{
    if (!last || shape.IsNull())
        return;
    std::vector<Bore> bores = threadsReaching(last, false);
    if (bores.empty())
        return;
    for (Bore &bore : bores) {
        bore = placed(bore, toLocal);
        bore.direction.Normalize();
    }

    TopTools_IndexedMapOfShape faces;
    TopExp::MapShapes(shape.Located(TopLoc_Location()), TopAbs_FACE, faces);
    for (int i = 1; i <= faces.Extent(); ++i) {
        gp_Ax1 axis;
        double radius = -1.0;
        double semiAngle = 0.0;
        try {
            BRepAdaptor_Surface adapt(TopoDS::Face(faces(i)));
            switch (adapt.GetType()) {
            case GeomAbs_Cylinder:
                axis = adapt.Cylinder().Axis();
                radius = adapt.Cylinder().Radius();
                break;
            case GeomAbs_Cone:
                axis = adapt.Cone().Axis();
                semiAngle = std::fabs(adapt.Cone().SemiAngle());
                break;
            default:
                continue;
            }
        }
        catch (const Standard_Failure &) {
            continue;
        }
        const gp_Lin line(axis);
        for (const Bore &bore : bores) {
            const gp_Dir dir(bore.direction.x, bore.direction.y, bore.direction.z);
            if (!axis.Direction().IsParallel(dir, 1e-6))
                continue;
            const gp_Pnt origin(bore.origin.x, bore.origin.y, bore.origin.z);
            const double tol = std::max(1e-5, 1e-7 * bore.radius);
            if (line.Distance(origin) > tol)
                continue;
            if (bore.taper == 0.0) {
                if (radius < 0.0 || std::fabs(radius - bore.radius) > tol)
                    continue;
            }
            else if (radius >= 0.0 || std::fabs(semiAngle - bore.taper) > 1e-6)
                continue;

            ImpliedFinish finish;
            finish.face = i - 1;
            finish.finish.pattern = bore.leftHand ? App::SurfaceFinish::ThreadLeft
                                                  : App::SurfaceFinish::Thread;
            finish.finish.pitch = float(bore.pitch);
            finish.finish.depth = float(bore.height);
            finish.finish.angle = float(bore.profileAngle);
            finish.finish.normalize();
            // The band starts a couple of turns short of the mouth, so the
            // runout the shader gives either end of it only shows where
            // the tap stopped
            const double z0 = bore.origin.Dot(bore.direction);
            finish.setExtent(bore.direction.x, bore.direction.y, bore.direction.z,
                             z0 - 2.0 * bore.pitch, z0 + bore.length);
            finishes.push_back(finish);
            break;
        }
    }
}

void ViewProvider::getImpliedFinishes(std::vector<ImpliedFinish> &finishes) const
{
    auto feature = Base::freecad_dynamic_cast<PartDesign::Feature>(getObject());
    if (!feature || !PartDesign::Body::findBodyOf(feature))
        return;
    TopoDS_Shape shape;
    try {
        shape = getShape().getShape();
    }
    catch (const Base::Exception &) {
        return;
    }
    cosmeticThreadFinishes(feature, feature->Placement.getValue().inverse(), shape, finishes);
}

void ViewProvider::updateVisual()
{
    inherited::updateVisual();
    if (testStatus(Gui::Detach))
        return;
    auto feature = Base::freecad_dynamic_cast<PartDesign::Feature>(getObject());
    if (feature && feature->SuppressedShape.getShape().isNull()) {
        std::vector<int> faces;
        std::vector<int> edges;
        std::vector<int> vertices;
        feature->getGeneratedIndices(faces,edges,vertices);

        lineset->highlightIndices.setNum(edges.size());
        if (edges.size())
            lineset->highlightIndices.setValues(0,edges.size(),&edges[0]);
        nodeset->highlightIndices.setNum(vertices.size());
        if (vertices.size())
            nodeset->highlightIndices.setValues(0,vertices.size(),&vertices[0]);
        faceset->highlightIndices.setNum(faces.size());
        if (faces.size())
            faceset->highlightIndices.setValues(0,faces.size(),&faces[0]);
    }
}

void ViewProvider::onChanged(const App::Property* prop) {
    if (prop == &IconColor) {
        pxTipIcon = QPixmap();
        signalChangeIcon();

        auto feature = Base::freecad_dynamic_cast<PartDesign::Feature>(getObject());
        auto body = PartDesign::Body::findBodyOf(getObject());
        if (feature && body) {
            auto siblings = body->getSiblings(feature);
            if(feature->_Siblings.getSize()) {
                if (siblings.size() && siblings.front() == feature)
                    feature->_Siblings.setValues();
            }

            if (IconColor.getValue().getPackedValue()
                    && Gui::ViewParams::getRandomColor())
            {
                if (siblings.size() && siblings.front() == feature)
                    MapFaceColor.setValue(false);
                ShapeColor.setValue(IconColor.getValue());
            }
        }
    }
    else if (auto linkProp = Base::freecad_dynamic_cast<App::PropertyLinkBase>(
                const_cast<App::Property*>(prop)))
    {
        if (autoCorrectingLink || !Part::PartParams::getAutoCorrectLink()) {
            PartGui::ViewProviderPartExt::onChanged(prop);
            return;
        }

        Base::StateLocker guard(autoCorrectingLink);

        auto body = PartDesign::Body::findBodyOf(getObject());
        auto editDoc = Gui::Application::Instance->editDocument();
        if (body && editDoc && editDoc->getInEdit() == this
                 && linkProp->getScope() == App::LinkScope::Local
                 && (prop->isDerivedFrom(App::PropertyLink::getClassTypeId())
                     || prop->isDerivedFrom(App::PropertyLinkSub::getClassTypeId())
                     || prop->isDerivedFrom(App::PropertyLinkSubList::getClassTypeId())))
        {
            // Auto import cross coordinate link to body using sub shape binder
            auto links = linkProp->linkedElements();
            std::map<App::DocumentObject*, App::DocumentObject*> imports;
            std::vector<App::SubObjectT> sels;
            try {
                App::AutoTransaction committer(
                        QT_TRANSLATE_NOOP("Command", "Auto import to body"), true);
                for (auto &v : links) {
                    bool invalid = false;
                    for (auto & sub : v.second) {
                        App::SubObjectT sobjT(v.first, sub.c_str());
                        if (sobjT.getSubObject() != v.first) {
                            invalid = true;
                            break;
                        }
                        // Make sure all element name are old style, so that we
                        // can transfer it to binder
                        sub = sobjT.getOldElementName();
                    }
                    if (invalid) // Do not support converting sub object link yet
                        continue;
                    App::DocumentObject *otherGroup = nullptr;
                    for (auto obj : v.first->getInList()) {
                        if (obj != body)
                            continue;
                        if (obj->hasExtension(App::GeoFeatureGroupExtension::getExtensionClassTypeId())) {
                            otherGroup = obj;
                            break;
                        }
                    }
                    if (otherGroup) {
                        if (sels.empty())
                            sels = Gui::Selection().getSelectionT("*", Gui::ResolveMode::NoResolve);
                        App::SubObjectT ref;
                        for (auto &sel : sels) {
                            if (sel.getSubObject() == v.first) {
                                ref = App::SubObjectT(sel.getObject(), sel.getSubNameNoElement().c_str());
                                break;
                            }
                        }
                        if (ref.getObjectName().empty()) {
                            std::vector<App::DocumentObject*> objs;
                            objs.push_back(otherGroup);
                            objs.push_back(v.first);
                            while (auto grp = App::GeoFeatureGroupExtension::getGroupOfObject(otherGroup)) {
                                objs.insert(objs.begin(), grp);
                                otherGroup = grp;
                            }
                            ref = App::SubObjectT(objs);
                        }
                        ref = PartDesignGui::importExternalObject(ref, false);
                        imports.emplace(v.first, ref.getSubObject());
                    }
                }
                if (imports.size()) {
                    if (auto link = Base::freecad_dynamic_cast<App::PropertyLink>(linkProp)) {
                        link->setValue(imports.begin()->second);
                    } else if (auto linksub = Base::freecad_dynamic_cast<App::PropertyLinkSub>(linkProp)) {
                        auto it = links.find(imports.begin()->first);
                        if (it != links.end())
                            linksub->setValue(imports.begin()->second, it->second);
                    } else if (auto linksubs = Base::freecad_dynamic_cast<App::PropertyLinkSubList>(linkProp)) {
                        std::vector<App::DocumentObject*> objs;
                        std::vector<std::string> subs;
                        for (auto &v : links) {
                            auto it = imports.find(v.first);
                            if (it != imports.end())
                                objs.push_back(it->second);
                            else
                                objs.push_back(v.first);
                            if (v.second.empty())
                                subs.emplace_back();
                            else {
                                bool first = true;
                                for (auto &sub : v.second) {
                                    if (first)
                                        first = false;
                                    else
                                        objs.push_back(objs.back());
                                    subs.push_back(std::move(sub));
                                }
                            }
                        }
                        linksubs->setValues(objs, subs);
                    }
                }
            } catch (Base::Exception &e) {
                e.ReportException();
            }
        }
    }

    PartGui::ViewProviderPartExt::onChanged(prop);
}

void ViewProvider::setTipIcon(bool onoff)
{
    if (isSetTipIcon != onoff) {
        isSetTipIcon = onoff;
        pxTipIcon = QPixmap();
        signalChangeIcon();
    }
}

QPixmap ViewProvider::getTagIcon() const
{
    auto feat = Base::freecad_dynamic_cast<PartDesign::Feature>(getObject());
    if (!feat)
        return QPixmap();

    unsigned long color = IconColor.getValue().getPackedValue();
    if (!color && !isSetTipIcon && !feat->_Siblings.getSize())
        return QPixmap();

    if (!color)
        color = 0xffffffff;

    if(pxTipIcon.isNull()) {
        std::map<unsigned long, unsigned long> colormap;
        colormap[0xffffff] = color >> 8;
        if (isSetTipIcon)
            colormap[0xf0f0f0] = 0x00ff00;
        pxTipIcon = Gui::BitmapFactory().pixmapFromSvg("PartDesign_Overlay.svg",
                                                        QSizeF(64,64),
                                                        colormap);
        if (feat->_Siblings.getSize()) {
            QPixmap px(64, 64);
            QPainter pt;
            px.fill(Qt::transparent);
            pt.begin(&px);
            pt.setRenderHints(QPainter::Antialiasing|QPainter::SmoothPixmapTransform);
            pt.rotate(10);
            pt.drawPixmap(0, 0, pxTipIcon);
            pt.rotate(-20);
            pt.drawPixmap(0, 0, pxTipIcon);
            pt.end();
            pxTipIcon = px;
        }
    }
    return pxTipIcon;
}

static QByteArray _IconTag("PartDesign::Tag");
static QByteArray _SuppressedTag("PartDesign::Suppressed");

void ViewProvider::getExtraIcons(std::vector<std::pair<QByteArray, QPixmap> > &icons) const
{
    auto feat = Base::freecad_dynamic_cast<PartDesign::Feature>(getObject());
    if (!feat) {
        inherited::getExtraIcons(icons);
        return;
    }

    QPixmap px = getTagIcon();
    if (!px.isNull())
        icons.emplace_back(_IconTag,px);

    if(feat->Suppress.getValue())
        icons.emplace_back(_SuppressedTag, Gui::BitmapFactory().pixmap("PartDesign_Suppressed.svg"));

    inherited::getExtraIcons(icons);
}

bool ViewProvider::iconMouseEvent(QMouseEvent *ev, const QByteArray &tag)
{
    auto feat = Base::freecad_dynamic_cast<PartDesign::Feature>(getObject());
    if (!feat)
        return false;
    if (ev->type() == QEvent::MouseButtonPress && ev->button() == Qt::LeftButton) {
        if (tag == _SuppressedTag) {
            App::AutoTransaction committer("Unsuppress");
            try {
                if(feat->Suppress.getValue())
                    Gui::cmdAppObject(feat, "Suppress = False");
                else
                    Gui::cmdAppObject(feat, "Suppress = True");
                Gui::cmdAppDocument(App::GetApplication().getActiveDocument(), "recompute()");
            } catch (Base::Exception &e) {
                e.ReportException();
            }
            return true;
        } else if (tag == _IconTag && !isSetTipIcon) {
            if (auto cmd = Gui::Application::Instance->commandManager().getCommandByName("PartDesign_MoveTip")) {
                cmd->invoke(0);
            }
            return true;
        }
    }
    return inherited::iconMouseEvent(ev, tag);
}

QString ViewProvider::getToolTip(const QByteArray &tag) const
{
    if (!Gui::isTreeViewDragging()) {
        if (tag == _SuppressedTag)
            return QObject::tr("Feature suppressed. ALT + click this icon to unsppress.");
        else if (tag == _IconTag) {
            if (isSetTipIcon)
                return QObject::tr("This is the tip of the body. New feature will be inserted after it.");
            else
                return QObject::tr("Alt + click this icon to set this feature as the tip of the body.\n"
                                "New feature will be inserted after it.");
        }
    }
    return inherited::getToolTip(tag);
}

bool ViewProvider::onDelete(const std::vector<std::string> &)
{
    PartDesign::Feature* feature = static_cast<PartDesign::Feature*>(getObject());

    App::DocumentObject* previousfeat = feature->BaseFeature.getValue();

    // Visibility - we want:
    // 1. If the visible object is not the one being deleted, we leave that one visible.
    // 2. If the visible object is the one being deleted, we make the previous object visible.
    if (isShow() && previousfeat && Gui::Application::Instance->getViewProvider(previousfeat)) {
        Gui::Application::Instance->getViewProvider(previousfeat)->show();
    }

    // find surrounding features in the tree
    Part::BodyBase* body = PartDesign::Body::findBodyOf(getObject());

    if (body) {
        // Deletion from the tree of a feature is handled by Document.removeObject, which has no clue
        // about what a body is. Therefore, Bodies, although an "activable" container, know nothing
        // about what happens at Document level with the features they contain.
        //
        // The Deletion command StdCmdDelete::activated, however does notify the viewprovider corresponding
        // to the feature (not body) of the imminent deletion (before actually doing it).
        //
        // Consequently, the only way of notifying a body of the imminent deletion of one of its features
        // so as to do the clean up required (moving basefeature references, tip management) is from the
        // viewprovider, so we call it here.
        //
        // fixes (#3084)

        FCMD_OBJ_CMD(body,"removeObject(" << Gui::Command::getObjectCmd(feature) << ')');
    }

    return true;
}

void ViewProvider::setBodyMode(bool bodymode) {

    std::vector<App::Property*> props;
    getPropertyList(props);

    auto vp = getBodyViewProvider();
    if(!vp)
        return;

#if 1
    // Realthunder: I want to test element color mapping in PartDesign.
    // If it works well, then there is no reason to hide all the properties.
    (void)bodymode;
#else
    for(App::Property* prop : props) {

        //we keep visibility and selectibility per object
        if(prop == &Visibility ||
           prop == &Selectable)
            continue;

        //we hide only properties which are available in the body, not special ones
        if(!vp->getPropertyByName(prop->getName()))
            continue;

        prop->setStatus(App::Property::Hidden, bodymode);
    }
#endif
}

void ViewProvider::makeTemporaryVisible(bool onoff)
{
    //make sure to not use the overridden versions, as they change properties
    if (onoff) {
        if (VisualTouched) {
            updateVisual();
        }
        Gui::ViewProvider::show();
    }
    else
        Gui::ViewProvider::hide();
}

PyObject* ViewProvider::getPyObject()
{
    if (!pyViewObject)
        pyViewObject = new ViewProviderPy(this);
    pyViewObject->IncRef();
    return pyViewObject;
}

ViewProviderBody* ViewProvider::getBodyViewProvider() {

    auto body = PartDesign::Body::findBodyOf(getObject());
    auto doc = getDocument();
    if(body && doc) {
        auto vp = doc->getViewProvider(body);
        if(vp && vp->isDerivedFrom(ViewProviderBody::getClassTypeId()))
           return static_cast<ViewProviderBody*>(vp);
    }

    return nullptr;
}

bool ViewProvider::hasBaseFeature() const{
    auto feature = Base::freecad_dynamic_cast<PartDesign::Feature>(getObject());
    if(feature && feature->getBaseObject(true))
        return true;
    return PartGui::ViewProviderPart::hasBaseFeature();
}

bool ViewProvider::canReplaceObject(App::DocumentObject *oldObj, App::DocumentObject *newObj)
{
    auto vp = Gui::Application::Instance->getViewProvider(
            PartDesign::Body::findBodyOf(getObject()));
    return vp && vp->canReplaceObject(oldObj, newObj);
}

int ViewProvider::replaceObject(App::DocumentObject *oldObj, App::DocumentObject *newObj)
{
    auto vp = Gui::Application::Instance->getViewProvider(
            PartDesign::Body::findBodyOf(getObject()));
    if (vp)
        return vp->replaceObject(oldObj, newObj);
    return 0;
}

bool ViewProvider::canReorderObject(App::DocumentObject *obj, App::DocumentObject *before)
{
    auto vp = Gui::Application::Instance->getViewProvider(
            PartDesign::Body::findBodyOf(getObject()));
    return vp && vp->canReorderObject(obj, before);
}

bool ViewProvider::reorderObjects(const std::vector<App::DocumentObject *> &objs, App::DocumentObject *before)
{
    auto vp = Gui::Application::Instance->getViewProvider(
            PartDesign::Body::findBodyOf(getObject()));
    if (!vp)
        return false;

    auto feature = Base::freecad_dynamic_cast<PartDesign::Feature>(getObject());
    if (!feature)
        return false;
    const auto &siblings = feature->_Siblings.getValues();
    if (siblings.empty())
        return false;

    // Siblings are grouped in reverse order, so must adjust 'before'
    if (!before)
        before = siblings.back();
    else {
        auto it = std::find(siblings.begin(), siblings.end(), before);
        if (it == siblings.end())
            return false;
        for (--it; it != siblings.begin(); --it) {
            if (std::find(objs.begin(), objs.end(), *it) == objs.end()) {
                before = *it;
                break;
            }
        }
        if (it == siblings.begin())
            before = getObject();
    }

    if (objs.size() > 1) {
        auto reversed = objs;
        std::reverse(reversed.begin(), reversed.end());
        return vp->reorderObjects(reversed, before);
    }
    return vp->reorderObjects(objs, before);
}

std::vector<App::DocumentObject*> ViewProvider::claimChildren(void) const
{
    auto res = _claimChildren();
    auto children = inherited::claimChildren();
    res.insert(res.end(), children.begin(), children.end());
    auto feature = Base::freecad_dynamic_cast<PartDesign::Feature>(getObject());
    if (feature) {
        const auto &siblings = feature->_Siblings.getValues();
        res.insert(res.end(), siblings.begin(), siblings.end());
    }
    return res;
}

void ViewProvider::beforeDelete()
{
    if (pSuppressedView)
        pSuppressedView->beforeDelete();
    inherited::beforeDelete();
}

void ViewProvider::reattach(App::DocumentObject *obj)
{
    inherited::reattach(obj);
    if (pSuppressedView)
        pSuppressedView->reattach(obj);
}

bool ViewProvider::getDetailPath(
        const char *subname, SoFullPath *path, bool append, SoDetail *&det) const
{
    if (!Data::isElementName(subname)) {
        auto body = PartDesign::Body::findBodyOf(getObject());
        auto dot = strchr(subname ? subname : "", '.');
        if (body && dot) {
            std::string sub(subname, dot-subname+1);
            auto svp = Gui::Application::Instance->getViewProvider(body->getSubObject(sub.c_str()));
            if (svp)
                return svp->getDetailPath(dot+1, path, append, det);
        }
    }
    return inherited::getDetailPath(subname, path, append, det);
}


namespace Gui {
/// @cond DOXERR
PROPERTY_SOURCE_TEMPLATE(PartDesignGui::ViewProviderPython, PartDesignGui::ViewProvider)
/// @endcond

// explicit template instantiation
template class PartDesignGuiExport ViewProviderFeaturePythonT<PartDesignGui::ViewProvider>;
}

