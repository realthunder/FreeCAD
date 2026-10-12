/****************************************************************************
 *   Copyright (c) 2026 Zheng Lei (realthunder) <realthunder.dev@gmail.com> *
 *                                                                          *
 *   This file is part of the FreeCAD CAx development system.               *
 *                                                                          *
 *   This library is free software; you can redistribute it and/or          *
 *   modify it under the terms of the GNU Library General Public            *
 *   License as published by the Free Software Foundation; either           *
 *   version 2 of the License, or (at your option) any later version.       *
 *                                                                          *
 *   This library  is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of         *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the          *
 *   GNU Library General Public License for more details.                   *
 *                                                                          *
 *   You should have received a copy of the GNU Library General Public      *
 *   License along with this library; see the file COPYING.LIB. If not,     *
 *   write to the Free Software Foundation, Inc., 59 Temple Place,          *
 *   Suite 330, Boston, MA  02111-1307, USA                                 *
 *                                                                          *
 ****************************************************************************/

#include "PreCompiled.h"

#include <App/Document.h>
#include <App/ElementNamingUtils.h>
#include <Base/Tools.h>

#include "Body.h"
#include "SolidBody.h"

using namespace PartDesign;

PROPERTY_SOURCE(PartDesign::SolidBody, Part::Feature)

SolidBody::SolidBody()
{
    ADD_PROPERTY_TYPE(Tip, (nullptr), "Base", App::Prop_None,
            "The feature this solid is rolled back to. The solid has its shape,\n"
            "and a new feature of the solid is inserted after it");
    ADD_PROPERTY_TYPE(Group, (nullptr), "Base",
            (App::PropertyType)(App::Prop_Hidden|App::Prop_ReadOnly|App::Prop_Output),
            "The features of this solid");
    ADD_PROPERTY_TYPE(_Body, (nullptr), "Base",
            (App::PropertyType)(App::Prop_Hidden|App::Prop_ReadOnly|App::Prop_Output), 0);
    ADD_PROPERTY_TYPE(_Shown, (nullptr), "Base",
            (App::PropertyType)(App::Prop_Hidden|App::Prop_ReadOnly|App::Prop_Output), 0);
    ADD_PROPERTY_TYPE(_ShownShape, (false), "Base",
            (App::PropertyType)(App::Prop_Hidden|App::Prop_ReadOnly|App::Prop_Output), 0);
    // View state, as Visibility is: no undo step and no modified document
    // for it
    _Shown.setStatus(App::Property::NoModify, true);
    _ShownShape.setStatus(App::Property::NoModify, true);

    // The shape is in the body's coordinates, as the body's own is
    Placement.setStatus(App::Property::Hidden, true);
    Placement.setStatus(App::Property::Immutable, true);
}

Body *SolidBody::getBody() const
{
    return Base::freecad_dynamic_cast<Body>(_Body.getValue());
}

bool SolidBody::hasFeature(const App::DocumentObject *obj) const
{
    return obj && obj->isAttachedToDocument()
        && Group.find(obj->getNameInDocument()) == obj;
}

bool SolidBody::isSolidVisible() const
{
    if (Visibility.getValue())
        return true;
    for (auto obj : Group.getValues()) {
        if (obj && obj->Visibility.getValue())
            return true;
    }
    return false;
}

App::DocumentObject *SolidBody::getShowTarget() const
{
    if (_ShownShape.getValue())
        return const_cast<SolidBody*>(this);
    auto shown = _Shown.getValue();
    if (shown && hasFeature(shown))
        return shown;
    if (auto tip = Tip.getValue())
        return tip;
    return Group.getSize() ? Group.getValues().back() : nullptr;
}

void SolidBody::rememberShown(const App::DocumentObject *obj)
{
    // A solid body just made has no features yet, and is hidden to leave the
    // drawing to them: nothing was seen through it
    if (!obj || !Group.getSize() || isSolidVisible())
        return;
    // A pinned version takes a change of what is visible, and no other
    auto doc = getDocument();
    if (!doc || doc->testStatus(App::Document::FrozenVersion))
        return;

    bool own = obj == this;
    if (!own && !hasFeature(obj))
        return;
    if (_ShownShape.getValue() != own)
        _ShownShape.setValue(own);
    if (!own) {
        // The tip is not held: a solid last seen through its tip is shown
        // through whichever feature is its tip by then
        auto shown = obj == Tip.getValue() ? nullptr : const_cast<App::DocumentObject*>(obj);
        if (_Shown.getValue() != shown)
            _Shown.setValue(shown);
    }
}

short SolidBody::mustExecute() const
{
    if (Tip.isTouched())
        return 1;
    return inherited::mustExecute();
}

App::DocumentObjectExecReturn *SolidBody::execute()
{
    return updateShape();
}

App::DocumentObjectExecReturn *SolidBody::updateShape()
{
    auto tip = Tip.getValue();
    if (!tip) {
        // A solid that lost every feature is taken away by its body; until
        // then it is empty, which is no error of its own
        Shape.setValue(Part::TopoShape());
        return App::DocumentObject::StdReturn;
    }
    if (!hasFeature(tip))
        return new App::DocumentObjectExecReturn(
                QT_TRANSLATE_NOOP("Exception", "Tip is not a feature of this solid"));
    if (!tip->isDerivedFrom<Part::Feature>())
        return new App::DocumentObjectExecReturn(
                QT_TRANSLATE_NOOP("Exception", "Linked object is not a PartDesign feature"));

    Part::TopoShape shape = static_cast<Part::Feature*>(tip)->Shape.getShape();
    if (shape.isNull())
        return new App::DocumentObjectExecReturn(
                QT_TRANSLATE_NOOP("Exception", "Tip shape is empty"));

    // The tip's own placement becomes part of the geometry, as in
    // Body::execute()
    shape.transformShape(shape.getTransform(), true);
    Shape.setValue(shape);
    return App::DocumentObject::StdReturn;
}

void SolidBody::onChanged(const App::Property* prop)
{
    auto doc = getDocument();
    if (doc && !isRestoring()
            && !doc->testStatus(App::Document::Restoring)
            && !doc->isPerformingTransaction()) {
        if (prop == &Tip) {
            // Body.Tip stays the tip feature of the solid that is the body's
            // tip, which is all an older build knows of either
            auto tip = Tip.getValue();
            auto body = getBody();
            if (tip && body && hasFeature(tip)) {
                auto bodyTip = body->Tip.getValue();
                if (bodyTip && bodyTip != tip && hasFeature(bodyTip))
                    body->Tip.setValue(tip);
            }
        }
        else if (prop == &Visibility) {
            if (Visibility.getValue()) {
                // The solid is shown either by one of its features or by this
                // object, which has the shape of its tip
                for (auto obj : Group.getValues()) {
                    if (obj && obj->Visibility.getValue())
                        obj->Visibility.setValue(false);
                }
            }
            else
                rememberShown(this);
        }
    }
    inherited::onChanged(prop);
}

App::DocumentObject *SolidBody::getSubObject(const char *subname,
        PyObject **pyObj, Base::Matrix4D *pmat, bool transform, int depth) const
{
    // A path entering here names a member of the body, as one entering a
    // feature does (Feature::getSubObject()). Placement is identity, so there
    // is nothing of this object's to cancel.
    if (subname && subname != Data::findElementName(subname)) {
        const char *dot = strchr(subname, '.');
        if (dot) {
            if (auto body = getBody()) {
                if (auto feat = body->Group.find(std::string(subname, dot)))
                    return feat->getSubObject(dot+1, pyObj, pmat, true, depth+1);
            }
        }
    }
    return inherited::getSubObject(subname, pyObj, pmat, transform, depth);
}
