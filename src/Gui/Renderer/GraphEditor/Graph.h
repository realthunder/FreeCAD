//
// Copyright Contributors to the MaterialX Project
// SPDX-License-Identifier: Apache-2.0
//
// Ported into FreeCAD's shader graph editor (docs/ShaderGraphEditor.md
// sec 4): the MaterialX viewer, file dialogs and library loading are
// replaced by a GraphHost and a string document round trip. Diff
// against MaterialX/source/MaterialXGraphEditor/Graph.h.

#ifndef MATERIALX_GRAPH_H
#define MATERIALX_GRAPH_H

#include "GraphHost.h"
#include "Layout.h"
#include "UiNode.h"

#include <MaterialXCore/Document.h>
#include <MaterialXFormat/File.h>
#include "UiProperties.h"

#include <imgui_node_editor.h>

#include <functional>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace Render::GraphEditor {

namespace ed = ax::NodeEditor;
namespace mx = ::MaterialX;

class MenuItem
{
  public:
    MenuItem(const std::string& name, const std::string& type, const std::string& category, const std::string& group, const std::set<std::string>& inputTypes, const std::set<std::string>& outputTypes) :
        name(name), type(type), category(category), group(group), inputTypes(inputTypes), outputTypes(outputTypes) { }

    // getters
    std::string getName() const { return name; }
    std::string getType() const { return type; }
    std::string getCategory() const { return category; }
    std::string getGroup() const { return group; }
    const std::set<std::string>& getInputTypes() const { return inputTypes; }
    const std::set<std::string>& getOutputTypes() const { return outputTypes; }

    // setters
    void setName(const std::string& newName) { this->name = newName; }
    void setType(const std::string& newType) { this->type = newType; }
    void setCategory(const std::string& newCategory) { this->category = newCategory; }
    void setGroup(const std::string& newGroup) { this->group = newGroup; }
    void setInputTypes(const std::set<std::string>& newInputTypes) { this->inputTypes = newInputTypes; }
    void setOutputTypes(const std::set<std::string>& newOutputTypes) { this->outputTypes = newOutputTypes; }

  private:
    std::string name;
    std::string type;
    std::string category;
    std::string group;
    std::set<std::string> inputTypes;
    std::set<std::string> outputTypes;
};

// A link connects two pins and includes a unique id and the ids of the two pins it connects
// Based on the Link struct from ImGui Node Editor blueprints-examples.cpp
struct Link
{
    Link(int id, int startAttr, int endAttr) :
        _id(id),
        _startAttr(startAttr),
        _endAttr(endAttr)
    {
    }

    int _id;
    int _startAttr, _endAttr;
};

// The UI state associated with a graph level (document or nodegraph).
struct GraphState
{
    // Display name for this graph level.
    std::string name;

    // MaterialX graph element for this level.
    mx::GraphElementPtr graphElem;
    bool isCompoundNodeGraph = false;

    // UI nodes and pins within this graph.
    std::vector<UiNodePtr> nodes;
    std::vector<UiPinPtr> pins;

    // Links and edges representing connections within this graph.
    std::vector<Link> links;
    std::vector<UiEdge> edges;

    // Counter for generating unique UI element IDs.
    int nextUiId = 1;
};

class Graph
{
  public:
    // The host answers what the MaterialX viewer used to; it outlives
    // the graph. The node list comes from the shared data library.
    explicit Graph(GraphHost* host);
    ~Graph() = default;

    /// Swap the host; the new one is told of the current document.
    void setHost(GraphHost* host);

    // Replace the document with one parsed from MaterialX XML. False,
    // with why in `error`, when the text does not parse; the previous
    // document stays. Needs the node editor current.
    bool setDocument(const std::string& xml, std::string& error);

    // The document as MaterialX XML, node positions included.
    std::string documentText() const;

    // The nodegraph a double-click on `node` opens -- a compound
    // nodegraph's own, or a functional node's implementation -- with
    // `compound` saying which; null for a node without one.
    mx::NodeGraphPtr diveTarget(const UiNodePtr& node, bool& compound) const;

    // Leave the current level for `target`, reached through `node` (the
    // double-click's body). `announce` shows the read-only popup a
    // library graph gets on a first visit; a re-entry after a reload
    // does not repeat it.
    void enterNodeGraph(const UiNodePtr& node, const mx::NodeGraphPtr& target, bool compound,
                        bool announce);

    // One step of the re-descent setDocument() queued, once the level
    // it is at has its layout.
    void restoreNavigation();

    // The name shown at the root of the graph path.
    void setName(const std::string& name);

    void drawGraph(ImVec2 mousePos);

    // Whether a completed gesture changed the document since the last
    // call -- a link, a node, a value, a rename, a move -- and the
    // document's text if so. Never true while a drag or a text field
    // is live, so a drag coalesces into one change. Clears the flag.
    bool takeChange(std::string& xml);

    void setFontScale(float val)
    {
        _fontScale = val;
    }

  private:
    // Generate node UI from nodedefs
    void createNodeUIList(mx::ConstDocumentPtr doc);

    // Build UiNode nodegraph upon loading a document
    void buildUiBaseGraph(mx::DocumentPtr doc);

    // Build UiNode node graph upon diving into a nodegraph node
    void buildUiNodeGraph(const mx::NodeGraphPtr& nodeGraphs);

    // Based on the comment node in the ImGui Node Editor blueprints-example.cpp.
    void buildGroupNode(UiNodePtr node);

    // Connect links via connected nodes in UiNodePtr
    void linkGraph();

    // Connect all links via the graph editor library
    void connectLinks();

    // Find link position in current links vector from link id
    int findLinkPosition(int id);

    // Check if link exists in the current link vector
    bool linkExists(const Link& newLink);

    // Check if link can be added. Show a diagnostic message as the label.
    bool checkCanAddLink(ed::PinId startPinId, ed::PinId endPinId);

    // Add link to nodegraph and set up connections between UiNodes and
    // MaterialX Nodes to update shader
    // startPinId - where the link was initiated
    // endPinId - where the link was ended
    void addLink(ed::PinId startPinId, ed::PinId endPinId);

    // Delete link from current link vector and remove any connections in
    // UiNode or MaterialX Nodes to update shader
    void deleteLink(ed::LinkId deletedLinkId);

    void deleteLinkInfo(int startAtrr, int endAttr);

    // Apply the layout engine to position all nodes.
    void applyLayout(const std::vector<int>& outputNodeIndices);

    // Return pin color based on the type of the value of that pin
    void setPinColor();

    // Based on the pin icon function in the ImGui Node Editor blueprints-example.cpp
    void drawPinIcon(const std::string& type, bool connected, int alpha);

    UiPinPtr getPin(ed::PinId id);
    void drawInputPin(UiPinPtr pin);

    // Return output pin needed to link the inputs and outputs
    ed::PinId getOutputPin(UiNodePtr node, UiNodePtr inputNode, UiPinPtr input);

    void drawOutputPins(UiNodePtr node, const std::string& longestInputLabel);

    // Create pins for outputs/inputs added while inside the node graph
    void addNodeGraphPins();

    std::vector<int> createNodes(bool nodegraph);
    int getNodeId(ed::PinId pinId);

    // Find node location in graph nodes vector from node id
    int findNode(int nodeId);

    // Return node position in current state's nodes from node name and type to
    // account for input/output UiNodes with same names as MaterialX nodes
    int findNode(const std::string& name, const std::string& type);

    // Return the node position of the upstream connection from the given input.
    int findUpstreamNode(mx::InputPtr input);

    // Add node to graphNodes based on nodedef information
    void addNode(const std::string& category, const std::string& name, const std::string& type);

    void deleteNode(UiNodePtr node);

    // Build the initial graph of a loaded document including shader, material and nodegraph node
    void setUiNodeInfo(UiNodePtr node, const std::string& type, const std::string& category);

    // Check if edge exists in edge vector
    bool edgeExists(const UiEdge& edge);

    // Create an edge between two nodes if it doesn't already exist.
    // Returns true if the edge was created, false if invalid or already exists.
    bool createEdge(UiNodePtr upNode, UiNodePtr downNode, mx::InputPtr connectingInput);

    // Create an edge from an output element to its connected upstream node.
    void createEdgeForOutput(mx::OutputPtr output);

    // Remove node edge based on connecting input
    void removeEdge(int downNode, int upNode, UiPinPtr pin);

    // Set position attributes for nodes which changed position
    void savePosition();

    // Restore node positions from MaterialX element attributes.
    void restorePositions();

    // Add an input to a node based on its NodeDef input definition.
    mx::InputPtr addNodeInput(UiNodePtr node, mx::InputPtr nodeDefInput);

    // Traversal methods
    void upNodeGraph();
    UiNodePtr traverseConnection(UiNodePtr node, bool traverseDownstream);

    // Show input values in property editor for a given input
    void showPropertyEditorValue(UiNodePtr node, mx::InputPtr input, const UIProperties& uiProperties);
    // Show input connections in property editor for a given node
    void showPropertyEditorOutputConnections(UiNodePtr node);
    // Show output connections in property editor for a given output pin
    void showPropertyEditorInputConnection(UiPinPtr pin);
    // Show property editor for a given node
    void propertyEditor();
    void setDefaults(mx::InputPtr input);

    // Setup UI information for add node popup
    void addExtraNodes();

    void copyInputs();

    // Set position of pasted nodes based on original node position
    void positionPasteBin(ImVec2 pos);

    void copyNodeGraph(UiNodePtr origGraph, UiNodePtr copyGraph);
    void copyUiNode(UiNodePtr node);

    void graphButtons();

    void addNodePopup(bool cursor);
    void searchNodePopup(bool cursor);
    bool isPinHovered();
    void addPinPopup();
    bool readOnly();
    void readOnlyPopup();

    // Compiling shaders message
    void shaderPopup();

    void updateMaterials(mx::InputPtr input = nullptr, mx::ValuePtr value = nullptr);

    // A topology change: the host hears of it two frames on, once the
    // editor has settled, as the MaterialX editor did.
    void scheduleUpdate();

    // Allow for camera manipulation of render view window
    void handleRenderViewInputs();

    // Set the node to display in render view based on selected node or nodegraph
    void setRenderMaterial(UiNodePtr node);

    // Initialize the graph state from the current document.
    void initializeGraph();

    void showHelp() const;

  private:
    GraphHost* _host;

    // The document and its display name.
    mx::DocumentPtr _graphDoc;
    std::string _documentName;

    // The text last handed out through takeChange() (or loaded); a
    // completed gesture compares against it.
    std::string _committedText;
    bool _commitReady;
    bool _updatePending;

    // Auxiliary node information.
    std::unordered_map<UiNodePtr, std::vector<UiPinPtr>> _downstreamInputs;
    std::unordered_map<std::string, ImColor> _pinColor;

    // Current graph state, including nodes, pins, and navigation context.
    GraphState _state;

    // current nodes and nodegraphs
    UiNodePtr _currUiNode;
    UiNodePtr _prevUiNode;
    UiNodePtr _currRenderNode;

    // for adding new nodes
    std::vector<MenuItem> _nodesToAdd;

    // Saved states of parent graphs for navigating the graph hierarchy.
    std::vector<GraphState> _parentStates;

    // Where the user was when setDocument() replaced the document under
    // them (an undo, a Surface pick, a write from Python): the nodegraph
    // levels below the root, top down, and the selected node's name.
    // Re-entered one level per settled layout by restoreNavigation(),
    // then the view is left where it was instead of framing the content.
    struct RestoreLevel
    {
        std::string path;
        bool compound = false;
    };
    std::vector<RestoreLevel> _restoreLevels;
    std::string _restoreSelection;
    bool _keepView = false;

    // map for copied nodes
    std::map<UiNodePtr, UiNodePtr> _copiedNodes;

    bool _needsLayout;
    bool _layoutPending;
    bool _needsNavigation;
    bool _delete;

    // popup up variables
    bool _popup;
    bool _shaderPopup;
    int _searchNodeId;
    bool _addNewNode;
    bool _ctrlClick;
    bool _isCut;
    // auto layout button clicked
    bool _autoLayout;

    // used when updating materials
    int _frameCount;
    // used for filtering pins when connecting links
    std::string _pinFilterType;
    // used for filtering pins when adding a node from a link
    std::string _menuFilterType;
    // used for auto connecting pins if a node is added by drawing a link from a pin
    ed::PinId _pinIdToLinkFrom;
    ed::PinId _pinIdToLinkTo;

    // DPI scaling for fonts
    float _fontScale;

    // Layout engine
    Layout _layout;

    // Thumbnail size in the property panel
    float _previewSize;
};

} // namespace Render::GraphEditor

#endif
