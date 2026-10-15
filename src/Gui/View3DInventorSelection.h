/****************************************************************************
 *   Copyright (c) 2022 Zheng Lei (realthunder) <realthunder.dev@gmail.com> *
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

#ifndef GUI_VIEW3DINVENTORSELECTION_H
#define GUI_VIEW3DINVENTORSELECTION_H

#include <map>
#include <memory>
#include <string>
#include <set>

#include <App/DocumentObserver.h>
#include <Base/BoundBox.h>
#include <Gui/InventorBase.h>
#include <Gui/Selection.h>

class SoGetBoundingBoxAction;
class SoGroup;
class SoNode;
class SoPath;
class SoSeparator;
class SoFCDisplayMode;
class SoFCSwitch;

namespace Gui {

class Document;
class SoFCUnifiedSelection;
class SoFCPathAnnotation;
class SoFCSelectionRoot;
class SoSelectionElementAction;
class SoHighlightElementAction;
class View3DInventorViewer;

class GuiExport View3DInventorSelection
{
public:
    View3DInventorSelection(View3DInventorViewer *owner, SoFCUnifiedSelection* root);
    ~View3DInventorSelection();

    void setDocument(Gui::Document *pcDocument) {
        guiDocument = pcDocument;
    }
    Gui::Document* getDocument() const {
        return guiDocument;
    }

    void checkGroupOnTop(const SelectionChanges &Reason, bool alt=false);
    void refreshGroupOnTop();
    void clearGroupOnTop(bool alt=false);
    bool isInGroupOnTop(const App::SubObjectT &objT, bool altOnly=true) const;
    bool hasOnTopObject() const;
    const std::set<App::SubObjectT> &getObjectsOnTop() const;

    /** Extend \a action by the bounds of everything this view shows on top,
     * each from its view provider by its object and sub-name, shown or not
     * (docs/HandsOnQueue.md entry 124): the view's near and far planes are
     * fitted to the scene's box, in which a body hidden in the document is
     * nothing, and a selected feature of it shown on top was cut off. Kept
     * while the on-top entries stay the same and until refreshGroupOnTop().
     */
    void extendByOnTop(SoGetBoundingBoxAction *action);

    SoGroup *getAuxRoot() const;
    SoSwitch *getGroupOnTopSwitch() const;
    SoFCDisplayMode *getGroupOnTopDispMode() const;
    SoPath *getGroupOnTopPath() const;
    SoPath *getRootPath() const;

private:
    Gui::Document* guiDocument = nullptr;
    View3DInventorViewer *owner = nullptr;

    CoinPtr<SoFCUnifiedSelection> selectionRoot;
    CoinPtr<SoFCSwitch> pcGroupOnTopSwitch;
    CoinPtr<SoFCSelectionRoot> pcGroupOnTopSel;
    CoinPtr<SoFCSelectionRoot> pcGroupOnTopPreSel;
    CoinPtr<SoPath> pcGroupOnTopPath;
    CoinPtr<SoPath> pCurrentHighlightPath;
    CoinPtr<SoFCDisplayMode> pcGroupOnTopDispMode;
    CoinPtr<SoGroup> pcAuxRoot;
    CoinPtr<SoPath> pcRootPath;

    CoinPtr<SoTempPath> tmpPath1;
    CoinPtr<SoTempPath> tmpPath2;
    CoinPtr<SoTempPath> tmpPath3;

    struct OnTopInfo {
        CoinPtr<SoFCPathAnnotation> node;
        bool alt = false;
        std::unordered_map<std::string, SoDetail*> elements;

        OnTopInfo() = default;
        OnTopInfo(OnTopInfo &&other);
        ~OnTopInfo();

        void clearElements();
    };

    // Objects explicitly added to on top group
    std::set<App::SubObjectT> objectsOnTop; 

    // Objects explicitly or implicitly added to on top group
    std::map<App::SubObjectT,OnTopInfo> objectsOnTopSel; 

    std::map<App::SubObjectT,OnTopInfo> objectsOnTopPreSel;

    // extendByOnTop(): the entries the bounds were taken for, the bounds,
    // and whether a change of the document has made them old
    std::string onTopBoundsKey;
    Base::BoundBox3d onTopBounds;
    bool onTopBoundsStale = true;
    bool onTopBoundsBusy = false;

    std::unique_ptr<SoSelectionElementAction> selAction;
    std::unique_ptr<SoHighlightElementAction> preselAction;
};

} //namespace Gui

#endif // GUI_VIEW3DINVENTORSELECTION_H
