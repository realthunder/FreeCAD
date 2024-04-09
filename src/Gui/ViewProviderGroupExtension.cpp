/***************************************************************************
 *   Copyright (c) 2016 Stefan Tröger <stefantroeger@gmx.net>              *
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
# include <QMouseEvent>
# include <QMessageBox>
#endif

#include <App/Document.h>
#include <App/DocumentObject.h>
#include <App/GroupExtension.h>
#include <App/GeoFeatureGroupExtension.h>
#include <Base/Console.h>
#include <Base/Tools.h>

#include "ViewProviderGroupExtension.h"
#include "ViewProviderDocumentObject.h"
#include "Command.h"
#include "Document.h"
#include "MainWindow.h"


using namespace Gui;

EXTENSION_PROPERTY_SOURCE(Gui::ViewProviderGroupExtension, Gui::ViewProviderExtension)

ViewProviderGroupExtension::ViewProviderGroupExtension()
{
    initExtensionType(ViewProviderGroupExtension::getExtensionClassTypeId());
}

ViewProviderGroupExtension::~ViewProviderGroupExtension() = default;

bool ViewProviderGroupExtension::extensionCanDragObjects() const {
    return true;
}

bool ViewProviderGroupExtension::extensionCanDragObject(App::DocumentObject*) const {

    //we can drag anything out
    return true;
}

void ViewProviderGroupExtension::extensionAttach(App::DocumentObject* pcObject)
{
    if(App::GeoFeatureGroupExtension::isNonGeoGroup(pcObject))
        getExtendedViewProvider()->SelectionStyle.setValue(1);
}

void ViewProviderGroupExtension::extensionDragObject(App::DocumentObject* obj) {

    Gui::Command::doCommand(Gui::Command::Doc,"App.getDocument(\"%s\").getObject(\"%s\").removeObject("
            "App.getDocument(\"%s\").getObject(\"%s\"))",
            getExtendedViewProvider()->getObject()->getDocument()->getName(), getExtendedViewProvider()->getObject()->getNameInDocument(),
            obj->getDocument()->getName(), obj->getNameInDocument() );
}

bool ViewProviderGroupExtension::extensionCanDropObjects() const {
    return true;
}

bool ViewProviderGroupExtension::extensionCanDropObject(App::DocumentObject* obj) const {

    auto* group = getExtendedViewProvider()->getObject()->getExtensionByType<App::GroupExtension>();

    //we cannot drop thing of this group into it again
    if (group->hasObject(obj))
        return false;

    if (group->allowObject(obj))
        return true;

    return false;
}

void ViewProviderGroupExtension::extensionDropObject(App::DocumentObject* obj) {

    auto grp = static_cast<App::DocumentObject*>(getExtendedViewProvider()->getObject());
    App::Document* doc = grp->getDocument();

    // build Python command for execution
    QString cmd;
    cmd = QStringLiteral("App.getDocument(\"%1\").getObject(\"%2\").addObject("
                        "App.getDocument(\"%1\").getObject(\"%3\"))")
                        .arg(QString::fromUtf8(doc->getName()),
                             QString::fromUtf8(grp->getNameInDocument()),
                             QString::fromUtf8(obj->getNameInDocument()));

    Gui::Command::doCommand(Gui::Command::App, cmd.toUtf8());
}

void ViewProviderGroupExtension::extensionUpdateData(const App::Property *prop)
{
    if (auto obj = static_cast<App::DocumentObject*>(getExtendedViewProvider()->getObject())) {
        if (!obj->getDocument()->testStatus(App::Document::Restoring)) {
            if (auto ext = obj->getExtensionByType<App::GroupExtension>(true)) {
                if (prop == &ext->ExportMode) {
                    for (auto obj : ext->Group.getValues()) {
                        auto vp = Application::Instance->getViewProvider(obj);
                        if (vp)
                            vp->signalChangeIcon();
                    }
                } else if (prop == &ext->_GroupTouched || prop == &ext->Group)
                    buildExport();
            }
        }
    }
    ViewProviderExtension::extensionUpdateData ( prop );
}

void ViewProviderGroupExtension::extensionClaimChildren(
        std::vector<App::DocumentObject *> &children) const 
{
    auto* group = getExtendedViewProvider()->getObject()->getExtensionByType<App::GroupExtension>();
    auto & prop = group->ClaimAllChildren.getValue() ? group->Group : group->_ExportChildren;
    auto objs = prop.getValues();
    children.insert(children.end(), objs.begin(), objs.end());
}

void ViewProviderGroupExtension::extensionFinishRestoring()
{
    auto* group = getExtendedViewProvider()->getObject()->getExtensionByType<App::GroupExtension>();
    if (!group->ClaimAllChildren.getValue() && !group->_ExportChildren.getSize())
        buildExport();
    ViewProviderExtension::extensionFinishRestoring();
}

static void filterLinksByScope(const App::DocumentObject *obj, std::vector<App::DocumentObject *> &children)
{
    if(!obj || !obj->getNameInDocument())
        return;

    std::vector<App::Property*> list;
    obj->getPropertyList(list);
    std::set<App::DocumentObject *> links;
    for(auto prop : list) {
        auto link = Base::freecad_dynamic_cast<App::PropertyLinkBase>(prop);
        if(link && link->getScope()!=App::LinkScope::Global)
            link->getLinkedObjects(std::inserter(links,links.end()),true);
    }
    for(auto it=children.begin();it!=children.end();) {
        if(!links.count(*it))
            it = children.erase(it);
        else
            ++it;
    }
}

void ViewProviderGroupExtension::buildExport() const {
    App::DocumentObject * obj = getExtendedViewProvider()->getObject();
    auto* group = obj->getExtensionByType<App::GroupExtension>();
    if(!group)
        return;

    if (group->ClaimAllChildren.getValue()) {
        group->_ExportChildren.setValues({});
        return;
    }

    if(obj->testStatus(App::PendingRecompute) && !obj->isRecomputing())
        return;

    auto model = group->Group.getValues ();
    std::set<App::DocumentObject*> outSet; //< set of objects not to claim (childrens of childrens)

    // search for objects handled (claimed) by the features
    for (auto obj: model) {
        if (!obj || !shouldCheckExport(obj)) { continue; }

        Gui::ViewProvider* vp = Gui::Application::Instance->getViewProvider ( obj );
        if (!vp || vp == getExtendedViewProvider()) { continue; }

        auto children = vp->claimChildren();
        filterLinksByScope(obj,children);
        outSet.insert(children.begin(),children.end());
    }

    // remove the otherwise handled objects, preserving their order so the order in the TreeWidget is correct
    for(auto it=model.begin();it!=model.end();) {
        auto obj = *it;
        if(!obj || !obj->getNameInDocument() || outSet.count(obj))
            it = model.erase(it);
        else
            ++it;
    }

    if(group->_ExportChildren.getValues()!=model)
        group->_ExportChildren.setValues(std::move(model));
}

bool ViewProviderGroupExtension::shouldCheckExport(App::DocumentObject *obj) const
{
    //By default, do not check geofeaturegroup children for export exclusion,
    //because stuff in another geofeaturegroup is normally not in the model
    return !obj->hasExtension(App::GeoFeatureGroupExtension::getExtensionClassTypeId());
}

bool ViewProviderGroupExtension::extensionOnDelete(const std::vector< std::string >& ) {

    auto* group = getExtendedViewProvider()->getObject()->getExtensionByType<App::GroupExtension>();
    // If the group is nonempty ask the user if they want to delete its content
    if (group->Group.getSize() > 0) {
        QMessageBox::StandardButton choice =
            QMessageBox::question(getMainWindow(), QObject::tr ( "Delete group content?" ),
                QObject::tr ( "The %1 is not empty, delete its content as well?")
                    .arg ( QString::fromUtf8 ( getExtendedViewProvider()->getObject()->Label.getValue () ) ),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes );

        if (choice == QMessageBox::Yes) {
            Gui::Command::doCommand(Gui::Command::Doc,
                    "App.getDocument(\"%s\").getObject(\"%s\").removeObjectsFromDocument()"
                    , getExtendedViewProvider()->getObject()->getDocument()->getName()
                    , getExtendedViewProvider()->getObject()->getNameInDocument());
        }
    }
    return true;
}

int ViewProviderGroupExtension::extensionCanReorderObject(App::DocumentObject* obj,
                                                          App::DocumentObject* before)
{
    auto* group = getExtendedViewProvider()->getObject()->getExtensionByType<App::GroupExtension>();
    return ViewProvider::canReorderObjectInProperty(&group->Group, obj, before) ? 1 : 0;
}

int ViewProviderGroupExtension::extensionReorderObjects(const std::vector<App::DocumentObject*> &objs,
                                                        App::DocumentObject* before)
{
    auto* group = getExtendedViewProvider()->getObject()->getExtensionByType<App::GroupExtension>();
    return ViewProvider::reorderObjectsInProperty(&group->Group, objs, before) ? 1 : 0;
}

bool ViewProviderGroupExtension::extensionGetToolTip(const QByteArray &tag, QString &tooltip) const
{
    if (tag == Gui::treeVisibilityIconTag())
        tooltip += QObject::tr("\nCtrl + click to toggle all children visibility");
    return false;
}

bool ViewProviderGroupExtension::extensionIconMouseEvent(QMouseEvent *event, const QByteArray &tag)
{
    if (tag == Gui::treeVisibilityIconTag()) {
        if (event->button() == Qt::LeftButton && event->modifiers() == Qt::ControlModifier) {
            if (auto vp = Base::freecad_dynamic_cast<ViewProviderDocumentObject>(getExtendedContainer())) {
                App::GroupExtension::ToggleNestedVisibility guard;
                vp->Visibility.setValue(!vp->Visibility.getValue());
                return true;
            }
        }
    }
    return false;
}

namespace Gui {
EXTENSION_PROPERTY_SOURCE_TEMPLATE(Gui::ViewProviderGroupExtensionPython, Gui::ViewProviderGroupExtension)

// explicit template instantiation
template class GuiExport ViewProviderExtensionPythonT<ViewProviderGroupExtension>;
}
