/***************************************************************************
 *   Copyright (c) 2011 Juergen Riegel <juergen.riegel@web.de>             *
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

#ifndef PARTGUI_VIEWPROVIDERPARTEXT_H
#define PARTGUI_VIEWPROVIDERPARTEXT_H

#include <map>
#include <memory>

#include <App/PropertyUnits.h>
#include <Gui/ViewProviderGeometryObject.h>

#include <Mod/Part/App/PartFeature.h>
#include <Mod/Part/PartGlobal.h>


class TopoDS_Shape;
class TopoDS_Edge;
class TopoDS_Wire;
class TopoDS_Face;
class SoSeparator;
class SoGroup;
class SoSwitch;
class SoVertexShape;
class SoPickedPoint;
class SoShapeHints;
class SoEventCallback;
class SbVec3f;
class SoSphere;
class SoScale;
class SoCoordinate3;
class SoIndexedFaceSet;
class SoNormal;
class SoNormalBinding;
class SoTextureCoordinate2;
class SoMaterialBinding;
class SoIndexedLineSet;

namespace PartGui {

class SoBrepFaceSet;
class SoBrepEdgeSet;
class SoBrepPointSet;
class SoFCCoordinate3;
struct ShapeInstanceRep;

/** DiffuseColor, whose storage is the object's ShapeAppearance
 *
 * The per-face colours are one datum and the appearance owns it. This keeps
 * the familiar name, the file format, and every C++ and Python call site
 * that says DiffuseColor, while reading and writing the appearance's diffuse
 * field so that the two can never disagree.
 *
 * It costs nothing to read: the appearance stores one array per material
 * field, so its diffuse field already IS a std::vector<Base::Color> and
 * getValues() hands back a reference to it. Writes go through the
 * appearance's setters, which is also where the change is announced -- a
 * write here notifies ShapeAppearance, not DiffuseColor.
 *
 * The readers below hide the base ones by name rather than making them
 * virtual; see docs/ShapeAppearanceDesign.md section 1.1 for why, and note
 * that nothing may reach this through an App::PropertyColorList pointer
 * (section 7.2). Everything that legitimately does go through a base
 * pointer -- Save, Restore, Copy, Paste, get/setPyObject, getSize -- is
 * virtual and overridden here.
 */
class PartGuiExport PropertyDiffuseColor : public App::PropertyColorList
{
    TYPESYSTEM_HEADER_WITH_OVERRIDE();

public:
    /** Wire the storage
     *
     * \a appearance is where the colours live; \a shapeColor is the object
     * colour that an empty assignment falls back to -- clearing the list has
     * always meant "every face back to the object's colour", and a field the
     * appearance stores as empty means the default material's colour
     * instead. Both null until the owner wires them, and the property is a
     * plain colour list until then.
     */
    void setAppearance(App::PropertyMaterialList *appearance,
                       const App::PropertyColor *shapeColor);

    /** @name Reads, overriding or hiding the base ones
     *
     * getValues() overrides -- App::PropertyColorList makes that one virtual
     * so that a property whose colours live somewhere else can say so once
     * and have the inherited restore paths follow. The rest hide, because
     * PropertyListsT reads its member directly and cannot be told otherwise.
     */
    //@{
    const std::vector<Base::Color> &getValues() const override;
    const std::vector<Base::Color> &getValue() const { return getValues(); }
    const Base::Color &operator[](int idx) const { return getValues()[idx]; }
    int getSize() const override;
    //@}

    /** @name Writes
     *
     * setValues(ListT&&) is the funnel: every other write the base offers --
     * setValue(colour), setValue(list), setValues(list), and the Python
     * paths -- reaches it virtually, so overriding it covers them all.
     */
    //@{
    using App::PropertyColorList::setValues;
    void setValues(std::vector<Base::Color> &&colors) override;
    void set1Value(int idx, const Base::Color &col) override;
    using App::PropertyColorList::setSize;
    void setSize(int newSize) override;
    void setSize(int newSize, const Base::Color &def) override;
    //@}

    unsigned int getMemSize() const override;
    unsigned int getSaveSize(Base::Writer &writer) const override;
    bool isSame(const App::Property &other) const override;
    App::Property *Copy() const override;
    void Paste(const App::Property &from) override;
    PyObject *getPyObject() override;
    void setPyObject(PyObject *value) override;

    void Save(Base::Writer &writer) const override;
    void Restore(Base::XMLReader &reader) override;

protected:
    /// Reinterpret an all-zero-alpha Python assignment written for the old
    /// meaning of alpha; defined next to setPyObject, its only caller.
    void guardLegacyAlpha(std::vector<Base::Color> &colors) const;

    /** The element name a colour list has always written
     *
     * A list's XML element is named after its type, so without this a
     * document would say <DiffuseColor> where every older one says
     * <ColorList> -- and the restore of those older ones, which reads the
     * element by name, would fail.
     */
    const char *xmlName() const override { return "ColorList"; }
    void restoreXML(Base::XMLReader &reader) override;
    bool saveXML(Base::Writer &writer) const override;
    void restoreStream(Base::InputStream &str, unsigned count) override;
    void saveStream(Base::OutputStream &str) const override;

private:
    App::PropertyMaterialList *_appearance {nullptr};
    const App::PropertyColor *_shapeColor {nullptr};
};

class PartGuiExport ViewProviderPartExt : public Gui::ViewProviderGeometryObject
{
    PROPERTY_HEADER_WITH_OVERRIDE(PartGui::ViewProviderPartExt);
    typedef Gui::ViewProviderGeometryObject inherited;

public:
    /// constructor
    ViewProviderPartExt();
    /// destructor
    ~ViewProviderPartExt() override;

    // Display properties
    App::PropertyFloatConstraint Deviation;
    App::PropertyBool ControlPoints;
    App::PropertyAngle AngularDeflection;
    App::PropertyEnumeration Lighting;
    App::PropertyEnumeration DrawStyle;
    // Points
    App::PropertyFloatConstraint PointSize;
    App::PropertyColor PointColor;
    App::PropertyMaterial PointMaterial;
    App::PropertyColorList PointColorArray;
    // Lines
    App::PropertyFloatConstraint LineWidth;
    App::PropertyColor LineColor;
    App::PropertyMaterial LineMaterial;
    App::PropertyColorList LineColorArray;
    // Faces (Gui::ViewProviderGeometryObject::ShapeColor and Gui::ViewProviderGeometryObject::ShapeAppearance apply)
    /// A name over ShapeAppearance's diffuse field, not a second store
    PropertyDiffuseColor DiffuseColor;

    App::PropertyColorList MappedColors;    
    App::PropertyBool MapFaceColor;    
    App::PropertyBool MapLineColor;    
    App::PropertyBool MapPointColor;    
    App::PropertyBool MapTransparency;    
    App::PropertyBool ForceMapColors;

    void attach(App::DocumentObject *) override;
    void setDisplayMode(const char* ModeName) override;
    /// returns a list of all possible modes
    std::vector<std::string> getDisplayModes() const override;
    /// Update the view representation
    void reload();
    /// If no other task is pending it opens a dialog to allow to change face colors
    bool changeFaceColors();

    void updateData(const App::Property*) override;

    virtual PyObject *getPyObject() override;

    /** @name Selection handling
     * This group of methods do the selection handling.
     * Here you can define how the selection for your ViewProfider
     * works.
     */
    //@{
    /// indicates if the ViewProvider use the new Selection model
    bool useNewSelectionModel() const override {return true;}
    bool getElementPicked(const SoPickedPoint *, std::string &subname) const override;
    std::string getElement(const SoDetail *detail) const override;
    SoDetail* getDetail(const char*) const override;
    bool getDetailPath(const char *subname, SoFullPath *pPath, bool append, SoDetail *&det) const override;
    std::vector<Base::Vector3d> getModelPoints(const SoPickedPoint *) const override;
    /// return the highlight lines for a given element or the whole shape
    std::vector<Base::Vector3d> getSelectionShape(const char* Element) const override;
    //@}

    /** @name Highlight handling
    * This group of methods do the highlighting of elements.
    */
    //@{
    void setHighlightedFaces(const std::vector<App::Color>& colors);
    void setHighlightedFaces(const std::vector<App::Material>& colors);
    void unsetHighlightedFaces();
    /// Reapply the document appearance to the face material node: the
    /// per-face colour path while diffuse is the only field that varies,
    /// the whole-material path once any other field does.
    void applyShapeAppearance();
    void setHighlightedEdges(const std::vector<App::Color>& colors);
    void unsetHighlightedEdges();
    void setHighlightedPoints(const std::vector<App::Color>& colors);
    void unsetHighlightedPoints();

    void enableFullSelectionHighlight(bool face=true, bool line=true, bool point=true);
    //@}

    /** @name Color management methods
     */
    //@{
    void setElementColors(const std::map<std::string,App::Color> &colors) override;
    std::map<std::string,App::Color> getElementColors(const char *element=nullptr) const override;
    //@}

    bool isUpdateForced() const override {
        return forceUpdateCount>0;
    }
    void forceUpdate(bool enable = true) override;

    bool allowOverride(const App::DocumentObject &) const override;

    virtual void updateColors(App::Document *sourceDoc=0, bool forceColorMap=false) override;

    virtual void checkColorUpdate() override;

    static std::vector<App::Color> getShapeColors(const Part::TopoShape &shape, App::Color &defColor,
            App::Document *sourceDoc=0, bool linkOnly=false);

    /** @name Edit methods */
    //@{
    void setupContextMenu(QMenu*, QObject*, const char*) override;
    virtual void setEditViewer(Gui::View3DInventorViewer*, int ModNum) override;

    virtual void setShapePropertyName(const char *propName);
    const char *getShapePropertyName() const;

    void setHighlightFaceEdges(bool enable);

    Part::TopoShape getShape() const;
    virtual void updateVisual();
    /// Bounding-box stand-in of an oversized part during a progressive
    /// import; the coarse mesh follows from the refine pool. See the
    /// definition. Returns whether the stand-in was built.
    bool buildCoarseStandIn();

    virtual void reattach(App::DocumentObject *) override;
    virtual void beforeDelete() override;
    virtual void finishRestoring() override;

protected:
    bool setEdit(int ModNum) override;
    void unsetEdit(int ModNum) override;
    //@}

    Base::BoundBox3d _getBoundingBox(const char *subname=0,
            const Base::Matrix4D *mat=0, bool transform=true,
            const Gui::View3DInventorViewer *view=0, int depth=0) const override;

protected:
    /// get called by the container whenever a property has been changed
    void onChanged(const App::Property* prop) override;
    /// Restore a document written before DiffuseColor became a name over
    /// ShapeAppearance: it kept its name and changed type, so it arrives
    /// here, and the base would drop it without a word.
    void handleChangedPropertyType(Base::XMLReader &reader,
                                   const char *TypeName,
                                   App::Property *prop) override;

    virtual bool hasBaseFeature() const;

    // nodes for the data representation
    SoMaterialBinding * pcFaceBind;
    SoMaterialBinding * pcLineBind;
    SoMaterialBinding * pcPointBind;
    SoMaterial        * pcLineMaterial;
    SoMaterial        * pcPointMaterial;
    SoDrawStyle       * pcLineStyle;
    SoDrawStyle       * pcPointStyle;
    SoShapeHints      * pShapeHints;

    SoCoordinate3     * coords;
    SoCoordinate3     * pcoords;
    SoBrepFaceSet     * faceset;
    SoNormal          * norm;
    SoNormalBinding   * normb;
    SoTextureCoordinate2 * texcoords;
    SoBrepEdgeSet     * lineset;
    SoBrepPointSet    * nodeset;
    
    Gui::CoinPtr<SoGroup>  pFaceRoot;
    Gui::CoinPtr<SoGroup>  pFaceEdgeRoot;
    Gui::CoinPtr<SoGroup>  pEdgeRoot;
    Gui::CoinPtr<SoGroup>  pVertexRoot;

    /// Roots of the TShape-instanced representation (shared sub-shape
    /// geometry under per-instance transforms); empty while the flattened
    /// build is active. See updateVisual().
    Gui::CoinPtr<SoGroup>  pFaceInstRoot;
    Gui::CoinPtr<SoGroup>  pEdgeInstRoot;
    Gui::CoinPtr<SoGroup>  pVertexInstRoot;
    std::unique_ptr<ShapeInstanceRep> instanced;

    /// TShape-instanced build of a qualifying compound (leaf sub-shapes
    /// with repeated TShapes share one tessellation from the global
    /// table under per-instance transforms). Returns false when the
    /// shape or environment does not qualify — the caller then runs the
    /// flattened build exactly as before.
    bool buildInstanced();
    /// Cheap pre-check whether the current shape could instance at all
    /// (environment gate + compound) — used to schedule lazy rebuilds on
    /// color-divergence transitions.
    bool instancingCandidate() const;

    /// Apply resolved per-face colors to the instanced representation:
    /// partitions the instances by their slice of the vector — uniform
    /// slices ride per-instance override materials on the shared base
    /// subgraph, divergent slices reference baked, refcounted color
    /// variants from the global table (one per distinct vector).
    void applyInstancedFaceColors(const std::vector<App::Color> &colors);
    /// Same partitioning for resolved per-edge / per-vertex colors:
    /// uniform slices ride per-instance override materials, divergent
    /// slices baked line/point color variants (diffuse only, like the
    /// flattened per-edge/per-vertex paths).
    void applyInstancedLineColors(const std::vector<App::Color> &colors);
    void applyInstancedPointColors(const std::vector<App::Color> &colors);

    /// One shape's worth of tessellation into the given nodes — the
    /// whole shape for the flattened build, one sub-shape (at identity
    /// location) per unique TShape for the instanced build. Static so
    /// the desktop exact refine can rebuild an instanced entry's
    /// shared nodes after the view provider that first built them is
    /// gone (the entry outlives any one sharer).
    /// One instanced geometry entry's whole level cycle (§13): the
    /// shared leaf registers at \a builtError (coarse) or at error 0
    /// (exact); the climb rebuilds the shared nodes exact and
    /// re-registers with the demotion armed; the demotion drops the
    /// exact triangulation (the coarse one never left the shape),
    /// rebuilds coarse and re-registers with a fresh climb. A named
    /// static rather than closures referencing each other, because
    /// that cycle of owning std::functions would keep the shape alive
    /// forever. Captured node pointers stay valid for as long as the
    /// registration lives — the entry release unregisters first.
    static void registerInstancedLevelEntry(const TopoDS_Shape &local,
                          bool exact, float builtError,
                          double coarseDefl, double coarseAng,
                          double exactDefl, double exactAng,
                          bool normalsFromUV,
                          SoCoordinate3 *coords, SoCoordinate3 *pcoords,
                          SoNormal *norm, SoTextureCoordinate2 *texcoords,
                          SoBrepFaceSet *faceset, SoBrepEdgeSet *lineset,
                          SoBrepPointSet *nodeset);

    static void buildVisualNodes(const TopoDS_Shape &cShape,
                          double deflection, double angDeflectionRads,
                          bool normalsFromUV,
                          SoCoordinate3 *coords, SoCoordinate3 *pcoords,
                          SoNormal *norm, SoTextureCoordinate2 *texcoords,
                          SoBrepFaceSet *faceset, SoBrepEdgeSet *lineset,
                          SoBrepPointSet *nodeset,
                          int &numTriangles, int &numNodes, int &numPoints,
                          int &numNorms, int &numFaces, int &numEdges,
                          int &numLines);

    bool VisualTouched;
    bool NormalsFromUV;
    /// This shape's visual build was put off to the progressive-load queue
    /// and is waiting its slice; a restore asks for the same visual more
    /// than once, and the flag keeps it queued only the first time.
    bool VisualDeferred = false;

    /** Park this shape's visual build instead of building it now.
     *
     * A document restore asks for the visual of every shape it brings back,
     * inline on the main thread, while nothing paints. With Progressive
     * document load on, the ask is parked and served in bounded slices once
     * the load is over. Returns whether it was parked, in which case the
     * shape is left visually touched and the caller is done.
     */
    bool deferVisualForLoad();
    /// Build one slice of the parked visuals, then reschedule if any remain.
    static void runDeferredVisualSlice();
    /// Post the next slice to the event loop (nothing if one is pending).
    static void scheduleDeferredVisualSlice(int delayMs = 0);
    /// The TShape whose exact tessellation the desktop refine has
    /// already transferred onto the flattened shape (§13): while the
    /// current shape still is that one, updateVisual builds at the
    /// full display deviation (the exact triangulation is resident —
    /// meshing is a no-op) instead of going coarse-first again.
    const void *ExactMeshTShape = nullptr;
    /// The error of the coarse rung that refine kept resident beside
    /// the exact one — what a demotion under memory pressure falls
    /// back to, and what the plan prices it by (§13 step 3).
    float ExactMeshCoarseError = 0.0f;
    /// The TShape whose coarse tessellation the refine pool has
    /// already delivered behind a bounding-box stand-in (progressive
    /// import of an oversized part): while the current shape still is
    /// that one, updateVisual takes the ordinary coarse-first path —
    /// the coarse triangulation is resident, meshing is a no-op —
    /// instead of standing in again.
    const void *CoarseMeshTShape = nullptr;
    bool UpdatingColor;
    bool highlightFaceEdges = false;

    /// Whether the last APPLIED per-face materials diverge in value in a
    /// way the instanced representation cannot carry (a same-valued
    /// array counts as uniform). Any per-element color vector is carried
    /// by the face/line/point color-variant layers, so this only rises
    /// for per-face MATERIAL divergence beyond diffuse+transparency.
    /// The setHighlightedFaces entry points maintain it and restructure
    /// on transitions.
    bool appliedFaceColorsDivergent = false;

    std::string shapePropName;

    friend class SoFCCoordinate3;

private:
    // settings stuff
    int forceUpdateCount;
    static App::PropertyFloatConstraint::Constraints sizeRange;
    static App::PropertyFloatConstraint::Constraints tessRange;
    static App::PropertyQuantityConstraint::Constraints angDeflectionRange;
    static const char* LightingEnums[];
    static const char* DrawStyleEnums[];

    Part::TopoShape cachedShape;
    boost::signals2::scoped_connection conn;
};

}

#endif // PARTGUI_VIEWPROVIDERPARTEXT_H
