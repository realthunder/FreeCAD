/****************************************************************************
 *   Copyright (c) 2017 Zheng Lei (realthunder) <realthunder.dev@gmail.com> *
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

#include <boost/range.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/preprocessor/stringize.hpp>
#include <boost/range.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <Base/Console.h>
#include <Base/Tools.h>
#include <Base/Uuid.h>

#include "Application.h"
#include "ElementNamingUtils.h"
#include "ComplexGeoDataPy.h"
#include "Document.h"
#include "DocumentObserver.h"
#include "GeoFeatureGroupExtension.h"
#include "Link.h"
#include "LinkBaseExtensionPy.h"

FC_LOG_LEVEL_INIT("App::Link", true,true)

using namespace App;
using namespace Base;
namespace sp = std::placeholders;

using CharRange = boost::iterator_range<const char*>;

////////////////////////////////////////////////////////////////////////

EXTENSION_PROPERTY_SOURCE(App::LinkBaseExtension, App::DocumentObjectExtension)

LinkBaseExtension::LinkBaseExtension()
{
    initExtensionType(LinkBaseExtension::getExtensionClassTypeId());
    EXTENSION_ADD_PROPERTY_TYPE(_LinkTouched, (false), " Link",
            PropertyType(Prop_Hidden|Prop_NoPersist),0);
    EXTENSION_ADD_PROPERTY_TYPE(_ChildCache, (), " Link",
            PropertyType(Prop_Hidden|Prop_NoPersist|Prop_ReadOnly),0);
    _ChildCache.setScope(LinkScope::Global);
    EXTENSION_ADD_PROPERTY_TYPE(_LinkVersion, (0), " Link", 
            PropertyType(Prop_Hidden|Prop_ReadOnly),0);
    EXTENSION_ADD_PROPERTY_TYPE(_LinkOwner, (0), " Link",
            PropertyType(Prop_Hidden|Prop_Output),0);
    props.resize(PropMax,nullptr);
}

PyObject* LinkBaseExtension::getExtensionPyObject() {
    if (ExtensionPythonObject.is(Py::_None())){
        // ref counter is set to 1
        ExtensionPythonObject = Py::Object(new LinkBaseExtensionPy(this),true);
    }
    return Py::new_reference_to(ExtensionPythonObject);
}

/*[[[cog
import Link
Link.define_link_base_extension()
]]]*/

// Auto generated code (App/Link.py:196)
const std::vector<LinkBaseExtension::PropInfo> &
LinkBaseExtension::getPropertyInfo()
{
    static std::vector<PropInfo> PropsInfo = {
        {PropIndex::PropLinkPlacement, "LinkPlacement", App::PropertyPlacement::getClassTypeId(),
            "Link placement"},
        {PropIndex::PropPlacement, "Placement", App::PropertyPlacement::getClassTypeId(),
            "Alias to LinkPlacement to make the link object compatibale with other objects"},
        {PropIndex::PropLinkedObject, "LinkedObject", App::PropertyLink::getClassTypeId(),
            "Linked object"},
        {PropIndex::PropLinkTransform, "LinkTransform", App::PropertyBool::getClassTypeId(),
            "Set to false to override linked object's placement"},
        {PropIndex::PropLinkClaimChild, "LinkClaimChild", App::PropertyBool::getClassTypeId(),
            "Claim the linked object as a child"},
        {PropIndex::PropLinkCopyOnChange, "LinkCopyOnChange", App::PropertyEnumeration::getClassTypeId(),
            "Disabled: disable copy on change\n"
            "Enabled: enable copy linked object on change of any of its property marked as CopyOnChange\n"
            "Owned: force copy of the linked object (if it has not done so) and take the ownership of the copy\n"
            "Tracking: enable copy on change and auto synchronization of changes in the source object."},
        {PropIndex::PropLinkCopyOnChangeSource, "LinkCopyOnChangeSource", App::PropertyLink::getClassTypeId(),
            "The copy on change source object"},
        {PropIndex::PropLinkCopyOnChangeGroup, "LinkCopyOnChangeGroup", App::PropertyLink::getClassTypeId(),
            "Linked to a internal group object for holding on change copies"},
        {PropIndex::PropLinkCopyOnChangeTouched, "LinkCopyOnChangeTouched", App::PropertyBool::getClassTypeId(),
            "Indicating the copy on change source object has been changed"},
        {PropIndex::PropSyncGroupVisibility, "SyncGroupVisibility", App::PropertyBool::getClassTypeId(),
            "Set to false to override (nested) child visibility when linked to a plain group"},
        {PropIndex::PropScale, "Scale", App::PropertyFloat::getClassTypeId(),
            "Scale factor"},
        {PropIndex::PropScaleVector, "ScaleVector", App::PropertyVector::getClassTypeId(),
            "Scale vector for non-uniform scaling. Please be aware that the underlying\n"
            "geometry may be transformed into BSpline surface due to non-uniform scale."},
        {PropIndex::PropMatrix, "Matrix", App::PropertyMatrix::getClassTypeId(),
            "Matrix transformation for the linked object. The transformation is applied\n"
            "before scale and placement."},
        {PropIndex::PropPlacementList, "PlacementList", App::PropertyPlacementList::getClassTypeId(),
            "The placement for each element in a link array"},
        {PropIndex::PropAutoPlacement, "AutoPlacement", App::PropertyBool::getClassTypeId(),
            "Enable auto placement of newly created array element"},
        {PropIndex::PropScaleList, "ScaleList", App::PropertyVectorList::getClassTypeId(),
            "The scale factors for each element in a link array"},
        {PropIndex::PropMatrixList, "MatrixList", App::PropertyMatrixList::getClassTypeId(),
            "Matrix transofmration of each element in a link array.\n"
            "The transformation is applied before scale and placement."},
        {PropIndex::PropVisibilityList, "VisibilityList", App::PropertyBoolList::getClassTypeId(),
            "The visibility state of element in a link arrayement"},
        {PropIndex::PropElementCount, "ElementCount", App::PropertyInteger::getClassTypeId(),
            "Link element count"},
        {PropIndex::PropElementList, "ElementList", App::PropertyLinkList::getClassTypeId(),
            "The link element object list"},
        {PropIndex::PropShowElement, "ShowElement", App::PropertyBool::getClassTypeId(),
            "Enable link element list"},
        {PropIndex::PropAutoLinkLabel, "AutoLinkLabel", App::PropertyBool::getClassTypeId(),
            "Enable link auto label according to linked object"},
        {PropIndex::PropLinkMode, "LinkMode", App::PropertyEnumeration::getClassTypeId(),
            "Link group mode"},
        {PropIndex::PropLinkExecute, "LinkExecute", App::PropertyString::getClassTypeId(),
            "Link execute function. Default to 'appLinkExecute'. 'None' to disable."},
        {PropIndex::PropColoredElements, "ColoredElements", App::PropertyLinkSubHidden::getClassTypeId(),
            "Link colored elements"},
    };
    return PropsInfo;
}
//[[[end]]]

const LinkBaseExtension::PropInfoMap &
LinkBaseExtension::getPropertyInfoMap()
{
    static PropInfoMap PropsMap;
    if(PropsMap.empty()) {
        const auto &infos = getPropertyInfo();
        for(const auto &info : infos)
            PropsMap[info.name] = info;
    }
    return PropsMap;
}

Property *LinkBaseExtension::getProperty(int idx) {
    if(idx>=0 && idx<(int)props.size())
        return props[idx];
    return nullptr;
}

Property *LinkBaseExtension::getProperty(const char *name) {
    const auto &info = getPropertyInfoMap();
    auto it = info.find(name);
    if(it == info.end())
        return nullptr;
    return getProperty(it->second.index);
}

void LinkBaseExtension::setProperty(int idx, Property *prop) {
    const auto &infos = getPropertyInfo();
    if(idx<0 || idx>=(int)infos.size())
        LINK_THROW(Base::RuntimeError,"App::LinkBaseExtension: property index out of range");

    if(props[idx]) {
        props[idx]->setStatus(Property::LockDynamic,false);
        props[idx] = nullptr;
    }
    if(!prop)
        return;
    if(!prop->isDerivedFrom(infos[idx].type)) {
        std::ostringstream str;
        str << "App::LinkBaseExtension: expected property type '" <<
            infos[idx].type.getName() << "', instead of '" <<
            prop->getClassTypeId().getName() << "'";
        LINK_THROW(Base::TypeError,str.str().c_str());
    }

    props[idx] = prop;
    props[idx]->setStatus(Property::LockDynamic,true);

    switch(idx) {
    case PropScaleVector:
        if (!GetApplication().isRestoring() && LinkParams::getHideScaleVector())
            prop->setStatus(Property::Hidden, true);
        break;
    case PropLinkMode: {
        static const char *linkModeEnums[] = {"None","Auto Delete","Auto Link","Auto Unlink",nullptr};
        auto propLinkMode = static_cast<PropertyEnumeration*>(prop);
        if(!propLinkMode->hasEnums())
            propLinkMode->setEnums(linkModeEnums);
        break;
    }
    case PropLinkCopyOnChange: {
        static const char *enums[] = {"Disabled","Enabled","Owned","Tracking",nullptr};
        auto propEnum = static_cast<PropertyEnumeration*>(prop);
        if(!propEnum->hasEnums())
            propEnum->setEnums(enums);
        break;
    }
    case PropLinkCopyOnChangeTouched:
    case PropLinkCopyOnChangeSource:
    case PropLinkCopyOnChangeGroup:
        prop->setStatus(Property::Hidden, true);
        break;
    case PropLinkTransform:
    case PropLinkPlacement:
    case PropPlacement:
        if(getLinkTransformProperty() &&
           getLinkPlacementProperty() &&
           getPlacementProperty())
        {
            bool transform = getLinkTransformValue();
            getPlacementProperty()->setStatus(Property::Hidden, transform);
            getLinkPlacementProperty()->setStatus(Property::Hidden, !transform);
        }
        break;
    case PropElementList:
        getElementListProperty()->setScope(LinkScope::Global);
        getElementListProperty()->setStatus(Property::Hidden, true);
        // fall through
    case PropLinkedObject:
        // Make ElementList as read-only if we are not a group (i.e. having
        // LinkedObject property), because it is for holding array elements.
        if(getElementListProperty())
            getElementListProperty()->setStatus(
                Property::Immutable, getLinkedObjectProperty() != nullptr);
        break;
    case PropVisibilityList:
        getVisibilityListProperty()->setStatus(Property::Immutable, true);
        getVisibilityListProperty()->setStatus(Property::Hidden, true);
        break;
    }

    if(FC_LOG_INSTANCE.isEnabled(FC_LOGLEVEL_TRACE)) {
        const char *propName;
        if(!prop)
            propName = "<null>";
        else if(prop->getContainer())
            propName = prop->getName();
        else
            propName = extensionGetPropertyName(prop);
        if(!Property::isValidName(propName))
            propName = "?";
        FC_TRACE("set property " << infos[idx].name << ": " << propName);
    }
}

static const char _GroupPrefix[] = "Configuration (";

App::DocumentObjectExecReturn *LinkBaseExtension::extensionExecute() {
    // The actual value of LinkTouched is not important, just to notify view
    // provider that the link (in fact, its dependents, i.e. linked ones) have
    // recomputed.
    _LinkTouched.touch();

    if(getLinkedObjectProperty()) {
        DocumentObject *linked = getTrueLinkedObject(true);
        if(!linked) {
            std::ostringstream ss;
            ss << "Link broken!";
            auto xlink = Base::freecad_dynamic_cast<PropertyXLink>(
                    getLinkedObjectProperty());
            if (xlink) {
                const char *objname = xlink->getObjectName();
                if (objname && objname[0])
                    ss << "\nObject: " << objname;
                const char *filename = xlink->getFilePath();
                if (filename && filename[0])
                    ss << "\nFile: " << filename;
            }
            return new App::DocumentObjectExecReturn(ss.str().c_str());
        }

        App::DocumentObject *container = getContainer();
        auto source = getLinkCopyOnChangeSourceValue();
        if (source && getLinkCopyOnChangeValue() == CopyOnChangeTracking
                   && getLinkCopyOnChangeTouchedValue())
        {
            syncCopyOnChange();
        }

        PropertyPythonObject *proxy = nullptr;
        if(getLinkExecuteProperty()
                && !boost::iequals(getLinkExecuteValue(), "none")
                && (!_LinkOwner.getValue()
                    || !container->getDocument()->getObjectByID(_LinkOwner.getValue())))
        {
            // Check if this is an element link. Do not invoke appLinkExecute()
            // if so, because it will be called from the link array.
            proxy = Base::freecad_dynamic_cast<PropertyPythonObject>(
                    linked->getPropertyByName("Proxy"));
        }
        if(proxy) {
            Base::PyGILStateLocker lock;
            const char *errMsg = "Linked proxy execute failed";
            try {
                Py::Tuple args(3);
                Py::Object proxyValue = proxy->getValue();
                const char *method = getLinkExecuteValue();
                if(!method || !method[0])
                    method = "appLinkExecute";
                if(proxyValue.hasAttr(method)) {
                    Py::Object attr = proxyValue.getAttr(method);
                    if(attr.ptr() && attr.isCallable()) {
                        Py::Tuple args(4);
                        args.setItem(0, Py::asObject(linked->getPyObject()));
                        args.setItem(1, Py::asObject(container->getPyObject()));
                        if(!_getElementCountValue()) {
                            Py::Callable(attr).apply(args);
                        } else {
                            const auto &elements = _getElementListValue();
                            for(int i=0; i<_getElementCountValue(); ++i) {
                                args.setItem(2, Py::Int(i));
                                if(i < (int)elements.size())
                                    args.setItem(3, Py::asObject(elements[i]->getPyObject()));
                                else
                                    args.setItem(3, Py::Object());
                                Py::Callable(attr).apply(args);
                            }
                        }
                    }
                }
            } catch (Py::Exception &) {
                Base::PyException e;
                e.ReportException();
                return new App::DocumentObjectExecReturn(errMsg);
            } catch (Base::Exception &e) {
                e.ReportException();
                return new App::DocumentObjectExecReturn(errMsg);
            }
        }

        auto parent = getContainer();
        setupCopyOnChange(parent);

        if(hasCopyOnChange && getLinkCopyOnChangeValue()==CopyOnChangeDisabled) {
            hasCopyOnChange = false;
            std::vector<Property*> props;
            parent->getPropertyList(props);
            for(auto prop : props) {
                if(isCopyOnChangeProperty(parent, *prop)) {
                    try {
                        parent->removeDynamicProperty(prop->getName());
                    } catch (Base::Exception &e) {
                        e.ReportException();
                    } catch (...) {
                    }
                }
            }
        }
    }
    return inherited::extensionExecute();
}

short LinkBaseExtension::extensionMustExecute() {
    auto link = getLink();
    if(!link)
        return 0;
    return link->mustExecute();
}

std::vector<App::DocumentObject*>
LinkBaseExtension::getOnChangeCopyObjects(
        std::vector<App::DocumentObject *> *excludes,
        App::DocumentObject *src)
{
    auto parent = getContainer();
    if (!src)
        src = getLinkCopyOnChangeSourceValue();
    if (!src || getLinkCopyOnChangeValue() == CopyOnChangeDisabled)
        return {};

    auto res = Document::getDependencyList({src}, Document::DepSort);
    for (auto it=res.begin(); it!=res.end();) {
        auto obj = *it;
        if (obj == src) {
            ++it;
            continue;
        }
        auto prop = Base::freecad_dynamic_cast<PropertyMap>(
                obj->getPropertyByName("_CopyOnChangeControl"));
        static std::map<std::string, std::string> dummy;
        const auto & map = prop && prop->getContainer()==obj ? prop->getValues() : dummy;
        const char *v = "";
        if (src->getDocument() != obj->getDocument())
            v = "-";
        auto iter = map.find("*");
        if (iter != map.end())
            v = iter->second.c_str();
        else if ((iter = map.find(parent->getNameInDocument())) != map.end())
            v = iter->second.c_str();
        if (boost::equals(v, "-")) {
            if (excludes)
                excludes->push_back(obj);
            else {
                it = res.erase(it);
                continue;
            }
        }
        ++it;
    }
    return res;
}

void LinkBaseExtension::setOnChangeCopyObject(
        App::DocumentObject *obj, OnChangeCopyOptions options)
{
    auto parent = getContainer();
    Base::Flags<OnChangeCopyOptions> flags(options);
    bool exclude = flags.testFlag(OnChangeCopyOptions::Exclude);
    bool external = parent->getDocument() != obj->getDocument();
    auto prop = Base::freecad_dynamic_cast<PropertyMap>(
            obj->getPropertyByName("_CopyOnChangeControl"));

    if (external == exclude && !prop)
        return;

    if (!prop) {
        try {
            prop = static_cast<PropertyMap*>(
                    obj->addDynamicProperty("App::PropertyMap", "_CopyOnChangeControl"));
        } catch (Base::Exception &e) {
            e.ReportException();
        }
        if (!prop) {
            FC_ERR("Failed to setup copy on change object " << obj->getFullName());
            return;
        }
    }

    const char *key = flags.testFlag(OnChangeCopyOptions::ApplyAll) ? "*" : parent->getNameInDocument();
    if (external)
        prop->setValue(key, exclude ? nullptr : "+");
    else
        prop->setValue(key, exclude ? "-" : nullptr);
}

// The purpose of this function is to synchronize the mutated copy to the
// original linked CopyOnChange object. It will make a new copy if any of the
// non-CopyOnChange property of the original object has changed.
void LinkBaseExtension::syncCopyOnChange()
{
    if (!getLinkCopyOnChangeValue())
        return;
    auto linkProp = getLinkedObjectProperty();
    auto srcProp = getLinkCopyOnChangeSourceProperty();
    auto srcTouched = getLinkCopyOnChangeTouchedProperty();
    if (!linkProp
            || !srcProp
            || !srcTouched
            || !srcProp->getValue()
            || !linkProp->getValue()
            || srcProp->getValue() == linkProp->getValue())
        return;

    auto parent = getContainer();

    auto linked = linkProp->getValue();

    std::vector<App::DocumentObjectT> oldObjs;
    std::vector<App::DocumentObject*> objs;

    // CopyOnChangeGroup is a hidden dynamic property for holding a LinkGroup
    // for holding the mutated copy of the original linked object and all its
    // dependencies.
    LinkGroup *copyOnChangeGroup = nullptr;
    if (auto prop = getLinkCopyOnChangeGroupProperty()) {
        copyOnChangeGroup = Base::freecad_dynamic_cast<LinkGroup>(prop->getValue());
        if (!copyOnChangeGroup) {
            // Create the LinkGroup if not exist
            auto group = new LinkGroup;
            group->LinkMode.setValue(LinkModeAutoDelete);
            parent->getDocument()->addObject(group, "CopyOnChangeGroup");
            prop->setValue(group);
        } else {
            // If it exists, then obtain all copied objects. Note that we stores
            // the dynamic property _SourceUUID in oldObjs if possible, in order
            // to match the possible new copy later.
            objs = copyOnChangeGroup->ElementList.getValues();
            for (auto obj : objs) {
                if (!obj->isAttachedToDocument())
                    continue;
                auto prop = Base::freecad_dynamic_cast<PropertyUUID>(
                        obj->getPropertyByName("_SourceUUID"));
                if (prop && prop->getContainer() == obj)
                    oldObjs.emplace_back(prop);
                else
                    oldObjs.emplace_back(obj);
            }
            std::sort(objs.begin(), objs.end());
        }
    }

    // Obtain the original linked object and its dependency in depending order.
    // The last being the original linked object.
    auto srcObjs = getOnChangeCopyObjects();
    // Refresh signal connection to monitor changes
    monitorOnChangeCopyObjects(srcObjs);

    // Copy the objects. Document::export/importObjects() (called by
    // copyObject()) will generate a _ObjectUUID for each source object and
    // match it with a _SourceUUID in the copy.
    auto copiedObjs = parent->getDocument()->copyObject(srcObjs);
    if(copiedObjs.empty())
        return;

    // copyObject() will return copy in order of the same order of the input,
    // so the last object will be the copy of the original linked object
    auto newLinked = copiedObjs.back();

    // We are copying from the original linked object and we've already mutated
    // it, so we need to copy all CopyOnChange properties from the mutated
    // object to the new copy.
    std::vector<App::Property*> propList;
    linked->getPropertyList(propList);
    for (auto prop : propList) {
        if(!prop->testStatus(Property::CopyOnChange)
                || prop->getContainer()!=linked)
            continue;
        auto p = newLinked->getPropertyByName(prop->getName());
        if (p && p->getTypeId() == prop->getTypeId()) {
            std::unique_ptr<Property> pCopy(prop->Copy());
            p->Paste(*pCopy);
        }
    }

    if (copyOnChangeGroup) {
        // The order of the copied objects is in dependency order (because of
        // getOnChangeCopyObjects()). We reverse it here so that we can later
        // on delete it in reverse order to avoid error (because some parent
        // objects may want to delete their own children).
        std::reverse(copiedObjs.begin(), copiedObjs.end());
        copyOnChangeGroup->ElementList.setValues(copiedObjs);
    }

    // Create a map to find the corresponding replacement of the new copies to
    // the mutated object. The reason for doing so is that we are copying from
    // the original linked object and its dependency, not the mutated objects
    // which are old copies. There could be arbitrary changes in the originals
    // which may add or remove or change depending orders, while the
    // replacement happens between the new and old copies.

    std::map<Base::Uuid, App::DocumentObjectT> newObjs;
    for (auto obj : copiedObjs) {
        auto prop = Base::freecad_dynamic_cast<PropertyUUID>(
                obj->getPropertyByName("_SourceUUID"));
        if (prop)
            newObjs.insert(std::make_pair(prop->getValue(), obj));
    }

    std::vector<std::pair<App::DocumentObject*, App::DocumentObject*> > replacements;
    for (const auto &objT : oldObjs) {
        auto prop = Base::freecad_dynamic_cast<PropertyUUID>(objT.getProperty());
        if (!prop)
            continue;
        auto it = newObjs.find(prop->getValue());
        if (it == newObjs.end())
            continue;
        auto oldObj = objT.getObject();
        auto newObj = it->second.getObject();
        if (oldObj && newObj)
            replacements.emplace_back(oldObj, newObj);
    }

    std::vector<std::pair<App::DocumentObjectT, std::unique_ptr<App::Property> > > propChanges;
    if (!replacements.empty()) {
        std::sort(copiedObjs.begin(), copiedObjs.end());

        // Global search for links affected by the replacement. We accumulate
        // the changes in propChanges without applying, in order to avoid any
        // side effect of changing while searching.
        for(auto doc : App::GetApplication().getDocuments()) {
            for(auto o : doc->getObjects()) {
                if (o == parent
                        || std::binary_search(objs.begin(), objs.end(), o)
                        || std::binary_search(copiedObjs.begin(), copiedObjs.end(), o))
                    continue;
                propList.clear();
                o->getPropertyList(propList);
                for(auto prop : propList) {
                    if (prop->getContainer() != o)
                        continue;
                    auto linkProp = Base::freecad_dynamic_cast<App::PropertyLinkBase>(prop);
                    if(!linkProp)
                        continue;
                    for (const auto &v : replacements) {
                        std::unique_ptr<App::Property> copy(
                                linkProp->CopyOnLinkReplace(parent,v.first,v.second));
                        if(!copy)
                            continue;
                        propChanges.emplace_back(App::DocumentObjectT(prop),std::move(copy));
                    }
                }
            }
        }
    }

    Base::StateLocker guard(pauseCopyOnChange);
    linkProp->setValue(newLinked);
    newLinked->Visibility.setValue(false);
    srcTouched->setValue(false);

    // Apply the global link changes.
    for(const auto &v : propChanges) {
        auto prop = v.first.getProperty();
        if(prop)
            prop->Paste(*v.second.get());
    }

    // Finally, remove all old copies. oldObjs stores type of DocumentObjectT
    // which stores the object name. Before removing, we need to make sure the
    // object exists, and it is not some new object with the same name, by
    // checking objs which stores DocumentObject pointer.
    for (const auto &objT : oldObjs) {
        auto obj = objT.getObject();
        if (obj && std::binary_search(objs.begin(), objs.end(), obj))
            obj->getDocument()->removeObject(obj->getNameInDocument());
    }
}

bool LinkBaseExtension::isLinkedToConfigurableObject() const
{
    if (auto linked = getLinkedObjectValue()) {
        std::vector<App::Property*> propList;
        linked->getPropertyList(propList);
        for (auto prop : propList) {
            if(prop->testStatus(Property::CopyOnChange)
                    && prop->getContainer()==linked)
                return true;
        }
    }
    return false;
}

bool LinkBaseExtension::isCopyOnChangeProperty(DocumentObject *obj, const App::Property &prop) {
    if(obj!=prop.getContainer() || !prop.testStatus(App::Property::PropDynamic))
        return false;
    auto group = prop.getGroup();
    return group && boost::starts_with(group,_GroupPrefix);
}

void LinkBaseExtension::setupCopyOnChange(DocumentObject *parent, bool checkSource) {
    copyOnChangeConns.clear();
    copyOnChangeSrcConns.clear();

    auto linked = getTrueLinkedObject(false);
    if(!linked || getLinkCopyOnChangeValue()==CopyOnChangeDisabled)
        return;

    if (checkSource && !pauseCopyOnChange) {
        PropertyLink *source = getLinkCopyOnChangeSourceProperty();
        if (source) {
            source->setValue(linked);
            if (auto touched = getLinkCopyOnChangeTouchedProperty())
                touched->setValue(false);
        }
    }

    hasCopyOnChange = setupCopyOnChange(parent,linked,&copyOnChangeConns,hasCopyOnChange);
    if (hasCopyOnChange && getLinkCopyOnChangeValue() == CopyOnChangeOwned
            && getLinkedObjectValue()
            && getLinkedObjectValue() == getLinkCopyOnChangeSourceValue())
    {
        makeCopyOnChange();
    }
}

bool LinkBaseExtension::setupCopyOnChange(DocumentObject *parent, DocumentObject *linked,
        std::vector<boost::signals2::scoped_connection> *copyOnChangeConns, bool checkExisting)
{
    if(!parent || !linked)
        return false;

    bool res = false;

    std::unordered_map<Property*, Property*> newProps;
    std::vector<Property*> props;
    linked->getPropertyList(props);
    for(auto prop : props) {
        if(!prop->testStatus(Property::CopyOnChange)
                || prop->getContainer()!=linked)
            continue;

        res = true;

        const char* linkedGroupName = prop->getGroup();
        if(!linkedGroupName || !linkedGroupName[0])
            linkedGroupName = "Base";

        std::string groupName;
        groupName = _GroupPrefix;
        if(boost::starts_with(linkedGroupName,_GroupPrefix))
            groupName += linkedGroupName + sizeof(_GroupPrefix)-1;
        else {
            groupName += linkedGroupName;
            groupName += ")";
        }

        auto p = parent->getPropertyByName(prop->getName());
        if(p) {
            if(p->getContainer()!=parent)
                p = nullptr;
            else {
                const char* otherGroupName = p->getGroup();
                if(!otherGroupName || !boost::starts_with(otherGroupName, _GroupPrefix)) {
                    FC_WARN(p->getFullName() << " shadows another CopyOnChange property "
                            << prop->getFullName());
                    continue;
                }
                if(p->getTypeId() != prop->getTypeId() || groupName != otherGroupName) {
                    parent->removeDynamicProperty(p->getName());
                    p = nullptr;
                }
            }
        }
        if(!p) {
            p = parent->addDynamicProperty(prop->getTypeId().getName(),
                    prop->getName(), groupName.c_str(), prop->getDocumentation());
            std::unique_ptr<Property> pcopy(prop->Copy());
            Base::ObjectStatusLocker<Property::Status,Property> guard(Property::User3, p);
            if(pcopy) {
                p->Paste(*pcopy);
            }
            p->setStatusValue(prop->getStatus());
        }
        newProps[p] = prop;
    }

    if(checkExisting) {
        props.clear();
        parent->getPropertyList(props);
        for(auto prop : props) {
            if(prop->getContainer()!=parent)
                continue;
            auto gname = prop->getGroup();
            if(!gname || !boost::starts_with(gname, _GroupPrefix))
                continue;
            if(!newProps.count(prop))
                parent->removeDynamicProperty(prop->getName());
        }
    }

    if(!copyOnChangeConns)
        return res;

    for(const auto &v : newProps) {
        // sync configuration properties
        copyOnChangeConns->push_back(v.second->signalChanged.connect([parent](const Property &prop) {
            if(!prop.testStatus(Property::CopyOnChange))
                return;
            auto p = parent->getPropertyByName(prop.getName());
            if(p && p->getTypeId()==prop.getTypeId()) {
                std::unique_ptr<Property> pcopy(prop.Copy());
                // temperoray set Output to prevent touching
                p->setStatus(Property::Output, true);
                // temperoray block copy on change
                Base::ObjectStatusLocker<Property::Status,Property> guard(Property::User3, p);
                if(pcopy)
                    p->Paste(*pcopy);
                p->setStatusValue(prop.getStatus());
            }
        }));
    }

    return res;
}

void LinkBaseExtension::checkCopyOnChange(App::DocumentObject *parent, const App::Property &prop)
{
    if(!parent || !parent->getDocument()
               || parent->getDocument()->isPerformingTransaction())
        return;

    auto linked = getLinkedObjectValue();
    if(!linked || getLinkCopyOnChangeValue()==CopyOnChangeDisabled
               || !isCopyOnChangeProperty(parent,prop))
        return;

    if(getLinkCopyOnChangeValue() == CopyOnChangeOwned ||
            (getLinkCopyOnChangeValue() == CopyOnChangeTracking
             && linked != getLinkCopyOnChangeSourceValue()))
    {
        auto p = linked->getPropertyByName(prop.getName());
        if(p && p->getTypeId()==prop.getTypeId()) {
            std::unique_ptr<Property> pcopy(prop.Copy());
            if(pcopy)
                p->Paste(*pcopy);
        }
        return;
    }

    auto linkedProp = linked->getPropertyByName(prop.getName());
    if(!linkedProp || linkedProp->getTypeId()!=prop.getTypeId() || linkedProp->isSame(prop))
        return;

    auto copied = makeCopyOnChange();
    if (copied) {
        linkedProp = copied->getPropertyByName(prop.getName());
        if(linkedProp && linkedProp->getTypeId()==prop.getTypeId()) {
            std::unique_ptr<Property> pcopy(prop.Copy());
            if(pcopy)
                linkedProp->Paste(*pcopy);
        }
    }
}

App::DocumentObject *LinkBaseExtension::makeCopyOnChange() {
    auto linked = getLinkedObjectValue();
    if (pauseCopyOnChange || !linked)
        return nullptr;
    auto parent = getContainer();
    auto srcobjs = getOnChangeCopyObjects(nullptr, linked);
    for (auto obj : srcobjs) {
        if (obj->testStatus(App::PartialObject)) {
            FC_THROWM(Base::RuntimeError, "Cannot copy partial loaded object: "
                << App::SubObjectT(obj).getObjectFullName(parent->getDocument()->getName()));
        }
    }
    auto objs = parent->getDocument()->copyObject(srcobjs);
    if(objs.empty())
        return nullptr;

    monitorOnChangeCopyObjects(srcobjs);

    linked = objs.back();
    linked->Visibility.setValue(false);

    Base::StateLocker guard(pauseCopyOnChange);
    getLinkedObjectProperty()->setValue(linked);
    if (getLinkCopyOnChangeValue() == CopyOnChangeEnabled)
        getLinkCopyOnChangeProperty()->setValue(CopyOnChangeOwned);

    if (auto prop = getLinkCopyOnChangeGroupProperty()) {
        if (auto obj = prop->getValue()) {
            if (obj->isAttachedToDocument() && obj->getDocument())
                obj->getDocument()->removeObject(obj->getNameInDocument());
        }
        auto group = new LinkGroup;
        group->LinkMode.setValue(LinkModeAutoDelete);
        getContainer()->getDocument()->addObject(group, "CopyOnChangeGroup");
        prop->setValue(group);

        // The order of the copied objects is in dependency order (because of
        // getOnChangeCopyObjects()). We reverse it here so that we can later
        // on delete it in reverse order to avoid error (because some parent
        // objects may want to delete their own children).
        std::reverse(objs.begin(), objs.end());
        group->ElementList.setValues(objs);
    }

    return linked;
}

void LinkBaseExtension::monitorOnChangeCopyObjects(
        const std::vector<App::DocumentObject*> &objs)
{
    copyOnChangeSrcConns.clear();
    if (getLinkCopyOnChangeValue() == CopyOnChangeDisabled)
        return;
    for(auto obj : objs) {
        copyOnChangeSrcConns.emplace_back(obj->signalChanged.connect(
            [this](const DocumentObject &, const Property &) {
                if (auto prop = this->getLinkCopyOnChangeTouchedProperty()) {
                    if (this->getLinkCopyOnChangeValue() != CopyOnChangeDisabled)
                        prop->setValue(true);
                }
            }));
    }
}

App::GroupExtension *LinkBaseExtension::linkedPlainGroup() const {
    if(!mySubElements.empty() && !mySubElements[0].empty())
        return nullptr;
    auto linked = getTrueLinkedObject(false);
    if(!linked)
        return nullptr;
    return GeoFeatureGroupExtension::getNonGeoGroup(linked);
}

App::PropertyLinkList *LinkBaseExtension::_getElementListProperty() const {
    auto group = linkedPlainGroup();
    if(group)
        return &group->Group;
    return const_cast<PropertyLinkList*>(getElementListProperty());
}

const std::vector<App::DocumentObject*> &LinkBaseExtension::_getElementListValue() const {
    if(_ChildCache.getSize())
        return _ChildCache.getValues();
    if(getElementListProperty())
        return getElementListProperty()->getValues();
    static const std::vector<DocumentObject*> empty;
    return empty;
}

App::PropertyBool *LinkBaseExtension::_getShowElementProperty() const {
    auto prop = getShowElementProperty();
    if(prop && !linkedPlainGroup())
        return const_cast<App::PropertyBool*>(prop);
    return nullptr;
}

bool LinkBaseExtension::_getShowElementValue() const {
    auto prop = _getShowElementProperty();
    if(prop)
        return prop->getValue();
    return true;
}

App::PropertyInteger *LinkBaseExtension::_getElementCountProperty() const {
    auto prop = getElementCountProperty();
    if(prop && !linkedPlainGroup())
        return const_cast<App::PropertyInteger*>(prop);
    return nullptr;
}

int LinkBaseExtension::_getElementCountValue() const {
    auto prop = _getElementCountProperty();
    if(prop)
        return prop->getValue();
    return 0;
}

bool LinkBaseExtension::extensionHasChildElement() const {
    if(!_getElementListValue().empty()
            || (_getElementCountValue() && _getShowElementValue()))
        return true;
    if (getLinkClaimChildValue())
        return false;
    DocumentObject *linked = getTrueLinkedObject(false);
    if(linked) {
        if(linked->hasChildElement())
            return true;
    }
    return false;
}

int LinkBaseExtension::extensionSetElementVisible(const char *element, bool visible) {
    if (!element)
        return -1;
    int index = std::isdigit((int)element[0]) ? getArrayIndex(element) : getElementIndex(element);
    if (index < 0) {
        if(_getElementListValue().size()>0 || _getElementCountValue()>0)
            return -1;
    }
    if(index>=0) {
        if(getSyncGroupVisibilityValue() && linkedPlainGroup()) {
            const auto &elements = _getElementListValue();
            if(index<(int)elements.size()) {
                elements[index]->Visibility.setValue(visible);
                return 1;
            }
            return -1;
        }
        auto propElementVis = getVisibilityListProperty();
        if(!propElementVis || !element || !element[0])
            return -1;
        if(propElementVis->getSize()<=index) {
            if(visible)
                return 1;
            propElementVis->setSize(index+1, true);
        }
        propElementVis->setStatus(Property::User3,true);
        propElementVis->set1Value(index,visible);
        propElementVis->setStatus(Property::User3,false);
        const auto &elements = _getElementListValue();
        if(index<(int)elements.size()) {
            if(!visible)
                myHiddenElements.insert(elements[index]);
            else
                myHiddenElements.erase(elements[index]);
        }
        return 1;
    }
    DocumentObject *linked = getTrueLinkedObject(false);
    if(linked)
        return linked->setElementVisible(element,visible);
    return -1;
}

int LinkBaseExtension::extensionIsElementVisible(const char *element) const {
    if (!element)
        return -1;
    int index = std::isdigit((int)element[0]) ? getArrayIndex(element) : getElementIndex(element);
    if (index < 0) {
        if(_getElementListValue().size()>0 || _getElementCountValue()>0)
            return -1;
    }
    if (index>=0) {
        if(getSyncGroupVisibilityValue() && linkedPlainGroup()) {
            const auto &elements = _getElementListValue();
            if(index<(int)elements.size()) {
                auto group = App::GroupExtension::getGroupOfObject(elements[index]);
                if(group)
                    return group->isElementVisible(elements[index]->getNameInDocument());
            }
            return -1;
        }
        auto propElementVis = getVisibilityListProperty();
        if(propElementVis) {
            if(propElementVis->getSize()<=index || propElementVis->getValues()[index])
                return 1;
            return 0;
        }
        return -1;
    }
    DocumentObject *linked = getTrueLinkedObject(false);
    if(linked)
        return linked->isElementVisible(element);
    return -1;
}

int LinkBaseExtension::extensionIsElementVisibleEx(const char *subname, int reason) const {
    if (!subname)
        return -1;
    auto element = Data::findElementName(subname);
    if(subname != element && isSubnameHidden(getContainer(),subname))
        return 0;

    int index = std::isdigit((int)element[0]) ? getArrayIndex(element) : getElementIndex(element);
    if (index < 0) {
        if(_getElementListValue().size()>0 ||_getElementCountValue()>0)
            return -1;
    }

    if (index>=0) {
        if(getSyncGroupVisibilityValue() && linkedPlainGroup()) {
            const auto &elements = _getElementListValue();
            if(index<(int)elements.size()) {
                auto group = App::GroupExtension::getGroupOfObject(elements[index]);
                if(group)
                    return group->isElementVisibleEx(elements[index]->getNameInDocument(),reason);
            }
            return -1;
        }
        auto propElementVis = getVisibilityListProperty();
        if(propElementVis) {
            if(propElementVis->getSize()<=index || propElementVis->getValues()[index])
                return 1;
            return 0;
        }
        return -1;
    }
    DocumentObject *linked = getTrueLinkedObject(false);
    if(linked)
        return linked->isElementVisibleEx(subname,reason);
    return -1;
}

const DocumentObject *LinkBaseExtension::getContainer() const {
    auto ext = getExtendedContainer();
    if(!ext || !ext->isDerivedFrom(DocumentObject::getClassTypeId()))
        LINK_THROW(Base::RuntimeError,"Link: container not derived from document object");
    return static_cast<const DocumentObject *>(ext);
}

DocumentObject *LinkBaseExtension::getContainer(){
    auto ext = getExtendedContainer();
    if(!ext || !ext->isDerivedFrom(DocumentObject::getClassTypeId()))
        LINK_THROW(Base::RuntimeError,"Link: container not derived from document object");
    return static_cast<DocumentObject *>(ext);
}

DocumentObject *LinkBaseExtension::getLink(int depth) const{
    if (!GetApplication().checkLinkDepth(depth, MessageOption::Error))
        return nullptr;
    if(getLinkedObjectProperty())
        return getLinkedObjectValue();
    return nullptr;
}

int LinkBaseExtension::getArrayIndex(const char *subname, const char **psubname) {
    if(!subname || Data::isMappedElement(subname))
        return -1;
    const char *dot = strchr(subname,'.');
    if(!dot) dot= subname+strlen(subname);
    if(dot == subname)
        return -1;
    int idx = 0;
    for(const char *c=subname;c!=dot;++c) {
        if(!isdigit(*c))
            return -1;
        idx = idx*10 + *c -'0';
    }
    if(psubname) {
        if(*dot)
            *psubname = dot+1;
        else
            *psubname = dot;
    }
    return idx;
}

int LinkBaseExtension::getElementIndex(const char *subname, const char **psubname) const {
    if(!subname || Data::isMappedElement(subname))
        return -1;
    int idx = -1;
    const char *dot = strchr(subname,'.');
    if(!dot) dot= subname+strlen(subname);

    if(isdigit(subname[0])) {
        // If the name start with digits, treat as index reference
        idx = getArrayIndex(subname,nullptr);
        if(idx<0)
            return -1;
        if(_getElementCountProperty()) {
            if(idx>=_getElementCountValue())
                return -1;
        }else if(idx>=(int)_getElementListValue().size())
            return -1;
    }else if(!_getShowElementValue() && _getElementCountValue()) {
        // If elements are collapsed, we check first for LinkElement naming
        // pattern, which is the owner object name + "_i" + index
        const char *name = subname[0]=='$'?subname+1:subname;
        auto owner = getContainer();
        if(owner && owner->isAttachedToDocument()) {
            std::string ownerName(owner->getNameInDocument());
            ownerName += '_';
            if(boost::algorithm::starts_with(name,ownerName.c_str())) {
                for(const char *txt=dot-1;txt>=name+ownerName.size();--txt) {
                    if(*txt == 'i') {
                        idx = getArrayIndex(txt+1,nullptr);
                        if(idx<0 || idx>=_getElementCountValue())
                            idx = -1;
                        break;
                    }
                    if(!isdigit(*txt))
                        break;
                }
            }
        }
        if(idx<0) {
            // Then check for the actual linked object's name or label, and
            // redirect that reference to the first array element
            auto linked = getTrueLinkedObject(false);
            if(!linked || !linked->isAttachedToDocument())
                return -1;
            if(subname[0]=='$') {
                CharRange sub(subname+1, dot);
                if (boost::equals(sub, linked->Label.getValue()))
                    idx = 0;
            } else {
                CharRange sub(subname, dot);
                if (boost::equals(sub, linked->getNameInDocument()))
                    idx = 0;
            }
            if(idx<0) {
                // Lastly, try to get sub object directly from the linked object
                auto sobj = linked->getSubObject(std::string(subname, dot-subname+1).c_str());
                if(!sobj)
                    return -1;
                if(psubname)
                    *psubname = subname;
                return 0;
            }
        }
    }else if(subname[0]!='$') {
        // Try search by element objects' name
        std::string name(subname,dot);
        if(_ChildCache.getSize()) {
            auto obj=_ChildCache.find(name,&idx);
            if(obj) {
                auto group = GeoFeatureGroupExtension::getNonGeoGroup(obj);
                if(group) {
                    int nidx = getElementIndex(dot+1,psubname);
                    if(nidx >= 0)
                        return nidx;
                }
            }
        } else if(getElementListProperty())
            getElementListProperty()->find(name.c_str(),&idx);
        if(idx<0)
            return -1;
    }else {
        // Try search by label if the reference name start with '$'
        ++subname;
        std::string name(subname,dot-subname);
        const auto &elements = _getElementListValue();
        if(enableLabelCache) {
            if(myLabelCache.empty())
                cacheChildLabel(1);
            auto it = myLabelCache.find(name);
            if(it == myLabelCache.end())
                return -1;
            idx = it->second;
        }else{
            idx = 0;
            for(auto element : elements) {
                if(element->Label.getStrValue() == name)
                    break;
                ++idx;
            }
        }
        if(idx<0 || idx>=(int)elements.size())
            return -1;
        auto obj = elements[idx];
        if(obj && _ChildCache.getSize()) {
            auto group = GeoFeatureGroupExtension::getNonGeoGroup(obj);
            if(group) {
                int nidx = getElementIndex(dot+1,psubname);
                if(nidx >= 0)
                    return nidx;
            }
        }
    }
    if(psubname)
        *psubname = dot[0]?dot+1:dot;
    return idx;
}

void LinkBaseExtension::elementNameFromIndex(int idx, std::ostream &ss) const {
    const auto &elements = _getElementListValue();
    if(idx < 0 || idx >= (int)elements.size())
        return;

    auto obj = elements[idx];
    if(_ChildCache.getSize()) {
        auto group = GroupExtension::getGroupOfObject(obj);
        if(group && _ChildCache.find(group->getNameInDocument(),&idx))
            elementNameFromIndex(idx,ss);
    }
    ss << obj->getNameInDocument() << '.';
}

Base::Vector3d LinkBaseExtension::getScaleVector() const {
    if(getScaleVectorProperty())
        return getScaleVectorValue();
    double s = getScaleValue();
    return Base::Vector3d(s,s,s);
}

Base::Matrix4D LinkBaseExtension::getTransform(bool transform) const {
    Base::Matrix4D mat;
    if(transform) {
        if(getLinkPlacementProperty())
            mat = getLinkPlacementValue().toMatrix();
        else if(getPlacementProperty())
            mat = getPlacementValue().toMatrix();
    }
    if(getScaleProperty() || getScaleVectorProperty()) {
        Base::Matrix4D s;
        s.scale(getScaleVector());
        mat *= s;
    }
    if(getMatrixProperty())
        mat *= getMatrixValue();
    return mat;
}

bool LinkBaseExtension::extensionGetSubObjects(std::vector<std::string> &ret, int reason) const {
    if(!getLinkedObjectProperty() && getElementListProperty()) {
        for(auto obj : getElementListProperty()->getValues()) {
            if(obj && obj->isAttachedToDocument()) {
                std::string name(obj->getNameInDocument());
                name+='.';
                ret.push_back(name);
            }
        }
        return true;
    }
    if(mySubElements.empty() || mySubElements[0].empty()) {
        DocumentObject *linked = getTrueLinkedObject(true);
        if(linked) {
            if(!_getElementCountValue())
                ret = linked->getSubObjects(reason);
            else{
                char index[30];
                for(int i=0,count=_getElementCountValue();i<count;++i) {
                    snprintf(index,sizeof(index),"%d.",i);
                    ret.emplace_back(index);
                }
            }
        }
    } else if(mySubElements.size()>1) {
        ret = mySubElements;
    }
    return true;
}

bool LinkBaseExtension::extensionGetSubObject(DocumentObject *&ret, const char *subname,
        PyObject **pyObj, Base::Matrix4D *mat, bool transform, int depth) const
{
    ret = nullptr;
    auto obj = getContainer();
    if(!subname || !subname[0]) {
        ret = const_cast<DocumentObject*>(obj);
        Base::Matrix4D _mat;
        if(mat) {
            // 'mat' here is used as an output to return the accumulated
            // transformation up until this object. Since 'subname' is empty
            // here, it means the we are at the end of the hierarchy. We shall
            // not include scale in the output transformation.
            //
            // Think of it this way, the transformation along object hierarchy
            // is public, while transformation through linkage is private to
            // link itself.
            if(transform) {
                if(getLinkPlacementProperty())
                    *mat *= getLinkPlacementValue().toMatrix();
                else if(getPlacementProperty())
                    *mat *= getPlacementValue().toMatrix();
            }
            _mat = *mat;
        }

        if(pyObj && !_getElementCountValue()
                && _getElementListValue().empty() && mySubElements.size()<=1)
        {
            // Scale will be included here
            if(getScaleProperty() || getScaleVectorProperty()) {
                Base::Matrix4D s;
                s.scale(getScaleVector());
                _mat *= s;
            }
            if(getMatrixProperty())
                _mat *= getMatrixValue();
            auto linked = getTrueLinkedObject(false,&_mat,depth);
            if(linked && linked!=obj) {
                linked->getSubObject(mySubElements.empty()?nullptr:mySubElements.front().c_str(),
                                     pyObj,&_mat,false,depth+1);
                checkGeoElementMap(obj,linked,pyObj,nullptr);
            }
        }
        return true;
    }

    if(mat) *mat *= getTransform(transform);

    //DocumentObject *element = 0;
    bool isElement = false;
    int idx = getElementIndex(subname,&subname);
    if(idx>=0) {
        const auto &elements = _getElementListValue();
        if(!elements.empty()) {
            if(idx>=(int)elements.size() || !elements[idx] || !elements[idx]->isAttachedToDocument())
                return true;
            ret = elements[idx]->getSubObject(subname,pyObj,mat,true,depth+1);
            // do not resolve the link if this element is the last referenced object
            if(!subname || Data::isMappedElement(subname) || !strchr(subname,'.'))
                ret = elements[idx];
            return true;
        }

        int elementCount = _getElementCountValue();
        if(idx>=elementCount)
            return true;
        isElement = true;
        if(mat) {
            auto placementList = getPlacementListProperty();
            if(placementList && placementList->getSize()>idx)
                *mat *= (*placementList)[idx].toMatrix();
            auto scaleList = getScaleListProperty();
            if(scaleList && scaleList->getSize()>idx) {
                Base::Matrix4D s;
                s.scale((*scaleList)[idx]);
                *mat *= s;
            }
            auto matrixList = getMatrixListProperty();
            if(matrixList && matrixList->getSize()>idx)
                *mat *= (*matrixList)[idx];
        }
    }

    auto linked = getTrueLinkedObject(false,mat,depth);
    if(!linked || linked==obj)
        return true;

    Base::Matrix4D matNext;

    // Because of the addition of LinkClaimChild, the linked object may be
    // claimed as the first child. Regardless of the current value of
    // LinkClaimChild, we must accept sub-object path that contains the linked
    // object, because other link property may store such reference.
    if (const char* dot=strchr(subname,'.')) {
        auto group = getLinkCopyOnChangeGroupValue();
        if (subname[0] == '$') {
            CharRange sub(subname+1,dot);
            if (group && boost::equals(sub, group->Label.getValue()))
                linked = group;
            else if(!boost::equals(sub, linked->Label.getValue()))
                dot = nullptr;
        } else {
            CharRange sub(subname,dot);
            if (group && boost::equals(sub, group->getNameInDocument()))
                linked = group;
            else if (!boost::equals(sub, linked->getNameInDocument()))
                dot = nullptr;
        }
        if (dot) {
            // Because of external linked object, It is possible for and
            // child object to have the exact same internal name or label
            // as the parent object. To resolve this potential ambiguity,
            // try assuming the current subname is referring to the parent
            // (i.e. the linked object), and if it fails, try again below.
            if(mat) matNext = *mat;
            ret = linked->getSubObject(dot+1,pyObj,mat?&matNext:nullptr,false,depth+1);
            if (ret && dot[1])
                subname = dot+1;
        }
    }

    if (!ret) {
        if(mat) matNext = *mat;
        ret = linked->getSubObject(subname,pyObj,mat?&matNext:nullptr,false,depth+1);
    }

    std::string postfix;
    if(ret) {
        // do not resolve the link if we are the last referenced object
        if(subname && !Data::isMappedElement(subname) && strchr(subname,'.')) {
            if(mat)
                *mat = matNext;
        }
        // This is a useless check as 'element' is never set to a value other than null
        //else if(element) {
        //    ret = element;
        //}
        else if(!isElement) {
            ret = const_cast<DocumentObject*>(obj);
        }
        else {
            if(idx) {
                postfix = Data::indexPostfix();
                postfix += std::to_string(idx);
            }
            if(mat)
                *mat = matNext;
        }
    }
    checkGeoElementMap(obj,linked,pyObj,!postfix.empty()?postfix.c_str():nullptr);
    return true;
}

void LinkBaseExtension::checkGeoElementMap(const App::DocumentObject *obj,
        const App::DocumentObject *linked, PyObject **pyObj, const char *postfix) const
{
    if(!pyObj || !*pyObj || !PyObject_TypeCheck(*pyObj, &Data::ComplexGeoDataPy::Type))
        return;

    auto geoData = static_cast<Data::ComplexGeoDataPy*>(*pyObj)->getComplexGeoDataPtr();
    std::string _postfix;
    if (linked && obj && linked->getDocument() != obj->getDocument()) {
        _postfix = Data::externalTagPostfix();
        if (postfix) {
            if (!boost::starts_with(postfix, Data::elementMapPrefix()))
                _postfix += Data::elementMapPrefix();
            _postfix += postfix;
        }
        postfix = _postfix.c_str();
    }
    geoData->reTagElementMap(obj->getID(),obj->getDocument()->getStringHasher(),postfix);
}

void LinkBaseExtension::onExtendedSetupObject() {
    _LinkVersion.setValue(1);
}

void LinkBaseExtension::onExtendedUnsetupObject() {
    if(!getElementListProperty())
        return;
    detachElements();
    if (auto obj = getLinkCopyOnChangeGroupValue()) {
        if(obj->isAttachedToDocument() && !obj->isRemoving())
            obj->getDocument()->removeObject(obj->getNameInDocument());
    }
}

DocumentObject *LinkBaseExtension::getTrueLinkedObject(
        bool recurse, Base::Matrix4D *mat, int depth, bool noElement) const
{
    if (noElement) {
        if (auto linkElement = Base::freecad_dynamic_cast<LinkElement>(getContainer())) {
            if (!linkElement->canDelete())
                return nullptr;
        }
    }

    auto ret = getLink(depth);
    if(!ret)
        return nullptr;
    bool transform = linkTransform();
    const char *subname = getSubName();
    if(subname || (mat && transform)) {
        ret = ret->getSubObject(subname,nullptr,mat,transform,depth+1);
        transform = false;
    }
    if(ret && recurse)
        ret = ret->getLinkedObject(recurse,mat,transform,depth+1);
    if(ret && !ret->isAttachedToDocument())
        return nullptr;
    return ret;
}

bool LinkBaseExtension::extensionGetLinkedObject(DocumentObject *&ret,
        bool recurse, Base::Matrix4D *mat, bool transform, int depth) const
{
    if(mat)
        *mat *= getTransform(transform);
    ret = nullptr;
    if(!_getElementCountValue())
        ret = getTrueLinkedObject(recurse,mat,depth);
    if(!ret)
        ret = const_cast<DocumentObject*>(getContainer());
    // always return true to indicate we've handled getLinkObject() call
    return true;
}

void LinkBaseExtension::extensionOnChanged(const Property *prop) {
    auto parent = getContainer();
    if(parent && !parent->isRestoring() && prop && !prop->testStatus(Property::User3)) {
        if (!parent->getDocument() || !parent->getDocument()->isPerformingTransaction())
            update(parent,prop);
        else {
            Base::StateLocker guard(pauseCopyOnChange);
            update(parent,prop);
        }
    }
    inherited::extensionOnChanged(prop);
}

void LinkBaseExtension::parseSubName() const {
    // If user has ever linked to some sub-element, the Link shall always accept
    // sub-element linking in the future, which affects how ViewProviderLink
    // dropObjectEx() behave. So we will push an empty string later even if no
    // sub-element linking this time.
    bool hasSubElement = !mySubElements.empty();
    mySubElements.clear();
    mySubName.clear();
    auto xlink = freecad_dynamic_cast<const PropertyXLink>(getLinkedObjectProperty());
    if(!xlink || xlink->getSubValues().empty()) {
        if(hasSubElement)
            mySubElements.emplace_back("");
        return;
    }
    const auto &subs = xlink->getSubValues();
    auto subname = subs.front().c_str();
    auto element = Data::findElementName(subname);
    if(!element || !element[0]) {
        mySubName = subs[0];
        if(hasSubElement)
            mySubElements.emplace_back("");
        return;
    }
    mySubElements.emplace_back(element);
    mySubName = std::string(subname,element-subname);
    for(std::size_t i=1;i<subs.size();++i) {
        auto &sub = subs[i];
        element = Data::findElementName(sub.c_str());
        if(element && element[0] && boost::starts_with(sub,mySubName))
            mySubElements.emplace_back(element);
    }
}

void LinkBaseExtension::updateGroup() {
    std::vector<GroupExtension*> groups;
    auto group = linkedPlainGroup();
    if(group) {
        groups.push_back(group);
    }else{
        for(auto o : getElementListProperty()->getValues()) {
            if(!o || !o->isAttachedToDocument())
                continue;
            auto ext = GeoFeatureGroupExtension::getNonGeoGroup(o);
            if(ext) 
                groups.push_back(ext);
        }
    }
    std::vector<App::DocumentObject*> children;
    plainGroupConns.clear();
    if(!groups.empty()) {
        children = getElementListValue();
        std::set<DocumentObject*> childSet(children.begin(),children.end());
        for(auto ext : groups) {
            plainGroupConns.push_back(ext->Group.signalChanged.connect(
                        std::bind(&LinkBaseExtension::updateGroup,this)));
            if(getSyncGroupVisibilityValue()) {
                plainGroupConns.push_back(ext->_GroupTouched.signalChanged.connect(
                            std::bind(&LinkBaseExtension::updateGroupVisibility,this)));
            }

            std::size_t count = children.size();
            ext->getAllChildren(children,childSet);
            for(;count<children.size();++count) {
                auto child = children[count];
                auto childGroup = GeoFeatureGroupExtension::getNonGeoGroup(child);
                if(!childGroup)
                    continue;
                //NOLINTBEGIN
                plainGroupConns.push_back(childGroup->Group.signalChanged.connect(
                                std::bind(&LinkBaseExtension::updateGroupVisibility,this)));
                if(getSyncGroupVisibilityValue()) {
                    plainGroupConns.push_back(childGroup->_GroupTouched.signalChanged.connect(
                                std::bind(&LinkBaseExtension::updateGroupVisibility,this)));
                }
                //NOLINTEND
            }
        }
    }
    if(children != _ChildCache.getValues())
        _ChildCache.setValue(children);
    updateGroupVisibility();
}

void LinkBaseExtension::updateGroupVisibility() {
    if(getSyncGroupVisibilityValue())
        _LinkTouched.touch();
}

void LinkBaseExtension::update(App::DocumentObject *parent, const Property *prop) {
    if(!prop)
        return;

    if(prop == getLinkPlacementProperty() || prop == getPlacementProperty()) {
        auto src = getLinkPlacementProperty();
        auto dst = getPlacementProperty();
        if(src!=prop) std::swap(src,dst);
        if(src && dst) {
            dst->setStatus(Property::User3,true);
            dst->setValue(src->getValue());
            dst->setStatus(Property::User3,false);
        }
    }else if(prop == getScaleProperty()) {
        if(!prop->testStatus(Property::User3) && getScaleVectorProperty()) {
            auto s = getScaleValue();
            auto p = getScaleVectorProperty();
            p->setStatus(Property::User3,true);
            p->setValue(s,s,s);
            p->setStatus(Property::User3,false);
        }
    }else if(prop == getScaleVectorProperty()) {
        if(!prop->testStatus(Property::User3) && getScaleProperty()) {
            const auto &v = getScaleVectorValue();
            if(v.x == v.y && v.x == v.z) {
                auto p = getScaleProperty();
                p->setStatus(Property::User3,true);
                p->setValue(v.x);
                p->setStatus(Property::User3,false);
            }
        }
    }else if(prop == _getShowElementProperty()) {
        if(_getShowElementValue())
            update(parent,_getElementCountProperty());
        else {
            auto objs = getElementListValue();

            // preserve element properties in ourself
            std::vector<Base::Placement> placements;
            std::vector<Base::Matrix4D> matrices;
            placements.reserve(objs.size());
            std::vector<Base::Vector3d> scales;
            scales.reserve(objs.size());
            static Base::Matrix4D identity;
            auto matrixList = getMatrixListProperty();
            for(size_t i=0;i<objs.size();++i) {
                auto element = freecad_dynamic_cast<LinkElement>(objs[i]);
                if(element) {
                    placements.push_back(element->Placement.getValue());
                    scales.push_back(element->getScaleVector());
                    if (matrixList && element->getMatrixValue() != identity) {
                        matrices.reserve(objs.size());
                        matrices.resize(i);
                        matrices.push_back(element->getMatrixValue());
                    }
                }else{
                    placements.emplace_back();
                    scales.emplace_back(1,1,1);
                }
            }
            // touch the property again to make sure view provider has been
            // signaled before clearing the elements
            getShowElementProperty()->setStatus(App::Property::User3, true);
            getShowElementProperty()->touch();
            getShowElementProperty()->setStatus(App::Property::User3, false);

            getElementListProperty()->setValues(std::vector<App::DocumentObject*>());

            if(getPlacementListProperty()) {
                getPlacementListProperty()->setStatus(Property::User3,true);
                getPlacementListProperty()->setValue(placements);
                getPlacementListProperty()->setStatus(Property::User3,false);
            }
            if(getScaleListProperty()) {
                getScaleListProperty()->setStatus(Property::User3,true);
                getScaleListProperty()->setValue(scales);
                getScaleListProperty()->setStatus(Property::User3,false);
            }
            if (matrixList) {
                matrixList->setStatus(Property::User3,true);
                matrixList->setValue(matrices);
                matrixList->setStatus(Property::User3,false);
            }

            if (getScaleListProperty())
                getScaleListProperty()->touch();
            else if (getPlacementListProperty())
                getPlacementListProperty()->touch();
            else if (matrixList)
                matrixList->touch();

            for(auto obj : objs) {
                if(obj && obj->isAttachedToDocument())
                    obj->getDocument()->removeObject(obj->getNameInDocument());
            }
        }
    }else if(prop == getElementCountProperty()) {
        int elementCount = getElementCountValue()<0?0:getElementCountValue();

        auto group = linkedPlainGroup();
        if(group) {
            if (elementCount && getLinkedObjectProperty()
                             && !App::Document::isAnyRestoring()
                             && !parent->getDocument()->isPerformingTransaction())
            {
                auto link = static_cast<App::Link*>(
                        parent->getDocument()->addObject("App::Link", "Link"));
                if (link) {
                    auto grp = group->getExtendedObject();
                    link->LinkedObject.setValue(grp);
                    link->Label.setValue(grp->Label.getValue());
                    getLinkedObjectProperty()->setValue(link);
                    // Element count will be reset to zero when linked object property changed
                    getElementCountProperty()->setValue(elementCount);
                }
            }
            return;
        }

        auto propVis = getVisibilityListProperty();
        if(propVis) {
            if(propVis->getSize()>elementCount)
                propVis->setSize(getElementCountValue(),true);
        }

        if(!_getShowElementValue()) {
            if(getScaleListProperty()) {
                auto prop = getScaleListProperty();
                auto scales = getScaleListValue();
                scales.resize(elementCount,Base::Vector3d(1,1,1));
                Base::ObjectStatusLocker<Property::Status,Property> guard(Property::User3, prop);
                prop->setValue(scales);
            }
            if(getPlacementListProperty()) {
                auto prop = getPlacementListProperty();
                Base::ObjectStatusLocker<Property::Status,Property> guard(Property::User3, prop);
                if(prop->getSize() < elementCount)
                    signalNewLinkElements(*parent, prop->getSize(), elementCount, nullptr);
                prop->setSize(elementCount);
            }
            if (getMatrixListProperty()) {
                auto prop = getMatrixListProperty();
                Base::ObjectStatusLocker<Property::Status,Property> guard(Property::User3, prop);
                prop->setSize(elementCount);
            }
        }else if(getElementListProperty()) {
            auto objs = getElementListValue();
            if(elementCount > (int)objs.size()) {
                std::string name = parent->getNameInDocument();
                auto doc = parent->getDocument();
                name += "_i";
                name = doc->getUniqueObjectName(name.c_str());
                if(name[name.size()-1] != 'i')
                    name += "_i";
                auto offset = name.size();
                auto scaleProp = getScaleListProperty();
                auto matrixProp = getMatrixListProperty();
                const auto &vis = getVisibilityListValue();

                auto owner = getContainer();
                long ownerID = owner?owner->getID():0;

                for(int i=(int)objs.size();i<elementCount;++i) {
                    name.resize(offset);
                    name += std::to_string(i);

                    // It is possible to have orphan LinkElement here due to,
                    // for example, undo and redo. So we try to re-claim the
                    // children element first.
                    auto obj = freecad_dynamic_cast<LinkElement>(doc->getObject(name.c_str()));
                    if(obj && (!obj->_LinkOwner.getValue() || obj->_LinkOwner.getValue()==ownerID)) {
                        // obj->Visibility.setValue(false);
                    } else {
                        obj = new LinkElement;
                        parent->getDocument()->addObject(obj,name.c_str());
                    }

                    if((int)vis.size() > i && !vis[i])
                        myHiddenElements.insert(obj);

                    if(scaleProp && scaleProp->getSize()>i)
                        obj->Scale.setValue(scaleProp->getValues()[i].x);
                    else
                        obj->Scale.setValue(1);

                    if (matrixProp && matrixProp->getSize()>i)
                        obj->Matrix.setValue(matrixProp->getValues()[i]);
                    else
                        obj->Matrix.setValue(Base::Matrix4D());

                    objs.push_back(obj);
                }
                signalNewLinkElements(*parent,
                                      getElementListProperty()->getSize(),
                                      elementCount,
                                      &objs);
                if(getPlacementListProperty())
                    getPlacementListProperty()->setSize(0);
                if(getScaleListProperty())
                    getScaleListProperty()->setSize(0);
                if(getMatrixListProperty())
                    getMatrixListProperty()->setSize(0);

                getElementListProperty()->setValue(objs);

            }else if(elementCount < (int)objs.size()){
                std::vector<App::DocumentObject*> tmpObjs;
                auto owner = getContainer();
                long ownerID = owner?owner->getID():0;
                while((int)objs.size() > elementCount) {
                    auto element = freecad_dynamic_cast<LinkElement>(objs.back());
                    if(element && element->_LinkOwner.getValue()==ownerID)
                        tmpObjs.push_back(objs.back());
                    objs.pop_back();
                }
                getElementListProperty()->setValue(objs);
                for(auto obj : tmpObjs) {
                    if(obj && obj->isAttachedToDocument())
                        obj->getDocument()->removeObject(obj->getNameInDocument());
                }
            }
        }
    }else if(prop == getVisibilityListProperty()) {
        if(_getShowElementValue()) {
            const auto &elements = _getElementListValue();
            const auto &vis = getVisibilityListValue();
            myHiddenElements.clear();
            for(size_t i=0;i<vis.size();++i) {
                if(i>=elements.size())
                    break;
                if(!vis[i])
                    myHiddenElements.insert(elements[i]);
            }
        }
    }else if(prop == getElementListProperty() || prop == &_ChildCache) {

        if(prop == getElementListProperty()) {
            _ChildCache.setStatus(Property::User3,true);
            updateGroup();
            _ChildCache.setStatus(Property::User3,false);
        }

        const auto &elements = _getElementListValue();

        if(enableLabelCache)
            myLabelCache.clear();

        // Element list changed, we need to sychrnoize VisibilityList.
        if(_getShowElementValue() && getVisibilityListProperty()) {
            if(parent->getDocument()->isPerformingTransaction()) {
                update(parent,getVisibilityListProperty());
            }else{
                boost::dynamic_bitset<> vis;
                vis.resize(elements.size(),true);
                std::unordered_set<const App::DocumentObject *> hiddenElements;
                for(size_t i=0;i<elements.size();++i) {
                    if(myHiddenElements.find(elements[i])!=myHiddenElements.end()) {
                        hiddenElements.insert(elements[i]);
                        vis[i] = false;
                    }
                }
                myHiddenElements.swap(hiddenElements);
                if(vis != getVisibilityListValue()) {
                    auto propVis = getVisibilityListProperty();
                    propVis->setStatus(Property::User3,true);
                    propVis->setValue(vis);
                    propVis->setStatus(Property::User3,false);
                }
            }
        }
        syncElementList();
        if(_getShowElementValue()
                && _getElementCountProperty()
                && getElementListProperty()
                && getElementCountValue()!=getElementListProperty()->getSize())
        {
            getElementCountProperty()->setValue(
                    getElementListProperty()->getSize());
        }
    }else if(prop == getSyncGroupVisibilityProperty()) {
        if(linkedPlainGroup())
            updateGroup();
    }else if(prop == getLinkedObjectProperty()) {
        if (getAutoLinkLabelValue()) {
            connLabelChange.disconnect();
            auto linked = getTrueLinkedObject(false);
            if (linked) {
                connLabelChange = linked->Label.signalChanged.connect(
                        std::bind(&Link::slotLabelChanged, this));
                slotLabelChanged();
            }
        }

        auto group = linkedPlainGroup();
        if(group && getElementCountValue()
                 && !App::Document::isAnyRestoring()
                 && !parent->getDocument()->isPerformingTransaction())
        {
            auto link = static_cast<App::Link*>(
                    parent->getDocument()->addObject("App::Link", "Link"));
            if (link) {
                auto grp = group->getExtendedObject();
                link->LinkedObject.setValue(grp);
                link->Label.setValue(grp->Label.getValue());
                getLinkedObjectProperty()->setValue(link);
                return;
            }
        }

        if(group) {
            updateGroup();
            if(!App::Document::isAnyRestoring()
                    && !parent->getDocument()->isPerformingTransaction()
                    && getVisibilityListProperty()
                    && prevLinkedObjectID != group->getExtendedObject()->getID())
            {
                auto vis = getVisibilityListValue();
                vis.clear();
                for (auto child : group->Group.getValues()) {
                    int v = group->extensionIsElementVisible(child->getNameInDocument());
                    if (v < 0)
                        v = child->Visibility.getValue() ? 1 : 0;
                    vis.push_back(v ? true : false);
                }
                getVisibilityListProperty()->setValues(vis);
            }
        } else if(_ChildCache.getSize())
            _ChildCache.setValue();
        if (auto linked = getLinkedObjectValue())
            prevLinkedObjectID = linked->getID();
        else
            prevLinkedObjectID = 0;
        parseSubName();
        syncElementList();

        if(getLinkCopyOnChangeValue()==CopyOnChangeOwned
                && !pauseCopyOnChange
                && !parent->getDocument()->isPerformingTransaction())
            getLinkCopyOnChangeProperty()->setValue(CopyOnChangeEnabled);
        else
            setupCopyOnChange(parent, true);

    }else if(prop == getLinkCopyOnChangeProperty()) {
        setupCopyOnChange(parent, getLinkCopyOnChangeSourceValue() == nullptr);
    } else if (prop == getLinkCopyOnChangeSourceProperty()) {
        if (auto source = getLinkCopyOnChangeSourceValue()) {
            this->connCopyOnChangeSource = source->signalChanged.connect(
                [this](const DocumentObject & obj, const Property &prop) {
                    auto src = getLinkCopyOnChangeSourceValue();
                    if (src != &obj || getLinkCopyOnChangeValue()==CopyOnChangeDisabled)
                        return;
                    if (App::Document::isAnyRestoring()
                            || obj.testStatus(ObjectStatus::NoTouch)
                            || (prop.getType() & Prop_Output)
                            || prop.testStatus(Property::Output))
                        return;
                    if (auto propTouch = getLinkCopyOnChangeTouchedProperty())
                        propTouch->setValue(true);
                });
        } else
            this->connCopyOnChangeSource.disconnect();

    }else if(prop == getLinkTransformProperty()) {
        auto linkPlacement = getLinkPlacementProperty();
        auto placement = getPlacementProperty();
        if(linkPlacement && placement) {
            bool transform = getLinkTransformValue();
            placement->setStatus(Property::Hidden,transform);
            linkPlacement->setStatus(Property::Hidden,!transform);
        }
        syncElementList();

    } else if(prop == getAutoLinkLabelProperty()) {
        connLabelChange.disconnect();
        if (getAutoLinkLabelValue()) {
            auto linked = getTrueLinkedObject(false);
            if (linked)
                connLabelChange = linked->Label.signalChanged.connect(
                        std::bind(&Link::slotLabelChanged, this));
        }
        slotLabelChanged();
    } else if (prop == &parent->Label) {
        slotLabelChanged();
    } else {
        checkCopyOnChange(parent, *prop);
    }
}

void LinkBaseExtension::slotLabelChanged()
{
    if (!getAutoLinkLabelValue())
        return;
    auto parent = Base::freecad_dynamic_cast<DocumentObject>(getExtendedContainer());
    if (!parent || !parent->getDocument()
                || !parent->isAttachedToDocument()
                || parent->isRestoring()
                || parent->getDocument()->isPerformingTransaction())
        return;

    std::string prefix = parent->getNameInDocument();
    prefix += "(";
    if (parent->Label.getStrValue() != parent->getNameInDocument()
            && !boost::starts_with(parent->Label.getStrValue(), prefix))
        return;

    auto linked = getTrueLinkedObject(false);
    if (!linked) {
        parent->Label.setValue(prefix + "?)");
        return;
    }

    std::string label;
    if (linked->isDerivedFrom(Link::getClassTypeId())) {
        std::string p = linked->getNameInDocument();
        p += "(";
        if (boost::starts_with(linked->Label.getValue(), p)) {
            const char *linkedLabel = linked->Label.getValue() + p.size();
            while (*linkedLabel == '*')
                ++linkedLabel;
            label = prefix + "*" + linkedLabel;
            if (boost::ends_with(label, ")"))
                label.resize(label.size()-1);
        }
    }
    if (label.empty())
        label = prefix + linked->Label.getValue();
    label += ")";
    parent->Label.setValue(label);
}

void LinkBaseExtension::cacheChildLabel(int enable) const {
    enableLabelCache = enable?true:false;
    myLabelCache.clear();
    if(enable<=0)
        return;

    int idx = 0;
    for(auto child : _getElementListValue()) {
        if(child && child->isAttachedToDocument())
            myLabelCache[child->Label.getStrValue()] = idx;
        ++idx;
    }
}

bool LinkBaseExtension::linkTransform() const {
    if(!getLinkTransformProperty() &&
       !getLinkPlacementProperty() &&
       !getPlacementProperty())
        return true;
    return getLinkTransformValue();
}

void LinkBaseExtension::syncElementList() {
    auto transform = getLinkTransformProperty();
    auto link = getLinkedObjectProperty();
    auto xlink = freecad_dynamic_cast<const PropertyXLink>(link);

    auto owner = getContainer();
    auto ownerID = owner?owner->getID():0;
    auto elements = getElementListValue();
    for (auto i : elements) {
        auto element = freecad_dynamic_cast<LinkElement>(i);
        if (!element
            || (element->_LinkOwner.getValue()
                && element->_LinkOwner.getValue() != ownerID))
            continue;

        element->_LinkOwner.setValue(ownerID);

        element->LinkTransform.setStatus(Property::Hidden, transform != nullptr);
        element->LinkTransform.setStatus(Property::Immutable, transform != nullptr);
        if (transform && element->LinkTransform.getValue() != transform->getValue())
            element->LinkTransform.setValue(transform->getValue());

        element->LinkedObject.setStatus(Property::Hidden, link != nullptr);
        element->LinkedObject.setStatus(Property::Immutable, link != nullptr);
        if (element->LinkCopyOnChange.getValue() == 2)
            continue;
        if (xlink) {
            if (element->LinkedObject.getValue() != xlink->getValue()
                || element->LinkedObject.getSubValues() != xlink->getSubValues()) {
                element->LinkedObject.setValue(xlink->getValue(), xlink->getSubValues());
            }
        }
        else if (link && (element->LinkedObject.getValue() != link->getValue()
                            || !element->LinkedObject.getSubValues().empty())) {
            element->setLink(-1, link->getValue());
        }
    }
}

void LinkBaseExtension::onExtendedDocumentRestored() {
    inherited::onExtendedDocumentRestored();
    myHiddenElements.clear();
    auto parent = getContainer();
    if(!parent)
        return;
    if(hasOldSubElement) {
        hasOldSubElement = false;
        // SubElements was stored as a PropertyStringList. It is now migrated to be
        // stored inside PropertyXLink.
        auto xlink = freecad_dynamic_cast<PropertyXLink>(getLinkedObjectProperty());
        if(!xlink)
            FC_ERR("Failed to restore SubElements for " << parent->getFullName());
        else if(!xlink->getValue())
            FC_ERR("Discard SubElements of " << parent->getFullName() << " due to null link");
        else if(xlink->getSubValues().size() > 1)
            FC_ERR("Failed to restore SubElements for " << parent->getFullName()
                    << " due to conflict subnames");
        else if(xlink->getSubValues().empty()) {
            auto subs = xlink->getSubValues();
            xlink->setSubValues(std::move(subs));
        } else {
            std::set<std::string> subset(mySubElements.begin(),mySubElements.end());
            auto sub = xlink->getSubValues().front();
            auto element = Data::findElementName(sub.c_str());
            if(element && element[0]) {
                subset.insert(element);
                sub.resize(element - sub.c_str());
            }
            std::vector<std::string> subs;
            for(const auto &s : subset)
                subs.push_back(sub + s);
            xlink->setSubValues(std::move(subs));
        }
    }
    if(getScaleVectorProperty()) {
        if(getScaleProperty()) {
            // Scale vector is added later. The code here is for migration.
            const auto &v = getScaleVectorValue();
            double s = getScaleValue();
            if(v.x == v.y && v.x == v.z && v.x != s)
                getScaleVectorProperty()->setValue(s,s,s);
        }
        if(_LinkVersion.getValue()<1 && LinkParams::getHideScaleVector())
            getScaleVectorProperty()->setStatus(Property::Hidden, true);
    }
    update(parent,getVisibilityListProperty());
    if (auto prop = getLinkedObjectProperty()) {
        Base::StateLocker guard(pauseCopyOnChange);
        update(parent,prop);
    }
    update(parent,getLinkCopyOnChangeSourceProperty());
    update(parent,getElementListProperty());

    if (_LinkVersion.getValue() == 0)
        _LinkVersion.setValue(1);

    if (getLinkCopyOnChangeValue() != CopyOnChangeDisabled)
        monitorOnChangeCopyObjects(getOnChangeCopyObjects());
}

void LinkBaseExtension::_handleChangedPropertyName(
        Base::XMLReader &reader, const char * TypeName, const char *PropName)
{
    if(strcmp(PropName,"SubElements")==0
        && strcmp(TypeName,PropertyStringList::getClassTypeId().getName())==0)
    {
        PropertyStringList prop;
        prop.setContainer(getContainer());
        prop.Restore(reader);
        if(prop.getSize()) {
            mySubElements = prop.getValues();
            hasOldSubElement = true;
        }
    }
}

void LinkBaseExtension::setLink(int index, DocumentObject *obj,
    const char *subname, const std::vector<std::string> &subElements)
{
    auto parent = getContainer();
    if(!parent)
        LINK_THROW(Base::RuntimeError,"No parent container");

    if(obj && !App::Document::isAnyRestoring()) {
        auto inSet = parent->getInListEx(true);
        inSet.insert(parent);
        if(inSet.find(obj)!=inSet.end())
            LINK_THROW(Base::RuntimeError,"Cyclic dependency");
    }

    auto linkProp = getLinkedObjectProperty();

    // If we are a group (i.e. no LinkObject property), and the index is
    // negative with a non-zero 'obj' assignment, we treat this as group
    // expansion by changing the index to one pass the existing group size
    if(index<0 && obj && !linkProp && getElementListProperty())
        index = getElementListProperty()->getSize();

    if(index>=0) {
        // LinkGroup assignment

        if(linkProp || !getElementListProperty())
            LINK_THROW(Base::RuntimeError,"Cannot set link element");

        DocumentObject *old = nullptr;
        const auto &elements = getElementListProperty()->getValues();
        if(!obj) {
            if(index>=(int)elements.size())
                LINK_THROW(Base::ValueError,"Link element index out of bound");
            std::vector<DocumentObject*> objs;
            old = elements[index];
            for(int i=0;i<(int)elements.size();++i) {
                if(i!=index)
                    objs.push_back(elements[i]);
            }
            getElementListProperty()->setValue(objs);
        }else if(!obj->isAttachedToDocument())
            LINK_THROW(Base::ValueError,"Invalid object");
        else{
            if(index>(int)elements.size())
                LINK_THROW(Base::ValueError,"Link element index out of bound");

            if(index < (int)elements.size())
                old = elements[index];

            int idx = -1;
            if(getLinkModeValue()>=LinkModeAutoLink ||
               (subname && subname[0]) ||
               !subElements.empty() ||
               obj->getDocument()!=parent->getDocument() ||
               (getElementListProperty()->find(obj->getNameInDocument(),&idx) && idx!=index))
            {
                std::string name = parent->getDocument()->getUniqueObjectName("Link");
                auto link = new Link;
                link->_LinkOwner.setValue(parent->getID());
                parent->getDocument()->addObject(link,name.c_str());
                link->setLink(-1,obj,subname,subElements);
                auto linked = link->getTrueLinkedObject(true);
                if(linked)
                    link->Label.setValue(linked->Label.getValue());
                auto pla = freecad_dynamic_cast<PropertyPlacement>(obj->getPropertyByName("Placement"));
                if(pla)
                    link->Placement.setValue(pla->getValue());
                // link->Visibility.setValue(false);
                obj = link;
            }

            if(old == obj)
                return;

            getElementListProperty()->set1Value(index,obj);
        }
        detachElement(old);
        return;
    }

    if(!linkProp) {
        // Reaching here means, we are group (i.e. no LinkedObject), and
        // index<0, and 'obj' is zero. We shall clear the whole group

        if(obj || !getElementListProperty())
            LINK_THROW(Base::RuntimeError,"No PropertyLink or PropertyLinkList configured");
        detachElements();
        return;
    }

    // Here means we are assigning a Link

    auto xlink = freecad_dynamic_cast<PropertyXLink>(linkProp);
    if(obj) {
        if(!obj->isAttachedToDocument())
            LINK_THROW(Base::ValueError,"Invalid document object");
        if(!xlink) {
            if(parent && obj->getDocument()!=parent->getDocument())
                LINK_THROW(Base::ValueError,"Cannot link to external object without PropertyXLink");
        }
    }

    if(!xlink) {
        if(!subElements.empty() || (subname && subname[0]))
            LINK_THROW(Base::RuntimeError,"SubName/SubElement link requires PropertyXLink");
        linkProp->setValue(obj);
        return;
    }

    std::vector<std::string> subs;
    if(!subElements.empty()) {
        subs.reserve(subElements.size());
        for(const auto &s : subElements) {
            subs.emplace_back(subname?subname:"");
            subs.back() += s;
        }
    } else if(subname && subname[0])
        subs.emplace_back(subname);
    xlink->setValue(obj,std::move(subs));
}

void LinkBaseExtension::detachElements()
{
    std::vector<App::DocumentObjectT> objs;
    for (auto obj : getElementListValue())
        objs.emplace_back(obj);
    getElementListProperty()->setValue();
    for(const auto &objT : objs)
        detachElement(objT.getObject());
}

void LinkBaseExtension::detachElement(DocumentObject *obj) {
    if(!obj || !obj->isAttachedToDocument() || obj->isRemoving())
        return;
    auto ext = obj->getExtensionByType<LinkBaseExtension>(true);
    auto owner = getContainer();
    long ownerID = owner?owner->getID():0;
    if(getLinkModeValue()==LinkModeAutoUnlink) {
        if(!ext || ext->_LinkOwner.getValue()!=ownerID)
            return;
    }else if(getLinkModeValue()!=LinkModeAutoDelete) {
        if(ext && ext->_LinkOwner.getValue()==ownerID)
            ext->_LinkOwner.setValue(0);
        return;
    }
    obj->getDocument()->removeObject(obj->getNameInDocument());
}

std::vector<App::DocumentObject*> LinkBaseExtension::getLinkedChildren(bool filter) const{
    if(!filter)
        return _getElementListValue();
    std::vector<App::DocumentObject*> ret;
    for(auto o : _getElementListValue()) {
        if(!GeoFeatureGroupExtension::isNonGeoGroup(o))
            ret.push_back(o);
    }
    return ret;
}

const char *LinkBaseExtension::flattenSubname(const char *subname) const {
    if(subname && _ChildCache.getSize()) {
        const char *sub = subname;
        std::string s;
        for(const char* dot=strchr(sub,'.');dot;sub=dot+1,dot=strchr(sub,'.')) {
            DocumentObject *obj = nullptr;
            s.clear();
            s.append(sub,dot+1);
            extensionGetSubObject(obj,s.c_str());
            if(!obj)
                break;
            if(!GeoFeatureGroupExtension::isNonGeoGroup(obj))
                return sub;
        }
    }
    return subname;
}

void LinkBaseExtension::expandSubname(std::string &subname) const {
    if(!_ChildCache.getSize())
        return;

    const char *pos = nullptr;
    int index = getElementIndex(subname.c_str(),&pos);
    if(index<0)
        return;
    std::ostringstream ss;
    elementNameFromIndex(index,ss);
    ss << pos;
    subname = ss.str();
}

static bool isExcludedProperties(const char *name) {
    if (boost::equals(name, "Shape"))
        return true;
    if (boost::equals(name, "Proxy"))
        return true;
    if (boost::equals(name, "Placement"))
        return true;
    return false;
}

Property *LinkBaseExtension::extensionGetPropertyByName(const char* name) const {
    if (checkingProperty)
        return inherited::extensionGetPropertyByName(name);
    Base::StateLocker guard(checkingProperty);
    if(isExcludedProperties(name))
        return nullptr;
    auto owner = getContainer();
    if (owner) {
        App::Property *prop = owner->getPropertyByName(name);
        if(prop)
            return prop;
        if(owner->canLinkProperties()) {
            auto linked = getTrueLinkedObject(true);
            if(linked)
                return linked->getPropertyByName(name);
        }
    }
    return nullptr;
}

bool LinkBaseExtension::isLinkMutated() const
{
    return getLinkCopyOnChangeValue() != CopyOnChangeDisabled
        && getLinkedObjectValue()
        && (!getLinkCopyOnChangeSourceValue()
            || (getLinkedObjectValue() != getLinkCopyOnChangeSourceValue()));
}

std::vector<std::string> LinkBaseExtension::getHiddenSubnames(
        const App::DocumentObject *obj, const char *prefix) 
{
    std::vector<std::string> res;
    if(!obj || !obj->isAttachedToDocument())
        return res;
    PropertyLinkSubHidden *prop;
    int depth=0;
    while((prop=Base::freecad_dynamic_cast<PropertyLinkSubHidden>(
                    obj->getPropertyByName("ColoredElements"))))
    {
        for(auto &v : prop->getShadowSubs()) {
            if(prefix && !boost::starts_with(v.first,prefix) && !boost::starts_with(v.second,prefix))
                continue;
            auto &s = v.second;
            if(boost::ends_with(s,DocumentObject::hiddenMarker()))
                res.push_back(s.substr(0,s.size()-DocumentObject::hiddenMarker().size()));
        }
        auto o = Base::freecad_dynamic_cast<DocumentObject>(prop->getContainer());
        if(!o)
            break;
        o = o->getLinkedObject(false);
        if(o==obj || !GetApplication().checkLinkDepth(++depth, MessageOption::Error))
            break;
        obj = o;
    }
    return res;
}

bool LinkBaseExtension::isSubnameHidden(const App::DocumentObject *obj, const char *subname) 
{
    if(!obj || !obj->isAttachedToDocument() || !subname || !subname[0])
        return false;
    PropertyLinkSubHidden *prop;
    int depth=0;
    while((prop=Base::freecad_dynamic_cast<PropertyLinkSubHidden>(
                    obj->getPropertyByName("ColoredElements"))))
    {
        for(auto &v : prop->getShadowSubs()) {
            if((boost::starts_with(v.first,subname) || boost::starts_with(v.second,subname))
                    && boost::ends_with(v.second, DocumentObject::hiddenMarker()))
                return true;
        }
        auto o = Base::freecad_dynamic_cast<DocumentObject>(prop->getContainer());
        if(!o)
            break;
        o = o->getLinkedObject(false);
        if(o==obj || !GetApplication().checkLinkDepth(++depth, MessageOption::Error))
            break;
        obj = o;
    }
    return false;
}

void LinkBaseExtension::extensionGetPropertyNamedList(
        std::vector<std::pair<const char *,Property*> > &List) const
{
    inherited::extensionGetPropertyNamedList(List);

    auto owner = getContainer();
    if(!owner || !owner->canLinkProperties())
        return;

    auto linked = getTrueLinkedObject(true);
    if(!linked)
        return;

    std::vector<std::pair<const char*, Property*> > props;
    linked->getPropertyNamedList(props);

    std::unordered_map<const char*, Property*, CStringHasher, CStringHasher> propMap;
    for(auto &v : props) {
        if(!isExcludedProperties(v.first))
            propMap[v.first] = v.second;
    }

    for(auto &v : List) 
        propMap.erase(v.first);

    List.reserve(List.size() + propMap.size());
    for(auto &v : propMap) 
        List.push_back(v);
}

///////////////////////////////////////////////////////////////////////////////////////////

namespace App {
EXTENSION_PROPERTY_SOURCE_TEMPLATE(App::LinkBaseExtensionPython, App::LinkBaseExtension)

// explicit template instantiation
template class AppExport ExtensionPythonT<LinkBaseExtension>;

}

//////////////////////////////////////////////////////////////////////////////

EXTENSION_PROPERTY_SOURCE(App::LinkExtension, App::LinkBaseExtension)

LinkExtension::LinkExtension()
{
    initExtensionType(LinkExtension::getExtensionClassTypeId());

    /*[[[cog
    import Link
    Link.init_link_extension()
    ]]]*/

    // Auto generated code (App/Link.py:240)
    EXTENSION_ADD_PROPERTY_TYPE(Scale, (1.0), " Link", App::Prop_None, getPropertyInfo()[PropIndex::PropScale].doc);
    EXTENSION_ADD_PROPERTY_TYPE(ScaleVector, (Base::Vector3d(1.0, 1.0 ,1.0)), " Link", App::Prop_None, getPropertyInfo()[PropIndex::PropScaleVector].doc);
    EXTENSION_ADD_PROPERTY_TYPE(Matrix, (Base::Matrix4D{}), " Link", App::Prop_Hidden, getPropertyInfo()[PropIndex::PropMatrix].doc);
    EXTENSION_ADD_PROPERTY_TYPE(ScaleList, (std::vector<Base::Vector3d>{}), " Link", App::Prop_Hidden, getPropertyInfo()[PropIndex::PropScaleList].doc);
    EXTENSION_ADD_PROPERTY_TYPE(MatrixList, (std::vector<Base::Matrix4D>{}), " Link", App::Prop_Hidden, getPropertyInfo()[PropIndex::PropMatrixList].doc);
    EXTENSION_ADD_PROPERTY_TYPE(VisibilityList, (boost::dynamic_bitset<>{}), " Link", App::Prop_None, getPropertyInfo()[PropIndex::PropVisibilityList].doc);
    EXTENSION_ADD_PROPERTY_TYPE(PlacementList, (std::vector<Base::Placement>{}), " Link", App::Prop_None, getPropertyInfo()[PropIndex::PropPlacementList].doc);
    EXTENSION_ADD_PROPERTY_TYPE(AutoPlacement, (true), " Link", App::Prop_None, getPropertyInfo()[PropIndex::PropAutoPlacement].doc);
    EXTENSION_ADD_PROPERTY_TYPE(ElementList, (std::vector<App::DocumentObject*>{}), " Link", App::Prop_None, getPropertyInfo()[PropIndex::PropElementList].doc);
    registerProperties();
    //[[[end]]]

    AutoPlacement.setValue(true);
}

/*[[[cog
import Link
Link.define_link_extension()
]]]*/

// Auto generated code (App/Link.py:249)
void LinkExtension::registerProperties()
{
    this->setProperty(PropIndex::PropScale, &Scale);
    this->setProperty(PropIndex::PropScaleVector, &ScaleVector);
    this->setProperty(PropIndex::PropMatrix, &Matrix);
    this->setProperty(PropIndex::PropScaleList, &ScaleList);
    this->setProperty(PropIndex::PropMatrixList, &MatrixList);
    this->setProperty(PropIndex::PropVisibilityList, &VisibilityList);
    this->setProperty(PropIndex::PropPlacementList, &PlacementList);
    this->setProperty(PropIndex::PropAutoPlacement, &AutoPlacement);
    this->setProperty(PropIndex::PropElementList, &ElementList);
}

// Auto generated code (App/Link.py:259)
void LinkExtension::onExtendedDocumentRestored()
{
    registerProperties();
    inherited::onExtendedDocumentRestored();
}
//[[[end]]]

///////////////////////////////////////////////////////////////////////////////////////////

namespace App {
EXTENSION_PROPERTY_SOURCE_TEMPLATE(App::LinkExtensionPython, App::LinkExtension)

// explicit template instantiation
template class AppExport ExtensionPythonT<App::LinkExtension>;

}

/*[[[cog
import Link
Link.define_link()
]]]*/

//////////////////////////////////////////////////////////////////////////////////////////
// Auto generated code (App/Link.py:285)
namespace App {
PROPERTY_SOURCE_TEMPLATE(App::LinkPython, App::Link)
template<> const char* App::LinkPython::getViewProviderName() const {
    return "Gui::ViewProviderLinkPython";
}
template class AppExport FeaturePythonT<App::Link>;
} // namespace App

//////////////////////////////////////////////////////////////////////////////////////////
// Auto generated code (App/Link.py:296)
PROPERTY_SOURCE_WITH_EXTENSIONS(App::Link, App::DocumentObject)
Link::Link()
{
    ADD_PROPERTY_TYPE(LinkedObject, (nullptr), " Link", App::Prop_None, getPropertyInfo()[PropIndex::PropLinkedObject].doc);
    ADD_PROPERTY_TYPE(LinkClaimChild, (false), " Link", App::Prop_None, getPropertyInfo()[PropIndex::PropLinkClaimChild].doc);
    ADD_PROPERTY_TYPE(LinkTransform, (false), " Link", App::Prop_None, getPropertyInfo()[PropIndex::PropLinkTransform].doc);
    ADD_PROPERTY_TYPE(LinkPlacement, (Base::Placement{}), " Link", App::Prop_None, getPropertyInfo()[PropIndex::PropLinkPlacement].doc);
    ADD_PROPERTY_TYPE(Placement, (Base::Placement{}), " Link", App::Prop_None, getPropertyInfo()[PropIndex::PropPlacement].doc);
    ADD_PROPERTY_TYPE(ShowElement, (true), " Link", App::Prop_None, getPropertyInfo()[PropIndex::PropShowElement].doc);
    ADD_PROPERTY_TYPE(SyncGroupVisibility, (false), " Link", App::Prop_None, getPropertyInfo()[PropIndex::PropSyncGroupVisibility].doc);
    ADD_PROPERTY_TYPE(ElementCount, (0), " Link", App::Prop_None, getPropertyInfo()[PropIndex::PropElementCount].doc);
    {// Auto generated code (App/Link.py:99)
        static const PropertyIntegerConstraint::Constraints s_constraints = {0, INT_MAX, 1};
        ElementCount.setConstraints(&s_constraints);
    }
    ADD_PROPERTY_TYPE(LinkExecute, (""), " Link", App::Prop_None, getPropertyInfo()[PropIndex::PropLinkExecute].doc);
    ADD_PROPERTY_TYPE(ColoredElements, (nullptr), " Link", App::Prop_Hidden, getPropertyInfo()[PropIndex::PropColoredElements].doc);
    ADD_PROPERTY_TYPE(LinkCopyOnChange, (long(0)), " Link", App::Prop_None, getPropertyInfo()[PropIndex::PropLinkCopyOnChange].doc);
    ADD_PROPERTY_TYPE(LinkCopyOnChangeSource, (nullptr), " Link", App::Prop_None, getPropertyInfo()[PropIndex::PropLinkCopyOnChangeSource].doc);
    ADD_PROPERTY_TYPE(LinkCopyOnChangeGroup, (nullptr), " Link", App::Prop_None, getPropertyInfo()[PropIndex::PropLinkCopyOnChangeGroup].doc);
    ADD_PROPERTY_TYPE(LinkCopyOnChangeTouched, (false), " Link", App::Prop_None, getPropertyInfo()[PropIndex::PropLinkCopyOnChangeTouched].doc);
    ADD_PROPERTY_TYPE(AutoLinkLabel, (false), " Link", App::Prop_Hidden, getPropertyInfo()[PropIndex::PropAutoLinkLabel].doc);
    registerProperties();
    inherited_extension::initExtension(this);
}

// Auto generated code (App/Link.py:308)
void Link::registerProperties()
{
    this->setProperty(PropIndex::PropLinkedObject, &LinkedObject);
    this->setProperty(PropIndex::PropLinkClaimChild, &LinkClaimChild);
    this->setProperty(PropIndex::PropLinkTransform, &LinkTransform);
    this->setProperty(PropIndex::PropLinkPlacement, &LinkPlacement);
    this->setProperty(PropIndex::PropPlacement, &Placement);
    this->setProperty(PropIndex::PropShowElement, &ShowElement);
    this->setProperty(PropIndex::PropSyncGroupVisibility, &SyncGroupVisibility);
    this->setProperty(PropIndex::PropElementCount, &ElementCount);
    this->setProperty(PropIndex::PropLinkExecute, &LinkExecute);
    this->setProperty(PropIndex::PropColoredElements, &ColoredElements);
    this->setProperty(PropIndex::PropLinkCopyOnChange, &LinkCopyOnChange);
    this->setProperty(PropIndex::PropLinkCopyOnChangeSource, &LinkCopyOnChangeSource);
    this->setProperty(PropIndex::PropLinkCopyOnChangeGroup, &LinkCopyOnChangeGroup);
    this->setProperty(PropIndex::PropLinkCopyOnChangeTouched, &LinkCopyOnChangeTouched);
    this->setProperty(PropIndex::PropAutoLinkLabel, &AutoLinkLabel);
}

// Auto generated code (App/Link.py:318)
void Link::onDocumentRestored()
{
    registerProperties();
    inherited::onDocumentRestored();
}
//[[[end]]]

void Link::setupObject()
{
    inherited::setupObject();
    AutoLinkLabel.setValue(true);
    ShowElement.setValue(LinkParams::getShowElement());
}

bool Link::canLinkProperties() const {
    auto prop = freecad_dynamic_cast<const PropertyXLink>(getLinkedObjectProperty());
    const char *subname;
    if(prop && (subname=prop->getSubName()) && *subname) {
        auto len = strlen(subname);
        // Do not link properties when we are linking to a sub-element (i.e.
        // vertex, edge or face)
        return subname[len-1]=='.';
    }
    return true;
}

/*[[[cog
import Link
Link.define_link_element()
]]]*/

//////////////////////////////////////////////////////////////////////////////////////////
// Auto generated code (App/Link.py:285)
namespace App {
PROPERTY_SOURCE_TEMPLATE(App::LinkElementPython, App::LinkElement)
template<> const char* App::LinkElementPython::getViewProviderName() const {
    return "Gui::ViewProviderLinkPython";
}
template class AppExport FeaturePythonT<App::LinkElement>;
} // namespace App

//////////////////////////////////////////////////////////////////////////////////////////
// Auto generated code (App/Link.py:296)
PROPERTY_SOURCE_WITH_EXTENSIONS(App::LinkElement, App::DocumentObject)
LinkElement::LinkElement()
{
    ADD_PROPERTY_TYPE(Scale, (1.0), " Link", App::Prop_None, getPropertyInfo()[PropIndex::PropScale].doc);
    ADD_PROPERTY_TYPE(ScaleVector, (Base::Vector3d(1.0, 1.0 ,1.0)), " Link", App::Prop_None, getPropertyInfo()[PropIndex::PropScaleVector].doc);
    ADD_PROPERTY_TYPE(Matrix, (Base::Matrix4D{}), " Link", App::Prop_Hidden, getPropertyInfo()[PropIndex::PropMatrix].doc);
    ADD_PROPERTY_TYPE(LinkedObject, (nullptr), " Link", App::Prop_None, getPropertyInfo()[PropIndex::PropLinkedObject].doc);
    ADD_PROPERTY_TYPE(LinkClaimChild, (false), " Link", App::Prop_None, getPropertyInfo()[PropIndex::PropLinkClaimChild].doc);
    ADD_PROPERTY_TYPE(LinkTransform, (false), " Link", App::Prop_None, getPropertyInfo()[PropIndex::PropLinkTransform].doc);
    ADD_PROPERTY_TYPE(LinkPlacement, (Base::Placement{}), " Link", App::Prop_None, getPropertyInfo()[PropIndex::PropLinkPlacement].doc);
    ADD_PROPERTY_TYPE(Placement, (Base::Placement{}), " Link", App::Prop_None, getPropertyInfo()[PropIndex::PropPlacement].doc);
    ADD_PROPERTY_TYPE(LinkCopyOnChange, (long(0)), " Link", App::Prop_None, getPropertyInfo()[PropIndex::PropLinkCopyOnChange].doc);
    ADD_PROPERTY_TYPE(LinkCopyOnChangeSource, (nullptr), " Link", App::Prop_None, getPropertyInfo()[PropIndex::PropLinkCopyOnChangeSource].doc);
    ADD_PROPERTY_TYPE(LinkCopyOnChangeGroup, (nullptr), " Link", App::Prop_None, getPropertyInfo()[PropIndex::PropLinkCopyOnChangeGroup].doc);
    ADD_PROPERTY_TYPE(LinkCopyOnChangeTouched, (false), " Link", App::Prop_None, getPropertyInfo()[PropIndex::PropLinkCopyOnChangeTouched].doc);
    registerProperties();
    inherited_extension::initExtension(this);
}

// Auto generated code (App/Link.py:308)
void LinkElement::registerProperties()
{
    this->setProperty(PropIndex::PropScale, &Scale);
    this->setProperty(PropIndex::PropScaleVector, &ScaleVector);
    this->setProperty(PropIndex::PropMatrix, &Matrix);
    this->setProperty(PropIndex::PropLinkedObject, &LinkedObject);
    this->setProperty(PropIndex::PropLinkClaimChild, &LinkClaimChild);
    this->setProperty(PropIndex::PropLinkTransform, &LinkTransform);
    this->setProperty(PropIndex::PropLinkPlacement, &LinkPlacement);
    this->setProperty(PropIndex::PropPlacement, &Placement);
    this->setProperty(PropIndex::PropLinkCopyOnChange, &LinkCopyOnChange);
    this->setProperty(PropIndex::PropLinkCopyOnChangeSource, &LinkCopyOnChangeSource);
    this->setProperty(PropIndex::PropLinkCopyOnChangeGroup, &LinkCopyOnChangeGroup);
    this->setProperty(PropIndex::PropLinkCopyOnChangeTouched, &LinkCopyOnChangeTouched);
}

// Auto generated code (App/Link.py:318)
void LinkElement::onDocumentRestored()
{
    registerProperties();
    inherited::onDocumentRestored();
}
//[[[end]]]

bool LinkElement::canDelete() const {
    if(!_LinkOwner.getValue())
        return true;

    auto owner = getContainer();
    return !owner || !owner->getDocument()->getObjectByID(_LinkOwner.getValue());
}

/*[[[cog
import Link
Link.define_link_group()
]]]*/

//////////////////////////////////////////////////////////////////////////////////////////
// Auto generated code (App/Link.py:285)
namespace App {
PROPERTY_SOURCE_TEMPLATE(App::LinkGroupPython, App::LinkGroup)
template<> const char* App::LinkGroupPython::getViewProviderName() const {
    return "Gui::ViewProviderLinkPython";
}
template class AppExport FeaturePythonT<App::LinkGroup>;
} // namespace App

//////////////////////////////////////////////////////////////////////////////////////////
// Auto generated code (App/Link.py:296)
PROPERTY_SOURCE_WITH_EXTENSIONS(App::LinkGroup, App::DocumentObject)
LinkGroup::LinkGroup()
{
    ADD_PROPERTY_TYPE(ElementList, (std::vector<App::DocumentObject*>{}), " Link", App::Prop_None, getPropertyInfo()[PropIndex::PropElementList].doc);
    ADD_PROPERTY_TYPE(Placement, (Base::Placement{}), " Link", App::Prop_None, getPropertyInfo()[PropIndex::PropPlacement].doc);
    ADD_PROPERTY_TYPE(VisibilityList, (boost::dynamic_bitset<>{}), " Link", App::Prop_None, getPropertyInfo()[PropIndex::PropVisibilityList].doc);
    ADD_PROPERTY_TYPE(LinkMode, (long(0)), " Link", App::Prop_None, getPropertyInfo()[PropIndex::PropLinkMode].doc);
    ADD_PROPERTY_TYPE(ColoredElements, (nullptr), " Link", App::Prop_Hidden, getPropertyInfo()[PropIndex::PropColoredElements].doc);
    registerProperties();
    inherited_extension::initExtension(this);
}

// Auto generated code (App/Link.py:308)
void LinkGroup::registerProperties()
{
    this->setProperty(PropIndex::PropElementList, &ElementList);
    this->setProperty(PropIndex::PropPlacement, &Placement);
    this->setProperty(PropIndex::PropVisibilityList, &VisibilityList);
    this->setProperty(PropIndex::PropLinkMode, &LinkMode);
    this->setProperty(PropIndex::PropColoredElements, &ColoredElements);
}

// Auto generated code (App/Link.py:318)
void LinkGroup::onDocumentRestored()
{
    registerProperties();
    inherited::onDocumentRestored();
}
//[[[end]]]
