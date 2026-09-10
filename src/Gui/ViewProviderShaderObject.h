/***************************************************************************
 *   Copyright (c) 2026 Zheng Lei (realthunder) <realthunder.dev@gmail.com>*
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

#ifndef GUI_ViewProviderShaderObject_H
#define GUI_ViewProviderShaderObject_H

#include <map>
#include <vector>

#include <QPointer>

#include "InventorBase.h"
#include "ViewProviderDocumentObject.h"
#include "ViewProviderLink.h"
#include "ViewProviderPythonFeature.h"

class SoShaderProgram;
class SoShaderParameterArray1f;
class SoVertexShader;
class SoFragmentShader;
class SoSeparator;
class SoGroup;

/// Declared rather than included: MaterialXSupport.h is the renderer
/// library's header, and one member function's parameter is no reason
/// for every consumer of this one to see it.
namespace Render::MaterialX
{
struct MaterialInput;
}

namespace Gui {

class View3DInventorViewer;

/** View provider of App::ShaderProgram (docs/RenderDebug.md §6.5).
 *
 * Owns the Coin SoShaderProgram node built from the object's properties.
 * The node instance is shared by every consumer (App::Shader demo preview,
 * ShaderBinding objects), so a property edit updates the node fields and Coin
 * notification invalidates all enclosing render caches — no cross-view
 * notification needed. Displays nothing itself.
 */
class GuiExport ViewProviderShaderProgram : public ViewProviderDocumentObject
{
    PROPERTY_HEADER_WITH_OVERRIDE(Gui::ViewProviderShaderProgram);

public:
    ViewProviderShaderProgram();
    ~ViewProviderShaderProgram() override;

    void attach(App::DocumentObject *obj) override;
    void updateData(const App::Property *prop) override;
    /// Rebuild the node once the document has finished restoring: the
    /// sources are blob-backed and their content arrives after attach()
    void finishRestoring() override;
    bool isShow() const override {return true;}

    /** @name The shader graph editor (docs/ShaderGraphEditor.md sec 4.4)
     *
     * A MATERIALX-dialect program opens in Gui::ShaderGraphView, placed
     * by Gui::ViewPlacement as a document view (a split cell by
     * default). getMDIView() and show() are what the split-view area
     * keys on: the cell content menu lists objects whose view is
     * open, and a saved layout rehydrates an O:<name> cell through
     * show(). Builds without the editor (no bgfx or no MaterialX)
     * have these do nothing.
     */
    //@{
    bool doubleClicked() override;
    void setupContextMenu(QMenu *menu, QObject *receiver,
                          const char *member) override;
    MDIView *getMDIView() const override;
    void show() override;
    void beforeDelete() override;
    /// Reveal the open view of this program, if any. False when none.
    bool activateView() const;
    /// Whether this program's document can be edited in the graph
    /// editor: MATERIALX dialect, and the editor built in.
    bool hasGraphEditor() const;
    //@}

    /// The shared shader program node consumers insert into their graphs
    SoShaderProgram *getShaderNode() const;

    /// Re-materialize the object's Param_* dynamic properties as
    /// SoShaderParameter nodes on the shader objects (§6.4)
    void syncParameters();

private:
    void updateShaderNode();
    /// Report what is wrong with a MATERIALX-dialect program's
    /// document, once per distinct text (docs/CyclesIntegration.md
    /// sec 8 item 15). A document is authored somewhere else and
    /// arrives here whole, so the earliest place it can be checked is
    /// where it is first materialized -- which is document load.
    void validateDocument();
    /// The warnings validateDocument last printed, so a note that holds
    /// across parses is printed when it appears and not on each.
    std::vector<std::string> reportedWarnings;
    /// Materialize the document's declared interface as the object's
    /// `Param_*` dynamic properties, the REVERSE of the hand-declared
    /// direction everything else here takes: the document says what
    /// the knobs are, and one that is gone takes its property with it.
    /// An existing property keeps its value -- it is what the user set
    /// -- so re-reading a document is not a reset.
    void syncDocumentInterface(
            const std::vector<Render::MaterialX::MaterialInput> &inputs);
    /// Keep the object's Images property in step with what the document
    /// refers to, so a document with maps travels in the .FCStd
    /// (docs/MaterialStorage.md sec 16). Same rule as the interface
    /// above: a name the document no longer refers to is dropped, and a
    /// name already held is left alone -- its bytes are the stored ones,
    /// which is the whole point, and re-reading them off this machine's
    /// disk would undo the travelling on the machine that has them.
    void syncDocumentImages(const std::string &xml, const std::string &sourcePath);

    CoinPtr<SoShaderProgram> pcShaderProgram;
    CoinPtr<SoVertexShader> pcVertexShader;
    CoinPtr<SoFragmentShader> pcFragmentShader;
    /// Second fragment object of the program: the particle state step
    /// of a stateful emitter (docs/RenderEngine.md §5.8)
    CoinPtr<SoFragmentShader> pcSimulateShader;
    // uniform name -> parameter node, updated in place so a value edit
    // notifies without relisting the parameter field
    std::map<std::string, CoinPtr<SoShaderParameterArray1f>> paramNodes;
    // What validateDocument() last had to say about, so that a resync
    // over unchanged text is silent. Every property write on the
    // object re-materializes the whole node triple.
    std::string validatedSource;
};

using ViewProviderShaderProgramPython = ViewProviderPythonFeatureT<ViewProviderShaderProgram>;


/** View provider of App::Shader: the demo preview.
 *
 * Displays the effect on a built-in Coin primitive (Demo property) with the
 * linked programs' shared SoShaderProgram nodes inserted ahead of the shape
 * (the SoFCRenderMaterial placement rules). Scene-level ("post") programs
 * are not applied by the demo -- activating those is the ShaderBinding object's
 * job.
 */
class GuiExport ViewProviderShader : public ViewProviderDocumentObject
{
    PROPERTY_HEADER_WITH_OVERRIDE(Gui::ViewProviderShader);

public:
    ViewProviderShader();
    ~ViewProviderShader() override;

    void attach(App::DocumentObject *obj) override;
    void updateData(const App::Property *prop) override;
    std::vector<std::string> getDisplayModes() const override;
    void setDisplayMode(const char *ModeName) override;
    bool isShow() const override {return true;}

    /// Rebuild the demo subtree (also poked by program view providers on attach)
    void updateDemo();

private:
    CoinPtr<SoSeparator> pcDemoRoot;
};

using ViewProviderShaderPython = ViewProviderPythonFeatureT<ViewProviderShader>;


/** View provider of App::ShaderBinding: activates the bound shader.
 *
 * The ShaderBinding is a link group (docs/RenderDebug.md sec 6.5): the shader is
 * the first child resolving to an App::Shader, every other child a target.
 * The children render like any link group's, so a target link child also
 * shows an instance carrying the effect.
 *
 * Scope=Object (direct attachment): each target's resolved final
 * object gets the effect's material-stage program node inserted at its
 * view-provider root — captured once into the object's own render cache
 * and merged down through all child caches (link material-override
 * semantics), it applies to every instance everywhere at zero per-frame
 * cost, in every 3D view.
 *
 * Scope=Instance (per-occurrence override): each target contributes its
 * resolved object chain; every scene occurrence whose resolved chain ends
 * in a registered chain gets a per-path shader override via the view's
 * render cache manager (SoFCRenderCacheManager::addShaderOverride) — the
 * persistent-selection side channel; costs scale with occurrences.
 *
 * Scope=Element: as Instance, but a target subname ending in a face
 * element (Face3) restricts the override to that face — a partial entry
 * rendered over the untouched base draw (no suppression), biased to win
 * the depth contest. Face elements only.
 *
 * Collisions: longest chain wins, then TreeRank (higher wins), then name,
 * in a per-document registry; element-scoped bindings coexist with a
 * whole-occurrence winner (and with each other, one winner per element)
 * and draw over it. Shader-only Appearances activate the effect's
 * post-stage programs scene-wide.
 */
class GuiExport ViewProviderShaderBinding : public ViewProviderLink
{
    PROPERTY_HEADER_WITH_OVERRIDE(Gui::ViewProviderShaderBinding);

public:
    ViewProviderShaderBinding();
    ~ViewProviderShaderBinding() override;

    void attach(App::DocumentObject *obj) override;
    void finishRestoring() override;
    void beforeDelete() override;
    void updateData(const App::Property *prop) override;
    void onChanged(const App::Property *prop) override;

    /// Re-evaluate every ShaderBinding of a document (chain-length +
    /// TreeRank precedence)
    static void rebuildAllBindings(App::Document *doc);

    /// A 3D view was created for the document: per-view registrations
    /// (Scope=Instance/Element path overrides, the scene-level post
    /// list) must reach the new view's cache manager too. Schedules a
    /// coalesced rebuild; no-op for documents without Appearances.
    static void onViewCreated(App::Document *doc);

    /** @name The shared node of a card-carried MaterialX document
     *
     * A material card that carries a MaterialX document puts its manifest
     * hash on the appearance (App::MaterialAppearance::materialx,
     * docs/MaterialStorage.md sec 17). Fifty objects wearing the card
     * share ONE SoShaderProgram built from that manifest -- one document
     * read, one generation, one compile (docs/CyclesIntegration.md 6.13
     * decision 1a) -- kept here, beside the binding machinery that already
     * inserts program nodes at target roots, in a per-document registry
     * keyed by the manifest hash. The node is immutable while shared: an
     * edit means materializing real shader objects, never touching this.
     */
    //@{
    /** The shared node for \a manifestHash in \a doc, built on first use
     *
     * The manifest and every file it names must already be in the
     * document's blob store; null while any is not (a restore still
     * draining, a card whose files were never found), and the caller asks
     * again later. Counted: every acquire is owed a release.
     */
    static SoShaderProgram *acquireMaterialXNode(App::Document *doc,
                                                 const std::string &manifestHash);
    /// Give back one hold on the shared node; the last release drops it.
    static void releaseMaterialXNode(App::Document *doc, const std::string &manifestHash);
    //@}

private:
    void clearBindings();
    /// Scope=Instance/Element: register per-path overrides with every
    /// 3D view; a non-empty element restricts each override to that
    /// face of the occurrence
    void applyPathBindings(const std::vector<std::pair<App::DocumentObject*,
                                                       std::string>> &targets,
                           const std::string &element = std::string());
    /// Scope=Object: insert the effect's material program node at the
    /// resolved targets' view-provider roots
    void applyDirectBindings(const std::vector<App::DocumentObject*> &targets);
    /// (Re)build this binding's own shader node (per-binding parameter
    /// overrides baked in) from the effect's first material-stage program
    SoShaderProgram *ownProgramNode();

    // (viewer, override key) registered with that viewer's cache manager
    std::vector<std::pair<QPointer<View3DInventorViewer>, std::string>> bound;
    // (target VP root, program node) inserted for direct attachment
    std::vector<std::pair<CoinPtr<SoGroup>, CoinPtr<SoNode>>> attached;
    // this binding's own program node for direct attachment
    CoinPtr<SoShaderProgram> pcOwnProgram;
    CoinPtr<SoVertexShader> pcOwnVertexShader;
    CoinPtr<SoFragmentShader> pcOwnFragmentShader;
    CoinPtr<SoFragmentShader> pcOwnSimulateShader;
    std::map<std::string, CoinPtr<SoShaderParameterArray1f>> ownParamNodes;
};

using ViewProviderShaderBindingPython = ViewProviderPythonFeatureT<ViewProviderShaderBinding>;

} // namespace Gui

#endif // GUI_ViewProviderShaderObject_H
