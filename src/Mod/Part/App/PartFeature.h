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

#include <array>
#include <map>
#include <string>
#include <vector>

#include <App/FeaturePython.h>
#include <App/GeoFeature.h>
#include <App/PropertyElementAppearance.h>
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
    /** @name What the shape's elements look like
     *
     * docs/ShapeAppearanceDesign.md sec 14.6: what an object looks like is
     * a value of the object and is made by the object, with a view provider
     * and without. PartFeatureAppearance.cpp.
     */
    //@{
    /// The looks stated of the object and of its elements, and what is
    /// drawn of them
    App::PropertyElementAppearance ElementAppearance;
    /// A face no name paints takes the look of the face it was made from
    App::PropertyBool MapFaceColor;
    App::PropertyBool MapLineColor;
    App::PropertyBool MapPointColor;
    /// The transparency of the source face is taken with its colour
    App::PropertyBool MapTransparency;
    /// The looks are taken from the sources though the object names none
    App::PropertyBool ForceMapColors;
    //@}
    /** @name The looks, by the names they have always had
     *
     * Names over ElementAppearance, with no value of their own and not
     * written to a file (docs/ShapeAppearanceDesign.md sec 14.6.1): what
     * the property editor shows, and what a script with no view provider
     * writes. A view provider has the same names, over the same value.
     */
    //@{
    /// The faces as they are drawn. A write is taken apart (writeFaces())
    App::PropertyAppearanceList ShapeAppearance;
    /// The object's own colour, and how far it is seen through
    App::PropertyColor ShapeColor;
    App::PropertyPercent Transparency;
    /// The own colour of the edges, and of the vertices
    App::PropertyColor LineColor;
    App::PropertyColor PointColor;
    //@}
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

    /** @name Making what is drawn (docs/ShapeAppearanceDesign.md sec 14.6.2) */
    //@{
    /** Make the drawn looks of the faces, edges and vertices again
     *
     * From what ElementAppearance states -- the object's own look, the
     * looks given by number, the names, each at the elements it is now --
     * and, where the Map* properties say so, from the looks of the elements
     * these were made from. Kept in ElementAppearance. No recompute, and no
     * change to undo.
     *
     * @param sourceDoc: where the objects the shape was made from are, if
     *                   not in this object's document
     * @param forceMap: take the looks of the sources now though the object
     *                  names none: a copy of another object's shape, made
     *                  once. What is taken is then stated, by number, as an
     *                  import's looks are, since nothing will make it again.
     * @param whileRestoring: make it though the document is being read.
     *                  Nothing is made then, since a file has what is drawn
     *                  -- but a file written before the looks were the
     *                  object's has not, and who takes such a file's looks
     *                  into the object asks for what is drawn of them.
     */
    void updateAppearance(App::Document *sourceDoc = nullptr, bool forceMap = false,
                          bool whileRestoring = false);
    bool getDrawnAppearance(int kind, App::AppearanceList &list) const override;
    void onSourceAppearanceChanged() override;
    /** What the names paint and what the sources hand on
     *
     * The part of updateAppearance() that is of a shape and not of where
     * its looks are kept, for a view provider that keeps them itself as
     * every one did before the looks were the object's: one that shows a
     * shape of no Part::Feature, which a child view provider on a link does
     * (docs/ShapeAppearanceDesign.md sec 14.6.11). Each array is by
     * App::PropertyElementAppearance::Kind.
     */
    struct ElementLooks
    {
        /// In: each kind's own look, and whether its sources are asked
        std::array<App::MaterialAppearance, 3> own;
        std::array<bool, 3> fromSources {{false, false, false}};
        /// In: whether a face takes its source's transparency with its colour
        bool sourceTransparency {false};
        /// Out: an element's number to the place of its name
        std::array<std::map<int, int>, 3> named;
        /// Out: the looks the sources hand on, by element
        std::array<std::map<int, App::MaterialAppearance>, 3> handedOn;
    };
    /// \a owner is the object whose shape \a shape is; \a names its names
    /// of the elements given a look, in the order of what they are given
    static void mapElementLooks(const App::DocumentObject *owner, const TopoShape &shape,
                                const std::vector<App::PropertyLinkBase::ShadowSub> &names,
                                App::Document *sourceDoc, ElementLooks &looks);
    /** Whether the shape is made from other objects
     *
     * Only then are the looks of the elements it was made from asked for
     * (unless ForceMapColors). The objects this one links to, by default.
     */
    virtual bool hasBaseFeature() const;

    /** The own look taking the material card's while it follows it
     *
     * docs/MaterialStorage.md sec 15.3. Nothing where the card says nothing
     * of a look, or where a look was chosen since.
     */
    void applyMaterialAppearance();
    /** The look an object has that nobody gave one
     *
     * docs/ShapeAppearanceDesign.md sec 14.6.10, ruled: every object is
     * given a look when it is made, by the object, so that one made with no
     * view provider is the colour it would be made with one. The faces are
     * the material card's -- the default card is the preference's shape
     * colour, or a random one where that is asked for -- and the edges and
     * vertices the preferences' line and vertex colours. Each still counts
     * as a look nobody chose (PropertyElementAppearance::isFollowingMaterial()).
     */
    void giveDefaultAppearance() override;
    /// Whether there is a card's look to go back to from a chosen one
    bool canResetAppearanceToMaterial() const;
    /// The card's look again, and following it from now on
    bool resetAppearanceToMaterial();

    /** What a link in the way lays over an element of what it shows
     *
     * A face made from a face seen through a link takes the look the link
     * gives it. The link holds that (App::LinkAppearance,
     * docs/ShapeAppearanceDesign.md sec 14.6.4), and with no function given
     * here the link is asked. The Gui gives one, which is asked first: it
     * knows a link that draws through a view provider of its own, whose
     * looks are that one's, and a link that holds none.
     *
     * @param mapped: the element, as the object the link shows names it
     * @param obj: the object the element's history leads to; changed to
     *             what the link shows of it
     * @param shown: set where the link draws through a view provider of
     *               its own: the looks are then that one's
     *               (ShapeAppearance, LineColorArray, PointColorArray)
     * @param color: the colour laid over, if any is
     * @return Whether \a color was given
     */
    using LinkLookFunc = bool (*)(const Data::MappedName &mapped, App::DocumentObject *&obj,
                                  const App::PropertyContainer *&shown, App::Color &color);
    static void setLinkLookFunc(LinkLookFunc func);
    //@}

    /** @name A write to what is drawn, given to what is stated
     *
     * docs/ShapeAppearanceDesign.md sec 14.6.3. The names above and a view
     * provider's end here.
     */
    //@{
    /** A write to the list of the faces, taken apart
     *
     * The base to the object's own look, each face the write changed to
     * its name or its number with the fields that changed as its own, a
     * face that came back to the object's look let go.
     *
     * @param before: the list as it was
     * @param after: the list as the write would leave it
     */
    void writeFaces(const App::AppearanceList &before, const App::AppearanceList &after);
    /// The object's own colour and no more: what is seen through it stays
    void writeOwnColor(const App::Color &color);
    /// The own transparency, and that of every face that states a colour
    void writeOwnTransparency(long percent);
    /// The own look, its shading model, finish and texture left as they are
    void writeOwnMaterial(const App::MaterialAppearance &look);
    /** The colours of the edges, or of the vertices
     *
     * @param kind: App::PropertyElementAppearance::Edge or Vertex
     * @param values: one colour is the kind's own; more are one an element
     */
    void writeColors(int kind, const std::vector<App::Color> &values);
    //@}

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
     * position), by searching the live shape for the same geometry.  Where
     * the request names its referrer and a generation is retained for it,
     * only such a generation answers: it is the one the reference was
     * resolved against, and the element's number is a position in it and in
     * no other.
     *
     * When no generation of this feature holds the element and the request
     * names its referrer, a referrer in another document is answered from
     * the sub-shape that document kept for the reference (ForeignBaseShapes).
     */
    const std::vector<std::string>& searchElementCache(const std::string &element,
                                                       Data::SearchOptions options = Data::SearchOption::CheckGeometry,
                                                       double tol = 1e-7,
                                                       double atol = 1e-10,
                                                       const App::PropertyLinkBase *referrer = nullptr,
                                                       const App::DocumentObject *obj = nullptr,
                                                       const char *subname = nullptr) const override;

    /** The shape another branch had, kept for a reference a merge took
     * from that branch (docs/TransactionLog.md sec 31.22).
     *
     * A generation as any other, with the property that persists it made
     * at once: the caller fills it. The referrer holds this one and no
     * other; whether it is still needed is asked once the recompute is
     * over, or at the next save, as for a referrer released.
     */
    App::Property *retainElementEvidence(const App::PropertyLinkBase *referrer,
                                         App::Property *evidence = nullptr) override;

    const std::vector<const char*>& getElementTypes(bool all=false) const override;

    void beforeSave(Base::Writer &writer) const override;

    bool removeDynamicProperty(const char* name) override;

    /** @name Retained base shapes (docs/TopoNamingEnhance.md section 7)
     *
     * A generation some missing reference was last resolved against is
     * kept on this feature as a dynamic `_BaseShape<N>` property, and the
     * `_BaseShapeRefs` map says which referrer holds which generation: a
     * generation lives exactly as long as some entry names it.
     */
    //@{
    /// The name prefix of a retained generation property, `_BaseShape`
    static const char *baseShapePrefix();
    /// The name of the referrer manifest, `_BaseShapeRefs`
    static const char *baseShapeRefsName();
    /// Whether 'prop' is a retained generation property of its owner
    static bool isBaseShapeVersion(const App::Property *prop);
    /// An old generation's map never asks for a recompute
    bool checkElementMapVersion(const App::Property *prop, const char *ver) const override;
    /// A referrer let go: re-evaluate the generations at the end of the recompute
    void onElementReferenceReleased(App::PropertyLinkBase *prop) override;
    /** Let go of what released referrers were holding.
     *
     * Connected to App::Application::signalRecomputed by the Part module:
     * a referrer releases while it is being re-set, so the decision waits
     * until the recompute that re-set it is over.  Also run by beforeSave.
     */
    static void releasePendingShapeVersions(const App::Document &doc);
    //@}

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

    // Auto generated code (Tools/params_utils.py:1437)
    App::PropertyLinkList *getShapeContentsProperty(bool force=false);
    App::PropertyBool *getShapeContentSuppressedProperty(bool force=false);
    App::PropertyLinkHidden *getShapeContentReplacementProperty(bool force=false);
    App::PropertyBool *getShapeContentReplacementSuppressedProperty(bool force=false);
    App::PropertyBool *getShapeContentDetachedProperty(bool force=false);
    App::PropertyLinkHidden *get_ShapeContentOwnerProperty(bool force=false);
    //[[[end]]]

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
    /// ColoredElements, of a file older than ElementAppearance: its names
    void handleChangedPropertyName(Base::XMLReader &reader, const char *TypeName,
                                   const char *PropName) override;
    /// A new object is given a look (giveDefaultAppearance())
    void setupObject() override;
    void unsetupObject() override;
    void onDocumentRestored() override;
    /// The change of a property that what is drawn is made of
    void onAppearanceChanged(const App::Property* prop);
    /// Whether \a prop is one of the names over ElementAppearance
    bool isLookName(const App::Property* prop) const;
    /// The names take what ElementAppearance has now
    void mirrorLooks();

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

    /// The shape property the element's registered prefix selects
    const Data::ComplexGeoData* getElementGeometry(const char*& element) const override;

    /** Keep or let go of every retained generation.
     *
     * Called after the element references into this feature have been
     * re-resolved against a new shape, and before a save.  A referrer is
     * kept on a generation only while its reference into that generation's
     * property is missing (a referrer in a document that is not loaded is
     * kept, nothing can be said about it); a generation nobody is kept for
     * is dropped, with its property, except the newest of each shape
     * property, which stays in memory.  With 'materialize', a generation
     * that has referrers and no property yet is given one.  The manifest
     * is rewritten to match.
     */
    void reconcileShapeVersions(bool materialize);
    /// Take the `_BaseShape<N>` properties on this feature into the list
    void adoptShapeVersions();
    /// Give every retained generation that has referrers its property
    void materializeShapeVersions();
    /// Rewrite `_BaseShapeRefs` from the materialized generations
    void writeShapeVersionRefs();

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
    friend class ForeignBaseShapes;
    /// One retained generation of a shape property, see PartFeature.cpp
    struct ShapeVersion;
    /// The retained generations, newest first (of every registered property)
    std::vector<ShapeVersion> _shapeVersions;
    std::vector<std::pair<std::string, PropertyPartShape*>> _elementCachePrefixMap;
    /// Inside updateAppearance(): what it writes is no reason to run it
    bool _updatingAppearance {false};
    /// Inside mirrorLooks(): what it sets is no write to the looks
    bool _mirroringLooks {false};
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
