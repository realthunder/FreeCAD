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

#ifndef PART_PROPERTY_DRESSUP
#define PART_PROPERTY_DRESSUP

#include <App/PropertyLinks.h>
#include "TopoShape.h"

namespace Part {

/// A property class to store multi-segment fillet radius
class PartExport PropertyFilletSegments : public App::Property
                                        , private App::AtomicPropertyChangeInterface<PropertyFilletSegments>
{
    TYPESYSTEM_HEADER_WITH_OVERRIDE();

    friend class AtomicPropertyChange;
public:
    PropertyFilletSegments();
    ~PropertyFilletSegments();

    typedef TopoShape::FilletSegment Segment;
    typedef TopoShape::FilletSegments Segments;

    void setValue(std::map<std::string, Segments> &&v);
    void setValue(const std::map<std::string, Segments> &v={});
    void setValue(const std::string &sub, double param, double radius, double length=0.0);
    void setValue(const std::string &sub, Segments &&segments);
    void removeValue(const std::string &sub, double param, double length=0.0);
    void removeValue(const std::string &sub, int index);
    void removeValue(const std::string &sub);

    const Segments &getValue(const std::string &sub) const;
    const std::map<std::string, Segments> &getValue() const;

    void connectLinkProperty(App::PropertyLinkSub &);

    virtual PyObject *getPyObject(void) override;
    virtual void setPyObject(PyObject *) override;

    virtual void Save (Base::Writer &writer) const override;
    virtual void Restore(Base::XMLReader &reader) override;

    virtual Property *Copy(void) const override;
    virtual void Paste(const Property &from) override;

    virtual bool getPyPathValue(const App::ObjectIdentifier &path, Py::Object &res) const override;
    virtual void getPaths(std::vector<App::ObjectIdentifier> &paths) const override;

    virtual bool setPyPathValue(const App::ObjectIdentifier & path, const Py::Object &value) override;

    virtual void setPathValue(const App::ObjectIdentifier &path, const App::any &value) override;
    virtual App::any getPathValue(const App::ObjectIdentifier &path) const override;

    virtual unsigned int getMemSize (void) const override;
    
    virtual bool isSame(const Property &other) const override;

protected:
    std::map<std::string, Segments> segmentsMap;
    std::map<std::string, std::string> referenceUpdates;
    fastsignals::scoped_connection connUpdateReference;
    fastsignals::scoped_connection connChanged;
};


/// A property class to store edge chamfer information
class PartExport PropertyChamferEdges : public App::Property
                                      , private App::AtomicPropertyChangeInterface<PropertyChamferEdges>
{
    TYPESYSTEM_HEADER_WITH_OVERRIDE();

    friend class AtomicPropertyChange;
public:
    PropertyChamferEdges();
    ~PropertyChamferEdges();

    typedef TopoShape::ChamferInfo ChamferInfo;

    void setValue(std::map<std::string, ChamferInfo> &&v);
    void setValue(const std::map<std::string, ChamferInfo> &v={});
    void setValue(const std::string &sub, const ChamferInfo &info);
    void removeValue(const std::string &sub);

    bool getValue(const std::string &sub, ChamferInfo &info) const;
    const std::map<std::string, ChamferInfo> &getValue() const;

    void connectLinkProperty(App::PropertyLinkSub &);

    virtual PyObject *getPyObject(void) override;
    virtual void setPyObject(PyObject *) override;

    virtual void Save (Base::Writer &writer) const override;
    virtual void Restore(Base::XMLReader &reader) override;

    virtual Property *Copy(void) const override;
    virtual void Paste(const Property &from) override;

    virtual bool getPyPathValue(const App::ObjectIdentifier &path, Py::Object &res) const override;
    virtual void getPaths(std::vector<App::ObjectIdentifier> &paths) const override;

    virtual bool setPyPathValue(const App::ObjectIdentifier & path, const Py::Object &value) override;

    virtual void setPathValue(const App::ObjectIdentifier &path, const App::any &value) override;
    virtual App::any getPathValue(const App::ObjectIdentifier &path) const override;

    virtual unsigned int getMemSize (void) const override;
    
    virtual bool isSame(const Property &other) const override;

protected:
    std::map<std::string, ChamferInfo> chamferEdgeMap;
    std::map<std::string, std::string> referenceUpdates;
    fastsignals::scoped_connection connUpdateReference;
    fastsignals::scoped_connection connChanged;
};

}

#endif // PART_PROPERTY_DRESSUP
