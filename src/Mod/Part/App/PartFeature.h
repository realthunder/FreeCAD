/***************************************************************************
 *   Copyright (c) 2002 Jürgen Riegel <juergen.riegel@web.de>              *
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


#ifndef PART_FEATURE_H
#define PART_FEATURE_H

#include <string>
#include <vector>

#include <App/FeaturePython.h>
#include <App/GeoFeature.h>
#include <Mod/Material/App/PropertyMaterial.h>
#include <Mod/Part/PartGlobal.h>

#include <TopoDS_Face.hxx>
#include "PropertyTopoShape.h"

class gp_Dir;

namespace Data
{
struct HistoryItem;
}

namespace App
{
class PropertyLinkList;
}

namespace Part
{

class PartFeaturePy;

/** Base class of all shape feature classes in FreeCAD
 */
class PartExport Feature : public App::GeoFeature
{
    using inherited = App::GeoFeature;
    PROPERTY_HEADER_WITH_OVERRIDE(Part::Feature);

public:
    /// Constructor
    Feature();
    ~Feature() override;

    PropertyPartShape Shape;
    /// The physical material card assigned to this shape
    Materials::PropertyMaterial ShapeMaterial;
    App::PropertyLinkSubHidden ColoredElements;
    App::PropertyBool ValidateShape;
    App::PropertyBool InvalidShape;
    App::PropertyEnumeration FixShape;

    /** @name methods override feature */
    //@{
    short mustExecute() const override;
    //@}

    /// returns the type name of the ViewProvider
    const char* getViewProviderName() const override;
    const App::PropertyComplexGeoData* getPropertyOfGeometry() const override;

    /// Appearance taken from the assigned material card
    App::MaterialAppearance getMaterialAppearance() const override;
    /// Assign the appearance half of the material card
    void setMaterialAppearance(const App::MaterialAppearance& material) override;
    App::MaterialRenderProperties getMaterialRenderProperties() const override;

    PyObject* getPyObject() override;

    virtual App::PropertyLinkList *getShapeLinksProperty() {return nullptr;}

    std::pair<std::string,std::string> getElementName(
            const char *name, ElementNameType type=Normal) const override;

    static std::list<Data::HistoryItem> getElementHistory(App::DocumentObject *obj,
            const char *name, bool recursive=true, bool sameType=false);

    static QVector<Data::MappedElement>
    getRelatedElements(App::DocumentObject *obj, const char *name, bool sameType=true, bool withCache=true);

    /** Obtain the element name from a feature based of the element name of its source feature
     *
     * @param obj: current feature
     * @param subname: sub-object/element reference
     * @param src: source feature
     * @param srcSub: sub-object/element reference of the source
     * @param single: if true, then return upon first match is found, or else
     *                return all matches. Multiple matches are possible for
     *                compound of multiple instances of the same source shape.
     *
     * @return Return a vector of pair of new style and old style element names.
     */
    static QVector<Data::MappedElement>
    getElementFromSource(App::DocumentObject *obj,
                         const char *subname,
                         App::DocumentObject *src,
                         const char *srcSub,
                         bool single = false);

    TopLoc_Location getLocation() const;

    DocumentObject *getSubObject(const char *subname, PyObject **pyObj, 
            Base::Matrix4D *mat, bool transform, int depth) const override;

    /** Convenience function to extract shape from fully qualified subname 
     *
     * @param obj: the parent object
     *
     * @param subname: dot separated full qualified subname
     *
     * @param needSubElement: whether to ignore the non-object subelement
     * reference inside \c subname
     *
     * @param pmat: used as current transformation on input, and return the
     * accumulated transformation on output
     *
     * @param owner: return the owner of the shape returned
     *
     * @param resolveLink: if true, resolve link(s) of the returned 'owner'
     * by calling its getLinkedObject(true) function
     *
     * @param transform: if true, apply obj's transformation. Set to false
     * if pmat already include obj's transformation matrix.
     */
    static TopoDS_Shape getShape(const App::DocumentObject *obj,
            const char *subname=nullptr, bool needSubElement=false, Base::Matrix4D *pmat=nullptr, 
            App::DocumentObject **owner=nullptr, bool resolveLink=true, bool transform=true);

    static TopoShape getTopoShape(const App::DocumentObject *obj,
            const char *subname=nullptr, bool needSubElement=false, Base::Matrix4D *pmat=nullptr, 
            App::DocumentObject **owner=nullptr, bool resolveLink=true, bool transform=true, 
            bool noElementMap=false);

    static App::DocumentObject *getShapeOwner(const App::DocumentObject *obj, const char *subname=nullptr);

    static bool hasShapeOwner(const App::DocumentObject *obj, const char *subname=nullptr) {
        auto owner = getShapeOwner(obj,subname);
        return owner && owner->isDerivedFrom(getClassTypeId());
    }

    static void disableElementMapping(App::PropertyContainer *container, bool disable=true);
    static bool isElementMappingDisabled(App::PropertyContainer *container);

    /** Find an element of a retained generation of this feature's shape in
     * the live shape.
     *
     * The feature keeps a list of generations of its shape properties in
     * memory (see docs/TopoNamingEnhance.md section 7): the whole shape as
     * it was before each change, newest first.  The request is answered from
     * the newest generation that holds 'element' (a mapped name goes through
     * that generation's own element map, an indexed name is taken by
     * position), by searching the live shape for the same geometry.
     */
    const std::vector<std::string>& searchElementCache(const std::string &element,
                                                       Data::SearchOptions options = Data::SearchOption::CheckGeometry,
                                                       double tol = 1e-7,
                                                       double atol = 1e-10) const override;

    const std::vector<const char*>& getElementTypes(bool all=false) const override;

    void beforeSave(Base::Writer &writer) const override;

    bool removeDynamicProperty(const char* name) override;

    void expandShapeContents();
    void mergeShapeContents();
    void collapseShapeContents(bool removeProperty=false);
    /// The restore-time shape-content checks: run by onDocumentRestored(),
    /// or -- when the shape's archive entry was parked by the deferred
    /// restore -- when the shape actually arrives.
    void restoreShapeContents();

    /*[[[cog
    import PartParams
    PartParams.declare_properties()
    ]]]*/

    // Auto generated code (Tools/params_utils.py:1012)
    App::PropertyLinkList *getShapeContentsProperty(bool force=false);
    App::PropertyBool *getShapeContentSuppressedProperty(bool force=false);
    App::PropertyLinkHidden *getShapeContentReplacementProperty(bool force=false);
    App::PropertyBool *getShapeContentReplacementSuppressedProperty(bool force=false);
    App::PropertyBool *getShapeContentDetachedProperty(bool force=false);
    App::PropertyLinkHidden *get_ShapeContentOwnerProperty(bool force=false);
    //[[[end]]]

    fastsignals::signal<void (App::Document *)> signalMapShapeColors;

    static Feature *create(const TopoShape &s,
                           const char *name = nullptr,
                           App::Document *doc = nullptr);

    void fixShape(TopoShape &s) const;

protected:
    /// recompute only this object
    App::DocumentObjectExecReturn *recompute() override;
    /// recalculate the feature
    App::DocumentObjectExecReturn *execute() override;
    void onBeforeChange(const App::Property* prop) override;
    void onChanged(const App::Property* prop) override;
    void unsetupObject() override;
    void onDocumentRestored() override;

    // Return true if need to apply the shape placement to the Placement property
    virtual bool shouldApplyPlacement();

    /** Register a second shape property whose elements are referenced with
     * an element-name prefix, so that its generations are retained and
     * searched like the main Shape's (the Sketcher's InternalShape).  A
     * null 'prop' unregisters the prefix.
     */
    void registerElementCache(const std::string &prefix, PropertyPartShape *prop);

    /** The shape property an element name belongs to, by its registered
     * prefix; the main Shape when no prefix matches.  'prefix' receives the
     * matched prefix, or null.
     */
    PropertyPartShape *shapePropertyOfElement(const char *element,
                                              const std::string **prefix = nullptr) const;

    /** Drop the retained generations no missing referrer needs any more.
     *
     * Called after the element references into this feature have been
     * re-resolved against a new shape: a referrer is kept on a generation
     * only while its reference into that generation's property is missing,
     * and a generation nobody is kept for is dropped, except the newest of
     * each property, which always stays in memory.
     */
    void reconcileShapeVersions();

    /** Helper function to obtain mapped and indexed element name from a shape
     * @params shape: source shape
     * @param name: the input name, can be either mapped or indexed name
     * @return Returns both the indexed and mapped name
     *
     * If the 'name' referencing a non-primary shape type, i.e. not
     * Vertex/Edge/Face, this function will auto generate a name from primary
     * sub-shapes.
     */
    std::pair<std::string,std::string> getExportElementName(TopoShape shape, const char *name) const;

private:
    /// One retained generation of a shape property, see PartFeature.cpp
    struct ShapeVersion;
    /// The retained generations, newest first (of every registered property)
    std::vector<ShapeVersion> _shapeVersions;
    std::vector<std::pair<std::string, PropertyPartShape*>> _elementCachePrefixMap;
};

class FilletBase : public Part::Feature
{
    PROPERTY_HEADER_WITH_OVERRIDE(Part::FilletBase);

public:
    FilletBase();

    App::PropertyLink   Base;
    PropertyFilletEdges Edges;
    App::PropertyLinkSub   EdgeLinks;

    short mustExecute() const override;
    void onUpdateElementReference(const App::Property *prop) override;

protected:
    void onDocumentRestored() override;
    void onChanged(const App::Property *) override;
    void syncEdgeLink();
};

typedef App::FeaturePythonT<Feature> FeaturePython;


/** Base class of all shape feature classes in FreeCAD
 */
class PartExport FeatureExt : public Feature
{
    PROPERTY_HEADER_WITH_OVERRIDE(Part::FeatureExt);

public:
    const char* getViewProviderName() const override {
        return "PartGui::ViewProviderPartExt";
    }
};

// Utility methods
/**
 * Find all faces cut by a line through the centre of gravity of a given face
 * Useful for the "up to face" options to pocket or pad
 */
struct cutFaces {
    TopoShape face;
    double distsq;
};

PartExport
std::vector<cutFaces> findAllFacesCutBy(const TopoShape& shape,
                                        const TopoShape& face, const gp_Dir& dir);

/**
  * Check for intersection between the two shapes. Only solids are guaranteed to work properly
  * There are two modes:
  * 1. Bounding box check only - quick but inaccurate
  * 2. Bounding box check plus (if necessary) boolean operation - costly but accurate
  * Return true if the shapes intersect, false if they don't
  * The flag touch_is_intersection decides whether shapes touching at distance zero are regarded
  * as intersecting or not
  * 1. If set to true, a true check result means that a boolean fuse operation between the two shapes
  *    will return a single solid
  * 2. If set to false, a true check result means that a boolean common operation will return a
  *    valid solid
  * If there is any error in the boolean operations, the check always returns false
  */
PartExport
bool checkIntersection(const TopoDS_Shape& first, const TopoDS_Shape& second,
                       const bool quick, const bool touch_is_intersection);

} //namespace Part


#endif // PART_FEATURE_H
