/****************************************************************************
 *   Copyright (c) 2022 Zheng Lei (realthunder) <realthunder.dev@gmail.com> *
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

#ifndef _PreComp_
#endif // _PreComp_

#include <Base/Console.h>
#include <Base/Writer.h>
#include <Base/Reader.h>
#include <Base/Exception.h>
#include <Base/Interpreter.h>
#include <Base/Stream.h>
#include <Base/QuantityPy.h>
#include <App/Application.h>
#include <App/Document.h>
#include <App/DocumentObject.h>
#include <App/ObjectIdentifier.h>

#include "PropertyDressUp.h"

using namespace Part;

TYPESYSTEM_SOURCE(Part::PropertyFilletSegments , App::Property)

PropertyFilletSegments::PropertyFilletSegments()
{
}

PropertyFilletSegments::~PropertyFilletSegments()
{
}

void PropertyFilletSegments::setValue(const std::string &id, double param, double radius, double length)
{
    auto it = segmentsMap.find(id);
    if (param >= 0.0 && param <= 1.0 && it == segmentsMap.end())
        return;
    if (length > 0.0) {
        for (auto &segment : it->second) {
            if (segment.length == length) {
                aboutToSetValue();
                segment.param = 0.0;
                segment.radius = radius;
                hasSetValue();
                return;
            }
        }
    } else {
        for (auto &segment : it->second) {
            if (segment.param == param) {
                aboutToSetValue();
                segment.radius = radius;
                segment.length = 0.0;
                hasSetValue();
                return;
            }
        }
    }
    aboutToSetValue();
    it->second.emplace_back(param, radius);
    hasSetValue();
}

void PropertyFilletSegments::setValue(const std::string &id, Segments &&segments)
{
    aboutToSetValue();
    segmentsMap[id] = std::move(segments);
    hasSetValue();
}

void PropertyFilletSegments::removeValue(const std::string &id, double param, double length)
{
    auto it = segmentsMap.find(id);
    if (it == segmentsMap.end())
        return;
    AtomicPropertyChange signaller(*this, false);
    if (length > 0.0) {
        for (auto iter=it->second.begin(); iter!=it->second.end(); ++iter) {
            if (iter->length == length) {
                signaller.aboutToChange();
                it->second.erase(iter);
            }
        }
    } else {
        for (auto iter=it->second.begin(); iter!=it->second.end(); ++iter) {
            if (iter->param == param) {
                signaller.aboutToChange();
                it->second.erase(iter);
            }
        }
    }
    signaller.tryInvoke();
}

void PropertyFilletSegments::removeValue(const std::string &id, int index)
{
    auto it = segmentsMap.find(id);
    if (it == segmentsMap.end() || index < 0 || index >= (int)it->second.size())
        return;
    aboutToSetValue();
    it->second.erase(it->second.begin() + index);
    hasSetValue();
}

void PropertyFilletSegments::removeValue(const std::string &id)
{
    auto it = segmentsMap.find(id);
    if (it == segmentsMap.end())
        return;
    aboutToSetValue();
    segmentsMap.erase(it);
    hasSetValue();
}

void PropertyFilletSegments::setValue(std::map<std::string, Segments> &&values)
{
    aboutToSetValue();
    segmentsMap = std::move(values);
    hasSetValue();
}

void PropertyFilletSegments::setValue(const std::map<std::string, Segments> &values)
{
    aboutToSetValue();
    segmentsMap = values;
    hasSetValue();
}

const std::map<std::string, PropertyFilletSegments::Segments> &
PropertyFilletSegments::getValue() const
{
    return segmentsMap;
}

const PropertyFilletSegments::Segments &
PropertyFilletSegments::getValue(const std::string &sub) const
{
    static const Segments nulSegment;
    auto it = segmentsMap.find(sub);
    if (it == segmentsMap.end())
        return nulSegment;
    return it->second;
}

void PropertyFilletSegments::connectLinkProperty(App::PropertyLinkSub &links)
{
    connChanged = links.signalChanged.connect(
        [this](const App::Property &prop) {
            std::map<std::string, Segments> value;
            auto obj = Base::freecad_dynamic_cast<App::DocumentObject>(getContainer());
            std::map<App::ObjectIdentifier, App::ObjectIdentifier> renames;
            for (const auto &sub : static_cast<const App::PropertyLinkSub&>(prop).getSubValues(false)) {
                auto it = referenceUpdates.find(sub);
                const auto &key = it==referenceUpdates.end() ? sub : it->second;
                auto iter = segmentsMap.find(key);
                if (iter == segmentsMap.end())
                    continue;
                if (obj) {
                    App::ObjectIdentifier path(*this);
                    path << App::ObjectIdentifier::SimpleComponent(sub);
                    App::ObjectIdentifier pathNew(*this);
                    pathNew << App::ObjectIdentifier::SimpleComponent(key);
                    renames.emplace(path, pathNew);
                    for (int i=0; i<(int)iter->second.size(); ++i) {
                        App::ObjectIdentifier p1(path);
                        p1 << App::ObjectIdentifier::ArrayComponent(i);
                        App::ObjectIdentifier p2(path);
                        p2 << App::ObjectIdentifier::ArrayComponent(i);
                        renames.emplace(p1, p2);
                        ++i;
                        renames.emplace(App::ObjectIdentifier(p1) << App::ObjectIdentifier::SimpleComponent("Radius"),
                                        App::ObjectIdentifier(p2) << App::ObjectIdentifier::SimpleComponent("Radius"));
                        renames.emplace(App::ObjectIdentifier(p1) << App::ObjectIdentifier::SimpleComponent("Param"),
                                        App::ObjectIdentifier(p2) << App::ObjectIdentifier::SimpleComponent("Param"));
                        renames.emplace(App::ObjectIdentifier(p1) << App::ObjectIdentifier::SimpleComponent("Length"),
                                        App::ObjectIdentifier(p2) << App::ObjectIdentifier::SimpleComponent("Length"));
                    }
                }
                value[sub] = iter->second;
            }
            if (value != segmentsMap)
                setValue(std::move(value));
            if (obj) {
                obj->ExpressionEngine.renameExpressions(renames);
                for (auto doc : App::GetApplication().getDocuments())
                    doc->renameObjectIdentifiers(renames);
            }
            referenceUpdates.clear();
        });

    connUpdateReference = links.signalUpdateElementReference.connect(
        [this](const std::string &sub, const std::string &newSub) {
            referenceUpdates.emplace(newSub, sub);
        });
}

PyObject *PropertyFilletSegments::getPyObject(void)
{
    Py::List list(segmentsMap.size());
    int i = 0;
    for (auto &v : segmentsMap) {
        Py::List entry(v.second.size());
        int j=0;
        for (auto &segment : v.second)
            entry[j++] = Py::TupleN(Py::Float(segment.param),
                                    Py::Float(segment.radius),
                                    Py::Float(segment.length));
        list[i++] = entry;
    }
    return Py::new_reference_to(list);
}

void PropertyFilletSegments::setPyObject(PyObject *pyobj)
{
    const char *msg = "Expect the input to be type of Sequence(Tuple(element:String, Sequence(param:Float, radius:Float, length:Float)))";
    try {
        std::map<std::string, Segments> value;
        Py::Sequence seq(pyobj);
        for (int i=0; i<seq.size(); ++i) {
            Py::Sequence item(seq[i].ptr());
            if (item.size() != 2)
                THROWM(Base::TypeError, msg)
            auto &segments = value[Py::String(item[0].ptr())];
            Py::Sequence pySegments(Py::Sequence(item[1].ptr()));
            for (int j=0; j<pySegments.size(); ++j) {
                Py::Sequence pySegment(pySegments[j].ptr());
                if (pySegment.size() < 2 || pySegment.size() > 3)
                    THROWM(Base::TypeError, msg)
                segments.emplace_back(Py::Float(pySegment[0].ptr()),
                                      Py::Float(pySegment[1].ptr()),
                                      pySegment.size()==2?0.0:Py::Float(pySegment[2].ptr()));
            }
        }
        if (value != segmentsMap)
            setValue(std::move(value));
    } catch (Py::Exception &) {
        THROWM(Base::TypeError, msg)
    }
}

void PropertyFilletSegments::Save(Base::Writer &writer) const {
    int count = 0;
    for (auto &v : segmentsMap) {
        if (!v.second.empty())
            ++count;
    }
    if (count == 0) {
        writer.Stream() << writer.ind() << "<EdgeSegments/>\n";
        return;
    }

    writer.Stream() << writer.ind() << "<EdgeSegments count=\"" <<  count <<"\">\n";
    writer.incInd();
    for (auto &v : segmentsMap) {
        if (v.second.empty())
            continue;
        writer.Stream() << writer.ind() << "<Segments id=\"" <<  encodeAttribute(v.first)
                        << "\" count=\"" << v.second.size() << "\">\n";
        writer.incInd();
        for (const auto &segment : v.second)
            writer.Stream() << writer.ind() << "<Segment param=\"" << segment.param
                            << "\" radius=\"" << segment.radius
                            << "\" length=\"" << segment.length << "\"/>\n";
        writer.decInd();
        writer.Stream() << writer.ind() << "</Segments>\n";
    }
    writer.decInd();
    writer.Stream() << writer.ind() << "</EdgeSegments>\n" ;
}

void PropertyFilletSegments::Restore(Base::XMLReader &reader)
{
    reader.readElement("EdgeSegments");
    unsigned count = reader.getAttributeAsUnsigned("count", "0");
    std::map<std::string, Segments> value;
    for (unsigned i=0; i<count; ++i) {
        reader.readElement("Segments");
        std::string id = reader.getAttribute("id");
        if (!id.empty()) {
            auto &segments = value[id];
            unsigned scount = reader.getAttributeAsUnsigned("count","0");
            for (unsigned j=0; j<scount; ++j) {
                reader.readElement("Segment");
                segments.emplace_back(reader.getAttributeAsFloat("param"),
                                    reader.getAttributeAsFloat("radius"),
                                    reader.getAttributeAsFloat("length"));
            }
        }
        reader.readEndElement("Segments");
    }
    reader.readEndElement("EdgeSegments");
    setValue(std::move(value));
}

void PropertyFilletSegments::getPaths(std::vector<App::ObjectIdentifier> &paths) const
{
    for(auto &v : segmentsMap)
        paths.push_back(App::ObjectIdentifier(*this) << App::ObjectIdentifier::SimpleComponent(v.first));
}

bool PropertyFilletSegments::setPyPathValue(const App::ObjectIdentifier & path, const Py::Object &value)
{
    if(path.numSubComponents()<2
            || path.numSubComponents()>4
            || path.getPropertyComponent(0).getName()!=getName())
        FC_THROWM(Base::ValueError,"invalid constraint path " << path.toString());

    const App::ObjectIdentifier::Component & c1 = path.getPropertyComponent(1);
    if (!c1.isSimple())
        FC_THROWM(Base::ValueError,"invalid path " << path.toString());

    try {
        if (path.numSubComponents() == 2) {
            if (value.isNone()) {
                removeValue(c1.getName());
                return true;
            }
            std::vector<Segment> segments;
            Py::Sequence seq(value);
            for (int i=0; i<seq.size(); ++i) {
                Py::Sequence pySegment(seq[i].ptr());
                if (pySegment.size() != 2)
                    FC_THROWM(Base::TypeError,"invalid value");
                segments.emplace_back(Py::Float(pySegment[0]),
                                      Py::Float(pySegment[1]));
            }
            setValue(c1.getName(), std::move(segments));
            return true;
        }
        const App::ObjectIdentifier::Component & c2 = path.getPropertyComponent(2);
        if (!c2.isArray())
            FC_THROWM(Base::ValueError,"invalid path " << path.toString());
        aboutToSetValue();
        auto &segments = segmentsMap[c1.getName()];
        size_t index = c2.getIndex(segments.size());
        if (path.numSubComponents() > 3) {
            const App::ObjectIdentifier::Component & c3 = path.getPropertyComponent(3);
            if (!c3.isSimple())
                FC_THROWM(Base::ValueError,"invalid path " << path.toString());
            if (c3.getName() == "Radius")
                segments[index].radius = Py::Float(value);
            else if (c3.getName() == "Param")
                segments[index].param = Py::Float(value);
            else if (c3.getName() == "Length")
                segments[index].length = Py::Float(value);
            else
                FC_THROWM(Base::ValueError,"invalid path " << path.toString());
        } else {
            Py::Sequence seq(value);
            if (seq.size() == 2) {
                segments[index].param = Py::Float(seq[0]);
                segments[index].radius = Py::Float(seq[1]);
            } else if (seq.size() == 3) {
                segments[index].param = Py::Float(seq[0]);
                segments[index].radius = Py::Float(seq[1]);
                segments[index].length = Py::Float(seq[1]);
            } else
                FC_THROWM(Base::TypeError,"invalid value");
        }
        hasSetValue();
    } catch (Py::Exception &) {
        FC_THROWM(Base::TypeError,"invalid value");
    }
    return true;
}

bool PropertyFilletSegments::getPyPathValue(const App::ObjectIdentifier &path, Py::Object &res) const {

    auto components = path.getPropertyComponents(1);
    if(components.size() < 1 || components.size() > 3)
        return false;

    const App::ObjectIdentifier::Component & c1 = components[0];
    const Segments *segments = nullptr;
    if (c1.isSimple()) {
        auto it = segmentsMap.find(c1.getName());
        if (it != segmentsMap.end())
            segments = &it->second;
    }
    if (!segments)
        return false;

    if (components.size() == 1) {
        Py::List list(segments->size());
        int i = 0;
        for (const auto &segment : *segments) {
            list[i++] = Py::TupleN(Py::Float(segment.param),
                                   Py::asObject(new Base::QuantityPy(new Base::Quantity(segment.radius, Base::Unit::Length))),
                                   Py::asObject(new Base::QuantityPy(new Base::Quantity(segment.length, Base::Unit::Length))));
        }
        res = list;
        return true;
    }

    const App::ObjectIdentifier::Component & c2 = components[1];
    if (!c2.isArray())
        return false;
    std::size_t index = c2.getIndex(segments->size());
    const auto &segment = (*segments)[index];
    if (components.size() == 2) {
        res = Py::TupleN(Py::Float(segment.param),
                         Py::asObject(new Base::QuantityPy(new Base::Quantity(segment.radius, Base::Unit::Length))),
                         Py::asObject(new Base::QuantityPy(new Base::Quantity(segment.length, Base::Unit::Length))));
        return true;
    }

    const App::ObjectIdentifier::Component & c3 = components[2];
    if (!c3.isSimple())
        return false;
    if (c3.getName() == "Radius")
        res = Py::Float(segment.radius);
    else if (c3.getName() == "Param")
        res = Py::Float(segment.param);
    else if (c3.getName() == "Length")
        res = Py::Float(segment.length);
    else
        return false;
    return true;
}

void PropertyFilletSegments::setPathValue(const App::ObjectIdentifier &path, const App::any &value)
{
    Base::PyGILStateLocker lock;
    setPyPathValue(path, pyObjectFromAny(value));
}

App::any PropertyFilletSegments::getPathValue(const App::ObjectIdentifier &path) const
{
    Base::PyGILStateLocker lock;
    Py::Object pyObj;
    if (getPyPathValue(path, pyObj))
        return App::any();
    return App::pyObjectToAny(pyObj);
}

App::Property *PropertyFilletSegments::Copy() const
{
    PropertyFilletSegments *p= new PropertyFilletSegments();
    p->segmentsMap = segmentsMap;
    return p;
}

void PropertyFilletSegments::Paste(const Property &from)
{
    setValue(dynamic_cast<const PropertyFilletSegments&>(from).segmentsMap);
}

unsigned int PropertyFilletSegments::getMemSize() const
{
    unsigned int size = 0;
    for (const auto &v : segmentsMap)
        size += v.second.size() & sizeof(Segment);
    return size;
}

bool PropertyFilletSegments::isSame(const Property &other) const
{
    if (this == &other)
        return true;
    if (!other.isDerivedFrom(getClassTypeId()))
        return false;
    return segmentsMap == static_cast<const PropertyFilletSegments&>(other).segmentsMap;
}

// -------------------------------------------------------------------------

TYPESYSTEM_SOURCE(Part::PropertyChamferEdges , App::Property)

PropertyChamferEdges::PropertyChamferEdges()
{
}

PropertyChamferEdges::~PropertyChamferEdges()
{
}

void PropertyChamferEdges::setValue(const std::string &id, const ChamferInfo &info)
{
    aboutToSetValue();
    chamferEdgeMap[id] = info;
    hasSetValue();
}

void PropertyChamferEdges::removeValue(const std::string &id)
{
    auto it = chamferEdgeMap.find(id);
    if (it == chamferEdgeMap.end())
        return;
    aboutToSetValue();
    chamferEdgeMap.erase(it);
    hasSetValue();
}

void PropertyChamferEdges::setValue(std::map<std::string, ChamferInfo> &&values)
{
    aboutToSetValue();
    chamferEdgeMap = std::move(values);
    hasSetValue();
}

void PropertyChamferEdges::setValue(const std::map<std::string, ChamferInfo> &values)
{
    aboutToSetValue();
    chamferEdgeMap = values;
    hasSetValue();
}

const std::map<std::string, PropertyChamferEdges::ChamferInfo> &
PropertyChamferEdges::getValue() const
{
    return chamferEdgeMap;
}

bool PropertyChamferEdges::getValue(const std::string &sub, ChamferInfo &info) const
{
    auto it = chamferEdgeMap.find(sub);
    if (it == chamferEdgeMap.end())
        return false;
    info = it->second;
    return true;
}

void PropertyChamferEdges::connectLinkProperty(App::PropertyLinkSub &links)
{
    connChanged = links.signalChanged.connect(
        [this](const App::Property &prop) {
            std::map<std::string, ChamferInfo> value;
            auto obj = Base::freecad_dynamic_cast<App::DocumentObject>(getContainer());
            std::map<App::ObjectIdentifier, App::ObjectIdentifier> renames;
            for (const auto &sub : static_cast<const App::PropertyLinkSub&>(prop).getSubValues(false)) {
                auto it = referenceUpdates.find(sub);
                const auto &key = it==referenceUpdates.end() ? sub : it->second;
                auto iter = chamferEdgeMap.find(key);
                if (iter == chamferEdgeMap.end())
                    continue;
                if (obj) {
                    App::ObjectIdentifier path(*this);
                    path << App::ObjectIdentifier::SimpleComponent(sub);
                    App::ObjectIdentifier pathNew(*this);
                    pathNew << App::ObjectIdentifier::SimpleComponent(key);
                    renames.emplace(path, pathNew);
                    renames.emplace(App::ObjectIdentifier(path) << App::ObjectIdentifier::SimpleComponent("Size"),
                                    App::ObjectIdentifier(pathNew) << App::ObjectIdentifier::SimpleComponent("Size"));
                    renames.emplace(App::ObjectIdentifier(path) << App::ObjectIdentifier::SimpleComponent("Size2"),
                                    App::ObjectIdentifier(pathNew) << App::ObjectIdentifier::SimpleComponent("Size2"));
                    renames.emplace(App::ObjectIdentifier(path) << App::ObjectIdentifier::SimpleComponent("Angle"),
                                    App::ObjectIdentifier(pathNew) << App::ObjectIdentifier::SimpleComponent("Angle"));
                    renames.emplace(App::ObjectIdentifier(path) << App::ObjectIdentifier::SimpleComponent("Flip"),
                                    App::ObjectIdentifier(pathNew) << App::ObjectIdentifier::SimpleComponent("Flip"));
                }
                value[sub] = iter->second;
            }
            if (value != chamferEdgeMap)
                setValue(std::move(value));
            if (obj) {
                obj->ExpressionEngine.renameExpressions(renames);
                for (auto doc : App::GetApplication().getDocuments())
                    doc->renameObjectIdentifiers(renames);
            }
            referenceUpdates.clear();
        });

    connUpdateReference = links.signalUpdateElementReference.connect(
        [this](const std::string &sub, const std::string &newSub) {
            referenceUpdates.emplace(newSub, sub);
        });
}

PyObject *PropertyChamferEdges::getPyObject(void)
{
    Py::List list(chamferEdgeMap.size());
    int i = 0;
    for (auto &v : chamferEdgeMap) {
        const auto &info = v.second;
        list[i++] = Py::TupleN(Py::String(v.first),
                               Py::Float(info.size),
                               Py::Float(info.size2),
                               Py::Boolean(info.angle),
                               Py::Boolean(info.flip));
    }
    return Py::new_reference_to(list);
}

void PropertyChamferEdges::setPyObject(PyObject *pyobj)
{
    const char *msg = "Expect the input to be type of Sequence(Tuple(element:String, size:Float, size2:Float, angle:Float, flip::Boolean))";
    try {
        std::map<std::string, ChamferInfo> value;
        Py::Sequence seq(pyobj);
        for (int i=0; i<seq.size(); ++i) {
            Py::Sequence item(seq[i].ptr());
            if (item.size() <= 1 || item.size() >= 4)
                THROWM(Base::TypeError, msg)
            auto &info = value[Py::String(item[0].ptr())];
            info.size = Py::Float(item[1].ptr());
            if (item.size() > 2)
                info.size2 = Py::Float(item[2].ptr());
            if (item.size() > 3)
                info.angle = Py::Float(item[3].ptr());
            if (item.size() > 4)
                info.flip = Py::Boolean(item[4].ptr());
        }
        if (value != chamferEdgeMap)
            setValue(std::move(value));
    } catch (Py::Exception &) {
        THROWM(Base::TypeError, msg)
    }
}

void PropertyChamferEdges::Save(Base::Writer &writer) const {
    if (chamferEdgeMap.empty()) {
        writer.Stream() << writer.ind() << "<EdgeChamferInfo/>\n";
        return;
    }

    writer.Stream() << writer.ind() << "<EdgeChamferInfo count=\""
                    << chamferEdgeMap.size() << "\">\n";
    writer.incInd();
    for (const auto &v : chamferEdgeMap) {
        const auto &info = v.second;
        writer.Stream() << writer.ind() << "<ChamferInfo id=\"" <<  encodeAttribute(v.first)
                        << "\" size=\"" << info.size;
        if (info.size > 0.0 && info.size != info.size2)
            writer.Stream() << "\" size2=\"" << info.size2;
        if (info.angle > 0.0)
            writer.Stream() << "\" angle=\"" << info.angle;
        if (info.flip)
            writer.Stream() << "\" flip=\"1";
        writer.Stream() << "\"/>\n";
    }
    writer.decInd();
    writer.Stream() << writer.ind() << "</EdgeChamferInfo>\n" ;
}

void PropertyChamferEdges::Restore(Base::XMLReader &reader)
{
    reader.readElement("EdgeChamferInfo");
    unsigned count = reader.getAttributeAsUnsigned("count", "0");
    std::map<std::string, ChamferInfo> value;
    for (unsigned i=0; i<count; ++i) {
        reader.readElement("ChamferInfo");
        std::string id = reader.getAttribute("id");
        if (!id.empty()) {
            auto &info = value[id];
            info.size = reader.getAttributeAsFloat("size");
            if (reader.hasAttribute("size2"))
                info.size2 = reader.getAttributeAsFloat("size2");
            else
                info.size2 = info.size;
            info.angle = reader.getAttributeAsFloat("angle", "0.0");
            info.flip = !!reader.getAttributeAsInteger("flip", "0");
        }
    }
    reader.readEndElement("EdgeChamferInfo");
    setValue(std::move(value));
}

void PropertyChamferEdges::getPaths(std::vector<App::ObjectIdentifier> &paths) const
{
    for(auto &v : chamferEdgeMap)
        paths.push_back(App::ObjectIdentifier(*this) << App::ObjectIdentifier::SimpleComponent(v.first));
}

bool PropertyChamferEdges::setPyPathValue(const App::ObjectIdentifier & path, const Py::Object &value)
{
    if(path.numSubComponents()<2
            || path.numSubComponents()>3
            || path.getPropertyComponent(0).getName()!=getName())
        FC_THROWM(Base::ValueError,"invalid constraint path " << path.toString());

    const App::ObjectIdentifier::Component & c1 = path.getPropertyComponent(1);
    if (!c1.isSimple())
        FC_THROWM(Base::ValueError,"invalid path " << path.toString());

    try {
        if (path.numSubComponents() == 2) {
            if (value.isNone()) {
                removeValue(c1.getName());
                return true;
            }
            ChamferInfo info;
            Py::Sequence seq(value);
            if (seq.size() >= 1)
                info.size = Py::Float(seq[0].ptr());
            if (seq.size() == 1)
                info.size2 = info.size;
            else {
                info.size2 = Py::Float(seq[1].ptr());
                if (seq.size() > 2)
                    info.angle = Py::Float(seq[2].ptr());
                if (seq.size() > 3)
                    info.flip = Py::Boolean(seq[3].ptr());
            }
            setValue(c1.getName(), info);
            return true;
        }
        aboutToSetValue();
        auto &info = chamferEdgeMap[c1.getName()];
        const App::ObjectIdentifier::Component & c2 = path.getPropertyComponent(2);
        if (!c2.isSimple())
            FC_THROWM(Base::ValueError,"invalid path " << path.toString());
        if (c2.getName() == "Size") {
            if (info.size == info.size2)
                info.size2 = Py::Float(value);
            info.size = Py::Float(value);
        }
        else if (c2.getName() == "Size2")
            info.size2 = Py::Float(value);
        else if (c2.getName() == "Angle")
            info.angle = Py::Float(value);
        else if (c2.getName() == "Flip")
            info.flip = Py::Boolean(value);
        else
            FC_THROWM(Base::ValueError,"invalid path " << path.toString());
        hasSetValue();
    } catch (Py::Exception &) {
        FC_THROWM(Base::TypeError,"invalid value");
    }
    return true;
}

bool PropertyChamferEdges::getPyPathValue(const App::ObjectIdentifier &path, Py::Object &res) const {

    auto components = path.getPropertyComponents(1);
    if(components.size() < 1 || components.size() > 2)
        return false;

    const App::ObjectIdentifier::Component & c1 = components[0];
    const ChamferInfo *info = nullptr;
    if (c1.isSimple()) {
        auto it = chamferEdgeMap.find(c1.getName());
        if (it != chamferEdgeMap.end())
            info = &it->second;
    }
    if (!info)
        return false;

    if (components.size() == 1) {
        res = Py::TupleN(Py::Float(info->size),
                         Py::Float(info->size2),
                         Py::Boolean(info->angle),
                         Py::Boolean(info->flip));
        return true;
    }

    const App::ObjectIdentifier::Component & c2 = components[1];
    if (!c2.isSimple())
        return false;
    if (c2.getName() == "Size")
        res = Py::Float(info->size);
    else if (c2.getName() == "Size2")
        res = Py::Float(info->size2);
    else if (c2.getName() == "Angle")
        res = Py::Float(info->angle);
    else if (c2.getName() == "Flip")
        res = Py::Boolean(info->flip);
    else
        return false;
    return true;
}

void PropertyChamferEdges::setPathValue(const App::ObjectIdentifier &path, const App::any &value)
{
    Base::PyGILStateLocker lock;
    setPyPathValue(path, pyObjectFromAny(value));
}

App::any PropertyChamferEdges::getPathValue(const App::ObjectIdentifier &path) const
{
    Base::PyGILStateLocker lock;
    Py::Object pyObj;
    if (getPyPathValue(path, pyObj))
        return App::any();
    return App::pyObjectToAny(pyObj);
}

App::Property *PropertyChamferEdges::Copy() const
{
    PropertyChamferEdges *p= new PropertyChamferEdges();
    p->chamferEdgeMap = chamferEdgeMap;
    return p;
}

void PropertyChamferEdges::Paste(const Property &from)
{
    setValue(dynamic_cast<const PropertyChamferEdges&>(from).chamferEdgeMap);
}

unsigned int PropertyChamferEdges::getMemSize() const
{
    return sizeof(ChamferInfo) * chamferEdgeMap.size();
}

bool PropertyChamferEdges::isSame(const Property &other) const
{
    if (this == &other)
        return true;
    if (!other.isDerivedFrom(getClassTypeId()))
        return false;
    return chamferEdgeMap == static_cast<const PropertyChamferEdges&>(other).chamferEdgeMap;
}

