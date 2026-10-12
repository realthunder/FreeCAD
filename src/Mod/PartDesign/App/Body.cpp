/***************************************************************************
 *   Copyright (c) 2010 Juergen Riegel <FreeCAD@juergen-riegel.net>        *
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

#include <Base/Console.h>
#include <Base/Placement.h>
#include <Base/Tools.h>
#include <App/Document.h>
#include <App/Origin.h>
#include <App/VarSet.h>


#include "Body.h"
#include "BodyPy.h"
#include "Feature.h"
#include "FeatureBase.h"
#include "FeatureExtrusion.h"
#include "FeatureSolid.h"
#include "FeatureSketchBased.h"
#include "FeatureTransformed.h"
#include "FeatureMultiTransform.h"
#include "FeatureWrap.h"
#include "FeatureDressUp.h"
#include "DatumPoint.h"
#include "DatumLine.h"
#include "DatumPlane.h"
#include "AuxGroup.h"
#include "ShapeBinder.h"
#include "SolidBody.h"

#include <Mod/Part/App/PartParams.h>

FC_LOG_LEVEL_INIT("PartDesign", true, true);

using namespace PartDesign;


PROPERTY_SOURCE(PartDesign::Body, Part::BodyBase)

Body::Body() {
    ADD_PROPERTY_TYPE(SingleSolid,(Part::PartParams::getSingleSolid()),
            "Base",(App::PropertyType)(App::Prop_None),
            "Enforce single solid on each feature");
    _GroupTouched.setStatus(App::Property::Output,true);
    BaseFeature.setScope(App::LinkScope::Global);

    ADD_PROPERTY_TYPE(AutoGroupSolids,(false),"Base",(App::PropertyType)(App::Prop_Output),
            "Auto group features of a solid under the latest feature");

    // Off here and switched on in setupObject(): a body restored from a
    // document that never had the property keeps the grouping it was saved with
    ADD_PROPERTY_TYPE(AddSolidBody,(false),"Base",(App::PropertyType)(App::Prop_Output),
            "Group the features of each solid under a solid body once the body\n"
            "has more than one solid. AutoGroupSolids is ignored while this is on");
}

/*
// Note: The following code will catch Python Document::removeObject() modifications. If the object removed is
// a member of the Body::Group, then it will be automatically removed from the Group property which triggers the
// following two methods
// But since we require the Python user to call both Document::addObject() and Body::addObject(), we should
// also require calling both Document::removeObject and Body::removeFeature() in order to be consistent
void Body::onBeforeChange(const App::Property *prop)
{
    // Remember the feature before the current Tip. If the Tip is already at the first feature, remember the next feature
    if (prop == &Group) {
        std::vector<App::DocumentObject*> features = Group.getValues();
        if (features.empty()) {
            rememberTip = NULL;
        } else {
            std::vector<App::DocumentObject*>::iterator it = std::find(features.begin(), features.end(), Tip.getValue());
            if (it == features.begin()) {
                it++;
                if (it == features.end())
                    rememberTip = NULL;
                else
                    rememberTip = *it;
            } else {
                it--;
                rememberTip = *it;
            }
        }
    }

    return Part::Feature::onBeforeChange(prop);
}

void Body::onChanged(const App::Property *prop)
{
    if (prop == &Group) {
        std::vector<App::DocumentObject*> features = Group.getValues();
        if (features.empty()) {
            Tip.setValue(NULL);
        } else {
            std::vector<App::DocumentObject*>::iterator it = std::find(features.begin(), features.end(), Tip.getValue());
            if (it == features.end()) {
                // Tip feature was deleted
                Tip.setValue(rememberTip);
            }
        }
    }

    return Part::Feature::onChanged(prop);
}
*/

short Body::mustExecute() const
{
    if ( Tip.isTouched() ) {
        return 1;
    }
    return Part::BodyBase::mustExecute();
}

App::DocumentObject* Body::getPrevSolidFeature(App::DocumentObject *start)
{
    if (!start) { // default to tip
        start = Tip.getValue();
    }

    int index;
    if (!start || !start->isAttachedToDocument()
               || start->isDerivedFrom<PartDesign::Extrusion>()
               || start->isDerivedFrom<PartDesign::Solid>()
               || !this->Group.find(start->getNameInDocument(), &index)) { // No Tip
        return nullptr;
    }

    const auto & objs = this->Group.getValues();
    for (--index; index>=0; --index) {
        if (isSolidFeature(objs[index]))
            return objs[index];
    }

    return nullptr;
}

App::DocumentObject* Body::getNextSolidFeature(App::DocumentObject *start)
{
    if (!start) { // default to tip
        start = Tip.getValue();
    }

    int index;
    if (!start || !start->isAttachedToDocument()
               || !this->Group.find(start->getNameInDocument(), &index)) { // No Tip
        return nullptr;
    }

    const auto & objs = this->Group.getValues();
    int count = this->Group.getSize();
    for (++index; index<count; ++index) {
        if (isSolidFeature(objs[index]))
            return objs[index];
    }

    return nullptr;
}

bool Body::isAfterInsertPoint(App::DocumentObject* feature) {
    App::DocumentObject *nextSolid = getNextSolidFeature ();
    assert (feature);

    if (feature == nextSolid) {
        return true;
    } else if (!nextSolid) { // the tip is last solid, we can't be placed after it
        return false;
    } else {
        return isAfter ( feature, nextSolid );
    }
}

bool Body::isMemberOfMultiTransform(const App::DocumentObject* obj)
{
    if (!obj || !obj->isDerivedFrom<PartDesign::Transformed>())
        return false;

    for (auto o : obj->getInList()) {
        if (o->isDerivedFrom<PartDesign::MultiTransform>())
            return true;
    }
    return false;
}

bool Body::isSolidFeature(const App::DocumentObject* obj) const
{
    if (!obj)
        return false;

    if (obj == BaseFeature.getValue())
        return true;

    auto type = obj->getTypeId();
    if (type.isDerivedFrom(PartDesign::Extrusion::getClassTypeId()))
        return static_cast<const PartDesign::Extrusion*>(obj)->NewSolid.getValue();

    if (type.isDerivedFrom(PartDesign::FeatureWrap::getClassTypeId()))
        return static_cast<const PartDesign::FeatureWrap*>(obj)->isSolidFeature();

    if (type.isDerivedFrom(PartDesign::Feature::getClassTypeId())
            && !PartDesign::Feature::isDatum(obj)) {
        // Transformed Features inside a MultiTransform are not solid features
        return !isMemberOfMultiTransform(obj);
    }
    return false;//DeepSOIC: work-in-progress?
}

bool Body::isAllowed(const App::DocumentObject *obj)
{
    if (!obj)
        return false;
    // a datum element of a coordinate system stays with it
    if (auto datum = Base::freecad_dynamic_cast<App::DatumElement>(obj)) {
        if (datum->getLCS())
            return false;
    }
    return isAllowed(obj->getTypeId());
}

bool Body::isAllowed(const Base::Type &type)
{
    return (type.isDerivedFrom(PartDesign::Feature::getClassTypeId()) ||
            type.isDerivedFrom(Part::Datum::getClassTypeId())   ||
            type.isDerivedFrom(PartDesign::Solid::getClassTypeId())   ||
            type.isDerivedFrom(PartDesign::SolidBody::getClassTypeId()) ||
            type.isDerivedFrom(Part::Part2DObject::getClassTypeId()) ||
            type.isDerivedFrom(PartDesign::ShapeBinder::getClassTypeId()) ||
            type.isDerivedFrom(Part::SubShapeBinder::getClassTypeId()) ||
            type.isDerivedFrom(PartDesign::AuxGroup::getClassTypeId()) ||
            // a coordinate system and a lone datum element, as upstream
            // takes them; an origin belongs to its own container
            type.isDerivedFrom(App::DatumElement::getClassTypeId()) ||
            (type.isDerivedFrom(App::LocalCoordinateSystem::getClassTypeId())
                && !type.isDerivedFrom(App::Origin::getClassTypeId())) ||
            // the parameters of the body (upstream ec841ed6d4)
            type.isDerivedFrom(App::VarSet::getClassTypeId()));
}


Body* Body::findBodyOf(const App::DocumentObject* feature)
{
    if(!feature)
        return nullptr;

    if (feature->isDerivedFrom<PartDesign::Feature>())
        return static_cast<const PartDesign::Feature*>(feature)->getFeatureBody();
    if (feature->isDerivedFrom<PartDesign::AuxGroup>())
        return static_cast<const PartDesign::AuxGroup*>(feature)->getBody();
    if (feature->isDerivedFrom<PartDesign::SolidBody>()) {
        if (auto body = static_cast<const PartDesign::SolidBody*>(feature)->getBody())
            return body;
    }

    return Base::freecad_dynamic_cast<Body>(BodyBase::findBodyOf(feature));
}


std::vector<App::DocumentObject*> Body::addObject(App::DocumentObject *feature)
{
    if(!isAllowed(feature))
        THROWM(Base::ValueError, "Body: object is not allowed")

    //TODO: features should not add all links

    //only one group per object. If it is in a body the single feature will be removed
    auto *group = App::GroupExtension::getGroupOfObject(feature);
    if(group && group != getExtendedObject())
        group->getExtensionByType<GroupExtension>()->removeObject(feature);
      
    // It is not safe to insert after tip (which is what we get by calling
    // getNextSolidFeature(nullptr)). For example, padding from a sketch, but
    // the sketch is based on a feature after the tip.
    //
    // It is only absolutely safe to insert at the very end. We'll have to rely
    // up on PartGui commands to determine the new object insertion point based
    // on user selection at the time of command invoking.
    //
    // insertObject (feature, getNextSolidFeature (), [>after = <] false);
    //
    insertObject (feature, nullptr, false);
    setTip(feature);
    return {feature};
}

std::vector< App::DocumentObject* > Body::addObjects(std::vector< App::DocumentObject* > objs) {

    for(auto obj : objs)
        addObject(obj);

    return objs;
}

App::DocumentObject *
Body::getInsertionPosition(const std::vector<App::DocumentObject*> &objs)
{
    App::DocumentObject * tip = this->Tip.getValue();
    if (objs.empty() || !tip)
        return getNextSolidFeature();

    // Use the tip to determine which sibling group to insert the new feature
    const auto siblings = getSiblings(tip);

    int index = -1;
    for (auto obj : objs) {
        int idx;
        if (!obj || !Group.find(obj->getNameInDocument(), &idx) || index>=idx)
            continue;
        // Only consider dependency within the same sibling group, and use the
        // last feature for insertion point.
        if (std::find(siblings.begin(), siblings.end(), obj) != siblings.end())
            index = idx;
    }

    if (index < 0)
        return getNextSolidFeature();
    return getNextSolidFeature(Group.getValues()[index]);
}

App::DocumentObject *
Body::newObjectAt(const char *type,
                  const char *name,
                  const std::vector<App::DocumentObject *> &deps,
                  bool activate)
{
    if (!isAllowed(Base::Type::fromName(type)))
        FC_THROWM(Base::TypeError, "Type '" << (type?type:"?")  << "' is not allowed in Body");

    App::DocumentObject *obj = getDocument()->addObject(type, name, activate);
    if (!obj)
        FC_THROWM(Base::RuntimeError, "Failed to create object");

    if (deps.size() && obj->isDerivedFrom<DressUp>())
        insertObject(obj, deps.back(), true);
    else
        insertObject(obj, getInsertionPosition(deps));

    auto tip = Tip.getValue();
    if (!tip || !isSibling(obj, tip))
        setTip(obj);
    else {
        int i=-1, j=-1;
        Group.find(tip->getNameInDocument(), &i);
        Group.find(obj->getNameInDocument(), &j);
        if (i < j)
            setTip(obj);
    }
    auto siblings = getSiblings(obj);
    if (siblings.empty() || siblings.back() == obj)
        obj->Visibility.setValue(true);
    return obj;
}

void Body::insertObject(App::DocumentObject* feature, App::DocumentObject* target, bool after)
{
    if (target && !Group.find(target->getNameInDocument())) {
        THROWM(Base::ValueError, "Body: the feature we should insert relative to is not part of that body")
    }

    // The feature is in Group before it has its BaseFeature: the solids are
    // looked at when both are set
    SolidBodySyncGuard syncGuard(this);

    //ensure that all origin links are ok
    relinkToOrigin(feature);

    std::vector<App::DocumentObject*> model = Group.getValues();

    // Just incase the feature is already inside the model
    model.erase(std::remove(model.begin(), model.end(), feature), model.end());

    std::vector<App::DocumentObject*>::iterator insertInto;

    // Find out the position there to insert the feature
    if (!target) {
        if (after) {
            insertInto = model.begin();
        } else {
            insertInto = model.end();
        }
    } else {
        std::vector<App::DocumentObject*>::iterator targetIt = std::find (model.begin(), model.end(), target);
        assert (targetIt != model.end());
        if (after) {
            insertInto = targetIt + 1;
        } else {
            insertInto = targetIt;
        }
    }

    // Insert the new feature after the given
    model.insert (insertInto, feature);

    {
        // User3 is to skip GeoFeatureGroupExtension group check
        Base::ObjectStatusLocker<App::Property::Status, App::Property>
            guard(App::Property::User3, &Group);
        Group.setValues (model);
    }

    if(feature->isDerivedFrom<PartDesign::Feature>())
        static_cast<PartDesign::Feature*>(feature)->_Body.setValue(this);

    // Set the BaseFeature property
    setBaseProperty(feature);
}

void Body::setTip(App::DocumentObject *feature)
{
    // A solid body stands for its own tip feature: Tip is always a feature
    if (auto solid = Base::freecad_dynamic_cast<SolidBody>(feature)) {
        if (solid->getBody() != this)
            return;
        feature = solid->Tip.getValue();
        if (!feature && solid->Group.getSize())
            feature = solid->Group.getValues().back();
    }

    if (!feature || Tip.getValue() == feature
                 || !isSolidFeature(feature)
                 || !Group.find(feature->getNameInDocument()))
        return;

    Tip.setValue(feature);
}

void Body::setBaseProperty(App::DocumentObject* feature)
{
    int index;
    if (!feature || !this->Group.find(feature->getNameInDocument(), &index))
        THROWM(Base::RuntimeError, "Feature not found in body")

    if (feature->isDerivedFrom<Transformed>()) {
        for (auto obj : Group.getValues()) {
            if (obj->testStatus(App::ObjectStatus::ObjEditing)
                    && obj->isDerivedFrom<MultiTransform>())
                return;
        }
    }

    if (feature->isDerivedFrom<PartDesign::Feature>()
        && !static_cast<PartDesign::Feature*>(feature)->NewSolid.getValue()
        && Body::isSolidFeature(feature))
    {
        // Set BaseFeature property to previous feature (this might be the Tip feature)
        App::DocumentObject* prevSolidFeature = getPrevSolidFeature(feature);

        // NULL is ok here, it just means we made the current one fiature the base solid
        static_cast<PartDesign::Feature*>(feature)->BaseFeature.setValue(prevSolidFeature);

        // Set the next feature's base to this feature
        for (int i=index+1, count=Group.getSize(); i<count; ++i) {
            auto obj = Group.getValues()[i];
            if (!obj || !obj->isDerivedFrom<PartDesign::Feature>()
                     || !isSolidFeature(obj))
                continue;
            auto feat = static_cast<PartDesign::Feature*>(obj);
            if (!feat->NewSolid.getValue()
                    && feat->BaseFeature.getValue() == prevSolidFeature)
            {
                feat->BaseFeature.setValue(feature);
                break;
            }
        }
    }
}

std::vector<App::DocumentObject*> Body::removeObject(App::DocumentObject* feature)
{
    // This method must be called BEFORE the feature is removed from the Document!
    //
    // Everything here works on the feature's own solid group, i.e. its
    // BaseFeature chain -- deliberately NOT getPrevSolidFeature() /
    // getNextSolidFeature(), which scan Group by position and know nothing
    // about solid grouping. With more than one solid in a body those routinely
    // land in a different group: Group order interleaves the groups whenever
    // the user works on two solids in turn, and a chain can even run backwards
    // through it. Rerouting by position then hands a feature of one solid to
    // another, which raises no error and only shows up as wrong geometry at the
    // next recompute. setBaseProperty() already reroutes by matching on the
    // link; this is the counterpart for removal.
    //
    // The solid bodies are looked at once the reroute below is complete.
    SolidBodySyncGuard syncGuard(this);

    auto feat = Base::freecad_dynamic_cast<PartDesign::Feature>(feature);
    const bool headsOwnSolid = feat && feat->NewSolid.getValue();
    // Predecessor inside the same solid group; null when this feature heads one
    // (or is the body's first feature, which is equally fine -- its successor
    // just becomes the new base solid).
    App::DocumentObject* siblingBase =
        (feat && !headsOwnSolid) ? feat->BaseFeature.getValue() : nullptr;

    // The successors in this feature's own solid group are exactly those that
    // name it as their base. Usually one; a body with branched history can hold
    // several, and all of them need rerouting.
    std::vector<PartDesign::Feature*> siblingSuccessors;
    if (isSolidFeature(feature)) {
        for (auto obj : Group.getValues()) {
            auto next = Base::freecad_dynamic_cast<PartDesign::Feature>(obj);
            if (next && next != feature && next->BaseFeature.getValue() == feature) {
                siblingSuccessors.push_back(next);
            }
        }
    }

    for (auto next : siblingSuccessors) {
        if (headsOwnSolid) {
            // The deleted feature started this solid, so its successor takes
            // that role rather than being grafted onto a neighbouring solid.
            // Setting NewSolid clears BaseFeature -- see onNewSolidChanged().
            next->NewSolid.setValue(true);
        }
        else {
            // Before the reroute, while the removed feature's shape still
            // answers for the element names that pointed into it.
            next->onBaseFeatureRerouted(feature, siblingBase);
            next->BaseFeature.setValue(siblingBase);
        }
    }

    std::vector<App::DocumentObject*> model = Group.getValues();
    std::vector<App::DocumentObject*>::iterator it = std::find(model.begin(), model.end(), feature);

    // The tip of the feature's solid moves the same way, and takes the body's
    // with it when this solid is the body's tip (SolidBody::onChanged()).
    if (auto solid = getSolidBody(feature)) {
        if (solid->Tip.getValue() == feature) {
            App::DocumentObject* newTip = siblingBase;
            if (!newTip && !siblingSuccessors.empty()) {
                newTip = siblingSuccessors.front();
            }
            solid->Tip.setValue(newTip);
        }
    }

    // Adjust Tip feature if it is pointing to the deleted object, preferring
    // this feature's own solid group over whatever happens to sit beside it.
    if (Tip.getValue() == feature) {
        App::DocumentObject* newTip = siblingBase;
        if (!newTip && !siblingSuccessors.empty()) {
            newTip = siblingSuccessors.front();
        }
        if (!newTip) {
            // Last solid of its group: fall back to a positional neighbour, as
            // any remaining solid is a better tip than none.
            newTip = getPrevSolidFeature(feature);
            if (!newTip) {
                newTip = getNextSolidFeature(feature);
            }
            // The neighbour's solid becomes the body's tip where it is
            // rolled back to, not where the neighbour happens to be.
            auto solid = getSolidBody(newTip);
            if (solid && solid->hasFeature(solid->Tip.getValue())) {
                newTip = solid->Tip.getValue();
            }
        }
        Tip.setValue(newTip);
    }

    // Erase feature from Group
    if (it != model.end()) {
        model.erase(it);
        // User3 is to skip GeoFeatureGroupExtension group check
        Base::ObjectStatusLocker<App::Property::Status, App::Property>
            guard(App::Property::User3, &Group);
        Group.setValues(model);
    }
    std::vector<App::DocumentObject*> result = {feature};
    return result;
}


App::DocumentObjectExecReturn *Body::execute()
{
    Part::BodyBase::execute();

    // Whatever changed the solids without saying it was done: a script
    // setting BaseFeature, a feature removed from the document directly
    syncSolidBodies();

    App::DocumentObject* tip = Tip.getValue();
    if (!tip) {
        const auto &objs = Group.getValues();
        for (auto rit = objs.rbegin(); rit != objs.rend(); ++rit) {
            if (isSolidFeature(*rit)) {
                Tip.setValue(*rit);
                tip = *rit;
                break;
            }
        }
    }

    Part::TopoShape tipShape;
    if ( tip ) {
        if ( !isSolidFeature(tip)
                && !tip->isDerivedFrom<PartDesign::Feature>()) {
            return new App::DocumentObjectExecReturn (QT_TRANSLATE_NOOP("Exception", "Linked object is not a PartDesign feature" ));
        }

        // get the shape of the tip
        tipShape = static_cast<Part::Feature *>(tip)->Shape.getShape();

        if ( tipShape.getShape().IsNull () ) {
            return new App::DocumentObjectExecReturn (QT_TRANSLATE_NOOP("Exception", "Tip shape is empty" ));
        }

        // We should hide here the transformation of the baseFeature
        tipShape.transformShape (tipShape.getTransform(), true );

    } else {
        tipShape = Part::TopoShape();
    }

    Shape.setValue ( tipShape );
    return App::DocumentObject::StdReturn;

}

void Body::onSettingDocument() {

    Part::BodyBase::onSettingDocument();
}

bool Body::getChildDefaultExport(App::DocumentObject *, int) const
{
    return false;
}

const App::PropertyLinkList& Body::getExportGroupProperty(int reason) const
{
    return reason == GS_SELECT ? Group : _ExportChildren;
}

void Body::onChanged (const App::Property* prop) {
    // we neither load a project nor perform undo/redo
    if (!this->isRestoring()
            && this->getDocument()
            && !this->getDocument()->testStatus(App::Document::Restoring)
            && !this->getDocument()->isPerformingTransaction()) {
        if (prop == &BaseFeature) {
            FeatureBase* bf = nullptr;
            auto first = Group.getValues().empty() ? nullptr : Group.getValues().front();

            if (BaseFeature.getValue()
                    && !Group.find(BaseFeature.getValue()->getNameInDocument())) {
                //setup the FeatureBase if needed
                if (!first || (!first->isDerivedFrom<FeatureBase>()
                                && !first->isDerivedFrom<Part::SubShapeBinder>()))
                {
                    bf = static_cast<FeatureBase*>(getDocument()->addObject("PartDesign::FeatureBase", "BaseFeature"));
                    insertObject(bf, first, false);

                    if (!Tip.getValue())
                        Tip.setValue(bf);
                }
                else {
                    bf = Base::freecad_dynamic_cast<FeatureBase>(first);
                }
            }

            if (bf && (bf->BaseFeature.getValue() != BaseFeature.getValue())) {
                auto base = BaseFeature.getValue();
                bf->BaseFeature.setValue(base);
                // The recompute keeps the base's geometry but gives it the
                // FeatureBase's placement, so place that where the base is:
                // left at identity, a base away from the origin jumped to the
                // body's origin, and moved with the body's placement.
                if (base) {
                    bf->Placement.setValue(globalPlacement().inverse()
                                           * App::GeoFeature::getGlobalPlacement(base));
                }
            }
        }
        else if( prop == &Group ) {
            touchSolidBodies();
            //if the FeatureBase was deleted we set the BaseFeature link to nullptr
            if (BaseFeature.getValue() &&
               (Group.getValues().empty() ||
                (!Group.getValues().front()->isDerivedFrom<FeatureBase>()
                 && Group.getValues().front()!=BaseFeature.getValue())))
            {
                BaseFeature.setValue(nullptr);
            }
        }
        else if (prop == &AddSolidBody) {
            syncSolidBodies(true);
        }
        else if (prop == &ShapeMaterial) {
            // A body is one solid of one material (upstream 0804d80ebf). Its
            // features only: a sketch or a datum has no use for it, and
            // setting it touches them (upstream takes every Part::Feature)
            for (auto obj : Group.getValues()) {
                auto feature = dynamic_cast<PartDesign::Feature*>(obj);
                if (feature && feature->ShapeMaterial.getValue().getUUID()
                        != ShapeMaterial.getValue().getUUID())
                    feature->ShapeMaterial.setValue(ShapeMaterial.getValue());
            }
        }
        else if( prop == &SingleSolid ) {
            for(auto obj : Group.getValues()) {
                if(obj->isDerivedFrom<PartDesign::Feature>())
                    obj->touch();
            }
        }
        else if (prop == &Tip) {
            FC_TRACE("tip changed");
            auto tip = Tip.getValue();
            if (auto solid = Base::freecad_dynamic_cast<SolidBody>(tip)) {
                // A solid body given as the tip stands for its own tip
                // feature. Tip holds a feature always: that is what a build
                // without SolidBody reads, and it gets the same body shape.
                App::DocumentObject *feature = nullptr;
                if (solid->getBody() == this) {
                    feature = solid->Tip.getValue();
                    if (!feature && solid->Group.getSize())
                        feature = solid->Group.getValues().back();
                }
                Tip.setValue(feature);
            }
            else if (tip) {
                // The solid the tip is in becomes the body's tip, rolled
                // back to that feature
                auto solid = getSolidBody(tip);
                if (solid && solid->Tip.getValue() != tip)
                    solid->Tip.setValue(tip);
            }
        }
    }

    Part::BodyBase::onChanged(prop);
}

void Body::setupObject () {
    Part::BodyBase::setupObject ();
    AutoGroupSolids.setValue(Part::PartParams::getAutoGroupSolids());
    AddSolidBody.setValue(true);

    // make sure the origins are created
    getOrigin()->getX();
}

void Body::unsetupObject () {
    Part::BodyBase::unsetupObject ();
}

PyObject *Body::getPyObject()
{
    if (PythonObject.is(Py::_None())){
        // ref counter is set to 1
        PythonObject = Py::Object(new BodyPy(this),true);
    }
    return Py::new_reference_to(PythonObject);
}

std::vector<std::string> Body::getSubObjects(int reason) const {
    if (reason != GS_DEFAULT || ExportMode.getValue() == ExportByChildQuery)
        return Part::BodyBase::getSubObjects(reason);
    return {};
}

App::DocumentObject *Body::getSubObject(const char *subname,
        PyObject **pyObj, Base::Matrix4D *pmat, bool transform, int depth) const
{
    while(subname && *subname=='.') ++subname; // skip leading .

    // PartDesign::Feature now support grouping sibling features, and the user
    // is free to expand/collapse at any time. To not disrupt subname path
    // because of this, the body will peek the next two sub-objects reference,
    // and skip the first sub-object if possible.
    if(subname) {
        const char * firstDot = strchr(subname,'.');
        if (firstDot) {
            const char * secondDot = strchr(firstDot+1, '.');
            if (secondDot) {
                auto firstObj = Group.find(std::string(subname, firstDot).c_str());
                if (!firstObj || firstObj->isDerivedFrom<PartDesign::Feature>()) {
                    auto secondObj = Group.find(std::string(firstDot+1, secondDot).c_str());
                    if (secondObj) {
                        // we support only one level of sibling grouping, so no
                        // recursive call to our own getSubObject()
                        return Part::BodyBase::getSubObject(firstDot+1,pyObj,pmat,transform,depth+1);
                    }
                }
            }
        }
    }
#if 1
    return Part::BodyBase::getSubObject(subname,pyObj,pmat,transform,depth);
#else
    // The following code returns Body shape only if there is at least one
    // child visible in the body (when show through, not show tip). The
    // original intention is to sync visual to shape returned by
    // Part.getShape() when the body is included in some other group. But this
    // interfere with direct modeling using body shape. Therefore it is
    // disabled here.

    if(!pyObj || showTip ||
       (subname && !Data::ComplexGeoData::isMappedElement(subname) && strchr(subname,'.')))
        return Part::BodyBase::getSubObject(subname,pyObj,pmat,transform,depth);

    // We return the shape only if there are feature visible inside
    for(auto obj : Group.getValues()) {
        if(obj->Visibility.getValue() &&
           obj->isDerivedFrom(PartDesign::Feature>())
        {
            return Part::BodyBase::getSubObject(subname,pyObj,pmat,transform,depth);
        }
    }
    if(pmat && transform)
        *pmat *= Placement.getValue().toMatrix();
    return const_cast<Body*>(this);
#endif
}

void Body::onDocumentRestored()
{
    for(auto obj : Group.getValues()) {
        auto feature = Base::freecad_dynamic_cast<PartDesign::Feature>(obj);
        if (!feature)
            continue;

        feature->_Body.setValue(this);
    }
    _GroupTouched.setStatus(App::Property::Output,true);

    // trigger ViewProviderBody::copyColorsfromTip
    if (Tip.getValue())
        Tip.touch();

    // Checked at the next recompute; nothing is made while restoring
    if (AddSolidBody.getValue())
        touchSolidBodies();

    DocumentObject::onDocumentRestored();
}

std::vector<std::vector<App::DocumentObject*>> Body::getSolids() const
{
    std::vector<std::vector<App::DocumentObject*>> res;

    std::vector<App::DocumentObject*> features;
    std::set<App::DocumentObject*> featureSet;
    for (auto obj : Group.getValues()) {
        if (obj && obj->isAttachedToDocument() && isSolidFeature(obj)
                && featureSet.insert(obj).second)
            features.push_back(obj);
    }

    // A feature follows its BaseFeature; one whose base is not a solid
    // feature of this body starts a solid
    std::map<App::DocumentObject*, std::vector<App::DocumentObject*>> successors;
    std::vector<App::DocumentObject*> heads;
    for (auto obj : features) {
        App::DocumentObject *base = nullptr;
        if (auto feat = Base::freecad_dynamic_cast<PartDesign::Feature>(obj))
            base = feat->BaseFeature.getValue();
        if (base && base != obj && featureSet.count(base))
            successors[base].push_back(obj);
        else
            heads.push_back(obj);
    }

    std::set<App::DocumentObject*> visited;
    auto collect = [&](App::DocumentObject *head) {
        std::vector<App::DocumentObject*> solid;
        std::vector<App::DocumentObject*> stack{head};
        while (!stack.empty()) {
            auto obj = stack.back();
            stack.pop_back();
            if (!visited.insert(obj).second)
                continue;
            solid.push_back(obj);
            auto it = successors.find(obj);
            if (it != successors.end())
                stack.insert(stack.end(), it->second.rbegin(), it->second.rend());
        }
        if (!solid.empty())
            res.push_back(std::move(solid));
    };
    for (auto head : heads)
        collect(head);
    // Features left over base each other in a cycle. Still a solid to show.
    for (auto obj : features) {
        if (!visited.count(obj))
            collect(obj);
    }
    return res;
}

std::vector<SolidBody*> Body::getSolidBodies() const
{
    std::vector<SolidBody*> res;
    for (auto obj : Group.getValues()) {
        if (obj && obj->isAttachedToDocument() && obj->isDerivedFrom<SolidBody>())
            res.push_back(static_cast<SolidBody*>(obj));
    }
    return res;
}

SolidBody *Body::getSolidBody(const App::DocumentObject *feature) const
{
    if (!feature || !feature->isAttachedToDocument())
        return nullptr;
    for (auto obj : Group.getValues()) {
        if (obj && obj->isDerivedFrom<SolidBody>()
                && static_cast<SolidBody*>(obj)->hasFeature(feature))
            return static_cast<SolidBody*>(obj);
    }
    return nullptr;
}

SolidBody *Body::getTipSolidBody() const
{
    return getSolidBody(Tip.getValue());
}

Body::SolidBodySyncGuard::SolidBodySyncGuard(Body *body)
    : bodyT(body)
{
    if (body)
        ++body->solidBodySyncBlocked;
}

Body::SolidBodySyncGuard::~SolidBodySyncGuard()
{
    auto body = Base::freecad_dynamic_cast<Body>(bodyT.getObject());
    if (!body || --body->solidBodySyncBlocked > 0)
        return;
    try {
        body->syncSolidBodies();
    }
    catch (Base::Exception &e) {
        e.ReportException();
    }
    catch (...) {
        FC_ERR("Failed to update the solid bodies of " << body->getFullName());
    }
}

void Body::removeSolidBody(SolidBody *solid)
{
    if (!solid || !solid->isAttachedToDocument())
        return;
    // Emptied first, so that nothing done on the way out acts on features
    // it no longer has
    if (solid->Tip.getValue())
        solid->Tip.setValue(nullptr);
    if (solid->Group.getSize())
        solid->Group.setValues();
    getDocument()->removeObject(solid->getNameInDocument());
}

void Body::syncSolidBodies(bool force)
{
    if (force)
        touchSolidBodies();
    if (!solidBodyTouched || solidBodySyncing || solidBodySyncBlocked > 0)
        return;

    auto doc = getDocument();
    if (!doc || !isAttachedToDocument() || isRestoring()
            || testStatus(App::Remove)
            || doc->testStatus(App::Document::Restoring)
            || doc->isPerformingTransaction())
        return;

    // Not while one of the features is being edited: a 'New shape' box
    // ticked and unticked would leave a solid body behind for a solid that
    // was never made. Accepting the edit recomputes, and that comes here.
    for (auto obj : Group.getValues()) {
        if (obj && obj->testStatus(App::ObjEditing))
            return;
    }

    Base::StateLocker guard(solidBodySyncing);
    solidBodyTouched = false;

    // Taken by name: removing one may take others with it
    std::vector<App::DocumentObjectT> existing;
    for (auto solid : getSolidBodies())
        existing.emplace_back(solid);

    if (!AddSolidBody.getValue()) {
        // _Siblings is left as it is: it holds each solid grouped under its
        // last feature, which is the grouping this body goes back to
        for (auto &objT : existing)
            removeSolidBody(static_cast<SolidBody*>(objT.getObject()));
        solidBodyTouched = false;
        return;
    }

    const auto solids = getSolids();
    std::map<App::DocumentObject*, int> solidOf;
    std::map<App::DocumentObject*, int> orderOf;
    for (int i=0; i<(int)solids.size(); ++i) {
        int order = 0;
        for (auto obj : solids[i]) {
            solidOf[obj] = i;
            orderOf[obj] = order++;
        }
    }

    // A solid body stays with the solid holding the earliest of its features
    // still there. Where two ask for one solid -- two solids were joined --
    // the one whose feature comes first in it keeps it, and the other looks
    // for a solid holding a later feature of its own.
    std::vector<SolidBody*> assigned(solids.size(), nullptr);
    std::vector<SolidBody*> pending;
    for (auto &objT : existing) {
        auto solid = static_cast<SolidBody*>(objT.getObject());
        if (solid->_Body.getValue() != this)
            solid->_Body.setValue(this);
        pending.push_back(solid);
    }
    while (!pending.empty()) {
        // For each solid, the claimant whose earliest feature comes first
        std::map<int, std::pair<int, SolidBody*>> claims;
        std::vector<SolidBody*> losers;
        for (auto solid : pending) {
            bool claimed = false;
            for (auto obj : solid->Group.getValues()) {
                auto it = solidOf.find(obj);
                if (it == solidOf.end() || assigned[it->second])
                    continue;
                int order = orderOf[obj];
                auto res = claims.emplace(it->second, std::make_pair(order, solid));
                if (!res.second) {
                    if (res.first->second.first <= order) {
                        losers.push_back(solid);
                    }
                    else {
                        losers.push_back(res.first->second.second);
                        res.first->second = std::make_pair(order, solid);
                    }
                }
                claimed = true;
                break;
            }
            if (!claimed)
                removeSolidBody(solid);
        }
        for (auto &v : claims)
            assigned[v.first] = v.second.second;
        pending.swap(losers);
    }

    // Every solid gets one once there are two solids. A lone solid keeps the
    // one it has, and is given none.
    std::vector<SolidBody*> created;
    if (solids.size() > 1) {
        auto group = Group.getValues();
        for (int i=0; i<(int)solids.size(); ++i) {
            if (assigned[i])
                continue;
            // Not made the document's active object: that stays the feature
            // whose arrival made the second solid
            auto solid = new SolidBody();
            // Named Solid, Solid001: a path names an object with a dot behind
            // it (Body.Solid001.Face1), so it is never read as the element Solid1
            doc->addObject(solid, "Solid", false);
            solid->_Body.setValue(this);
            // Its features draw the solid; this object's shape is for referencing
            solid->Visibility.setValue(false);
            assigned[i] = solid;
            created.push_back(solid);
            // Behind the first feature of the solid, never in front of the
            // body's base feature
            auto it = std::find(group.begin(), group.end(), solids[i].front());
            group.insert(it == group.end() ? it : it+1, solid);
        }
        if (!created.empty()) {
            // User3 is to skip GeoFeatureGroupExtension group check
            Base::ObjectStatusLocker<App::Property::Status, App::Property>
                guard(App::Property::User3, &Group);
            Group.setValues(group);
        }
    }

    auto bodyTip = Tip.getValue();
    std::vector<SolidBody*> changed;
    for (int i=0; i<(int)solids.size(); ++i) {
        const auto &features = solids[i];
        auto solid = assigned[i];

        // What a build without SolidBody shows: the solid's features under
        // its last one, latest first. A solid with no solid body is not
        // grouped.
        PartDesign::Feature *last = nullptr;
        std::vector<App::DocumentObject*> siblings;
        if (solid) {
            for (auto rit = features.rbegin(); rit != features.rend(); ++rit) {
                auto feat = Base::freecad_dynamic_cast<PartDesign::Feature>(*rit);
                if (!feat)
                    continue;
                if (!last)
                    last = feat;
                else
                    siblings.push_back(feat);
            }
        }
        for (auto obj : features) {
            auto feat = Base::freecad_dynamic_cast<PartDesign::Feature>(obj);
            if (!feat)
                continue;
            if (feat == last) {
                if (feat->_Siblings.getValues() != siblings)
                    feat->_Siblings.setValues(siblings);
            }
            else if (feat->_Siblings.getSize())
                feat->_Siblings.setValues();
        }

        if (!solid)
            continue;

        bool touched = false;
        if (solid->Group.getValues() != features) {
            solid->Group.setValues(features);
            touched = true;
        }
        // The body's tip is the tip of its solid. Otherwise the solid stays
        // rolled back to where it was, or ends at its last feature.
        App::DocumentObject *tip = solid->Tip.getValue();
        if (std::find(features.begin(), features.end(), bodyTip) != features.end())
            tip = bodyTip;
        else if (std::find(features.begin(), features.end(), tip) == features.end())
            tip = features.back();
        if (solid->Tip.getValue() != tip) {
            solid->Tip.setValue(tip);
            touched = true;
        }
        if (touched)
            changed.push_back(solid);
    }

    // In a recompute these have had their turn already, or never get one
    if (doc->testStatus(App::Document::Recomputing)) {
        for (auto solid : changed) {
            std::unique_ptr<App::DocumentObjectExecReturn> ret(solid->updateShape());
            solid->purgeTouched();
        }
    }

    // The changes made here are not news
    solidBodyTouched = false;
}

Body::Relation
Body::getRelation(const App::DocumentObject *obj, const App::DocumentObject *other) const
{
    if (!obj || !obj->isAttachedToDocument() || !other || !other->isAttachedToDocument())
        return RelationStranger;

    if (obj == other)
        return RelationSibling;

    int index;
    if (!Group.find(obj->getNameInDocument(), &index))
        return RelationStranger;

    int otherIndex;
    if (!Group.find(other->getNameInDocument(), &otherIndex))
        return RelationStranger;
    
    if (index < otherIndex) {
        std::swap(obj, other);
        std::swap(index, otherIndex);
    }

    if (isSolidFeature(obj)) {
        auto feature = Base::freecad_dynamic_cast<const PartDesign::Feature>(obj);
        std::set<App::DocumentObject*> objset; // to prevent live-lock in case there is cycle
        while (feature) {
            auto base = feature->BaseFeature.getValue();
            if (base == other)
                return RelationSibling;
            if (!base)
                break;
            if (!isSolidFeature(base) || !objset.insert(base).second)
                break;
            feature = Base::freecad_dynamic_cast<PartDesign::Feature>(base);
        }
    }
    return RelationCousin;
}

std::deque<App::DocumentObject*>
Body::getSiblings(App::DocumentObject *obj, bool all, bool reversed) const
{
    std::deque<App::DocumentObject *> res;
    if (!obj || !obj->isAttachedToDocument()
             || !obj->isDerivedFrom<PartDesign::Feature>())
        return res;

    int index = -1;
    if (!Group.find(obj->getNameInDocument(), &index))
        return res;

    if (isSolidFeature(obj)) {
        auto feature = static_cast<PartDesign::Feature*>(obj);
        while (feature) {
            auto base = feature->BaseFeature.getValue();
            if (!base || !Group.find(base->getNameInDocument()))
                break;
            if (reversed)
                res.push_back(base);
            else
                res.push_front(base);
            if (res.size() >= Group.getValues().size())
                break;
            if (!isSolidFeature(base))
                break;
            feature = Base::freecad_dynamic_cast<PartDesign::Feature>(base);
        }
        if (!all)
            return res;
        if (reversed)
            res.push_front(obj);
        else
            res.push_back(obj);
        if (res.size() >= Group.getValues().size()) {
            // This means cyclic dependency actually
            return res;
        }
    }

    const auto & objs = Group.getValues();
    for (int count=Group.getSize(); index<count; ++index) {
        App::DocumentObject * o = objs[index];
        if(!o || !o->isAttachedToDocument()
                || !o->isDerivedFrom<PartDesign::Feature>()
                || !isSolidFeature(o))
            continue;

        if (static_cast<PartDesign::Feature*>(o)->BaseFeature.getValue() == obj) {
            obj = o;
            if (reversed)
                res.push_front(o);
            else
                res.push_back(o);
        }
    }
    return res;
}

int Body::isElementVisible(const char *element) const
{
    auto obj = Group.find(element);
    if (obj && obj->isDerivedFrom<AuxGroup>()) {
        auto group = static_cast<AuxGroup*>(obj);
        for (auto child : group->Group.getValues()) {
            if (child->Visibility.getValue())
                return 1;
        }
        return 0;
    }
    if (obj && obj->isDerivedFrom<SolidBody>()) {
        // A solid is seen through one of its features, or through the solid
        // body's own shape when that was asked for
        auto solid = static_cast<SolidBody*>(obj);
        if (solid->Visibility.getValue())
            return 1;
        for (auto child : solid->Group.getValues()) {
            if (child->Visibility.getValue())
                return 1;
        }
        return 0;
    }
    return -1;
}

int Body::setElementVisible(const char *element, bool visible)
{
    auto obj = Group.find(element);
    if (obj && obj->isDerivedFrom<AuxGroup>()) {
        auto group = static_cast<AuxGroup*>(obj);
        for (auto child : group->Group.getValues())
            child->Visibility.setValue(visible);
        return 1;
    }
    if (obj && obj->isDerivedFrom<SolidBody>()) {
        auto solid = static_cast<SolidBody*>(obj);
        if (!visible) {
            if (solid->Visibility.getValue())
                solid->Visibility.setValue(false);
            for (auto child : solid->Group.getValues()) {
                if (child->Visibility.getValue())
                    child->Visibility.setValue(false);
            }
        }
        else if (isElementVisible(element) == 0) {
            // Showing a solid shows the feature it is rolled back to
            auto tip = solid->Tip.getValue();
            if (!tip && solid->Group.getSize())
                tip = solid->Group.getValues().back();
            if (tip)
                tip->Visibility.setValue(true);
        }
        return 1;
    }
    return -1;
}

// a body is solid if it has features that are solid
bool Body::isSolid()
{
    std::vector<App::DocumentObject *> features = getFullModel();
    for (auto feature : features){
        if (isSolidFeature(feature))
            return true;
    }
    return false;
}
