/******************************************************************************
 *   Copyright (c) 2012 Konstantinos Poulios <logari81@gmail.com>             *
 *                                                                            *
 *   This file is part of the FreeCAD CAx development system.                 *
 *                                                                            *
 *   This library is free software; you can redistribute it and/or            *
 *   modify it under the terms of the GNU Library General Public              *
 *   License as published by the Free Software Foundation; either             *
 *   version 2 of the License, or (at your option) any later version.         *
 *                                                                            *
 *   This library  is distributed in the hope that it will be useful,         *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of           *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the            *
 *   GNU Library General Public License for more details.                     *
 *                                                                            *
 *   You should have received a copy of the GNU Library General Public        *
 *   License along with this library; see the file COPYING.LIB. If not,       *
 *   write to the Free Software Foundation, Inc., 59 Temple Place,            *
 *   Suite 330, Boston, MA  02111-1307, USA                                   *
 *                                                                            *
 ******************************************************************************/

#include "PreCompiled.h"

#ifndef _PreComp_
# include <BRepAdaptor_Curve.hxx>
# include <BRepAdaptor_Surface.hxx>
# include <gp_Pln.hxx>
# include <TopoDS.hxx>
# include <TopoDS_Edge.hxx>
# include <TopoDS_Face.hxx>
# include <QDialog>
#endif

#include <boost/algorithm/string/find.hpp>

#include <App/Document.h>
#include <App/Origin.h>
#include <App/OriginFeature.h>
#include <App/MappedElement.h>
#include <App/Part.h>
#include <Base/Console.h>
#include <Gui/Command.h>
#include <Gui/Document.h>
#include <Gui/MainWindow.h>
#include <Mod/Part/App/PartFeature.h>
#include <Mod/Part/App/TopoShape.h>
#include <Mod/PartDesign/App/Feature.h>
#include <Mod/PartDesign/App/Body.h>
#include <Mod/PartDesign/App/DatumLine.h>
#include <Mod/PartDesign/App/DatumPlane.h>
#include <Mod/PartDesign/App/DatumPoint.h>

#include "ui_DlgReference.h"
#include "ReferenceSelection.h"
#include "TaskFeaturePick.h"
#include "Utils.h"

FC_LOG_LEVEL_INIT("Sketch",true,true)

using namespace PartDesignGui;
using namespace Gui;

/* TRANSLATOR PartDesignGui::ReferenceSelection.cpp */

bool ReferenceSelection::allow(App::Document* pDoc, App::DocumentObject* pObj, const char* sSubName)
{
    PartDesign::Body *body = getBody();
    App::OriginGroupExtension* originGroup = getOriginGroupExtension(body);

    // Don't allow selection in other document
    if (support && pDoc != support->getDocument()) {
        return false;
    }

    // Enable selection from origin of current part/
    if (pObj->isDerivedFrom<App::OriginFeature>()) {
        return allowOrigin(body, originGroup, pObj);
    }

    if (pObj->isDerivedFrom<Part::Datum>()) {
        return allowDatum(body, pObj);
    }

    // The flag was used to be set. So, this block will never be treated and really doesn't make sense anyway
#if 0
    if (!type.testFlag(AllowSelection::OTHERBODY)) {
        if (support == NULL)
            return false;
        if (pObj != support)
            return false;
    }
#endif
    // Handle selection of geometry elements
    if (!sSubName || sSubName[0] == '\0')
        return type.testFlag(AllowSelection::WHOLE);

    return allowFeature(pObj, sSubName);
}

PartDesign::Body* ReferenceSelection::getBody() const
{
    PartDesign::Body *body;
    if (support) {
        body = PartDesign::Body::findBodyOf (support);
    }
    else {
        body = PartDesignGui::getBody(false);
    }

    return body;
}

App::OriginGroupExtension* ReferenceSelection::getOriginGroupExtension(PartDesign::Body *body) const
{
    App::DocumentObject* originGroupObject = nullptr;

    if (body) { // Search for Part of the body
        originGroupObject = App::OriginGroupExtension::getGroupOfObject(body);
    }
    else if (support) { // if no body search part for support
        originGroupObject = App::OriginGroupExtension::getGroupOfObject(support);
    }
    else { // fallback to active part
        originGroupObject = PartDesignGui::getActivePart();
    }

    App::OriginGroupExtension* originGroup = nullptr;
    if (originGroupObject)
        originGroup = originGroupObject->getExtensionByType<App::OriginGroupExtension>();

    return originGroup;
}

bool ReferenceSelection::allowOrigin(PartDesign::Body *body, App::OriginGroupExtension* originGroup, App::DocumentObject* pObj) const
{
    bool fits = false;
    if (type.testFlag(AllowSelection::FACE) && pObj->isDerivedFrom<App::Plane>()) {
        fits = true;
    }
    else if (type.testFlag(AllowSelection::EDGE) && pObj->isDerivedFrom<App::Line>()) {
        fits = true;
    }

    if (fits) { // check that it actually belongs to the chosen body or part
        try { // here are some throwers
            if (body) {
                if (body->getOrigin ()->hasObject (pObj) ) {
                    return true;
                }
            }
            else if (originGroup ) {
                if (originGroup->getOrigin()->hasObject(pObj)) {
                    return true;
                }
            }
        }
        catch (const Base::Exception&) {
        }
    }
    return false; // The Plane/Axis doesn't fits our needs
}

bool ReferenceSelection::allowDatum(PartDesign::Body *body, App::DocumentObject* pObj) const
{
    if (!body) { // Allow selecting Part::Datum features from the active Body
        return false;
    }
    else if (!type.testFlag(AllowSelection::OTHERBODY) && !body->hasObject(pObj)) {
        return false;
    }

    if (type.testFlag(AllowSelection::FACE) && (pObj->isDerivedFrom<PartDesign::Plane>()))
        return true;
    if (type.testFlag(AllowSelection::EDGE) && (pObj->isDerivedFrom<PartDesign::Line>()))
        return true;
    if (type.testFlag(AllowSelection::POINT) && (pObj->isDerivedFrom<PartDesign::Point>()))
        return true;

    return false;
}

bool ReferenceSelection::allowFeature(App::DocumentObject* pObj, const char* sSubName) const
{
    if (type.testFlag(AllowSelection::POINT) && boost::ifind_first(sSubName, "vertex")) {
        return true;
    }

    if (type.testFlag(AllowSelection::EDGE) && boost::ifind_first(sSubName, "edge")) {
        if (isEdge(pObj, sSubName))
            return true;
    }

    if (type.testFlag(AllowSelection::CIRCLE) && boost::ifind_first(sSubName, "edge")) {
        if (isCircle(pObj, sSubName))
            return true;
    }

    if (type.testFlag(AllowSelection::FACE) && boost::ifind_first(sSubName, "face")) {
        if (isFace(pObj, sSubName))
            return true;
    }

    if (type.testFlag(AllowSelection::WIRE) && boost::ifind_first(sSubName, "wire")) {
        if (isWire(pObj, sSubName))
            return true;
    }

    return false;
}

bool ReferenceSelection::isEdge(App::DocumentObject* pObj, const char* sSubName) const
{
    const Part::TopoShape &shape = Part::Feature::getTopoShape(pObj, sSubName, /*needSubElement*/true);
    if (shape.shapeType(/*silent*/true) == TopAbs_EDGE) {
        if (type.testFlag(AllowSelection::PLANAR)) {
            return shape.isLinearEdge();
        }
        return true;
    }

    return false;
}

bool ReferenceSelection::isFace(App::DocumentObject* pObj, const char* sSubName) const
{
    const Part::TopoShape &shape = Part::Feature::getTopoShape(pObj, sSubName, /*needSubElement*/true);
    if (shape.shapeType(/*silent*/true) == TopAbs_FACE) {
        if (type.testFlag(AllowSelection::PLANAR)) {
            return shape.isPlanarFace();
        }
        return true;
    }

    return false;
}

bool ReferenceSelection::isWire(App::DocumentObject* pObj, const char* sSubName) const
{
    const Part::TopoShape &shape = Part::Feature::getTopoShape(pObj, sSubName, /*needSubElement*/true);
    if (shape.shapeType(/*silent*/true) == TopAbs_WIRE) {
        if (type.testFlag(AllowSelection::PLANAR)) {
            gp_Pln pln;
            return shape.findPlane(pln);
        }
        return true;
    }

    return false;
}

bool ReferenceSelection::isCircle(App::DocumentObject* pObj, const char* sSubName) const
{
    const Part::TopoShape &shape = Part::Feature::getTopoShape(pObj, sSubName, /*needSubElement*/true);
    if (shape.shapeType(/*silent*/true) == TopAbs_EDGE) {
        const TopoDS_Edge& edgeShape = TopoDS::Edge(shape.getShape());
        BRepAdaptor_Curve adapt(edgeShape);
        if (adapt.GetType() == GeomAbs_Circle) {
            return true;
        }
    }
    return false;
}

// ----------------------------------------------------------------------------

bool NoDependentsSelection::allow(App::Document* /*pDoc*/, App::DocumentObject* pObj, const char* /*sSubName*/)
{
    if(!inList.count(pObj)) {
        return true;
    }
    else {
        this->notAllowedReason = QT_TR_NOOP("Selecting this will cause circular dependency.");
        return false;
    }
}

bool CombineSelectionFilterGates::allow(App::Document* pDoc, App::DocumentObject* pObj, const char* sSubName)
{
    return filter1->allow(pDoc, pObj, sSubName) && filter2->allow(pDoc, pObj, sSubName);
}


namespace PartDesignGui
{

bool getReferencedSelection(const App::DocumentObject* thisObj, const Gui::SelectionChanges& msg,
                            App::DocumentObject*& selObj, std::vector<std::string>& selSub)
{
    selObj = nullptr;
    if (!thisObj)
        return false;

    App::SubObjectT sel = msg.pOriginalMsg ? msg.pOriginalMsg->Object : msg.Object;
    if (sel.getDocument() != thisObj->getDocument())
        return false;

    auto sobj = sel.getSubObject();
    if (!sobj || sobj == thisObj)
        return false;

    if (PartDesign::Feature::isDatum(sobj->getLinkedObject()))
        sel.setSubName(sel.getSubNameNoElement());

    auto objT = PartDesignGui::importExternalObject(sel, false, false);
    selObj = objT.getObject();
    selSub.push_back(objT.getSubName());
    return true;
}

QString getRefStr(const App::DocumentObject* obj, const std::vector<std::string>& sub)
{
    if (!obj)
        return {};

    if (PartDesign::Feature::isDatum(obj))
        return QString::fromUtf8(obj->getNameInDocument());
    else if (!sub.empty())
        return QString::fromUtf8(obj->getNameInDocument()) + QStringLiteral(":") +
               QString::fromUtf8(sub.front().c_str());
    else
        return {};
}

bool populateRefElement(App::PropertyLinkSub *prop, QLabel *label, bool canTouch) {
    if(!prop || !label)
        return false;

    App::DocumentObject* obj = prop->getValue();
    if(!obj || !obj->getNameInDocument()) {
        label->setText(QObject::tr("None"));
        return false;
    }
    const auto &subs = prop->getShadowSubs();
    if(subs.empty()) {
        label->setText(QString::fromUtf8(obj->getNameInDocument()));
        return false;
    }

    // first=new style topo name, second=old index based name
    std::pair<std::string, std::string> sub = subs.front();

    QString fmt = QStringLiteral("%1:%2");

    // Check if the element is missing
    bool touched = false;
    if(Data::hasMissingElement(sub.second.c_str())) {
        if(canTouch) {
            for(auto &mapped : Part::Feature::getRelatedElements(obj,sub.first.c_str())) {
                FC_WARN("guess element reference: " << sub.first << " -> " << mapped.name);
                touched = true;
                sub.second.clear();
                mapped.index.appendToStringBuffer(sub.second);
                prop->setValue(obj, {sub.second});
                break;
            }
        }
        if(!touched)
            fmt = QStringLiteral("<font color='red'>%1:%2</font>");
    }

    // Display the shorter index based name
    label->setText(fmt.arg(QString::fromUtf8(obj->getNameInDocument()),
                           QString::fromUtf8(sub.second.c_str())));
    return touched;
}


std::string buildLinkSubPythonStr(const App::DocumentObject* obj, const std::vector<std::string>& subs)
{
    if (!obj)
        return "None";

    std::ostringstream ss;
    ss << "(" << Gui::Command::getObjectCmd(obj) << ", [";

    for (const auto & sub : subs)
        ss << "'" << sub << "',";
    ss << "])";
    return ss.str();
}

std::string buildLinkSingleSubPythonStr(const App::DocumentObject* obj,
        const std::vector<std::string>& subs)
{
    if (!obj)
        return "None";

    if (PartDesign::Feature::isDatum(obj))
        return Gui::Command::getObjectCmd(obj,"(",", [''])");
    else
        return Gui::Command::getObjectCmd(obj,"(",", ['") + subs.front() + "'])";
}

std::string buildLinkListPythonStr(const std::vector<App::DocumentObject*> & objs)
{
    if ( objs.empty() ) {
        return "None";
    }

    std::string result("[");

    for (auto obj : objs)
        result += Gui::Command::getObjectCmd(obj,nullptr,",");
    result += "]";

    return result;
}

std::string buildLinkSubListPythonStr(const std::vector<App::DocumentObject*> & objs,
        const std::vector<std::string>& subs)
{
    if ( objs.empty() ) {
        return "None";
    }

    std::string result("[");

    assert (objs.size () == subs.size () );

    for (size_t i=0, objs_sz=objs.size(); i < objs_sz; i++) {
        if (objs[i] ) {
            result += '(';
            result += Gui::Command::getObjectCmd(objs[i]);
            result += ",\"";
            result += subs[i];
            result += "\"),";
        }
    }

    result += "]";

    return result;
}
}
