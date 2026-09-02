/***************************************************************************
 *   Copyright (c) 2006 Werner Mayer <wmayer[at]users.sourceforge.net>     *
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


#ifndef GUI_VIEWPROVIDER_GEOMETRYOBJECT_H
#define GUI_VIEWPROVIDER_GEOMETRYOBJECT_H

#include "ViewProviderDragger.h"
#include <App/MaterialAppearance.h>
#include <Base/Tools.h>
#include <Inventor/lists/SoPickedPointList.h>
#include <cstdint>
#include <vector>

class SoPickedPointList;
class SoSwitch;
class SoSensor;
class SbVec2s;
class SoBaseColor;
class SoNodeSensor;
class SoTexture2;
class SoTexture2Transform;
class SoBumpMap;
class SoShadowStyle;

class SoShaderProgram;

namespace Gui {

/** ShapeColor, whose storage is the object's ShapeAppearance
 *
 * The diffuse colour is one datum and the appearance owns it. This keeps
 * the familiar name, the property editor row and every call site that says
 * vp->ShapeColor, while writing through to the appearance so that the two
 * can never disagree.
 *
 * PropertyColor::getValue() returns a reference and is not virtual, so the
 * inherited value is kept as a mirror of the appearance's entry 0 and every
 * write refreshes it. The writers below hide the base ones by name, which
 * is all that is needed: every call site reaches this through its own
 * static type, and everything that goes through a Property base pointer --
 * Save, Restore, Copy, Paste, get/setPyObject -- is already virtual.
 */
class GuiExport PropertyShapeColor : public App::PropertyColor
{
    TYPESYSTEM_HEADER_WITH_OVERRIDE();

public:
    /// The appearance this colour lives in. Null until the owner wires it.
    void setAppearance(App::PropertyAppearanceList *appearance)
    { _appearance = appearance; }

    void setValue(const Base::Color &col);
    void setValue(float r, float g, float b, float a = 1.0F);
    void setValue(uint32_t rgba);

    /// Refresh the mirror from the appearance, without writing back. A value
    /// that has not moved is not written: PropertyColor::setValue announces
    /// unconditionally, and every appearance change refreshes this.
    void mirrorValue(const Base::Color &col)
    { if (col != getValue()) App::PropertyColor::setValue(col); }

    void Restore(Base::XMLReader &reader) override;

    /** Fold a restored colour into the appearance
     *
     * Also the entry point for an old document's ShapeColor. Writes entry 0
     * only when the appearance is not already carrying per-face colours:
     * before this property existed, ShapeColor was the whole-object colour
     * and DiffuseColor the per-face override, and they were allowed to
     * disagree. DiffuseColor and ShapeAppearance both sort before ShapeColor,
     * so by the time this runs the more specific value is already in place
     * and must win.
     */
    void applyToAppearance();

private:
    App::PropertyAppearanceList *_appearance {nullptr};
};

/** ShapeMaterial, kept as a name over the appearance
 *
 * The class was PropertyShapeMaterial; the PROPERTY is still called
 * ShapeMaterial and stays that way -- that name is in every saved
 * document and every macro, and renaming it is a separate decision from
 * renaming the type. Its registered type name keeps an alias to the old
 * spelling (see Gui::PropertyShapeAppearance::init).
 *
 * Retired as a store: the appearance holds the material. Kept as a property
 * so old macros and old documents that say ShapeMaterial still land
 * somewhere, and hidden from the property editor so one datum does not
 * appear as two rows.
 */
class GuiExport PropertyShapeAppearance : public App::PropertyAppearance
{
    TYPESYSTEM_HEADER_WITH_OVERRIDE();

public:
    void setAppearance(App::PropertyAppearanceList *appearance)
    { _appearance = appearance; }

    void setValue(const App::MaterialAppearance &mat);
    /// See PropertyShapeColor::mirrorValue; same no-op rule
    void mirrorValue(const App::MaterialAppearance &mat)
    { if (!(mat == getValue())) App::PropertyAppearance::setValue(mat); }

    void Restore(Base::XMLReader &reader) override;
    /// See PropertyShapeColor::applyToAppearance; same ordering rule
    void applyToAppearance();

private:
    App::PropertyAppearanceList *_appearance {nullptr};
};

class SoFCSelection;
class SoFCBoundingBox;
class SoFCRenderMaterial;
class SoFCRenderTexture;
class View3DInventorViewer;

/**
 * The base class for all view providers that display geometric data, like mesh, point clouds and shapes.
 * @author Werner Mayer
 */
class GuiExport ViewProviderGeometryObject : public ViewProviderDragger
{
    PROPERTY_HEADER_WITH_OVERRIDE(Gui::ViewProviderGeometryObject);

    typedef ViewProviderDragger inherited;

public:
    /// constructor.
    ViewProviderGeometryObject();

    /// destructor.
    ~ViewProviderGeometryObject() override;

    // Display properties
    PropertyShapeColor ShapeColor;
    App::PropertyPercent Transparency;
    /** The object's appearance, one entry per face or a single shared one
     *
     * Replaces the old ShapeMaterial. Storage is per field, so the common
     * case of one appearance for the whole object costs one entry per field
     * rather than one whole material (docs/ShapeAppearanceDesign.md).
     */
    App::PropertyAppearanceList ShapeAppearance;
    /// Retired store, kept as a name over the appearance (hidden in the editor)
    PropertyShapeAppearance ShapeMaterial;
    App::PropertyBool BoundingBox;

    /**
     * Attaches the document object to this view provider.
     */
    void attach(App::DocumentObject *pcObject) override;
    void updateData(const App::Property*) override;

    /// Adding a Render_* dynamic property takes effect immediately:
    /// same-value property writes do not notify (Property::hasSetValue
    /// skips them), so e.g. a freshly added default-false
    /// Render_CastShadow would otherwise stay inert until toggled.
    App::Property* addDynamicProperty(
            const char* type, const char* name = nullptr,
            const char* group = nullptr, const char* doc = nullptr,
            short attr = 0, bool ro = false, bool hidden = false) override;

    /// Removing a Render_* dynamic property reverts its effect right
    /// away (removal does not notify onChanged).
    bool removeDynamicProperty(const char* name) override;

    /// Adds "Render settings..." (the Render_* task panel).
    void setupContextMenu(QMenu* menu, QObject* receiver,
                          const char* member) override;

    void finishRestoring() override;

    /** Re-derive ShapeColor, ShapeMaterial and Transparency from the
     * appearance. A no-op for a document that states them; the whole
     * migration for one that has only ShapeAppearance.
     */
    void refreshAppearanceMirrors();

    /** Choose the appearance's base, for a document written without one
     *
     * Every document written before docs/ShapeAppearanceDesign.md 12 -- and
     * every one saved at schema 4, which cannot state a base -- holds one
     * material per face and nothing saying which of them the object is.
     * This runs the heuristic of 12.4 once, here, where the mirror is in
     * hand: the mirror first, then the face areas if it declines.
     */
    void deriveAppearanceBase();

    /** One weight per face for that heuristic, if this view provider has
     * a shape to measure
     *
     * Area, not count: a green board with five hundred gold pads is decided
     * the wrong way by count. False when there is nothing to measure, and
     * then the count decides -- which is what 12.4 calls the fallback with
     * no shape at hand. Asked only when the mirror has already declined,
     * because measuring every face of an import is not free.
     */
    virtual bool getFaceWeights(std::vector<double> &weights) const;

    /** Take the object's material card as the appearance's base
     *
     * A no-op unless the appearance is FOLLOWING the card
     * (docs/MaterialStorage.md 15.3) -- or has never been touched, which is
     * how a fresh object with a card starts following one. The overriding
     * faces are left alone, so a following object keeps its painted faces.
     */
    void applyMaterialAppearance();

    /** Whether going back to the card's look would change anything
     *
     * There has to be a card with a look to go back TO, and the object has
     * to have stopped following it. What the panel's Reset to material
     * button and the context-menu command both ask before offering
     * themselves (docs/MaterialStorage.md 15.5).
     */
    bool canResetAppearanceToMaterial() const;

    /** Take the card's look again, and follow it from now on
     *
     * The deliberate way back, so no follow guard: this is what ENDS a
     * look the user chose. The faces holding a look of their own keep it
     * -- this is not "clear the overrides". Answers whether it did
     * anything.
     */
    bool resetAppearanceToMaterial();

    /** Decide, once, whether a restored appearance follows its card
     *
     * A document written before the flag existed cannot state it, and
     * neither can one saved at schema 4. The answer is the one the old
     * runtime heuristic gave: a base that is the card's look, or an
     * untouched one while the card has a look, is following
     * (docs/MaterialStorage.md 15.4).
     */
    void deriveFollowMaterial();

    /**
     * Returns a list of picked points from the geometry under \a getRoot().
     * If \a pickAll is false (the default) only the intersection point closest to the camera will be picked, otherwise
     * all intersection points will be picked.
     */
    SoPickedPointList getPickedPoints(const SbVec2s& pos, const View3DInventorViewer& viewer,bool pickAll=false) const;
    /**
     * This method is provided for convenience and does basically the same as getPickedPoints() unless that only the closest
     * point to the camera will be picked.
     * \note It is in the response of the client programmer to delete the returned SoPickedPoint object.
     */
    SoPickedPoint* getPickedPoint(const SbVec2s& pos, const View3DInventorViewer& viewer) const;

    /** @name Edit methods */
    //@{
    virtual void showBoundingBox(bool);
    //@}

    /** The shared card program node this object wears, or null
     *
     * For the one other inserter at the root's head: a Scope=Object
     * ShaderBinding must place its own program AFTER this one,
     * because the render cache keeps the LAST material-stage
     * program traversed and an explicit binding beats the card the
     * object wears.
     */
    SoShaderProgram *getMaterialXNode() const { return pcMaterialXNode; }

protected:
    /// get called by the container whenever a property has been changed
    void onChanged(const App::Property* prop) override;
    /// Restore a pre-ShapeAppearance document. ShapeColor and ShapeMaterial
    /// kept their names but changed type, so they arrive here; the base does
    /// nothing, which would drop the value.
    void handleChangedPropertyType(Base::XMLReader &reader,
                                   const char *TypeName,
                                   App::Property *prop) override;

    /// Push one whole material into the Coin material node
    void setCoinAppearance(const App::MaterialAppearance &mat);

    virtual unsigned long getBoundColor() const;
    void updateBoundingBox();
    void addBoundSwitch();
    /// Sync the optional SoFCRenderMaterial node (render engine per-object
    /// PBR parameters) with the Render_* dynamic properties.
    void updateRenderMaterial();
    /** Sync the shared MaterialX program node with the appearance
     *
     * The base's MaterialAppearance::materialx names a card-carried
     * document set by its manifest hash; the shared node built from it
     * (ViewProviderShaderBinding::acquireMaterialXNode) goes in at the
     * root's head like a binding's does, and is given back when the hash
     * changes or goes. Called from updateRenderMaterial(), so both an
     * appearance edit and the post-restore pass reach it -- the second
     * matters because the node needs blobs a restore drains only after
     * the view document has been read.
     */
    void updateMaterialXNode();
    /// Sync the optional SoTexture2/SoTexture2Transform/SoBumpMap/
    /// SoFCRenderTexture nodes with the Render_BaseColorTexture /
    /// Render_Texture* / Render_NormalMap / Render_EmissiveMap /
    /// Render_OcclusionMap / Render_MetallicRoughnessMap dynamic
    /// properties.
    void updateRenderTexture();
    /** Sync the per-face texture palette with the ShapeAppearance
     *
     * A face of the appearance may name an image (Material::imagePath,
     * or Material::image holding the encoded bytes of one), and the
     * distinct images become a PALETTE of SoFCRenderTexture nodes in
     * the FACE slot -- layer 1 and up, since layer 0 is the untextured
     * face. \a indices comes back holding the layer of every face, or
     * empty when no face names an image.
     *
     * Only the render engine draws these: Coin's GL path binds one
     * texture per draw and has nowhere to put a palette.
     */
    void updateFaceTextures(std::vector<int32_t> &indices);
    /** Millimetres of object space per tile of a per-face image
     *
     * Render_FaceTextureScale, resolved: an explicit zero asks for the
     * mesh's OWN texture coordinates and comes back negative, an unset
     * property takes the hand-sized default, and a positive value is
     * itself. Both the material node and the texture nodes key on it --
     * the mesh-UV reading needs a texture unit enabled before the
     * shapes will generate any coordinates at all.
     */
    float faceTextureScale() const;
    /** The render material node needs what only the geometry can state
     *
     * The projection frames a finish and the per-face images are laid
     * out in are written onto SoFCRenderMaterial at TESSELLATION time
     * (the Part view provider reads them off the OCCT surfaces), and
     * that node does not exist until something states a render
     * property. So a shape drawn plain and given an image afterwards
     * has a node with no frames in it, and every face would be
     * projected in the first one's.
     *
     * This is called once in that situation, and a view provider that
     * can restate its geometry answers by re-running its visual build.
     * The default does nothing -- a view provider with no analytic
     * surfaces has no frames to state.
     */
    virtual void renderMaterialNeedsGeometry() {}
    /// Sync the optional SoShadowStyle node with the Render_CastShadow /
    /// Render_ReceiveShadow dynamic properties.
    void updateRenderShadowStyle();
    /// Dispatch a Render_* property (name) change to the update above.
    void updateRenderProperty(const char *name);

protected:
    SoMaterial       * pcShapeMaterial{nullptr};
    SoFCRenderMaterial * pcRenderMaterial{nullptr};
    /// The shared card node this object wears, and the manifest hash it
    /// was acquired for (see updateMaterialXNode)
    SoShaderProgram  * pcMaterialXNode{nullptr};
    std::string materialXHash;
    /// Whether the appearance's MaterialX column VARIED at the last
    /// sync. Only the base is drawn, so this is what keeps the
    /// warning about that to the edit that starts it.
    bool materialXVaries{false};
    SoTexture2       * pcRenderTexture{nullptr};
    SoTexture2Transform * pcRenderTexTransform{nullptr};
    SoBumpMap        * pcRenderBumpMap{nullptr};
    SoFCRenderTexture * pcRenderEmissiveMap{nullptr};
    SoFCRenderTexture * pcRenderOcclusionMap{nullptr};
    SoFCRenderTexture * pcRenderMetallicRoughnessMap{nullptr};
    /// The per-face texture palette, one node a layer, in layer order
    /// (entry 0 is layer 1 -- layer 0 is the untextured face).
    std::vector<SoFCRenderTexture *> pcFaceTextures;
    /// Whether renderMaterialNeedsGeometry() has been asked already, so
    /// a shape whose geometry states no frames at all is asked once
    /// rather than on every appearance edit.
    bool renderGeometryAsked{false};
    /// What each of those nodes was loaded from (image path, inline
    /// image data): a palette node is only rewritten when its source
    /// changed, since every write invalidates the render caches below.
    std::vector<std::pair<std::string, std::string>> faceTextureSources;
    SoShadowStyle    * pcRenderShadowStyle{nullptr};

private:
    SoFCBoundingBox  * pcBoundingBox{nullptr};
    SoSwitch         * pcBoundSwitch{nullptr};
    SoBaseColor      * pcBoundColor{nullptr};
    SoNodeSensor     * pcSwitchSensor{nullptr};
};

/** @name The Render_* dynamic view properties
 *
 * These are optional per-object properties: a feature is "overridden"
 * exactly when its property exists on the view provider, and absent means
 * the engine's default rather than zero. Two callers create them -- the
 * Render Settings task panel and a material card carrying render
 * properties -- so the creation lives here and there is one creator.
 * addDynamicProperty applies a fresh property immediately.
 *
 * The helpers take the container, not the view provider: nothing in them
 * is Gui, and that is what lets the mapping be tested on a document
 * object -- a view provider cannot exist without the Gui application and
 * its main window, which every property write of one reaches.
 */
//@{
template<class PropT>
PropT *getRenderProperty(App::PropertyContainer *vp, const char *name)
{
    return Base::freecad_dynamic_cast<PropT>(vp->getPropertyByName(name));
}

template<class PropT>
PropT *ensureRenderProperty(App::PropertyContainer *vp, const char *type,
                            const char *name, const char *doc)
{
    if (auto prop = getRenderProperty<PropT>(vp, name))
        return prop;
    return Base::freecad_dynamic_cast<PropT>(
            vp->addDynamicProperty(type, name, "Render", doc));
}

inline void removeRenderProperty(App::PropertyContainer *vp, const char *name)
{
    if (vp->getPropertyByName(name))
        vp->removeDynamicProperty(name);
}

/** State a material card's render properties on a view provider.
 *
 * Creates what @a props names and REMOVES every property of a feature it
 * does not name, so switching from a glass card to an ordinary one leaves
 * no strays behind. Answers whether anything changed.
 */
GuiExport bool applyMaterialRenderProperties(App::PropertyContainer *vp,
                                             const App::MaterialRenderProperties &props);
//@}

} // namespace Gui


#endif // GUI_VIEWPROVIDER_GEOMETRYOBJECT_H
