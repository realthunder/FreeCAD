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
    // Faces (Gui::ViewProviderGeometryObject::ShapeColor and Gui::ViewProviderGeometryObject::ShapeMaterial apply)
    App::PropertyColorList DiffuseColor;

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
    /// already transferred onto the flattened shape (sec 13): while the
    /// current shape still is that one, updateVisual builds at the
    /// full display deviation (the exact triangulation is resident --
    /// meshing is a no-op) instead of going coarse-first again.
    const void *ExactMeshTShape = nullptr;
    /// The error of the coarse rung that refine kept resident beside
    /// the exact one -- what a demotion under memory pressure falls
    /// back to, and what the plan prices it by (sec 13 step 3).
    float ExactMeshCoarseError = 0.0f;
    /// The TShape whose coarse tessellation the refine pool has
    /// already delivered behind a bounding-box stand-in (progressive
    /// import of an oversized part): while the current shape still is
    /// that one, updateVisual takes the ordinary coarse-first path --
    /// the coarse triangulation is resident, meshing is a no-op --
    /// instead of standing in again.
    const void *CoarseMeshTShape = nullptr;
    /// How much coarser than its ladder rung this object is currently
    /// tessellated (sec 13, dynamic scale). 1 is the rung itself; the
    /// level plan multiplies it by Render_LevelScale each time it
    /// picks this object to free memory, so the descent is unbounded
    /// and, above all, PER OBJECT -- the plan spends the cheapest
    /// visible error in the scene, not a global coarseness. Reset
    /// whenever the shape changes, since it describes a tessellation
    /// of that shape and nothing else.
    double MeshErrorScale = 1.0;
    /// The TShape MeshErrorScale is about; a different one is a new
    /// shape and starts again at its rung.
    const void *MeshErrorScaleTShape = nullptr;
    /// Set when a coarser re-tessellation came back no smaller than
    /// what it replaced -- deflection has run out for this shape.
    ///
    /// It does for most mechanical geometry sooner than one would
    /// think: a planar face is two triangles at any deflection, so a
    /// shape whose faces are mostly flat cannot be coarsened by asking
    /// BRepMesh for a bigger number. Measured on the rack model, a 4x
    /// coarser tessellation removed only 19% of the primitives. Once
    /// this is set the object stops paying for re-tessellations that
    /// buy nothing and goes to a representation that actually drops
    /// faces (its bounding box today; mesh simplification, which can
    /// merge across faces, is the better rung and belongs here).
    bool MeshErrorScaleExhausted = false;
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
