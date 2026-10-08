/****************************************************************************
 *   Copyright (c) 2026 Zheng Lei (realthunder) <realthunder.dev@gmail.com> *
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

#include "PreCompiled.h"

/*[[[cog
import MiscParams
MiscParams.define()
]]]*/

// Auto generated code (Tools/params_utils.py:198)
#include <unordered_map>
#include <App/Application.h>
#include <App/DynamicProperty.h>
#include <App/ParamRegistry.h>
#include "MiscParams.h"
using namespace Gui;

// Auto generated code (Tools/params_utils.py:210)
namespace {
class MiscParamsP: public ParameterGrp::ObserverType {
public:
    ParameterGrp::handle handle;
    std::unordered_map<const char *,void(*)(MiscParamsP*),App::CStringHasher,App::CStringHasher> funcs;
    std::vector<ParameterGrp::handle> subHandles;
    long RecentMacros;
    long ShortcutCount;
    std::string ShortcutModifiers;
    long CoarseLinearSnapMultiplier;
    long CoarseRotationSnapMultiplier;
    unsigned long CacheLimit;
    long CachePeriod;
    long ShortcutTimeout;
    bool ShowTabBar;
    bool TabBarShowText;
    long TabBarMaxLength;
    bool DisableDpiScaling;
    bool UseSoftwareOpenGL;
    bool Unflatten;
    bool GeoFeatureSubgraphs;
    bool EnableGizmos;
    bool DelayedGizmoUpdate;
    bool EnableCoarseSnap;
    long FineSnapModifier;
    long DefaultCoarseDragBehavior;
    bool PropertyViewAutoTransactionView;
    bool PropertyViewAutoTransactionData;
    bool PropertyViewAutoExpandView;
    bool PropertyViewAutoExpandData;
    bool PropertyViewHideHeader;
    long PropertyViewViewSectionSize;
    long PropertyViewDataSectionSize;
    long PropertyViewLastTabIndex;
    std::string NewPropertyType;
    std::string NewPropertyGroup;
    bool NewPropertyAppend;
    std::string PanelMirror;
    long PanelPollMs;
    bool AutoShowSelectionView;
    bool SingleClickFeatureSelect;
    long DAGViewSelectionMode;
    long DAGViewFontPointSize;
    double DAGViewDirection;
    double CustomViewQ0;
    double CustomViewQ1;
    double CustomViewQ2;
    double CustomViewQ3;
    long RecentFiles;
    std::string DonatePage;
    std::string Graphviz;
    std::string IconThemeName;
    std::string IconThemeSearchPath;
    bool IconThemeSearchPaths;
    bool TreeViewEnabled;
    bool PropertyViewEnabled;
    bool ComboViewEnabled;
    bool TaskWatcherEnabled;
    bool DAGViewEnabled;
    long ComboViewTreeViewSize;
    long ComboViewPropertyViewSize;
    long PlacementRotationMethod;
    long SharePort;
    std::string ShareDoor;
    std::string ShareExternalHost;
    std::string ShareViewerPage;
    bool ShareTrustProxy;
    bool ShareDoorsSeeded;
    bool ShareGrantsMigrated;
    double DraggerLastTranslationIncrement;
    double DraggerLastRotationIncrement;
    bool ActivateOverlay;
    long DockCursorMargin;
    bool DockStdTreeView;
    bool DockStdPropertyView;
    std::string OverlayLeftWidgets;
    long OverlayLeftWidth;
    long OverlayLeftHeight;
    long OverlayLeftOffset1;
    long OverlayLeftOffset3;
    long OverlayLeftOffset2;
    std::string OverlayLeftSizes;
    bool OverlayLeftAutoHide;
    bool OverlayLeftEditHide;
    bool OverlayLeftEditShow;
    bool OverlayLeftTaskShow;
    bool OverlayLeftClosed;
    bool OverlayLeftTransparent;
    std::string OverlayRightWidgets;
    long OverlayRightWidth;
    long OverlayRightHeight;
    long OverlayRightOffset1;
    long OverlayRightOffset3;
    long OverlayRightOffset2;
    std::string OverlayRightSizes;
    bool OverlayRightAutoHide;
    bool OverlayRightEditHide;
    bool OverlayRightEditShow;
    bool OverlayRightTaskShow;
    bool OverlayRightClosed;
    bool OverlayRightTransparent;
    std::string OverlayTopWidgets;
    long OverlayTopWidth;
    long OverlayTopHeight;
    long OverlayTopOffset1;
    long OverlayTopOffset3;
    long OverlayTopOffset2;
    std::string OverlayTopSizes;
    bool OverlayTopAutoHide;
    bool OverlayTopEditHide;
    bool OverlayTopEditShow;
    bool OverlayTopTaskShow;
    bool OverlayTopClosed;
    bool OverlayTopTransparent;
    std::string OverlayBottomWidgets;
    long OverlayBottomWidth;
    long OverlayBottomHeight;
    long OverlayBottomOffset1;
    long OverlayBottomOffset3;
    long OverlayBottomOffset2;
    std::string OverlayBottomSizes;
    bool OverlayBottomAutoHide;
    bool OverlayBottomEditHide;
    bool OverlayBottomEditShow;
    bool OverlayBottomTaskShow;
    bool OverlayBottomClosed;
    bool OverlayBottomTransparent;

    // Auto generated code (Tools/params_utils.py:254)
    MiscParamsP() {
        handle = App::GetApplication().GetParameterGroupByPath("User parameter:BaseApp/Preferences");
        handle->Attach(this);

        subHandles.resize(30);
        subHandles[0] = handle->GetGroup("RecentMacros");
        subHandles[0]->Attach(this);
        subHandles[1] = handle->GetGroup("Gui/Gizmos");
        subHandles[1]->Attach(this);
        subHandles[2] = handle->GetGroup("CacheDirectory");
        subHandles[2]->Attach(this);
        subHandles[3] = handle->GetGroup("Shortcut/Settings");
        subHandles[3]->Attach(this);
        subHandles[4] = handle->GetGroup("Workbenches");
        subHandles[4]->Attach(this);
        subHandles[5] = handle->GetGroup("HighDPI");
        subHandles[5]->Attach(this);
        subHandles[6] = handle->GetGroup("OpenGL");
        subHandles[6]->Attach(this);
        subHandles[7] = handle->GetGroup("DependencyGraph");
        subHandles[7]->Attach(this);
        subHandles[8] = handle->GetGroup("PropertyView");
        subHandles[8]->Attach(this);
        subHandles[9] = handle->GetGroup("Fw");
        subHandles[9]->Attach(this);
        subHandles[10] = handle->GetGroup("Selection");
        subHandles[10]->Attach(this);
        subHandles[11] = handle->GetGroup("DAGView");
        subHandles[11]->Attach(this);
        subHandles[12] = handle->GetGroup("View/Custom");
        subHandles[12]->Attach(this);
        subHandles[13] = handle->GetGroup("RecentFiles");
        subHandles[13]->Attach(this);
        subHandles[14] = handle->GetGroup("Websites");
        subHandles[14]->Attach(this);
        subHandles[15] = handle->GetGroup("Paths");
        subHandles[15]->Attach(this);
        subHandles[16] = handle->GetGroup("Bitmaps/Theme");
        subHandles[16]->Attach(this);
        subHandles[17] = handle->GetGroup("DockWindows/TreeView");
        subHandles[17]->Attach(this);
        subHandles[18] = handle->GetGroup("DockWindows/PropertyView");
        subHandles[18]->Attach(this);
        subHandles[19] = handle->GetGroup("DockWindows/ComboView");
        subHandles[19]->Attach(this);
        subHandles[20] = handle->GetGroup("DockWindows/TaskWatcher");
        subHandles[20]->Attach(this);
        subHandles[21] = handle->GetGroup("DockWindows/DAGView");
        subHandles[21]->Attach(this);
        subHandles[22] = handle->GetGroup("Placement");
        subHandles[22]->Attach(this);
        subHandles[23] = handle->GetGroup("SceneShare");
        subHandles[23]->Attach(this);
        subHandles[24] = App::GetApplication().GetParameterGroupByPath("User parameter:BaseApp/History/Dragger");
        subHandles[24]->Attach(this);
        subHandles[25] = App::GetApplication().GetParameterGroupByPath("User parameter:BaseApp/MainWindow/DockWindows");
        subHandles[25]->Attach(this);
        subHandles[26] = App::GetApplication().GetParameterGroupByPath("User parameter:BaseApp/MainWindow/DockWindows/OverlayLeft");
        subHandles[26]->Attach(this);
        subHandles[27] = App::GetApplication().GetParameterGroupByPath("User parameter:BaseApp/MainWindow/DockWindows/OverlayRight");
        subHandles[27]->Attach(this);
        subHandles[28] = App::GetApplication().GetParameterGroupByPath("User parameter:BaseApp/MainWindow/DockWindows/OverlayTop");
        subHandles[28]->Attach(this);
        subHandles[29] = App::GetApplication().GetParameterGroupByPath("User parameter:BaseApp/MainWindow/DockWindows/OverlayBottom");
        subHandles[29]->Attach(this);
        RecentMacros = this->subHandles[0]->GetInt("RecentMacros", 12);
        funcs["RecentMacros"] = &MiscParamsP::updateRecentMacros;
        ShortcutCount = this->subHandles[0]->GetInt("ShortcutCount", 3);
        funcs["ShortcutCount"] = &MiscParamsP::updateShortcutCount;
        ShortcutModifiers = this->subHandles[0]->GetASCII("ShortcutModifiers", "Ctrl+Shift+");
        funcs["ShortcutModifiers"] = &MiscParamsP::updateShortcutModifiers;
        CoarseLinearSnapMultiplier = this->subHandles[1]->GetInt("CoarseLinearSnapMultiplier", 5);
        funcs["CoarseLinearSnapMultiplier"] = &MiscParamsP::updateCoarseLinearSnapMultiplier;
        CoarseRotationSnapMultiplier = this->subHandles[1]->GetInt("CoarseRotationSnapMultiplier", 5);
        funcs["CoarseRotationSnapMultiplier"] = &MiscParamsP::updateCoarseRotationSnapMultiplier;
        CacheLimit = this->subHandles[2]->GetUnsigned("Limit", 500);
        funcs["Limit"] = &MiscParamsP::updateCacheLimit;
        CachePeriod = this->subHandles[2]->GetInt("Period", 2);
        funcs["Period"] = &MiscParamsP::updateCachePeriod;
        ShortcutTimeout = this->subHandles[3]->GetInt("ShortcutTimeout", 300);
        funcs["ShortcutTimeout"] = &MiscParamsP::updateShortcutTimeout;
        ShowTabBar = this->subHandles[4]->GetBool("ShowTabBar", false);
        funcs["ShowTabBar"] = &MiscParamsP::updateShowTabBar;
        TabBarShowText = this->subHandles[4]->GetBool("TabBarShowText", false);
        funcs["TabBarShowText"] = &MiscParamsP::updateTabBarShowText;
        TabBarMaxLength = this->subHandles[4]->GetInt("TabBarMaxLength", 0);
        funcs["TabBarMaxLength"] = &MiscParamsP::updateTabBarMaxLength;
        DisableDpiScaling = this->subHandles[5]->GetBool("DisableDpiScaling", false);
        funcs["DisableDpiScaling"] = &MiscParamsP::updateDisableDpiScaling;
        UseSoftwareOpenGL = this->subHandles[6]->GetBool("UseSoftwareOpenGL", false);
        funcs["UseSoftwareOpenGL"] = &MiscParamsP::updateUseSoftwareOpenGL;
        Unflatten = this->subHandles[7]->GetBool("Unflatten", true);
        funcs["Unflatten"] = &MiscParamsP::updateUnflatten;
        GeoFeatureSubgraphs = this->subHandles[7]->GetBool("GeoFeatureSubgraphs", true);
        funcs["GeoFeatureSubgraphs"] = &MiscParamsP::updateGeoFeatureSubgraphs;
        EnableGizmos = this->subHandles[1]->GetBool("EnableGizmos", true);
        funcs["EnableGizmos"] = &MiscParamsP::updateEnableGizmos;
        DelayedGizmoUpdate = this->subHandles[1]->GetBool("DelayedGizmoUpdate", false);
        funcs["DelayedGizmoUpdate"] = &MiscParamsP::updateDelayedGizmoUpdate;
        EnableCoarseSnap = this->subHandles[1]->GetBool("EnableCoarseSnap", true);
        funcs["EnableCoarseSnap"] = &MiscParamsP::updateEnableCoarseSnap;
        FineSnapModifier = this->subHandles[1]->GetInt("FineSnapModifier", 33554432);
        funcs["FineSnapModifier"] = &MiscParamsP::updateFineSnapModifier;
        DefaultCoarseDragBehavior = this->subHandles[1]->GetInt("DefaultCoarseDragBehavior", 0);
        funcs["DefaultCoarseDragBehavior"] = &MiscParamsP::updateDefaultCoarseDragBehavior;
        PropertyViewAutoTransactionView = this->subHandles[8]->GetBool("AutoTransactionView", false);
        funcs["AutoTransactionView"] = &MiscParamsP::updatePropertyViewAutoTransactionView;
        PropertyViewAutoTransactionData = this->subHandles[8]->GetBool("AutoTransactionData", true);
        funcs["AutoTransactionData"] = &MiscParamsP::updatePropertyViewAutoTransactionData;
        PropertyViewAutoExpandView = this->subHandles[8]->GetBool("AutoExpandView", false);
        funcs["AutoExpandView"] = &MiscParamsP::updatePropertyViewAutoExpandView;
        PropertyViewAutoExpandData = this->subHandles[8]->GetBool("AutoExpandData", false);
        funcs["AutoExpandData"] = &MiscParamsP::updatePropertyViewAutoExpandData;
        PropertyViewHideHeader = this->subHandles[8]->GetBool("HideHeader", false);
        funcs["HideHeader"] = &MiscParamsP::updatePropertyViewHideHeader;
        PropertyViewViewSectionSize = this->subHandles[8]->GetInt("ViewSectionSize", 150);
        funcs["ViewSectionSize"] = &MiscParamsP::updatePropertyViewViewSectionSize;
        PropertyViewDataSectionSize = this->subHandles[8]->GetInt("DataSectionSize", 150);
        funcs["DataSectionSize"] = &MiscParamsP::updatePropertyViewDataSectionSize;
        PropertyViewLastTabIndex = this->subHandles[8]->GetInt("LastTabIndex", 1);
        funcs["LastTabIndex"] = &MiscParamsP::updatePropertyViewLastTabIndex;
        NewPropertyType = this->subHandles[8]->GetASCII("NewPropertyType", "App::PropertyString");
        funcs["NewPropertyType"] = &MiscParamsP::updateNewPropertyType;
        NewPropertyGroup = this->subHandles[8]->GetASCII("NewPropertyGroup", "Base");
        funcs["NewPropertyGroup"] = &MiscParamsP::updateNewPropertyGroup;
        NewPropertyAppend = this->subHandles[8]->GetBool("NewPropertyAppend", true);
        funcs["NewPropertyAppend"] = &MiscParamsP::updateNewPropertyAppend;
        PanelMirror = this->subHandles[9]->GetASCII("PanelMirror", "all");
        funcs["PanelMirror"] = &MiscParamsP::updatePanelMirror;
        PanelPollMs = this->subHandles[9]->GetInt("PanelPollMs", 500);
        funcs["PanelPollMs"] = &MiscParamsP::updatePanelPollMs;
        AutoShowSelectionView = this->subHandles[10]->GetBool("AutoShowSelectionView", false);
        funcs["AutoShowSelectionView"] = &MiscParamsP::updateAutoShowSelectionView;
        SingleClickFeatureSelect = this->subHandles[10]->GetBool("singleClickFeatureSelect", true);
        funcs["singleClickFeatureSelect"] = &MiscParamsP::updateSingleClickFeatureSelect;
        DAGViewSelectionMode = this->subHandles[11]->GetInt("SelectionMode", 0);
        funcs["SelectionMode"] = &MiscParamsP::updateDAGViewSelectionMode;
        DAGViewFontPointSize = this->subHandles[11]->GetInt("FontPointSize", 0);
        funcs["FontPointSize"] = &MiscParamsP::updateDAGViewFontPointSize;
        DAGViewDirection = this->subHandles[11]->GetFloat("Direction", 1.0);
        funcs["Direction"] = &MiscParamsP::updateDAGViewDirection;
        CustomViewQ0 = this->subHandles[12]->GetFloat("Q0", 0.0);
        funcs["Q0"] = &MiscParamsP::updateCustomViewQ0;
        CustomViewQ1 = this->subHandles[12]->GetFloat("Q1", 0.0);
        funcs["Q1"] = &MiscParamsP::updateCustomViewQ1;
        CustomViewQ2 = this->subHandles[12]->GetFloat("Q2", 0.0);
        funcs["Q2"] = &MiscParamsP::updateCustomViewQ2;
        CustomViewQ3 = this->subHandles[12]->GetFloat("Q3", 1.0);
        funcs["Q3"] = &MiscParamsP::updateCustomViewQ3;
        RecentFiles = this->subHandles[13]->GetInt("RecentFiles", 4);
        funcs["RecentFiles"] = &MiscParamsP::updateRecentFiles;
        DonatePage = this->subHandles[14]->GetASCII("DonatePage", "https://wiki.freecad.org/Donate");
        funcs["DonatePage"] = &MiscParamsP::updateDonatePage;
        Graphviz = this->subHandles[15]->GetASCII("Graphviz", "");
        funcs["Graphviz"] = &MiscParamsP::updateGraphviz;
        IconThemeName = this->subHandles[16]->GetASCII("Name", "");
        funcs["Name"] = &MiscParamsP::updateIconThemeName;
        IconThemeSearchPath = this->subHandles[16]->GetASCII("SearchPath", "");
        funcs["SearchPath"] = &MiscParamsP::updateIconThemeSearchPath;
        IconThemeSearchPaths = this->subHandles[16]->GetBool("_ThemeSearchPaths", false);
        funcs["_ThemeSearchPaths"] = &MiscParamsP::updateIconThemeSearchPaths;
        TreeViewEnabled = this->subHandles[17]->GetBool("Enabled", true);
        funcs["Enabled"] = &MiscParamsP::updateTreeViewEnabled;
        PropertyViewEnabled = this->subHandles[18]->GetBool("Enabled", true);
        funcs["Enabled"] = &MiscParamsP::updatePropertyViewEnabled;
        ComboViewEnabled = this->subHandles[19]->GetBool("Enabled", false);
        funcs["Enabled"] = &MiscParamsP::updateComboViewEnabled;
        TaskWatcherEnabled = this->subHandles[20]->GetBool("Enabled", false);
        funcs["Enabled"] = &MiscParamsP::updateTaskWatcherEnabled;
        DAGViewEnabled = this->subHandles[21]->GetBool("Enabled", false);
        funcs["Enabled"] = &MiscParamsP::updateDAGViewEnabled;
        ComboViewTreeViewSize = this->subHandles[19]->GetInt("TreeViewSize", 0);
        funcs["TreeViewSize"] = &MiscParamsP::updateComboViewTreeViewSize;
        ComboViewPropertyViewSize = this->subHandles[19]->GetInt("PropertyViewSize", 0);
        funcs["PropertyViewSize"] = &MiscParamsP::updateComboViewPropertyViewSize;
        PlacementRotationMethod = this->subHandles[22]->GetInt("RotationMethod", 0);
        funcs["RotationMethod"] = &MiscParamsP::updatePlacementRotationMethod;
        SharePort = this->subHandles[23]->GetInt("Port", 8210);
        funcs["Port"] = &MiscParamsP::updateSharePort;
        ShareDoor = this->subHandles[23]->GetASCII("Door", "");
        funcs["Door"] = &MiscParamsP::updateShareDoor;
        ShareExternalHost = this->subHandles[23]->GetASCII("ExternalHost", "");
        funcs["ExternalHost"] = &MiscParamsP::updateShareExternalHost;
        ShareViewerPage = this->subHandles[23]->GetASCII("ViewerPage", "");
        funcs["ViewerPage"] = &MiscParamsP::updateShareViewerPage;
        ShareTrustProxy = this->subHandles[23]->GetBool("TrustProxy", false);
        funcs["TrustProxy"] = &MiscParamsP::updateShareTrustProxy;
        ShareDoorsSeeded = this->subHandles[23]->GetBool("DoorsSeeded", false);
        funcs["DoorsSeeded"] = &MiscParamsP::updateShareDoorsSeeded;
        ShareGrantsMigrated = this->subHandles[23]->GetBool("GrantsMigrated", false);
        funcs["GrantsMigrated"] = &MiscParamsP::updateShareGrantsMigrated;
        DraggerLastTranslationIncrement = this->subHandles[24]->GetFloat("LastTranslationIncrement", 1.0);
        funcs["LastTranslationIncrement"] = &MiscParamsP::updateDraggerLastTranslationIncrement;
        DraggerLastRotationIncrement = this->subHandles[24]->GetFloat("LastRotationIncrement", 15.0);
        funcs["LastRotationIncrement"] = &MiscParamsP::updateDraggerLastRotationIncrement;
        ActivateOverlay = this->subHandles[25]->GetBool("ActivateOverlay", true);
        funcs["ActivateOverlay"] = &MiscParamsP::updateActivateOverlay;
        DockCursorMargin = this->subHandles[25]->GetInt("CursorMargin", 5);
        funcs["CursorMargin"] = &MiscParamsP::updateDockCursorMargin;
        DockStdTreeView = this->subHandles[25]->GetBool("Std_TreeView", true);
        funcs["Std_TreeView"] = &MiscParamsP::updateDockStdTreeView;
        DockStdPropertyView = this->subHandles[25]->GetBool("Std_PropertyView", false);
        funcs["Std_PropertyView"] = &MiscParamsP::updateDockStdPropertyView;
        OverlayLeftWidgets = this->subHandles[26]->GetASCII("Widgets", "");
        funcs["Widgets"] = &MiscParamsP::updateOverlayLeftWidgets;
        OverlayLeftWidth = this->subHandles[26]->GetInt("Width", 0);
        funcs["Width"] = &MiscParamsP::updateOverlayLeftWidth;
        OverlayLeftHeight = this->subHandles[26]->GetInt("Height", 0);
        funcs["Height"] = &MiscParamsP::updateOverlayLeftHeight;
        OverlayLeftOffset1 = this->subHandles[26]->GetInt("Offset1", 0);
        funcs["Offset1"] = &MiscParamsP::updateOverlayLeftOffset1;
        OverlayLeftOffset3 = this->subHandles[26]->GetInt("Offset3", 0);
        funcs["Offset3"] = &MiscParamsP::updateOverlayLeftOffset3;
        OverlayLeftOffset2 = this->subHandles[26]->GetInt("Offset2", 0);
        funcs["Offset2"] = &MiscParamsP::updateOverlayLeftOffset2;
        OverlayLeftSizes = this->subHandles[26]->GetASCII("Sizes", "");
        funcs["Sizes"] = &MiscParamsP::updateOverlayLeftSizes;
        OverlayLeftAutoHide = this->subHandles[26]->GetBool("AutoHide", false);
        funcs["AutoHide"] = &MiscParamsP::updateOverlayLeftAutoHide;
        OverlayLeftEditHide = this->subHandles[26]->GetBool("EditHide", false);
        funcs["EditHide"] = &MiscParamsP::updateOverlayLeftEditHide;
        OverlayLeftEditShow = this->subHandles[26]->GetBool("EditShow", false);
        funcs["EditShow"] = &MiscParamsP::updateOverlayLeftEditShow;
        OverlayLeftTaskShow = this->subHandles[26]->GetBool("TaskShow", false);
        funcs["TaskShow"] = &MiscParamsP::updateOverlayLeftTaskShow;
        OverlayLeftClosed = this->subHandles[26]->GetBool("Closed", false);
        funcs["Closed"] = &MiscParamsP::updateOverlayLeftClosed;
        OverlayLeftTransparent = this->subHandles[26]->GetBool("Transparent", false);
        funcs["Transparent"] = &MiscParamsP::updateOverlayLeftTransparent;
        OverlayRightWidgets = this->subHandles[27]->GetASCII("Widgets", "");
        funcs["Widgets"] = &MiscParamsP::updateOverlayRightWidgets;
        OverlayRightWidth = this->subHandles[27]->GetInt("Width", 0);
        funcs["Width"] = &MiscParamsP::updateOverlayRightWidth;
        OverlayRightHeight = this->subHandles[27]->GetInt("Height", 0);
        funcs["Height"] = &MiscParamsP::updateOverlayRightHeight;
        OverlayRightOffset1 = this->subHandles[27]->GetInt("Offset1", 0);
        funcs["Offset1"] = &MiscParamsP::updateOverlayRightOffset1;
        OverlayRightOffset3 = this->subHandles[27]->GetInt("Offset3", 0);
        funcs["Offset3"] = &MiscParamsP::updateOverlayRightOffset3;
        OverlayRightOffset2 = this->subHandles[27]->GetInt("Offset2", 0);
        funcs["Offset2"] = &MiscParamsP::updateOverlayRightOffset2;
        OverlayRightSizes = this->subHandles[27]->GetASCII("Sizes", "");
        funcs["Sizes"] = &MiscParamsP::updateOverlayRightSizes;
        OverlayRightAutoHide = this->subHandles[27]->GetBool("AutoHide", false);
        funcs["AutoHide"] = &MiscParamsP::updateOverlayRightAutoHide;
        OverlayRightEditHide = this->subHandles[27]->GetBool("EditHide", false);
        funcs["EditHide"] = &MiscParamsP::updateOverlayRightEditHide;
        OverlayRightEditShow = this->subHandles[27]->GetBool("EditShow", false);
        funcs["EditShow"] = &MiscParamsP::updateOverlayRightEditShow;
        OverlayRightTaskShow = this->subHandles[27]->GetBool("TaskShow", false);
        funcs["TaskShow"] = &MiscParamsP::updateOverlayRightTaskShow;
        OverlayRightClosed = this->subHandles[27]->GetBool("Closed", false);
        funcs["Closed"] = &MiscParamsP::updateOverlayRightClosed;
        OverlayRightTransparent = this->subHandles[27]->GetBool("Transparent", false);
        funcs["Transparent"] = &MiscParamsP::updateOverlayRightTransparent;
        OverlayTopWidgets = this->subHandles[28]->GetASCII("Widgets", "");
        funcs["Widgets"] = &MiscParamsP::updateOverlayTopWidgets;
        OverlayTopWidth = this->subHandles[28]->GetInt("Width", 0);
        funcs["Width"] = &MiscParamsP::updateOverlayTopWidth;
        OverlayTopHeight = this->subHandles[28]->GetInt("Height", 0);
        funcs["Height"] = &MiscParamsP::updateOverlayTopHeight;
        OverlayTopOffset1 = this->subHandles[28]->GetInt("Offset1", 0);
        funcs["Offset1"] = &MiscParamsP::updateOverlayTopOffset1;
        OverlayTopOffset3 = this->subHandles[28]->GetInt("Offset3", 0);
        funcs["Offset3"] = &MiscParamsP::updateOverlayTopOffset3;
        OverlayTopOffset2 = this->subHandles[28]->GetInt("Offset2", 0);
        funcs["Offset2"] = &MiscParamsP::updateOverlayTopOffset2;
        OverlayTopSizes = this->subHandles[28]->GetASCII("Sizes", "");
        funcs["Sizes"] = &MiscParamsP::updateOverlayTopSizes;
        OverlayTopAutoHide = this->subHandles[28]->GetBool("AutoHide", false);
        funcs["AutoHide"] = &MiscParamsP::updateOverlayTopAutoHide;
        OverlayTopEditHide = this->subHandles[28]->GetBool("EditHide", false);
        funcs["EditHide"] = &MiscParamsP::updateOverlayTopEditHide;
        OverlayTopEditShow = this->subHandles[28]->GetBool("EditShow", false);
        funcs["EditShow"] = &MiscParamsP::updateOverlayTopEditShow;
        OverlayTopTaskShow = this->subHandles[28]->GetBool("TaskShow", false);
        funcs["TaskShow"] = &MiscParamsP::updateOverlayTopTaskShow;
        OverlayTopClosed = this->subHandles[28]->GetBool("Closed", false);
        funcs["Closed"] = &MiscParamsP::updateOverlayTopClosed;
        OverlayTopTransparent = this->subHandles[28]->GetBool("Transparent", false);
        funcs["Transparent"] = &MiscParamsP::updateOverlayTopTransparent;
        OverlayBottomWidgets = this->subHandles[29]->GetASCII("Widgets", "");
        funcs["Widgets"] = &MiscParamsP::updateOverlayBottomWidgets;
        OverlayBottomWidth = this->subHandles[29]->GetInt("Width", 0);
        funcs["Width"] = &MiscParamsP::updateOverlayBottomWidth;
        OverlayBottomHeight = this->subHandles[29]->GetInt("Height", 0);
        funcs["Height"] = &MiscParamsP::updateOverlayBottomHeight;
        OverlayBottomOffset1 = this->subHandles[29]->GetInt("Offset1", 0);
        funcs["Offset1"] = &MiscParamsP::updateOverlayBottomOffset1;
        OverlayBottomOffset3 = this->subHandles[29]->GetInt("Offset3", 0);
        funcs["Offset3"] = &MiscParamsP::updateOverlayBottomOffset3;
        OverlayBottomOffset2 = this->subHandles[29]->GetInt("Offset2", 0);
        funcs["Offset2"] = &MiscParamsP::updateOverlayBottomOffset2;
        OverlayBottomSizes = this->subHandles[29]->GetASCII("Sizes", "");
        funcs["Sizes"] = &MiscParamsP::updateOverlayBottomSizes;
        OverlayBottomAutoHide = this->subHandles[29]->GetBool("AutoHide", false);
        funcs["AutoHide"] = &MiscParamsP::updateOverlayBottomAutoHide;
        OverlayBottomEditHide = this->subHandles[29]->GetBool("EditHide", false);
        funcs["EditHide"] = &MiscParamsP::updateOverlayBottomEditHide;
        OverlayBottomEditShow = this->subHandles[29]->GetBool("EditShow", false);
        funcs["EditShow"] = &MiscParamsP::updateOverlayBottomEditShow;
        OverlayBottomTaskShow = this->subHandles[29]->GetBool("TaskShow", false);
        funcs["TaskShow"] = &MiscParamsP::updateOverlayBottomTaskShow;
        OverlayBottomClosed = this->subHandles[29]->GetBool("Closed", false);
        funcs["Closed"] = &MiscParamsP::updateOverlayBottomClosed;
        OverlayBottomTransparent = this->subHandles[29]->GetBool("Transparent", false);
        funcs["Transparent"] = &MiscParamsP::updateOverlayBottomTransparent;
    }

    // Auto generated code (Tools/params_utils.py:284)
    ~MiscParamsP() override = default;

    // Auto generated code (Tools/params_utils.py:297)
    void OnChange(Base::Subject<const char*> &, const char* sReason) override {
        if(!sReason)
            return;
        auto it = funcs.find(sReason);
        if(it == funcs.end())
            return;
        it->second(this);
    }


    // Auto generated code (Tools/params_utils.py:314)
    static void updateRecentMacros(MiscParamsP *self) {
        self->RecentMacros = self->subHandles[0]->GetInt("RecentMacros", 12);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateShortcutCount(MiscParamsP *self) {
        self->ShortcutCount = self->subHandles[0]->GetInt("ShortcutCount", 3);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateShortcutModifiers(MiscParamsP *self) {
        self->ShortcutModifiers = self->subHandles[0]->GetASCII("ShortcutModifiers", "Ctrl+Shift+");
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateCoarseLinearSnapMultiplier(MiscParamsP *self) {
        self->CoarseLinearSnapMultiplier = self->subHandles[1]->GetInt("CoarseLinearSnapMultiplier", 5);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateCoarseRotationSnapMultiplier(MiscParamsP *self) {
        self->CoarseRotationSnapMultiplier = self->subHandles[1]->GetInt("CoarseRotationSnapMultiplier", 5);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateCacheLimit(MiscParamsP *self) {
        self->CacheLimit = self->subHandles[2]->GetUnsigned("Limit", 500);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateCachePeriod(MiscParamsP *self) {
        self->CachePeriod = self->subHandles[2]->GetInt("Period", 2);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateShortcutTimeout(MiscParamsP *self) {
        self->ShortcutTimeout = self->subHandles[3]->GetInt("ShortcutTimeout", 300);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateShowTabBar(MiscParamsP *self) {
        self->ShowTabBar = self->subHandles[4]->GetBool("ShowTabBar", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateTabBarShowText(MiscParamsP *self) {
        self->TabBarShowText = self->subHandles[4]->GetBool("TabBarShowText", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateTabBarMaxLength(MiscParamsP *self) {
        self->TabBarMaxLength = self->subHandles[4]->GetInt("TabBarMaxLength", 0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateDisableDpiScaling(MiscParamsP *self) {
        self->DisableDpiScaling = self->subHandles[5]->GetBool("DisableDpiScaling", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateUseSoftwareOpenGL(MiscParamsP *self) {
        self->UseSoftwareOpenGL = self->subHandles[6]->GetBool("UseSoftwareOpenGL", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateUnflatten(MiscParamsP *self) {
        self->Unflatten = self->subHandles[7]->GetBool("Unflatten", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateGeoFeatureSubgraphs(MiscParamsP *self) {
        self->GeoFeatureSubgraphs = self->subHandles[7]->GetBool("GeoFeatureSubgraphs", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateEnableGizmos(MiscParamsP *self) {
        self->EnableGizmos = self->subHandles[1]->GetBool("EnableGizmos", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateDelayedGizmoUpdate(MiscParamsP *self) {
        self->DelayedGizmoUpdate = self->subHandles[1]->GetBool("DelayedGizmoUpdate", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateEnableCoarseSnap(MiscParamsP *self) {
        self->EnableCoarseSnap = self->subHandles[1]->GetBool("EnableCoarseSnap", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateFineSnapModifier(MiscParamsP *self) {
        self->FineSnapModifier = self->subHandles[1]->GetInt("FineSnapModifier", 33554432);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateDefaultCoarseDragBehavior(MiscParamsP *self) {
        self->DefaultCoarseDragBehavior = self->subHandles[1]->GetInt("DefaultCoarseDragBehavior", 0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updatePropertyViewAutoTransactionView(MiscParamsP *self) {
        self->PropertyViewAutoTransactionView = self->subHandles[8]->GetBool("AutoTransactionView", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updatePropertyViewAutoTransactionData(MiscParamsP *self) {
        self->PropertyViewAutoTransactionData = self->subHandles[8]->GetBool("AutoTransactionData", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updatePropertyViewAutoExpandView(MiscParamsP *self) {
        self->PropertyViewAutoExpandView = self->subHandles[8]->GetBool("AutoExpandView", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updatePropertyViewAutoExpandData(MiscParamsP *self) {
        self->PropertyViewAutoExpandData = self->subHandles[8]->GetBool("AutoExpandData", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updatePropertyViewHideHeader(MiscParamsP *self) {
        self->PropertyViewHideHeader = self->subHandles[8]->GetBool("HideHeader", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updatePropertyViewViewSectionSize(MiscParamsP *self) {
        self->PropertyViewViewSectionSize = self->subHandles[8]->GetInt("ViewSectionSize", 150);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updatePropertyViewDataSectionSize(MiscParamsP *self) {
        self->PropertyViewDataSectionSize = self->subHandles[8]->GetInt("DataSectionSize", 150);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updatePropertyViewLastTabIndex(MiscParamsP *self) {
        self->PropertyViewLastTabIndex = self->subHandles[8]->GetInt("LastTabIndex", 1);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateNewPropertyType(MiscParamsP *self) {
        self->NewPropertyType = self->subHandles[8]->GetASCII("NewPropertyType", "App::PropertyString");
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateNewPropertyGroup(MiscParamsP *self) {
        self->NewPropertyGroup = self->subHandles[8]->GetASCII("NewPropertyGroup", "Base");
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateNewPropertyAppend(MiscParamsP *self) {
        self->NewPropertyAppend = self->subHandles[8]->GetBool("NewPropertyAppend", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updatePanelMirror(MiscParamsP *self) {
        self->PanelMirror = self->subHandles[9]->GetASCII("PanelMirror", "all");
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updatePanelPollMs(MiscParamsP *self) {
        self->PanelPollMs = self->subHandles[9]->GetInt("PanelPollMs", 500);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateAutoShowSelectionView(MiscParamsP *self) {
        self->AutoShowSelectionView = self->subHandles[10]->GetBool("AutoShowSelectionView", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateSingleClickFeatureSelect(MiscParamsP *self) {
        self->SingleClickFeatureSelect = self->subHandles[10]->GetBool("singleClickFeatureSelect", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateDAGViewSelectionMode(MiscParamsP *self) {
        self->DAGViewSelectionMode = self->subHandles[11]->GetInt("SelectionMode", 0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateDAGViewFontPointSize(MiscParamsP *self) {
        self->DAGViewFontPointSize = self->subHandles[11]->GetInt("FontPointSize", 0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateDAGViewDirection(MiscParamsP *self) {
        self->DAGViewDirection = self->subHandles[11]->GetFloat("Direction", 1.0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateCustomViewQ0(MiscParamsP *self) {
        self->CustomViewQ0 = self->subHandles[12]->GetFloat("Q0", 0.0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateCustomViewQ1(MiscParamsP *self) {
        self->CustomViewQ1 = self->subHandles[12]->GetFloat("Q1", 0.0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateCustomViewQ2(MiscParamsP *self) {
        self->CustomViewQ2 = self->subHandles[12]->GetFloat("Q2", 0.0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateCustomViewQ3(MiscParamsP *self) {
        self->CustomViewQ3 = self->subHandles[12]->GetFloat("Q3", 1.0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateRecentFiles(MiscParamsP *self) {
        self->RecentFiles = self->subHandles[13]->GetInt("RecentFiles", 4);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateDonatePage(MiscParamsP *self) {
        self->DonatePage = self->subHandles[14]->GetASCII("DonatePage", "https://wiki.freecad.org/Donate");
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateGraphviz(MiscParamsP *self) {
        self->Graphviz = self->subHandles[15]->GetASCII("Graphviz", "");
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateIconThemeName(MiscParamsP *self) {
        self->IconThemeName = self->subHandles[16]->GetASCII("Name", "");
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateIconThemeSearchPath(MiscParamsP *self) {
        self->IconThemeSearchPath = self->subHandles[16]->GetASCII("SearchPath", "");
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateIconThemeSearchPaths(MiscParamsP *self) {
        self->IconThemeSearchPaths = self->subHandles[16]->GetBool("_ThemeSearchPaths", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateTreeViewEnabled(MiscParamsP *self) {
        self->TreeViewEnabled = self->subHandles[17]->GetBool("Enabled", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updatePropertyViewEnabled(MiscParamsP *self) {
        self->PropertyViewEnabled = self->subHandles[18]->GetBool("Enabled", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateComboViewEnabled(MiscParamsP *self) {
        self->ComboViewEnabled = self->subHandles[19]->GetBool("Enabled", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateTaskWatcherEnabled(MiscParamsP *self) {
        self->TaskWatcherEnabled = self->subHandles[20]->GetBool("Enabled", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateDAGViewEnabled(MiscParamsP *self) {
        self->DAGViewEnabled = self->subHandles[21]->GetBool("Enabled", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateComboViewTreeViewSize(MiscParamsP *self) {
        self->ComboViewTreeViewSize = self->subHandles[19]->GetInt("TreeViewSize", 0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateComboViewPropertyViewSize(MiscParamsP *self) {
        self->ComboViewPropertyViewSize = self->subHandles[19]->GetInt("PropertyViewSize", 0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updatePlacementRotationMethod(MiscParamsP *self) {
        self->PlacementRotationMethod = self->subHandles[22]->GetInt("RotationMethod", 0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateSharePort(MiscParamsP *self) {
        self->SharePort = self->subHandles[23]->GetInt("Port", 8210);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateShareDoor(MiscParamsP *self) {
        self->ShareDoor = self->subHandles[23]->GetASCII("Door", "");
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateShareExternalHost(MiscParamsP *self) {
        self->ShareExternalHost = self->subHandles[23]->GetASCII("ExternalHost", "");
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateShareViewerPage(MiscParamsP *self) {
        self->ShareViewerPage = self->subHandles[23]->GetASCII("ViewerPage", "");
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateShareTrustProxy(MiscParamsP *self) {
        self->ShareTrustProxy = self->subHandles[23]->GetBool("TrustProxy", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateShareDoorsSeeded(MiscParamsP *self) {
        self->ShareDoorsSeeded = self->subHandles[23]->GetBool("DoorsSeeded", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateShareGrantsMigrated(MiscParamsP *self) {
        self->ShareGrantsMigrated = self->subHandles[23]->GetBool("GrantsMigrated", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateDraggerLastTranslationIncrement(MiscParamsP *self) {
        self->DraggerLastTranslationIncrement = self->subHandles[24]->GetFloat("LastTranslationIncrement", 1.0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateDraggerLastRotationIncrement(MiscParamsP *self) {
        self->DraggerLastRotationIncrement = self->subHandles[24]->GetFloat("LastRotationIncrement", 15.0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateActivateOverlay(MiscParamsP *self) {
        self->ActivateOverlay = self->subHandles[25]->GetBool("ActivateOverlay", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateDockCursorMargin(MiscParamsP *self) {
        self->DockCursorMargin = self->subHandles[25]->GetInt("CursorMargin", 5);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateDockStdTreeView(MiscParamsP *self) {
        self->DockStdTreeView = self->subHandles[25]->GetBool("Std_TreeView", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateDockStdPropertyView(MiscParamsP *self) {
        self->DockStdPropertyView = self->subHandles[25]->GetBool("Std_PropertyView", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateOverlayLeftWidgets(MiscParamsP *self) {
        self->OverlayLeftWidgets = self->subHandles[26]->GetASCII("Widgets", "");
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateOverlayLeftWidth(MiscParamsP *self) {
        self->OverlayLeftWidth = self->subHandles[26]->GetInt("Width", 0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateOverlayLeftHeight(MiscParamsP *self) {
        self->OverlayLeftHeight = self->subHandles[26]->GetInt("Height", 0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateOverlayLeftOffset1(MiscParamsP *self) {
        self->OverlayLeftOffset1 = self->subHandles[26]->GetInt("Offset1", 0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateOverlayLeftOffset3(MiscParamsP *self) {
        self->OverlayLeftOffset3 = self->subHandles[26]->GetInt("Offset3", 0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateOverlayLeftOffset2(MiscParamsP *self) {
        self->OverlayLeftOffset2 = self->subHandles[26]->GetInt("Offset2", 0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateOverlayLeftSizes(MiscParamsP *self) {
        self->OverlayLeftSizes = self->subHandles[26]->GetASCII("Sizes", "");
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateOverlayLeftAutoHide(MiscParamsP *self) {
        self->OverlayLeftAutoHide = self->subHandles[26]->GetBool("AutoHide", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateOverlayLeftEditHide(MiscParamsP *self) {
        self->OverlayLeftEditHide = self->subHandles[26]->GetBool("EditHide", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateOverlayLeftEditShow(MiscParamsP *self) {
        self->OverlayLeftEditShow = self->subHandles[26]->GetBool("EditShow", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateOverlayLeftTaskShow(MiscParamsP *self) {
        self->OverlayLeftTaskShow = self->subHandles[26]->GetBool("TaskShow", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateOverlayLeftClosed(MiscParamsP *self) {
        self->OverlayLeftClosed = self->subHandles[26]->GetBool("Closed", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateOverlayLeftTransparent(MiscParamsP *self) {
        self->OverlayLeftTransparent = self->subHandles[26]->GetBool("Transparent", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateOverlayRightWidgets(MiscParamsP *self) {
        self->OverlayRightWidgets = self->subHandles[27]->GetASCII("Widgets", "");
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateOverlayRightWidth(MiscParamsP *self) {
        self->OverlayRightWidth = self->subHandles[27]->GetInt("Width", 0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateOverlayRightHeight(MiscParamsP *self) {
        self->OverlayRightHeight = self->subHandles[27]->GetInt("Height", 0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateOverlayRightOffset1(MiscParamsP *self) {
        self->OverlayRightOffset1 = self->subHandles[27]->GetInt("Offset1", 0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateOverlayRightOffset3(MiscParamsP *self) {
        self->OverlayRightOffset3 = self->subHandles[27]->GetInt("Offset3", 0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateOverlayRightOffset2(MiscParamsP *self) {
        self->OverlayRightOffset2 = self->subHandles[27]->GetInt("Offset2", 0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateOverlayRightSizes(MiscParamsP *self) {
        self->OverlayRightSizes = self->subHandles[27]->GetASCII("Sizes", "");
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateOverlayRightAutoHide(MiscParamsP *self) {
        self->OverlayRightAutoHide = self->subHandles[27]->GetBool("AutoHide", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateOverlayRightEditHide(MiscParamsP *self) {
        self->OverlayRightEditHide = self->subHandles[27]->GetBool("EditHide", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateOverlayRightEditShow(MiscParamsP *self) {
        self->OverlayRightEditShow = self->subHandles[27]->GetBool("EditShow", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateOverlayRightTaskShow(MiscParamsP *self) {
        self->OverlayRightTaskShow = self->subHandles[27]->GetBool("TaskShow", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateOverlayRightClosed(MiscParamsP *self) {
        self->OverlayRightClosed = self->subHandles[27]->GetBool("Closed", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateOverlayRightTransparent(MiscParamsP *self) {
        self->OverlayRightTransparent = self->subHandles[27]->GetBool("Transparent", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateOverlayTopWidgets(MiscParamsP *self) {
        self->OverlayTopWidgets = self->subHandles[28]->GetASCII("Widgets", "");
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateOverlayTopWidth(MiscParamsP *self) {
        self->OverlayTopWidth = self->subHandles[28]->GetInt("Width", 0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateOverlayTopHeight(MiscParamsP *self) {
        self->OverlayTopHeight = self->subHandles[28]->GetInt("Height", 0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateOverlayTopOffset1(MiscParamsP *self) {
        self->OverlayTopOffset1 = self->subHandles[28]->GetInt("Offset1", 0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateOverlayTopOffset3(MiscParamsP *self) {
        self->OverlayTopOffset3 = self->subHandles[28]->GetInt("Offset3", 0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateOverlayTopOffset2(MiscParamsP *self) {
        self->OverlayTopOffset2 = self->subHandles[28]->GetInt("Offset2", 0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateOverlayTopSizes(MiscParamsP *self) {
        self->OverlayTopSizes = self->subHandles[28]->GetASCII("Sizes", "");
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateOverlayTopAutoHide(MiscParamsP *self) {
        self->OverlayTopAutoHide = self->subHandles[28]->GetBool("AutoHide", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateOverlayTopEditHide(MiscParamsP *self) {
        self->OverlayTopEditHide = self->subHandles[28]->GetBool("EditHide", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateOverlayTopEditShow(MiscParamsP *self) {
        self->OverlayTopEditShow = self->subHandles[28]->GetBool("EditShow", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateOverlayTopTaskShow(MiscParamsP *self) {
        self->OverlayTopTaskShow = self->subHandles[28]->GetBool("TaskShow", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateOverlayTopClosed(MiscParamsP *self) {
        self->OverlayTopClosed = self->subHandles[28]->GetBool("Closed", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateOverlayTopTransparent(MiscParamsP *self) {
        self->OverlayTopTransparent = self->subHandles[28]->GetBool("Transparent", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateOverlayBottomWidgets(MiscParamsP *self) {
        self->OverlayBottomWidgets = self->subHandles[29]->GetASCII("Widgets", "");
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateOverlayBottomWidth(MiscParamsP *self) {
        self->OverlayBottomWidth = self->subHandles[29]->GetInt("Width", 0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateOverlayBottomHeight(MiscParamsP *self) {
        self->OverlayBottomHeight = self->subHandles[29]->GetInt("Height", 0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateOverlayBottomOffset1(MiscParamsP *self) {
        self->OverlayBottomOffset1 = self->subHandles[29]->GetInt("Offset1", 0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateOverlayBottomOffset3(MiscParamsP *self) {
        self->OverlayBottomOffset3 = self->subHandles[29]->GetInt("Offset3", 0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateOverlayBottomOffset2(MiscParamsP *self) {
        self->OverlayBottomOffset2 = self->subHandles[29]->GetInt("Offset2", 0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateOverlayBottomSizes(MiscParamsP *self) {
        self->OverlayBottomSizes = self->subHandles[29]->GetASCII("Sizes", "");
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateOverlayBottomAutoHide(MiscParamsP *self) {
        self->OverlayBottomAutoHide = self->subHandles[29]->GetBool("AutoHide", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateOverlayBottomEditHide(MiscParamsP *self) {
        self->OverlayBottomEditHide = self->subHandles[29]->GetBool("EditHide", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateOverlayBottomEditShow(MiscParamsP *self) {
        self->OverlayBottomEditShow = self->subHandles[29]->GetBool("EditShow", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateOverlayBottomTaskShow(MiscParamsP *self) {
        self->OverlayBottomTaskShow = self->subHandles[29]->GetBool("TaskShow", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateOverlayBottomClosed(MiscParamsP *self) {
        self->OverlayBottomClosed = self->subHandles[29]->GetBool("Closed", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateOverlayBottomTransparent(MiscParamsP *self) {
        self->OverlayBottomTransparent = self->subHandles[29]->GetBool("Transparent", false);
    }
};

// Auto generated code (Tools/params_utils.py:336)
MiscParamsP *instance() {
    static MiscParamsP *inst = new MiscParamsP;
    return inst;
}

} // Anonymous namespace

// Auto generated code (Tools/params_utils.py:352)
static const App::ParamRegistry::Registrar _MiscParamsRegistrar({
    App::ParamInfo("Gui", "MiscParams", "User parameter:BaseApp/Preferences/RecentMacros", "RecentMacros", "RecentMacros", App::ParamInfo::Int, 12)
        .setTitle("Size of recent macro list")
        .setDoc("Number of macros the recent macros menu lists."),
    App::ParamInfo("Gui", "MiscParams", "User parameter:BaseApp/Preferences/RecentMacros", "ShortcutCount", "ShortcutCount", App::ParamInfo::Int, 3)
        .setTitle("Recent macros with a shortcut")
        .setDoc("Number of entries of the recent macros menu that get a keyboard\n"
"shortcut, the modifiers below and a digit. At most 9."),
    App::ParamInfo("Gui", "MiscParams", "User parameter:BaseApp/Preferences/RecentMacros", "ShortcutModifiers", "ShortcutModifiers", App::ParamInfo::String, "Ctrl+Shift+")
        .setTitle("Recent macro shortcut modifiers")
        .setDoc("Modifier keys of the shortcuts of the recent macros menu, written\n"
"as in a shortcut and ending in +, such as Ctrl+Shift+."),
    App::ParamInfo("Gui", "MiscParams", "User parameter:BaseApp/Preferences/Gui/Gizmos", "CoarseLinearSnapMultiplier", "CoarseLinearSnapMultiplier", App::ParamInfo::Int, 5)
        .setTitle("Coarse linear step of a gizmo")
        .setDoc("How many times larger the step of a linear gizmo is while the\n"
"key for coarse steps is held."),
    App::ParamInfo("Gui", "MiscParams", "User parameter:BaseApp/Preferences/Gui/Gizmos", "CoarseRotationSnapMultiplier", "CoarseRotationSnapMultiplier", App::ParamInfo::Int, 5)
        .setTitle("Coarse rotation step of a gizmo")
        .setDoc("How many times larger the step of a rotation gizmo is while the\n"
"key for coarse steps is held."),
    App::ParamInfo("Gui", "MiscParams", "User parameter:BaseApp/Preferences/CacheDirectory", "CacheLimit", "Limit", App::ParamInfo::UInt, 500)
        .setTitle("Cache size limit")
        .setDoc("Size in megabytes the cache directory may grow to before the\n"
"program offers to clean it."),
    App::ParamInfo("Gui", "MiscParams", "User parameter:BaseApp/Preferences/CacheDirectory", "CachePeriod", "Period", App::ParamInfo::Int, 2)
        .setTitle("Cache check period")
        .setDoc("How often the size of the cache directory is checked: 0 always,\n"
"1 daily, 2 weekly, 3 monthly, 4 yearly, 5 never."),
    App::ParamInfo("Gui", "MiscParams", "User parameter:BaseApp/Preferences/Shortcut/Settings", "ShortcutTimeout", "ShortcutTimeout", App::ParamInfo::Int, 300)
        .setTitle("Shortcut sequence timeout")
        .setDoc("Milliseconds the program waits for the next key of a shortcut\n"
"made of several keys before it acts on what was typed."),
    App::ParamInfo("Gui", "MiscParams", "User parameter:BaseApp/Preferences/Workbenches", "ShowTabBar", "ShowTabBar", App::ParamInfo::Bool, false)
        .setTitle("Workbench tab bar")
        .setDoc("Show the workbenches as a bar of tabs instead of a drop-down\n"
"list."),
    App::ParamInfo("Gui", "MiscParams", "User parameter:BaseApp/Preferences/Workbenches", "TabBarShowText", "TabBarShowText", App::ParamInfo::Bool, false)
        .setTitle("Workbench tab bar text")
        .setDoc("Show the name of each workbench on its tab, beside its icon."),
    App::ParamInfo("Gui", "MiscParams", "User parameter:BaseApp/Preferences/Workbenches", "TabBarMaxLength", "TabBarMaxLength", App::ParamInfo::Int, 0)
        .setTitle("Workbench tab bar length")
        .setDoc("Room in pixels the workbench tab bar may take along the way its\n"
"tabs run. 0 takes what its tabs need."),
    App::ParamInfo("Gui", "MiscParams", "User parameter:BaseApp/Preferences/HighDPI", "DisableDpiScaling", "DisableDpiScaling", App::ParamInfo::Bool, false)
        .setTitle("Disable high DPI scaling")
        .setDoc("Switch Qt's scaling for high resolution screens off. Read at\n"
"startup."),
    App::ParamInfo("Gui", "MiscParams", "User parameter:BaseApp/Preferences/OpenGL", "UseSoftwareOpenGL", "UseSoftwareOpenGL", App::ParamInfo::Bool, false)
        .setTitle("Use software OpenGL")
        .setDoc("Draw with a software implementation of OpenGL instead of the\n"
"graphics driver. Read at startup."),
    App::ParamInfo("Gui", "MiscParams", "User parameter:BaseApp/Preferences/DependencyGraph", "Unflatten", "Unflatten", App::ParamInfo::Bool, true)
        .setTitle("Unflatten the dependency graph")
        .setDoc("Run the dependency graph through Graphviz's unflatten, which\n"
"makes wide graphs narrower."),
    App::ParamInfo("Gui", "MiscParams", "User parameter:BaseApp/Preferences/DependencyGraph", "GeoFeatureSubgraphs", "GeoFeatureSubgraphs", App::ParamInfo::Bool, true)
        .setTitle("Sub-graphs in the dependency graph")
        .setDoc("Draws the objects of each coordinate system -- a Part, a Body --\n"
"inside a box of its own in the dependency graph. On no page.\n"
"Takes effect when the graph is next drawn."),
    App::ParamInfo("Gui", "MiscParams", "User parameter:BaseApp/Preferences/Gui/Gizmos", "EnableGizmos", "EnableGizmos", App::ParamInfo::Bool, true)
        .setTitle("Show interactive draggers when editing features")
        .setDoc("Enables on-screen handles (draggers) in the 3D view for\n"
"interactively modifying dimensions and parameters of the feature\n"
"being edited by dragging."),
    App::ParamInfo("Gui", "MiscParams", "User parameter:BaseApp/Preferences/Gui/Gizmos", "DelayedGizmoUpdate", "DelayedGizmoUpdate", App::ParamInfo::Bool, false)
        .setTitle("Disable recompute while dragging")
        .setDoc("Prevents the model from recalculating while manipulating\n"
"draggers. The shape updates only after release of the mouse\n"
"button."),
    App::ParamInfo("Gui", "MiscParams", "User parameter:BaseApp/Preferences/Gui/Gizmos", "EnableCoarseSnap", "EnableCoarseSnap", App::ParamInfo::Bool, true)
        .setTitle("Enable coarse snapping while dragging")
        .setDoc("Enables larger snapping increments while manipulating draggers."),
    App::ParamInfo("Gui", "MiscParams", "User parameter:BaseApp/Preferences/Gui/Gizmos", "FineSnapModifier", "FineSnapModifier", App::ParamInfo::Int, 33554432)
        .setTitle("Fine snap modifier")
        .setDoc("Defines the modifier key used for fine snapping while dragging,\n"
"as Qt numbers it: 33554432 is Shift, 67108864 is Ctrl. Anything\n"
"else is taken for Shift."),
    App::ParamInfo("Gui", "MiscParams", "User parameter:BaseApp/Preferences/Gui/Gizmos", "DefaultCoarseDragBehavior", "DefaultCoarseDragBehavior", App::ParamInfo::Int, 0)
        .setTitle("Default coarse drag behavior")
        .setDoc("Determines whether the drag is coarse or fine without holding\n"
"the modifier key.")
        .setProxy("ComboBox")
        .setItems({{"Coarse", "", nullptr}, {"Fine", "", nullptr}}, false, true),
    App::ParamInfo("Gui", "MiscParams", "User parameter:BaseApp/Preferences/PropertyView", "PropertyViewAutoTransactionView", "AutoTransactionView", App::ParamInfo::Bool, false)
        .setTitle("Property view: undo for the View tab")
        .setDoc("An edit in the View tab of the property view opens an undo step of\n"
"its own and recomputes the document when it ends, as one in the\n"
"Data tab does. On no page."),
    App::ParamInfo("Gui", "MiscParams", "User parameter:BaseApp/Preferences/PropertyView", "PropertyViewAutoTransactionData", "AutoTransactionData", App::ParamInfo::Bool, true)
        .setTitle("Property view: undo for the Data tab")
        .setDoc("An edit in the Data tab of the property view opens an undo step of\n"
"its own and recomputes the document when it ends. On no page."),
    App::ParamInfo("Gui", "MiscParams", "User parameter:BaseApp/Preferences/PropertyView", "PropertyViewAutoExpandView", "AutoExpandView", App::ParamInfo::Bool, false)
        .setTitle("Property view: expand the View tab")
        .setDoc("The View tab of the property view starts a session with\n"
"everything unfolded: the groups, and the parts of a property that\n"
"has some. 'Auto expand' of the view's context menu switches it\n"
"for the session. On no page."),
    App::ParamInfo("Gui", "MiscParams", "User parameter:BaseApp/Preferences/PropertyView", "PropertyViewAutoExpandData", "AutoExpandData", App::ParamInfo::Bool, false)
        .setTitle("Property view: expand the Data tab")
        .setDoc("The Data tab of the property view starts a session with\n"
"everything unfolded: the groups, and the parts of a property that\n"
"has some. 'Auto expand' of the view's context menu switches it\n"
"for the session. On no page."),
    App::ParamInfo("Gui", "MiscParams", "User parameter:BaseApp/Preferences/PropertyView", "PropertyViewHideHeader", "HideHeader", App::ParamInfo::Bool, false)
        .setTitle("Property view: hide header")
        .setDoc("Hides the header row, Property and Value, of the property view.\n"
"Set by 'Hide header' of the view's context menu."),
    App::ParamInfo("Gui", "MiscParams", "User parameter:BaseApp/Preferences/PropertyView", "PropertyViewViewSectionSize", "ViewSectionSize", App::ParamInfo::Int, 150)
        .setTitle("Property view: name column of the View tab")
        .setDoc("Width in pixels of the Property column of the View tab. Stored\n"
"by the property view when the column is resized."),
    App::ParamInfo("Gui", "MiscParams", "User parameter:BaseApp/Preferences/PropertyView", "PropertyViewDataSectionSize", "DataSectionSize", App::ParamInfo::Int, 150)
        .setTitle("Property view: name column of the Data tab")
        .setDoc("Width in pixels of the Property column of the Data tab. Stored\n"
"by the property view when the column is resized."),
    App::ParamInfo("Gui", "MiscParams", "User parameter:BaseApp/Preferences/PropertyView", "PropertyViewLastTabIndex", "LastTabIndex", App::ParamInfo::Int, 1)
        .setTitle("Property view: last tab")
        .setDoc("The tab the property view was last on: 0 View, 1 Data. Stored\n"
"when the tab changes, read when the view is made."),
    App::ParamInfo("Gui", "MiscParams", "User parameter:BaseApp/Preferences/PropertyView", "NewPropertyType", "NewPropertyType", App::ParamInfo::String, "App::PropertyString")
        .setTitle("Add property: last type")
        .setDoc("The property type the Add Property dialog was last used with,\n"
"which it opens on. Stored when the dialog is accepted."),
    App::ParamInfo("Gui", "MiscParams", "User parameter:BaseApp/Preferences/PropertyView", "NewPropertyGroup", "NewPropertyGroup", App::ParamInfo::String, "Base")
        .setTitle("Add property: last group")
        .setDoc("The group the Add Property dialog last put a property in, which\n"
"it opens with. Stored when the dialog is accepted."),
    App::ParamInfo("Gui", "MiscParams", "User parameter:BaseApp/Preferences/PropertyView", "NewPropertyAppend", "NewPropertyAppend", App::ParamInfo::Bool, true)
        .setTitle("Add property: prefix the name with the group")
        .setDoc("The Add Property dialog opens with its box checked that puts the\n"
"group's name in front of the property's. Stored when the dialog\n"
"is accepted."),
    App::ParamInfo("Gui", "MiscParams", "User parameter:BaseApp/Preferences/Fw", "PanelMirror", "PanelMirror", App::ParamInfo::String, "all")
        .setTitle("Mirrored task dialogs")
        .setDoc("Which task dialogs a served session mirrors to its viewers: all,\n"
"none, or the class names of the dialogs separated by commas. On\n"
"no page. Takes effect at the next dialog."),
    App::ParamInfo("Gui", "MiscParams", "User parameter:BaseApp/Preferences/Fw", "PanelPollMs", "PanelPollMs", App::ParamInfo::Int, 500)
        .setTitle("Panel mirror poll interval (ms)")
        .setDoc("Milliseconds between two looks the panel mirror takes at a\n"
"mirrored task dialog for changes its widgets did not announce; 0\n"
"switches the polling off. On no page. Takes effect at the next\n"
"dialog."),
    App::ParamInfo("Gui", "MiscParams", "User parameter:BaseApp/Preferences/Selection", "AutoShowSelectionView", "AutoShowSelectionView", App::ParamInfo::Bool, false)
        .setTitle("Show the selection view on selection")
        .setDoc("Brings the selection view up when something is selected and\n"
"puts it away when the selection is empty. On no page. Takes\n"
"effect at the next change of the selection."),
    App::ParamInfo("Gui", "MiscParams", "User parameter:BaseApp/Preferences/Selection", "SingleClickFeatureSelect", "singleClickFeatureSelect", App::ParamInfo::Bool, true)
        .setTitle("Feature picker: accept on one click")
        .setDoc("In PartDesign's dialog that asks for a feature to work on, a\n"
"click on a feature picks it and goes on; off, the choice has to\n"
"be confirmed. On no page."),
    App::ParamInfo("Gui", "MiscParams", "User parameter:BaseApp/Preferences/DAGView", "DAGViewSelectionMode", "SelectionMode", App::ParamInfo::Int, 0)
        .setTitle("DAG view: selection mode")
        .setDoc("Whether a click in the DAG view selects one object in place of\n"
"the selection or adds to it. On no page. Read when the view is\n"
"made.")
        .setProxy("ComboBox")
        .setItems({{"Single", "", nullptr}, {"Multiple", "", nullptr}}, false, true),
    App::ParamInfo("Gui", "MiscParams", "User parameter:BaseApp/Preferences/DAGView", "DAGViewFontPointSize", "FontPointSize", App::ParamInfo::Int, 0)
        .setTitle("DAG view: font size")
        .setDoc("Font size of the DAG view in points; 0 is the application's. On\n"
"no page. Read when the view is made."),
    App::ParamInfo("Gui", "MiscParams", "User parameter:BaseApp/Preferences/DAGView", "DAGViewDirection", "Direction", App::ParamInfo::Float, 1.0)
        .setTitle("DAG view: direction")
        .setDoc("The direction the DAG view lists the objects in: 1, or -1 for the\n"
"other way up. Anything else is taken for 1. On no page. Read when\n"
"the view is made."),
    App::ParamInfo("Gui", "MiscParams", "User parameter:BaseApp/Preferences/View/Custom", "CustomViewQ0", "Q0", App::ParamInfo::Float, 0.0)
        .setTitle("Custom view orientation: q0")
        .setDoc("First component of the quaternion a new document's view is turned\n"
"to when its camera orientation is 'Custom'. Set by the dialog of\n"
"the Navigation page."),
    App::ParamInfo("Gui", "MiscParams", "User parameter:BaseApp/Preferences/View/Custom", "CustomViewQ1", "Q1", App::ParamInfo::Float, 0.0)
        .setTitle("Custom view orientation: q1")
        .setDoc("Second component of the quaternion of the 'Custom' camera\n"
"orientation of a new document."),
    App::ParamInfo("Gui", "MiscParams", "User parameter:BaseApp/Preferences/View/Custom", "CustomViewQ2", "Q2", App::ParamInfo::Float, 0.0)
        .setTitle("Custom view orientation: q2")
        .setDoc("Third component of the quaternion of the 'Custom' camera\n"
"orientation of a new document."),
    App::ParamInfo("Gui", "MiscParams", "User parameter:BaseApp/Preferences/View/Custom", "CustomViewQ3", "Q3", App::ParamInfo::Float, 1.0)
        .setTitle("Custom view orientation: q3")
        .setDoc("Fourth component of the quaternion of the 'Custom' camera\n"
"orientation of a new document."),
    App::ParamInfo("Gui", "MiscParams", "User parameter:BaseApp/Preferences/RecentFiles", "RecentFiles", "RecentFiles", App::ParamInfo::Int, 4)
        .setTitle("Size of the recent file list")
        .setDoc("How many files the recent files menu shows. The General page\n"
"has it. The list itself is kept beside it, a key per file, and is\n"
"not listed."),
    App::ParamInfo("Gui", "MiscParams", "User parameter:BaseApp/Preferences/Websites", "DonatePage", "DonatePage", App::ParamInfo::String, "https://wiki.freecad.org/Donate")
        .setTitle("Donation page")
        .setDoc("The address Help > Donate opens. The command stores what it read,\n"
"so the key is there after its first use."),
    App::ParamInfo("Gui", "MiscParams", "User parameter:BaseApp/Preferences/Paths", "Graphviz", "Graphviz", App::ParamInfo::String, "")
        .setTitle("Graphviz folder")
        .setDoc("Folder of the Graphviz programs the dependency graph is drawn\n"
"with. Not set, /usr/bin is tried on Linux and the search path\n"
"elsewhere; when that fails the program asks for the folder and\n"
"stores the answer here."),
    App::ParamInfo("Gui", "MiscParams", "User parameter:BaseApp/Preferences/Bitmaps/Theme", "IconThemeName", "Name", App::ParamInfo::String, "")
        .setTitle("Icon theme")
        .setDoc("Name of the icon theme Qt is told to use. Empty, the program's own\n"
"icons are used. On no page. Read at start."),
    App::ParamInfo("Gui", "MiscParams", "User parameter:BaseApp/Preferences/Bitmaps/Theme", "IconThemeSearchPath", "SearchPath", App::ParamInfo::String, "")
        .setTitle("Icon theme search path")
        .setDoc("A folder put in front of the places Qt looks for icon themes in.\n"
"On no page. Read at start."),
    App::ParamInfo("Gui", "MiscParams", "User parameter:BaseApp/Preferences/Bitmaps/Theme", "IconThemeSearchPaths", "_ThemeSearchPaths", App::ParamInfo::Bool, false)
        .setTitle("Use the desktop's icon themes")
        .setDoc("Linux only: leaves the desktop's icon theme and its search paths\n"
"in place, where the program otherwise uses its own icons alone.\n"
"It rarely works, the common themes lack most of the icons. On no\n"
"page. Read at start."),
    App::ParamInfo("Gui", "MiscParams", "User parameter:BaseApp/Preferences/DockWindows/TreeView", "TreeViewEnabled", "Enabled", App::ParamInfo::Bool, true)
        .setTitle("Tree view in a dock of its own")
        .setDoc("The model tree has a dock window of its own, with the property\n"
"view in another. 'Tree view mode' of the General page stores it\n"
"with the two below. Read when the main window sets its dock\n"
"windows up."),
    App::ParamInfo("Gui", "MiscParams", "User parameter:BaseApp/Preferences/DockWindows/PropertyView", "PropertyViewEnabled", "Enabled", App::ParamInfo::Bool, true)
        .setTitle("Property view in a dock of its own")
        .setDoc("The property view has a dock window of its own. It always has\n"
"one while the tree view does. Read when the main window sets its\n"
"dock windows up."),
    App::ParamInfo("Gui", "MiscParams", "User parameter:BaseApp/Preferences/DockWindows/ComboView", "ComboViewEnabled", "Enabled", App::ParamInfo::Bool, false)
        .setTitle("Combo view")
        .setDoc("The combo view -- model tree and property view in one dock window\n"
"-- is there. Read when the main window sets its dock windows up."),
    App::ParamInfo("Gui", "MiscParams", "User parameter:BaseApp/Preferences/DockWindows/TaskWatcher", "TaskWatcherEnabled", "Enabled", App::ParamInfo::Bool, false)
        .setTitle("Separate task list from task view")
        .setDoc("The list of tasks of the active workbench has a dock window of its\n"
"own, apart from the task view. The General page has it. Read\n"
"when the main window sets its dock windows up."),
    App::ParamInfo("Gui", "MiscParams", "User parameter:BaseApp/Preferences/DockWindows/DAGView", "DAGViewEnabled", "Enabled", App::ParamInfo::Bool, false)
        .setTitle("DAG view")
        .setDoc("The DAG view, a dock window that shows the objects of the document\n"
"as a dependency graph, is there. On no page. Read when the main\n"
"window sets its dock windows up."),
    App::ParamInfo("Gui", "MiscParams", "User parameter:BaseApp/Preferences/DockWindows/ComboView", "ComboViewTreeViewSize", "TreeViewSize", App::ParamInfo::Int, 0)
        .setTitle("Combo view: height of the tree")
        .setDoc("Height in pixels the model tree last had in the combo view; 0\n"
"leaves it to the layout. Stored by the program."),
    App::ParamInfo("Gui", "MiscParams", "User parameter:BaseApp/Preferences/DockWindows/ComboView", "ComboViewPropertyViewSize", "PropertyViewSize", App::ParamInfo::Int, 0)
        .setTitle("Combo view: height of the property view")
        .setDoc("Height in pixels the property view last had in the combo view; 0\n"
"leaves it to the layout. Stored by the program."),
    App::ParamInfo("Gui", "MiscParams", "User parameter:BaseApp/Preferences/Placement", "PlacementRotationMethod", "RotationMethod", App::ParamInfo::Int, 0)
        .setTitle("Placement dialog: last rotation input")
        .setDoc("The way of giving a rotation the Placement dialog was last on, as\n"
"the number of its entry. Stored by the dialog."),
    App::ParamInfo("Gui", "MiscParams", "User parameter:BaseApp/Preferences/SceneShare", "SharePort", "Port", App::ParamInfo::Int, 8210)
        .setTitle("Share: port")
        .setDoc("The port the scene server listens on when a document is shared."),
    App::ParamInfo("Gui", "MiscParams", "User parameter:BaseApp/Preferences/SceneShare", "ShareDoor", "Door", App::ParamInfo::String, "")
        .setTitle("Share: front door")
        .setDoc("Name of the front door last chosen in the Share dialog: one of\n"
"the doors kept beside this setting, each a way viewers reach\n"
"this machine."),
    App::ParamInfo("Gui", "MiscParams", "User parameter:BaseApp/Preferences/SceneShare", "ShareExternalHost", "ExternalHost", App::ParamInfo::String, "")
        .setTitle("Share: address of this machine")
        .setDoc("The address viewers reach this machine at, as the share links\n"
"carry it. Empty, the address the program finds itself is used."),
    App::ParamInfo("Gui", "MiscParams", "User parameter:BaseApp/Preferences/SceneShare", "ShareViewerPage", "ViewerPage", App::ParamInfo::String, "")
        .setTitle("Share: viewer page")
        .setDoc("Address of the viewer page the share links point at. Empty, the\n"
"page the program serves itself is used."),
    App::ParamInfo("Gui", "MiscParams", "User parameter:BaseApp/Preferences/SceneShare", "ShareTrustProxy", "TrustProxy", App::ParamInfo::Bool, false)
        .setTitle("Share: trust the proxy for client addresses")
        .setDoc("Takes the address of a viewer from the X-Forwarded-For header a\n"
"proxy in front of this machine adds, instead of the address the\n"
"connection comes from. Only for a door on the local network."),
    App::ParamInfo("Gui", "MiscParams", "User parameter:BaseApp/Preferences/SceneShare", "ShareDoorsSeeded", "DoorsSeeded", App::ParamInfo::Bool, false)
        .setTitle("Share: doors made")
        .setDoc("The program has made the first front doors of the Share dialog.\n"
"Stored by the program so that it does it once."),
    App::ParamInfo("Gui", "MiscParams", "User parameter:BaseApp/Preferences/SceneShare", "ShareGrantsMigrated", "GrantsMigrated", App::ParamInfo::Bool, false)
        .setTitle("Share: grants taken over")
        .setDoc("The program has turned what an older version kept -- one token\n"
"and a list of clients -- into grants. Stored by the program so\n"
"that it does it once."),
    App::ParamInfo("Gui", "MiscParams", "User parameter:BaseApp/History/Dragger", "DraggerLastTranslationIncrement", "LastTranslationIncrement", App::ParamInfo::Float, 1.0)
        .setTitle("Transform: last translation increment")
        .setDoc("The translation increment the Transform task panel was last left\n"
"with. Stored when the panel is accepted."),
    App::ParamInfo("Gui", "MiscParams", "User parameter:BaseApp/History/Dragger", "DraggerLastRotationIncrement", "LastRotationIncrement", App::ParamInfo::Float, 15.0)
        .setTitle("Transform: last rotation increment")
        .setDoc("The rotation increment, in degrees, the Transform task panel was\n"
"last left with. Stored when the panel is accepted."),
    App::ParamInfo("Gui", "MiscParams", "User parameter:BaseApp/MainWindow/DockWindows", "ActivateOverlay", "ActivateOverlay", App::ParamInfo::Bool, true)
        .setTitle("Overlay docks")
        .setDoc("Sets the overlay management of the dock windows up: with it a\n"
"dock window can be laid over the 3D view. On no page. Read at\n"
"start."),
    App::ParamInfo("Gui", "MiscParams", "User parameter:BaseApp/MainWindow/DockWindows", "DockCursorMargin", "CursorMargin", App::ParamInfo::Int, 5)
        .setTitle("Dock window edge margin")
        .setDoc("Distance in pixels from the edge of a dock window within which\n"
"the mouse counts as on the edge, for resizing an overlaid one.\n"
"On no page. Takes effect at once."),
    App::ParamInfo("Gui", "MiscParams", "User parameter:BaseApp/MainWindow/DockWindows", "DockStdTreeView", "Std_TreeView", App::ParamInfo::Bool, true)
        .setTitle("Tree view dock shown")
        .setDoc("The tree view dock was last shown. Kept by the program; it also\n"
"decides whether the tree view has a dock of its own while that\n"
"setting is not stored."),
    App::ParamInfo("Gui", "MiscParams", "User parameter:BaseApp/MainWindow/DockWindows", "DockStdPropertyView", "Std_PropertyView", App::ParamInfo::Bool, false)
        .setTitle("Property view dock shown")
        .setDoc("The property view dock was last shown. Kept by the program; it\n"
"also decides whether the property view has a dock of its own\n"
"while that setting is not stored."),
    App::ParamInfo("Gui", "MiscParams", "User parameter:BaseApp/MainWindow/DockWindows/OverlayLeft", "OverlayLeftWidgets", "Widgets", App::ParamInfo::String, "")
        .setTitle("Overlay left: dock windows")
        .setDoc("The names of the dock windows laid over the left side of the 3D view,\n"
"separated by commas, in the order of their tabs."),
    App::ParamInfo("Gui", "MiscParams", "User parameter:BaseApp/MainWindow/DockWindows/OverlayLeft", "OverlayLeftWidth", "Width", App::ParamInfo::Int, 0)
        .setTitle("Overlay left: width")
        .setDoc("Width in pixels of the overlay panel of the left side; 0 while it has\n"
"never been sized."),
    App::ParamInfo("Gui", "MiscParams", "User parameter:BaseApp/MainWindow/DockWindows/OverlayLeft", "OverlayLeftHeight", "Height", App::ParamInfo::Int, 0)
        .setTitle("Overlay left: height")
        .setDoc("Height in pixels of the overlay panel of the left side; 0 while it has\n"
"never been sized."),
    App::ParamInfo("Gui", "MiscParams", "User parameter:BaseApp/MainWindow/DockWindows/OverlayLeft", "OverlayLeftOffset1", "Offset1", App::ParamInfo::Int, 0)
        .setTitle("Overlay left: first offset")
        .setDoc("First of the two offsets, in pixels, the overlay panel of the left side\n"
"is placed with."),
    App::ParamInfo("Gui", "MiscParams", "User parameter:BaseApp/MainWindow/DockWindows/OverlayLeft", "OverlayLeftOffset3", "Offset3", App::ParamInfo::Int, 0)
        .setTitle("Overlay left: second offset")
        .setDoc("Second of the two offsets, in pixels, the overlay panel of the left side\n"
"is placed with."),
    App::ParamInfo("Gui", "MiscParams", "User parameter:BaseApp/MainWindow/DockWindows/OverlayLeft", "OverlayLeftOffset2", "Offset2", App::ParamInfo::Int, 0)
        .setTitle("Overlay left: size change")
        .setDoc("Pixels the size of the overlay panel of the left side is changed by."),
    App::ParamInfo("Gui", "MiscParams", "User parameter:BaseApp/MainWindow/DockWindows/OverlayLeft", "OverlayLeftSizes", "Sizes", App::ParamInfo::String, "")
        .setTitle("Overlay left: sizes of its dock windows")
        .setDoc("The sizes in pixels of the dock windows in the overlay panel of this\n"
"side, separated by commas, in the order of their tabs."),
    App::ParamInfo("Gui", "MiscParams", "User parameter:BaseApp/MainWindow/DockWindows/OverlayLeft", "OverlayLeftAutoHide", "AutoHide", App::ParamInfo::Bool, false)
        .setTitle("Overlay left: auto hide")
        .setDoc("The overlay panel of the left side hides while the mouse is away from\n"
"it. Of the four modes -- this one, EditHide, EditShow, TaskShow --\n"
"the first that is on counts."),
    App::ParamInfo("Gui", "MiscParams", "User parameter:BaseApp/MainWindow/DockWindows/OverlayLeft", "OverlayLeftEditHide", "EditHide", App::ParamInfo::Bool, false)
        .setTitle("Overlay left: hide on editing")
        .setDoc("The overlay panel of the left side hides while an object is edited."),
    App::ParamInfo("Gui", "MiscParams", "User parameter:BaseApp/MainWindow/DockWindows/OverlayLeft", "OverlayLeftEditShow", "EditShow", App::ParamInfo::Bool, false)
        .setTitle("Overlay left: show on editing")
        .setDoc("The overlay panel of the left side shows only while an object is\n"
"edited."),
    App::ParamInfo("Gui", "MiscParams", "User parameter:BaseApp/MainWindow/DockWindows/OverlayLeft", "OverlayLeftTaskShow", "TaskShow", App::ParamInfo::Bool, false)
        .setTitle("Overlay left: show on task")
        .setDoc("The overlay panel of the left side shows only while a task dialog is\n"
"open."),
    App::ParamInfo("Gui", "MiscParams", "User parameter:BaseApp/MainWindow/DockWindows/OverlayLeft", "OverlayLeftClosed", "Closed", App::ParamInfo::Bool, false)
        .setTitle("Overlay left: closed")
        .setDoc("The overlay panel of the left side was last hidden by the user. Counts\n"
"only while none of its automatic modes is on."),
    App::ParamInfo("Gui", "MiscParams", "User parameter:BaseApp/MainWindow/DockWindows/OverlayLeft", "OverlayLeftTransparent", "Transparent", App::ParamInfo::Bool, false)
        .setTitle("Overlay left: transparent")
        .setDoc("The overlay panel of the left side lets the 3D view show through."),
    App::ParamInfo("Gui", "MiscParams", "User parameter:BaseApp/MainWindow/DockWindows/OverlayRight", "OverlayRightWidgets", "Widgets", App::ParamInfo::String, "")
        .setTitle("Overlay right: dock windows")
        .setDoc("The names of the dock windows laid over the right side of the 3D view,\n"
"separated by commas, in the order of their tabs."),
    App::ParamInfo("Gui", "MiscParams", "User parameter:BaseApp/MainWindow/DockWindows/OverlayRight", "OverlayRightWidth", "Width", App::ParamInfo::Int, 0)
        .setTitle("Overlay right: width")
        .setDoc("Width in pixels of the overlay panel of the right side; 0 while it has\n"
"never been sized."),
    App::ParamInfo("Gui", "MiscParams", "User parameter:BaseApp/MainWindow/DockWindows/OverlayRight", "OverlayRightHeight", "Height", App::ParamInfo::Int, 0)
        .setTitle("Overlay right: height")
        .setDoc("Height in pixels of the overlay panel of the right side; 0 while it has\n"
"never been sized."),
    App::ParamInfo("Gui", "MiscParams", "User parameter:BaseApp/MainWindow/DockWindows/OverlayRight", "OverlayRightOffset1", "Offset1", App::ParamInfo::Int, 0)
        .setTitle("Overlay right: first offset")
        .setDoc("First of the two offsets, in pixels, the overlay panel of the right side\n"
"is placed with."),
    App::ParamInfo("Gui", "MiscParams", "User parameter:BaseApp/MainWindow/DockWindows/OverlayRight", "OverlayRightOffset3", "Offset3", App::ParamInfo::Int, 0)
        .setTitle("Overlay right: second offset")
        .setDoc("Second of the two offsets, in pixels, the overlay panel of the right side\n"
"is placed with."),
    App::ParamInfo("Gui", "MiscParams", "User parameter:BaseApp/MainWindow/DockWindows/OverlayRight", "OverlayRightOffset2", "Offset2", App::ParamInfo::Int, 0)
        .setTitle("Overlay right: size change")
        .setDoc("Pixels the size of the overlay panel of the right side is changed by."),
    App::ParamInfo("Gui", "MiscParams", "User parameter:BaseApp/MainWindow/DockWindows/OverlayRight", "OverlayRightSizes", "Sizes", App::ParamInfo::String, "")
        .setTitle("Overlay right: sizes of its dock windows")
        .setDoc("The sizes in pixels of the dock windows in the overlay panel of this\n"
"side, separated by commas, in the order of their tabs."),
    App::ParamInfo("Gui", "MiscParams", "User parameter:BaseApp/MainWindow/DockWindows/OverlayRight", "OverlayRightAutoHide", "AutoHide", App::ParamInfo::Bool, false)
        .setTitle("Overlay right: auto hide")
        .setDoc("The overlay panel of the right side hides while the mouse is away from\n"
"it. Of the four modes -- this one, EditHide, EditShow, TaskShow --\n"
"the first that is on counts."),
    App::ParamInfo("Gui", "MiscParams", "User parameter:BaseApp/MainWindow/DockWindows/OverlayRight", "OverlayRightEditHide", "EditHide", App::ParamInfo::Bool, false)
        .setTitle("Overlay right: hide on editing")
        .setDoc("The overlay panel of the right side hides while an object is edited."),
    App::ParamInfo("Gui", "MiscParams", "User parameter:BaseApp/MainWindow/DockWindows/OverlayRight", "OverlayRightEditShow", "EditShow", App::ParamInfo::Bool, false)
        .setTitle("Overlay right: show on editing")
        .setDoc("The overlay panel of the right side shows only while an object is\n"
"edited."),
    App::ParamInfo("Gui", "MiscParams", "User parameter:BaseApp/MainWindow/DockWindows/OverlayRight", "OverlayRightTaskShow", "TaskShow", App::ParamInfo::Bool, false)
        .setTitle("Overlay right: show on task")
        .setDoc("The overlay panel of the right side shows only while a task dialog is\n"
"open."),
    App::ParamInfo("Gui", "MiscParams", "User parameter:BaseApp/MainWindow/DockWindows/OverlayRight", "OverlayRightClosed", "Closed", App::ParamInfo::Bool, false)
        .setTitle("Overlay right: closed")
        .setDoc("The overlay panel of the right side was last hidden by the user. Counts\n"
"only while none of its automatic modes is on."),
    App::ParamInfo("Gui", "MiscParams", "User parameter:BaseApp/MainWindow/DockWindows/OverlayRight", "OverlayRightTransparent", "Transparent", App::ParamInfo::Bool, false)
        .setTitle("Overlay right: transparent")
        .setDoc("The overlay panel of the right side lets the 3D view show through."),
    App::ParamInfo("Gui", "MiscParams", "User parameter:BaseApp/MainWindow/DockWindows/OverlayTop", "OverlayTopWidgets", "Widgets", App::ParamInfo::String, "")
        .setTitle("Overlay top: dock windows")
        .setDoc("The names of the dock windows laid over the top side of the 3D view,\n"
"separated by commas, in the order of their tabs."),
    App::ParamInfo("Gui", "MiscParams", "User parameter:BaseApp/MainWindow/DockWindows/OverlayTop", "OverlayTopWidth", "Width", App::ParamInfo::Int, 0)
        .setTitle("Overlay top: width")
        .setDoc("Width in pixels of the overlay panel of the top side; 0 while it has\n"
"never been sized."),
    App::ParamInfo("Gui", "MiscParams", "User parameter:BaseApp/MainWindow/DockWindows/OverlayTop", "OverlayTopHeight", "Height", App::ParamInfo::Int, 0)
        .setTitle("Overlay top: height")
        .setDoc("Height in pixels of the overlay panel of the top side; 0 while it has\n"
"never been sized."),
    App::ParamInfo("Gui", "MiscParams", "User parameter:BaseApp/MainWindow/DockWindows/OverlayTop", "OverlayTopOffset1", "Offset1", App::ParamInfo::Int, 0)
        .setTitle("Overlay top: first offset")
        .setDoc("First of the two offsets, in pixels, the overlay panel of the top side\n"
"is placed with."),
    App::ParamInfo("Gui", "MiscParams", "User parameter:BaseApp/MainWindow/DockWindows/OverlayTop", "OverlayTopOffset3", "Offset3", App::ParamInfo::Int, 0)
        .setTitle("Overlay top: second offset")
        .setDoc("Second of the two offsets, in pixels, the overlay panel of the top side\n"
"is placed with."),
    App::ParamInfo("Gui", "MiscParams", "User parameter:BaseApp/MainWindow/DockWindows/OverlayTop", "OverlayTopOffset2", "Offset2", App::ParamInfo::Int, 0)
        .setTitle("Overlay top: size change")
        .setDoc("Pixels the size of the overlay panel of the top side is changed by."),
    App::ParamInfo("Gui", "MiscParams", "User parameter:BaseApp/MainWindow/DockWindows/OverlayTop", "OverlayTopSizes", "Sizes", App::ParamInfo::String, "")
        .setTitle("Overlay top: sizes of its dock windows")
        .setDoc("The sizes in pixels of the dock windows in the overlay panel of this\n"
"side, separated by commas, in the order of their tabs."),
    App::ParamInfo("Gui", "MiscParams", "User parameter:BaseApp/MainWindow/DockWindows/OverlayTop", "OverlayTopAutoHide", "AutoHide", App::ParamInfo::Bool, false)
        .setTitle("Overlay top: auto hide")
        .setDoc("The overlay panel of the top side hides while the mouse is away from\n"
"it. Of the four modes -- this one, EditHide, EditShow, TaskShow --\n"
"the first that is on counts."),
    App::ParamInfo("Gui", "MiscParams", "User parameter:BaseApp/MainWindow/DockWindows/OverlayTop", "OverlayTopEditHide", "EditHide", App::ParamInfo::Bool, false)
        .setTitle("Overlay top: hide on editing")
        .setDoc("The overlay panel of the top side hides while an object is edited."),
    App::ParamInfo("Gui", "MiscParams", "User parameter:BaseApp/MainWindow/DockWindows/OverlayTop", "OverlayTopEditShow", "EditShow", App::ParamInfo::Bool, false)
        .setTitle("Overlay top: show on editing")
        .setDoc("The overlay panel of the top side shows only while an object is\n"
"edited."),
    App::ParamInfo("Gui", "MiscParams", "User parameter:BaseApp/MainWindow/DockWindows/OverlayTop", "OverlayTopTaskShow", "TaskShow", App::ParamInfo::Bool, false)
        .setTitle("Overlay top: show on task")
        .setDoc("The overlay panel of the top side shows only while a task dialog is\n"
"open."),
    App::ParamInfo("Gui", "MiscParams", "User parameter:BaseApp/MainWindow/DockWindows/OverlayTop", "OverlayTopClosed", "Closed", App::ParamInfo::Bool, false)
        .setTitle("Overlay top: closed")
        .setDoc("The overlay panel of the top side was last hidden by the user. Counts\n"
"only while none of its automatic modes is on."),
    App::ParamInfo("Gui", "MiscParams", "User parameter:BaseApp/MainWindow/DockWindows/OverlayTop", "OverlayTopTransparent", "Transparent", App::ParamInfo::Bool, false)
        .setTitle("Overlay top: transparent")
        .setDoc("The overlay panel of the top side lets the 3D view show through."),
    App::ParamInfo("Gui", "MiscParams", "User parameter:BaseApp/MainWindow/DockWindows/OverlayBottom", "OverlayBottomWidgets", "Widgets", App::ParamInfo::String, "")
        .setTitle("Overlay bottom: dock windows")
        .setDoc("The names of the dock windows laid over the bottom side of the 3D view,\n"
"separated by commas, in the order of their tabs."),
    App::ParamInfo("Gui", "MiscParams", "User parameter:BaseApp/MainWindow/DockWindows/OverlayBottom", "OverlayBottomWidth", "Width", App::ParamInfo::Int, 0)
        .setTitle("Overlay bottom: width")
        .setDoc("Width in pixels of the overlay panel of the bottom side; 0 while it has\n"
"never been sized."),
    App::ParamInfo("Gui", "MiscParams", "User parameter:BaseApp/MainWindow/DockWindows/OverlayBottom", "OverlayBottomHeight", "Height", App::ParamInfo::Int, 0)
        .setTitle("Overlay bottom: height")
        .setDoc("Height in pixels of the overlay panel of the bottom side; 0 while it has\n"
"never been sized."),
    App::ParamInfo("Gui", "MiscParams", "User parameter:BaseApp/MainWindow/DockWindows/OverlayBottom", "OverlayBottomOffset1", "Offset1", App::ParamInfo::Int, 0)
        .setTitle("Overlay bottom: first offset")
        .setDoc("First of the two offsets, in pixels, the overlay panel of the bottom side\n"
"is placed with."),
    App::ParamInfo("Gui", "MiscParams", "User parameter:BaseApp/MainWindow/DockWindows/OverlayBottom", "OverlayBottomOffset3", "Offset3", App::ParamInfo::Int, 0)
        .setTitle("Overlay bottom: second offset")
        .setDoc("Second of the two offsets, in pixels, the overlay panel of the bottom side\n"
"is placed with."),
    App::ParamInfo("Gui", "MiscParams", "User parameter:BaseApp/MainWindow/DockWindows/OverlayBottom", "OverlayBottomOffset2", "Offset2", App::ParamInfo::Int, 0)
        .setTitle("Overlay bottom: size change")
        .setDoc("Pixels the size of the overlay panel of the bottom side is changed by."),
    App::ParamInfo("Gui", "MiscParams", "User parameter:BaseApp/MainWindow/DockWindows/OverlayBottom", "OverlayBottomSizes", "Sizes", App::ParamInfo::String, "")
        .setTitle("Overlay bottom: sizes of its dock windows")
        .setDoc("The sizes in pixels of the dock windows in the overlay panel of this\n"
"side, separated by commas, in the order of their tabs."),
    App::ParamInfo("Gui", "MiscParams", "User parameter:BaseApp/MainWindow/DockWindows/OverlayBottom", "OverlayBottomAutoHide", "AutoHide", App::ParamInfo::Bool, false)
        .setTitle("Overlay bottom: auto hide")
        .setDoc("The overlay panel of the bottom side hides while the mouse is away from\n"
"it. Of the four modes -- this one, EditHide, EditShow, TaskShow --\n"
"the first that is on counts."),
    App::ParamInfo("Gui", "MiscParams", "User parameter:BaseApp/MainWindow/DockWindows/OverlayBottom", "OverlayBottomEditHide", "EditHide", App::ParamInfo::Bool, false)
        .setTitle("Overlay bottom: hide on editing")
        .setDoc("The overlay panel of the bottom side hides while an object is edited."),
    App::ParamInfo("Gui", "MiscParams", "User parameter:BaseApp/MainWindow/DockWindows/OverlayBottom", "OverlayBottomEditShow", "EditShow", App::ParamInfo::Bool, false)
        .setTitle("Overlay bottom: show on editing")
        .setDoc("The overlay panel of the bottom side shows only while an object is\n"
"edited."),
    App::ParamInfo("Gui", "MiscParams", "User parameter:BaseApp/MainWindow/DockWindows/OverlayBottom", "OverlayBottomTaskShow", "TaskShow", App::ParamInfo::Bool, false)
        .setTitle("Overlay bottom: show on task")
        .setDoc("The overlay panel of the bottom side shows only while a task dialog is\n"
"open."),
    App::ParamInfo("Gui", "MiscParams", "User parameter:BaseApp/MainWindow/DockWindows/OverlayBottom", "OverlayBottomClosed", "Closed", App::ParamInfo::Bool, false)
        .setTitle("Overlay bottom: closed")
        .setDoc("The overlay panel of the bottom side was last hidden by the user. Counts\n"
"only while none of its automatic modes is on."),
    App::ParamInfo("Gui", "MiscParams", "User parameter:BaseApp/MainWindow/DockWindows/OverlayBottom", "OverlayBottomTransparent", "Transparent", App::ParamInfo::Bool, false)
        .setTitle("Overlay bottom: transparent")
        .setDoc("The overlay panel of the bottom side lets the 3D view show through."),
});

// Auto generated code (Tools/params_utils.py:368)
ParameterGrp::handle MiscParams::getHandle() {
    return instance()->handle;
}

// Auto generated code (Tools/params_utils.py:397)
const char *MiscParams::docRecentMacros() {
    return QT_TRANSLATE_NOOP("MiscParams",
"Number of macros the recent macros menu lists.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & MiscParams::getRecentMacros() {
    return instance()->RecentMacros;
}

// Auto generated code (Tools/params_utils.py:413)
const long & MiscParams::defaultRecentMacros() {
    const static long def = 12;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MiscParams::setRecentMacros(const long &v) {
    instance()->subHandles[0]->SetInt("RecentMacros",v);
    instance()->RecentMacros = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MiscParams::removeRecentMacros() {
    instance()->subHandles[0]->RemoveInt("RecentMacros");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MiscParams::docShortcutCount() {
    return QT_TRANSLATE_NOOP("MiscParams",
"Number of entries of the recent macros menu that get a keyboard\n"
"shortcut, the modifiers below and a digit. At most 9.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & MiscParams::getShortcutCount() {
    return instance()->ShortcutCount;
}

// Auto generated code (Tools/params_utils.py:413)
const long & MiscParams::defaultShortcutCount() {
    const static long def = 3;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MiscParams::setShortcutCount(const long &v) {
    instance()->subHandles[0]->SetInt("ShortcutCount",v);
    instance()->ShortcutCount = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MiscParams::removeShortcutCount() {
    instance()->subHandles[0]->RemoveInt("ShortcutCount");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MiscParams::docShortcutModifiers() {
    return QT_TRANSLATE_NOOP("MiscParams",
"Modifier keys of the shortcuts of the recent macros menu, written\n"
"as in a shortcut and ending in +, such as Ctrl+Shift+.");
}

// Auto generated code (Tools/params_utils.py:405)
const std::string & MiscParams::getShortcutModifiers() {
    return instance()->ShortcutModifiers;
}

// Auto generated code (Tools/params_utils.py:413)
const std::string & MiscParams::defaultShortcutModifiers() {
    const static std::string def = "Ctrl+Shift+";
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MiscParams::setShortcutModifiers(const std::string &v) {
    instance()->subHandles[0]->SetASCII("ShortcutModifiers",v);
    instance()->ShortcutModifiers = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MiscParams::removeShortcutModifiers() {
    instance()->subHandles[0]->RemoveASCII("ShortcutModifiers");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MiscParams::docCoarseLinearSnapMultiplier() {
    return QT_TRANSLATE_NOOP("MiscParams",
"How many times larger the step of a linear gizmo is while the\n"
"key for coarse steps is held.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & MiscParams::getCoarseLinearSnapMultiplier() {
    return instance()->CoarseLinearSnapMultiplier;
}

// Auto generated code (Tools/params_utils.py:413)
const long & MiscParams::defaultCoarseLinearSnapMultiplier() {
    const static long def = 5;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MiscParams::setCoarseLinearSnapMultiplier(const long &v) {
    instance()->subHandles[1]->SetInt("CoarseLinearSnapMultiplier",v);
    instance()->CoarseLinearSnapMultiplier = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MiscParams::removeCoarseLinearSnapMultiplier() {
    instance()->subHandles[1]->RemoveInt("CoarseLinearSnapMultiplier");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MiscParams::docCoarseRotationSnapMultiplier() {
    return QT_TRANSLATE_NOOP("MiscParams",
"How many times larger the step of a rotation gizmo is while the\n"
"key for coarse steps is held.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & MiscParams::getCoarseRotationSnapMultiplier() {
    return instance()->CoarseRotationSnapMultiplier;
}

// Auto generated code (Tools/params_utils.py:413)
const long & MiscParams::defaultCoarseRotationSnapMultiplier() {
    const static long def = 5;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MiscParams::setCoarseRotationSnapMultiplier(const long &v) {
    instance()->subHandles[1]->SetInt("CoarseRotationSnapMultiplier",v);
    instance()->CoarseRotationSnapMultiplier = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MiscParams::removeCoarseRotationSnapMultiplier() {
    instance()->subHandles[1]->RemoveInt("CoarseRotationSnapMultiplier");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MiscParams::docCacheLimit() {
    return QT_TRANSLATE_NOOP("MiscParams",
"Size in megabytes the cache directory may grow to before the\n"
"program offers to clean it.");
}

// Auto generated code (Tools/params_utils.py:405)
const unsigned long & MiscParams::getCacheLimit() {
    return instance()->CacheLimit;
}

// Auto generated code (Tools/params_utils.py:413)
const unsigned long & MiscParams::defaultCacheLimit() {
    const static unsigned long def = 500;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MiscParams::setCacheLimit(const unsigned long &v) {
    instance()->subHandles[2]->SetUnsigned("Limit",v);
    instance()->CacheLimit = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MiscParams::removeCacheLimit() {
    instance()->subHandles[2]->RemoveUnsigned("Limit");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MiscParams::docCachePeriod() {
    return QT_TRANSLATE_NOOP("MiscParams",
"How often the size of the cache directory is checked: 0 always,\n"
"1 daily, 2 weekly, 3 monthly, 4 yearly, 5 never.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & MiscParams::getCachePeriod() {
    return instance()->CachePeriod;
}

// Auto generated code (Tools/params_utils.py:413)
const long & MiscParams::defaultCachePeriod() {
    const static long def = 2;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MiscParams::setCachePeriod(const long &v) {
    instance()->subHandles[2]->SetInt("Period",v);
    instance()->CachePeriod = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MiscParams::removeCachePeriod() {
    instance()->subHandles[2]->RemoveInt("Period");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MiscParams::docShortcutTimeout() {
    return QT_TRANSLATE_NOOP("MiscParams",
"Milliseconds the program waits for the next key of a shortcut\n"
"made of several keys before it acts on what was typed.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & MiscParams::getShortcutTimeout() {
    return instance()->ShortcutTimeout;
}

// Auto generated code (Tools/params_utils.py:413)
const long & MiscParams::defaultShortcutTimeout() {
    const static long def = 300;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MiscParams::setShortcutTimeout(const long &v) {
    instance()->subHandles[3]->SetInt("ShortcutTimeout",v);
    instance()->ShortcutTimeout = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MiscParams::removeShortcutTimeout() {
    instance()->subHandles[3]->RemoveInt("ShortcutTimeout");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MiscParams::docShowTabBar() {
    return QT_TRANSLATE_NOOP("MiscParams",
"Show the workbenches as a bar of tabs instead of a drop-down\n"
"list.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & MiscParams::getShowTabBar() {
    return instance()->ShowTabBar;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & MiscParams::defaultShowTabBar() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MiscParams::setShowTabBar(const bool &v) {
    instance()->subHandles[4]->SetBool("ShowTabBar",v);
    instance()->ShowTabBar = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MiscParams::removeShowTabBar() {
    instance()->subHandles[4]->RemoveBool("ShowTabBar");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MiscParams::docTabBarShowText() {
    return QT_TRANSLATE_NOOP("MiscParams",
"Show the name of each workbench on its tab, beside its icon.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & MiscParams::getTabBarShowText() {
    return instance()->TabBarShowText;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & MiscParams::defaultTabBarShowText() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MiscParams::setTabBarShowText(const bool &v) {
    instance()->subHandles[4]->SetBool("TabBarShowText",v);
    instance()->TabBarShowText = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MiscParams::removeTabBarShowText() {
    instance()->subHandles[4]->RemoveBool("TabBarShowText");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MiscParams::docTabBarMaxLength() {
    return QT_TRANSLATE_NOOP("MiscParams",
"Room in pixels the workbench tab bar may take along the way its\n"
"tabs run. 0 takes what its tabs need.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & MiscParams::getTabBarMaxLength() {
    return instance()->TabBarMaxLength;
}

// Auto generated code (Tools/params_utils.py:413)
const long & MiscParams::defaultTabBarMaxLength() {
    const static long def = 0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MiscParams::setTabBarMaxLength(const long &v) {
    instance()->subHandles[4]->SetInt("TabBarMaxLength",v);
    instance()->TabBarMaxLength = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MiscParams::removeTabBarMaxLength() {
    instance()->subHandles[4]->RemoveInt("TabBarMaxLength");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MiscParams::docDisableDpiScaling() {
    return QT_TRANSLATE_NOOP("MiscParams",
"Switch Qt's scaling for high resolution screens off. Read at\n"
"startup.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & MiscParams::getDisableDpiScaling() {
    return instance()->DisableDpiScaling;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & MiscParams::defaultDisableDpiScaling() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MiscParams::setDisableDpiScaling(const bool &v) {
    instance()->subHandles[5]->SetBool("DisableDpiScaling",v);
    instance()->DisableDpiScaling = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MiscParams::removeDisableDpiScaling() {
    instance()->subHandles[5]->RemoveBool("DisableDpiScaling");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MiscParams::docUseSoftwareOpenGL() {
    return QT_TRANSLATE_NOOP("MiscParams",
"Draw with a software implementation of OpenGL instead of the\n"
"graphics driver. Read at startup.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & MiscParams::getUseSoftwareOpenGL() {
    return instance()->UseSoftwareOpenGL;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & MiscParams::defaultUseSoftwareOpenGL() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MiscParams::setUseSoftwareOpenGL(const bool &v) {
    instance()->subHandles[6]->SetBool("UseSoftwareOpenGL",v);
    instance()->UseSoftwareOpenGL = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MiscParams::removeUseSoftwareOpenGL() {
    instance()->subHandles[6]->RemoveBool("UseSoftwareOpenGL");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MiscParams::docUnflatten() {
    return QT_TRANSLATE_NOOP("MiscParams",
"Run the dependency graph through Graphviz's unflatten, which\n"
"makes wide graphs narrower.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & MiscParams::getUnflatten() {
    return instance()->Unflatten;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & MiscParams::defaultUnflatten() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MiscParams::setUnflatten(const bool &v) {
    instance()->subHandles[7]->SetBool("Unflatten",v);
    instance()->Unflatten = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MiscParams::removeUnflatten() {
    instance()->subHandles[7]->RemoveBool("Unflatten");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MiscParams::docGeoFeatureSubgraphs() {
    return QT_TRANSLATE_NOOP("MiscParams",
"Draws the objects of each coordinate system -- a Part, a Body --\n"
"inside a box of its own in the dependency graph. On no page.\n"
"Takes effect when the graph is next drawn.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & MiscParams::getGeoFeatureSubgraphs() {
    return instance()->GeoFeatureSubgraphs;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & MiscParams::defaultGeoFeatureSubgraphs() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MiscParams::setGeoFeatureSubgraphs(const bool &v) {
    instance()->subHandles[7]->SetBool("GeoFeatureSubgraphs",v);
    instance()->GeoFeatureSubgraphs = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MiscParams::removeGeoFeatureSubgraphs() {
    instance()->subHandles[7]->RemoveBool("GeoFeatureSubgraphs");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MiscParams::docEnableGizmos() {
    return QT_TRANSLATE_NOOP("MiscParams",
"Enables on-screen handles (draggers) in the 3D view for\n"
"interactively modifying dimensions and parameters of the feature\n"
"being edited by dragging.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & MiscParams::getEnableGizmos() {
    return instance()->EnableGizmos;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & MiscParams::defaultEnableGizmos() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MiscParams::setEnableGizmos(const bool &v) {
    instance()->subHandles[1]->SetBool("EnableGizmos",v);
    instance()->EnableGizmos = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MiscParams::removeEnableGizmos() {
    instance()->subHandles[1]->RemoveBool("EnableGizmos");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MiscParams::docDelayedGizmoUpdate() {
    return QT_TRANSLATE_NOOP("MiscParams",
"Prevents the model from recalculating while manipulating\n"
"draggers. The shape updates only after release of the mouse\n"
"button.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & MiscParams::getDelayedGizmoUpdate() {
    return instance()->DelayedGizmoUpdate;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & MiscParams::defaultDelayedGizmoUpdate() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MiscParams::setDelayedGizmoUpdate(const bool &v) {
    instance()->subHandles[1]->SetBool("DelayedGizmoUpdate",v);
    instance()->DelayedGizmoUpdate = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MiscParams::removeDelayedGizmoUpdate() {
    instance()->subHandles[1]->RemoveBool("DelayedGizmoUpdate");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MiscParams::docEnableCoarseSnap() {
    return QT_TRANSLATE_NOOP("MiscParams",
"Enables larger snapping increments while manipulating draggers.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & MiscParams::getEnableCoarseSnap() {
    return instance()->EnableCoarseSnap;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & MiscParams::defaultEnableCoarseSnap() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MiscParams::setEnableCoarseSnap(const bool &v) {
    instance()->subHandles[1]->SetBool("EnableCoarseSnap",v);
    instance()->EnableCoarseSnap = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MiscParams::removeEnableCoarseSnap() {
    instance()->subHandles[1]->RemoveBool("EnableCoarseSnap");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MiscParams::docFineSnapModifier() {
    return QT_TRANSLATE_NOOP("MiscParams",
"Defines the modifier key used for fine snapping while dragging,\n"
"as Qt numbers it: 33554432 is Shift, 67108864 is Ctrl. Anything\n"
"else is taken for Shift.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & MiscParams::getFineSnapModifier() {
    return instance()->FineSnapModifier;
}

// Auto generated code (Tools/params_utils.py:413)
const long & MiscParams::defaultFineSnapModifier() {
    const static long def = 33554432;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MiscParams::setFineSnapModifier(const long &v) {
    instance()->subHandles[1]->SetInt("FineSnapModifier",v);
    instance()->FineSnapModifier = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MiscParams::removeFineSnapModifier() {
    instance()->subHandles[1]->RemoveInt("FineSnapModifier");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MiscParams::docDefaultCoarseDragBehavior() {
    return QT_TRANSLATE_NOOP("MiscParams",
"Determines whether the drag is coarse or fine without holding\n"
"the modifier key.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & MiscParams::getDefaultCoarseDragBehavior() {
    return instance()->DefaultCoarseDragBehavior;
}

// Auto generated code (Tools/params_utils.py:413)
const long & MiscParams::defaultDefaultCoarseDragBehavior() {
    const static long def = 0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MiscParams::setDefaultCoarseDragBehavior(const long &v) {
    instance()->subHandles[1]->SetInt("DefaultCoarseDragBehavior",v);
    instance()->DefaultCoarseDragBehavior = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MiscParams::removeDefaultCoarseDragBehavior() {
    instance()->subHandles[1]->RemoveInt("DefaultCoarseDragBehavior");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MiscParams::docPropertyViewAutoTransactionView() {
    return QT_TRANSLATE_NOOP("MiscParams",
"An edit in the View tab of the property view opens an undo step of\n"
"its own and recomputes the document when it ends, as one in the\n"
"Data tab does. On no page.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & MiscParams::getPropertyViewAutoTransactionView() {
    return instance()->PropertyViewAutoTransactionView;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & MiscParams::defaultPropertyViewAutoTransactionView() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MiscParams::setPropertyViewAutoTransactionView(const bool &v) {
    instance()->subHandles[8]->SetBool("AutoTransactionView",v);
    instance()->PropertyViewAutoTransactionView = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MiscParams::removePropertyViewAutoTransactionView() {
    instance()->subHandles[8]->RemoveBool("AutoTransactionView");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MiscParams::docPropertyViewAutoTransactionData() {
    return QT_TRANSLATE_NOOP("MiscParams",
"An edit in the Data tab of the property view opens an undo step of\n"
"its own and recomputes the document when it ends. On no page.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & MiscParams::getPropertyViewAutoTransactionData() {
    return instance()->PropertyViewAutoTransactionData;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & MiscParams::defaultPropertyViewAutoTransactionData() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MiscParams::setPropertyViewAutoTransactionData(const bool &v) {
    instance()->subHandles[8]->SetBool("AutoTransactionData",v);
    instance()->PropertyViewAutoTransactionData = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MiscParams::removePropertyViewAutoTransactionData() {
    instance()->subHandles[8]->RemoveBool("AutoTransactionData");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MiscParams::docPropertyViewAutoExpandView() {
    return QT_TRANSLATE_NOOP("MiscParams",
"The View tab of the property view starts a session with\n"
"everything unfolded: the groups, and the parts of a property that\n"
"has some. 'Auto expand' of the view's context menu switches it\n"
"for the session. On no page.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & MiscParams::getPropertyViewAutoExpandView() {
    return instance()->PropertyViewAutoExpandView;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & MiscParams::defaultPropertyViewAutoExpandView() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MiscParams::setPropertyViewAutoExpandView(const bool &v) {
    instance()->subHandles[8]->SetBool("AutoExpandView",v);
    instance()->PropertyViewAutoExpandView = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MiscParams::removePropertyViewAutoExpandView() {
    instance()->subHandles[8]->RemoveBool("AutoExpandView");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MiscParams::docPropertyViewAutoExpandData() {
    return QT_TRANSLATE_NOOP("MiscParams",
"The Data tab of the property view starts a session with\n"
"everything unfolded: the groups, and the parts of a property that\n"
"has some. 'Auto expand' of the view's context menu switches it\n"
"for the session. On no page.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & MiscParams::getPropertyViewAutoExpandData() {
    return instance()->PropertyViewAutoExpandData;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & MiscParams::defaultPropertyViewAutoExpandData() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MiscParams::setPropertyViewAutoExpandData(const bool &v) {
    instance()->subHandles[8]->SetBool("AutoExpandData",v);
    instance()->PropertyViewAutoExpandData = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MiscParams::removePropertyViewAutoExpandData() {
    instance()->subHandles[8]->RemoveBool("AutoExpandData");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MiscParams::docPropertyViewHideHeader() {
    return QT_TRANSLATE_NOOP("MiscParams",
"Hides the header row, Property and Value, of the property view.\n"
"Set by 'Hide header' of the view's context menu.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & MiscParams::getPropertyViewHideHeader() {
    return instance()->PropertyViewHideHeader;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & MiscParams::defaultPropertyViewHideHeader() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MiscParams::setPropertyViewHideHeader(const bool &v) {
    instance()->subHandles[8]->SetBool("HideHeader",v);
    instance()->PropertyViewHideHeader = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MiscParams::removePropertyViewHideHeader() {
    instance()->subHandles[8]->RemoveBool("HideHeader");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MiscParams::docPropertyViewViewSectionSize() {
    return QT_TRANSLATE_NOOP("MiscParams",
"Width in pixels of the Property column of the View tab. Stored\n"
"by the property view when the column is resized.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & MiscParams::getPropertyViewViewSectionSize() {
    return instance()->PropertyViewViewSectionSize;
}

// Auto generated code (Tools/params_utils.py:413)
const long & MiscParams::defaultPropertyViewViewSectionSize() {
    const static long def = 150;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MiscParams::setPropertyViewViewSectionSize(const long &v) {
    instance()->subHandles[8]->SetInt("ViewSectionSize",v);
    instance()->PropertyViewViewSectionSize = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MiscParams::removePropertyViewViewSectionSize() {
    instance()->subHandles[8]->RemoveInt("ViewSectionSize");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MiscParams::docPropertyViewDataSectionSize() {
    return QT_TRANSLATE_NOOP("MiscParams",
"Width in pixels of the Property column of the Data tab. Stored\n"
"by the property view when the column is resized.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & MiscParams::getPropertyViewDataSectionSize() {
    return instance()->PropertyViewDataSectionSize;
}

// Auto generated code (Tools/params_utils.py:413)
const long & MiscParams::defaultPropertyViewDataSectionSize() {
    const static long def = 150;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MiscParams::setPropertyViewDataSectionSize(const long &v) {
    instance()->subHandles[8]->SetInt("DataSectionSize",v);
    instance()->PropertyViewDataSectionSize = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MiscParams::removePropertyViewDataSectionSize() {
    instance()->subHandles[8]->RemoveInt("DataSectionSize");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MiscParams::docPropertyViewLastTabIndex() {
    return QT_TRANSLATE_NOOP("MiscParams",
"The tab the property view was last on: 0 View, 1 Data. Stored\n"
"when the tab changes, read when the view is made.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & MiscParams::getPropertyViewLastTabIndex() {
    return instance()->PropertyViewLastTabIndex;
}

// Auto generated code (Tools/params_utils.py:413)
const long & MiscParams::defaultPropertyViewLastTabIndex() {
    const static long def = 1;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MiscParams::setPropertyViewLastTabIndex(const long &v) {
    instance()->subHandles[8]->SetInt("LastTabIndex",v);
    instance()->PropertyViewLastTabIndex = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MiscParams::removePropertyViewLastTabIndex() {
    instance()->subHandles[8]->RemoveInt("LastTabIndex");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MiscParams::docNewPropertyType() {
    return QT_TRANSLATE_NOOP("MiscParams",
"The property type the Add Property dialog was last used with,\n"
"which it opens on. Stored when the dialog is accepted.");
}

// Auto generated code (Tools/params_utils.py:405)
const std::string & MiscParams::getNewPropertyType() {
    return instance()->NewPropertyType;
}

// Auto generated code (Tools/params_utils.py:413)
const std::string & MiscParams::defaultNewPropertyType() {
    const static std::string def = "App::PropertyString";
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MiscParams::setNewPropertyType(const std::string &v) {
    instance()->subHandles[8]->SetASCII("NewPropertyType",v);
    instance()->NewPropertyType = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MiscParams::removeNewPropertyType() {
    instance()->subHandles[8]->RemoveASCII("NewPropertyType");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MiscParams::docNewPropertyGroup() {
    return QT_TRANSLATE_NOOP("MiscParams",
"The group the Add Property dialog last put a property in, which\n"
"it opens with. Stored when the dialog is accepted.");
}

// Auto generated code (Tools/params_utils.py:405)
const std::string & MiscParams::getNewPropertyGroup() {
    return instance()->NewPropertyGroup;
}

// Auto generated code (Tools/params_utils.py:413)
const std::string & MiscParams::defaultNewPropertyGroup() {
    const static std::string def = "Base";
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MiscParams::setNewPropertyGroup(const std::string &v) {
    instance()->subHandles[8]->SetASCII("NewPropertyGroup",v);
    instance()->NewPropertyGroup = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MiscParams::removeNewPropertyGroup() {
    instance()->subHandles[8]->RemoveASCII("NewPropertyGroup");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MiscParams::docNewPropertyAppend() {
    return QT_TRANSLATE_NOOP("MiscParams",
"The Add Property dialog opens with its box checked that puts the\n"
"group's name in front of the property's. Stored when the dialog\n"
"is accepted.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & MiscParams::getNewPropertyAppend() {
    return instance()->NewPropertyAppend;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & MiscParams::defaultNewPropertyAppend() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MiscParams::setNewPropertyAppend(const bool &v) {
    instance()->subHandles[8]->SetBool("NewPropertyAppend",v);
    instance()->NewPropertyAppend = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MiscParams::removeNewPropertyAppend() {
    instance()->subHandles[8]->RemoveBool("NewPropertyAppend");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MiscParams::docPanelMirror() {
    return QT_TRANSLATE_NOOP("MiscParams",
"Which task dialogs a served session mirrors to its viewers: all,\n"
"none, or the class names of the dialogs separated by commas. On\n"
"no page. Takes effect at the next dialog.");
}

// Auto generated code (Tools/params_utils.py:405)
const std::string & MiscParams::getPanelMirror() {
    return instance()->PanelMirror;
}

// Auto generated code (Tools/params_utils.py:413)
const std::string & MiscParams::defaultPanelMirror() {
    const static std::string def = "all";
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MiscParams::setPanelMirror(const std::string &v) {
    instance()->subHandles[9]->SetASCII("PanelMirror",v);
    instance()->PanelMirror = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MiscParams::removePanelMirror() {
    instance()->subHandles[9]->RemoveASCII("PanelMirror");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MiscParams::docPanelPollMs() {
    return QT_TRANSLATE_NOOP("MiscParams",
"Milliseconds between two looks the panel mirror takes at a\n"
"mirrored task dialog for changes its widgets did not announce; 0\n"
"switches the polling off. On no page. Takes effect at the next\n"
"dialog.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & MiscParams::getPanelPollMs() {
    return instance()->PanelPollMs;
}

// Auto generated code (Tools/params_utils.py:413)
const long & MiscParams::defaultPanelPollMs() {
    const static long def = 500;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MiscParams::setPanelPollMs(const long &v) {
    instance()->subHandles[9]->SetInt("PanelPollMs",v);
    instance()->PanelPollMs = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MiscParams::removePanelPollMs() {
    instance()->subHandles[9]->RemoveInt("PanelPollMs");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MiscParams::docAutoShowSelectionView() {
    return QT_TRANSLATE_NOOP("MiscParams",
"Brings the selection view up when something is selected and\n"
"puts it away when the selection is empty. On no page. Takes\n"
"effect at the next change of the selection.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & MiscParams::getAutoShowSelectionView() {
    return instance()->AutoShowSelectionView;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & MiscParams::defaultAutoShowSelectionView() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MiscParams::setAutoShowSelectionView(const bool &v) {
    instance()->subHandles[10]->SetBool("AutoShowSelectionView",v);
    instance()->AutoShowSelectionView = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MiscParams::removeAutoShowSelectionView() {
    instance()->subHandles[10]->RemoveBool("AutoShowSelectionView");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MiscParams::docSingleClickFeatureSelect() {
    return QT_TRANSLATE_NOOP("MiscParams",
"In PartDesign's dialog that asks for a feature to work on, a\n"
"click on a feature picks it and goes on; off, the choice has to\n"
"be confirmed. On no page.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & MiscParams::getSingleClickFeatureSelect() {
    return instance()->SingleClickFeatureSelect;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & MiscParams::defaultSingleClickFeatureSelect() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MiscParams::setSingleClickFeatureSelect(const bool &v) {
    instance()->subHandles[10]->SetBool("singleClickFeatureSelect",v);
    instance()->SingleClickFeatureSelect = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MiscParams::removeSingleClickFeatureSelect() {
    instance()->subHandles[10]->RemoveBool("singleClickFeatureSelect");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MiscParams::docDAGViewSelectionMode() {
    return QT_TRANSLATE_NOOP("MiscParams",
"Whether a click in the DAG view selects one object in place of\n"
"the selection or adds to it. On no page. Read when the view is\n"
"made.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & MiscParams::getDAGViewSelectionMode() {
    return instance()->DAGViewSelectionMode;
}

// Auto generated code (Tools/params_utils.py:413)
const long & MiscParams::defaultDAGViewSelectionMode() {
    const static long def = 0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MiscParams::setDAGViewSelectionMode(const long &v) {
    instance()->subHandles[11]->SetInt("SelectionMode",v);
    instance()->DAGViewSelectionMode = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MiscParams::removeDAGViewSelectionMode() {
    instance()->subHandles[11]->RemoveInt("SelectionMode");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MiscParams::docDAGViewFontPointSize() {
    return QT_TRANSLATE_NOOP("MiscParams",
"Font size of the DAG view in points; 0 is the application's. On\n"
"no page. Read when the view is made.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & MiscParams::getDAGViewFontPointSize() {
    return instance()->DAGViewFontPointSize;
}

// Auto generated code (Tools/params_utils.py:413)
const long & MiscParams::defaultDAGViewFontPointSize() {
    const static long def = 0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MiscParams::setDAGViewFontPointSize(const long &v) {
    instance()->subHandles[11]->SetInt("FontPointSize",v);
    instance()->DAGViewFontPointSize = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MiscParams::removeDAGViewFontPointSize() {
    instance()->subHandles[11]->RemoveInt("FontPointSize");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MiscParams::docDAGViewDirection() {
    return QT_TRANSLATE_NOOP("MiscParams",
"The direction the DAG view lists the objects in: 1, or -1 for the\n"
"other way up. Anything else is taken for 1. On no page. Read when\n"
"the view is made.");
}

// Auto generated code (Tools/params_utils.py:405)
const double & MiscParams::getDAGViewDirection() {
    return instance()->DAGViewDirection;
}

// Auto generated code (Tools/params_utils.py:413)
const double & MiscParams::defaultDAGViewDirection() {
    const static double def = 1.0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MiscParams::setDAGViewDirection(const double &v) {
    instance()->subHandles[11]->SetFloat("Direction",v);
    instance()->DAGViewDirection = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MiscParams::removeDAGViewDirection() {
    instance()->subHandles[11]->RemoveFloat("Direction");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MiscParams::docCustomViewQ0() {
    return QT_TRANSLATE_NOOP("MiscParams",
"First component of the quaternion a new document's view is turned\n"
"to when its camera orientation is 'Custom'. Set by the dialog of\n"
"the Navigation page.");
}

// Auto generated code (Tools/params_utils.py:405)
const double & MiscParams::getCustomViewQ0() {
    return instance()->CustomViewQ0;
}

// Auto generated code (Tools/params_utils.py:413)
const double & MiscParams::defaultCustomViewQ0() {
    const static double def = 0.0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MiscParams::setCustomViewQ0(const double &v) {
    instance()->subHandles[12]->SetFloat("Q0",v);
    instance()->CustomViewQ0 = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MiscParams::removeCustomViewQ0() {
    instance()->subHandles[12]->RemoveFloat("Q0");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MiscParams::docCustomViewQ1() {
    return QT_TRANSLATE_NOOP("MiscParams",
"Second component of the quaternion of the 'Custom' camera\n"
"orientation of a new document.");
}

// Auto generated code (Tools/params_utils.py:405)
const double & MiscParams::getCustomViewQ1() {
    return instance()->CustomViewQ1;
}

// Auto generated code (Tools/params_utils.py:413)
const double & MiscParams::defaultCustomViewQ1() {
    const static double def = 0.0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MiscParams::setCustomViewQ1(const double &v) {
    instance()->subHandles[12]->SetFloat("Q1",v);
    instance()->CustomViewQ1 = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MiscParams::removeCustomViewQ1() {
    instance()->subHandles[12]->RemoveFloat("Q1");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MiscParams::docCustomViewQ2() {
    return QT_TRANSLATE_NOOP("MiscParams",
"Third component of the quaternion of the 'Custom' camera\n"
"orientation of a new document.");
}

// Auto generated code (Tools/params_utils.py:405)
const double & MiscParams::getCustomViewQ2() {
    return instance()->CustomViewQ2;
}

// Auto generated code (Tools/params_utils.py:413)
const double & MiscParams::defaultCustomViewQ2() {
    const static double def = 0.0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MiscParams::setCustomViewQ2(const double &v) {
    instance()->subHandles[12]->SetFloat("Q2",v);
    instance()->CustomViewQ2 = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MiscParams::removeCustomViewQ2() {
    instance()->subHandles[12]->RemoveFloat("Q2");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MiscParams::docCustomViewQ3() {
    return QT_TRANSLATE_NOOP("MiscParams",
"Fourth component of the quaternion of the 'Custom' camera\n"
"orientation of a new document.");
}

// Auto generated code (Tools/params_utils.py:405)
const double & MiscParams::getCustomViewQ3() {
    return instance()->CustomViewQ3;
}

// Auto generated code (Tools/params_utils.py:413)
const double & MiscParams::defaultCustomViewQ3() {
    const static double def = 1.0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MiscParams::setCustomViewQ3(const double &v) {
    instance()->subHandles[12]->SetFloat("Q3",v);
    instance()->CustomViewQ3 = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MiscParams::removeCustomViewQ3() {
    instance()->subHandles[12]->RemoveFloat("Q3");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MiscParams::docRecentFiles() {
    return QT_TRANSLATE_NOOP("MiscParams",
"How many files the recent files menu shows. The General page\n"
"has it. The list itself is kept beside it, a key per file, and is\n"
"not listed.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & MiscParams::getRecentFiles() {
    return instance()->RecentFiles;
}

// Auto generated code (Tools/params_utils.py:413)
const long & MiscParams::defaultRecentFiles() {
    const static long def = 4;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MiscParams::setRecentFiles(const long &v) {
    instance()->subHandles[13]->SetInt("RecentFiles",v);
    instance()->RecentFiles = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MiscParams::removeRecentFiles() {
    instance()->subHandles[13]->RemoveInt("RecentFiles");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MiscParams::docDonatePage() {
    return QT_TRANSLATE_NOOP("MiscParams",
"The address Help > Donate opens. The command stores what it read,\n"
"so the key is there after its first use.");
}

// Auto generated code (Tools/params_utils.py:405)
const std::string & MiscParams::getDonatePage() {
    return instance()->DonatePage;
}

// Auto generated code (Tools/params_utils.py:413)
const std::string & MiscParams::defaultDonatePage() {
    const static std::string def = "https://wiki.freecad.org/Donate";
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MiscParams::setDonatePage(const std::string &v) {
    instance()->subHandles[14]->SetASCII("DonatePage",v);
    instance()->DonatePage = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MiscParams::removeDonatePage() {
    instance()->subHandles[14]->RemoveASCII("DonatePage");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MiscParams::docGraphviz() {
    return QT_TRANSLATE_NOOP("MiscParams",
"Folder of the Graphviz programs the dependency graph is drawn\n"
"with. Not set, /usr/bin is tried on Linux and the search path\n"
"elsewhere; when that fails the program asks for the folder and\n"
"stores the answer here.");
}

// Auto generated code (Tools/params_utils.py:405)
const std::string & MiscParams::getGraphviz() {
    return instance()->Graphviz;
}

// Auto generated code (Tools/params_utils.py:413)
const std::string & MiscParams::defaultGraphviz() {
    const static std::string def = "";
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MiscParams::setGraphviz(const std::string &v) {
    instance()->subHandles[15]->SetASCII("Graphviz",v);
    instance()->Graphviz = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MiscParams::removeGraphviz() {
    instance()->subHandles[15]->RemoveASCII("Graphviz");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MiscParams::docIconThemeName() {
    return QT_TRANSLATE_NOOP("MiscParams",
"Name of the icon theme Qt is told to use. Empty, the program's own\n"
"icons are used. On no page. Read at start.");
}

// Auto generated code (Tools/params_utils.py:405)
const std::string & MiscParams::getIconThemeName() {
    return instance()->IconThemeName;
}

// Auto generated code (Tools/params_utils.py:413)
const std::string & MiscParams::defaultIconThemeName() {
    const static std::string def = "";
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MiscParams::setIconThemeName(const std::string &v) {
    instance()->subHandles[16]->SetASCII("Name",v);
    instance()->IconThemeName = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MiscParams::removeIconThemeName() {
    instance()->subHandles[16]->RemoveASCII("Name");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MiscParams::docIconThemeSearchPath() {
    return QT_TRANSLATE_NOOP("MiscParams",
"A folder put in front of the places Qt looks for icon themes in.\n"
"On no page. Read at start.");
}

// Auto generated code (Tools/params_utils.py:405)
const std::string & MiscParams::getIconThemeSearchPath() {
    return instance()->IconThemeSearchPath;
}

// Auto generated code (Tools/params_utils.py:413)
const std::string & MiscParams::defaultIconThemeSearchPath() {
    const static std::string def = "";
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MiscParams::setIconThemeSearchPath(const std::string &v) {
    instance()->subHandles[16]->SetASCII("SearchPath",v);
    instance()->IconThemeSearchPath = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MiscParams::removeIconThemeSearchPath() {
    instance()->subHandles[16]->RemoveASCII("SearchPath");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MiscParams::docIconThemeSearchPaths() {
    return QT_TRANSLATE_NOOP("MiscParams",
"Linux only: leaves the desktop's icon theme and its search paths\n"
"in place, where the program otherwise uses its own icons alone.\n"
"It rarely works, the common themes lack most of the icons. On no\n"
"page. Read at start.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & MiscParams::getIconThemeSearchPaths() {
    return instance()->IconThemeSearchPaths;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & MiscParams::defaultIconThemeSearchPaths() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MiscParams::setIconThemeSearchPaths(const bool &v) {
    instance()->subHandles[16]->SetBool("_ThemeSearchPaths",v);
    instance()->IconThemeSearchPaths = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MiscParams::removeIconThemeSearchPaths() {
    instance()->subHandles[16]->RemoveBool("_ThemeSearchPaths");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MiscParams::docTreeViewEnabled() {
    return QT_TRANSLATE_NOOP("MiscParams",
"The model tree has a dock window of its own, with the property\n"
"view in another. 'Tree view mode' of the General page stores it\n"
"with the two below. Read when the main window sets its dock\n"
"windows up.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & MiscParams::getTreeViewEnabled() {
    return instance()->TreeViewEnabled;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & MiscParams::defaultTreeViewEnabled() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MiscParams::setTreeViewEnabled(const bool &v) {
    instance()->subHandles[17]->SetBool("Enabled",v);
    instance()->TreeViewEnabled = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MiscParams::removeTreeViewEnabled() {
    instance()->subHandles[17]->RemoveBool("Enabled");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MiscParams::docPropertyViewEnabled() {
    return QT_TRANSLATE_NOOP("MiscParams",
"The property view has a dock window of its own. It always has\n"
"one while the tree view does. Read when the main window sets its\n"
"dock windows up.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & MiscParams::getPropertyViewEnabled() {
    return instance()->PropertyViewEnabled;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & MiscParams::defaultPropertyViewEnabled() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MiscParams::setPropertyViewEnabled(const bool &v) {
    instance()->subHandles[18]->SetBool("Enabled",v);
    instance()->PropertyViewEnabled = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MiscParams::removePropertyViewEnabled() {
    instance()->subHandles[18]->RemoveBool("Enabled");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MiscParams::docComboViewEnabled() {
    return QT_TRANSLATE_NOOP("MiscParams",
"The combo view -- model tree and property view in one dock window\n"
"-- is there. Read when the main window sets its dock windows up.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & MiscParams::getComboViewEnabled() {
    return instance()->ComboViewEnabled;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & MiscParams::defaultComboViewEnabled() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MiscParams::setComboViewEnabled(const bool &v) {
    instance()->subHandles[19]->SetBool("Enabled",v);
    instance()->ComboViewEnabled = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MiscParams::removeComboViewEnabled() {
    instance()->subHandles[19]->RemoveBool("Enabled");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MiscParams::docTaskWatcherEnabled() {
    return QT_TRANSLATE_NOOP("MiscParams",
"The list of tasks of the active workbench has a dock window of its\n"
"own, apart from the task view. The General page has it. Read\n"
"when the main window sets its dock windows up.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & MiscParams::getTaskWatcherEnabled() {
    return instance()->TaskWatcherEnabled;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & MiscParams::defaultTaskWatcherEnabled() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MiscParams::setTaskWatcherEnabled(const bool &v) {
    instance()->subHandles[20]->SetBool("Enabled",v);
    instance()->TaskWatcherEnabled = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MiscParams::removeTaskWatcherEnabled() {
    instance()->subHandles[20]->RemoveBool("Enabled");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MiscParams::docDAGViewEnabled() {
    return QT_TRANSLATE_NOOP("MiscParams",
"The DAG view, a dock window that shows the objects of the document\n"
"as a dependency graph, is there. On no page. Read when the main\n"
"window sets its dock windows up.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & MiscParams::getDAGViewEnabled() {
    return instance()->DAGViewEnabled;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & MiscParams::defaultDAGViewEnabled() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MiscParams::setDAGViewEnabled(const bool &v) {
    instance()->subHandles[21]->SetBool("Enabled",v);
    instance()->DAGViewEnabled = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MiscParams::removeDAGViewEnabled() {
    instance()->subHandles[21]->RemoveBool("Enabled");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MiscParams::docComboViewTreeViewSize() {
    return QT_TRANSLATE_NOOP("MiscParams",
"Height in pixels the model tree last had in the combo view; 0\n"
"leaves it to the layout. Stored by the program.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & MiscParams::getComboViewTreeViewSize() {
    return instance()->ComboViewTreeViewSize;
}

// Auto generated code (Tools/params_utils.py:413)
const long & MiscParams::defaultComboViewTreeViewSize() {
    const static long def = 0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MiscParams::setComboViewTreeViewSize(const long &v) {
    instance()->subHandles[19]->SetInt("TreeViewSize",v);
    instance()->ComboViewTreeViewSize = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MiscParams::removeComboViewTreeViewSize() {
    instance()->subHandles[19]->RemoveInt("TreeViewSize");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MiscParams::docComboViewPropertyViewSize() {
    return QT_TRANSLATE_NOOP("MiscParams",
"Height in pixels the property view last had in the combo view; 0\n"
"leaves it to the layout. Stored by the program.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & MiscParams::getComboViewPropertyViewSize() {
    return instance()->ComboViewPropertyViewSize;
}

// Auto generated code (Tools/params_utils.py:413)
const long & MiscParams::defaultComboViewPropertyViewSize() {
    const static long def = 0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MiscParams::setComboViewPropertyViewSize(const long &v) {
    instance()->subHandles[19]->SetInt("PropertyViewSize",v);
    instance()->ComboViewPropertyViewSize = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MiscParams::removeComboViewPropertyViewSize() {
    instance()->subHandles[19]->RemoveInt("PropertyViewSize");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MiscParams::docPlacementRotationMethod() {
    return QT_TRANSLATE_NOOP("MiscParams",
"The way of giving a rotation the Placement dialog was last on, as\n"
"the number of its entry. Stored by the dialog.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & MiscParams::getPlacementRotationMethod() {
    return instance()->PlacementRotationMethod;
}

// Auto generated code (Tools/params_utils.py:413)
const long & MiscParams::defaultPlacementRotationMethod() {
    const static long def = 0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MiscParams::setPlacementRotationMethod(const long &v) {
    instance()->subHandles[22]->SetInt("RotationMethod",v);
    instance()->PlacementRotationMethod = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MiscParams::removePlacementRotationMethod() {
    instance()->subHandles[22]->RemoveInt("RotationMethod");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MiscParams::docSharePort() {
    return QT_TRANSLATE_NOOP("MiscParams",
"The port the scene server listens on when a document is shared.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & MiscParams::getSharePort() {
    return instance()->SharePort;
}

// Auto generated code (Tools/params_utils.py:413)
const long & MiscParams::defaultSharePort() {
    const static long def = 8210;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MiscParams::setSharePort(const long &v) {
    instance()->subHandles[23]->SetInt("Port",v);
    instance()->SharePort = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MiscParams::removeSharePort() {
    instance()->subHandles[23]->RemoveInt("Port");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MiscParams::docShareDoor() {
    return QT_TRANSLATE_NOOP("MiscParams",
"Name of the front door last chosen in the Share dialog: one of\n"
"the doors kept beside this setting, each a way viewers reach\n"
"this machine.");
}

// Auto generated code (Tools/params_utils.py:405)
const std::string & MiscParams::getShareDoor() {
    return instance()->ShareDoor;
}

// Auto generated code (Tools/params_utils.py:413)
const std::string & MiscParams::defaultShareDoor() {
    const static std::string def = "";
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MiscParams::setShareDoor(const std::string &v) {
    instance()->subHandles[23]->SetASCII("Door",v);
    instance()->ShareDoor = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MiscParams::removeShareDoor() {
    instance()->subHandles[23]->RemoveASCII("Door");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MiscParams::docShareExternalHost() {
    return QT_TRANSLATE_NOOP("MiscParams",
"The address viewers reach this machine at, as the share links\n"
"carry it. Empty, the address the program finds itself is used.");
}

// Auto generated code (Tools/params_utils.py:405)
const std::string & MiscParams::getShareExternalHost() {
    return instance()->ShareExternalHost;
}

// Auto generated code (Tools/params_utils.py:413)
const std::string & MiscParams::defaultShareExternalHost() {
    const static std::string def = "";
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MiscParams::setShareExternalHost(const std::string &v) {
    instance()->subHandles[23]->SetASCII("ExternalHost",v);
    instance()->ShareExternalHost = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MiscParams::removeShareExternalHost() {
    instance()->subHandles[23]->RemoveASCII("ExternalHost");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MiscParams::docShareViewerPage() {
    return QT_TRANSLATE_NOOP("MiscParams",
"Address of the viewer page the share links point at. Empty, the\n"
"page the program serves itself is used.");
}

// Auto generated code (Tools/params_utils.py:405)
const std::string & MiscParams::getShareViewerPage() {
    return instance()->ShareViewerPage;
}

// Auto generated code (Tools/params_utils.py:413)
const std::string & MiscParams::defaultShareViewerPage() {
    const static std::string def = "";
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MiscParams::setShareViewerPage(const std::string &v) {
    instance()->subHandles[23]->SetASCII("ViewerPage",v);
    instance()->ShareViewerPage = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MiscParams::removeShareViewerPage() {
    instance()->subHandles[23]->RemoveASCII("ViewerPage");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MiscParams::docShareTrustProxy() {
    return QT_TRANSLATE_NOOP("MiscParams",
"Takes the address of a viewer from the X-Forwarded-For header a\n"
"proxy in front of this machine adds, instead of the address the\n"
"connection comes from. Only for a door on the local network.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & MiscParams::getShareTrustProxy() {
    return instance()->ShareTrustProxy;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & MiscParams::defaultShareTrustProxy() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MiscParams::setShareTrustProxy(const bool &v) {
    instance()->subHandles[23]->SetBool("TrustProxy",v);
    instance()->ShareTrustProxy = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MiscParams::removeShareTrustProxy() {
    instance()->subHandles[23]->RemoveBool("TrustProxy");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MiscParams::docShareDoorsSeeded() {
    return QT_TRANSLATE_NOOP("MiscParams",
"The program has made the first front doors of the Share dialog.\n"
"Stored by the program so that it does it once.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & MiscParams::getShareDoorsSeeded() {
    return instance()->ShareDoorsSeeded;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & MiscParams::defaultShareDoorsSeeded() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MiscParams::setShareDoorsSeeded(const bool &v) {
    instance()->subHandles[23]->SetBool("DoorsSeeded",v);
    instance()->ShareDoorsSeeded = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MiscParams::removeShareDoorsSeeded() {
    instance()->subHandles[23]->RemoveBool("DoorsSeeded");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MiscParams::docShareGrantsMigrated() {
    return QT_TRANSLATE_NOOP("MiscParams",
"The program has turned what an older version kept -- one token\n"
"and a list of clients -- into grants. Stored by the program so\n"
"that it does it once.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & MiscParams::getShareGrantsMigrated() {
    return instance()->ShareGrantsMigrated;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & MiscParams::defaultShareGrantsMigrated() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MiscParams::setShareGrantsMigrated(const bool &v) {
    instance()->subHandles[23]->SetBool("GrantsMigrated",v);
    instance()->ShareGrantsMigrated = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MiscParams::removeShareGrantsMigrated() {
    instance()->subHandles[23]->RemoveBool("GrantsMigrated");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MiscParams::docDraggerLastTranslationIncrement() {
    return QT_TRANSLATE_NOOP("MiscParams",
"The translation increment the Transform task panel was last left\n"
"with. Stored when the panel is accepted.");
}

// Auto generated code (Tools/params_utils.py:405)
const double & MiscParams::getDraggerLastTranslationIncrement() {
    return instance()->DraggerLastTranslationIncrement;
}

// Auto generated code (Tools/params_utils.py:413)
const double & MiscParams::defaultDraggerLastTranslationIncrement() {
    const static double def = 1.0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MiscParams::setDraggerLastTranslationIncrement(const double &v) {
    instance()->subHandles[24]->SetFloat("LastTranslationIncrement",v);
    instance()->DraggerLastTranslationIncrement = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MiscParams::removeDraggerLastTranslationIncrement() {
    instance()->subHandles[24]->RemoveFloat("LastTranslationIncrement");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MiscParams::docDraggerLastRotationIncrement() {
    return QT_TRANSLATE_NOOP("MiscParams",
"The rotation increment, in degrees, the Transform task panel was\n"
"last left with. Stored when the panel is accepted.");
}

// Auto generated code (Tools/params_utils.py:405)
const double & MiscParams::getDraggerLastRotationIncrement() {
    return instance()->DraggerLastRotationIncrement;
}

// Auto generated code (Tools/params_utils.py:413)
const double & MiscParams::defaultDraggerLastRotationIncrement() {
    const static double def = 15.0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MiscParams::setDraggerLastRotationIncrement(const double &v) {
    instance()->subHandles[24]->SetFloat("LastRotationIncrement",v);
    instance()->DraggerLastRotationIncrement = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MiscParams::removeDraggerLastRotationIncrement() {
    instance()->subHandles[24]->RemoveFloat("LastRotationIncrement");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MiscParams::docActivateOverlay() {
    return QT_TRANSLATE_NOOP("MiscParams",
"Sets the overlay management of the dock windows up: with it a\n"
"dock window can be laid over the 3D view. On no page. Read at\n"
"start.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & MiscParams::getActivateOverlay() {
    return instance()->ActivateOverlay;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & MiscParams::defaultActivateOverlay() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MiscParams::setActivateOverlay(const bool &v) {
    instance()->subHandles[25]->SetBool("ActivateOverlay",v);
    instance()->ActivateOverlay = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MiscParams::removeActivateOverlay() {
    instance()->subHandles[25]->RemoveBool("ActivateOverlay");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MiscParams::docDockCursorMargin() {
    return QT_TRANSLATE_NOOP("MiscParams",
"Distance in pixels from the edge of a dock window within which\n"
"the mouse counts as on the edge, for resizing an overlaid one.\n"
"On no page. Takes effect at once.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & MiscParams::getDockCursorMargin() {
    return instance()->DockCursorMargin;
}

// Auto generated code (Tools/params_utils.py:413)
const long & MiscParams::defaultDockCursorMargin() {
    const static long def = 5;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MiscParams::setDockCursorMargin(const long &v) {
    instance()->subHandles[25]->SetInt("CursorMargin",v);
    instance()->DockCursorMargin = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MiscParams::removeDockCursorMargin() {
    instance()->subHandles[25]->RemoveInt("CursorMargin");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MiscParams::docDockStdTreeView() {
    return QT_TRANSLATE_NOOP("MiscParams",
"The tree view dock was last shown. Kept by the program; it also\n"
"decides whether the tree view has a dock of its own while that\n"
"setting is not stored.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & MiscParams::getDockStdTreeView() {
    return instance()->DockStdTreeView;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & MiscParams::defaultDockStdTreeView() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MiscParams::setDockStdTreeView(const bool &v) {
    instance()->subHandles[25]->SetBool("Std_TreeView",v);
    instance()->DockStdTreeView = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MiscParams::removeDockStdTreeView() {
    instance()->subHandles[25]->RemoveBool("Std_TreeView");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MiscParams::docDockStdPropertyView() {
    return QT_TRANSLATE_NOOP("MiscParams",
"The property view dock was last shown. Kept by the program; it\n"
"also decides whether the property view has a dock of its own\n"
"while that setting is not stored.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & MiscParams::getDockStdPropertyView() {
    return instance()->DockStdPropertyView;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & MiscParams::defaultDockStdPropertyView() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MiscParams::setDockStdPropertyView(const bool &v) {
    instance()->subHandles[25]->SetBool("Std_PropertyView",v);
    instance()->DockStdPropertyView = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MiscParams::removeDockStdPropertyView() {
    instance()->subHandles[25]->RemoveBool("Std_PropertyView");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MiscParams::docOverlayLeftWidgets() {
    return QT_TRANSLATE_NOOP("MiscParams",
"The names of the dock windows laid over the left side of the 3D view,\n"
"separated by commas, in the order of their tabs.");
}

// Auto generated code (Tools/params_utils.py:405)
const std::string & MiscParams::getOverlayLeftWidgets() {
    return instance()->OverlayLeftWidgets;
}

// Auto generated code (Tools/params_utils.py:413)
const std::string & MiscParams::defaultOverlayLeftWidgets() {
    const static std::string def = "";
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MiscParams::setOverlayLeftWidgets(const std::string &v) {
    instance()->subHandles[26]->SetASCII("Widgets",v);
    instance()->OverlayLeftWidgets = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MiscParams::removeOverlayLeftWidgets() {
    instance()->subHandles[26]->RemoveASCII("Widgets");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MiscParams::docOverlayLeftWidth() {
    return QT_TRANSLATE_NOOP("MiscParams",
"Width in pixels of the overlay panel of the left side; 0 while it has\n"
"never been sized.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & MiscParams::getOverlayLeftWidth() {
    return instance()->OverlayLeftWidth;
}

// Auto generated code (Tools/params_utils.py:413)
const long & MiscParams::defaultOverlayLeftWidth() {
    const static long def = 0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MiscParams::setOverlayLeftWidth(const long &v) {
    instance()->subHandles[26]->SetInt("Width",v);
    instance()->OverlayLeftWidth = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MiscParams::removeOverlayLeftWidth() {
    instance()->subHandles[26]->RemoveInt("Width");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MiscParams::docOverlayLeftHeight() {
    return QT_TRANSLATE_NOOP("MiscParams",
"Height in pixels of the overlay panel of the left side; 0 while it has\n"
"never been sized.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & MiscParams::getOverlayLeftHeight() {
    return instance()->OverlayLeftHeight;
}

// Auto generated code (Tools/params_utils.py:413)
const long & MiscParams::defaultOverlayLeftHeight() {
    const static long def = 0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MiscParams::setOverlayLeftHeight(const long &v) {
    instance()->subHandles[26]->SetInt("Height",v);
    instance()->OverlayLeftHeight = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MiscParams::removeOverlayLeftHeight() {
    instance()->subHandles[26]->RemoveInt("Height");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MiscParams::docOverlayLeftOffset1() {
    return QT_TRANSLATE_NOOP("MiscParams",
"First of the two offsets, in pixels, the overlay panel of the left side\n"
"is placed with.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & MiscParams::getOverlayLeftOffset1() {
    return instance()->OverlayLeftOffset1;
}

// Auto generated code (Tools/params_utils.py:413)
const long & MiscParams::defaultOverlayLeftOffset1() {
    const static long def = 0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MiscParams::setOverlayLeftOffset1(const long &v) {
    instance()->subHandles[26]->SetInt("Offset1",v);
    instance()->OverlayLeftOffset1 = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MiscParams::removeOverlayLeftOffset1() {
    instance()->subHandles[26]->RemoveInt("Offset1");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MiscParams::docOverlayLeftOffset3() {
    return QT_TRANSLATE_NOOP("MiscParams",
"Second of the two offsets, in pixels, the overlay panel of the left side\n"
"is placed with.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & MiscParams::getOverlayLeftOffset3() {
    return instance()->OverlayLeftOffset3;
}

// Auto generated code (Tools/params_utils.py:413)
const long & MiscParams::defaultOverlayLeftOffset3() {
    const static long def = 0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MiscParams::setOverlayLeftOffset3(const long &v) {
    instance()->subHandles[26]->SetInt("Offset3",v);
    instance()->OverlayLeftOffset3 = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MiscParams::removeOverlayLeftOffset3() {
    instance()->subHandles[26]->RemoveInt("Offset3");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MiscParams::docOverlayLeftOffset2() {
    return QT_TRANSLATE_NOOP("MiscParams",
"Pixels the size of the overlay panel of the left side is changed by.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & MiscParams::getOverlayLeftOffset2() {
    return instance()->OverlayLeftOffset2;
}

// Auto generated code (Tools/params_utils.py:413)
const long & MiscParams::defaultOverlayLeftOffset2() {
    const static long def = 0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MiscParams::setOverlayLeftOffset2(const long &v) {
    instance()->subHandles[26]->SetInt("Offset2",v);
    instance()->OverlayLeftOffset2 = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MiscParams::removeOverlayLeftOffset2() {
    instance()->subHandles[26]->RemoveInt("Offset2");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MiscParams::docOverlayLeftSizes() {
    return QT_TRANSLATE_NOOP("MiscParams",
"The sizes in pixels of the dock windows in the overlay panel of this\n"
"side, separated by commas, in the order of their tabs.");
}

// Auto generated code (Tools/params_utils.py:405)
const std::string & MiscParams::getOverlayLeftSizes() {
    return instance()->OverlayLeftSizes;
}

// Auto generated code (Tools/params_utils.py:413)
const std::string & MiscParams::defaultOverlayLeftSizes() {
    const static std::string def = "";
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MiscParams::setOverlayLeftSizes(const std::string &v) {
    instance()->subHandles[26]->SetASCII("Sizes",v);
    instance()->OverlayLeftSizes = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MiscParams::removeOverlayLeftSizes() {
    instance()->subHandles[26]->RemoveASCII("Sizes");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MiscParams::docOverlayLeftAutoHide() {
    return QT_TRANSLATE_NOOP("MiscParams",
"The overlay panel of the left side hides while the mouse is away from\n"
"it. Of the four modes -- this one, EditHide, EditShow, TaskShow --\n"
"the first that is on counts.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & MiscParams::getOverlayLeftAutoHide() {
    return instance()->OverlayLeftAutoHide;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & MiscParams::defaultOverlayLeftAutoHide() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MiscParams::setOverlayLeftAutoHide(const bool &v) {
    instance()->subHandles[26]->SetBool("AutoHide",v);
    instance()->OverlayLeftAutoHide = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MiscParams::removeOverlayLeftAutoHide() {
    instance()->subHandles[26]->RemoveBool("AutoHide");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MiscParams::docOverlayLeftEditHide() {
    return QT_TRANSLATE_NOOP("MiscParams",
"The overlay panel of the left side hides while an object is edited.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & MiscParams::getOverlayLeftEditHide() {
    return instance()->OverlayLeftEditHide;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & MiscParams::defaultOverlayLeftEditHide() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MiscParams::setOverlayLeftEditHide(const bool &v) {
    instance()->subHandles[26]->SetBool("EditHide",v);
    instance()->OverlayLeftEditHide = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MiscParams::removeOverlayLeftEditHide() {
    instance()->subHandles[26]->RemoveBool("EditHide");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MiscParams::docOverlayLeftEditShow() {
    return QT_TRANSLATE_NOOP("MiscParams",
"The overlay panel of the left side shows only while an object is\n"
"edited.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & MiscParams::getOverlayLeftEditShow() {
    return instance()->OverlayLeftEditShow;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & MiscParams::defaultOverlayLeftEditShow() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MiscParams::setOverlayLeftEditShow(const bool &v) {
    instance()->subHandles[26]->SetBool("EditShow",v);
    instance()->OverlayLeftEditShow = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MiscParams::removeOverlayLeftEditShow() {
    instance()->subHandles[26]->RemoveBool("EditShow");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MiscParams::docOverlayLeftTaskShow() {
    return QT_TRANSLATE_NOOP("MiscParams",
"The overlay panel of the left side shows only while a task dialog is\n"
"open.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & MiscParams::getOverlayLeftTaskShow() {
    return instance()->OverlayLeftTaskShow;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & MiscParams::defaultOverlayLeftTaskShow() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MiscParams::setOverlayLeftTaskShow(const bool &v) {
    instance()->subHandles[26]->SetBool("TaskShow",v);
    instance()->OverlayLeftTaskShow = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MiscParams::removeOverlayLeftTaskShow() {
    instance()->subHandles[26]->RemoveBool("TaskShow");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MiscParams::docOverlayLeftClosed() {
    return QT_TRANSLATE_NOOP("MiscParams",
"The overlay panel of the left side was last hidden by the user. Counts\n"
"only while none of its automatic modes is on.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & MiscParams::getOverlayLeftClosed() {
    return instance()->OverlayLeftClosed;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & MiscParams::defaultOverlayLeftClosed() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MiscParams::setOverlayLeftClosed(const bool &v) {
    instance()->subHandles[26]->SetBool("Closed",v);
    instance()->OverlayLeftClosed = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MiscParams::removeOverlayLeftClosed() {
    instance()->subHandles[26]->RemoveBool("Closed");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MiscParams::docOverlayLeftTransparent() {
    return QT_TRANSLATE_NOOP("MiscParams",
"The overlay panel of the left side lets the 3D view show through.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & MiscParams::getOverlayLeftTransparent() {
    return instance()->OverlayLeftTransparent;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & MiscParams::defaultOverlayLeftTransparent() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MiscParams::setOverlayLeftTransparent(const bool &v) {
    instance()->subHandles[26]->SetBool("Transparent",v);
    instance()->OverlayLeftTransparent = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MiscParams::removeOverlayLeftTransparent() {
    instance()->subHandles[26]->RemoveBool("Transparent");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MiscParams::docOverlayRightWidgets() {
    return QT_TRANSLATE_NOOP("MiscParams",
"The names of the dock windows laid over the right side of the 3D view,\n"
"separated by commas, in the order of their tabs.");
}

// Auto generated code (Tools/params_utils.py:405)
const std::string & MiscParams::getOverlayRightWidgets() {
    return instance()->OverlayRightWidgets;
}

// Auto generated code (Tools/params_utils.py:413)
const std::string & MiscParams::defaultOverlayRightWidgets() {
    const static std::string def = "";
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MiscParams::setOverlayRightWidgets(const std::string &v) {
    instance()->subHandles[27]->SetASCII("Widgets",v);
    instance()->OverlayRightWidgets = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MiscParams::removeOverlayRightWidgets() {
    instance()->subHandles[27]->RemoveASCII("Widgets");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MiscParams::docOverlayRightWidth() {
    return QT_TRANSLATE_NOOP("MiscParams",
"Width in pixels of the overlay panel of the right side; 0 while it has\n"
"never been sized.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & MiscParams::getOverlayRightWidth() {
    return instance()->OverlayRightWidth;
}

// Auto generated code (Tools/params_utils.py:413)
const long & MiscParams::defaultOverlayRightWidth() {
    const static long def = 0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MiscParams::setOverlayRightWidth(const long &v) {
    instance()->subHandles[27]->SetInt("Width",v);
    instance()->OverlayRightWidth = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MiscParams::removeOverlayRightWidth() {
    instance()->subHandles[27]->RemoveInt("Width");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MiscParams::docOverlayRightHeight() {
    return QT_TRANSLATE_NOOP("MiscParams",
"Height in pixels of the overlay panel of the right side; 0 while it has\n"
"never been sized.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & MiscParams::getOverlayRightHeight() {
    return instance()->OverlayRightHeight;
}

// Auto generated code (Tools/params_utils.py:413)
const long & MiscParams::defaultOverlayRightHeight() {
    const static long def = 0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MiscParams::setOverlayRightHeight(const long &v) {
    instance()->subHandles[27]->SetInt("Height",v);
    instance()->OverlayRightHeight = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MiscParams::removeOverlayRightHeight() {
    instance()->subHandles[27]->RemoveInt("Height");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MiscParams::docOverlayRightOffset1() {
    return QT_TRANSLATE_NOOP("MiscParams",
"First of the two offsets, in pixels, the overlay panel of the right side\n"
"is placed with.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & MiscParams::getOverlayRightOffset1() {
    return instance()->OverlayRightOffset1;
}

// Auto generated code (Tools/params_utils.py:413)
const long & MiscParams::defaultOverlayRightOffset1() {
    const static long def = 0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MiscParams::setOverlayRightOffset1(const long &v) {
    instance()->subHandles[27]->SetInt("Offset1",v);
    instance()->OverlayRightOffset1 = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MiscParams::removeOverlayRightOffset1() {
    instance()->subHandles[27]->RemoveInt("Offset1");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MiscParams::docOverlayRightOffset3() {
    return QT_TRANSLATE_NOOP("MiscParams",
"Second of the two offsets, in pixels, the overlay panel of the right side\n"
"is placed with.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & MiscParams::getOverlayRightOffset3() {
    return instance()->OverlayRightOffset3;
}

// Auto generated code (Tools/params_utils.py:413)
const long & MiscParams::defaultOverlayRightOffset3() {
    const static long def = 0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MiscParams::setOverlayRightOffset3(const long &v) {
    instance()->subHandles[27]->SetInt("Offset3",v);
    instance()->OverlayRightOffset3 = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MiscParams::removeOverlayRightOffset3() {
    instance()->subHandles[27]->RemoveInt("Offset3");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MiscParams::docOverlayRightOffset2() {
    return QT_TRANSLATE_NOOP("MiscParams",
"Pixels the size of the overlay panel of the right side is changed by.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & MiscParams::getOverlayRightOffset2() {
    return instance()->OverlayRightOffset2;
}

// Auto generated code (Tools/params_utils.py:413)
const long & MiscParams::defaultOverlayRightOffset2() {
    const static long def = 0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MiscParams::setOverlayRightOffset2(const long &v) {
    instance()->subHandles[27]->SetInt("Offset2",v);
    instance()->OverlayRightOffset2 = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MiscParams::removeOverlayRightOffset2() {
    instance()->subHandles[27]->RemoveInt("Offset2");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MiscParams::docOverlayRightSizes() {
    return QT_TRANSLATE_NOOP("MiscParams",
"The sizes in pixels of the dock windows in the overlay panel of this\n"
"side, separated by commas, in the order of their tabs.");
}

// Auto generated code (Tools/params_utils.py:405)
const std::string & MiscParams::getOverlayRightSizes() {
    return instance()->OverlayRightSizes;
}

// Auto generated code (Tools/params_utils.py:413)
const std::string & MiscParams::defaultOverlayRightSizes() {
    const static std::string def = "";
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MiscParams::setOverlayRightSizes(const std::string &v) {
    instance()->subHandles[27]->SetASCII("Sizes",v);
    instance()->OverlayRightSizes = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MiscParams::removeOverlayRightSizes() {
    instance()->subHandles[27]->RemoveASCII("Sizes");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MiscParams::docOverlayRightAutoHide() {
    return QT_TRANSLATE_NOOP("MiscParams",
"The overlay panel of the right side hides while the mouse is away from\n"
"it. Of the four modes -- this one, EditHide, EditShow, TaskShow --\n"
"the first that is on counts.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & MiscParams::getOverlayRightAutoHide() {
    return instance()->OverlayRightAutoHide;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & MiscParams::defaultOverlayRightAutoHide() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MiscParams::setOverlayRightAutoHide(const bool &v) {
    instance()->subHandles[27]->SetBool("AutoHide",v);
    instance()->OverlayRightAutoHide = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MiscParams::removeOverlayRightAutoHide() {
    instance()->subHandles[27]->RemoveBool("AutoHide");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MiscParams::docOverlayRightEditHide() {
    return QT_TRANSLATE_NOOP("MiscParams",
"The overlay panel of the right side hides while an object is edited.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & MiscParams::getOverlayRightEditHide() {
    return instance()->OverlayRightEditHide;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & MiscParams::defaultOverlayRightEditHide() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MiscParams::setOverlayRightEditHide(const bool &v) {
    instance()->subHandles[27]->SetBool("EditHide",v);
    instance()->OverlayRightEditHide = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MiscParams::removeOverlayRightEditHide() {
    instance()->subHandles[27]->RemoveBool("EditHide");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MiscParams::docOverlayRightEditShow() {
    return QT_TRANSLATE_NOOP("MiscParams",
"The overlay panel of the right side shows only while an object is\n"
"edited.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & MiscParams::getOverlayRightEditShow() {
    return instance()->OverlayRightEditShow;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & MiscParams::defaultOverlayRightEditShow() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MiscParams::setOverlayRightEditShow(const bool &v) {
    instance()->subHandles[27]->SetBool("EditShow",v);
    instance()->OverlayRightEditShow = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MiscParams::removeOverlayRightEditShow() {
    instance()->subHandles[27]->RemoveBool("EditShow");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MiscParams::docOverlayRightTaskShow() {
    return QT_TRANSLATE_NOOP("MiscParams",
"The overlay panel of the right side shows only while a task dialog is\n"
"open.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & MiscParams::getOverlayRightTaskShow() {
    return instance()->OverlayRightTaskShow;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & MiscParams::defaultOverlayRightTaskShow() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MiscParams::setOverlayRightTaskShow(const bool &v) {
    instance()->subHandles[27]->SetBool("TaskShow",v);
    instance()->OverlayRightTaskShow = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MiscParams::removeOverlayRightTaskShow() {
    instance()->subHandles[27]->RemoveBool("TaskShow");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MiscParams::docOverlayRightClosed() {
    return QT_TRANSLATE_NOOP("MiscParams",
"The overlay panel of the right side was last hidden by the user. Counts\n"
"only while none of its automatic modes is on.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & MiscParams::getOverlayRightClosed() {
    return instance()->OverlayRightClosed;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & MiscParams::defaultOverlayRightClosed() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MiscParams::setOverlayRightClosed(const bool &v) {
    instance()->subHandles[27]->SetBool("Closed",v);
    instance()->OverlayRightClosed = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MiscParams::removeOverlayRightClosed() {
    instance()->subHandles[27]->RemoveBool("Closed");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MiscParams::docOverlayRightTransparent() {
    return QT_TRANSLATE_NOOP("MiscParams",
"The overlay panel of the right side lets the 3D view show through.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & MiscParams::getOverlayRightTransparent() {
    return instance()->OverlayRightTransparent;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & MiscParams::defaultOverlayRightTransparent() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MiscParams::setOverlayRightTransparent(const bool &v) {
    instance()->subHandles[27]->SetBool("Transparent",v);
    instance()->OverlayRightTransparent = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MiscParams::removeOverlayRightTransparent() {
    instance()->subHandles[27]->RemoveBool("Transparent");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MiscParams::docOverlayTopWidgets() {
    return QT_TRANSLATE_NOOP("MiscParams",
"The names of the dock windows laid over the top side of the 3D view,\n"
"separated by commas, in the order of their tabs.");
}

// Auto generated code (Tools/params_utils.py:405)
const std::string & MiscParams::getOverlayTopWidgets() {
    return instance()->OverlayTopWidgets;
}

// Auto generated code (Tools/params_utils.py:413)
const std::string & MiscParams::defaultOverlayTopWidgets() {
    const static std::string def = "";
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MiscParams::setOverlayTopWidgets(const std::string &v) {
    instance()->subHandles[28]->SetASCII("Widgets",v);
    instance()->OverlayTopWidgets = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MiscParams::removeOverlayTopWidgets() {
    instance()->subHandles[28]->RemoveASCII("Widgets");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MiscParams::docOverlayTopWidth() {
    return QT_TRANSLATE_NOOP("MiscParams",
"Width in pixels of the overlay panel of the top side; 0 while it has\n"
"never been sized.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & MiscParams::getOverlayTopWidth() {
    return instance()->OverlayTopWidth;
}

// Auto generated code (Tools/params_utils.py:413)
const long & MiscParams::defaultOverlayTopWidth() {
    const static long def = 0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MiscParams::setOverlayTopWidth(const long &v) {
    instance()->subHandles[28]->SetInt("Width",v);
    instance()->OverlayTopWidth = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MiscParams::removeOverlayTopWidth() {
    instance()->subHandles[28]->RemoveInt("Width");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MiscParams::docOverlayTopHeight() {
    return QT_TRANSLATE_NOOP("MiscParams",
"Height in pixels of the overlay panel of the top side; 0 while it has\n"
"never been sized.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & MiscParams::getOverlayTopHeight() {
    return instance()->OverlayTopHeight;
}

// Auto generated code (Tools/params_utils.py:413)
const long & MiscParams::defaultOverlayTopHeight() {
    const static long def = 0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MiscParams::setOverlayTopHeight(const long &v) {
    instance()->subHandles[28]->SetInt("Height",v);
    instance()->OverlayTopHeight = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MiscParams::removeOverlayTopHeight() {
    instance()->subHandles[28]->RemoveInt("Height");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MiscParams::docOverlayTopOffset1() {
    return QT_TRANSLATE_NOOP("MiscParams",
"First of the two offsets, in pixels, the overlay panel of the top side\n"
"is placed with.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & MiscParams::getOverlayTopOffset1() {
    return instance()->OverlayTopOffset1;
}

// Auto generated code (Tools/params_utils.py:413)
const long & MiscParams::defaultOverlayTopOffset1() {
    const static long def = 0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MiscParams::setOverlayTopOffset1(const long &v) {
    instance()->subHandles[28]->SetInt("Offset1",v);
    instance()->OverlayTopOffset1 = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MiscParams::removeOverlayTopOffset1() {
    instance()->subHandles[28]->RemoveInt("Offset1");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MiscParams::docOverlayTopOffset3() {
    return QT_TRANSLATE_NOOP("MiscParams",
"Second of the two offsets, in pixels, the overlay panel of the top side\n"
"is placed with.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & MiscParams::getOverlayTopOffset3() {
    return instance()->OverlayTopOffset3;
}

// Auto generated code (Tools/params_utils.py:413)
const long & MiscParams::defaultOverlayTopOffset3() {
    const static long def = 0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MiscParams::setOverlayTopOffset3(const long &v) {
    instance()->subHandles[28]->SetInt("Offset3",v);
    instance()->OverlayTopOffset3 = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MiscParams::removeOverlayTopOffset3() {
    instance()->subHandles[28]->RemoveInt("Offset3");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MiscParams::docOverlayTopOffset2() {
    return QT_TRANSLATE_NOOP("MiscParams",
"Pixels the size of the overlay panel of the top side is changed by.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & MiscParams::getOverlayTopOffset2() {
    return instance()->OverlayTopOffset2;
}

// Auto generated code (Tools/params_utils.py:413)
const long & MiscParams::defaultOverlayTopOffset2() {
    const static long def = 0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MiscParams::setOverlayTopOffset2(const long &v) {
    instance()->subHandles[28]->SetInt("Offset2",v);
    instance()->OverlayTopOffset2 = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MiscParams::removeOverlayTopOffset2() {
    instance()->subHandles[28]->RemoveInt("Offset2");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MiscParams::docOverlayTopSizes() {
    return QT_TRANSLATE_NOOP("MiscParams",
"The sizes in pixels of the dock windows in the overlay panel of this\n"
"side, separated by commas, in the order of their tabs.");
}

// Auto generated code (Tools/params_utils.py:405)
const std::string & MiscParams::getOverlayTopSizes() {
    return instance()->OverlayTopSizes;
}

// Auto generated code (Tools/params_utils.py:413)
const std::string & MiscParams::defaultOverlayTopSizes() {
    const static std::string def = "";
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MiscParams::setOverlayTopSizes(const std::string &v) {
    instance()->subHandles[28]->SetASCII("Sizes",v);
    instance()->OverlayTopSizes = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MiscParams::removeOverlayTopSizes() {
    instance()->subHandles[28]->RemoveASCII("Sizes");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MiscParams::docOverlayTopAutoHide() {
    return QT_TRANSLATE_NOOP("MiscParams",
"The overlay panel of the top side hides while the mouse is away from\n"
"it. Of the four modes -- this one, EditHide, EditShow, TaskShow --\n"
"the first that is on counts.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & MiscParams::getOverlayTopAutoHide() {
    return instance()->OverlayTopAutoHide;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & MiscParams::defaultOverlayTopAutoHide() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MiscParams::setOverlayTopAutoHide(const bool &v) {
    instance()->subHandles[28]->SetBool("AutoHide",v);
    instance()->OverlayTopAutoHide = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MiscParams::removeOverlayTopAutoHide() {
    instance()->subHandles[28]->RemoveBool("AutoHide");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MiscParams::docOverlayTopEditHide() {
    return QT_TRANSLATE_NOOP("MiscParams",
"The overlay panel of the top side hides while an object is edited.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & MiscParams::getOverlayTopEditHide() {
    return instance()->OverlayTopEditHide;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & MiscParams::defaultOverlayTopEditHide() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MiscParams::setOverlayTopEditHide(const bool &v) {
    instance()->subHandles[28]->SetBool("EditHide",v);
    instance()->OverlayTopEditHide = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MiscParams::removeOverlayTopEditHide() {
    instance()->subHandles[28]->RemoveBool("EditHide");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MiscParams::docOverlayTopEditShow() {
    return QT_TRANSLATE_NOOP("MiscParams",
"The overlay panel of the top side shows only while an object is\n"
"edited.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & MiscParams::getOverlayTopEditShow() {
    return instance()->OverlayTopEditShow;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & MiscParams::defaultOverlayTopEditShow() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MiscParams::setOverlayTopEditShow(const bool &v) {
    instance()->subHandles[28]->SetBool("EditShow",v);
    instance()->OverlayTopEditShow = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MiscParams::removeOverlayTopEditShow() {
    instance()->subHandles[28]->RemoveBool("EditShow");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MiscParams::docOverlayTopTaskShow() {
    return QT_TRANSLATE_NOOP("MiscParams",
"The overlay panel of the top side shows only while a task dialog is\n"
"open.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & MiscParams::getOverlayTopTaskShow() {
    return instance()->OverlayTopTaskShow;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & MiscParams::defaultOverlayTopTaskShow() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MiscParams::setOverlayTopTaskShow(const bool &v) {
    instance()->subHandles[28]->SetBool("TaskShow",v);
    instance()->OverlayTopTaskShow = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MiscParams::removeOverlayTopTaskShow() {
    instance()->subHandles[28]->RemoveBool("TaskShow");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MiscParams::docOverlayTopClosed() {
    return QT_TRANSLATE_NOOP("MiscParams",
"The overlay panel of the top side was last hidden by the user. Counts\n"
"only while none of its automatic modes is on.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & MiscParams::getOverlayTopClosed() {
    return instance()->OverlayTopClosed;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & MiscParams::defaultOverlayTopClosed() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MiscParams::setOverlayTopClosed(const bool &v) {
    instance()->subHandles[28]->SetBool("Closed",v);
    instance()->OverlayTopClosed = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MiscParams::removeOverlayTopClosed() {
    instance()->subHandles[28]->RemoveBool("Closed");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MiscParams::docOverlayTopTransparent() {
    return QT_TRANSLATE_NOOP("MiscParams",
"The overlay panel of the top side lets the 3D view show through.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & MiscParams::getOverlayTopTransparent() {
    return instance()->OverlayTopTransparent;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & MiscParams::defaultOverlayTopTransparent() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MiscParams::setOverlayTopTransparent(const bool &v) {
    instance()->subHandles[28]->SetBool("Transparent",v);
    instance()->OverlayTopTransparent = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MiscParams::removeOverlayTopTransparent() {
    instance()->subHandles[28]->RemoveBool("Transparent");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MiscParams::docOverlayBottomWidgets() {
    return QT_TRANSLATE_NOOP("MiscParams",
"The names of the dock windows laid over the bottom side of the 3D view,\n"
"separated by commas, in the order of their tabs.");
}

// Auto generated code (Tools/params_utils.py:405)
const std::string & MiscParams::getOverlayBottomWidgets() {
    return instance()->OverlayBottomWidgets;
}

// Auto generated code (Tools/params_utils.py:413)
const std::string & MiscParams::defaultOverlayBottomWidgets() {
    const static std::string def = "";
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MiscParams::setOverlayBottomWidgets(const std::string &v) {
    instance()->subHandles[29]->SetASCII("Widgets",v);
    instance()->OverlayBottomWidgets = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MiscParams::removeOverlayBottomWidgets() {
    instance()->subHandles[29]->RemoveASCII("Widgets");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MiscParams::docOverlayBottomWidth() {
    return QT_TRANSLATE_NOOP("MiscParams",
"Width in pixels of the overlay panel of the bottom side; 0 while it has\n"
"never been sized.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & MiscParams::getOverlayBottomWidth() {
    return instance()->OverlayBottomWidth;
}

// Auto generated code (Tools/params_utils.py:413)
const long & MiscParams::defaultOverlayBottomWidth() {
    const static long def = 0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MiscParams::setOverlayBottomWidth(const long &v) {
    instance()->subHandles[29]->SetInt("Width",v);
    instance()->OverlayBottomWidth = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MiscParams::removeOverlayBottomWidth() {
    instance()->subHandles[29]->RemoveInt("Width");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MiscParams::docOverlayBottomHeight() {
    return QT_TRANSLATE_NOOP("MiscParams",
"Height in pixels of the overlay panel of the bottom side; 0 while it has\n"
"never been sized.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & MiscParams::getOverlayBottomHeight() {
    return instance()->OverlayBottomHeight;
}

// Auto generated code (Tools/params_utils.py:413)
const long & MiscParams::defaultOverlayBottomHeight() {
    const static long def = 0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MiscParams::setOverlayBottomHeight(const long &v) {
    instance()->subHandles[29]->SetInt("Height",v);
    instance()->OverlayBottomHeight = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MiscParams::removeOverlayBottomHeight() {
    instance()->subHandles[29]->RemoveInt("Height");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MiscParams::docOverlayBottomOffset1() {
    return QT_TRANSLATE_NOOP("MiscParams",
"First of the two offsets, in pixels, the overlay panel of the bottom side\n"
"is placed with.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & MiscParams::getOverlayBottomOffset1() {
    return instance()->OverlayBottomOffset1;
}

// Auto generated code (Tools/params_utils.py:413)
const long & MiscParams::defaultOverlayBottomOffset1() {
    const static long def = 0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MiscParams::setOverlayBottomOffset1(const long &v) {
    instance()->subHandles[29]->SetInt("Offset1",v);
    instance()->OverlayBottomOffset1 = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MiscParams::removeOverlayBottomOffset1() {
    instance()->subHandles[29]->RemoveInt("Offset1");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MiscParams::docOverlayBottomOffset3() {
    return QT_TRANSLATE_NOOP("MiscParams",
"Second of the two offsets, in pixels, the overlay panel of the bottom side\n"
"is placed with.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & MiscParams::getOverlayBottomOffset3() {
    return instance()->OverlayBottomOffset3;
}

// Auto generated code (Tools/params_utils.py:413)
const long & MiscParams::defaultOverlayBottomOffset3() {
    const static long def = 0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MiscParams::setOverlayBottomOffset3(const long &v) {
    instance()->subHandles[29]->SetInt("Offset3",v);
    instance()->OverlayBottomOffset3 = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MiscParams::removeOverlayBottomOffset3() {
    instance()->subHandles[29]->RemoveInt("Offset3");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MiscParams::docOverlayBottomOffset2() {
    return QT_TRANSLATE_NOOP("MiscParams",
"Pixels the size of the overlay panel of the bottom side is changed by.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & MiscParams::getOverlayBottomOffset2() {
    return instance()->OverlayBottomOffset2;
}

// Auto generated code (Tools/params_utils.py:413)
const long & MiscParams::defaultOverlayBottomOffset2() {
    const static long def = 0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MiscParams::setOverlayBottomOffset2(const long &v) {
    instance()->subHandles[29]->SetInt("Offset2",v);
    instance()->OverlayBottomOffset2 = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MiscParams::removeOverlayBottomOffset2() {
    instance()->subHandles[29]->RemoveInt("Offset2");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MiscParams::docOverlayBottomSizes() {
    return QT_TRANSLATE_NOOP("MiscParams",
"The sizes in pixels of the dock windows in the overlay panel of this\n"
"side, separated by commas, in the order of their tabs.");
}

// Auto generated code (Tools/params_utils.py:405)
const std::string & MiscParams::getOverlayBottomSizes() {
    return instance()->OverlayBottomSizes;
}

// Auto generated code (Tools/params_utils.py:413)
const std::string & MiscParams::defaultOverlayBottomSizes() {
    const static std::string def = "";
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MiscParams::setOverlayBottomSizes(const std::string &v) {
    instance()->subHandles[29]->SetASCII("Sizes",v);
    instance()->OverlayBottomSizes = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MiscParams::removeOverlayBottomSizes() {
    instance()->subHandles[29]->RemoveASCII("Sizes");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MiscParams::docOverlayBottomAutoHide() {
    return QT_TRANSLATE_NOOP("MiscParams",
"The overlay panel of the bottom side hides while the mouse is away from\n"
"it. Of the four modes -- this one, EditHide, EditShow, TaskShow --\n"
"the first that is on counts.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & MiscParams::getOverlayBottomAutoHide() {
    return instance()->OverlayBottomAutoHide;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & MiscParams::defaultOverlayBottomAutoHide() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MiscParams::setOverlayBottomAutoHide(const bool &v) {
    instance()->subHandles[29]->SetBool("AutoHide",v);
    instance()->OverlayBottomAutoHide = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MiscParams::removeOverlayBottomAutoHide() {
    instance()->subHandles[29]->RemoveBool("AutoHide");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MiscParams::docOverlayBottomEditHide() {
    return QT_TRANSLATE_NOOP("MiscParams",
"The overlay panel of the bottom side hides while an object is edited.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & MiscParams::getOverlayBottomEditHide() {
    return instance()->OverlayBottomEditHide;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & MiscParams::defaultOverlayBottomEditHide() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MiscParams::setOverlayBottomEditHide(const bool &v) {
    instance()->subHandles[29]->SetBool("EditHide",v);
    instance()->OverlayBottomEditHide = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MiscParams::removeOverlayBottomEditHide() {
    instance()->subHandles[29]->RemoveBool("EditHide");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MiscParams::docOverlayBottomEditShow() {
    return QT_TRANSLATE_NOOP("MiscParams",
"The overlay panel of the bottom side shows only while an object is\n"
"edited.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & MiscParams::getOverlayBottomEditShow() {
    return instance()->OverlayBottomEditShow;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & MiscParams::defaultOverlayBottomEditShow() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MiscParams::setOverlayBottomEditShow(const bool &v) {
    instance()->subHandles[29]->SetBool("EditShow",v);
    instance()->OverlayBottomEditShow = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MiscParams::removeOverlayBottomEditShow() {
    instance()->subHandles[29]->RemoveBool("EditShow");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MiscParams::docOverlayBottomTaskShow() {
    return QT_TRANSLATE_NOOP("MiscParams",
"The overlay panel of the bottom side shows only while a task dialog is\n"
"open.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & MiscParams::getOverlayBottomTaskShow() {
    return instance()->OverlayBottomTaskShow;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & MiscParams::defaultOverlayBottomTaskShow() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MiscParams::setOverlayBottomTaskShow(const bool &v) {
    instance()->subHandles[29]->SetBool("TaskShow",v);
    instance()->OverlayBottomTaskShow = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MiscParams::removeOverlayBottomTaskShow() {
    instance()->subHandles[29]->RemoveBool("TaskShow");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MiscParams::docOverlayBottomClosed() {
    return QT_TRANSLATE_NOOP("MiscParams",
"The overlay panel of the bottom side was last hidden by the user. Counts\n"
"only while none of its automatic modes is on.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & MiscParams::getOverlayBottomClosed() {
    return instance()->OverlayBottomClosed;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & MiscParams::defaultOverlayBottomClosed() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MiscParams::setOverlayBottomClosed(const bool &v) {
    instance()->subHandles[29]->SetBool("Closed",v);
    instance()->OverlayBottomClosed = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MiscParams::removeOverlayBottomClosed() {
    instance()->subHandles[29]->RemoveBool("Closed");
}

// Auto generated code (Tools/params_utils.py:397)
const char *MiscParams::docOverlayBottomTransparent() {
    return QT_TRANSLATE_NOOP("MiscParams",
"The overlay panel of the bottom side lets the 3D view show through.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & MiscParams::getOverlayBottomTransparent() {
    return instance()->OverlayBottomTransparent;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & MiscParams::defaultOverlayBottomTransparent() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void MiscParams::setOverlayBottomTransparent(const bool &v) {
    instance()->subHandles[29]->SetBool("Transparent",v);
    instance()->OverlayBottomTransparent = v;
}

// Auto generated code (Tools/params_utils.py:431)
void MiscParams::removeOverlayBottomTransparent() {
    instance()->subHandles[29]->RemoveBool("Transparent");
}
//[[[end]]]
