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


/** A property class to store fillet setback corners (docs/CornerBlending.md),
 * keyed by vertex sub-name. Path values: Corners.Vertex7 for the whole
 * corner, Corners.Vertex7.Setback and Corners.Vertex7.Edge3.
 */
/** The setback corners of a fillet, by vertex (docs/CornerBlending.md).
 *
 * It is itself a link to the shape the fillet is made on, whose sub-names
 * are the corners' vertexes and the faces they give depths, so that those
 * names follow the topology as a link's do. The edges a corner sets back
 * are named as the fillet's edge link names them (connectLinkProperty).
 */
class PartExport PropertyFilletCorners : public App::PropertyLinkSub
{
    TYPESYSTEM_HEADER_WITH_OVERRIDE();

public:
    PropertyFilletCorners();
    ~PropertyFilletCorners();

    /// The setbacks at one vertex. @sa TopoShape::FilletCorner
    struct Corner {
        /// The setback of every fillet ending at the vertex, less than 0 for none
        double setback = -1.0;
        /** The setbacks of single fillets by edge sub-name, over 'setback',
         * and the depths of faces by face sub-name (a face's patch boundary
         * bows into it, away from the vertex, that far at its middle)
         */
        std::map<std::string, double> edges;

        bool operator==(const Corner &other) const {
            return setback == other.setback && edges == other.edges;
        }
        bool operator!=(const Corner &other) const {
            return !(*this == other);
        }
    };

    void setValue(std::map<std::string, Corner> &&v);
    void setValue(const std::map<std::string, Corner> &v={});
    void setValue(const std::string &vertex, const Corner &corner);
    /// Sets the setback of every fillet at the vertex
    void setValue(const std::string &vertex, double setback);
    /// Sets the setback of the fillet of one edge at the vertex, or a face's depth
    void setValue(const std::string &vertex, const std::string &edge, double setback);
    void removeValue(const std::string &vertex);
    void removeValue(const std::string &vertex, const std::string &edge);

    /// The corner at the vertex, null if none
    const Corner *getValue(const std::string &vertex) const;
    const std::map<std::string, Corner> &getValue() const;

    /// The object the corners' vertexes and faces are of, the fillet's base
    App::DocumentObject *getLinkObject() const {
        return PropertyLinkSub::getValue();
    }
    /// Sets that object, the vertexes and faces kept by name
    void setLinkObject(App::DocumentObject *obj);

    /** Follows the edge names of the corners as the sub-names of the
     * fillet's edge link change
     */
    void connectLinkProperty(App::PropertyLinkSub &);

    /// Whether a sub-name in a corner is a face's (a depth) rather than an edge's
    static bool isFaceName(const std::string &name);

    virtual PyObject *getPyObject(void) override;
    virtual void setPyObject(PyObject *) override;

    virtual void Save (Base::Writer &writer) const override;
    virtual void Restore(Base::XMLReader &reader) override;

    virtual Property *Copy(void) const override;
    /// From another PropertyFilletCorners all of it; from a plain link (a
    /// relabel or import copy), the link alone
    virtual void Paste(const Property &from) override;

    virtual const char* getEditorName() const override { return ""; }

    virtual void updateElementReference(
            App::DocumentObject *feature, bool reverse=false, bool notify=false) override;

    virtual bool getPyPathValue(const App::ObjectIdentifier &path, Py::Object &res) const override;
    virtual void getPaths(std::vector<App::ObjectIdentifier> &paths) const override;

    virtual bool setPyPathValue(const App::ObjectIdentifier & path, const Py::Object &value) override;

    virtual void setPathValue(const App::ObjectIdentifier &path, const App::any &value) override;
    virtual App::any getPathValue(const App::ObjectIdentifier &path) const override;

    virtual unsigned int getMemSize (void) const override;

    virtual bool isSame(const Property &other) const override;

protected:
    /// The link's sub-names from the corners: their vertexes, then the faces
    /// they give depths; names kept keep their mapped names
    void syncSubs();
    /// Renames the corners' vertexes and faces, and the expressions on them
    void renameCorners(const std::map<std::string, std::string> &renamed);

protected:
    std::map<std::string, Corner> cornerMap;
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
