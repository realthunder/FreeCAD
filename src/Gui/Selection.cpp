/***************************************************************************
 *   Copyright (c) 2011 Jürgen Riegel <juergen.riegel@web.de>              *
 *   Copyright (c) 2011 Werner Mayer <wmayer[at]users.sourceforge.net>     *
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

#include "PreCompiled.h"

#ifndef _PreComp_
# include <boost/algorithm/string/predicate.hpp>
# include <QApplication>
# include <QString>
# include <QTextStream>
#endif

#include <App/Application.h>
#include <App/Document.h>
#include <App/DocumentObject.h>
#include <App/DocumentObjectPy.h>
#include <App/DocumentObserver.h>
#include <App/GeoFeature.h>
#include <Base/Console.h>
#include <Base/Exception.h>
#include <Base/Interpreter.h>
#include <Base/Tools.h>
#include <Base/UnitsApi.h>
#include <Gui/SelectionObjectPy.h>

#include "Command.h"
#include "Selection.h"
#include "SelectionObject.h"
#include "Application.h"
#include "Document.h"
#include "Macro.h"
#include "MainWindow.h"
#include "MDIView.h"
#include "SelectionFilter.h"
#include "SelectionObserverPython.h"
#include "Tree.h"
#include "TreeParams.h"
#include "ViewParams.h"
#include "ViewProviderDocumentObject.h"
#include "Widgets.h"

FC_LOG_LEVEL_INIT("Selection",false,true,true)

using namespace Gui;
using namespace std;
namespace bp = boost::placeholders;

SelectionGateFilterExternal::SelectionGateFilterExternal(const char *docName, const char *objName) {
    if(docName) {
        DocName = docName;
        if(objName)
            ObjName = objName;
    }
}

bool SelectionGateFilterExternal::allow(App::Document *doc ,App::DocumentObject *obj, const char*) {
    if(!doc || !obj)
        return true;
    if(!DocName.empty() && doc->getName()!=DocName)
        notAllowedReason = "Cannot select external object";
    else if(!ObjName.empty() && ObjName==obj->getNameInDocument())
        notAllowedReason = "Cannot select self";
    else
        return true;
    return false;
}

//////////////////////////////////////////////////////////////////////////////////////////

SelectionObserver::SelectionObserver(bool attach, ResolveMode resolve)
    : resolve(resolve)
    , blockedSelection(false)
{
    if (attach)
        attachSelection();
}

SelectionObserver::SelectionObserver(const ViewProviderDocumentObject *vp, bool attach, ResolveMode resolve)
    : resolve(resolve)
    , blockedSelection(false)
{
    if (vp && vp->getObject() && vp->getObject()->getDocument()) {
        filterDocName = vp->getObject()->getDocument()->getName();
        filterObjName = vp->getObject()->getNameInDocument();
    }
    if (attach)
        attachSelection();
}


SelectionObserver::~SelectionObserver()
{
    detachSelection();
}

bool SelectionObserver::blockSelection(bool block)
{
    bool ok = blockedSelection;
    blockedSelection = block;
    return ok;
}

bool SelectionObserver::isSelectionBlocked() const
{
    return blockedSelection;
}

bool SelectionObserver::isSelectionAttached() const
{
    return connectSelection.connected();
}

void SelectionObserver::attachSelection()
{
    if (!connectSelection.connected()) {
        bool newStyle = (resolve >= ResolveMode::NewStyleElement);
        bool oldStyle = (resolve == ResolveMode::OldStyleElement);
        auto &signal = newStyle ? Selection().signalSelectionChanged3 :
                       oldStyle ? Selection().signalSelectionChanged2 :
                                  Selection().signalSelectionChanged  ;
        connectSelection = signal.connect(boost::bind
            (&SelectionObserver::_onSelectionChanged, this, bp::_1));

        if (!filterDocName.empty()) {
            Selection().addSelectionGate(
                    new SelectionGateFilterExternal(filterDocName.c_str(),filterObjName.c_str()));
        }
    }
}

void SelectionObserver::_onSelectionChanged(const SelectionChanges& msg) {
    try {
        if (blockedSelection)
            return;
        onSelectionChanged(msg);
    } catch (Base::Exception &e) {
        e.ReportException();
        FC_ERR("Unhandled Base::Exception caught in selection observer");
    } catch (Py::Exception &) {
        Base::PyGILStateLocker lock;
        Base::PyException e;
        e.ReportException();
        FC_ERR("Unhandled Python exception caught in selection observer");
    } catch (std::exception &e) {
        FC_ERR("Unhandled std::exception caught in selection observer: " << e.what());
    } catch (...) {
        FC_ERR("Unhandled unknown exception caught in selection observer");
    }
}

void SelectionObserver::detachSelection()
{
    if (connectSelection.connected()) {
        connectSelection.disconnect();
        if (!filterDocName.empty())
            Selection().rmvSelectionGate();
    }
}

// -------------------------------------------

static int _DisableTopParentCheck;

SelectionNoTopParentCheck::SelectionNoTopParentCheck() {
    ++_DisableTopParentCheck;
}

SelectionNoTopParentCheck::~SelectionNoTopParentCheck() {
    if(_DisableTopParentCheck>0)
        --_DisableTopParentCheck;
}

bool SelectionNoTopParentCheck::enabled() {
    return _DisableTopParentCheck>0;
}

// -------------------------------------------

static int _PauseNotification;

SelectionPauseNotification::SelectionPauseNotification() {
    ++_PauseNotification;
}

SelectionPauseNotification::~SelectionPauseNotification() {
    if(_PauseNotification>0) {
        --_PauseNotification;
        Selection().flushNotifications();
    }
}

bool SelectionPauseNotification::enabled() {
    return _PauseNotification>0;
}
// -------------------------------------------

SelectionContext::SelectionContext(const App::SubObjectT &sobj)
{
    Selection().pushContext(sobj);
}

SelectionContext::~SelectionContext()
{
    Selection().popContext();
}

// -------------------------------------------
bool SelectionSingleton::hasSelection() const
{
    return !_SelList.empty();
}

bool SelectionSingleton::hasPreselection() const {
    return !CurrentPreselection.Object.getObjectName().empty();
}

std::vector<SelectionSingleton::SelObj> SelectionSingleton::getCompleteSelection(ResolveMode resolve) const
{
    return getSelection("*", resolve);
}

std::vector<App::SubObjectT> SelectionSingleton::getSelectionT(
        const char* pDocName, ResolveMode resolve, bool single) const
{
    auto sels = getSelection(pDocName,resolve,single);
    std::vector<App::SubObjectT> res;
    res.reserve(sels.size());
    for(auto &sel : sels)
        res.emplace_back(sel.pObject,sel.SubName);
    return res;
}

std::vector<SelectionSingleton::SelObj> SelectionSingleton::getSelection(const char* pDocName, ResolveMode resolve, bool single) const
{
    std::vector<SelObj> temp;
    if (single)
        temp.reserve(1);
    SelObj tempSelObj;

    App::Document *pcDoc = nullptr;
    if(!pDocName || strcmp(pDocName,"*") != 0) {
        pcDoc = getDocument(pDocName);
        if (!pcDoc)
            return temp;
    }

    std::map<App::DocumentObject*,std::set<std::string> > objMap;

    for(const auto &sel : _SelList.get<0>()) {
        if(!sel.pDoc)
            continue;
        const char *subelement = nullptr;
        auto obj = getObjectOfType(sel, App::DocumentObject::getClassTypeId(), resolve, &subelement);
        if (!obj || (pcDoc && sel.pObject->getDocument() != pcDoc))
            continue;

        // In case we are resolving objects, make sure no duplicates
        if (resolve != ResolveMode::NoResolve && !objMap[obj].insert(std::string(subelement ? subelement : "")).second)
            continue;

        if (single && !temp.empty()) {
            temp.clear();
            break;
        }

        tempSelObj.DocName  = obj->getDocument()->getName();
        tempSelObj.FeatName = obj->getNameInDocument();
        tempSelObj.SubName = subelement;
        tempSelObj.TypeName = obj->getTypeId().getName();
        tempSelObj.pObject  = obj;
        tempSelObj.pResolvedObject  = sel.pResolvedObject;
        tempSelObj.pDoc     = obj->getDocument();
        tempSelObj.x        = sel.x;
        tempSelObj.y        = sel.y;
        tempSelObj.z        = sel.z;

        temp.push_back(tempSelObj);
    }

    return temp;
}

bool SelectionSingleton::hasSelection(const char* doc, ResolveMode resolve) const
{
    App::Document *pcDoc = nullptr;
    if (!doc || strcmp(doc,"*") != 0) {
        pcDoc = getDocument(doc);
        if (!pcDoc)
            return false;
    }
    for(const auto &sel : _SelList.get<0>()) {
        if (!sel.pDoc)
            continue;
        auto obj = getObjectOfType(sel, App::DocumentObject::getClassTypeId(), resolve);
        if (obj && (!pcDoc || sel.pObject->getDocument()==pcDoc)) {
            return true;
        }
    }

    return false;
}

bool SelectionSingleton::hasSubSelection(const char* doc, bool subElement) const
{
    App::Document *pcDoc = nullptr;
    if(!doc || strcmp(doc,"*")!=0) {
        pcDoc = getDocument(doc);
        if (!pcDoc)
            return false;
    }
    for(auto &sel : _SelList.get<0>()) {
        if(pcDoc && pcDoc != sel.pDoc)
            continue;
        if(sel.SubName.empty())
            continue;
        if(subElement && sel.SubName.back()!='.')
            return true;
        if(sel.pObject != sel.pResolvedObject)
            return true;
    }

    return false;
}

std::vector<App::SubObjectT> SelectionSingleton::getPickedList(const char* pDocName) const
{
    std::vector<App::SubObjectT> res;

    App::Document *pcDoc = nullptr;
    if(!pDocName || strcmp(pDocName,"*") != 0) {
        pcDoc = getDocument(pDocName);
        if (!pcDoc)
            return res;
    }

    res.reserve(_PickedList.size());
    for(auto &sel : _PickedList) {
        if (!pcDoc || sel.pDoc == pcDoc) {
            res.emplace_back(sel.DocName.c_str(), sel.FeatName.c_str(), sel.SubName.c_str());
        }
    }
    return res;
}

template<class T>
std::vector<SelectionObject>
SelectionSingleton::getObjectList(const char* pDocName,
                                  Base::Type typeId,
                                  T &objList,
                                  ResolveMode resolve,
                                  bool single) const

{
    std::vector<SelectionObject> temp;
    if (single)
        temp.reserve(1);
    std::map<App::DocumentObject*,size_t> SortMap;

    // check the type
    if (typeId == Base::Type::badType())
        return temp;

    App::Document *pcDoc = nullptr;
    if (!pDocName || strcmp(pDocName,"*") != 0) {
        pcDoc = getDocument(pDocName);
        if (!pcDoc)
            return temp;
    }

    for (const auto &sel : objList) {
        if(!sel.pDoc)
            continue;
        const char *subelement = nullptr;
        auto obj = getObjectOfType(sel,typeId,resolve,&subelement);
        if (!obj || (pcDoc && sel.pObject->getDocument() != pcDoc))
            continue;
        auto it = SortMap.find(obj);
        if(it!=SortMap.end()) {
            // only add sub-element
            if (subelement && *subelement) {
                auto &entry = temp[it->second];
                if(resolve != ResolveMode::NoResolve && !entry._SubNameSet.insert(subelement).second)
                    continue;
                if (entry.SubNames.empty()) {
                    // It means there is previous whole object selection. Don't
                    // loose that information.
                    entry.SubNames.emplace_back();
                    entry.SelPoses.emplace_back(0, 0, 0);
                }
                entry.SubNames.push_back(subelement);
                entry.SelPoses.emplace_back(sel.x,sel.y,sel.z);
            }
        }
        else {
            if (single && !temp.empty()) {
                temp.clear();
                break;
            }
            // create a new entry
            temp.emplace_back(obj);
            if (subelement && *subelement) {
                temp.back().SubNames.emplace_back(subelement);
                temp.back().SelPoses.emplace_back(sel.x,sel.y,sel.z);
                if (resolve != ResolveMode::NoResolve)
                    temp.back()._SubNameSet.insert(subelement);
            }
            SortMap.insert(std::make_pair(obj,temp.size()-1));
        }
    }

    return temp;
}

std::vector<SelectionObject>
SelectionSingleton::getSelectionEx(const char* pDocName, Base::Type typeId, ResolveMode resolve, bool single) const
{
    return getObjectList(pDocName,typeId,_SelList.get<0>(),resolve,single);
}

std::vector<SelectionObject>
SelectionSingleton::getPickedListEx(const char* pDocName, Base::Type typeId) const
{
    return getObjectList(pDocName,typeId,_PickedList,ResolveMode::NoResolve);
}

bool SelectionSingleton::needPickedList() const
{
    return _needPickedList;
}

void SelectionSingleton::enablePickedList(bool enable)
{
    if(enable != _needPickedList) {
        _needPickedList = enable;
        _PickedList.clear();
        notify(SelectionChanges(SelectionChanges::PickedListChanged));
    }
}

void SelectionSingleton::notify(SelectionChanges &&Chng)
{
    if(Notifying || SelectionPauseNotification::enabled()) {
        switch(Chng.Type) {
        // In case of ClrSelection or SetSelection, we can erase any
        // Add/RmvSelection from tail up.
        case SelectionChanges::ClrSelection:
        case SelectionChanges::SetSelection:
            while(NotificationQueue.size()) {
                auto &entry = NotificationQueue.back();
                switch(entry.Type) {
                case SelectionChanges::AddSelection:
                case SelectionChanges::RmvSelection:
                case SelectionChanges::ClrSelection:
                case SelectionChanges::SetSelection:
                    if (Chng.Object.getDocumentName().empty()
                            || Chng.Object.getDocument() != entry.Object.getDocument())
                    {
                        NotificationQueue.pop_back();
                        continue;
                    }
                    // fall through
                default:
                    break;
                }
                break;
            }
            NotificationQueue.emplace_back(std::move(Chng));
            return;
        // In case the queued Add/RmvSelection message exceed the limit,
        // replace them with a SetSelection message. The expected response to
        // this message for the observer is to recheck the entire selections.
        case SelectionChanges::AddSelection:
        case SelectionChanges::RmvSelection:
            if (NotificationQueue.size()
                    && ViewParams::getMaxSelectionNotification()
                    && ++PendingAddSelection >= ViewParams::getMaxSelectionNotification())
            {
                if (NotificationQueue.back().Type != SelectionChanges::SetSelection)
                    notify(SelectionChanges(SelectionChanges::SetSelection));
                return;
            }
        default:
            break;
        }

        long limit = std::max(1000l, ViewParams::getMaxSelectionNotification());
        if (++NotificationRecursion > limit) {
            if (NotificationRecursion == limit + 1)
                FC_WARN("Discard selection notification " << Chng.Type);
            return;
        }

        NotificationQueue.push_back(std::move(Chng));
        return;
    }

    NotificationRecursion = 0;
    PendingAddSelection = 0;
    Base::FlagToggler<bool> flag(Notifying);
    NotificationQueue.push_back(std::move(Chng));
    while(!NotificationQueue.empty()) {
        auto msg = NotificationQueue.front();
        NotificationQueue.pop_front();
        bool notify;
        switch(msg.Type) {
        case SelectionChanges::AddSelection:
            notify = isSelected(msg.pDocName, msg.pObjectName, msg.pSubName, ResolveMode::NoResolve);
            break;
        case SelectionChanges::RmvSelection:
            notify = !isSelected(msg.pDocName, msg.pObjectName, msg.pSubName, ResolveMode::NoResolve);
            break;
        case SelectionChanges::SetPreselect:
            notify = CurrentPreselection.Type==SelectionChanges::SetPreselect
                && CurrentPreselection.Object == msg.Object;
            break;
        case SelectionChanges::RmvPreselect:
            notify = CurrentPreselection.Type==SelectionChanges::ClrSelection;
            break;
        default:
            notify = true;
        }
        if(notify) {
            Notify(msg);
            try {
                signalSelectionChanged(msg);
            }
            catch (const boost::exception&) {
                // reported by code analyzers
                Base::Console().Warning("notify: Unexpected boost exception\n");
            }
        }
    }
    NotificationRecursion = 0;
}

void SelectionSingleton::flushNotifications()
{
    if(Notifying || SelectionPauseNotification::enabled()
                 || NotificationQueue.empty())
        return;
    auto chg = NotificationQueue.back();
    NotificationQueue.pop_back();
    notify(std::move(chg));

    _selStackPush(_SelList.size() > 0);
}

bool SelectionSingleton::hasPickedList() const
{
    return !_PickedList.empty();
}

int SelectionSingleton::getAsPropertyLinkSubList(App::PropertyLinkSubList &prop) const
{
    std::vector<Gui::SelectionObject> sel = this->getSelectionEx();
    std::vector<App::DocumentObject*> objs; objs.reserve(sel.size() * 2);
    std::vector<std::string> subs; subs.reserve(sel.size()*2);
    for (auto & selitem : sel) {
        App::DocumentObject* obj = selitem.getObject();
        const std::vector<std::string> &subnames = selitem.getSubNames();

        //whole object is selected
        if (subnames.empty()){
            objs.push_back(obj);
            subs.emplace_back();
        }
        else {
            for (const auto & subname : subnames) {
                objs.push_back(obj);
                subs.push_back(subname);
            }
        }
    }
    assert(objs.size()==subs.size());
    prop.setValues(objs, subs);
    return objs.size();
}

App::DocumentObject *
SelectionSingleton::getObjectOfType(const _SelObj &sel, Base::Type typeId, ResolveMode resolve, const char **subelement)
{
    auto obj = sel.pObject;
    if(!obj || !obj->getNameInDocument())
        return nullptr;
    const char *subname = sel.SubName.c_str();
    if (resolve != ResolveMode::NoResolve) {
        obj = sel.pResolvedObject;
        if ((resolve == ResolveMode::NewStyleElement || sel.elementName.second.empty())
                && !sel.elementName.first.empty())
            subname = sel.elementName.first.c_str();
        else
            subname = sel.elementName.second.c_str();
    }

    if (!obj)
        return nullptr;

    if (!obj->isDerivedFrom(typeId) && (resolve != ResolveMode::FollowLink || !obj->getLinkedObject(true)->isDerivedFrom(typeId)))
        return nullptr;

    if (subelement)
        *subelement = subname;

    return obj;
}

vector<App::DocumentObject*> SelectionSingleton::getObjectsOfType(const Base::Type& typeId, const char* pDocName, ResolveMode resolve) const
{
    std::vector<App::DocumentObject*> temp;

    App::Document *pcDoc = nullptr;
    if (!pDocName || strcmp(pDocName,"*") != 0) {
        pcDoc = getDocument(pDocName);
        if (!pcDoc)
            return temp;
    }

    std::set<App::DocumentObject*> objs;
    for(const auto &sel : _SelList.get<0>()) {
        if(pcDoc && pcDoc!=sel.pDoc) continue;
        App::DocumentObject *pObject = getObjectOfType(sel, typeId, resolve);
        if (pObject) {
            auto ret = objs.insert(pObject);
            if(ret.second)
                temp.push_back(pObject);
        }
    }

    return temp;
}

std::vector<App::DocumentObject*> SelectionSingleton::getObjectsOfType(const char* typeName, const char* pDocName, ResolveMode resolve) const
{
    Base::Type typeId = Base::Type::fromName(typeName);
    if (typeId == Base::Type::badType())
        return std::vector<App::DocumentObject*>();
    return getObjectsOfType(typeId, pDocName, resolve);
}

unsigned int SelectionSingleton::countObjectsOfType(const Base::Type& typeId, const char* pDocName, ResolveMode resolve) const
{
    unsigned int iNbr=0;
    App::Document *pcDoc = nullptr;
    if(!pDocName || strcmp(pDocName,"*") != 0) {
        pcDoc = getDocument(pDocName);
        if (!pcDoc)
            return 0;
    }

    for (const auto &sel : _SelList) {
        if((!pcDoc||pcDoc==sel.pDoc) && getObjectOfType(sel, typeId, resolve))
            iNbr++;
    }

    return iNbr;
}

unsigned int SelectionSingleton::countObjectsOfType(const char* typeName, const char* pDocName, ResolveMode resolve) const
{
    if (!typeName)
        return size();
    Base::Type typeId = Base::Type::fromName(typeName);
    if (typeId == Base::Type::badType())
        return 0;
    return countObjectsOfType(typeId, pDocName, resolve);
}


void SelectionSingleton::slotSelectionChanged(const SelectionChanges& msg)
{
    if(msg.Type == SelectionChanges::ShowSelection ||
       msg.Type == SelectionChanges::HideSelection)
        return;

    if(!msg.Object.getSubName().empty()) {
        auto pParent = msg.Object.getObject();
        if(!pParent)
            return;
        std::pair<std::string,std::string> elementName;
        auto &newElementName = elementName.first;
        auto &oldElementName = elementName.second;
        auto pObject = App::GeoFeature::resolveElement(pParent,msg.pSubName,elementName);
        if (!pObject)
            return;
        SelectionChanges msg2(msg.Type,pObject->getDocument()->getName(),
                pObject->getNameInDocument(),
                !newElementName.empty()?newElementName.c_str():oldElementName.c_str(),
                pObject->getTypeId().getName(), msg.x,msg.y,msg.z);

        try {
            msg2.pOriginalMsg = &msg;
            signalSelectionChanged3(msg2);

            msg2.Object.setSubName(oldElementName.c_str());
            msg2.pSubName = msg2.Object.getSubName().c_str();
            signalSelectionChanged2(msg2);
        }
        catch (const boost::exception&) {
            // reported by code analyzers
            Base::Console().Warning("slotSelectionChanged: Unexpected boost exception\n");
        }
    }
    else {
        try {
            signalSelectionChanged3(msg);
            signalSelectionChanged2(msg);
        }
        catch (const boost::exception&) {
            // reported by code analyzers
            Base::Console().Warning("slotSelectionChanged: Unexpected boost exception\n");
        }
    }
}

int SelectionSingleton::pushContext(const App::SubObjectT &sobj)
{
    ContextObjectStack.push_back(sobj);
    return (int)ContextObjectStack.size();
}

int SelectionSingleton::popContext()
{
    if (ContextObjectStack.empty())
        return -1;
    ContextObjectStack.pop_back();
    return (int)ContextObjectStack.size();
}

int SelectionSingleton::setContext(const App::SubObjectT &sobj)
{
    if (ContextObjectStack.size())
        ContextObjectStack.back() = sobj;
    return (int)ContextObjectStack.size();
}

const App::SubObjectT &SelectionSingleton::getContext(int pos) const
{
    if (pos >= 0 && pos < (int)ContextObjectStack.size())
        return ContextObjectStack[ContextObjectStack.size() - 1 - pos];
    static App::SubObjectT dummy;
    return dummy;
}

App::SubObjectT SelectionSingleton::getExtendedContext(App::DocumentObject *obj) const
{
    auto checkSel = [this, obj](const App::SubObjectT &sel) {
        auto sobj = sel.getSubObject();
        if (!sobj)
            return false;
        if (!obj || obj == sobj)
            return true;
        if (obj && ContextObjectStack.size()) {
            auto objs = ContextObjectStack.back().getSubObjectList();
            if (std::find(objs.begin(), objs.end(), obj) != objs.end())
                return true;
        }
        return false;
    };

    if (ContextObjectStack.size() && checkSel(ContextObjectStack.back()))
        return ContextObjectStack.back();

    if (checkSel(CurrentPreselection.Object))
        return CurrentPreselection.Object;

    if (!obj) {
        auto sels = getSelectionT(nullptr, ResolveMode::NoResolve, true);
        if (sels.size())
            return sels.front();
    } else {
        auto objs = getSelectionT(nullptr, ResolveMode::NoResolve);
        for (auto &sel : objs) {
            auto sobj = sel.getSubObject();
            if (sobj == obj)
                return sel;
        }
        for (auto &sel : objs) {
            auto sobjs = sel.getSubObjectList();
            if (std::find(sobjs.begin(), sobjs.end(), obj) != sobjs.end())
                return sel;
        }
    }

    auto gdoc = Application::Instance->editDocument();
    if (gdoc && gdoc == Application::Instance->activeDocument()) {
        auto objT = gdoc->getInEditT();
        if (checkSel(objT))
            return objT;
    }

    return App::SubObjectT();
}

SelectionSingleton::PreselectResult
SelectionSingleton::setPreselect(const char* pDocName, const char* pObjectName, const char* pSubName,
                                     float x, float y, float z, SelectionChanges::MsgSource signal, bool msg)
{
    if (!pDocName || !pObjectName) {
        rmvPreselect();
        return PreselectResult::None;
    }
    if (!pSubName) pSubName = "";

    if(DocName==pDocName && FeatName==pObjectName && SubName==pSubName) {
        if(hx!=x || hy!=y || hz!=z) {
            hx = x;
            hy = y;
            hz = z;
            CurrentPreselection.x = x;
            CurrentPreselection.y = y;
            CurrentPreselection.z = z;

            if (msg)
                format(0,0,0,x,y,z,true);

            // MovePreselect is likely going to slow down large scene rendering.
            // Disable it for now.
#if 0
            SelectionChanges Chng(SelectionChanges::MovePreselect,
                    DocName,FeatName,SubName,std::string(),x,y,z);
            notify(Chng);
#endif
        }
        return PreselectResult::Same;
    }

    // Do not restore cursor to prevent causing flash of cursor
    rmvPreselect(/*restoreCursor*/false);

    if (ActiveGate && signal != SelectionChanges::MsgSource::Internal) {
        App::Document* pDoc = getDocument(pDocName);
        if (!pDoc || !pObjectName) {
            ActiveGate->restoreCursor();
            return PreselectResult::None;
        }
        std::pair<std::string,std::string> elementName;
        auto pObject = pDoc->getObject(pObjectName);
        if(!pObject) {
            ActiveGate->restoreCursor();
            return PreselectResult::None;
        }

        const char *subelement = pSubName;
        if (gateResolve != ResolveMode::NoResolve) {
            auto &newElementName = elementName.first;
            auto &oldElementName = elementName.second;
            pObject = App::GeoFeature::resolveElement(pObject,pSubName,elementName);
            if (!pObject) {
                ActiveGate->restoreCursor();
                return PreselectResult::None;
            }
            if (gateResolve > ResolveMode::OldStyleElement)
                subelement = !newElementName.empty() ? newElementName.c_str() : oldElementName.c_str();
            else
                subelement = oldElementName.c_str();
        }
        if (!ActiveGate->allow(pObject->getDocument(), pObject, subelement)) {
            QString msg;
            if (ActiveGate->notAllowedReason.length() > 0){
                msg = QObject::tr(ActiveGate->notAllowedReason.c_str());
            } else {
                msg = QCoreApplication::translate("SelectionFilter","Not allowed:");
            }
            auto sobjT = App::SubObjectT(pDocName, pObjectName, pSubName);
            msg.append(QStringLiteral(" %1").arg(
                        QString::fromUtf8(sobjT.getSubObjectFullName().c_str())));

            if (getMainWindow()) {
                getMainWindow()->showMessage(msg);
                ActiveGate->setOverrideCursor();

                DocName = pDocName; // So that rmvPreselect() will restore the cursor
            }
            return PreselectResult::Blocked;
        }
        ActiveGate->restoreCursor();
    }

    DocName = pDocName;
    FeatName= pObjectName;
    SubName = pSubName;
    hx = x;
    hy = y;
    hz = z;

    App::SubObjectT sobjT(pDocName, pObjectName, pSubName);
    if (signal == SelectionChanges::MsgSource::TreeView && !SelectionNoTopParentCheck::enabled())
        checkTopParent(sobjT);

    // set up the change object
    SelectionChanges Chng(SelectionChanges::SetPreselect,
                          sobjT, x, y, z, signal);

    CurrentPreselection = Chng;

    auto vp = Application::Instance->getViewProvider(Chng.Object.getObject());
    if (vp) {
        // Trigger populating bounding box cache. This also makes sure the
        // invisible object gets their geometry visual populated.
        vp->getBoundingBox(Chng.Object.getSubNameNoElement().c_str());
    }

    if (msg)
        format(0,0,0,x,y,z,true);

    FC_TRACE("preselect " << sobjT.getSubObjectFullName());
    notify(Chng);

    // It is possible the preselect is removed during notification
    return DocName.empty() ? PreselectResult::None : PreselectResult::OK;
}

void SelectionSingleton::setPreselectCoord( float x, float y, float z)
{
    // if nothing is in preselect ignore
    if(CurrentPreselection.Object.getObjectName().empty())
        return;

    CurrentPreselection.x = x;
    CurrentPreselection.y = y;
    CurrentPreselection.z = z;

    format(0,0,0,x,y,z,true);
}

QString SelectionSingleton::format(App::DocumentObject *obj,
                                   const char *subname, 
                                   float x, float y, float z,
                                   bool show)
{
    if (!obj || !obj->getNameInDocument())
        return QString();
    return format(obj->getDocument()->getName(), obj->getNameInDocument(), subname, x, y, z, show);
}

QString SelectionSingleton::format(const char *docname,
                                   const char *objname,
                                   const char *subname, 
                                   float x, float y, float z,
                                   bool show)
{
    App::SubObjectT objT(docname?docname:DocName.c_str(),
                         objname?objname:FeatName.c_str(),
                         subname?subname:SubName.c_str());

    QString text;
    QTextStream ts(&text);

    auto sobj = objT.getSubObject();
    if (sobj) {
        int index = -1;
        std::string element = objT.getOldElementName(&index);
        ts << QString::fromUtf8(sobj->getNameInDocument());
        if (index > 0)
            ts << "." << QString::fromUtf8(element.c_str()) << index;
        ts << " | ";
        if (sobj->Label.getStrValue() != sobj->getNameInDocument())
            ts << QString::fromUtf8(sobj->Label.getValue()) << " | ";
    }
    else
        return QString();

    if(x != 0. || y != 0. || z != 0.) {
        auto fmt = [this](double v) -> QString {
            Base::Quantity q(v, Base::Quantity::MilliMetre.getUnit());
            double factor;
            QString unit;
            Base::UnitsApi::schemaTranslate(q, factor, unit);
            QLocale Lc;
            const Base::QuantityFormat& format = q.getFormat();
            if (format.option != Base::QuantityFormat::None) {
                uint opt = static_cast<uint>(format.option);
                Lc.setNumberOptions(static_cast<QLocale::NumberOptions>(opt));
            }
            return QStringLiteral("%1 %2").arg(
                        Lc.toString(v/factor, format.toFormat(),
                                    fmtDecimal<0 ? format.precision : fmtDecimal),
                        unit);
        };
        if (QApplication::queryKeyboardModifiers() == Qt::AltModifier) {
            ts << qSetRealNumberPrecision(std::numeric_limits<double>::digits10 + 1);
            ts << x << "; " << y << "; " << z;
        } else
            ts << fmt(x) << "; " << fmt(y) << "; " << fmt(z);
        ts << QStringLiteral(" | ");
    }

    ts << QString::fromUtf8(objT.getDocumentName().c_str()) << "#" 
       << QString::fromUtf8(objT.getObjectName().c_str()) << "."
       << QString::fromUtf8(objT.getSubName().c_str());

    PreselectionText.clear();
    if (show && getMainWindow()) {
        getMainWindow()->showMessage(text);

        std::vector<std::string> cmds;
        const auto &cmdmap = Gui::Application::Instance->commandManager().getCommands();
        for (auto it = cmdmap.upper_bound("Std_Macro_Presel"); it != cmdmap.end(); ++it) {
            if (!boost::starts_with(it->first, "Std_Macro_Presel"))
                break;
            auto cmd = dynamic_cast<MacroCommand*>(it->second);
            if (cmd && cmd->isPreselectionMacro() && cmd->getOption())
                cmds.push_back(it->first);
        }

        for (auto &name : cmds) {
            auto cmd = dynamic_cast<MacroCommand*>(
                    Gui::Application::Instance->commandManager().getCommandByName(name.c_str()));
            if (cmd)
                cmd->invoke(2);
        }
        if (cmds.empty()) 
            ToolTip::hideText();
        else {
            QPoint pt(ViewParams::getPreselectionToolTipOffsetX(),
                      ViewParams::getPreselectionToolTipOffsetY());
            ToolTip::showText(pt,
                              QString::fromUtf8(PreselectionText.c_str()),
                              Application::Instance->activeView(),
                              true,
                              (ToolTip::Corner)ViewParams::getPreselectionToolTipCorner());
        }
    }

    return text;
}

const std::string &SelectionSingleton::getPreselectionText() const
{
    return PreselectionText;
}

void SelectionSingleton::setPreselectionText(const std::string &txt) 
{
    PreselectionText = txt;
}

void SelectionSingleton::setFormatDecimal(int d)
{
    fmtDecimal = d;
}

void SelectionSingleton::rmvPreselect(bool restoreCursor)
{
    if (DocName.empty())
        return;

    SelectionChanges Chng(SelectionChanges::RmvPreselect,DocName,FeatName,SubName);

    // reset the current preselection
    CurrentPreselection = SelectionChanges();

    DocName = "";
    FeatName= "";
    SubName = "";
    hx = 0;
    hy = 0;
    hz = 0;

    if (restoreCursor && ActiveGate) {
        ActiveGate->restoreCursor();
    }

    FC_TRACE("rmv preselect");
    ToolTip::hideText();

    // notify observing objects
    notify(std::move(Chng));
}

const SelectionChanges &SelectionSingleton::getPreselection() const
{
    return CurrentPreselection;
}

// add a SelectionGate to control what is selectable
void SelectionSingleton::addSelectionGate(Gui::SelectionGate *gate, ResolveMode resolve)
{
    if (ActiveGate)
        rmvSelectionGate();

    ActiveGate = gate;
    gateResolve = resolve;
}

// remove the active SelectionGate
void SelectionSingleton::rmvSelectionGate()
{
    if (ActiveGate) {
        // Delay deletion to avoid recursion
        std::unique_ptr<Gui::SelectionGate> guard(ActiveGate);
        ActiveGate = nullptr;
        Gui::Document* doc = Gui::Application::Instance->activeDocument();
        if (doc) {
            // if a document is about to be closed it has no MDI view any more
            Gui::MDIView* mdi = doc->getActiveView();
            if (mdi)
                mdi->restoreOverrideCursor();
        }
    }
}

void SelectionGate::setOverrideCursor()
{
    if (Gui::Document* doc = Gui::Application::Instance->activeDocument()) {
        if (Gui::MDIView* mdi = doc->getActiveView())
            mdi->setOverrideCursor(Qt::ForbiddenCursor);
    }
}

void SelectionGate::restoreCursor()
{
    if (Gui::Document* doc = Gui::Application::Instance->activeDocument()) {
        if (Gui::MDIView* mdi = doc->getActiveView())
            mdi->restoreOverrideCursor();
    }
}

App::Document* SelectionSingleton::getDocument(const char* pDocName) const
{
    if (pDocName && pDocName[0])
        return App::GetApplication().getDocument(pDocName);
    else
        return App::GetApplication().getActiveDocument();
}

int SelectionSingleton::disableCommandLog() {
    if(!logDisabled)
        logHasSelection = hasSelection();
    return ++logDisabled;
}

int SelectionSingleton::enableCommandLog(bool silent) {
    --logDisabled;
    if(!logDisabled && !silent) {
        auto manager = Application::Instance->macroManager();
        if(!hasSelection()) {
            if(logHasSelection)
                manager->addLine(MacroManager::Cmt, "Gui.Selection.clearSelection()");
        }else{
            for(const auto &sel : _SelList.get<0>())
                sel.log();
        }
    }
    return logDisabled;
}

void SelectionSingleton::_SelObj::log(bool remove, bool clearPreselect) const {
    if(logged && !remove)
        return;
    logged = true;
    std::ostringstream ss;
    ss << "Gui.Selection." << (remove?"removeSelection":"addSelection")
        << "('" << DocName  << "','" << FeatName << "'";
    if(SubName.size()) {
        // Use old style indexed based selection may have ambiguity, e.g in
        // sketch, where the editing geometry and shape geometry may have the
        // same sub element name but refers to different geometry element.
        if(elementName.second.size() && elementName.first.size())
            ss << ",'" << SubName.substr(0,SubName.size()-elementName.first.size())
                << elementName.second << "'";
        else
            ss << ",'" << SubName << "'";
    }
    if(!remove && (x || y || z || !clearPreselect)) {
        if(SubName.empty())
            ss << ",''";
        ss << ',' << x << ',' << y << ',' << z;
        if(!clearPreselect)
            ss << ",False";
    }
    ss << ')';
    Application::Instance->macroManager()->addLine(MacroManager::Cmt, ss.str().c_str());
}

static bool _SelStackLock;

bool SelectionSingleton::addSelection(const char* pDocName, const char* pObjectName,
        const char* pSubName, float x, float y, float z,
        const std::vector<SelObj> *pickedList, bool clearPreselect)
{
    if(pickedList) {
        _PickedList.clear();
        for(const auto &sel : *pickedList) {
            _PickedList.emplace_back();
            auto &s = _PickedList.back();
            s.DocName = sel.DocName;
            s.FeatName = sel.FeatName;
            s.SubName = sel.SubName;
            s.TypeName = sel.TypeName;
            s.pObject = sel.pObject;
            s.pDoc = sel.pDoc;
            s.x = sel.x;
            s.y = sel.y;
            s.z = sel.z;
        }
        notify(SelectionChanges(SelectionChanges::PickedListChanged));
    }

    _SelObj temp;
    int ret = checkSelection(pDocName, pObjectName, pSubName, ResolveMode::NoResolve, temp);
    if (ret!=0)
        return false;

    temp.x        = x;
    temp.y        = y;
    temp.z        = z;

    // check for a Selection Gate
    if (ActiveGate) {
        const char *subelement = nullptr;
        auto pObject = getObjectOfType(temp,App::DocumentObject::getClassTypeId(),gateResolve,&subelement);
        if (!ActiveGate->allow(pObject?pObject->getDocument():temp.pDoc,pObject,subelement)) {
            if (getMainWindow()) {
                QString msg;
                if (ActiveGate->notAllowedReason.length() > 0) {
                    msg = QObject::tr(ActiveGate->notAllowedReason.c_str());
                } else {
                    msg = QCoreApplication::translate("SelectionFilter","Selection not allowed by filter");
                }
                getMainWindow()->showMessage(msg);
                ActiveGate->setOverrideCursor();
            }
            ActiveGate->notAllowedReason.clear();
            QApplication::beep();
            return false;
        }
    }

    if(!logDisabled)
        temp.log(false,clearPreselect);

    _SelList.get<0>().push_back(temp);

    _SelStackForward.clear();
    if (TreeParams::getRecordSelection() && !_SelStackLock)
        _selStackPush(_SelList.size() > 1);

    if(clearPreselect)
        rmvPreselect();

    SelectionChanges Chng(SelectionChanges::AddSelection,
            temp.DocName,temp.FeatName,temp.SubName,temp.TypeName, x,y,z);

    FC_LOG("Add Selection "<<Chng.pDocName<<'#'<<Chng.pObjectName<<'.'<<Chng.pSubName
            << " (" << x << ", " << y << ", " << z << ')');

    auto vp = Application::Instance->getViewProvider(Chng.Object.getObject());
    if (vp) {
        // Trigger populating bounding box cache. This also makes sure the
        // invisible object gets their geometry visual populated.
        vp->getBoundingBox(Chng.Object.getSubNameNoElement().c_str());
    }

    notify(std::move(Chng));

    getMainWindow()->updateActions();

    // There is a possibility that some observer removes or clears selection
    // inside signal handler, hence the check here
    return isSelected(temp.DocName.c_str(),temp.FeatName.c_str(), temp.SubName.c_str());
}

void SelectionSingleton::selStackPush(bool clearForward, bool overwrite) {
    // Change of behavior. If tree view record selection option is active, we'll
    // manage selection recording internally and bypass any external call of
    // stack push.
    if (!TreeParams::getRecordSelection()) {
        if (clearForward)
            _SelStackForward.clear();
        _selStackPush(overwrite);
    }
}

void SelectionSingleton::_selStackPush(bool overwrite) {
    if (SelectionPauseNotification::enabled() || _SelList.empty())
        return;
    if(_SelStackBack.size() >= ViewParams::getSelectionStackSize())
        _SelStackBack.pop_front();
    SelStackItem item;
    for(auto &sel : _SelList.get<0>())
        item.emplace_back(sel.DocName.c_str(),sel.FeatName.c_str(),sel.SubName.c_str());
    if(!_SelStackBack.empty() && _SelStackBack.back()==item)
        return;
    if(!overwrite || _SelStackBack.empty())
        _SelStackBack.emplace_back();
    _SelStackBack.back() = std::move(item);
}

void SelectionSingleton::selStackGoBack(int count, const std::vector<int> &indices, bool skipEmpty)
{
    Base::StateLocker guard(_SelStackLock);
    if((int)_SelStackBack.size()<count)
        count = _SelStackBack.size();
    if(count<=0)
        return;
    if(!_SelList.empty()) {
        _selStackPush(true);
        clearCompleteSelection();
    } else
        --count;
    for(int i=0;i<count;++i) {
        _SelStackForward.push_front(std::move(_SelStackBack.back()));
        _SelStackBack.pop_back();
    }
    std::deque<SelStackItem> tmpStack;
    _SelStackForward.swap(tmpStack);
    while(!_SelStackBack.empty()) {
        bool found = !skipEmpty || !indices.empty();
        int idx = -1;
        for(auto &sobjT : _SelStackBack.back()) {
            ++idx;
            if (!indices.empty() && std::find(indices.begin(), indices.end(), idx) == indices.end())
                continue;
            if(sobjT.getSubObject()) {
                addSelection(sobjT.getDocumentName().c_str(),
                             sobjT.getObjectName().c_str(),
                             sobjT.getSubName().c_str());
                found = true;
            }
        }
        if(found)
            break;
        tmpStack.push_front(std::move(_SelStackBack.back()));
        _SelStackBack.pop_back();
    }
    _SelStackForward = std::move(tmpStack);
    getMainWindow()->updateActions();
    if (TreeParams::getSyncSelection())
        TreeWidget::scrollItemToTop();
}

void SelectionSingleton::selStackGoForward(int count, const std::vector<int> &indices, bool skipEmpty)
{
    Base::StateLocker guard(_SelStackLock);
    if((int)_SelStackForward.size()<count)
        count = _SelStackForward.size();
    if(count<=0)
        return;
    if(!_SelList.empty()) {
        _selStackPush(true);
        clearCompleteSelection();
    }
    for(int i=0;i<count;++i) {
        _SelStackBack.push_back(_SelStackForward.front());
        _SelStackForward.pop_front();
    }
    std::deque<SelStackItem> tmpStack;
    _SelStackForward.swap(tmpStack);
    while(1) {
        bool found = !skipEmpty || !indices.empty();
        int idx = -1;
        for(auto &sobjT : _SelStackBack.back()) {
            ++idx;
            if (!indices.empty() && std::find(indices.begin(), indices.end(), idx) == indices.end())
                continue;
            if(sobjT.getSubObject()) {
                addSelection(sobjT.getDocumentName().c_str(),
                            sobjT.getObjectName().c_str(),
                            sobjT.getSubName().c_str());
                found = true;
            }
        }
        if(found || tmpStack.empty())
            break;
        _SelStackBack.push_back(tmpStack.front());
        tmpStack.pop_front();
    }
    _SelStackForward = std::move(tmpStack);
    getMainWindow()->updateActions();
    if (TreeParams::getSyncSelection())
        TreeWidget::scrollItemToTop();
}

std::vector<SelectionObject> SelectionSingleton::selStackGet(const char* pDocName, ResolveMode resolve, int index) const
{
    if (!pDocName)
        pDocName = "*";
    const SelStackItem *item = nullptr;
    if(index>=0) {
        if(index>=(int)_SelStackBack.size())
            return {};
        item = &_SelStackBack[_SelStackBack.size()-1-index];
    }
    else {
        index = -index-1;
        if(index>=(int)_SelStackForward.size())
            return {};
        item = &_SelStackForward[index];
    }

    SelContainer selList;
    for(auto &sobjT : *item) {
        _SelObj sel;
        if(checkSelection(sobjT.getDocumentName().c_str(),
                          sobjT.getObjectName().c_str(),
                          sobjT.getSubName().c_str(),
                          ResolveMode::NoResolve,
                          sel,
                          &selList)!=-1)
        {
            selList.get<0>().push_back(sel);
        }
    }

    return getObjectList(pDocName,App::DocumentObject::getClassTypeId(), selList.get<0>(), resolve);
}

std::vector<App::SubObjectT>
SelectionSingleton::selStackGetT(const char* pDocName, ResolveMode resolve, int index) const
{
    std::vector<App::SubObjectT> res;
    for (const auto &sel : selStackGet(pDocName, resolve, index)) {
        if (sel.getSubNames().empty())
            res.emplace_back(sel.getDocName(), sel.getFeatName(), "");
        else {
            for (const auto &sub : sel.getSubNames())
                res.emplace_back(sel.getDocName(), sel.getFeatName(), sub.c_str());
        }
    }
    return res;
}

int SelectionSingleton::addSelections(const std::vector<App::SubObjectT> &objs)
{
    if(!logDisabled) {
        std::ostringstream ss;
        ss << "Gui.Selection.addSelections([";
        size_t count = 0;
        for (const auto &objT : objs) {
            ss << objT.getSubObjectPython(false) << ", ";
            if (++count >= 10)
                break;
        }
        ss << "])";
        if (objs.size() > count)
            ss << " # with more objects not shown...";
        Application::Instance->macroManager()->addLine(MacroManager::Cmt, ss.str().c_str());
    }
    int count = 0;
    SelectionPauseNotification guard;
    SelectionLogDisabler disabler(true);
    for (const auto &objT : objs) {
        if (addSelection(objT))
            ++count;
    }
    return count;
}

int SelectionSingleton::addSelections(const char* pDocName, const char* pObjectName, const std::vector<std::string>& pSubNames)
{
    std::vector<App::SubObjectT> objs;
    App::SubObjectT objT(pDocName, pObjectName, "");
    for (const auto &sub : pSubNames) {
        objT.setSubName(sub);
        objs.push_back(objT);
    }
    return addSelections(objs);
}

bool SelectionSingleton::updateSelection(bool show, const char* pDocName,
                            const char* pObjectName, const char* pSubName)
{
    if(!pDocName || !pObjectName)
        return false;
    if(!pSubName)
        pSubName = "";
    auto pDoc = getDocument(pDocName);
    if(!pDoc)
        return false;
    auto pObject = pDoc->getObject(pObjectName);
    if(!pObject) return false;
    _SelObj sel;
    if(checkSelection(pDocName,pObjectName,pSubName,ResolveMode::NoResolve,sel,&_SelList)<=0)
        return false;
    if(DocName==sel.DocName && FeatName==sel.FeatName && SubName==sel.SubName) {
        if(show) {
            FC_TRACE("preselect signal");
            notify(SelectionChanges(SelectionChanges::SetPreselect,DocName,FeatName,SubName));
        }else
            rmvPreselect();
    }
    SelectionChanges Chng(show?SelectionChanges::ShowSelection:SelectionChanges::HideSelection,
            sel.DocName, sel.FeatName, sel.SubName, sel.pObject->getTypeId().getName());

    FC_LOG("Update Selection "<<Chng.pDocName << '#' << Chng.pObjectName << '.' <<Chng.pSubName);

    notify(std::move(Chng));

    return true;
}

bool SelectionSingleton::addSelection(const SelectionObject& obj, bool clearPreselect)
{
    const std::vector<std::string>& subNames = obj.getSubNames();
    const std::vector<Base::Vector3d> points = obj.getPickedPoints();
    if (!subNames.empty() && subNames.size() == points.size()) {
        bool ok = true;
        for (std::size_t i=0; i<subNames.size(); i++) {
            const std::string& name = subNames[i];
            const Base::Vector3d& pnt = points[i];
            ok &= addSelection(obj.getDocName(), obj.getFeatName(), name.c_str(),
                               static_cast<float>(pnt.x),
                               static_cast<float>(pnt.y),
                               static_cast<float>(pnt.z),nullptr,clearPreselect);
        }
        return ok;
    }
    else if (!subNames.empty()) {
        bool ok = true;
        for (std::size_t i=0; i<subNames.size(); i++) {
            const std::string& name = subNames[i];
            ok &= addSelection(obj.getDocName(), obj.getFeatName(), name.c_str());
        }
        return ok;
    }
    else {
        return addSelection(obj.getDocName(), obj.getFeatName());
    }
}


void SelectionSingleton::rmvSelection(const char* pDocName, const char* pObjectName, const char* pSubName,
        const std::vector<SelObj> *pickedList)
{
    if(pickedList) {
        _PickedList.clear();
        for(const auto &sel : *pickedList) {
            _PickedList.emplace_back();
            auto &s = _PickedList.back();
            s.DocName = sel.DocName;
            s.FeatName = sel.FeatName;
            s.SubName = sel.SubName;
            s.TypeName = sel.TypeName;
            s.pObject = sel.pObject;
            s.pDoc = sel.pDoc;
            s.x = sel.x;
            s.y = sel.y;
            s.z = sel.z;
        }
        notify(SelectionChanges(SelectionChanges::PickedListChanged));
    }

    if(!pDocName)
        return;

    _SelObj temp;
    int ret = checkSelection(pDocName, pObjectName, pSubName, ResolveMode::NoResolve, temp);
    if (ret<0)
        return;

    std::vector<SelectionChanges> changes;
    for(auto It=_SelList.get<0>().begin(),ItNext=It;It!=_SelList.get<0>().end();It=ItNext) {
        ++ItNext;
        if(It->DocName!=temp.DocName || It->FeatName!=temp.FeatName)
            continue;

        // If no sub-element (e.g. Face, Edge) is specified, remove all
        // sub-element selection of the matching sub-object.

        if(!temp.elementName.second.empty()) {
            if (!boost::equals(It->SubName, temp.SubName))
                continue;
        } else if (!boost::starts_with(It->SubName,temp.SubName))
            continue;
        else if (auto element = Data::ComplexGeoData::findElementName(It->SubName.c_str())) {
            if (element - It->SubName.c_str() != (int)temp.SubName.size())
                continue;
        }

        It->log(true);

        changes.emplace_back(SelectionChanges::RmvSelection,
                It->DocName,It->FeatName,It->SubName,It->TypeName);

        // destroy the _SelObj item
        _SelList.get<0>().erase(It);
    }

    // NOTE: It can happen that there are nested calls of rmvSelection()
    // so that it's not safe to invoke the notifications inside the loop
    // as this can invalidate the iterators and thus leads to undefined
    // behaviour.
    // So, the notification is done after the loop, see also #0003469
    if(!changes.empty()) {
        if (!_SelList.empty()) {
            _SelStackForward.clear();
            if (TreeParams::getRecordSelection() && !_SelStackLock)
                _selStackPush(true);
        }

        for(auto &Chng : changes) {
            FC_LOG("Rmv Selection "<<Chng.pDocName<<'#'<<Chng.pObjectName<<'.'<<Chng.pSubName);
            notify(std::move(Chng));
        }
        getMainWindow()->updateActions();
    }
}

struct SelInfo {
    std::string DocName;
    std::string FeatName;
    std::string SubName;
    SelInfo(const std::string &docName,
            const std::string &featName,
            const std::string &subName)
        :DocName(docName)
        ,FeatName(featName)
        ,SubName(subName)
    {}
};

void SelectionSingleton::setVisible(VisibleState vis, const std::vector<App::SubObjectT> &_sels) {
    std::set<std::pair<App::DocumentObject*,App::DocumentObject*> > filter;
    int visible;
    switch(vis) {
    case VisShow:
        visible = 1;
        break;
    case VisToggle:
        visible = -1;
        break;
    default:
        visible = 0;
    }

    const auto &sels = _sels.size()?_sels:getSelectionT(nullptr, ResolveMode::NoResolve);
    for(auto &sel : sels) {
        App::DocumentObject *obj = sel.getObject();
        if(!obj) continue;

        // get parent object
        App::DocumentObject *parent = nullptr;
        std::string elementName;
        obj = obj->resolve(sel.getSubName().c_str(),&parent,&elementName);
        if (!obj || !obj->getNameInDocument() || (parent && !parent->getNameInDocument()))
            continue;
        // try call parent object's setElementVisible
        if (parent) {
            // prevent setting the same object visibility more than once
            if (!filter.insert(std::make_pair(obj,parent)).second)
                continue;
            int visElement = parent->isElementVisible(elementName.c_str());
            if (visElement >= 0) {
                if (visElement > 0)
                    visElement = 1;
                if (visible >= 0) {
                    if (visElement == visible)
                        continue;
                    visElement = visible;
                }
                else {
                    visElement = !visElement;
                }

                if(!visElement)
                    updateSelection(false,
                                    sel.getDocumentName().c_str(),
                                    sel.getObjectName().c_str(),
                                    sel.getSubName().c_str());

                parent->setElementVisible(elementName.c_str(),visElement?true:false);

                if(visElement && ViewParams::getUpdateSelectionVisual())
                    updateSelection(true,
                                    sel.getDocumentName().c_str(),
                                    sel.getObjectName().c_str(),
                                    sel.getSubName().c_str());
                continue;
            }

            // Fall back to direct object visibility setting
        }
        if(!filter.insert(std::make_pair(obj,static_cast<App::DocumentObject*>(nullptr))).second){
            continue;
        }

        auto vp = Application::Instance->getViewProvider(obj);

        if(vp) {
            bool visObject;
            if(visible>=0)
                visObject = visible ? true : false;
            else
                visObject = !vp->isShow();

            SelectionNoTopParentCheck guard;
            if(visObject) {
                vp->show();
                if(ViewParams::getUpdateSelectionVisual())
                    updateSelection(true,
                                    sel.getDocumentName().c_str(),
                                    sel.getObjectName().c_str(),
                                    sel.getSubName().c_str());
            } else {
                updateSelection(false,
                                sel.getDocumentName().c_str(),
                                sel.getObjectName().c_str(),
                                sel.getSubName().c_str());
                vp->hide();
            }
        }
    }
}

void SelectionSingleton::setSelection(const std::vector<App::DocumentObject*>& sel)
{
    std::vector<App::SubObjectT> objs;
    for (const auto &obj : sel)
        objs.emplace_back(obj);
    addSelections(objs);
}

void SelectionSingleton::clearSelection(const char* pDocName, bool clearPreSelect)
{
    // Because the introduction of external editing, it is best to make
    // clearSelection(0) behave as clearCompleteSelection(), which is the same
    // behavior of python Selection.clearSelection(None)
    if (!pDocName || !pDocName[0] || strcmp(pDocName,"*")==0) {
        clearCompleteSelection(clearPreSelect);
        return;
    }

    if (!_PickedList.empty()) {
        _PickedList.clear();
        notify(SelectionChanges(SelectionChanges::PickedListChanged));
    }

    App::Document* pDoc;
    pDoc = getDocument(pDocName);
    if (pDoc) {
        std::string docName = pDocName;
        if (clearPreSelect && DocName == docName)
            rmvPreselect();

        bool touched = false;
        for (auto it=_SelList.get<0>().begin();it!=_SelList.get<0>().end();) {
            if (it->DocName == docName) {
                touched = true;
                it = _SelList.get<0>().erase(it);
            }
            else {
                ++it;
            }
        }

        if (!touched)
            return;

        if (!logDisabled) {
            std::ostringstream ss;
            ss << "Gui.Selection.clearSelection('" << docName << "'";
            if (!clearPreSelect)
                ss << ", False";
            ss << ')';
            Application::Instance->macroManager()->addLine(MacroManager::Cmt,ss.str().c_str());
        }

        notify(SelectionChanges(SelectionChanges::ClrSelection,docName.c_str()));

        getMainWindow()->updateActions();
    }
}

void SelectionSingleton::clearCompleteSelection(bool clearPreSelect)
{
    if(!_PickedList.empty()) {
        _PickedList.clear();
        notify(SelectionChanges(SelectionChanges::PickedListChanged));
    }

    if(clearPreSelect)
        rmvPreselect();

    if(_SelList.empty())
        return;

    if(!logDisabled)
        Application::Instance->macroManager()->addLine(MacroManager::Cmt,
                clearPreSelect?"Gui.Selection.clearSelection()"
                              :"Gui.Selection.clearSelection(False)");

    _SelList.clear();

    SelectionChanges Chng(SelectionChanges::ClrSelection);

    FC_LOG("Clear selection");

    notify(std::move(Chng));
    getMainWindow()->updateActions();
}

bool SelectionSingleton::isSelected(const char* pDocName, const char* pObjectName,
                                    const char* pSubName, ResolveMode resolve) const
{
    _SelObj sel;
    return checkSelection(pDocName, pObjectName, pSubName, resolve, sel, &_SelList) > 0;
}

bool SelectionSingleton::isSelected(App::DocumentObject* pObject, const char* pSubName, ResolveMode resolve) const
{
    if (!pObject || !pObject->getNameInDocument() || !pObject->getDocument())
        return false;
    _SelObj sel;
    return checkSelection(pObject->getDocument()->getName(),
            pObject->getNameInDocument(), pSubName, resolve, sel, &_SelList) > 0;
}

void SelectionSingleton::checkTopParent(App::DocumentObject *&obj, std::string &subname)
{
    TreeWidget::checkTopParent(obj,subname);
}

bool SelectionSingleton::checkTopParent(App::SubObjectT &sobjT) {
    auto obj = sobjT.getObject();
    auto subname = sobjT.getSubName();
    auto parent = obj;
    TreeWidget::checkTopParent(parent,subname);
    if (parent != obj) {
        sobjT = App::SubObjectT(parent, subname.c_str());
        return true;
    }
    return false;
}

int SelectionSingleton::checkSelection(const char *pDocName, const char *pObjectName, const char *pSubName,
                                       ResolveMode resolve, _SelObj &sel, const SelContainer *selList) const
{
    sel.pDoc = getDocument(pDocName);
    if(!sel.pDoc) {
        if(!selList)
            FC_ERR("Cannot find document");
        return -1;
    }
    pDocName = sel.pDoc->getName();
    sel.DocName = pDocName;

    if(pObjectName)
        sel.pObject = sel.pDoc->getObject(pObjectName);
    else
        sel.pObject = nullptr;
    if (!sel.pObject) {
        if(!selList)
            FC_ERR("Object not found");
        return -1;
    }
    if (sel.pObject->testStatus(App::ObjectStatus::Remove))
        return -1;
    if (pSubName)
       sel.SubName = pSubName;
    if (resolve == ResolveMode::NoResolve)
        checkTopParent(sel.pObject,sel.SubName);
    pSubName = sel.SubName.size()?sel.SubName.c_str():nullptr;
    sel.FeatName = sel.pObject->getNameInDocument();
    sel.TypeName = sel.pObject->getTypeId().getName();
    const char *element = nullptr;
    sel.pResolvedObject = App::GeoFeature::resolveElement(sel.pObject,
            pSubName,sel.elementName,false,App::GeoFeature::Normal,nullptr,&element);
    if(!sel.pResolvedObject) {
        if(!selList)
            FC_ERR("Sub-object " << sel.DocName << '#' << sel.FeatName << '.' << sel.SubName << " not found");
        return -1;
    }
    if(sel.pResolvedObject->testStatus(App::ObjectStatus::Remove))
        return -1;
    std::string subname;
    std::string prefix;
    if(pSubName && element) {
        prefix = std::string(pSubName, element-pSubName);
        if(!sel.elementName.first.empty()) {
            // make sure the selected sub name is a new style if available
            subname = prefix + sel.elementName.first;
            pSubName = subname.c_str();
            sel.SubName = subname;
        }
    }
    if(!selList)
        selList = &_SelList;

    if(!pSubName)
        pSubName = "";

    const auto &selMap = selList->get<1>();
    auto it = selMap.lower_bound(sel);
    if (it != selMap.end()) {
        auto &s = *it;
        if (s.DocName==pDocName && s.FeatName==sel.FeatName) {
            if(s.SubName==pSubName)
                return 1;
            if (resolve > ResolveMode::OldStyleElement && boost::starts_with(s.SubName,prefix))
                return 1;
        }
    }
    if(resolve == ResolveMode::OldStyleElement) {
        const auto &resolvedMap = selList->get<2>();
        for (auto it = resolvedMap.lower_bound(sel.pResolvedObject); it != resolvedMap.end(); ++it) {
            const auto &s = *it;
            if(s.pResolvedObject != sel.pResolvedObject)
                break;
            if(!pSubName[0])
                return 1;
            if (!s.elementName.first.empty()) {
                if (s.elementName.first == sel.elementName.first)
                    return 1;
            }
            else if(s.SubName == sel.elementName.second)
                return 1;
        }
    }
    return 0;
}

const char *SelectionSingleton::getSelectedElement(App::DocumentObject *obj, const char* pSubName) const
{
    if (!obj)
        return nullptr;

    std::pair<std::string,std::string> elementName;
    if(!App::GeoFeature::resolveElement(obj,pSubName,elementName,true))
        return 0;

    if(elementName.first.size())
        pSubName = elementName.first.c_str();

    for(auto It = _SelList.get<0>().begin();It != _SelList.get<0>().end();++It) {
        if (It->pObject == obj) {
            auto len = It->SubName.length();
            if(!len)
                return "";
            if (pSubName && strncmp(pSubName,It->SubName.c_str(),It->SubName.length())==0){
                if(pSubName[len]==0 || pSubName[len-1] == '.')
                    return It->SubName.c_str();
            }
        }
    }
    return nullptr;
}

void SelectionSingleton::slotDeletedObject(const App::DocumentObject& Obj)
{
    if(!Obj.getNameInDocument())
        return;

    // For safety reason, don't bother checking
    rmvPreselect();

    auto pObj = const_cast<App::DocumentObject*>(&Obj);

    // Remove also from the selection, if selected
    // We don't walk down the hierarchy for each selection, so there may be stray selection
    std::vector<SelectionChanges> changes;
    for(auto it=_SelList.get<2>().lower_bound(pObj); it!=_SelList.get<2>().end(); ) {
        if (it->pResolvedObject != pObj)
            break;
        changes.emplace_back(SelectionChanges::RmvSelection,
                it->DocName,it->FeatName,it->SubName,it->TypeName);
        it = _SelList.get<2>().erase(it);
    }
    for(auto it=_SelList.get<3>().lower_bound(pObj); it!=_SelList.get<3>().end(); ) {
        if (it->pObject != pObj)
            break;
        changes.emplace_back(SelectionChanges::RmvSelection,
                it->DocName,it->FeatName,it->SubName,it->TypeName);
        it = _SelList.get<3>().erase(it);
    }
    if(!changes.empty()) {
        for(auto &Chng : changes) {
            FC_LOG("Rmv Selection "<<Chng.pDocName<<'#'<<Chng.pObjectName<<'.'<<Chng.pSubName);
            notify(std::move(Chng));
        }
        getMainWindow()->updateActions();
    }

    if(!_PickedList.empty()) {
        bool changed = false;
        for(auto it=_PickedList.begin(),itNext=it;it!=_PickedList.end();it=itNext) {
            ++itNext;
            auto &sel = *it;
            if(sel.pObject == &Obj) {
                changed = true;
                _PickedList.erase(it);
            }
        }
        if(changed)
            notify(SelectionChanges(SelectionChanges::PickedListChanged));
    }
}


//**************************************************************************
// Construction/Destruction

/**
 * A constructor.
 * A more elaborate description of the constructor.
 */
SelectionSingleton::SelectionSingleton()
    :CurrentPreselection(SelectionChanges::ClrSelection)
    ,_needPickedList(false)
{
    hx = 0;
    hy = 0;
    hz = 0;
    ActiveGate = nullptr;
    gateResolve = ResolveMode::OldStyleElement;
    App::GetApplication().signalDeletedObject.connect(boost::bind(&Gui::SelectionSingleton::slotDeletedObject, this, bp::_1));
    signalSelectionChanged.connect(boost::bind(&Gui::SelectionSingleton::slotSelectionChanged, this, bp::_1));

    auto hGrp = App::GetApplication().GetParameterGroupByPath(
            "User parameter:BaseApp/Preferences/Units");
    fmtDecimal = hGrp->GetInt("DecimalsPreSel",-1);
}

/**
 * A destructor.
 * A more elaborate description of the destructor.
 */
SelectionSingleton::~SelectionSingleton()
{
}

SelectionSingleton* SelectionSingleton::_pcSingleton = nullptr;

SelectionSingleton& SelectionSingleton::instance()
{
    if (!_pcSingleton)
        _pcSingleton = new SelectionSingleton;
    return *_pcSingleton;
}

void SelectionSingleton::destruct ()
{
    if (_pcSingleton)
        delete _pcSingleton;
    _pcSingleton = nullptr;
}

//**************************************************************************
// Python stuff

// SelectionSingleton Methods  // Methods structure
PyMethodDef SelectionSingleton::Methods[] = {
    {"addSelection",         (PyCFunction) SelectionSingleton::sAddSelection, METH_VARARGS,
     "addSelection(docName, objName, subName, x=0, y=0, z=0, clear=True) -> None\n"
     "addSelection(obj, subName, x=0, y=0, z=0, clear=True) -> None\n"
     "addSelection(obj, subNames, clear=True) -> None\n"
     "\n"
     "Add an object to the selection.\n"
     "\n"
     "docName : str\n    Name of the `App.Document`.\n"
     "objName : str\n    Name of the `App.DocumentObject` to add.\n"
     "obj : App.DocumentObject\n    Object to add.\n"
     "subName : str\n    Subelement name.\n"
     "x : float\n    Coordinate `x` of the point to pick.\n"
     "y : float\n    Coordinate `y` of the point to pick.\n"
     "z : float\n    Coordinate `z` of the point to pick.\n"
     "subNames : list of str\n    List of subelement names.\n"
     "clear : bool\n    Clear preselection."},
    {"addSelections",        (PyCFunction) SelectionSingleton::sAddSelections, METH_VARARGS,
     "Add a number of objects to the selection\n"
     "addSelections(sels : Sequence[DocumentObject|Tuple[DocumentObject, String]])"},
    {"updateSelection",      (PyCFunction) SelectionSingleton::sUpdateSelection, METH_VARARGS,
     "updateSelection(show, obj, subName) -> None\n"
     "\n"
     "Update an object in the selection.\n"
     "\n"
     "show : bool\n    Show or hide the selection.\n"
     "obj : App.DocumentObject\n    Object to update.\n"
     "subName : str\n    Name of the subelement to update."},
    {"removeSelection",      (PyCFunction) SelectionSingleton::sRemoveSelection, METH_VARARGS,
     "removeSelection(obj, subName) -> None\n"
     "removeSelection(docName, objName, subName) -> None\n"
     "\n"
     "Remove an object from the selection.\n"
     "\n"
     "docName : str\n    Name of the `App.Document`.\n"
     "objName : str\n    Name of the `App.DocumentObject` to remove.\n"
     "obj : App.DocumentObject\n    Object to remove.\n"
     "subName : str\n    Name of the subelement to remove."},
    {"clearSelection"  ,     (PyCFunction) SelectionSingleton::sClearSelection, METH_VARARGS,
     "clearSelection(docName, clearPreSelect=True) -> None\n"
     "clearSelection(clearPreSelect=True) -> None\n"
     "\n"
     "Clear the selection in the given document. If no document is\n"
     "given the complete selection is cleared.\n"
     "\n"
     "docName : str\n    Name of the `App.Document`.\n"
     "clearPreSelect : bool\n    Clear preselection."},
    {"isSelected",           (PyCFunction) SelectionSingleton::sIsSelected, METH_VARARGS,
     "isSelected(obj, subName, resolve=ResolveMode.OldStyleElement) -> bool\n"
     "\n"
     "Check if a given object is selected.\n"
     "\n"
     "obj : App.DocumentObject\n    Object to check.\n"
     "subName : str\n    Name of the subelement.\n"
     "resolve : int\n    Resolve subelement reference."},
    {"setPreselection",      reinterpret_cast<PyCFunction>(reinterpret_cast<void (*) ()>( SelectionSingleton::sSetPreselection )), METH_VARARGS|METH_KEYWORDS,
     "setPreselection(obj, subName, x=0, y=0, z=0, type=1) -> None\n"
     "\n"
     "Set preselected object.\n"
     "\n"
     "obj : App.DocumentObject\n    Object to preselect.\n"
     "subName : str\n    Subelement name.\n"
     "x : float\n    Coordinate `x` of the point to preselect.\n"
     "y : float\n    Coordinate `y` of the point to preselect.\n"
     "z : float\n    Coordinate `z` of the point to preselect.\n"
     "type : int"},
    {"getPreselection",      (PyCFunction) SelectionSingleton::sGetPreselection, METH_VARARGS,
    "getPreselection() -> Gui.SelectionObject\n"
    "\n"
    "Get preselected object."},
    {"clearPreselection",   (PyCFunction) SelectionSingleton::sRemPreselection, METH_VARARGS,
     "clearPreselection() -> None\n"
     "\n"
     "Clear the preselection."},
    {"setPreselectionText", (PyCFunction) SelectionSingleton::sSetPreselectionText, METH_VARARGS,
     "setPreselectionText() -- Set preselection message text"},
    {"getPreselectionText", (PyCFunction) SelectionSingleton::sGetPreselectionText, METH_VARARGS,
     "getPreselectionText() -- Get preselected message text"},
    {"countObjectsOfType",   (PyCFunction) SelectionSingleton::sCountObjectsOfType, METH_VARARGS,
     "countObjectsOfType(type, docName, resolve=ResolveMode.OldStyleElement) -> int\n"
     "\n"
     "Get the number of selected objects. If no document name is given the\n"
     "active document is used and '*' means all documents.\n"
     "\n"
     "type : str\n    Object type id name.\n"
     "docName : str\n    Name of the `App.Document`.\n"
     "resolve : int"},
    {"getSelection",         (PyCFunction) SelectionSingleton::sGetSelection, METH_VARARGS,
     "getSelection(docName, resolve=ResolveMode.OldStyleElement, single=False) -> list\n"
     "\n"
     "Return a list of selected objects. If no document name is given\n"
     "the active document is used and '*' means all documents.\n"
     "\n"
     "docName : str\n    Name of the `App.Document`.\n"
     "resolve : int\n    Resolve the subname references.\n"
     "    0: do not resolve, 1: resolve, 2: resolve with element map.\n"
     "single : bool\n    Only return if there is only one selection."},
    {"getPickedList",         (PyCFunction) SelectionSingleton::sGetPickedList, 1,
     "getPickedList(docName) -> list of Gui.SelectionObject\n"
     "\n"
     "Return a list of SelectionObjects generated by the last mouse click.\n"
     "If no document name is given the active document is used and '*'\n"
     "means all documents.\n"
     "\n"
     "docName : str\n    Name of the `App.Document`."},
    {"enablePickedList",      (PyCFunction) SelectionSingleton::sEnablePickedList, METH_VARARGS,
     "enablePickedList(enable=True) -> None\n"
     "\n"
     "Enable/disable pick list.\n"
     "\n"
     "enable : bool"},
    {"needPickedList",      (PyCFunction) SelectionSingleton::sNeedPickedList, METH_VARARGS,
     "Check whether picked list is enabled\n"
     "needPickedList() -> boolean"},
    {"getCompleteSelection", (PyCFunction) SelectionSingleton::sGetCompleteSelection, METH_VARARGS,
     "getCompleteSelection(resolve=ResolveMode.OldStyleElement) -> list\n"
     "\n"
     "Return a list of selected objects across all documents.\n"
     "\n"
     "resolve : int"},
    {"getSelectionEx",         (PyCFunction) SelectionSingleton::sGetSelectionEx, METH_VARARGS,
     "getSelectionEx(docName, resolve=ResolveMode.OldStyleElement, single=False) -> list of Gui.SelectionObject\n"
     "\n"
     "Return a list of SelectionObjects. If no document name is given the\n"
     "active document is used and '*' means all documents.\n"
     "The SelectionObjects contain a variety of information about the selection,\n"
     "e.g. subelement names.\n"
     "\n"
     "docName : str\n    Name of the `App.Document`.\n"
     "resolve : int\n    Resolve the subname references.\n"
     "    0: do not resolve, 1: resolve, 2: resolve with element map.\n"
     "single : bool\n    Only return if there is only one selection."},
    {"getSelectionObject",  (PyCFunction) SelectionSingleton::sGetSelectionObject, METH_VARARGS,
     "getSelectionObject(docName, objName, subName, point) -> Gui.SelectionObject\n"
     "\n"
     "Return a SelectionObject.\n"
     "\n"
     "docName : str\n    Name of the `App.Document`.\n"
     "objName : str\n    Name of the `App.DocumentObject` to select.\n"
     "subName : str\n    Subelement name.\n"
     "point : tuple\n    Coordinates of the point to pick."},
    {"addObserver",         (PyCFunction) SelectionSingleton::sAddSelObserver, METH_VARARGS,
     "addObserver(object, resolve=ResolveMode.OldStyleElement) -> None\n"
     "\n"
     "Install an observer.\n"
     "\n"
     "object : object\n    Python object instance.\n"
     "resolve : int"},
    {"removeObserver",      (PyCFunction) SelectionSingleton::sRemSelObserver, METH_VARARGS,
     "removeObserver(object) -> None\n"
     "\n"
     "Uninstall an observer.\n"
     "\n"
     "object : object\n    Python object instance."},
    {"addSelectionGate",      (PyCFunction) SelectionSingleton::sAddSelectionGate, METH_VARARGS,
     "addSelectionGate(filter, resolve=ResolveMode.OldStyleElement) -> None\n"
     "\n"
     "Activate the selection gate.\n"
     "The selection gate will prohibit all selections that do not match\n"
     "the given selection criteria.\n"
     "\n"
     "filter : str, SelectionFilter, object\n"
     "resolve : int\n"
     "\n"
     "Examples strings are:\n"
     "Gui.Selection.addSelectionGate('SELECT Part::Feature SUBELEMENT Edge')\n"
     "Gui.Selection.addSelectionGate('SELECT Robot::RobotObject')\n"
     "\n"
     "An instance of SelectionFilter can also be set:\n"
     "filter = Gui.Selection.Filter('SELECT Part::Feature SUBELEMENT Edge')\n"
     "Gui.Selection.addSelectionGate(filter)\n"
     "\n"
     "The most flexible approach is to write a selection gate class that\n"
     "implements the method 'allow':\n"
     "class Gate:\n"
     "    def allow(self,doc,obj,sub):\n"
     "        return (sub[0:4] == 'Face')\n"
     "Gui.Selection.addSelectionGate(Gate())"},
    {"removeSelectionGate",      (PyCFunction) SelectionSingleton::sRemoveSelectionGate, METH_VARARGS,
     "removeSelectionGate() -> None\n"
     "\n"
     "Remove the active selection gate."},
    {"setVisible",            (PyCFunction) SelectionSingleton::sSetVisible, METH_VARARGS,
     "setVisible(visible=None) -> None\n"
     "\n"
     "Set visibility of all selection items.\n"
     "\n"
     "visible : bool, None\n    If None, then toggle visibility."},
    {"pushSelStack",      (PyCFunction) SelectionSingleton::sPushSelStack, METH_VARARGS,
     "pushSelStack(clearForward=True, overwrite=False) -> None\n"
     "\n"
     "Push current selection to stack.\n"
     "\n"
     "clearForward : bool\n    Clear the forward selection stack.\n"
     "overwrite : bool\n    Overwrite the top back selection stack with current selection."},
    {"hasSelection",      (PyCFunction) SelectionSingleton::sHasSelection, METH_VARARGS,
     "hasSelection(docName, resolve=ResolveMode.NoResolve) -> bool\n"
     "\n"
     "Check if there is any selection. If no document name is given,\n"
     "checks selections in all documents.\n"
     "\n"
     "docName : str\n    Name of the `App.Document`.\n"
     "resolve : int"},
    {"hasSubSelection",   (PyCFunction) SelectionSingleton::sHasSubSelection, METH_VARARGS,
     "hasSubSelection(docName, subElement=False) -> bool\n"
     "\n"
     "Check if there is any selection with subname. If no document name\n"
     "is given the active document is used and '*' means all documents.\n"
     "\n"
     "docName : str\n    Name of the `App.Document`.\n"
     "subElement : bool"},
    {"getSelectionFromStack",(PyCFunction) SelectionSingleton::sGetSelectionFromStack, METH_VARARGS,
     "getSelectionFromStack(docName, resolve=ResolveMode.OldStyleElement, index=0) -> list of Gui.SelectionObject\n"
     "\n"
     "Return SelectionObjects from selection stack. If no document name is given\n"
     "the active document is used and '*' means all documents.\n"
     "\n"
     "docName : str\n    Name of the `App.Document`.\n"
     "resolve : int\n    Resolve the subname references.\n"
     "    0: do not resolve, 1: resolve, 2: resolve with element map.\n"
     "index : int\n    Select stack index.\n"
     "    0: last pushed selection, > 0: trace back, < 0: trace forward."},
    {"checkTopParent",   (PyCFunction) SelectionSingleton::sCheckTopParent, METH_VARARGS,
     "checkTopParent(obj, subname='')\n\n"
     "Check object hierarchy to find the top parent of the given (sub)object.\n"
     "Returns (topParent,subname)\n"},
    {"pushContext",   (PyCFunction) SelectionSingleton::sPushContext, METH_VARARGS,
     "pushContext(obj, subname='') -> Int\n\n"
     "Push a context object into stack for use by ViewObject.doubleClicked/setupContextMenu().\n"
     "Return the stack size after push."},
    {"popContext",   (PyCFunction) SelectionSingleton::sPopContext, METH_VARARGS,
     "popContext() -> Int\n\n"
      "Pop context object. Return the context stack size after push."},
    {"setContext",   (PyCFunction) SelectionSingleton::sSetContext, METH_VARARGS,
     "setContext(obj, subname='') -> Int\n\n"
     "Set the context object at the top of the stack. No effect if stack is empty.\n"
     "Return the current stack size."},
    {"getContext",   (PyCFunction) SelectionSingleton::sGetContext, METH_VARARGS,
     "getContext(extended = False) -> (obj, subname)\n\n"
     "Obtain the current context sub-object.\n"
     "If extended is True, then try various options in the following order,\n"
     " * Explicitly set context by calling pushContext(),\n"
     " * Current pre-selected object,\n"
     " * If there is only one selection in the active document,\n"
     " * If the active document is in editing, then return the editing object."},
    {nullptr, nullptr, 0, nullptr}  /* Sentinel */
};

PyObject *SelectionSingleton::sAddSelection(PyObject * /*self*/, PyObject *args)
{
    SelectionLogDisabler disabler(true);
    PyObject *clearPreselect = Py_True;
    char *objname;
    char *docname;
    char* subname = nullptr;
    float x = 0, y = 0, z = 0;
    if (PyArg_ParseTuple(args, "ss|sfffO!", &docname, &objname ,
                &subname,&x,&y,&z,&PyBool_Type,&clearPreselect)) {
        Selection().addSelection(docname, objname, subname, x, y, z, nullptr, Base::asBoolean(clearPreselect));
        Py_Return;
    }

    PyErr_Clear();
    PyObject *object;
    subname = nullptr;
    x = 0, y = 0, z = 0;
    if (PyArg_ParseTuple(args, "O!|sfffO!", &(App::DocumentObjectPy::Type),&object,
                &subname,&x,&y,&z,&PyBool_Type,&clearPreselect)) {
        auto docObjPy = static_cast<App::DocumentObjectPy*>(object);
        App::DocumentObject* docObj = docObjPy->getDocumentObjectPtr();
        if (!docObj || !docObj->getNameInDocument()) {
            PyErr_SetString(Base::PyExc_FC_GeneralError, "Cannot check invalid object");
            return nullptr;
        }

        Selection().addSelection(docObj->getDocument()->getName(),
                                 docObj->getNameInDocument(),
                                 subname, x, y, z, nullptr, Base::asBoolean(clearPreselect));
        Py_Return;
    }

    PyErr_Clear();
    PyObject *sequence;
    if (PyArg_ParseTuple(args, "O!O|O!", &(App::DocumentObjectPy::Type),&object,
                &sequence,&PyBool_Type,&clearPreselect))
    {
        auto docObjPy = static_cast<App::DocumentObjectPy*>(object);
        App::DocumentObject* docObj = docObjPy->getDocumentObjectPtr();
        if (!docObj || !docObj->getNameInDocument()) {
            PyErr_SetString(Base::PyExc_FC_GeneralError, "Cannot check invalid object");
            return nullptr;
        }

        try {
            if (PyTuple_Check(sequence) || PyList_Check(sequence)) {
                Py::Sequence list(sequence);
                for (Py::Sequence::iterator it = list.begin(); it != list.end(); ++it) {
                    std::string subname = static_cast<std::string>(Py::String(*it));
                    Selection().addSelection(docObj->getDocument()->getName(),
                                             docObj->getNameInDocument(),
                                             subname.c_str(), 0, 0, 0, nullptr, Base::asBoolean(clearPreselect));
                }
                Py_Return;
            }
        }
        catch (const Py::Exception&) {
            // do nothing here
        }
    }

    PyErr_SetString(PyExc_ValueError, "type must be 'DocumentObject[,subname[,x,y,z]]' or 'DocumentObject, list or tuple of subnames'");

    return nullptr;
}

PyObject *SelectionSingleton::sAddSelections(PyObject * /*self*/, PyObject *args)
{
    SelectionLogDisabler disabler(true);
    PyObject *sequence;
    if (!PyArg_ParseTuple(args, "O", &sequence))
        return nullptr;

    const char *errmsg = "Expect the first argument to be of type Sequence[DocumentObject | Tuple(DocumentObject, String)]";
    if (!PySequence_Check(sequence)) {
        PyErr_SetString(PyExc_TypeError, errmsg);
        return nullptr;
    }

    std::vector<App::SubObjectT> objs;
    Py::Sequence seq(sequence);
    for (Py_ssize_t i=0;i<seq.size();++i) {
        std::string subname;
        PyObject *pyObj = seq[i].ptr();
        if (PySequence_Check(pyObj)) {
            Py::Sequence item(pyObj);
            if (item.size() != 2) {
                PyErr_SetString(PyExc_TypeError, errmsg);
                return nullptr;
            }
            pyObj = item[0].ptr();
            if (!PyUnicode_Check(item[1].ptr())) {
                PyErr_SetString(PyExc_TypeError, errmsg);
                return nullptr;
            }
            subname = PyUnicode_AsUTF8(item[1].ptr());
        }
        if (!PyObject_TypeCheck(pyObj, &App::DocumentObjectPy::Type)) {
            PyErr_SetString(PyExc_TypeError, errmsg);
            return nullptr;
        }
        objs.emplace_back(static_cast<App::DocumentObjectPy*>(pyObj)->getDocumentObjectPtr(), subname.c_str());
    }
    try {
        Selection().addSelections(objs);
        Py_Return;
    } PY_CATCH
}

PyObject *SelectionSingleton::sUpdateSelection(PyObject * /*self*/, PyObject *args)
{
    PyObject *show;
    PyObject *object;
    char* subname=nullptr;
    if(!PyArg_ParseTuple(args, "O!O!|s", &PyBool_Type, &show, &(App::DocumentObjectPy::Type),
            &object, &subname))
        return nullptr;

    auto docObjPy = static_cast<App::DocumentObjectPy*>(object);
    App::DocumentObject* docObj = docObjPy->getDocumentObjectPtr();
    if (!docObj || !docObj->getNameInDocument()) {
        PyErr_SetString(Base::PyExc_FC_GeneralError, "Cannot check invalid object");
        return nullptr;
    }

    Selection().updateSelection(Base::asBoolean(show),
            docObj->getDocument()->getName(), docObj->getNameInDocument(), subname);

    Py_Return;
}


PyObject *SelectionSingleton::sRemoveSelection(PyObject * /*self*/, PyObject *args)
{
    SelectionLogDisabler disabler(true);
    char *docname, *objname;
    char* subname = nullptr;
    if(PyArg_ParseTuple(args, "ss|s", &docname,&objname,&subname)) {
        Selection().rmvSelection(docname,objname,subname);
        Py_Return;
    }

    PyErr_Clear();
    PyObject *object;
    subname = nullptr;
    if (!PyArg_ParseTuple(args, "O!|s", &(App::DocumentObjectPy::Type),&object,&subname))
        return nullptr;

    auto docObjPy = static_cast<App::DocumentObjectPy*>(object);
    App::DocumentObject* docObj = docObjPy->getDocumentObjectPtr();
    if (!docObj || !docObj->getNameInDocument()) {
        PyErr_SetString(Base::PyExc_FC_GeneralError, "Cannot check invalid object");
        return nullptr;
    }

    Selection().rmvSelection(docObj->getDocument()->getName(),
                             docObj->getNameInDocument(),
                             subname);

    Py_Return;
}

PyObject *SelectionSingleton::sClearSelection(PyObject * /*self*/, PyObject *args)
{
    SelectionLogDisabler disabler(true);
    PyObject *clearPreSelect = Py_True;
    char *documentName = nullptr;
    if (!PyArg_ParseTuple(args, "|O!", &PyBool_Type, &clearPreSelect)) {
        PyErr_Clear();
        if (!PyArg_ParseTuple(args, "|sO!", &documentName, &PyBool_Type, &clearPreSelect))
            return nullptr;
    }
    Selection().clearSelection(documentName, Base::asBoolean(clearPreSelect));

    Py_Return;
}

namespace {
ResolveMode toEnum(int value) {
    switch (value) {
    case 0:
        return ResolveMode::NoResolve;
    case 1:
        return ResolveMode::OldStyleElement;
    case 2:
        return ResolveMode::NewStyleElement;
    case 3:
        return ResolveMode::FollowLink;
    default:
        throw Base::ValueError("Wrong enum value");
    }
}
}

PyObject *SelectionSingleton::sIsSelected(PyObject * /*self*/, PyObject *args)
{
    PyObject *object;
    char* subname = nullptr;
    int resolve = 1;
    if (!PyArg_ParseTuple(args, "O!|si", &(App::DocumentObjectPy::Type), &object, &subname, &resolve))
        return nullptr;

    try {
        auto docObj = static_cast<App::DocumentObjectPy*>(object);
        bool ok = Selection().isSelected(docObj->getDocumentObjectPtr(), subname, toEnum(resolve));

        return Py_BuildValue("O", (ok ? Py_True : Py_False));
    }
    catch (const Base::Exception& e) {
        e.setPyException();
        return nullptr;
    }
}

PyObject *SelectionSingleton::sCountObjectsOfType(PyObject * /*self*/, PyObject *args)
{
    char* objecttype = nullptr;
    char* document = nullptr;
    int resolve = 1;
    if (!PyArg_ParseTuple(args, "|ssi", &objecttype, &document,&resolve))
        return nullptr;

    try {
        unsigned int count = Selection().countObjectsOfType(objecttype, document, toEnum(resolve));
        return PyLong_FromLong(count);
    } PY_CATCH
}

PyObject *SelectionSingleton::sGetSelection(PyObject * /*self*/, PyObject *args)
{
    char *documentName = nullptr;
    int resolve = 1;
    PyObject *single = Py_False;
    if (!PyArg_ParseTuple(args, "|siO!", &documentName, &resolve, &PyBool_Type, &single))
        return nullptr;

    try {
        std::vector<SelectionSingleton::SelObj> sel;
        sel = Selection().getSelection(documentName, toEnum(resolve), Base::asBoolean(single));

        std::set<App::DocumentObject*> noduplicates;
        std::vector<App::DocumentObject*> selectedObjects; // keep the order of selection
        Py::List list;
        for (const auto & it : sel) {
            if (noduplicates.insert(it.pObject).second) {
                selectedObjects.push_back(it.pObject);
            }
        }
        for (const auto & selectedObject : selectedObjects) {
            list.append(Py::asObject(selectedObject->getPyObject()));
        }
        return Py::new_reference_to(list);
    }
    catch (const Base::Exception& e) {
        e.setPyException();
        return nullptr;
    }
    catch (Py::Exception&) {
        return nullptr;
    }
}

PyObject *SelectionSingleton::sEnablePickedList(PyObject * /*self*/, PyObject *args)
{
    PyObject *enable = Py_True;
    if (!PyArg_ParseTuple(args, "|O!", &PyBool_Type, &enable))
        return nullptr;

    Selection().enablePickedList(Base::asBoolean(enable));

    Py_Return;
}

PyObject *SelectionSingleton::sNeedPickedList(PyObject * /*self*/, PyObject *args)
{
    if (!PyArg_ParseTuple(args, ""))
        return nullptr;

    return Py::new_reference_to(Py::Boolean(Selection().needPickedList()));
}

PyObject *SelectionSingleton::sSetPreselection(PyObject * /*self*/, PyObject *args, PyObject *kwd)
{
    PyObject *object;
    char* subname = nullptr;
    float x = 0, y = 0, z = 0;
    int type = 1;
    static char *kwlist[] = {"obj","subname","x","y","z","tp",nullptr};
    if (PyArg_ParseTupleAndKeywords(args, kwd, "O!|sfffi", kwlist,
                &(App::DocumentObjectPy::Type),&object,&subname,&x,&y,&z,&type)) {
        auto docObjPy = static_cast<App::DocumentObjectPy*>(object);
        App::DocumentObject* docObj = docObjPy->getDocumentObjectPtr();
        if (!docObj || !docObj->getNameInDocument()) {
            PyErr_SetString(Base::PyExc_FC_GeneralError, "Cannot check invalid object");
            return nullptr;
        }

        Selection().setPreselect(docObj->getDocument()->getName(),
                                 docObj->getNameInDocument(),
                                 subname,x,y,z, static_cast<SelectionChanges::MsgSource>(type));
        Py_Return;
    }

    PyErr_SetString(PyExc_ValueError, "type must be 'DocumentObject[,subname[,x,y,z]]'");

    return nullptr;
}

PyObject *SelectionSingleton::sGetPreselection(PyObject * /*self*/, PyObject *args)
{
    if (!PyArg_ParseTuple(args, ""))
        return nullptr;

    const SelectionChanges& sel = Selection().getPreselection();
    SelectionObject obj(sel);

    return obj.getPyObject();
}

PyObject *SelectionSingleton::sRemPreselection(PyObject * /*self*/, PyObject *args)
{
    if (!PyArg_ParseTuple(args, ""))
        return nullptr;

    Selection().rmvPreselect();

    Py_Return;
}

PyObject *SelectionSingleton::sGetCompleteSelection(PyObject * /*self*/, PyObject *args)
{
    int resolve = 1;
    if (!PyArg_ParseTuple(args, "|i",&resolve))
        return nullptr;

    try {
        std::vector<SelectionSingleton::SelObj> sel;
        sel = Selection().getCompleteSelection(toEnum(resolve));

        Py::List list;
        for (const auto & it : sel) {
            SelectionObject obj(SelectionChanges(SelectionChanges::AddSelection,
                                                 it.DocName,
                                                 it.FeatName,
                                                 it.SubName,
                                                 it.TypeName,
                                                 it.x, it.y, it.z));
            list.append(Py::asObject(obj.getPyObject()));
        }
        return Py::new_reference_to(list);
    }
    catch (const Base::Exception& e) {
        e.setPyException();
        return nullptr;
    }
    catch (Py::Exception&) {
        return nullptr;
    }
}

PyObject *SelectionSingleton::sGetSelectionEx(PyObject * /*self*/, PyObject *args)
{
    char *documentName = nullptr;
    int resolve = 1;
    PyObject *single = Py_False;
    if (!PyArg_ParseTuple(args, "|siO!", &documentName, &resolve, &PyBool_Type, &single))
        return nullptr;

    try {
        std::vector<SelectionObject> sel;
        sel = Selection().getSelectionEx(documentName,
                App::DocumentObject::getClassTypeId(), toEnum(resolve), Base::asBoolean(single));

        Py::List list;
        for (auto & it : sel) {
            list.append(Py::asObject(it.getPyObject()));
        }
        return Py::new_reference_to(list);
    }
    catch (const Base::Exception& e) {
        e.setPyException();
        return nullptr;
    }
    catch (Py::Exception&) {
        return nullptr;
    }
}

PyObject *SelectionSingleton::sGetPickedList(PyObject * /*self*/, PyObject *args)
{
    char *documentName = nullptr;
    if (!PyArg_ParseTuple(args, "|s", &documentName))
        return nullptr;

    std::vector<SelectionObject> sel;
    sel = Selection().getPickedListEx(documentName);

    try {
        Py::List list;
        for (auto & it : sel) {
            list.append(Py::asObject(it.getPyObject()));
        }
        return Py::new_reference_to(list);
    }
    catch (Py::Exception&) {
        return nullptr;
    }
}

PyObject *SelectionSingleton::sGetSelectionObject(PyObject * /*self*/, PyObject *args)
{
    char *docName, *objName, *subName;
    PyObject* tuple = nullptr;
    if (!PyArg_ParseTuple(args, "sss|O!", &docName, &objName, &subName, &PyTuple_Type, &tuple))
        return nullptr;

    try {
        SelectionObject selObj;
        selObj.DocName  = docName;
        selObj.FeatName = objName;
        std::string sub = subName;
        if (!sub.empty()) {
            selObj.SubNames.push_back(sub);
            if (tuple) {
                Py::Tuple t(tuple);
                double x = (double)Py::Float(t.getItem(0));
                double y = (double)Py::Float(t.getItem(1));
                double z = (double)Py::Float(t.getItem(2));
                selObj.SelPoses.emplace_back(x,y,z);
            }
        }

        return selObj.getPyObject();
    }
    catch (const Py::Exception&) {
        return nullptr;
    }
    catch (const Base::Exception& e) {
        e.setPyException();
        return nullptr;
    }
}

PyObject *SelectionSingleton::sAddSelObserver(PyObject * /*self*/, PyObject *args)
{
    PyObject* o;
    int resolve = 1;
    if (!PyArg_ParseTuple(args, "O|i", &o, &resolve))
        return nullptr;

    PY_TRY {
        SelectionObserverPython::addObserver(Py::Object(o), toEnum(resolve));
        Py_Return;
    }
    PY_CATCH;
}

PyObject *SelectionSingleton::sRemSelObserver(PyObject * /*self*/, PyObject *args)
{
    PyObject* o;
    if (!PyArg_ParseTuple(args, "O", &o))
        return nullptr;

    PY_TRY {
        SelectionObserverPython::removeObserver(Py::Object(o));
        Py_Return;
    }
    PY_CATCH;
}

PyObject *SelectionSingleton::sAddSelectionGate(PyObject * /*self*/, PyObject *args)
{
    char* filter;
    int resolve = 1;
    if (PyArg_ParseTuple(args, "s|i", &filter, &resolve)) {
        PY_TRY {
            Selection().addSelectionGate(new SelectionFilterGate(filter), toEnum(resolve));
            Py_Return;
        }
        PY_CATCH;
    }

    PyErr_Clear();
    PyObject* filterPy;
    if (PyArg_ParseTuple(args, "O!|i",SelectionFilterPy::type_object(),&filterPy,resolve)) {
        PY_TRY {
            Selection().addSelectionGate(new SelectionFilterGatePython(
                        static_cast<SelectionFilterPy*>(filterPy)), toEnum(resolve));
            Py_Return;
        }
        PY_CATCH;
    }

    PyErr_Clear();
    PyObject* gate;
    if (PyArg_ParseTuple(args, "O|i",&gate,&resolve)) {
        PY_TRY {
            Selection().addSelectionGate(new SelectionGatePython(Py::Object(gate, false)), toEnum(resolve));
            Py_Return;
        }
         PY_CATCH;
    }

    PyErr_SetString(PyExc_ValueError, "Argument is neither string nor SelectionFiler nor SelectionGate");

    return nullptr;
}

PyObject *SelectionSingleton::sRemoveSelectionGate(PyObject * /*self*/, PyObject *args)
{
    if (!PyArg_ParseTuple(args, ""))
        return nullptr;

    PY_TRY {
        Selection().rmvSelectionGate();
        Py_Return;
    }
    PY_CATCH;
}

PyObject *SelectionSingleton::sSetVisible(PyObject * /*self*/, PyObject *args)
{
    PyObject *visible = Py_None;
    if (!PyArg_ParseTuple(args, "|O", &visible))
        return nullptr;

    PY_TRY {
        VisibleState vis = VisToggle;
        Base::PyTypeCheck(&visible, &PyBool_Type);
        if (visible)
            vis = PyObject_IsTrue(visible) ? VisShow : VisHide;

        Selection().setVisible(vis);
        Py_Return;
    }
    PY_CATCH;
}

PyObject *SelectionSingleton::sPushSelStack(PyObject * /*self*/, PyObject *args)
{
    PyObject *clear = Py_True;
    PyObject *overwrite = Py_False;
    if (!PyArg_ParseTuple(args, "|O!O!", &PyBool_Type, &clear, &PyBool_Type, &overwrite))
        return nullptr;

    Selection().selStackPush(Base::asBoolean(clear), Base::asBoolean(overwrite));

    Py_Return;
}

PyObject *SelectionSingleton::sHasSelection(PyObject * /*self*/, PyObject *args)
{
    const char *doc = nullptr;
    int resolve = 0;
    if (!PyArg_ParseTuple(args, "|sO!", &doc, &resolve))
        return nullptr;

    PY_TRY {
        bool ret;
        if (doc || resolve > 0)
            ret = Selection().hasSelection(doc, toEnum(resolve));
        else
            ret = Selection().hasSelection();

        return Py::new_reference_to(Py::Boolean(ret));
    }
    PY_CATCH;
}

PyObject *SelectionSingleton::sHasSubSelection(PyObject * /*self*/, PyObject *args)
{
    const char *doc = nullptr;
    PyObject *subElement = Py_False;
    if (!PyArg_ParseTuple(args, "|sO!",&doc,&PyBool_Type,&subElement))
        return nullptr;

    PY_TRY {
        return Py::new_reference_to(
               Py::Boolean(Selection().hasSubSelection(doc, Base::asBoolean(subElement))));
    }
    PY_CATCH;
}

PyObject *SelectionSingleton::sGetSelectionFromStack(PyObject * /*self*/, PyObject *args)
{
    char *documentName = nullptr;
    int resolve = 1;
    int index = 0;
    if (!PyArg_ParseTuple(args, "|sii", &documentName, &resolve, &index))
        return nullptr;

    PY_TRY {
        Py::List list;
        for(auto &sel : Selection().selStackGet(documentName, toEnum(resolve), index))
            list.append(Py::asObject(sel.getPyObject()));
        return Py::new_reference_to(list);
    }
    PY_CATCH;
}

PyObject* SelectionSingleton::sCheckTopParent(PyObject *, PyObject *args) {
    PyObject *pyObj;
    const char *subname = 0;
    if (!PyArg_ParseTuple(args, "O!|s", &App::DocumentObjectPy::Type,&pyObj,&subname))
        return 0;
    PY_TRY {
        std::string sub;
        if(subname)
            sub = subname;
        App::DocumentObject *obj = static_cast<App::DocumentObjectPy*>(pyObj)->getDocumentObjectPtr();
        checkTopParent(obj,sub);
        return Py::new_reference_to(Py::TupleN(Py::asObject(obj->getPyObject()),Py::String(sub)));
    } PY_CATCH;
}

PyObject *SelectionSingleton::sGetContext(PyObject *, PyObject *args)
{
    PyObject *extended = Py_False;
    int pos = 0;
    if (!PyArg_ParseTuple(args, "|Oi", &extended, &pos))
        return 0;
    App::SubObjectT sobjT;
    if (PyObject_IsTrue(extended))
        sobjT = Selection().getExtendedContext();
    else
        sobjT = Selection().getContext(pos);
    auto obj = sobjT.getObject();
    if (!obj)
        Py_Return;
    return Py::new_reference_to(Py::TupleN(Py::asObject(obj->getPyObject()),
                                           Py::String(sobjT.getSubName().c_str())));
}

PyObject *SelectionSingleton::sPushContext(PyObject *, PyObject *args)
{
    PyObject *pyObj;
    char *subname = "";
    if (!PyArg_ParseTuple(args, "O!|s", &App::DocumentObjectPy::Type,&pyObj,&subname))
        return 0;
    return Py::new_reference_to(Py::Int(Selection().pushContext(App::SubObjectT(
            static_cast<App::DocumentObjectPy*>(pyObj)->getDocumentObjectPtr(), subname))));
}

PyObject *SelectionSingleton::sPopContext(PyObject *, PyObject *args)
{
    if (!PyArg_ParseTuple(args, ""))
        return 0;
    return Py::new_reference_to(Py::Int(Selection().popContext()));
}

PyObject *SelectionSingleton::sSetContext(PyObject *, PyObject *args)
{
    PyObject *pyObj;
    char *subname = "";
    if (!PyArg_ParseTuple(args, "O!|s", &App::DocumentObjectPy::Type,&pyObj,&subname))
        return 0;
    return Py::new_reference_to(Py::Int(Selection().setContext(App::SubObjectT(
            static_cast<App::DocumentObjectPy*>(pyObj)->getDocumentObjectPtr(), subname))));
}

PyObject *SelectionSingleton::sGetPreselectionText(PyObject *, PyObject *args)
{
    if (!PyArg_ParseTuple(args, ""))
        return 0;
    return Py::new_reference_to(Py::String(Selection().getPreselectionText()));
}

PyObject *SelectionSingleton::sSetPreselectionText(PyObject *, PyObject *args)
{
    const char *subname;
    if (!PyArg_ParseTuple(args, "s", &subname))
        return 0;
    Selection().setPreselectionText(subname);
    Py_Return;
}
