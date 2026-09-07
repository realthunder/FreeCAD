/***************************************************************************
 *   Copyright (c) 2026 FreeCAD contributors                               *
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
# include <cstdlib>
# include <cstring>
# include <map>
# include <vector>
# include <BRep_Builder.hxx>
# include <Standard_Failure.hxx>
# include <TopoDS_Compound.hxx>
# include <TopoDS_Iterator.hxx>
#endif

#include <App/Document.h>
#include <App/DocumentObject.h>
#include <App/ElementNamingUtils.h>
#include <App/GeoFeature.h>
#include <App/PropertyLinks.h>
#include <App/PropertyStandard.h>
#include <Base/Console.h>
#include <Base/Exception.h>

#include "ForeignBaseShapes.h"
#include "PartFeature.h"
#include "PropertyTopoShape.h"
#include "TopoShape.h"

FC_LOG_LEVEL_INIT("Part", true, true)

using namespace Part;

const char *ForeignBaseShapes::shapesName()
{
    return "_ForeignBaseShapes";
}

const char *ForeignBaseShapes::refsName()
{
    return "_ForeignBaseShapeRefs";
}

std::string ForeignBaseShapes::referenceKey(const App::PropertyLinkBase *referrer,
                                            const App::DocumentObject *obj,
                                            const char *subname)
{
    std::string key;
    if (!referrer || !obj || !subname || !obj->getDocument())
        return key;
    auto owner = Base::freecad_dynamic_cast<const App::DocumentObject>(referrer->getContainer());
    if (!owner || !owner->getDocument())
        return key;
    if (obj->getDocument() != owner->getDocument()) {
        // The document part is the path the link persists, not the runtime
        // name of the document, which is not stable from one session to the
        // next.
        auto xlink = Base::freecad_dynamic_cast<const App::PropertyXLink>(referrer);
        if (!xlink)
            return key;
        try {
            key = xlink->getFilePath(xlink->getPathResolveMode());
        }
        catch (Base::Exception &) {
            return std::string();
        }
        if (key.empty())
            return key;
        key += '#';
    }
    key += obj->getNameInDocument();
    key += '.';
    key += subname;
    return key;
}

static PropertyPartShape *shapesProperty(const App::Document &doc)
{
    return Base::freecad_dynamic_cast<PropertyPartShape>(
            doc.getDynamicPropertyByName(ForeignBaseShapes::shapesName()));
}

static App::PropertyMap *refsProperty(const App::Document &doc)
{
    return Base::freecad_dynamic_cast<App::PropertyMap>(
            doc.getDynamicPropertyByName(ForeignBaseShapes::refsName()));
}

/// The children of the compound a store holds, in order
static std::vector<TopoDS_Shape> children(const PropertyPartShape *prop)
{
    std::vector<TopoDS_Shape> res;
    if (!prop)
        return res;
    // Held for the loop: getShape() returns the value, not a reference
    const TopoShape held = prop->getShape();
    const TopoDS_Shape &compound = held.getShape();
    if (compound.IsNull())
        return res;
    for (TopoDS_Iterator it(compound); it.More(); it.Next())
        res.push_back(it.Value());
    return res;
}

/// The child a manifest entry names, or a null shape
static TopoDS_Shape child(const std::vector<TopoDS_Shape> &shapes,
                          const std::map<std::string, std::string> &refs,
                          const std::string &key)
{
    auto it = refs.find(key);
    if (it == refs.end())
        return TopoDS_Shape();
    long index = std::strtol(it->second.c_str(), nullptr, 10);
    if (index < 1 || static_cast<size_t>(index) > shapes.size())
        return TopoDS_Shape();
    return shapes[index - 1];
}

TopoShape ForeignBaseShapes::find(const App::Document *doc, const std::string &key)
{
    if (!doc || key.empty())
        return TopoShape();
    auto refs = refsProperty(*doc);
    if (!refs || !refs->getValues().count(key))
        return TopoShape();
    return TopoShape(child(children(shapesProperty(*doc)), refs->getValues(), key));
}

void ForeignBaseShapes::rebuild(App::Document &doc)
{
    auto shapesProp = shapesProperty(doc);
    auto refsProp = refsProperty(doc);
    const std::vector<TopoDS_Shape> oldChildren = children(shapesProp);
    const std::map<std::string, std::string> oldRefs = refsProp
        ? refsProp->getValues() : std::map<std::string, std::string>();

    // One entry per distinct reference name, in name order so that the same
    // references produce the same compound.
    std::map<std::string, TopoDS_Shape> entries;
    std::vector<App::DocumentObject *> objs;
    std::vector<std::string> subs;
    for (auto &v : App::PropertyLinkBase::getExternalElementReferences(&doc)) {
        auto feature = Base::freecad_dynamic_cast<Feature>(v.first);
        if (!feature)
            continue;
        for (auto prop : v.second) {
            objs.clear();
            subs.clear();
            prop->getLinks(objs, true, &subs, true);
            if (objs.empty())
                continue;
            for (size_t i = 0; i < subs.size(); ++i) {
                // A sub-list pairs objects and sub-names; a single link holds
                // one object for all of its sub-names.
                auto obj = objs.size() == subs.size() ? objs[i] : objs.front();
                std::pair<std::string, std::string> elementName;
                const char *element = nullptr;
                App::GeoFeature *geo = nullptr;
                App::GeoFeature::resolveElement(obj, subs[i].c_str(), elementName, true,
                        App::GeoFeature::ElementNameType::Export, nullptr, &element, &geo);
                if (geo != feature || !element || !element[0])
                    continue;
                // The indexed element name.  A reference already marked
                // missing carries it in its own sub-name, after the mapped
                // name (';Face6;...,F.?Face3'); the resolver has nothing to
                // say about it.  A healthy one is read from the resolution,
                // which may itself have just gone missing.
                bool missing = Data::hasMissingElement(element);
                std::string indexed;
                if (missing) {
                    const char *dot = strrchr(element, '.');
                    const char *name = dot ? dot + 1 : element;
                    if (Data::hasMissingElement(name))
                        name += Data::missingPrefix().size();
                    indexed = name;
                }
                else {
                    const char *name = Data::findElementName(elementName.second.c_str());
                    if (!name || !name[0])
                        continue;
                    if (Data::hasMissingElement(name)) {
                        missing = true;
                        name += Data::missingPrefix().size();
                    }
                    indexed = name;
                }
                if (indexed.empty())
                    continue;
                std::string sub(subs[i].c_str(), element - subs[i].c_str());
                sub += indexed;
                std::string key = referenceKey(prop, obj, sub.c_str());
                if (key.empty() || entries.count(key))
                    continue;
                TopoDS_Shape shape;
                if (missing) {
                    // The reference cannot be refreshed: it keeps the child
                    // it has, which is what a later search asks about.
                    shape = child(oldChildren, oldRefs, key);
                }
                else {
                    const std::string *prefix = nullptr;
                    auto propShape = feature->shapePropertyOfElement(indexed.c_str(), &prefix);
                    TopoShape subShape = propShape->getShape().getSubTopoShape(
                            indexed.c_str() + (prefix ? prefix->size() : 0), true);
                    // Bare geometry: the element map is the feature's business
                    shape = subShape.getShape();
                }
                if (!shape.IsNull())
                    entries.emplace(std::move(key), shape);
            }
        }
    }

    // The document's own dynamic properties are added and removed through
    // the container's plain interface: the store is a save artifact, not a
    // change the undo stack should hold.
    if (entries.empty()) {
        if (shapesProp)
            doc.App::PropertyContainer::removeDynamicProperty(shapesName());
        if (refsProp)
            doc.App::PropertyContainer::removeDynamicProperty(refsName());
        return;
    }

    // Unchanged since the last rebuild: the same names, the same children
    // in the same order.  Then the property keeps its value and the file the
    // last save wrote for it.
    bool same = shapesProp && refsProp
        && oldChildren.size() == entries.size() && oldRefs.size() == entries.size();
    if (same) {
        size_t i = 0;
        for (auto &e : entries) {
            auto it = oldRefs.find(e.first);
            if (it == oldRefs.end() || it->second != std::to_string(i + 1)
                                   || !oldChildren[i].IsSame(e.second)) {
                same = false;
                break;
            }
            ++i;
        }
    }
    if (same)
        return;

    BRep_Builder builder;
    TopoDS_Compound compound;
    builder.MakeCompound(compound);
    std::map<std::string, std::string> refs;
    int index = 0;
    for (auto &e : entries) {
        builder.Add(compound, e.second);
        refs[e.first] = std::to_string(++index);
    }

    const auto attr = App::Prop_Hidden | App::Prop_ReadOnly | App::Prop_Output | App::Prop_NoRecompute;
    if (!shapesProp) {
        shapesProp = Base::freecad_dynamic_cast<PropertyPartShape>(
                doc.App::PropertyContainer::addDynamicProperty("Part::PropertyPartShape",
                    shapesName(), "BaseShape",
                    "The sub-shapes the element references into other documents were resolved against",
                    attr, true, true));
    }
    if (!refsProp) {
        refsProp = Base::freecad_dynamic_cast<App::PropertyMap>(
                doc.App::PropertyContainer::addDynamicProperty("App::PropertyMap",
                    refsName(), "BaseShape",
                    "Reference name -> child index in the sub-shape compound",
                    attr, true, true));
    }
    if (!shapesProp || !refsProp) {
        FC_ERR("Failed to create the foreign base shape store of " << doc.getName());
        return;
    }
    // Borrows from the files of this save, publishes nothing: a child that
    // is dropped must not leave a borrower behind.
    shapesProp->_publishes = false;
    shapesProp->setValue(TopoShape(compound));
    refsProp->setValues(std::move(refs));
}

void ForeignBaseShapes::onStartSaveDocument(const App::Document &doc, const std::string &filename)
{
    (void)filename;
    if (doc.testStatus(App::Document::PartialDoc))
        return;
    try {
        rebuild(const_cast<App::Document &>(doc));
    }
    catch (Base::Exception &e) {
        FC_ERR("Failed to rebuild the foreign base shape store of " << doc.getName()
                << ": " << e.what());
    }
    catch (Standard_Failure &e) {
        FC_ERR("Failed to rebuild the foreign base shape store of " << doc.getName()
                << ": " << e.GetMessageString());
    }
}
