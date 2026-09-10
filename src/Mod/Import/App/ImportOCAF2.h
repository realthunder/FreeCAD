/****************************************************************************
 *   Copyright (c) 2018 Zheng Lei (realthunder) <realthunder.dev@gmail.com> *
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

#ifndef IMPORT_IMPORTOCAF2_H
#define IMPORT_IMPORTOCAF2_H

#include <climits>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <TDocStd_Document.hxx>
#include <TopoDS_Shape.hxx>
#include <XCAFDoc_ColorTool.hxx>
#include <XCAFDoc_ShapeTool.hxx>
#include <XCAFDoc_VisMaterial.hxx>
#include <XCAFDoc_VisMaterialTool.hxx>

#include <App/MaterialAppearance.h>
#include <Base/Placement.h>
#include <Base/Sequencer.h>
#include <Mod/Part/App/TopoShape.h>

#include "ExportOCAF.h"
#include "ImportOCAF.h"
#include "ReaderStep.h"
#include "RenderMaterial.h"
#include "Tools.h"


class TDF_Label;
class TopLoc_Location;

namespace App
{
class Document;
class DocumentObject;
}  // namespace App
namespace Part
{
class Feature;
}

namespace Import
{

struct ImportExport ImportOCAFOptions
{
    ImportOCAFOptions();
    App::Color defaultFaceColor;
    App::Color defaultEdgeColor;
    bool merge = false;
    bool useLinkGroup = false;
    bool useBaseName = true;
    bool importHidden = true;
    bool reduceObjects = false;
    bool showProgress = false;
    bool useLegacyImporter = false;
    int mode = 0;
};

class ImportExport ImportOCAF2
{
public:
    ImportOCAF2(Handle(TDocStd_Document) h, App::Document* d, const std::string& name);
    virtual ~ImportOCAF2();
    ImportOCAF2(const ImportOCAF2 &) = delete;
    ImportOCAF2 & operator=(const ImportOCAF2 &) = delete;
    App::DocumentObject* loadShapes();

    /** @name Progressive (two-stage) import
     *
     * analyze() walks the XCAF document into an ordered list of create
     * ops without touching the App document, so it can run on a worker
     * thread while the GUI pumps. The op order is skeleton-first:
     * containers come before their members and link targets before the
     * links, so every applied object lands at its correct global
     * placement immediately. applyNextOp()/finishOps() then materialize
     * the ops on the GUI thread, one call per op, letting the caller
     * interleave event processing between batches.
     *
     * analyze() returns false when this import cannot run progressively
     * (multi-document modes, merge, the legacy importer, or per-face
     * visualization materials that would split an object); the caller
     * falls back to loadShapes().
     */
    //@{
    bool analyze();
    std::size_t opCount() const
    {
        return myProgOps.size();
    }
    std::size_t opsApplied() const
    {
        return myProgApplied;
    }
    /// Apply the next published op. Returns false when none are pending.
    bool applyNextOp();
    /// Terminal pass after the ops (or a canceled prefix of them) have
    /// been applied: recompute and return the root object.
    App::DocumentObject* finishOps();
    //@}

    /** @name Streamed analysis
     * The batched form of analyze() for streamed transfers: begin() checks
     * eligibility and reserves the root container, analyzeRoots() consumes
     * whatever free shapes the transferred batches have added so far, and
     * analyzeEnd() takes the final single-root decisions. Sealed ops become
     * visible to the GUI thread in batches ("published"); the GUI may apply
     * them (applyNextOp) while the worker keeps transferring, but nothing is
     * published before a second root confirms the root group is final.
     */
    //@{
    bool analyzeBegin();
    bool analyzeRoots();
    bool analyzeEnd();
    /** @name Component streaming
     * When the components of a single root are transferred ahead of the root
     * itself they appear as free shapes and analyzeRoots() picks them up as
     * root children. setComponentStreaming() remembers each such instance so
     * that the root's own analysis - announced by expectRootAssembly() before
     * the root is transferred - recognizes them instead of emitting them a
     * second time, and adopts the reserved root container as the assembly's
     * group rather than nesting one inside the other.
     *
     * Components deeper than the top level need their container to exist
     * before they arrive, because a claim can only add an object to a group,
     * never move it out of one. beginSkeleton() therefore reserves a
     * container per assembly of the tree - read from the product structure,
     * so before any geometry exists - and addStreamedShapes() says which node
     * each transferred occurrence became: a component goes into the container
     * of its owner, an assembly is the container itself and the root pass
     * analyzes it into that one instead of nesting another.
     */
    //@{
    void setComponentStreaming(bool enable)
    {
        myStreamComponents = enable;
    }
    void expectRootAssembly()
    {
        myRootAssemblyPending = true;
    }
    bool beginSkeleton(const std::vector<AssemblyNode>& nodes);
    void addStreamedShapes(const std::vector<std::pair<TopoDS_Shape, int>>& shapes);
    //@}
    /// Number of ops the GUI thread may apply (mutex-guarded).
    std::size_t opsPublished() const;
    /// Delete every object created by applied ops (falling back or aborting
    /// mid-stream). GUI thread only.
    void rollbackOps();
    //@}

    static ImportOCAFOptions customImportOptions();
    void setImportOptions(ImportOCAFOptions opts);
    void setMerge(bool enable)
    {
        options.merge = enable;
    }
    void setUseLinkGroup(bool enable);
    void setBaseName(bool enable)
    {
        options.useBaseName = enable;
    }
    void setImportHiddenObject(bool enable)
    {
        options.importHidden = enable;
    }
    void setReduceObjects(bool enable)
    {
        options.reduceObjects = enable;
    }
    void setShowProgress(bool enable)
    {
        options.showProgress = enable;
    }
    void setUseLegacyImporter(bool enable)
    {
        options.useLegacyImporter = enable;
    }

    enum ImportMode
    {
        SingleDoc = 0,
        GroupPerDoc = 1,
        GroupPerDir = 2,
        ObjectPerDoc = 3,
        ObjectPerDir = 4,
        ModeMax,
    };
    void setMode(int m);
    int getMode() const
    {
        return options.mode;
    }

private:
    struct Info
    {
        std::string baseName;
        App::DocumentObject* obj = nullptr;
        App::PropertyPlacement* propPlacement = nullptr;
        App::Color faceColor;
        App::Color edgeColor;
        bool hasFaceColor = false;
        bool hasEdgeColor = false;
        int free = true;
    };

    struct ColorInfo;

    /// One deferred object-creation step of a progressive import.
    /// Ops reference each other by index into myProgOps; the index
    /// order is the apply order.
    struct ProgOp
    {
        enum Type
        {
            Object,     ///< Part::Feature from a base shape
            Group,      ///< App::LinkGroup / App::Part container
            Link,       ///< App::Link to an earlier op's object
            LinkArray,  ///< App::Link with ElementCount instances
            Collapsed,  ///< reduced single-child group: aliases resolveTo
            Dropped,    ///< analysis discarded it; skipped at apply
            Claim,      ///< first-use claim of an already applied op's
                        ///< object: relabel/re-place/reparent it
        };
        Type type = Dropped;
        int parent = -1;     ///< op index of the owning Group (-1 = root)
        int target = -1;     ///< linked op for Link/LinkArray
        int resolveTo = -1;  ///< replacement child for Collapsed
        bool visible = true;
        bool free = true;    ///< first-use claim still available
        bool collapsedRep = false;  ///< a collapsed group resolves to me
        std::string label;
        std::string internalName;  ///< addObject() base name
        Base::Placement placement;
        // Object
        TopoDS_Shape shape;  ///< base shape (location-free)
        App::Color faceColor;
        App::Color edgeColor;
        bool hasFaceColor = false;
        bool hasEdgeColor = false;
        std::vector<App::Color> faceColors;
        std::vector<App::Color> edgeColors;
        std::vector<App::MaterialAppearance> faceMaterials;
        /// faceMaterials carry raw PBR slots; see ColorInfo::pbrMaterials
        bool pbrMaterials = false;
        RenderMaterial material;
        // Group
        App::Color groupColor;
        bool hasGroupColor = false;
        // Link
        App::Color linkColor;
        bool hasLinkColor = false;
        // LinkArray
        std::vector<Base::Placement> placements;
        boost::dynamic_bitset<> visList;
        std::map<int, App::Color> elemColors;
    };

    struct MaterialGroups;
    void scanElementColors(TDF_Label label,
                           ColorInfo& colors,
                           Info& info,
                           bool& hasFaceColors,
                           bool& hasEdgeColors);
    void scanMaterialGroups(TDF_Label label, Part::TopoShape& tshape, MaterialGroups& groups);
    /// Resolve each face label's visualization material into a whole
    /// App::MaterialAppearance entry (diffuse from the already-resolved face
    /// colours). Fills colors.faceMaterials and returns true only when a
    /// field other than diffuse varies from face to face -- the case a
    /// colour list cannot carry.
    bool scanFaceMaterials(TDF_Label label, ColorInfo& colors, const Info& info);

    int newProgOp(ProgOp::Type type);
    int resolveOp(int node) const;
    int analyzeShape(TDF_Label label,
                     const TopoDS_Shape& shape,
                     int parent,
                     bool visible,
                     bool baseOnly = false);
    int analyzeObject(TDF_Label label, const TopoDS_Shape& shape);
    /// @param groupIdx existing group op to fill (-1 = create one); an
    /// existing one may already be published, so its own record is left
    /// alone and label/color arrive through a Claim op instead.
    int analyzeAssembly(TDF_Label label, const TopoDS_Shape& shape, int groupIdx = -1);
    /// Analyze the streamed root's assembly into the reserved root container.
    bool analyzeRootAssembly(TDF_Label label);
    /// Analyze a streamed sub-assembly into the container reserved for it.
    bool adoptSkeleton(TDF_Label label, const TopoDS_Shape& shape, int group);
    /// The node a translated assembly stands for, told by the components
    /// streamed into it; 0 when none of them is recognized.
    int skeletonNodeOf(const TopoDS_Shape& shape) const;
    /// Bring an applied container to the given placement, label and color.
    void claimGroup(int group,
                    const Base::Placement& placement,
                    const std::string& label);
    bool hasSHUOColors(TDF_Label label);
    void applyOp(ProgOp& op, int index);
    App::DocumentObject* progObject(int node) const;

    App::DocumentObject* loadShape(App::Document* doc,
                                   TDF_Label label,
                                   const TopoDS_Shape& shape,
                                   bool baseOnly = false,
                                   bool newDoc = true);
    App::Document* getDocument(App::Document* doc, TDF_Label label);
    bool createAssembly(App::Document* doc,
                        TDF_Label label,
                        const TopoDS_Shape& shape,
                        Info& info,
                        bool newDoc);
    bool createObject(App::Document* doc,
                      TDF_Label label,
                      const TopoDS_Shape& shape,
                      Info& info,
                      bool newDoc);
    bool createGroup(App::Document* doc,
                     Info& info,
                     const TopoDS_Shape& shape,
                     std::vector<App::DocumentObject*>& children,
                     const boost::dynamic_bitset<>& visibilities,
                     bool canReduce = false);
    bool
    getColor(const TopoDS_Shape& shape, Info& info, bool check = false, bool noDefault = false);
    void
    getSHUOColors(TDF_Label label, std::map<std::string, App::Color>& colors, bool appendFirst);
    void setObjectName(Info &info, TDF_Label label, bool checkExistingName=false);
    std::string getLabelName(TDF_Label label);

    virtual void applyEdgeColors(Part::Feature*, const std::vector<App::Color>&)
    {}
    virtual void applyFaceColors(Part::Feature*, const std::vector<App::Color>&)
    {}
    /// Per-face whole materials (glTF visualization materials whose
    /// fields beyond diffuse vary across faces); the Gui importer puts
    /// them into the view provider's ShapeAppearance.
    virtual void applyFaceMaterials(Part::Feature*, const std::vector<App::MaterialAppearance>&, bool /*pbr*/)
    {}
    virtual void applyElementColors(App::DocumentObject*, const std::map<std::string, App::Color>&)
    {}
    virtual void applyLinkColor(App::DocumentObject*, int /*index*/, App::Color)
    {}
    /// Per-object render (PBR) material resolved from XCAFDoc_VisMaterial
    /// (glTF import); the Gui importer mirrors it into the view provider's
    /// Render_* dynamic properties.
    virtual void applyRenderMaterial(Part::Feature*, const RenderMaterial&)
    {}

    /// Resolve the label's XCAFDoc_VisMaterial (checking the sub shape
    /// labels when the label itself carries none) into a neutral
    /// RenderMaterial, extracting embedded texture images to temporary
    /// files. Returns false when there is no PBR material.
    bool getRenderMaterial(TDF_Label label, RenderMaterial& mat);
    /// Convert an already resolved visualization material into a neutral
    /// RenderMaterial (texture extraction included). Returns false when
    /// the material is the color-only default.
    bool extractRenderMaterial(const Handle(XCAFDoc_VisMaterial)& visMat,
                               RenderMaterial& mat);

private:
    class ImportLegacy: public ImportOCAF
    {
    public:
        explicit ImportLegacy(ImportOCAF2& parent)
            : ImportOCAF(parent.pDoc, parent.pDocument, parent.default_name)
            , myParent(parent)
        {}

    private:
        void applyColors(Part::Feature* part, const std::vector<App::Color>& colors) override
        {
            myParent.applyFaceColors(part, colors);
        }

        ImportOCAF2& myParent;
    };
    friend class ImportLegacy;

    Handle(TDocStd_Document) pDoc;
    App::Document* pDocument;
    Handle(XCAFDoc_ShapeTool) aShapeTool;
    Handle(XCAFDoc_ColorTool) aColorTool;
    Handle(XCAFDoc_VisMaterialTool) aMaterialTool;
    std::string default_name;

    ImportOCAFOptions options;
    std::string filePath;

    std::unordered_map<TopoDS_Shape, Info, ShapeHasher> myShapes;
    std::unordered_map<TDF_Label, std::string, LabelHasher> myNames;
    std::unordered_map<App::DocumentObject*, App::PropertyPlacement*> myCollapsedObjects;

    struct DocumentInfo
    {
        App::Document *doc;
        std::vector<App::DocumentObject*> &children;
        DocumentInfo(App::Document *d, std::vector<App::DocumentObject*> &objs)
            :doc(d), children(objs)
        {}
    };
    std::vector<DocumentInfo> myDocumentStack;
    std::vector<App::Document*> myNewDocuments;

    // Progressive import state.
    // Streaming contract: ops with index < myProgPublished are sealed - the
    // worker neither writes their fields nor depends on them changing, and
    // the GUI thread copies them (under myProgMutex, which also serializes
    // the vector reallocation in newProgOp) before applying. myProgObjs is
    // GUI-thread-only and grown lazily to the published size.
    std::vector<ProgOp> myProgOps;
    std::vector<App::DocumentObject*> myProgObjs;
    std::size_t myProgApplied = 0;
    int myProgRoot = -1;
    bool myProgFailed = false;
    std::unordered_map<TopoDS_Shape, int, ShapeHasher> myShapeNodes;
    std::unordered_map<TDF_Label, std::pair<int, int>, LabelHasher> myLabelNodes;

    // Streamed analysis state (worker side unless noted)
    mutable std::mutex myProgMutex;
    std::size_t myProgPublished = 0;  ///< guarded by myProgMutex
    bool myProgStreaming = false;
    int myRootGroup = -1;
    int myRootChildren = 0;
    int myLastRootChild = -1;
    std::unordered_map<TDF_Label, bool, LabelHasher> myAnalyzedRoots;
    std::unordered_set<int> myClaimedSealed;  ///< sealed ops claimed via Claim op
    bool myStreamComponents = false;
    bool myRootAssemblyPending = false;
    /// located shape of a streamed component instance -> its op
    std::unordered_map<TopoDS_Shape, int, ShapeHasher> myInstanceNodes;
    /// the assembly tree the containers were reserved from (1-based nodes)
    std::vector<AssemblyNode> mySkeleton;
    /// node -> its container op, [0] being the reserved root container
    std::vector<int> myNodeGroups;
    /// located shape of a streamed component -> its node
    std::unordered_map<TopoDS_Shape, int, ShapeHasher> myStreamedNodes;
    /// nodes whose reserved container the root pass has taken over
    std::unordered_set<int> myAdoptedNodes;

    void publishOps();
    /// free-flag view that folds in claims of sealed ops
    bool opFree(int node) const
    {
        return myProgOps[node].free && myClaimedSealed.find(node) == myClaimedSealed.end();
    }

    Base::SequencerLauncher* sequencer {nullptr};
};

class ImportExport ImportOCAFExt: public ImportOCAF2
{
public:
    ImportOCAFExt(Handle(TDocStd_Document) hStdDoc, App::Document* doc, const std::string& name);

    std::map<Part::Feature*, std::vector<App::Color>> partColors;

private:
    void applyFaceColors(Part::Feature* part, const std::vector<App::Color>& colors) override;
};

}  // namespace Import

#endif  // IMPORT_IMPORTOCAF2_H
