/***************************************************************************
 *   Copyright (c) 2017 Werner Mayer <wmayer[at]users.sourceforge.net>     *
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
# include <string>
# include <QAction>
# include <QMenu>
# include <QPointer>
# include <Inventor/draggers/SoDragger.h>
# include <Inventor/nodes/SoPickStyle.h>
# include <Inventor/nodes/SoTransform.h>
#endif

#include <App/ComplexGeoDataPy.h>
#include <App/GeoFeature.h>
#include <Base/Placement.h>
#include <Base/Console.h>
#include "Gui/ViewParams.h"

#include "Application.h"
#include "BitmapFactory.h"
#include "Control.h"
#include "Document.h"
#include "ViewProviderLink.h"
#include "Window.h"

#include "SoFCUnifiedSelection.h"
#include "SoFCCSysDragger.h"
#include "SoFCUnifiedSelection.h"
#include "TaskCSysDragger.h"
#include "View3DInventorViewer.h"
#include "ViewProviderDragger.h"


using namespace Gui;

PROPERTY_SOURCE(Gui::ViewProviderDragger, Gui::ViewProviderDocumentObject)

ViewProviderDragger::ViewProviderDragger() = default;

ViewProviderDragger::~ViewProviderDragger() = default;

void ViewProviderDragger::updateData(const App::Property* prop)
{
    if (prop->isDerivedFrom(App::PropertyPlacement::getClassTypeId()) &&
             strcmp(prop->getName(), "Placement") == 0) {
        // Note: If R is the rotation, c the rotation center and t the translation
        // vector then Inventor applies the following transformation: R*(x-c)+c+t
        // In FreeCAD a placement only has a rotation and a translation part but
        // no rotation center. This means that the following equation must be ful-
        // filled: R * (x-c) + c + t = R * x + t
        //    <==> R * x + t - R * c + c = R * x + t
        //    <==> (I-R) * c = 0 ==> c = 0
        // This means that the center point must be the origin!
        Base::Placement p = static_cast<const App::PropertyPlacement*>(prop)->getValue();
        updateTransform(p, pcTransform);
    }

    ViewProviderDocumentObject::updateData(prop);
}

bool ViewProviderDragger::doubleClicked()
{
    Gui::Application::Instance->activeDocument()->setEdit(this, (int)ViewProvider::Default);
    return true;
}

void ViewProviderDragger::setupContextMenu(QMenu* menu, QObject* receiver, const char* member)
{
    if (!getObject())
        return;
    if (auto propPlacement = Base::freecad_dynamic_cast<App::PropertyPlacement>(
                getObject()->getPropertyByName("Placement")))
    {
        if (!propPlacement->testStatus(App::Property::Hidden)
                && !propPlacement->testStatus(App::Property::ReadOnly))
        {
            QAction* act = menu->addAction(QObject::tr("Transform"), receiver, member);
            act->setIcon(BitmapFactory().pixmap("Std_TransformManip.svg"));
            act->setToolTip(QObject::tr("Transform at the origin of the placement"));
            act->setData(QVariant((int)ViewProvider::Transform));
            act = menu->addAction(QObject::tr("Transform at"), receiver, member);
            act->setToolTip(QObject::tr("Transform at the center of the shape"));
            act->setData(QVariant((int)ViewProvider::TransformAt));
        }
    }
    ViewProviderDocumentObject::setupContextMenu(menu, receiver, member);
}

ViewProvider *ViewProviderDragger::startEditing(int mode) {
    _linkDragger = nullptr;
    auto ret = ViewProviderDocumentObject::startEditing(mode);
    if(!ret)
        return ret;
    return _linkDragger?_linkDragger:ret;
}

bool ViewProviderDragger::checkLink(int mode) {
    // Trying to detect if the editing request is forwarded by a link object,
    // usually by doubleClicked(). If so, we route the request back. There shall
    // be no risk of infinite recursion, as ViewProviderLink handles
    // ViewProvider::Transform request by itself.
    ViewProviderDocumentObject *vpParent = nullptr;
    std::string subname;
    auto doc = Application::Instance->editDocument();
    if(!doc)
        return false;
    doc->getInEdit(&vpParent,&subname);
    if(!vpParent)
        return false;

    auto parent = vpParent->getObject();
    // Check for collapsed link array
    std::vector<int> subSizes;
    auto objs = parent->getSubObjectList(subname.c_str(), &subSizes);
    auto it = std::find(objs.begin(), objs.end(), getObject());
    if (it != objs.end()) {
        auto prev = it;
        --prev;
        std::string sub(subname.begin()+(prev - objs.begin()),
                        subname.begin()+(it - objs.begin()));
        if (sub.size() && std::isdigit(*sub.begin())) {
            auto prevObj = prev==objs.begin() ? parent : *(prev-1);
            auto linkExt = prevObj->getExtensionByType<App::LinkBaseExtension>(true);
            if (linkExt && !linkExt->getShowElementValue() && linkExt->getElementCountValue() > 0) {
                if (auto linkVp = Base::freecad_dynamic_cast<ViewProviderLink>(
                        Application::Instance->getViewProvider(prevObj))) {
                    _linkArrayIndex = atoi(sub.c_str());
                    Base::Matrix4D mat;
                    parent->getSubObject(std::string(subname.c_str(), subSizes[prev-objs.begin()]).c_str(), nullptr, &mat);
                    auto matSave = doc->getEditingTransform();
                    doc->setEditingTransform(mat);
                    if (linkVp->startDragArrayElement(mode, _linkArrayIndex)) {
                        _linkArray = prevObj;
                        _linkDragger = linkVp;
                    } else
                        doc->setEditingTransform(matSave);
                    return true;
                }
            }
        }
    }

    auto sobj = vpParent->getObject()->getSubObject(subname.c_str());
    if(!sobj || sobj==getObject() || sobj->getLinkedObject(true)!=getObject())
        return false;
    auto vp = Application::Instance->getViewProvider(sobj);
    if(!vp)
        return false;
    _linkDragger = vp->startEditing(mode);
    if(_linkDragger)
        return true;
    return false;
}

Base::Matrix4D ViewProviderDragger::getDragOffset(const ViewProviderDocumentObject *vp)
{
    Base::Matrix4D res;
    if (!vp || !vp->getObject())
        return res;
    auto selctx = Gui::Selection().getExtendedContext(vp->getObject());
    auto parent = selctx.getObject();
    if (!parent)
        return res;
    std::vector<int> subSizes;
    auto objs = parent->getSubObjectList(selctx.getSubName().c_str(), &subSizes);
    auto it = std::find(objs.begin(), objs.end(), vp->getObject());
    if (it != objs.end() && it != objs.begin()) {
        int offset = it - objs.begin();
        selctx = App::SubObjectT(vp->getObject(), selctx.getSubName().c_str() + subSizes[offset]);
    }

    Base::Rotation rot;
    Base::BoundBox3d bbox;

    PyObject *pyobj = nullptr;
    Base::Matrix4D mat;
    vp->getObject()->getSubObject(selctx.getSubName().c_str(), &pyobj, &mat, false);
    if (pyobj) {
        Base::PyGILStateLocker lock;
        Py::Object pyObj(pyobj, true);
        try {
            if (PyObject_TypeCheck(pyobj, &Data::ComplexGeoDataPy::Type)) {
                auto geodata = static_cast<Data::ComplexGeoDataPy*>(pyobj)->getComplexGeoDataPtr();
                if (!geodata->getRotation(rot))
                    rot = geodata->getPlacement().getRotation();
                bbox = geodata->getBoundBox();
            }
        } catch (Base::Exception &e) {
            e.ReportException();
        }
    }

    if (!bbox.IsValid()) {
        Base::Matrix4D mat;
        bbox = vp->getBoundingBox(selctx.getSubName().c_str(),&mat,false);
    }
    if (bbox.IsValid()) 
        res = Base::Placement(bbox.GetCenter(), rot).toMatrix();
    return res;
}

Base::Matrix4D ViewProviderDragger::getDragOffset()
{
    return getDragOffset(this);
}

static QPointer<TaskCSysDragger> _TaskDragger;

bool ViewProviderDragger::setEdit(int ModNum)
{
  if (ModNum != ViewProvider::Transform 
          && ModNum != ViewProvider::TransformAt
          && ModNum != ViewProvider::Default)
      return ViewProviderDocumentObject::setEdit(ModNum);

  if(checkLink(ModNum))
      return true;

  App::DocumentObject *genericObject = this->getObject();
  if (genericObject->isDerivedFrom(App::GeoFeature::getClassTypeId()))
  {
    App::GeoFeature *geoFeature = static_cast<App::GeoFeature *>(genericObject);

    if (ModNum == TransformAt) {
        this->dragOffset = getDragOffset();
    } else
        this->dragOffset = Base::Matrix4D();
    
    Base::Placement placement =
        geoFeature->Placement.getValue().toMatrix() * this->dragOffset;
    this->dragOffset.inverse();
    auto tempTransform = new SoTransform();
    tempTransform->ref();
    updateTransform(placement, tempTransform);

    assert(!csysDragger);
    csysDragger = new SoFCCSysDragger();
    csysDragger->setAxisColors(
      Gui::ViewParams::getAxisXColor(),
      Gui::ViewParams::getAxisYColor(),
      Gui::ViewParams::getAxisZColor()
    );
    csysDragger->draggerSize.setValue(0.05f);
    csysDragger->translation.setValue(tempTransform->translation.getValue());
    csysDragger->rotation.setValue(tempTransform->rotation.getValue());

    tempTransform->unref();

    csysDragger->addStartCallback(dragStartCallback, this);
    csysDragger->addFinishCallback(dragFinishCallback, this);
    csysDragger->addMotionCallback(dragMotionCallback, this);

    // dragger node is added to viewer's editing root in setEditViewer
    // pcRoot->insertChild(csysDragger, 0);
    csysDragger->ref();

    _TaskDragger = new TaskCSysDragger(this, csysDragger);
    Gui::Control().showDialog(_TaskDragger);
  }

  return true;
}

void ViewProviderDragger::unsetEdit(int ModNum)
{
  Q_UNUSED(ModNum);

  if (_linkArrayIndex >= 0) {
      auto vp = Base::freecad_dynamic_cast<ViewProviderLink>(
              Application::Instance->getViewProvider(_linkArray.getObject()));
      if (vp)
          vp->endDragArrayElement();
      _linkArrayIndex = -1;
  }

  if(csysDragger)
  {
    // dragger node is added to viewer's editing root in setEditViewer
    // pcRoot->removeChild(csysDragger); //should delete csysDragger
    csysDragger->unref();
    csysDragger = nullptr;
  }
  Gui::Control().closeDialog();
}

void ViewProviderDragger::setEditViewer(Gui::View3DInventorViewer* viewer, int ModNum)
{
    Q_UNUSED(ModNum);

    if (csysDragger && viewer)
    {
      auto rootPickStyle = new SoPickStyle();
      rootPickStyle->style = SoPickStyle::UNPICKABLE;
      auto selection = static_cast<SoGroup*>(viewer->getSceneGraph());
      selection->insertChild(rootPickStyle, 0);
      viewer->setSelectionEnabled(false);
      csysDragger->setUpAutoScale(viewer->getSoRenderManager()->getCamera());

      auto mat = viewer->getDocument()->getEditingTransform();
      viewer->getDocument()->setEditingTransform(mat);
      auto feat = dynamic_cast<App::GeoFeature *>(getObject());
      if(feat) {
          auto matInverse = feat->Placement.getValue().toMatrix();
          matInverse.inverse();
          mat *= matInverse;
      }
      viewer->setupEditingRoot(csysDragger,&mat);
    }
}

void ViewProviderDragger::unsetEditViewer(Gui::View3DInventorViewer* viewer)
{
    auto selection = static_cast<SoGroup*>(viewer->getSceneGraph());
    SoNode *child = selection->getChild(0);
    if (child && child->isOfType(SoPickStyle::getClassTypeId())) {
        selection->removeChild(child);
        viewer->setSelectionEnabled(true);
    }
}

void ViewProviderDragger::dragStartCallback(void *data, SoDragger *d)
{
    // This is called when a manipulator is about to manipulating
    reinterpret_cast<ViewProviderDragger*>(data)->onDragStart(d);
}

void ViewProviderDragger::dragFinishCallback(void *data, SoDragger *d)
{
    // This is called when a manipulator has done manipulating
    reinterpret_cast<ViewProviderDragger *>(data)->onDragFinish(d);
}

void ViewProviderDragger::dragMotionCallback(void *data, SoDragger *d)
{
    reinterpret_cast<ViewProviderDragger *>(data)->onDragMotion(d);
}

void ViewProviderDragger::onDragStart(SoDragger *)
{
    Gui::Application::Instance->activeDocument()->openCommand(QT_TRANSLATE_NOOP("Command", "Transform"));
}

void ViewProviderDragger::onDragFinish(SoDragger *d)
{
    auto dragger = static_cast<SoFCCSysDragger *>(d);
    updatePlacementFromDragger(dragger);

    if (_TaskDragger)
        _TaskDragger->onEndMove();
    Gui::Application::Instance->activeDocument()->commitCommand();
}

void ViewProviderDragger::onDragMotion(SoDragger *d)
{
    SoFCCSysDragger *dragger = static_cast<SoFCCSysDragger *>(d);
    SbVec3f v;
    SbRotation r;
    v = dragger->translation.getValue();
    r = dragger->rotation.getValue();
    float q1,q2,q3,q4;
    r.getValue(q1,q2,q3,q4);
    Base::Placement pla(Base::Vector3d(v[0],v[1],v[2]),Base::Rotation(q1,q2,q3,q4));
    updateTransform(pla * this->dragOffset, this->pcTransform);
}

void ViewProviderDragger::updatePlacementFromDragger(SoFCCSysDragger* draggerIn)
{
  App::DocumentObject *genericObject = this->getObject();
  if (!genericObject->isDerivedFrom(App::GeoFeature::getClassTypeId()))
    return;
  auto geoFeature = static_cast<App::GeoFeature *>(genericObject);
  Base::Placement originalPlacement = geoFeature->Placement.getValue();
  auto offset = this->dragOffset;
  offset.inverse();
  Base::Placement freshPlacement = originalPlacement.toMatrix() * offset;
  if (draggerIn->getMovement(freshPlacement))
    geoFeature->Placement.setValue(freshPlacement * this->dragOffset);
}

void ViewProviderDragger::updateTransform(const Base::Placement& from, SoTransform* to)
{
    auto q0 = (float)from.getRotation().getValue()[0];
    auto q1 = (float)from.getRotation().getValue()[1];
    auto q2 = (float)from.getRotation().getValue()[2];
    auto q3 = (float)from.getRotation().getValue()[3];
    auto px = (float)from.getPosition().x;
    auto py = (float)from.getPosition().y;
    auto pz = (float)from.getPosition().z;
  to->rotation.setValue(q0,q1,q2,q3);
  to->translation.setValue(px,py,pz);
  to->center.setValue(0.0f,0.0f,0.0f);
  to->scaleFactor.setValue(1.0f,1.0f,1.0f);
}
