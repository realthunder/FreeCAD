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
# include <Inventor/actions/SoGetBoundingBoxAction.h>
# include <Inventor/SbColor.h>
# include <Precision.hxx>
# include <QMenu>
# include <QMessageBox>
#endif

#include <App/Part.h>
#include <App/Origin.h>
#include <App/DocumentObserver.h>
#include <Base/Console.h>
#include <Base/Tools.h>
#include <Gui/ActionFunction.h>
#include <Gui/Application.h>
#include <Gui/Command.h>
#include <Gui/Document.h>
#include <Gui/MDIView.h>
#include <Gui/View3DInventor.h>
#include <Gui/View3DInventorViewer.h>
#include <Gui/ViewProviderOrigin.h>
#include <Gui/ViewProviderOriginFeature.h>
#include <Gui/SoFCUnifiedSelection.h>
#include <Gui/MainWindow.h>
#include <Gui/Selection.h>

#include <Mod/PartDesign/App/Body.h>
#include <Mod/PartDesign/App/FeatureSketchBased.h>
#include <Mod/PartDesign/App/FeatureSolid.h>
#include <Mod/PartDesign/App/FeatureExtrusion.h>
#include <Mod/PartDesign/App/FeatureMultiTransform.h>
#include <Mod/PartDesign/App/DatumLine.h>
#include <Mod/PartDesign/App/DatumPlane.h>
#include <Mod/PartDesign/App/DatumCS.h>
#include <Mod/PartDesign/App/FeatureSketchBased.h>
#include <Mod/PartDesign/App/FeatureBase.h>
#include <Mod/PartDesign/App/FeatureWrap.h>
#include <Mod/PartDesign/App/ShapeBinder.h>
#include <Mod/PartDesign/App/AuxGroup.h>

#include "ViewProviderBody.h"
#include "Utils.h"
#include "ViewProvider.h"
#include "ViewProviderDatum.h"


using namespace PartDesignGui;
namespace sp = std::placeholders;

const char* PartDesignGui::ViewProviderBody::BodyModeEnum[] = {"Through","Tip",nullptr};

PROPERTY_SOURCE_WITH_EXTENSIONS(PartDesignGui::ViewProviderBody,PartGui::ViewProviderPart)

ViewProviderBody::ViewProviderBody()
{
    PartDesignGui::initMonitor();

    ADD_PROPERTY(DisplayModeBody,((long)0));
    DisplayModeBody.setEnums(BodyModeEnum);

    sPixmap = "PartDesign_Body_Tree.svg";

    MapTransparency.setValue(true);
    MapFaceColor.setValue(true);
    MapLineColor.setValue(true);
    MapPointColor.setValue(true);
    
    Gui::ViewProviderOriginGroupExtension::initExtension(this);
}

ViewProviderBody::~ViewProviderBody()
{
}

void ViewProviderBody::attach(App::DocumentObject *pcFeat)
{
    // call parent attach method
    ViewProviderPart::attach(pcFeat);

    //set default display mode
    onChanged(&DisplayModeBody);

}

// TODO on activating the body switch to the "Through" mode (2015-09-05, Fat-Zer)
// TODO different icon in tree if mode is Through (2015-09-05, Fat-Zer)
// TODO drag&drop (2015-09-05, Fat-Zer)
// TODO Add activate () call (2015-09-08, Fat-Zer)

void ViewProviderBody::setDisplayMode(const char* ModeName) {

    //if we show "Through" we must avoid to set the display mask modes, as this would result
    //in going into "tip" mode. When through is chosen the child features are displayed, and all
    //we need to ensure is that the display mode change is propagated to them from within the
    //onChanged() method.
    if(DisplayModeBody.getValue() == 1)
        PartGui::ViewProviderPartExt::setDisplayMode(ModeName);
}

void ViewProviderBody::setOverrideMode(const std::string& mode) {

    //if we are in through mode, we need to ensure that the override mode is not set for the body
    //(as this would result in "tip" mode), it is enough when the children are set to the correct
    //override mode.

    if(DisplayModeBody.getValue() != 0)
        Gui::ViewProvider::setOverrideMode(mode);
    else
        overrideMode = mode;
}

void ViewProviderBody::setupContextMenu(QMenu* menu, QObject* receiver, const char* member)
{
    Q_UNUSED(receiver);
    Q_UNUSED(member);

    auto body = Base::freecad_dynamic_cast<PartDesign::Body>(getObject());
    if (!body)
        return;

    Gui::ActionFunction* func = new Gui::ActionFunction(menu);
    QAction* act = menu->addAction(tr("Toggle active body"));
    func->trigger(act, [this]() {
        this->doubleClicked();
    });

    act = menu->addAction(body->AutoGroupSolids.getValue() ? 
                          tr("Disable auto solid group") : tr("Enable auto solid group"));
    func->trigger(act,
        [this]() {
            auto body = Base::freecad_dynamic_cast<PartDesign::Body>(getObject());
            if (!body)
                return;
            App::AutoTransaction committer("Toggle solid group");
            try {
                body->AutoGroupSolids.setValue(!body->AutoGroupSolids.getValue());
            } catch (Base::Exception &e) {
                e.ReportException();
                QMessageBox::critical(Gui::getMainWindow(), tr("Auto group"),
                        tr("Toggle auto group failed. Please check report view for more details."));
                committer.close(true);
            }
        });

    act = menu->addAction(tr("Toggle auto auxiliary group"));
    func->trigger(act,
        [this]() {
            auto body = Base::freecad_dynamic_cast<PartDesign::Body>(getObject());
            if (!body)
                return;
            App::AutoTransaction committer("Toggle aux group");
            try {
                std::vector<App::DocumentObjectT> groups;
                for (auto obj : body->Group.getValues()) {
                    if (obj->isDerivedFrom(PartDesign::AuxGroup::getClassTypeId())) {
                        auto group = static_cast<PartDesign::AuxGroup*>(obj);
                        if (group->getGroupType() != PartDesign::AuxGroup::OtherGroup)
                            groups.emplace_back(obj);
                    }
                }
                if (groups.empty()) {
                    int pos = 0;
                    auto children = body->Group.getValues();
                    if (children.size() && children[0] == body->BaseFeature.getValue())
                        ++pos;
                    auto sketchGroup = static_cast<PartDesign::AuxGroup*>(
                            body->getDocument()->addObject("PartDesign::AuxGroup", "Sketches"));
                    sketchGroup->Label.setValue("Sketches");
                    auto datumGroup = static_cast<PartDesign::AuxGroup*>(
                            body->getDocument()->addObject("PartDesign::AuxGroup", "Datums"));
                    datumGroup->Label.setValue("Datums");
                    auto miscGroup = static_cast<PartDesign::AuxGroup*>(
                            body->getDocument()->addObject("PartDesign::AuxGroup", "Misc"));
                    miscGroup->Label.setValue("Misc");
                    children.insert(children.begin()+pos, miscGroup);
                    children.insert(children.begin()+pos, datumGroup);
                    children.insert(children.begin()+pos, sketchGroup);
                    body->Group.setValues(children);
                    sketchGroup->_Body.setValue(body);
                    datumGroup->_Body.setValue(body);
                    miscGroup->_Body.setValue(body);
                } else {
                    for (auto & objT : groups) {
                        auto group = static_cast<PartDesign::AuxGroup*>(objT.getObject());
                        if (group) {
                            group->_Body.setValue(nullptr);
                            body->getDocument()->removeObject(group->getNameInDocument());
                        }
                    }
                }
            } catch (Base::Exception &e) {
                e.ReportException();
                QMessageBox::critical(Gui::getMainWindow(), tr("Auto group"),
                        tr("Toggle auto group failed. Please check report view for more details."));
                committer.close(true);
            }
        });

    act = menu->addAction(tr("Add group"));
    func->trigger(act,
        [this]() {
            auto body = Base::freecad_dynamic_cast<PartDesign::Body>(getObject());
            if (!body)
                return;
            App::AutoTransaction committer("Body add group");
            try {
                auto group = static_cast<PartDesign::AuxGroup*>(
                        body->getDocument()->addObject("PartDesign::AuxGroup", "Group"));
                body->addObject(group);
                group->_Body.setValue(body);
            } catch (Base::Exception &e) {
                e.ReportException();
                QMessageBox::critical(Gui::getMainWindow(), tr("Add group"),
                        tr("Failed to add group. Please check report view for more details."));
                committer.close(true);
            }
        });

    Gui::ViewProviderGeometryObject::setupContextMenu(menu, receiver, member);
}

bool ViewProviderBody::doubleClicked()
{
    //first, check if the body is already active.
    auto activeDoc = Gui::Application::Instance->activeDocument();
    if(!activeDoc)
        activeDoc = getDocument();
    auto activeView = activeDoc->setActiveView(this);
    if(!activeView)
        return false;

    App::DocumentObject *topParent = nullptr;
    std::string subname;
    if (activeView->getActiveObject<App::DocumentObject*>(PDBODYKEY, &topParent, &subname) == getObject()) {
        bool selected;
        {
            Gui::SelectionNoTopParentCheck guard;
            selected = Gui::Selection().isSelected(topParent ? topParent : getObject(), subname.c_str(), Gui::ResolveMode::NoResolve);
        }
        if (selected) {
            //active body double-clicked. Deactivate.
            Gui::Command::doCommand(Gui::Command::Gui,
                    "Gui.ActiveDocument.ActiveView.setActiveObject('%s', None)", PDBODYKEY);
            return true;
        }
    }
    // assure the PartDesign workbench
    if(App::GetApplication().GetUserParameter().GetGroup("BaseApp/Preferences/Mod/PartDesign")->GetBool("SwitchToWB", true))
        Gui::Command::assureWorkbench("PartDesignWorkbench");

    Gui::Command::doCommand(Gui::Command::Gui,
            "Gui.ActiveDocument.ActiveView.setActiveObject('%s',%s)",
            PDBODYKEY, getObject()->getFullName(true).c_str());

    topParent = nullptr;
    subname.clear();
    activeView->getActiveObject<App::DocumentObject*>(PDBODYKEY, &topParent, &subname);
    if (topParent) {
        auto objs = topParent->getSubObjectList(subname.c_str());
        if (objs.size() > 1) {
            auto parent = objs[objs.size()-2]->getLinkedObject(true);
            if (parent->isDerivedFrom(App::Part::getClassTypeId())) {
                objs.pop_back();
                App::SubObjectT part(objs);
                if (!activeView->isActiveObject(part.getObject(),PARTKEY,part.getSubName().c_str()))
                    Gui::Command::doCommand(Gui::Command::Gui,
                            "Gui.ActiveDocument.ActiveView.setActiveObject('%s',%s, u'%s')",
                            PARTKEY, part.getObjectPython().c_str(), part.getSubName().c_str());
            }
        }
    }
    return true;
}


// TODO To be deleted (2015-09-08, Fat-Zer)
//void ViewProviderBody::updateTree()
//{
//    if (ActiveGuiDoc == NULL) return;
//
//    // Highlight active body and all its features
//    //Base::Console().Error("ViewProviderBody::updateTree()\n");
//    PartDesign::Body* body = static_cast<PartDesign::Body*>(getObject());
//    bool active = body->IsActive.getValue();
//    //Base::Console().Error("Body is %s\n", active ? "active" : "inactive");
//    ActiveGuiDoc->signalHighlightObject(*this, Gui::Blue, active);
//    std::vector<App::DocumentObject*> features = body->Group.getValues();
//    bool highlight = true;
//    App::DocumentObject* tip = body->Tip.getValue();
//    for (std::vector<App::DocumentObject*>::const_iterator f = features.begin(); f != features.end(); f++) {
//        //Base::Console().Error("Highlighting %s: %s\n", (*f)->getNameInDocument(), highlight ? "true" : "false");
//        Gui::ViewProviderDocumentObject* vp = dynamic_cast<Gui::ViewProviderDocumentObject*>(Gui::Application::Instance->getViewProvider(*f));
//        if (vp != NULL)
//            ActiveGuiDoc->signalHighlightObject(*vp, Gui::LightBlue, active ? highlight : false);
//        if (highlight && (tip == *f))
//            highlight = false;
//    }
//}

bool ViewProviderBody::onDelete ( const std::vector<std::string> &) {
    // TODO May be do it conditionally? (2015-09-05, Fat-Zer)
    FCMD_OBJ_CMD(getObject(),"removeObjectsFromDocument()");
    return true;
}

unsigned long ViewProviderBody::generateIconColor(App::DocumentObject *feat) const
{
    auto body = Base::freecad_dynamic_cast<PartDesign::Body>(getObject());
    if (!body || (feat && !body->isSolidFeature(feat)))
        return 0;

    if (feat) {
        PartDesignGui::ViewProvider *vp = nullptr;
        unsigned long color = 0;
        for (auto o : body->getSiblings(feat)) {
            if (o == feat)
                break;
            vp = Base::freecad_dynamic_cast<PartDesignGui::ViewProvider>(
                    Gui::Application::Instance->getViewProvider(o));
        }
        if (vp) {
            color = vp->IconColor.getValue().getPackedValue();
            if (color)
                return color;
        }
    }

    std::set<unsigned long> colors;
    colors.insert(0x00ff00ff);
    colors.insert(0xff);
    colors.insert(0xffffffff);
    int solid_count = 0;
    for (auto o : body->Group.getValue()) {
        if (!body->isSolidFeature(o))
            continue;
        auto feat = Base::freecad_dynamic_cast<PartDesign::Feature>(o);
        if (!feat)
            continue;
        if (!feat->BaseFeature.getValue())
            ++solid_count;
        auto vp = Base::freecad_dynamic_cast<PartDesignGui::ViewProvider>(
                Gui::Application::Instance->getViewProvider(o));
        if (vp)
            colors.insert(vp->IconColor.getValue().getPackedValue() | 0xff);
    }
    if (colors.size()==3 && solid_count <= 1)
        return 0;
    std::vector<SbVec3f> hsvs;
    for (unsigned long color : colors) {
        SbColor c;
        float t;
        c.setPackedValue(color, t);
        hsvs.emplace_back();
        c.getHSVValue(hsvs.back()[0], hsvs.back()[1], hsvs.back()[2]);
    }
    float h, s, v;
    for(int retry=0; retry<100; ++retry) {
        h = ((float)rand())/(float(RAND_MAX));
        s = 1.0;
        v = ((float)rand())/(float(RAND_MAX)) * 0.5f + 0.25f;
        bool done = true;
        for (auto hsv : hsvs) {
            if(fabs(h-hsv[0]) + fabs(v-hsv[2]) < 0.2f) {
                done = false;
                break;
            }
        }
        if (done)
            break;
    }
    float t = 0.0f;
    SbColor c;
    c.setHSVValue(h, s, v);
    return c.getPackedValue(t);
}

bool ViewProviderBody::checkSiblings()
{
    // This function is to make sure the sibling self grouping only contain
    // previous siblings that are not already grouped.
    //
    // In addition, it makes sure any non solid feature do not mix with grouped
    // siblings.

    if (checkingSiblings || App::GetApplication().isRestoring())
        return true;

    auto body = Base::freecad_dynamic_cast<PartDesign::Body>(getObject());
    if (!body)
        return true;

    Base::StateLocker guard(checkingSiblings);

    bool touched = false;
    std::set<App::DocumentObject *> grouped;
    std::vector<App::DocumentObject *> children;
    bool groupTouched = false;
    auto group = body->Group.getValues();

    auto checkOrder = [&]() {
        if (children.size() <= 1)
            return;
        int i = -1, j = -1;
        body->Group.find(children.front()->getNameInDocument(), &j);
        body->Group.find(children.back()->getNameInDocument(), &i);
        for (int k=i; k<j; ++k) {
            auto obj = group[k];
            if (!obj->isDerivedFrom(PartDesign::Feature::getClassTypeId())) {
                group.erase(group.begin() + k);
                group.insert(group.begin() + i, obj);
                ++i;
                groupTouched = true;
            }
        }
    };

    if (body->AutoGroupSolids.getValue()) {
        for (auto obj : body->Group.getValues()) {
            auto feat = Base::freecad_dynamic_cast<PartDesign::Feature>(obj);
            if (!feat || grouped.count(feat))
                continue;
            auto siblings = body->getSiblings(feat, true, true);
            if (siblings.size() <= 1)
                continue;
            grouped.insert(siblings.begin(), siblings.end());
            auto lastFeat = Base::freecad_dynamic_cast<PartDesign::Feature>(siblings.front());
            if (!lastFeat)
                continue;
            siblings.pop_front();
            children.clear();
            for (auto sibling : siblings) {
                auto other = Base::freecad_dynamic_cast<PartDesign::Feature>(sibling);
                if (other) {
                    if (other->_Siblings.getSize()) {
                        touched = true;
                        other->_Siblings.setValues();
                    }
                    children.push_back(sibling);
                }
            }
            touched = touched || children != lastFeat->_Siblings.getValues();
            lastFeat->_Siblings.setValues(children);
            checkOrder();
        }
    } else {
        for (auto obj : body->Group.getValues()) {
            auto feat = Base::freecad_dynamic_cast<PartDesign::Feature>(obj);
            if (!feat || !feat->_Siblings.getSize() || grouped.count(feat))
                continue;

            auto siblings = body->getSiblings(feat, false);
            children.clear();
            children.reserve(siblings.size());
            for (auto sibling : siblings) {
                if (grouped.insert(sibling).second) {
                    auto other = Base::freecad_dynamic_cast<PartDesign::Feature>(sibling);
                    if (other && other->_Siblings.getSize() == 0)
                        children.insert(children.begin(), sibling);
                }
            }
            if (children.size() != siblings.size()) {
                touched = true;
                feat->_Siblings.setValues(children);
            }
            checkOrder();
        }
    }

    if (groupTouched) {
        body->Group.setValues(group);
        return true;
    }
    if (touched)
        buildExport();
    return touched;
}

void ViewProviderBody::updateData(const App::Property* prop)
{
    PartDesign::Body* body = static_cast<PartDesign::Body*>(getObject());
 
    if (prop == &body->Group) {
        setVisualBodyMode(true);
        checkSiblings();
    } else if (prop == &body->BaseFeature) {
        //ensure all model features are in visual body mode
        setVisualBodyMode(true);
    } else if (prop == &body->Tip) {
        // We changed Tip
        App::DocumentObject* tip = body->Tip.getValue();
        auto features = body->Group.getValues();

        // restore icons
        for (auto feature : features) {
            Gui::ViewProvider* vp = Gui::Application::Instance->getViewProvider(feature);
            if (vp && vp->isDerivedFrom(PartDesignGui::ViewProvider::getClassTypeId())) {
                static_cast<PartDesignGui::ViewProvider*>(vp)->setTipIcon(feature == tip);
            }
        }

        if (tip)
            copyColorsfromTip(tip);

    } else if (prop == &body->AutoGroupSolids) {
        checkSiblings();
    }

    PartGui::ViewProviderPart::updateData(prop);
}

void ViewProviderBody::copyColorsfromTip(App::DocumentObject* tip)
{
    // update DiffuseColor
    Gui::ViewProvider* vptip = Gui::Application::Instance->getViewProvider(tip);
    if (vptip && vptip->isDerivedFrom(PartGui::ViewProviderPartExt::getClassTypeId())) {
        auto colors = static_cast<PartGui::ViewProviderPartExt*>(vptip)->DiffuseColor.getValues();
        this->DiffuseColor.setValues(colors);
    }
}

void ViewProviderBody::onChanged(const App::Property* prop) {

    if (prop == &DisplayModeBody) {
        auto body = Base::freecad_dynamic_cast<PartDesign::Body>(getObject());

        if(pcModeSwitch->isOfType(SoFCSwitch::getClassTypeId())) {
            static_cast<SoFCSwitch*>(pcModeSwitch)->allowNamedOverride =
                (DisplayModeBody.getValue() != 0);
        }

        if ( DisplayModeBody.getValue() == 0 )  {
            //if we are in an override mode we need to make sure to come out, because
            //otherwise the maskmode is blocked and won't go into "through"
            if(getOverrideMode() != "As Is") {
                auto mode = getOverrideMode();
                ViewProvider::setOverrideMode("As Is");
                overrideMode = mode;
            }
            setDisplayMaskMode("Group");
            if(body)
                body->setShowTip(false);
        }
        else {
            if(body)
                body->setShowTip(true);
            if(getOverrideMode() == "As Is")
                setDisplayMaskMode(DisplayMode.getValueAsString());
            else {
                Base::Console().Message("Set override mode: %s\n", getOverrideMode().c_str());
                setDisplayMaskMode(getOverrideMode().c_str());
            }
        }

        // #0002559: Body becomes visible upon changing DisplayModeBody
        Visibility.touch();
    }
    else
        unifyVisualProperty(prop);

    PartGui::ViewProviderPartExt::onChanged(prop);
}


void ViewProviderBody::unifyVisualProperty(const App::Property* prop) {

    if (!pcObject || isRestoring()) {
        return;
    }

    if(prop == &Visibility ||
       prop == &Selectable ||
       prop == &DisplayModeBody ||
       prop == &MapFaceColor ||
       prop == &MapTransparency ||
       prop == &MapLineColor ||
       prop == &MapPointColor ||
       prop == &ForceMapColors ||
       prop == &MappedColors ||
       prop == &DiffuseColor ||
       prop == &PointColorArray ||
       prop == &LineColorArray) {
        return;
    }

    // Fixes issue 11197. In case of affected projects where the bounding box of a sub-feature
    // is shown allow it to hide it
    if (prop == &BoundingBox) {
        if (BoundingBox.getValue()) {
            return;
        }
    }

    Gui::Document *gdoc = Gui::Application::Instance->getDocument ( pcObject->getDocument() ) ;

    PartDesign::Body *body = static_cast<PartDesign::Body *> ( getObject() );
    // Experiementing element color mapping in PartDesign, enable visual copy on Tip only
#if 1
    auto feature = body->Tip.getValue();
    if (!feature)
        feature = body->BaseFeature.getValue();
    if(feature) {
#else
    auto features = body->Group.getValues();
    for(auto feature : features) {
        if(feature->isDerivedFrom<PartDesign::Feature>())
#endif
        {
            //copy over the properties data
            if (auto vp = Base::freecad_dynamic_cast<PartGui::ViewProviderPart>(
                        gdoc->getViewProvider(feature))) {
                auto p = vp->getPropertyByName(prop->getName());
                if(p && prop->isDerivedFrom(p->getTypeId())) {
                    if(prop==&ShapeColor) {
                        if(vp->MapFaceColor.getValue())
                            vp->MapFaceColor.setValue(false);
                    }else if(prop == &PointColor) {
                        if(vp->MapPointColor.getValue())
                            vp->MapPointColor.setValue(false);
                    }else if(prop == &LineColor) {
                        if(vp->MapLineColor.getValue())
                            vp->MapLineColor.setValue(false);
                    }else if(prop == &Transparency) {
                        if(vp->MapTransparency.getValue())
                            vp->MapTransparency.setValue(false);
                    }
                    p->Paste(*prop);
                }
            }
        }
    }
}

void ViewProviderBody::setVisualBodyMode(bool bodymode) {

    Gui::Document *gdoc = Gui::Application::Instance->getDocument ( pcObject->getDocument() ) ;

    PartDesign::Body *body = static_cast<PartDesign::Body *> ( getObject() );
    auto features = body->Group.getValues();
    for(auto feature : features) {

        if(!feature->isDerivedFrom(PartDesign::Feature::getClassTypeId()))
            continue;

        auto* vp = static_cast<PartDesignGui::ViewProvider*>(gdoc->getViewProvider(feature));
        if (vp) vp->setBodyMode(bodymode);
    }
}

std::vector< std::string > ViewProviderBody::getDisplayModes() const {

    //we get all display modes and remove the "Group" mode, as this is what we use for "Through"
    //body display mode
    std::vector< std::string > modes = ViewProviderPart::getDisplayModes();
    modes.erase(modes.begin());
    return modes;
}

bool ViewProviderBody::canDropObject(App::DocumentObject* obj) const
{
    PartDesign::Body* body = Base::freecad_dynamic_cast<PartDesign::Body>(getObject());
    if (!body || !obj)
        return false;

    // Selection context is set in TreeWidget::checkDropEvent/dropEvent().
    // Position 0 (default) stores the dragging object. We want to check
    // whether the parent of the dragging object is an aux group. More
    // specifically, we want to know if the user is dragging an object out of
    // an aux group and drop it on to the owner body, which indicates the user
    // wants to remove and object from the aux group.
    if (auto group = Base::freecad_dynamic_cast<PartDesign::AuxGroup>(
                Gui::Selection().getContext().getParent().getSubObject()))
    {
        return PartDesign::Body::findBodyOf(group) == body
            && group->getGroupType() == PartDesign::AuxGroup::OtherGroup;
    }

    if (PartDesign::Body::findBodyOf(obj) == body)
        return false;

    if (!body->getPrevSolidFeature()
            && !body->BaseFeature.getValue()
            && obj->isDerivedFrom(Part::Feature::getClassTypeId()))
        return true;

    if (!PartDesign::Body::isAllowed(obj))
        return false;

    // App::Part checking is too restrictive. It may mess up things, or it may
    // not. Just let user undo if anything is wrong.

    // App::Part *actPart = PartDesignGui::getActivePart();
    // App::Part* partOfBaseFeature = App::Part::getPartOfObject(obj);
    // if (partOfBaseFeature != 0 && partOfBaseFeature != actPart)
    //     return false;

    return true;
}

bool ViewProviderBody::canDragAndDropObject(App::DocumentObject * obj) const
{
    PartDesign::Body* body = Base::freecad_dynamic_cast<PartDesign::Body>(getObject());
    if (!body)
        return true;

    // Check if the parent of the dragging object is an aux group
    if (auto group = Base::freecad_dynamic_cast<PartDesign::AuxGroup>(
                Gui::Selection().getContext().getParent().getSubObject()))
    {
        if (PartDesign::Body::findBodyOf(group) == body
                && group->getGroupType() == PartDesign::AuxGroup::OtherGroup)
            return true;
    }

    auto type = obj->getTypeId();
    if (type.isDerivedFrom(Part::Datum::getClassTypeId())   ||
        type.isDerivedFrom(Part::Part2DObject::getClassTypeId()) ||
        type.isDerivedFrom(PartDesign::ShapeBinder::getClassTypeId()) ||
        type.isDerivedFrom(Part::SubShapeBinder::getClassTypeId()))
        return true;
    
    if (!body->getPrevSolidFeature()
             && !body->BaseFeature.getValue()
             && obj->isDerivedFrom(Part::Feature::getClassTypeId())
             && !obj->isDerivedFrom(PartDesign::Feature::getClassTypeId()))
        return false;
    return true;
}

bool ViewProviderBody::canDragObject(App::DocumentObject *obj) const
{
    if (obj->isDerivedFrom(PartDesign::AuxGroup::getClassTypeId()))
        return false;
    PartDesign::Body* body = Base::freecad_dynamic_cast<PartDesign::Body>(getObject());
    if (!body)
        return true;

    // Selection context is set in TreeWidget::checkDropEvent/dropEvent().
    // position 1 stores full object path to the dropping target.
    // position 0 (default) stores the dragging source, which points to this object.
    auto target = Gui::Selection().getContext(1).getSubObject();
    if (PartDesign::Body::findBodyOf(target) == body)
        return false;

    if (body->BaseFeature.getValue() == obj)
        return false;

    return PartDesignGui::isFeatureMovable(obj);
}

std::string ViewProviderBody::dropObjectEx(App::DocumentObject *obj,
                                           App::DocumentObject *owner, 
                                           const char *subname,
                                           const std::vector<std::string> &elements)
{
    PartDesign::Body* body = Base::freecad_dynamic_cast<PartDesign::Body>(getObject());
    if (!body)
        FC_THROWM(Base::RuntimeError, "No body");

    if (!owner)
        owner = obj;

    auto type = obj->getTypeId();
    if (type.isDerivedFrom(Part::Datum::getClassTypeId())   ||
        type.isDerivedFrom(Part::Part2DObject::getClassTypeId()) ||
        type.isDerivedFrom(PartDesign::ShapeBinder::getClassTypeId()) ||
        type.isDerivedFrom(Part::SubShapeBinder::getClassTypeId()))
    {
        body->addObject(obj);
    }
    else if (PartDesign::Body::isAllowed(obj) && PartDesignGui::isFeatureMovable(obj)) {
        std::vector<App::DocumentObject*> move;
        move.push_back(obj);
        std::vector<App::DocumentObject*> deps = PartDesignGui::collectMovableDependencies(move);
        move.insert(std::end(move), std::begin(deps), std::end(deps));

        PartDesign::Body* source = PartDesign::Body::findBodyOf(obj);
        if (source == body) {
            // To allow object to be dragged out of an aux group, we do not
            // consider it as an error to add an object already in the body.
            //
            // FC_THROWM(Base::RuntimeError, "Feature already inside the body");
        }
        else {
            if(source)
                source->removeObjects(move);
            body->addObjects(move);
        }
    } else {
        bool canWrap = true;
        bool hasDep = false;
        auto deps = obj->getOutList();
        for (auto dep : deps) {
            if (PartDesign::Body::findBodyOf(dep) == body) {
                if (dep->isDerivedFrom(PartDesign::FeatureWrap::getClassTypeId()))
                    FC_THROWM(Base::RuntimeError, "Feature has already been wrapped inside the body");
                hasDep = true;
            } else
                canWrap = false;
        }
        if (hasDep && canWrap) {
            auto wrap = static_cast<PartDesign::FeatureWrap*>(
                    body->newObjectAt("PartDesign::FeatureWrap", "Wrap", deps, false));
            wrap->Label.setValue(obj->Label.getValue());
            wrap->WrapFeature.setValue(obj);
            obj = wrap;
        }
        else if (elements.size() || hasDep) {
            auto binder = static_cast<PartDesign::SubShapeBinder*>(
                    body->getDocument()->addObject("PartDesign::SubShapeBinder", "Binder"));
            std::map<App::DocumentObject *, std::vector<std::string> > links;
            auto & subs = links[owner];
            std::string sub(subname ? subname : "");
            for (auto & element : elements)
                subs.emplace_back(sub + element);
            binder->setLinks(std::move(links));
            body->addObject(binder);
            obj = binder;
        }
        else if (!body->getPrevSolidFeature() && !body->BaseFeature.getValue()) {
            if (owner == obj) {
                body->BaseFeature.setValue(obj);
                auto tip = body->Tip.getValue();
                if (tip)
                    return std::string(tip->getNameInDocument()) + ".";
                return std::string();
            }

            auto binder = static_cast<PartDesign::SubShapeBinder*>(
                    body->getDocument()->addObject("PartDesign::SubShapeBinder",
                                                    "BaseFeature"));
            std::map<App::DocumentObject *, std::vector<std::string> > links;
            auto & subs = links[owner];
            if (subname && subname[0])
                subs.emplace_back(subname);
            binder->setLinks(std::move(links));
            auto children = body->Group.getValues();
            children.insert(children.begin(), binder);
            body->Group.setValues(children);
            body->BaseFeature.setValue(binder);
            obj = binder;
        }
    }
    return std::string(obj->getNameInDocument()) + ".";
}

bool ViewProviderBody::canReplaceObject(App::DocumentObject *oldObj,
                                        App::DocumentObject *newObj)
{
    auto body = Base::freecad_dynamic_cast<PartDesign::Body>(getObject());
    if (!body || !oldObj || !newObj || oldObj == newObj
            || oldObj == body->BaseFeature.getValue()
            || newObj == body->BaseFeature.getValue())
        return false;

    // Selection context is set in TreeWidget::checkDropEvent/dropEvent().
    // position 1 stores full object path to the dropping target. Here, it is the newObj.
    // position 0 (default) stores the dragging source, which is the oldObj.
    auto newObjParent = Gui::Selection().getContext(1).getParent().getSubObject(); 
    auto oldObjParent = Gui::Selection().getContext(0).getParent().getSubObject(); 
    if (newObjParent && oldObjParent
            && newObjParent != oldObjParent
            && (newObjParent->isDerivedFrom(PartDesign::AuxGroup::getClassTypeId())
                || oldObjParent->isDerivedFrom(PartDesign::AuxGroup::getClassTypeId())))
        return false;

    if (!body->Group.find(oldObj->getNameInDocument())
            || !body->Group.find(newObj->getNameInDocument()))
        return false;

    if (!newObj->isDerivedFrom(PartDesign::Feature::getClassTypeId()))
        return true;

    return body->isSibling(oldObj, newObj);
}

int ViewProviderBody::replaceObject(App::DocumentObject *oldObj, App::DocumentObject *newObj)
{
    auto body = Base::freecad_dynamic_cast<PartDesign::Body>(getObject());
    if (!body || !canReplaceObject(oldObj, newObj))
        return 0;
    App::AutoTransaction committer("Reorder body feature");
    bool needCheckSiblings = false;
    if (!_reorderObject(body, newObj, oldObj, true, needCheckSiblings))
        return 0;
    if (needCheckSiblings) {
        checkingSiblings = false;
        if (!checkSiblings())
            buildExport();
    }
    Gui::Command::updateActive();
    return 1;
}

bool ViewProviderBody::reorderObjects(const std::vector<App::DocumentObject *> &objs,
                                      App::DocumentObject *before)
{
    auto body = Base::freecad_dynamic_cast<PartDesign::Body>(getObject());
    if (!body)
        return false;

    App::DocumentObject *last = before;
    for (auto obj : objs) {
        if (!canReorderObject(obj, last))
            return false;
        last = obj;
    }

    App::AutoTransaction committer("Reorder body feature");
    bool needCheckSiblings = false;

    last = before;
    for (auto obj : objs) {
        if (!_reorderObject(body, obj, last, false, needCheckSiblings))
            return false;
        last = obj;
    }
    if (needCheckSiblings) {
        checkingSiblings = false;
        if (!checkSiblings())
            buildExport();
    }
    Gui::Command::updateActive();
    return 1;
}

bool ViewProviderBody::_reorderObject(PartDesign::Body *body,
                                      App::DocumentObject *obj,
                                      App::DocumentObject *before,
                                      bool canSwap,
                                      bool &needCheckSiblings)
{
    auto oldObj = before;
    auto newObj = obj;

    int i, j;
    if (!body->Group.find(oldObj->getNameInDocument(), &i)
            || !body->Group.find(newObj->getNameInDocument(), &j))
        return false;

    if (i-1 == j)
        return true;

    auto secondFeat = Base::freecad_dynamic_cast<PartDesign::Feature>(oldObj);
    auto firstFeat = Base::freecad_dynamic_cast<PartDesign::Feature>(newObj);

    // In case the old object has self sibling group, repoint the old object to
    // the earliest sibling
    if (i > j && secondFeat && secondFeat->_Siblings.getSize()) {
        const auto & siblings = secondFeat->_Siblings.getValues();
        for (auto rit=siblings.rbegin(); rit!=siblings.rend(); ++rit) {
            auto feat = Base::freecad_dynamic_cast<PartDesign::Feature>(*rit);
            if (feat) {
                if (feat != newObj && body->Group.find(feat->getNameInDocument(), &i))
                    secondFeat = feat;
                break;
            }
        }
    }

    if (auto next = Base::freecad_dynamic_cast<PartDesign::Feature>(body->getNextSolidFeature(firstFeat))) {
        if (next != secondFeat && next->BaseFeature.getValue() == firstFeat)
            next->BaseFeature.setValue(firstFeat->BaseFeature.getValue());
    }

    // This function is used by both reorderObjects() and replaceObject().
    // 'first', 'second' here refers to the order after replaceObject()
    // operation
    if (canSwap && i > j)
        std::swap(secondFeat, firstFeat);

    auto objs = body->Group.getValues();
    objs.erase(objs.begin() + j);
    if (!canSwap && j < i)
        --i;
    objs.insert(objs.begin() + i, newObj);

    Base::StateLocker guard(checkingSiblings); // delay sibling check
    body->Group.setValues(objs);

    if (firstFeat && secondFeat) {
        Base::ObjectStatusLocker<App::Property::Status,App::Property>
            guard1(App::Property::User3, &firstFeat->BaseFeature);
        Base::ObjectStatusLocker<App::Property::Status,App::Property>
            guard2(App::Property::User3, &secondFeat->BaseFeature);

        // Must change BaseFeature before NewSolid, because changing NewSolid
        // will auto adjust BaseFeature
        firstFeat->BaseFeature.setValue(secondFeat->BaseFeature.getValue());
        secondFeat->BaseFeature.setValue(firstFeat);
        if (secondFeat->NewSolid.getValue()) {
            firstFeat->NewSolid.setValue(true);
            secondFeat->NewSolid.setValue(false);
        }
        for (auto obj : objs) {
            if (obj == secondFeat
                    || !body->isSolidFeature(obj)
                    || !obj->isDerivedFrom(PartDesign::Feature::getClassTypeId()))
                continue;
            auto feat = static_cast<PartDesign::Feature*>(obj);
            if (feat->BaseFeature.getValue() == firstFeat)
                feat->BaseFeature.setValue(secondFeat);
        }

        if (body->Tip.getValue() == firstFeat)
            body->setTip(secondFeat);

        needCheckSiblings = true;
    }
    return true;
}

bool ViewProviderBody::canReorderObject(App::DocumentObject* obj, App::DocumentObject* before)
{
    return canReplaceObject(before, obj);
}

std::vector<App::DocumentObject*> ViewProviderBody::claimChildren3D(void) const {
    auto children = inherited::claimChildren3D();
    for(auto it=children.begin();it!=children.end();) {
        auto obj = *it;
        if(obj->isDerivedFrom(PartDesign::Solid::getClassTypeId()))
            it = children.erase(it);
        else
            ++it;
    }
    return children;
}

void ViewProviderBody::groupSiblings(PartDesign::Feature *feat, bool collapse, bool all)
{
    auto body = Base::freecad_dynamic_cast<PartDesign::Body>(getObject());
    if (!body)
        return;

    std::string cmd(collapse ? "Collapse" : "Expand");
    cmd += " body solid";
    App::AutoTransaction committer(cmd.c_str());
    if (all) {
        auto siblings = body->getSiblings(feat, true, true);
        PartDesign::Feature *feat = nullptr;
        while (siblings.size()) {
            feat = Base::freecad_dynamic_cast<PartDesign::Feature>(siblings.front());
            siblings.pop_front();
            if (feat)
                break;
        }
        if (!feat || siblings.empty())
            return;
        if (collapse) {
            std::vector<App::DocumentObject*> objs(siblings.begin(), siblings.end());
            feat->_Siblings.setValues(objs);
        } else
            feat->_Siblings.setValues();
        for (auto obj : siblings) {
            auto feat = Base::freecad_dynamic_cast<PartDesign::Feature>(obj);
            if (feat)
                feat->_Siblings.setValues();
        }
        if (!collapse || !checkSiblings())
            buildExport();
        return;
    }

    if (!collapse) {
        feat->_Siblings.setValues();
        buildExport();
        return;
    }

    auto siblings = body->getSiblings(feat, false, true);

    std::vector<App::DocumentObject*> objs;
    objs.reserve(feat->_Siblings.getSize() + siblings.size());

    objs = feat->_Siblings.getValues();
    std::set<App::DocumentObject*> existing(objs.begin(), objs.end());

    bool first = true;
    for (auto obj : siblings) {
        if (existing.count(obj))
            continue;
        auto feat = Base::freecad_dynamic_cast<PartDesign::Feature>(obj);
        if (!feat || !feat->_Siblings.getSize()) {
            if (feat)
                first = false;
            objs.push_back(obj);
            continue;
        }
        if (first) {
            // expand the first sibling that has its own group, and join its
            // group to this feature. 
            first = false;
            objs.push_back(obj);
            for (auto o : feat->_Siblings.getValues())
                objs.push_back(o);
            feat->_Siblings.setValues();
            break;
        }
    }
    feat->_Siblings.setValues(objs);
    if (!checkSiblings())
        buildExport();
}

bool ViewProviderBody::shouldCheckExport(App::DocumentObject *) const
{
    return true;
}

std::map<std::string,App::Color> ViewProviderBody::getElementColors(const char *element) const
{
    auto body = Base::freecad_dynamic_cast<PartDesign::Body>(getObject());
    if (body && body->getSubObjects().size())
        return {};
    return inherited::getElementColors(element);
}
