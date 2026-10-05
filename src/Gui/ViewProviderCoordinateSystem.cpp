/***************************************************************************
 *   Copyright (c) 2015 Stefan Tröger <stefantroeger@gmx.net>              *
 *   Copyright (c) 2015 Alexander Golubev (Fat-Zer) <fatzer2@gmail.com>    *
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
# include <QMenu>
# include <QAction>
# include <Inventor/nodes/SoGroup.h>
# include <Inventor/nodes/SoSwitch.h>
# include <Inventor/nodes/SoLightModel.h>
# include <Inventor/nodes/SoSeparator.h>
#endif

#include <App/Document.h>
#include <App/DocumentObserver.h>
#include <App/Origin.h>
#include <App/OriginGroupExtension.h>
#include "Base/Console.h"
#include <Base/Vector3D.h>

#include "ViewProviderCoordinateSystem.h"
#include "Application.h"
#include "Command.h"
#include "Document.h"
#include "SoFCUnifiedSelection.h"
#include "ViewParams.h"
#include "ViewProviderLine.h"
#include "ViewProviderPlane.h"

using namespace Gui;


PROPERTY_SOURCE(Gui::ViewProviderCoordinateSystem, Gui::ViewProviderDocumentObject)

/**
 * Creates the view provider for an object group.
 */
ViewProviderCoordinateSystem::ViewProviderCoordinateSystem()
{
    auto sz = defaultSize();
    ADD_PROPERTY_TYPE ( Size, (Base::Vector3d(sz,sz,sz)), 0, App::Prop_None,
        QT_TRANSLATE_NOOP("App::Property", "The displayed size of the origin"));
    Size.setStatus(App::Property::ReadOnly, true);

    ADD_PROPERTY_TYPE ( Margin, (Base::Vector3d(2,2,2)), 0, App::Prop_None,
        QT_TRANSLATE_NOOP("App::Property", "Margin to be added to the calculated size of the origin"));

    sPixmap = "Std_CoordinateSystem";
    Visibility.setValue(false);

    // Do not override visibility of origin group
    if(pcModeSwitch->isOfType(SoFCSwitch::getClassTypeId())) 
        static_cast<SoFCSwitch*>(pcModeSwitch)->defaultChild = -1;

    pcGroupChildren = new SoGroup();
    pcGroupChildren->ref();

    auto lm = new SoLightModel();
    lm->model = SoLightModel::BASE_COLOR;
    pcRoot->insertChild(lm, 0);
}

ViewProviderCoordinateSystem::~ViewProviderCoordinateSystem() {
    pcGroupChildren->unref();
    pcGroupChildren = nullptr;
}

std::vector<App::DocumentObject*> ViewProviderCoordinateSystem::claimChildren() const {
    return static_cast<App::Origin*>( getObject() )->OriginFeatures.getValues ();
}

std::vector<App::DocumentObject*> ViewProviderCoordinateSystem::claimChildren3D() const {
    return claimChildren ();
}

void ViewProviderCoordinateSystem::attach(App::DocumentObject* pcObject)
{
    Gui::ViewProviderDocumentObject::attach(pcObject);
    addDisplayMaskMode(pcGroupChildren, "Base");
}

std::vector<std::string> ViewProviderCoordinateSystem::getDisplayModes() const
{
    return { "Base" };
}

void ViewProviderCoordinateSystem::setDisplayMode(const char* ModeName)
{
    if (strcmp(ModeName, "Base") == 0)
        setDisplayMaskMode("Base");
    ViewProviderDocumentObject::setDisplayMode(ModeName);
}

namespace {
/// Who holds the origin's entries in an edit's views
/// (Gui::Document::setEditVisibility).
const char *TemporaryVisibilityOwner = "Origin.Temporary";

/// The origin as the edit under way reaches it: under the occurrence of
/// its container that the edit goes through. An object with no subname
/// when it does not go through one -- an origin shown for an edit of
/// something else -- which is the origin wherever it is drawn.
App::SubObjectT originInEdit(Gui::Document *gdoc, App::DocumentObject *origin)
{
    ViewProviderDocumentObject *parentVp = nullptr;
    std::string subname;
    gdoc->getInEdit(&parentVp, &subname);
    if (parentVp && parentVp->getObject()) {
        App::SubObjectT objT(parentVp->getObject(), subname.c_str());
        for (; !objT.getObjectName().empty(); objT = objT.getParent()) {
            auto sobj = objT.getSubObject();
            auto linked = sobj ? sobj->getLinkedObject(true) : nullptr;
            auto group = linked
                ? linked->getExtensionByType<App::OriginGroupExtension>(true) : nullptr;
            if (group && group->Origin.getValue() == origin)
                return objT.getChild(origin);
        }
    }
    return App::SubObjectT(origin, "");
}
}

bool ViewProviderCoordinateSystem::setTemporaryVisibilityInEdit(bool axis, bool plane)
{
    auto origin = static_cast<App::Origin*>(getObject());
    auto gdoc = Application::Instance->editDocument();
    if (!origin || !gdoc || !gdoc->canSetEditVisibility())
        return false;

    const App::SubObjectT originT = originInEdit(gdoc, origin);
    const bool rooted = !originT.getSubName().empty();
    std::vector<App::SubObjectT> entries;
    auto entry = [&](App::DocumentObject *obj, bool visible) {
        if (!obj)
            return true;
        App::SubObjectT objT = obj == origin ? originT
            : (rooted ? originT.getChild(obj) : App::SubObjectT(obj, ""));
        if (!gdoc->setEditVisibility(TemporaryVisibilityOwner, objT.getObject(),
                                     objT.getSubName().c_str(), visible ? 1 : 0))
            return false;
        entries.push_back(std::move(objT));
        return true;
    };
    bool done = true;
    try {
        for (auto obj : origin->axes())
            done = done && entry(obj, axis);
        for (auto obj : origin->planes())
            done = done && entry(obj, plane);
    } catch (const Base::Exception &ex) {
        Base::Console().Error ("%s\n", ex.what() );
    }
    done = done && entry(origin, true);
    if (!done) {
        // All or nothing: Visibility is written instead
        for (const auto &objT : entries)
            gdoc->setEditVisibility(TemporaryVisibilityOwner, objT.getObject(),
                                    objT.getSubName().c_str(), -1);
        return false;
    }
    tempEditEntries = std::move(entries);
    tempEditDocument = gdoc->getDocument()->getName();
    return true;
}

void ViewProviderCoordinateSystem::setTemporaryVisibility(bool axis, bool plane) {
    auto origin = static_cast<App::Origin*>( getObject() );

    // In the views of the edit, where they can take it; not once
    // Visibility has been written for this origin, which then stays the
    // one way until it is reset.
    if (tempVisMap.empty() && setTemporaryVisibilityInEdit(axis, plane))
        return;

    bool saveState = tempVisMap.empty();

    try {
        // Remember & Set axis visibility
        for(App::DocumentObject* obj : origin->axes()) {
            if (obj) {
                Gui::ViewProvider* vp = Gui::Application::Instance->getViewProvider(obj);
                if(vp) {
                    if (saveState) {
                        tempVisMap.emplace(obj, vp->isVisible());
                    }
                    vp->setVisible(axis);
                }
            }
        }

        // Remember & Set plane visibility
        for(App::DocumentObject* obj : origin->planes()) {
            if (obj) {
                Gui::ViewProvider* vp = Gui::Application::Instance->getViewProvider(obj);
                if(vp) {
                    if (saveState) {
                        tempVisMap.emplace(obj, vp->isVisible());
                    }
                    vp->setVisible(plane);
                }
            }
        }
    } catch (const Base::Exception &ex) {
        Base::Console().Error ("%s\n", ex.what() );
    }

    // Remember & Set self visibility
    tempVisMap.emplace(getObject(), isVisible());
    setVisible(true);

}

void ViewProviderCoordinateSystem::resetTemporaryVisibility() {
    if (!tempEditEntries.empty()) {
        // Taken back by name: the edit may have ended already, and taken
        // the entries with it.
        Gui::Document *gdoc = nullptr;
        if (auto doc = App::GetApplication().getDocument(tempEditDocument.c_str()))
            gdoc = Application::Instance->getDocument(doc);
        for (const auto &objT : tempEditEntries) {
            auto obj = objT.getObject();
            if (gdoc && obj)
                gdoc->setEditVisibility(TemporaryVisibilityOwner, obj,
                                        objT.getSubName().c_str(), -1);
        }
        tempEditEntries.clear();
        tempEditDocument.clear();
    }
    for(const auto &pair : tempVisMap) {
        if (auto vp = Gui::Application::Instance->getViewProvider(pair.first))
            vp->setVisible(pair.second);
    }
    tempVisMap.clear ();
}

void ViewProviderCoordinateSystem::setTemporaryScale(double factor)
{
    for (auto obj : static_cast<App::LocalCoordinateSystem*>(getObject())->OriginFeatures.getValues()) {
        if (auto vp = Base::freecad_dynamic_cast<ViewProviderDatum>(
                    Gui::Application::Instance->getViewProvider(obj)))
            vp->setTemporaryScale(factor);
    }
}

void ViewProviderCoordinateSystem::resetTemporarySize()
{
    setTemporaryScale(1.0);
}

void ViewProviderCoordinateSystem::setPlaneLabelVisibility(bool visible)
{
    for (auto obj : static_cast<App::LocalCoordinateSystem*>(getObject())->OriginFeatures.getValues()) {
        if (auto vp = Base::freecad_dynamic_cast<ViewProviderPlane>(
                    Gui::Application::Instance->getViewProvider(obj)))
            vp->setLabelVisibility(visible);
    }
}

double ViewProviderCoordinateSystem::defaultSize()
{
    return 0.25 * ViewParams::getNewDocumentCameraScale();
}

double ViewProviderCoordinateSystem::baseSize()
{
    return 10;
}

bool ViewProviderCoordinateSystem::isTemporaryVisibility() {
    return !tempVisMap.empty() || !tempEditEntries.empty();
}

void ViewProviderCoordinateSystem::updateData(const App::Property *prop) {
    App::Origin* origin = static_cast<App::Origin*> ( getObject() );
    if(origin) {
        if(prop == &origin->OriginFeatures && origin->OriginFeatures.getSize()
                && !origin->getDocument()->isPerformingTransaction()
                && !origin->testStatus(App::ObjectStatus::Remove))
        {
            Size.touch();
        }
    }
    ViewProviderDocumentObject::updateData ( prop );
}

bool ViewProviderCoordinateSystem::doubleClicked() {
    App::Origin* origin = static_cast<App::Origin*> ( getObject() );
    if(origin)
        origin->initObjects();
    return true;
}

void ViewProviderCoordinateSystem::setupContextMenu(QMenu* menu, QObject* receiver, const char* member)
{
    App::Origin* origin = static_cast<App::Origin*> ( getObject() );
    if(origin && !origin->OriginFeatures.getSize()) {
        QAction* act = menu->addAction(QObject::tr("Create origin features"), receiver, member);
        act->setData(QVariant((int)ViewProvider::Default));
    }
}

bool ViewProviderCoordinateSystem::setEdit(int ModNum)
{
    if(ModNum == ViewProvider::Default)
        return doubleClicked();
    return false;
}

void ViewProviderCoordinateSystem::onChanged(const App::Property* prop) {
    App::Origin* origin = static_cast<App::Origin*> ( getObject() );
    if(!origin) {
        ViewProviderDocumentObject::onChanged ( prop );
        return;
    }

    if ((prop==&Size || prop==&Margin) && origin->OriginFeatures.getSize()) {
        try {
            Gui::Application *app = Gui::Application::Instance;
            Base::Vector3d sz = Size.getValue() + Margin.getValue();

            // Calculate axes and planes sizes
            double szXY = std::max ( sz.x, sz.y );
            double szXZ = std::max ( sz.x, sz.z );
            double szYZ = std::max ( sz.y, sz.z );

            double szX = std::min ( szXY, szXZ );
            double szY = std::min ( szXY, szYZ );
            double szZ = std::min ( szXZ, szYZ );

            // Find view providers
            Gui::ViewProviderPlane* vpPlaneXY, *vpPlaneXZ, *vpPlaneYZ;
            Gui::ViewProviderLine* vpLineX, *vpLineY, *vpLineZ;
            // Planes
            vpPlaneXY = static_cast<Gui::ViewProviderPlane *> ( app->getViewProvider ( origin->getXY () ) );
            vpPlaneXZ = static_cast<Gui::ViewProviderPlane *> ( app->getViewProvider ( origin->getXZ () ) );
            vpPlaneYZ = static_cast<Gui::ViewProviderPlane *> ( app->getViewProvider ( origin->getYZ () ) );
            // Axes
            vpLineX = static_cast<Gui::ViewProviderLine *> ( app->getViewProvider ( origin->getX () ) );
            vpLineY = static_cast<Gui::ViewProviderLine *> ( app->getViewProvider ( origin->getY () ) );
            vpLineZ = static_cast<Gui::ViewProviderLine *> ( app->getViewProvider ( origin->getZ () ) );

            // set their sizes
            if (vpPlaneXY) { vpPlaneXY->Size.setValue ( szXY ); }
            if (vpPlaneXZ) { vpPlaneXZ->Size.setValue ( szXZ ); }
            if (vpPlaneYZ) { vpPlaneYZ->Size.setValue ( szYZ ); }
            if (vpLineX) { vpLineX->Size.setValue ( szX * axesScaling ); }
            if (vpLineY) { vpLineY->Size.setValue ( szY * axesScaling ); }
            if (vpLineZ) { vpLineZ->Size.setValue ( szZ * axesScaling ); }

        } catch (const Base::Exception &ex) {
            // While restoring a document don't report errors if one of the lines or planes
            // cannot be found.
            App::Document* doc = getObject()->getDocument();
            if (!doc->testStatus(App::Document::Restoring))
                Base::Console().Error ("%s\n", ex.what() );
        }
    } else if (prop == &Visibility) {
        if(Visibility.getValue())
            origin->initObjects();
    }

    ViewProviderDocumentObject::onChanged ( prop );
}

bool ViewProviderCoordinateSystem::onDelete(const std::vector<std::string> &) {
    auto origin = static_cast<App::Origin*>( getObject() );

    if ( !origin->getInList().empty() ) {
        return false;
    }

    auto objs = origin->OriginFeatures.getValues();
    origin->OriginFeatures.setValues({});

    for (auto obj: objs ) {
        Gui::Command::doCommand( Gui::Command::Doc, "App.getDocument(\"%s\").removeObject(\"%s\")",
                obj->getDocument()->getName(), obj->getNameInDocument() );
    }

    return true;
}
