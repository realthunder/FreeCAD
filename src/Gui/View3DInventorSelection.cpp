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

#include "PreCompiled.h"
#ifndef _PreComp_
# include <Inventor/details/SoDetail.h>
# include <Inventor/nodes/SoMaterial.h>
# include <Inventor/nodes/SoPickStyle.h>
# include <Inventor/nodes/SoSeparator.h>
# include <Inventor/SoPath.h>
#endif

#include <App/Document.h>
#include <App/GeoFeatureGroupExtension.h>
#include <Base/Console.h>
#include "Application.h"
#include "Document.h"
#include "SoFCUnifiedSelection.h"
#include "View3DInventorSelection.h"
#include "ViewParams.h"
#include "ViewProviderDocumentObject.h"
#include "Inventor/SoFCDisplayMode.h"
#include "Inventor/SoFCDisplayModeElement.h"
#include "Inventor/SoFCRenderCacheManager.h"

FC_LOG_LEVEL_INIT("3DViewerSelection",true,true)

using namespace Gui;

View3DInventorSelection::OnTopInfo::OnTopInfo(OnTopInfo &&other)
    :node(std::move(other.node)),elements(std::move(other.elements))
{
}

View3DInventorSelection::OnTopInfo::~OnTopInfo()
{
    clearElements();
}

void View3DInventorSelection::OnTopInfo::clearElements()
{
    for(auto &v : elements)
        delete v.second;
    elements.clear();
}

View3DInventorSelection::View3DInventorSelection(View3DInventorViewer *owner, SoFCUnifiedSelection* root)
    : owner(owner)
    , selectionRoot(root)
    , tmpPath1(new SoTempPath(10))
    , tmpPath2(new SoTempPath(10))
    , tmpPath3(new SoTempPath(10))
{
    selAction.reset(new SoSelectionElementAction);
    preselAction.reset(new SoHighlightElementAction);

    auto pcGroupOnTop = new SoSeparator;
    pcGroupOnTop->renderCaching = SoSeparator::OFF;
    pcGroupOnTop->boundingBoxCaching = SoSeparator::OFF;
    pcGroupOnTop->setName("GroupOnTop");

    auto pickStyle = new SoPickStyle;
    pickStyle->style = SoPickStyle::UNPICKABLE;
    pickStyle->setOverride(true);
    pcGroupOnTop->addChild(pickStyle);

    coin_setenv("COIN_SEPARATE_DIFFUSE_TRANSPARENCY_OVERRIDE", "1", TRUE);

    // auto pcGroupOnTopMaterial = new SoMaterial;
    // pcGroupOnTopMaterial->transparency = ViewParams::getTransparencyOnTop();
    // pcGroupOnTopMaterial->diffuseColor.setIgnored(true);
    // pcGroupOnTopMaterial->setOverride(true);
    // pcGroupOnTop->addChild(pcGroupOnTopMaterial);

    pcGroupOnTopDispMode = new SoFCDisplayMode;
    pcGroupOnTopDispMode->transparency = ViewParams::getTransparencyOnTop();
    pcGroupOnTopDispMode->faceColor.setIgnored(true);
    pcGroupOnTopDispMode->lineColor.setIgnored(true);
    pcGroupOnTop->addChild(pcGroupOnTopDispMode);

    pcGroupOnTopSel = new SoFCSelectionRoot;
    pcGroupOnTopSel->selectionStyle = SoFCSelectionRoot::PassThrough;
    pcGroupOnTopSel->setName("GroupOnTopSel");

    pcGroupOnTopPreSel = new SoFCSelectionRoot;
    pcGroupOnTopPreSel->selectionStyle = SoFCSelectionRoot::PassThrough;
    pcGroupOnTopPreSel->setName("GroupOnTopPreSel");

    pcGroupOnTopSwitch = new SoFCSwitch;
    pcGroupOnTopSwitch->overrideSwitch = SoFCSwitch::OverrideDefault;
    pcGroupOnTopSwitch->whichChild = SO_SWITCH_ALL;
    pcGroupOnTopSwitch->defaultChild = SO_SWITCH_ALL;
    pcGroupOnTop->addChild(pcGroupOnTopSwitch);

    pcGroupOnTopSwitch->addChild(pcGroupOnTopSel);
    pcGroupOnTopSwitch->addChild(pcGroupOnTopPreSel);

    SoSearchAction sa;
    sa.setNode(selectionRoot);
    sa.apply(owner->getSoRenderManager()->getSceneGraph());

    auto path = sa.getPath();
    assert(path && path->getLength()>1);
    pcRootPath = path->copy();
    pcRootPath->truncate(path->getLength()-1);

    auto sceneNode = path->getNodeFromTail(1);
    assert(sceneNode->isOfType(SoGroup::getClassTypeId()));

    pcAuxRoot = new SoGroup;
    static_cast<SoGroup*>(sceneNode)->addChild(pcAuxRoot);

    pcAuxRoot->addChild(pcGroupOnTop);

    pcGroupOnTopPath = pcRootPath->copy();
    pcGroupOnTopPath->append(pcAuxRoot);
    pcGroupOnTopPath->append(pcGroupOnTop);
    pcGroupOnTopPath->append(pcGroupOnTopSwitch);
    pcGroupOnTopPath->append(pcGroupOnTopSel);
}

View3DInventorSelection::~View3DInventorSelection()
{
}

const std::set<App::SubObjectT> &View3DInventorSelection::getObjectsOnTop() const
{
    return objectsOnTop;
}

void View3DInventorSelection::clearGroupOnTop(bool alt)
{
    if (alt && guiDocument && objectsOnTop.size()) {
        objectsOnTop.clear();
        guiDocument->signalOnTopObject(SelectionChanges::ClrSelection, App::SubObjectT());
    }

    auto manager = selectionRoot->getRenderManager();
    if (manager) {
        if (alt && manager->clearSelection(true))
            owner->redraw();
    }

    if(objectsOnTopSel.empty() && objectsOnTopPreSel.empty())
        return;

    SoPath & tmpPath = *tmpPath1;
    tmpPath.truncate(0);

    if(!alt) {
        if(objectsOnTopPreSel.size()) {
            tmpPath.append(pcGroupOnTopSwitch);
            tmpPath.append(pcGroupOnTopPreSel);
            for(auto &v : objectsOnTopPreSel) {
                auto &info = v.second;
                tmpPath.truncate(2);
                tmpPath.append(info.node);
                tmpPath.append(info.node->getPath());
                selAction->setSecondary(true);
                selAction->setType(SoSelectionElementAction::None);
                selAction->apply(&tmpPath);
            }
            objectsOnTopPreSel.clear();
        }
        pcGroupOnTopPreSel->resetContext();
        coinRemoveAllChildren(pcGroupOnTopPreSel);
    }

    if(objectsOnTopSel.size()) {
        tmpPath.truncate(0);
        tmpPath.append(pcGroupOnTopSwitch);
        tmpPath.append(pcGroupOnTopSel);
        if(alt) {
            for(auto it=objectsOnTopSel.begin();it!=objectsOnTopSel.end();) {
                auto &info = it->second;
                if(!info.alt || info.elements.size()) {
                    info.alt = false;
                    ++it;
                    continue;
                }
                int idx = pcGroupOnTopSel->findChild(info.node);
                if(idx >= 0) {
                    tmpPath.truncate(2);
                    tmpPath.append(info.node);
                    tmpPath.append(info.node->getPath());
                    selAction->setSecondary(false);
                    selAction->setType(SoSelectionElementAction::None);
                    selAction->apply(&tmpPath);
                    pcGroupOnTopSel->removeChild(idx);
                }
                it = objectsOnTopSel.erase(it);
            }
            tmpPath.truncate(0);
            return;
        }

        pcGroupOnTopSel->resetContext();
        coinRemoveAllChildren(pcGroupOnTopSel);

        selAction->setColor(SbColor(0,0,0));
        selAction->setSecondary(false);
        selAction->setType(SoSelectionElementAction::Append);
        for(auto it=objectsOnTopSel.begin();it!=objectsOnTopSel.end();) {
            auto &info = it->second;
            if(!info.alt)
                it = objectsOnTopSel.erase(it);
            else {
                ++it;
                pcGroupOnTopSel->addChild(info.node);
                info.clearElements();
                tmpPath.truncate(2);
                tmpPath.append(info.node);
                tmpPath.append(info.node->getPath());
                selAction->apply(&tmpPath);
            }
        }
    }

    tmpPath.truncate(0);
}

bool View3DInventorSelection::isInGroupOnTop(const App::SubObjectT &_objT, bool altOnly) const
{
    auto objT(_objT);
    objT.normalize();
    auto manager = selectionRoot->getRenderManager();
    if (manager)
        return manager->isOnTop(objT.getSubNameNoElement(true), altOnly);
    auto it = objectsOnTopSel.find(objT);
    return it!=objectsOnTopSel.end() && (!altOnly || it->second.alt);
}

bool View3DInventorSelection::hasOnTopObject() const
{
    auto manager = selectionRoot->getRenderManager();
    if (manager)
        return manager->hasOnTopObject();
    return !objectsOnTopSel.empty();
}

void View3DInventorSelection::refreshGroupOnTop()
{
    std::vector<App::SubObjectT> objs;
    for (const auto &v : objectsOnTopPreSel) {
        if (!v.first.getSubObject())
            objs.push_back(v.first);
    }
    for (const auto &objT : objs)
        checkGroupOnTop(SelectionChanges(SelectionChanges::RmvPreselect,objT), false);

    objs.clear();
    for (const auto &v : objectsOnTopSel) {
        if (!v.first.getSubObject())
            objs.push_back(v.first);
    }

    for (const auto &objT : objs)
        checkGroupOnTop(SelectionChanges(SelectionChanges::RmvSelection,objT), false);

    objs.clear();
    for (const auto &objT : objectsOnTop) {
        if (!objT.getSubObject())
            objs.push_back(objT);
    }
    for (const auto &objT : objs)
        checkGroupOnTop(SelectionChanges(SelectionChanges::RmvSelection,objT), true);

    // Entries whose object is still there but whose Coin path went stale (a
    // structural change under it) or never resolved (replayed at restore
    // before the object's nodes were in place) are no longer registered with
    // the renderer. They are still in objectsOnTop, so resolve them again now
    // that the scene graph has settled -- otherwise show-on-top silently dies
    // on the next body edit and is lost from the document on the next save.
    if (auto manager = selectionRoot->getRenderManager()) {
        objs.clear();
        for (const auto &objT : objectsOnTop) {
            if (objT.getSubObject() && !manager->isOnTop(objT.getSubNameNoElement(true)))
                objs.push_back(objT);
        }
        for (const auto &objT : objs) {
            FC_LOG("on top re-resolve " << objT.getSubObjectFullName());
            checkGroupOnTop(SelectionChanges(SelectionChanges::AddSelection,objT), true);
        }
    }
}

void View3DInventorSelection::checkGroupOnTop(const SelectionChanges &Reason, bool alt)
{
    auto manager = selectionRoot->getRenderManager();
    if (manager) {
        clearGroupOnTop();
        if (!alt)
            return;
        switch(Reason.Type) {
        case SelectionChanges::AddSelection: {
            auto objT = Reason.Object.normalized(App::SubObjectT::NormalizeOption::ConvertIndex);
            ViewProvider *vp = Application::Instance->getViewProvider(objT.getObject());
            if (vp && objT.getSubObject()) {
                SoDetail *detail = nullptr;
                SoFullPath * nodePath = tmpPath1.get();
                nodePath->truncate(0);
                nodePath->append(selectionRoot);
                // getDetailPath() truncates back to what it was handed when the
                // walk to the sub-object fails (ViewProviderDocumentObject.cpp),
                // which happens while the target's nodes are not in place yet --
                // e.g. replaying the saved on-top set at restore, where a hidden
                // object has had no setModeSwitch(). Handing that bare
                // selectionRoot to addSelection() would make the whole visible
                // scene render on top, so require a path that actually descends,
                // as the non-manager branch below already does.
                bool ok = vp->getDetailPath(objT.getSubNameNoElement().c_str(),
                                            nodePath, true, detail)
                          && nodePath->getLength() > 1;
                delete detail;
                detail = nullptr;

                SoFullPath * detailPath = nodePath;
                if (ok && objT.hasSubElement()) {
                    detailPath = tmpPath2.get();
                    detailPath->truncate(0);
                    detailPath->append(selectionRoot);
                    ok = vp->getDetailPath(objT.getSubName().c_str(), detailPath, true, detail)
                         && detailPath->getLength() > 1;
                }

                if (!ok) {
                    // Keep the bookkeeping entry -- it is what refreshGroupOnTop()
                    // retries from, and what gets persisted -- but register
                    // nothing with the renderer.
                    FC_LOG("on top path unresolved " << objT.getSubObjectFullName());
                    nodePath->truncate(0);
                    detailPath->truncate(0);
                    delete detail;
                    if (guiDocument) {
                        objT.setSubName(objT.getSubNameNoElement());
                        objectsOnTop.insert(objT);
                        guiDocument->signalOnTopObject(Reason.Type, objT);
                    }
                    break;
                }

                manager->addSelection(objT.getSubNameNoElement(true),
                                      objT.getOldElementName(),
                                      nodePath,
                                      detailPath,
                                      detail,
                                      (uint32_t)(ViewParams::getTransparencyOnTop() * 255),
                                      true,
                                      true);
                if (guiDocument) {
                    objT.setSubName(objT.getSubNameNoElement());
                    objectsOnTop.insert(objT);
                    guiDocument->signalOnTopObject(Reason.Type, objT);
                }
                nodePath->truncate(0);
                detailPath->truncate(0);
                delete detail;
            }
            break;
        }
        case SelectionChanges::RmvSelection: {
            auto objT = Reason.Object.normalized(App::SubObjectT::NormalizeOption::ConvertIndex);
            manager->removeSelection(objT.getSubNameNoElement(true),
                                     objT.getOldElementName(),
                                     true);
            if (guiDocument) {
                objT.setSubName(objT.getSubNameNoElement());
                objectsOnTop.erase(objT);
                guiDocument->signalOnTopObject(Reason.Type, objT);
            }
            break;
        }
        case SelectionChanges::ClrSelection:
            if(!alt)
                selectionRoot->setSelectAll(false);
            manager->clearSelection(true);
            if (guiDocument && objectsOnTop.size()) {
                objectsOnTop.clear();
                guiDocument->signalOnTopObject(Reason.Type, App::SubObjectT());
            }
            break;
        default:
            return;
        }
        owner->getSoRenderManager()->scheduleRedraw();
        return;
    }

    bool preselect = false;

    switch(Reason.Type) {
    case SelectionChanges::SetSelection:
        clearGroupOnTop();
        if(!guiDocument)
            return;
        else {
            auto sels = Gui::Selection().getSelectionT(guiDocument->getDocument()->getName(), ResolveMode::NoResolve);
            if(ViewParams::getMaxOnTopSelections() < (int)sels.size()) {
                // setSelection() is normally used for selectAll(). Let's not blow up
                // the whole scene with all those invisible objects
                selectionRoot->setSelectAll(true);
                return;
            }
            for(auto &sel : sels ) {
                checkGroupOnTop(SelectionChanges(SelectionChanges::AddSelection,sel), alt);
            }
        }
        return;
    case SelectionChanges::ClrSelection:
        clearGroupOnTop(alt);
        if(!alt)
            selectionRoot->setSelectAll(false);
        return;
    case SelectionChanges::SetPreselect:
    case SelectionChanges::RmvPreselect:
        preselect = true;
        break;
    default:
        break;
    }

    if(!getDocument() || !Reason.pDocName || !Reason.pDocName[0] || !Reason.pObjectName)
        return;

    std::string element = Reason.Object.getOldElementName();
    auto objT = Reason.Object.normalized(App::SubObjectT::NormalizeOption::ConvertIndex
                                        |App::SubObjectT::NormalizeOption::NoElement);
    if (alt && Reason.Type == SelectionChanges::RmvSelection) {
        objectsOnTop.erase(objT);
        guiDocument->signalOnTopObject(Reason.Type, objT);
    }

    switch(Reason.Type) {
    case SelectionChanges::SetPreselect:
        if(Reason.SubType!=SelectionChanges::MsgSource::TreeView) {
            // 2 means it is triggered from tree view. If not from tree view
            // and not belong to on top object, do not handle it.
            return;
        }
        break;
    case SelectionChanges::HideSelection:
    case SelectionChanges::RmvPreselect:
    case SelectionChanges::RmvSelection: {

        auto &objs = preselect?objectsOnTopPreSel:objectsOnTopSel;
        auto pcGroup = preselect?pcGroupOnTopPreSel:pcGroupOnTopSel;

        if(preselect && pCurrentHighlightPath) {
            preselAction->setHighlighted(false);
            preselAction->apply(pCurrentHighlightPath);
            pCurrentHighlightPath.reset();
        }

        auto it = objs.find(objT);
        if(it == objs.end())
            return;
        auto &info = it->second;

        if(Reason.Type == SelectionChanges::HideSelection) {
            if(!info.alt) {
                pcGroup->removeChild(info.node);
                objs.erase(it);
            }
            return;
        }

        SoDetail *det = 0;
        auto eit = info.elements.end();

        if(Reason.Type==SelectionChanges::RmvSelection && alt) {
            // When alt is true, remove this object from the on top group
            // regardless of whether it is previously selected with alt or not.
            info.alt = false;
            info.clearElements();
        } else {
            eit = info.elements.find(element);
            if(eit == info.elements.end())
                return;
            else
                det = eit->second;
        }

        if(preselect) {
            auto it2 = objectsOnTopSel.find(objT);
            if(it2!=objectsOnTopSel.end()
                    && it2->second.elements.empty()
                    && !it2->second.alt)
            {
                pcGroupOnTopSel->removeChild(it2->second.node);
                objectsOnTopSel.erase(it2);
            }
        }

        int index = pcGroup->findChild(info.node);
        if(index < 0) {
            objs.erase(it);
            return;
        }

        SoPath * path = info.node->getPath();
        SoPath & tmpPath = *tmpPath1;
        tmpPath.truncate(0);
        tmpPath.append(pcGroupOnTopSwitch);
        tmpPath.append(pcGroup);
        tmpPath.append(info.node);
        if(path)
            tmpPath.append(path);

        if(pcGroup == pcGroupOnTopSel) {
            selAction->setElement(det);
            selAction->setSecondary(false);
            selAction->setType(det?SoSelectionElementAction::Remove:SoSelectionElementAction::None);
            selAction->apply(&tmpPath);
            selAction->setElement(0);

            if(info.alt && eit!=info.elements.end() && info.elements.size()==1) {
                selAction->setColor(SbColor(0,0,0));
                selAction->setType(SoSelectionElementAction::All);
                selAction->apply(&tmpPath);
            }
        } else {
            selAction->setElement(det);
            selAction->setSecondary(true);
            selAction->setType(det?SoSelectionElementAction::Remove:SoSelectionElementAction::None);
            selAction->apply(&tmpPath);
            selAction->setElement(0);
        }

        tmpPath.truncate(0);

        if(eit != info.elements.end()) {
            delete eit->second;
            info.elements.erase(eit);
        }
        if(info.elements.empty() && !info.alt) {
            pcGroup->removeChild(index);
            objs.erase(it);
            if(alt) {
                Gui::Selection().rmvPreselect();
                if (objT.isVisible()) {
                    Gui::Selection().updateSelection(true,
                            Reason.pDocName, Reason.pObjectName, Reason.pSubName);
                }
            }
        }
        return;
    }
    default:
        break;
    }

    if(!preselect && element.size()
            && ViewParams::getShowSelectionBoundingBox())
        return;

    auto &objs = preselect?objectsOnTopPreSel:objectsOnTopSel;
    auto pcGroup = preselect?pcGroupOnTopPreSel:pcGroupOnTopSel;

    const char *subname = Reason.pSubName;
    auto vp = Base::freecad_dynamic_cast<ViewProviderDocumentObject>(
            Application::Instance->getViewProvider(objT.getObject()));
    if(!vp)
        return;
    auto svp = Base::freecad_dynamic_cast<ViewProviderDocumentObject>(
            Application::Instance->getViewProvider(objT.getSubObject()));
    if(!svp)
        return;
    int onTop;
    // onTop==2 means on top only if whole object is selected,
    // onTop==3 means on top only if some sub-element is selected
    // onTop==1 means either
    if(Gui::Selection().needPickedList()
            || (alt && Reason.Type == SelectionChanges::AddSelection)
            || ViewParams::getShowSelectionOnTop()
            || isInGroupOnTop(objT))
        onTop = 1;
    else if(vp->OnTopWhenSelected.getValue())
        onTop = vp->OnTopWhenSelected.getValue();
    else
        onTop = svp->OnTopWhenSelected.getValue();

    if(preselect) {
        preselAction->setHighlighted(true);
        preselAction->setColor(selectionRoot->colorHighlight.getValue());
        preselAction->apply(pcGroupOnTopPreSel);
        if(!onTop)
            onTop = 2;
    }else if (Reason.Type == SelectionChanges::AddSelection) {
        if(!onTop)
            return;
        selAction->setColor(selectionRoot->colorSelection.getValue());
    }
    if(onTop==2 || onTop==3) {
        if(subname && *subname) {
            size_t len = strlen(subname);
            if(subname[len-1]=='.') {
                // ending with '.' means whole object selection
                if(onTop == 3)
                    return;
            }else if(onTop==2)
                return;
        }else if(onTop==3)
            return;
    }

    SoFullPath & detailPath = *tmpPath1;
    detailPath.truncate(0);

    if (alt && Reason.Type == SelectionChanges::AddSelection) {
        objectsOnTop.insert(objT);
        guiDocument->signalOnTopObject(Reason.Type, objT);
    }

    SoDetail *det = 0;
    if(vp->getDetailPath(subname, &detailPath, true, det) && detailPath.getLength()) {
        auto &info = objs[objT];
        if(!info.node) {
            info.node = new SoFCPathAnnotation(vp,subname,owner);
            info.node->priority.setValue(preselect?1:0);
            info.node->setPath(&detailPath);
            pcGroup->addChild(info.node);
        }
        if(alt && Reason.Type == SelectionChanges::AddSelection) {
            if(!info.alt) {
                info.alt = true;
                if(info.elements.empty()) {
                    SoPath & altPath = *tmpPath2;
                    altPath.truncate(0);
                    altPath.append(pcGroupOnTopSwitch);
                    altPath.append(pcGroup);
                    altPath.append(info.node);
                    altPath.append(&detailPath);
                    selAction->setColor(SbColor(0,0,0));
                    selAction->setSecondary(false);
                    selAction->setType(SoSelectionElementAction::All);
                    selAction->apply(&altPath);
                    altPath.truncate(0);
                }
            }
        } else if(info.elements.emplace(element,det).second) {
            SoPath & altPath = *tmpPath2;
            altPath.truncate(0);
            altPath.append(pcGroupOnTopSwitch);
            altPath.append(pcGroup);
            altPath.append(info.node);
            altPath.append(&detailPath);
            if(pcGroup == pcGroupOnTopSel) {
                selAction->setSecondary(false);
                if(info.elements.size()==1 && info.alt) {
                    selAction->setType(SoSelectionElementAction::None);
                    selAction->apply(&altPath);
                }
                info.node->setDetail(!!det);
                selAction->setElement(det);
                selAction->setType(det?SoSelectionElementAction::Append:SoSelectionElementAction::All);
                selAction->apply(&altPath);
                selAction->setElement(0);
            } else if (det) {
                // We are preselecting some element. In this case, we do not
                // use PreSelGroup for highlighting, but instead rely on
                // OnTopGroup. Because we want SoBrepFaceSet to pick the proper
                // highlight color, in case it conflicts with the selection or
                // the object's original color.

                info.node->setPath(0);

                auto &selInfo = objectsOnTopSel[objT];
                if(!selInfo.node) {
                    selInfo.node = new SoFCPathAnnotation(vp,subname,owner);
                    selInfo.node->setPath(&detailPath);
                    selInfo.node->setDetail(true);
                    pcGroupOnTopSel->addChild(selInfo.node);
                }

                if(pCurrentHighlightPath) {
                    preselAction->setHighlighted(false);
                    preselAction->apply(pCurrentHighlightPath);
                    pCurrentHighlightPath.reset();
                }

                SoPath & tmpPath = *tmpPath3;
                tmpPath.truncate(0);
                tmpPath.append(pcGroupOnTopSwitch);
                tmpPath.append(pcGroupOnTopSel);
                tmpPath.append(selInfo.node);
                tmpPath.append(&detailPath);
                preselAction->setHighlighted(true);
                preselAction->setElement(det);
                preselAction->apply(&tmpPath);
                preselAction->setElement(0);

                pCurrentHighlightPath = tmpPath.copy();
                tmpPath.truncate(0);
            } else {
                // NOTE: assuming preselect is only applicable to one single
                // object(or sub-element) at a time. If in the future we shall
                // support multiple preselect element, include the element in
                // the objectsOnTopPreSel key.
                info.node->setDetail(false);
                selAction->setSecondary(true);
                selAction->setElement(0);
                selAction->setType(SoSelectionElementAction::None);
                selAction->apply(&altPath);
            }
            det = 0;
            altPath.truncate(0);
        }
    }

    delete det;
    detailPath.truncate(0);
}

SoGroup *View3DInventorSelection::getAuxRoot() const
{
    return pcAuxRoot;
}

SoSwitch *View3DInventorSelection::getGroupOnTopSwitch() const
{
    return pcGroupOnTopSwitch;
}

SoFCDisplayMode *View3DInventorSelection::getGroupOnTopDispMode() const
{
    return pcGroupOnTopDispMode;
}

SoPath *View3DInventorSelection::getGroupOnTopPath() const
{
    return pcGroupOnTopPath;
}

SoPath *View3DInventorSelection::getRootPath() const
{
    return pcRootPath;
}
