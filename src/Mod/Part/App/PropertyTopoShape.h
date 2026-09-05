/***************************************************************************
 *   Copyright (c) 2008 Jürgen Riegel <juergen.riegel@web.de>              *
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

#ifndef PART_PROPERTYTOPOSHAPE_H
#define PART_PROPERTYTOPOSHAPE_H

#include <map>
#include <vector>

class BRepBuilderAPI_MakeShape;
#include <map>
#include <vector>

#include "TopoShape.h"
#include <TopAbs_ShapeEnum.hxx>

#include <App/FileBlobManager.h>
#include <App/PropertyGeo.h>
#include <App/DocumentObject.h>

namespace Part
{

class Feature;
class ShapeRefSet;

/** The part shape property class.
 * @author Werner Mayer
 */
class PartExport PropertyPartShape : public App::PropertyComplexGeoData,
                                     public App::BlobReferrerProperty
{
    TYPESYSTEM_HEADER_WITH_OVERRIDE();

public:
    PropertyPartShape();
    ~PropertyPartShape() override;

    /** @name Getter/setter */
    //@{
    /// set the part shape
    void setValue(const TopoShape&);
    /// set the part shape
    void setValue(const TopoDS_Shape&, bool resetElementMap=true);
    /// get the part shape
    const TopoDS_Shape& getValue() const;
    TopoShape getShape() const;
    const Data::ComplexGeoData* getComplexData() const override;
    //@}

    /** @name Modification */
    //@{
    /// Set the placement of the geometry
    void setTransform(const Base::Matrix4D& rclTrf) override;
    /// Get the placement of the geometry
    Base::Matrix4D getTransform() const override;
    /// Transform the real shape data
    void transformGeometry(const Base::Matrix4D &rclMat) override;
    //@}

    /** @name Getting basic geometric entities */
    //@{
    /** Returns the bounding box around the underlying mesh kernel */
    Base::BoundBox3d getBoundingBox() const override;
    //@}

    /** @name Python interface */
    //@{
    PyObject* getPyObject() override;
    void setPyObject(PyObject *value) override;
    //@}

    /** @name Save/restore */
    //@{
    void Save (Base::Writer &writer) const override;
    void Restore(Base::XMLReader &reader) override;

    void beforeSave(Base::Writer &writer) const override;

    void SaveDocFile (Base::Writer &writer) const override;
    void RestoreDocFile(Base::Reader &reader) override;

    App::Property *Copy(void) const override;
    void Paste(const App::Property &from) override;
    unsigned int getMemSize (void) const override;
    //@}

    /// Get valid paths for this property; used by auto completer
    void getPaths(std::vector<App::ObjectIdentifier> & paths) const override;

    std::string getElementMapVersion(bool restored=false) const override;
    void resetElementMapVersion() {_Ver.clear();}

    void afterRestore() override;

    /** @name Deferred shape restore (docs/DocumentLoad.md §14)
     * The shape's archive entry may be parked by the restore and read on
     * first real use; every accessor that touches _Shape goes through
     * ensureRestored() first, so the value is never observably missing.
     */
    //@{
    bool canDeferRestore() const override { return true; }
    bool isRestorePending() const override { return _RestorePending; }
    void setRestorePending(bool on) override { _RestorePending = on; }
    //@}

    /** @name Blob storage (docs/SharedShapeStorage.md sec 12.3)
     *
     * From schema 5 on the geometry is an ordinary file in the document's
     * blob store, named `Box.Shape.brp` after this property and shared by
     * content: two objects whose geometry serializes to the same bytes --
     * which, with the location canonicalized out, is every pair of equal
     * parts -- are one file, parsed once.
     */
    //@{
    /// Take the geometry file this restore was handed. Does not parse it.
    void assignRestoredBlob(const App::FileBlobHandle &blob) override;
    /** Nothing: the geometry is noted at write time, by noteBlob().
     *
     * Not an oversight. The extension the file is stored under says whether
     * the geometry was written as ASCII or binary BRep, which is the
     * writer's choice and is not known during the collect pass; and a shape
     * that changed since the last save drops its blob when it is written,
     * which would leave a collect-time note pointing at content the file no
     * longer refers to.
     */
    void collectBlobs(App::FileBlobManager &, const App::DocumentObject *) const override {}
    /// The file holding this property's geometry, or null.
    const App::FileBlobHandle &getBlob() const { return _blob; }
    //@}

    friend class Feature;
    friend class ForeignBaseShapes;
    /// Stamps _StorePos during the pre-save collect, and serves it on restore.
    friend class PropertyShapeStore;

protected:
    void validateShape(App::DocumentObject *);

private:
    void saveToFile(Base::Writer &writer) const;
    TopoDS_Shape loadFromFile(Base::Reader &reader);
    TopoDS_Shape loadFromStream(Base::Reader &reader);

    /** @name Location canonicalization (docs/SharedShapeStorage.md sec 11.4)
     *
     * From schema 5 on the geometry is written with its top level location
     * taken off and the location spelled as a `loc=` attribute instead. Moving
     * an object then leaves its shape bytes untouched, and two equal parts at
     * different placements serialize to the same bytes.
     */
    //@{
    /// Whether this writer canonicalizes locations at all.
    static bool stripsLocation(Base::Writer &writer);
    /// The geometry as this writer wants it: at the identity, or as it is.
    TopoDS_Shape shapeForSave(Base::Writer &writer) const;
    /** Put the `loc=` this restore read back onto arriving geometry.
     *
     * *** Must run BEFORE the value is announced. Outside a recompute
     * Feature::onChanged copies Placement out of the shape's own transform,
     * so a shape announced at the identity zeroes the placement.
     */
    TopoDS_Shape locatedForRestore(const TopoDS_Shape &shape) const;
    //@}

    /// Serve the parked archive entry, if any, before _Shape is used.
    void ensureRestored() const;
    /// Drop the parked entry unserved -- the value got overwritten.
    void cancelRestorePending();
    /// Take this shape out of the document's store, at _StorePos.
    void serveFromStore();

    /** @name Blob storage, the parts that are not the public interface */
    //@{
    App::FileBlobManager &blobManager() const;
    /// Whether this writer puts geometry in the blob store.
    bool usesBlob(Base::Writer &writer) const;
    /// Serialize the geometry into the store, unless _blob already holds it.
    void makeBlob(Base::Writer &writer) const;
    /// Write the geometry to a new file in the store and take it as _blob.
    void storeBlob(Base::Writer &writer, ShapeRefSet *refs) const;
    /// Tell the manager this save refers to _blob, and what to name its file.
    void noteBlob(Base::Writer &writer) const;
    /// Parse the geometry out of _blob and announce it.
    void serveFromBlob();
    /** Drop _blob if it no longer describes the value being set.
     *
     * A shape that differs only in where it sits keeps it: the location is
     * canonicalized out of the file (sec 11.4), so moving an object leaves
     * the geometry it already serialized valid, and the save writes nothing.
     */
    void dropBlob(const TopoDS_Shape &next);
    //@}

private:
    TopoShape _Shape;
    TopoShape _ShapeNoName;
    std::string _Ver;
    mutable int _HasherIndex = 0;
    mutable bool _SaveHasher = false;
    mutable bool _RestorePending = false;
    /** Byte position of this shape in the document's shared store.
     *
     * Two lives, never at once: on save it is what the collect pass stamped
     * and Save() prints; on restore it is where ensureRestored() will find
     * the shape. Absent means this property carries its own archive member,
     * which is every document below schema 5.
     */
    mutable uint64_t _StorePos = ~static_cast<uint64_t>(0);
    /** The location the `loc=` attribute of this restore carried.
     *
     * Identity when the document was written without canonicalized locations,
     * which is every document below schema 5 -- there the geometry still has
     * its location baked in and nothing has to be put back.
     */
    TopLoc_Location _RestoreLoc;
    /** The file this property's geometry is, or is about to be, stored in.
     *
     * Held from the moment the content is known until the value stops
     * matching it, which is what lets a save write nothing for a shape that
     * did not change, and what lets two objects with equal geometry share one
     * file and one parse.
     */
    mutable App::FileBlobHandle _blob;
    /** What _blob's file borrows from other files, as ShapeRefSet::plan().
     *
     * A file's bytes are a function of the shape *and* of what the save
     * decided to borrow (docs/SharedShapeStorage.md sec 12.4), so an unchanged
     * shape is not on its own a reason to keep the file written for it: an
     * object that used to borrow from a file which has since gone would
     * otherwise keep a reference to it. Set both by writing the file and by
     * parsing it, so a reopened document still knows what its own files say.
     */
    mutable std::string _blobPlan;
    /** The motion from _blob's geometry to this shape's, when the file was
     * written for another instance of the same part.
     *
     * Identity for a file written for this shape, which is the ordinary
     * case. When it is not identity, this property owns none of the file:
     * the geometry in it sits where the other instance sits, and this one
     * is that geometry moved. It is composed into the location written to
     * the XML, so nothing on the reading side has to know about any of it.
     */
    mutable TopLoc_Location _blobMotion;
    /** Whether this property's file is offered for other files to borrow
     * from (ShapeRefSet::publish). Off for a retained generation
     * (Feature::materializeShapeVersions): it may borrow, but nothing
     * may depend on a file that is dropped the day its last referrer is
     * repaired.
     */
    bool _publishes = true;
    /// The motion a restore has to put back into the geometry, from the
    /// `motion` attribute. Identity for a file written for this shape.
    TopLoc_Location _RestoreMotion;
    /// Content hash a restore read, empty when this shape is not a blob.
    std::string _RestoreHash;
    /// Manager the pending referrer was queued with, for withdrawing it.
    App::FileBlobManager *_PendingManager {nullptr};
};

struct PartExport ShapeHistory {
    /**
    * @brief MapList: key is index of subshape (of type 'type') in source
    * shape. Value is list of indexes of subshapes in result shape.
    */
    using MapList = std::map<int, std::vector<int> >;
    using List = std::vector<int>;

    TopAbs_ShapeEnum type;
    MapList shapeMap;

    ShapeHistory() {}
    /**
     * Build a history of changes
     * MakeShape: The operation that created the changes, e.g. BRepAlgoAPI_Common
     * type: The type of object we are interested in, e.g. TopAbs_FACE
     * newS: The new shape that was created by the operation
     * oldS: The original shape prior to the operation
     */
    ShapeHistory(BRepBuilderAPI_MakeShape& mkShape, TopAbs_ShapeEnum type,
                 const TopoDS_Shape& newS, const TopoDS_Shape& oldS);
    void reset(BRepBuilderAPI_MakeShape& mkShape, TopAbs_ShapeEnum type,
               const TopoDS_Shape& newS, const TopoDS_Shape& oldS);
    void join(const ShapeHistory &newH);

};

class PartExport PropertyShapeHistory : public App::PropertyLists
{
    TYPESYSTEM_HEADER_WITH_OVERRIDE();

public:
    PropertyShapeHistory();
    ~PropertyShapeHistory() override;

    void setSize(int newSize) override {
        _lValueList.resize(newSize);
    }
    int getSize() const override {
        return _lValueList.size();
    }

    /** Sets the property
     */
    void setValue(const ShapeHistory&);

    void setValues (const std::vector<ShapeHistory>& values);

    const std::vector<ShapeHistory> &getValues() const {
        return _lValueList;
    }

    bool isSame(const App::Property &) const override {return false;}

    App::Property *copyBeforeChange() const override {return nullptr;}

    PyObject *getPyObject() override;
    void setPyObject(PyObject *) override;

    void Save (Base::Writer &writer) const override;
    void Restore(Base::XMLReader &reader) override;

    void SaveDocFile (Base::Writer &writer) const override;
    void RestoreDocFile(Base::Reader &reader) override;

    Property *Copy() const override;
    void Paste(const Property &from) override;

    unsigned int getMemSize () const override {
        return _lValueList.size() * sizeof(ShapeHistory);
    }

private:
    std::vector<ShapeHistory> _lValueList;
};

/** A property class to store hash codes and two radii for the fillet algorithm.
 * @author Werner Mayer
 */
struct PartExport FilletElement {
    int edgeid;
    double radius1, radius2;

    FilletElement(int id=0,double r1=1.0,double r2=1.0)
        :edgeid(id),radius1(r1),radius2(r2)
    {}

    bool operator<(const FilletElement &other) const {
        return edgeid < other.edgeid;
    }

    bool operator==(const FilletElement &other) const {
        return edgeid == other.edgeid
            && radius1 == other.radius1
            && radius2 == other.radius2;
    }
};

class PartExport PropertyFilletEdges : public App::PropertyListsT<FilletElement>
{
    TYPESYSTEM_HEADER_WITH_OVERRIDE();

    using inherited = PropertyListsT<FilletElement>;

public:
    PropertyFilletEdges();
    ~PropertyFilletEdges() override;

    /** Sets the property
     */
    void setValue(int id, double r1, double r2);
    using inherited::setValue;

    PyObject *getPyObject(void) override;

    Property *Copy(void) const override;
    void Paste(const Property &from) override;

protected:
    FilletElement getPyValue(PyObject *item) const override;

    void restoreXML(Base::XMLReader &) override;
    bool saveXML(Base::Writer &) const override;
    bool canSaveStream(Base::Writer &) const override { return true; }
    void restoreStream(Base::InputStream &s, unsigned count) override;
    void saveStream(Base::OutputStream &) const override;
};

class PartExport PropertyShapeCache: public App::Property {

    TYPESYSTEM_HEADER_WITH_OVERRIDE();

public:
    App::Property *Copy(void) const override;

    void Paste(const App::Property &) override;

    PyObject *getPyObject() override;

    void setPyObject(PyObject *value) override;

    void Save (Base::Writer &writer) const override;

    void Restore(Base::XMLReader &reader) override;

    bool isSame(const App::Property &) const override {return false;}

    App::Property *copyBeforeChange() const override {return nullptr;}

    static PropertyShapeCache *get(const App::DocumentObject *obj, bool create);
    static bool getShape(const App::DocumentObject *obj, TopoShape &shape, const char *subname=0);
    static void setShape(const App::DocumentObject *obj, const TopoShape &shape, const char *subname=0);

private:
    void slotChanged(const App::DocumentObject &, const App::Property &prop);

private:
    std::unordered_map<std::string, TopoShape> cache;
    fastsignals::scoped_connection connChanged;
};

} //namespace Part


#endif // PART_PROPERTYTOPOSHAPE_H
