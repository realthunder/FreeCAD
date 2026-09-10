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

namespace Render {
struct SimplifiedMesh;
struct SimplifyStats;
}

namespace Gui { class SoFCRenderMaterial; }

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
    void setAppearance(App::PropertyAppearanceList *appearance,
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
    App::PropertyAppearanceList *_appearance {nullptr};
    const App::PropertyColor *_shapeColor {nullptr};
    /** Where getValues() resolves the appearance into
     *
     * The appearance stores a base and the faces that override it, so there
     * is no colour-per-face array in it to hand a reference into. This is
     * that array, rebuilt on every read -- which is what returning it by
     * value would cost anyway, and this way the reference stays good until
     * the next read, which is what every caller of a list property's
     * getValues() has always been able to assume.
     */
    mutable std::vector<Base::Color> _resolved;
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

    /// Why the dynamic-scale descent stopped re-tessellating a shape.
    /// The two non-No states are DIFFERENT CLAIMS, and one slot
    /// carrying both is how a 63% became a 100% once already
    /// (docs/SceneStreaming.md #13e):
    /// - Proved: a coarser ask came back no smaller (after*10 >=
    ///   before*9). Sound AT THE STEP THAT ESTABLISHED IT, and that
    ///   is all: audited on the rack model (2026-08-13), one proved
    ///   shape in five resumes shrinking at some coarser ask, so a
    ///   skip keyed on this claim -- alone or combined with the
    ///   redundancy check's finer-resident case -- denies a real
    ///   coarsening ~20% of the time and is REFUTED. Do not build
    ///   one without re-running that audit (MeshCallProbe counts
    ///   both halves).
    /// - BoxChosen: the plan stopped because the scaled error passed
    ///   the box threshold. The shape may well still coarsen; same
    ///   verdict, worse numbers (~50%).
    /// Public because the call probe that audits the two populations
    /// lives outside the class.
    enum class ScaleSpent : unsigned char { No, BoxChosen, Proved };

    // Display properties
    App::PropertyFloatConstraint Deviation;
    App::PropertyBool ControlPoints;
    App::PropertyAngle AngularDeflection;
    App::PropertyEnumeration Lighting;
    App::PropertyEnumeration DrawStyle;
    // Points
    App::PropertyFloatConstraint PointSize;
    App::PropertyColor PointColor;
    App::PropertyAppearance PointMaterial;
    App::PropertyColorList PointColorArray;
    // Lines
    App::PropertyFloatConstraint LineWidth;
    App::PropertyColor LineColor;
    App::PropertyAppearance LineMaterial;
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
    void setHighlightedFaces(const std::vector<App::MaterialAppearance>& colors);
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
    virtual void setEditViewer(Gui::ViewerContext*, int ModNum) override;

    virtual void setShapePropertyName(const char *propName);
    const char *getShapePropertyName() const;

    void setHighlightFaceEdges(bool enable);

    Part::TopoShape getShape() const;
    virtual void updateVisual();
    /// Draw the shape as its 12-triangle bounding box: the stand-in of
    /// an oversized part during a progressive import (the coarse mesh
    /// follows from the refine pool), or -- \a underPressure -- the
    /// level plan's descent (sec 13, dynamic scale). Under pressure it
    /// skips the gates that exist to stop the import path standing in
    /// for a mesh already built, because here the mesh exists and
    /// giving it back is the point. Returns whether the box was built.
    bool buildCoarseStandIn(bool underPressure = false);

    virtual void reattach(App::DocumentObject *) override;
    virtual void beforeDelete() override;
    virtual void finishRestoring() override;
    /// The area of every face of the shape, for the appearance base
    /// heuristic. @see Gui::ViewProviderGeometryObject::getFaceWeights
    bool getFaceWeights(std::vector<double> &weights) const override;

protected:
    bool setEdit(int ModNum) override;
    void unsetEdit(int ModNum) override;
    /// The projection frames are read off the OCCT surfaces and written
    /// into the render material by the visual build, so restating them
    /// means running it again (buildVisualNodes, ~7160).
    void renderMaterialNeedsGeometry() override {
        if (isUpdateForced() || Visibility.getValue()) {
            updateVisual();
        }
        else {
            VisualTouched = true;
        }
    }
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
    /// shape or environment does not qualify -- the caller then runs the
    /// flattened build exactly as before.
    bool buildInstanced();
    /// Cheap pre-check whether the current shape could instance at all
    /// (environment gate + compound) -- used to schedule lazy rebuilds on
    /// color-divergence transitions.
    bool instancingCandidate() const;

    /// Apply resolved per-face colors to the instanced representation:
    /// partitions the instances by their slice of the vector -- uniform
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

    /// One shape's worth of tessellation into the given nodes -- the
    /// whole shape for the flattened build, one sub-shape (at identity
    /// location) per unique TShape for the instanced build. Static so
    /// the desktop exact refine can rebuild an instanced entry's
    /// shared nodes after the view provider that first built them is
    /// gone (the entry outlives any one sharer).
    /// One instanced geometry entry's whole level cycle (sec 13): the
    /// shared leaf registers at \a builtError (coarse) or at error 0
    /// (exact); the climb rebuilds the shared nodes exact and
    /// re-registers with the demotion armed; the demotion drops the
    /// exact triangulation (the coarse one never left the shape),
    /// rebuilds coarse and re-registers with a fresh climb. A named
    /// static rather than closures referencing each other, because
    /// that cycle of owning std::functions would keep the shape alive
    /// forever. Captured node pointers stay valid for as long as the
    /// registration lives -- the entry release unregisters first.
    static void registerInstancedLevelEntry(const TopoDS_Shape &local,
                          bool exact, float builtError,
                          double coarseDefl, double coarseAng,
                          double exactDefl, double exactAng,
                          bool normalsFromUV,
                          SoCoordinate3 *coords, SoCoordinate3 *pcoords,
                          SoNormal *norm, SoTextureCoordinate2 *texcoords,
                          SoBrepFaceSet *faceset, SoBrepEdgeSet *lineset,
                          SoBrepPointSet *nodeset);

    struct MeshLadderState;
    /// One rebuild's fill, detached from the display nodes: the
    /// GUI-captured handles to everything mutable the fill reads
    /// (resident triangulations, edge polygons -- the topology
    /// itself is immutable at runtime), and the plain arrays the
    /// fill produces. Defined in the implementation file; the split
    /// into capture/fill/apply below is what lets the fill of a big
    /// landing rebuild run on the refine pool
    /// (Render_VisualFillOnPool) with the capture and the apply
    /// staying on the GUI thread.
    struct VisualFillData;
    /// GUI thread: the tessellation step (with its skip rules) and
    /// the snapshot of every handle the fill dereferences. False when
    /// the capture failed -- the caller falls back to the inline
    /// fill.
    static bool captureVisualFill(const TopoDS_Shape &cShape,
                          double deflection, double angDeflectionRads,
                          bool normalsFromUV,
                          ScaleSpent tessellationSpent,
                          MeshLadderState *ladder,
                          bool residentLanded,
                          VisualFillData &data);
    /// Any thread: fill the detached arrays from the captured
    /// handles and the (immutable) topology of the captured shape.
    static void fillVisualArrays(VisualFillData &data);
    /// Any thread, called by the fill when the pooled path asked for
    /// it: emit the vertex-cache content of the three drawables next
    /// to the display arrays (docs/WorkerVertexCache.md), for the
    /// landing to register and the next publish to adopt in place of
    /// the traversal capture.
    static void emitVisualVertexCache(VisualFillData &data);
    /// GUI thread: write the filled arrays into the display nodes.
    static void applyVisualFill(const VisualFillData &data,
                          SoCoordinate3 *coords, SoCoordinate3 *pcoords,
                          SoNormal *norm, SoTextureCoordinate2 *texcoords,
                          SoBrepFaceSet *faceset, SoBrepEdgeSet *lineset,
                          SoBrepPointSet *nodeset,
                          int &numTriangles, int &numNodes, int &numPoints,
                          int &numNorms, int &numFaces, int &numEdges,
                          int &numLines,
                          /// Where the per-face projection frames the
                          /// fill computed are stated; null on the
                          /// paths that build no particular object's
                          /// nodes (see buildVisualNodes).
                          Gui::SoFCRenderMaterial *rendermat = nullptr);
    /// Queue this rebuild's fill on the refine pool
    /// (Render_VisualFillOnPool): capture here, fill on a worker,
    /// land the array writes plus the epilogue updateVisual would
    /// have run (arm, decimation post-step, highlight re-apply)
    /// through the landing pump. The landing is guarded by the shape
    /// identity and meshLadder.visualFillSeq, and the worker token is
    /// keyed on the coords node -- its own slot, so a pending fill
    /// and a pending decimation or mesh job never cancel each other.
    /// False when the capture failed; the caller fills inline then.
    bool queueVisualFillOnPool(const TopoDS_Shape &cShape,
                               double deflection, double angDeflectionRads,
                               bool residentLanded,
                               float builtError, double shapeDiag);
    static void buildVisualNodes(const TopoDS_Shape &cShape,
                          double deflection, double angDeflectionRads,
                          bool normalsFromUV,
                          SoCoordinate3 *coords, SoCoordinate3 *pcoords,
                          SoNormal *norm, SoTextureCoordinate2 *texcoords,
                          SoBrepFaceSet *faceset, SoBrepEdgeSet *lineset,
                          SoBrepPointSet *nodeset,
                          int &numTriangles, int &numNodes, int &numPoints,
                          int &numNorms, int &numFaces, int &numEdges,
                          int &numLines,
                          /// Whether -- and on WHOSE authority -- the
                          /// caller already knows re-tessellating this
                          /// shape buys nothing (meshLadder.scaleSpent).
                          /// A spent rebuild's deflection keeps doubling
                          /// away from a mesh that will never move, so
                          /// the call cannot achieve anything -- it is
                          /// the caller's knowledge, and this function
                          /// is static and cannot ask. The distinction
                          /// between the two spent states is what the
                          /// call probe audits before any skip may act
                          /// on it.
                          ScaleSpent tessellationSpent = ScaleSpent::No,
                          /// The caller's ladder state, where the
                          /// deflection-invariance classification is
                          /// cached -- again the caller's knowledge,
                          /// because this function is static. Null (the
                          /// instanced-leaf and stand-in paths) never
                          /// classifies or skips on invariance.
                          MeshLadderState *ladder = nullptr,
                          /// This fill is the display half of a landing
                          /// (MeshLadderState::residentLanded): the
                          /// caller just installed the triangulation to
                          /// display, so the BRepMesh call would only
                          /// validate it (Render_MeshSkipLanded).
                          bool residentLanded = false,
                          /** Where the per-face projection frames of a
                           * surface finish go (faceProjectionFrame).
                           * Optional because this is a static builder
                           * several paths share, and only the ones that
                           * build a PARTICULAR object's nodes have a
                           * render material to write: the shared
                           * instanced geometry and the bounding-box
                           * stand-in do not, and their faces keep the
                           * renderer's triplanar projection. */
                          Gui::SoFCRenderMaterial *rendermat = nullptr);

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
    /// Decimate the display nodes in place onto a grid of \a cellSize
    /// (docs/SceneStreaming.md #13c), the rung between "OCCT cannot
    /// tessellate this any coarser" and the bounding box. Reads and
    /// rewrites the nodes only -- no re-tessellation, and the OCCT
    /// triangulation is untouched, so a full rebuild restores exactness.
    /// False when it is disabled, has no triangles to work on, or did
    /// not remove enough to be worth the rebuild; the caller then takes
    /// the box, which is the next step down.
    bool simplifyVisualInPlace(double cellSize, double shapeDiag,
                               float builtErrorNow);
    /// Write a clustered rung back into the display nodes and restate
    /// the published error -- the landing half of the decimation,
    /// shared by the synchronous rebuild post-step and the worker
    /// job's GUI landing. \a beforeTris is the input's triangle count
    /// (the reduction judgement); false when the rung did not remove
    /// enough to be worth the rewrite (the caller reads that as
    /// "decimation is spent, the box is next").
    bool applySimplifiedRung(const Render::SimplifiedMesh &rung,
                             const Render::SimplifyStats &stats,
                             size_t beforeTris, double cellSize,
                             double shapeDiag, float builtErrorNow);
    /// (Re-)register this object's shape with the level registry off
    /// the context updateVisual stored in meshLadder -- the arming of
    /// every climb/descent hook, callable from a worker landing
    /// without paying updateVisual's rebuild.
    void armMeshLevelSource();

    /// Vertex-cache content waiting to be registered for the render
    /// cache to adopt (docs/WorkerVertexCache.md): filled by the pooled
    /// fill's landing from the worker's emission, or re-emitted from
    /// the final node arrays after a decimation rewrite.
    struct PendingVisualVCache;
    std::unique_ptr<PendingVisualVCache> pendingVCache;
    /// GUI thread, after an in-place rewrite of the display arrays
    /// (a decimation rung): re-emit the vertex-cache content from the
    /// node arrays as they now stand, into the pending stash.
    void emitVisualVertexCacheFromNodes();
    /// The same, for the SHARED instanced tessellation, which has no
    /// view provider to stash on: emits from the given nodes and
    /// registers immediately, so the caller must have finished writing
    /// them (docs/WorkerVertexCache.md).
    static void emitAndRegisterSharedVertexCache(
            SoCoordinate3 *coords, SoCoordinate3 *pcoords, SoNormal *norm,
            SoBrepFaceSet *faceset, SoBrepEdgeSet *lineset,
            SoBrepPointSet *nodeset);
    /// GUI thread, at the END of a rebuild epilogue (after the
    /// highlight re-apply -- the node ids are stamped here, and any
    /// later touch voids the entry): register the stashed content.
    void registerPendingVisualVertexCache();
    /// One decimation descent step, the climb's shape (sec 13c):
    /// snapshot the displayed arrays here on the GUI thread, cluster
    /// them on the refine worker pool, land the node writes and the
    /// re-arm back here. meshLadder.errorScale must already be
    /// advanced to the step being taken.
    void queueDecimationDescent();

    bool deferVisualForLoad();
    /// Build one slice of the parked visuals, then reschedule if any remain.
    static void runDeferredVisualSlice();
    /// Post the next slice to the event loop (nothing if one is pending).
    static void scheduleDeferredVisualSlice(int delayMs = 0);
    /// The coarse-first ladder's per-object state (sec 13). Every
    /// member is a claim PROVED AGAINST ONE SHAPE, so they live behind
    /// a single identity and a single reset: rebind() runs once per
    /// updateVisual, right after the shape is installed and before any
    /// early return, and wipes everything when the TShape moved.
    ///
    /// Why that placement is what makes the identity sound: `anchor`
    /// is a weak address, never dereferenced, so a freed TShape's
    /// address could in principle be recycled by a later allocation.
    /// But rebind() always holds the address of the IMMEDIATELY
    /// previous shape, and inside updateVisual the previous and the
    /// incoming shape coexist (the new one is fetched before the old
    /// reference is dropped), so the two addresses can never collide.
    /// The old scheme -- three anchors reset at three different depths
    /// of the build, some behind early returns -- had exactly that
    /// hole: an object parked on its box never reached the exact-mesh
    /// clear, and a recompute two shapes later could fake
    /// exactResident on a recycled address.
    struct MeshLadderState {
        /// The TShape every claim below is about; compared, never
        /// dereferenced.
        const void *anchor = nullptr;
        /// The desktop refine has transferred this shape's exact
        /// tessellation onto the flattened shape: updateVisual builds
        /// at the full display deviation (the exact triangulation is
        /// resident -- meshing is a no-op) instead of going
        /// coarse-first again.
        bool exactResident = false;
        /// The error of the coarse rung that refine kept resident
        /// beside the exact one -- what a demotion under memory
        /// pressure falls back to, and what the plan prices it by
        /// (sec 13 step 3).
        float exactCoarseError = 0.0f;
        /// The refine pool has delivered this shape's coarse mesh
        /// behind a bounding-box stand-in (progressive import of an
        /// oversized part): updateVisual takes the ordinary
        /// coarse-first path -- the coarse triangulation is resident,
        /// meshing is a no-op -- instead of standing in again.
        bool coarseResolved = false;
        /// How much coarser than its ladder rung this object is
        /// currently tessellated (sec 13, dynamic scale). 1 is the
        /// rung itself; the level plan multiplies it by
        /// Render_LevelScale each time it picks this object to free
        /// memory, so the descent is unbounded and, above all, PER
        /// OBJECT -- the plan spends the cheapest visible error in
        /// the scene, not a global coarseness.
        double errorScale = 1.0;
        /// Non-No when deflection has run out for this shape and the
        /// descent stops re-tessellating. It does for most mechanical
        /// geometry sooner than one would think: a planar face is two
        /// triangles at any deflection (measured on the rack model, a
        /// 4x coarser tessellation removed only 19% of the
        /// primitives). Once set the object stops paying for
        /// re-tessellations that buy nothing and goes to a
        /// representation that actually drops faces: decimation of
        /// the mesh it already has, and below that its bounding box.
        /// The VALUE says which claim stopped it -- a measured proof
        /// or a mere box choice (see ScaleSpent); the behaviour here
        /// is the same for both, but what a skip may safely do with
        /// each is not. Proved is never downgraded to BoxChosen: a
        /// proof outlives the plan's pricing.
        ScaleSpent scaleSpent = ScaleSpent::No;
        /// Set when decimation in turn stopped removing enough to be
        /// worth the rewrite (Render_SimplifyMinReduction) -- the rung
        /// below deflection is spent too, and the next step is the
        /// bounding box. Separate from scaleSpent because the two
        /// are different exhaustions with different next steps, and
        /// one flag doing both jobs would send an object to its box
        /// the moment tessellation saturated, which is exactly the
        /// step the decimation rung exists to delay. Cleared wherever
        /// scaleSpent is: a shape that may be tessellated again
        /// may be decimated again.
        bool decimationSpent = false;
        /// The build context the arming of the level hooks needs
        /// (armMeshLevelSource), stored by updateVisual so a worker
        /// landing can re-register without paying the rebuild:
        /// the shape's bbox diagonal, the coarse ladder rung it was
        /// built for (-1 = exact), and the FULL display-formula
        /// parameters (what the on-demand exact build uses). All
        /// claims about `anchor`'s shape, reset with the rest.
        double shapeDiag = 0.0;
        int coarseLevel = -1;
        double exactDefl = 0.0;
        double exactAng = 0.0;
        /// Whether `anchor`'s tessellation provably cannot depend on
        /// the deflection asked -- every face planar, every edge curve
        /// a straight line (Render_MeshSkipInvariant). A statement
        /// about the GEOMETRY, so it is computed once per anchor and
        /// never invalidated short of rebind; Unknown means not yet
        /// classified.
        enum class MeshInvariance : unsigned char {
            Unknown,
            Invariant,
            Varies,
        };
        MeshInvariance meshInvariance = MeshInvariance::Unknown;
        /// One-shot: the NEXT updateVisual is the rebuild half of a
        /// landing -- a transfer/demote/downgrade on this anchor's
        /// shape just established the very triangulation the rebuild
        /// is to display, so its BRepMesh call is validated-only by
        /// construction: on every landing path the resident rung is
        /// never coarser than the ask (Render_MeshSkipLanded).
        /// Consumed by updateVisual before ANY early exit, so a stale
        /// claim cannot outlive the one build it was made for.
        bool residentLanded = false;
        /// Which rebuild owns the display arrays. Bumped by every
        /// updateVisual that reaches its fill and by every decimation
        /// rewrite; a pooled fill (Render_VisualFillOnPool) captures
        /// the value at queue time and lands only while it still
        /// matches -- any rebuild that ran in between simply wins,
        /// and the stale arrays are dropped instead of applied over
        /// newer ones. Reset with the rest on rebind; an in-flight
        /// fill for the old shape is already dead by the anchor
        /// check.
        unsigned visualFillSeq = 0;

        /// THE reset: a different TShape starts every claim over.
        void rebind(const void *tsh)
        {
            if (anchor == tsh)
                return;
            *this = MeshLadderState();
            anchor = tsh;
        }
        /// Climbing out of a pressure box (the coarse mesh of a boxed
        /// descent arrived): the object may be tessellated again, so
        /// the flags that sent it to the box are spent. The scale
        /// resets WITH the flags -- the worker meshed at the unscaled
        /// rung deflection, and a rebuild still asking scale-x coarser
        /// would refuse that mesh and re-tessellate inline on the GUI
        /// thread, the stall the stand-in and the pool exist to avoid.
        /// One rung, one statement of it.
        void resetDescent()
        {
            errorScale = 1.0;
            scaleSpent = ScaleSpent::No;
            decimationSpent = false;
        }
    };
    MeshLadderState meshLadder;
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
    fastsignals::scoped_connection conn;
};

}

#endif // PARTGUI_VIEWPROVIDERPARTEXT_H
