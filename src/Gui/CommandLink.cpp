/****************************************************************************
 *   Copyright (c) 2017 Zheng Lei (realthunder) <realthunder.dev@gmail.com> *
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
# include <sstream>
# include <QMessageBox>
#endif

#include <iomanip>
#include <boost/algorithm/string/predicate.hpp>

#include <App/Application.h>
#include <App/AutoTransaction.h>
#include <App/ComplexGeoData.h>
#include <App/ElementNamingUtils.h>
#include <App/Document.h>
#include <App/DocumentObjectGroup.h>
#include <App/DocumentObject.h>
#include <App/DocumentObserver.h>
#include <App/Link.h>
#include <App/Part.h>
#include <Base/Console.h>
#include <Base/Exception.h>
#include <Base/Tools.h>

#include "Action.h"
#include "ActiveObjectList.h"
#include "Application.h"
#include "CommandT.h"
#include "Document.h"
#include "MainWindow.h"
#include "Selection.h"
#include "Tree.h"
#include "ViewProviderDocumentObject.h"
#include "WaitCursor.h"

FC_LOG_LEVEL_INIT("CommandLink", true, true)

using namespace Gui;

static void setLinkPlacement(std::ostringstream &ss,
                             App::DocumentObject *link,
                             const App::SubObjectT *sobj,
                             const App::SubObjectT *parentT = nullptr)
{
    if (!link || !App::LinkParams::getCreateInPlace())
        return;
    ss.str("");
    ss << "_t,_r,_s,_ = (";
    if (parentT)  {
        ss << parentT->getObjectPython() << ".getSubObject(u'"
            << parentT->getSubNameNoElement() << "', retType=4).inverse()";
        if (sobj)
            ss << " * ";
    }
    if (sobj) 
        ss << sobj->getObjectPython() << ".getSubObject(u'"
            << sobj->getSubNameNoElement() << "', retType=4)";
    ss << ").getTransform()\n"
       << "if not _t.isEqual(App.Vector(),1e-7) or not _r.isSame(App.Rotation(),1e-12):\n"
       << "    " << link->getFullName(true) << ".Placement = App.Placement(_t, _r)\n"
       << "if not _s.isEqual(App.Vector(1,1,1),1e-7):\n"
       << "    " << link->getFullName(true) << ".ScaleVector = _s\n"
       << "del _t, _r, _s\n";

    Command::runCommand(Command::Doc, ss.str().c_str());
}

static App::DocumentObject *getActiveContainer(
        App::DocumentObject **topParent=nullptr, std::string *subname=nullptr)
{
    if (!App::LinkParams::getCreateInContainer())
        return nullptr;
    Gui::MDIView *activeView = Gui::Application::Instance->activeView();
    if ( activeView ) {
        const auto &key = App::LinkParams::getActiveContainerKey();
        return activeView->getActiveObject<App::DocumentObject*> (
                key.size() ? key.c_str() : PARTKEY, topParent, subname);
    } else {
        return 0;
    }
}

////////////////////////////////////////////////////////////////////////////////////////////

class StdCmdLinkMakeGroup : public Gui::Command
{
public:
    StdCmdLinkMakeGroup();
    const char* className() const override
    { return "StdCmdLinkMakeGroup"; }

protected:
    void activated(int iMsg) override;
    bool isActive() override;
    Action * createAction() override;
    void languageChange() override;
};

StdCmdLinkMakeGroup::StdCmdLinkMakeGroup()
  : Command("Std_LinkMakeGroup")
{
    sGroup        = "Link";
    sMenuText     = QT_TR_NOOP("Make link group");
    sToolTipText  = QT_TR_NOOP("Create a group of links");
    sWhatsThis    = "Std_LinkMakeGroup";
    sStatusTip    = sToolTipText;
    eType         = AlterDoc;
    sPixmap       = "LinkGroup";
}

bool StdCmdLinkMakeGroup::isActive() {
    return !!App::GetApplication().getActiveDocument();
}

Action * StdCmdLinkMakeGroup::createAction()
{
    auto pcAction = new ActionGroup(this, getMainWindow());
    pcAction->setDropDownMenu(true);
    applyCommandData(this->className(), pcAction);

    // add the action items
    QAction* action = nullptr;
    action = pcAction->addAction(QObject::tr("Simple group"));
    action->setWhatsThis(QString::fromUtf8(getWhatsThis()));
    action = pcAction->addAction(QObject::tr("Group with links"));
    action->setWhatsThis(QString::fromUtf8(getWhatsThis()));
    action = pcAction->addAction(QObject::tr("Group with transform links"));
    action->setWhatsThis(QString::fromUtf8(getWhatsThis()));
    return pcAction;
}

void StdCmdLinkMakeGroup::languageChange()
{
    Command::languageChange();

    if (!_pcAction)
        return;
    auto pcAction = qobject_cast<ActionGroup*>(_pcAction);
    QList<QAction*> acts = pcAction->actions();
    acts[0]->setText(QObject::tr("Simple group"));
    acts[1]->setText(QObject::tr("Group with links"));
    acts[2]->setText(QObject::tr("Group with transform links"));
}


void StdCmdLinkMakeGroup::activated(int option) {

    auto doc = App::GetApplication().getActiveDocument();
    if(!doc) {
        FC_ERR("no active document");
        return;
    }

    std::set<App::DocumentObject*> inList;
    App::DocumentObject *topParent=nullptr;
    std::string parentSub;
    auto container = getActiveContainer(&topParent, &parentSub);
    if (container) {
        inList = container->getInListEx(true);
        inList.insert(container);
    }

    auto objs = Gui::Selection().getSelectionT("*", ResolveMode::NoResolve);

    std::set<App::DocumentObject*> objset;
    for (auto it=objs.begin(); it!=objs.end(); ) {
        auto sobj = it->getSubObject();
        if (!sobj)
            continue;
        if (option == 0) {
            if (!objset.insert(sobj).second) {
                it = objs.erase(it);
                continue;
            } else
                ++it;
        } else
            ++it;
        if (inList.count(sobj)) {
            inList.clear();
            container = nullptr;
        }
    }

    if (container)
        doc = container->getDocument();

    Selection().selStackPush();
    Selection().clearCompleteSelection();

    App::AutoTransaction committer(QT_TRANSLATE_NOOP("Command", "Make link group"));
    try {
        std::ostringstream ss;
        ss << std::setprecision(std::numeric_limits<double>::digits10 + 1);
        std::string groupName = doc->getUniqueObjectName("LinkGroup");
        cmdAppDocument(doc, ss << "addObject('App::LinkGroup','" << groupName << "')");
        auto group = doc->getObject(groupName.c_str());
        if (group && container)
            cmdAppObjectArgs(container, "addObject(%s)", group->getFullName(true));
            
        if(objs.empty()) {
            Selection().addSelection(doc->getName(),groupName.c_str());
            Selection().selStackPush();
        }else{
            Command::doCommand(Command::Doc,"__objs__ = []");
            std::vector<std::string> newNames;
            for(auto &objT : objs) {
                auto obj = objT.getSubObject();
                std::string name;
                if(option!=0 || doc!=obj->getDocument()) {
                    name = doc->getUniqueObjectName("Link");
                    ss.str("");
                    cmdAppDocument(doc, ss << "addObject('App::Link','" << name
                            << "').setLink(" << obj->getFullName(true) << ")");
                    auto link = doc->getObject(name.c_str());
                    if(option==2)
                        cmdAppObject(link, "LinkTransform = True");
                    if (container) {
                        App::SubObjectT parent(topParent, parentSub.c_str());
                        setLinkPlacement(ss, link, option==2 ? nullptr : &objT, &parent);
                    } else if (option != 2)
                        setLinkPlacement(ss, link, &objT);
                } else
                    name = obj->getNameInDocument();

                Command::doCommand(Command::Doc,"__objs__.append(App.getDocument('%s').getObject('%s'))",
                        doc->getName(),name.c_str());
                name += ".";
                newNames.push_back(std::move(name));
            }

            cmdAppObject(group, "setLink(__objs__)");
            Command::doCommand(Command::Doc,"del __objs__");

            App::SubObjectT sel;
            if (container)
                sel = topParent;
            else
                sel = group;
            for (auto &name : newNames) {
                if (container)
                    sel.setSubName(parentSub + groupName + "." + name);
                else
                    sel.setSubName(name);
                Selection().addSelection(sel);
            }
            Selection().selStackPush();
        }
        if(option!=0) {
            Command::doCommand(Command::Doc,
                    "App.getDocument('%s').getObject('%s').LinkMode = 'Auto Delete'",
                    doc->getName(),groupName.c_str());
        }
        updateActive();
    } catch (const Base::Exception& e) {
        committer.close(true);
        QMessageBox::critical(getMainWindow(), QObject::tr("Create link group failed"),
            QString::fromUtf8(e.what()));
        Command::abortCommand();
        e.ReportException();
    }
}

////////////////////////////////////////////////////////////////////////////////////////////

DEF_STD_CMD_A(StdCmdLinkMake)

StdCmdLinkMake::StdCmdLinkMake()
  : Command("Std_LinkMake")
{
    sGroup        = "Link";
    sMenuText     = QT_TR_NOOP("Make link");
    static std::string toolTip = std::string("<p>")
        + QT_TR_NOOP("A Link is an object that references or links to another object in the same "
        "document, or in another document.Unlike Clones, Links reference the original Shape directly, "
        " making them more memory efficient which helps with the creation of complex assemblies.")
        + "</p>";
    sToolTipText = toolTip.c_str();
    sWhatsThis    = "Std_LinkMake";
    sStatusTip    = sToolTipText;
    eType         = AlterDoc;
    sPixmap       = "Link";
}

bool StdCmdLinkMake::isActive() {
    return App::GetApplication().getActiveDocument();
}

void StdCmdLinkMake::activated(int) {

    std::set<App::DocumentObject*> inList;
    App::DocumentObject *topParent=nullptr;
    std::string parentSub;
    auto container = getActiveContainer(&topParent, &parentSub);
    if (container) {
        inList = container->getInListEx(true);
        inList.insert(container);
    }

    auto sels = Selection().getSelectionT("*",  ResolveMode::NoResolve);

    Selection().selStackPush();
    Selection().clearCompleteSelection();

    App::AutoTransaction committer(QT_TRANSLATE_NOOP("Command", "Make link"));
    try {
        if (sels.empty()) {
            auto doc = App::GetApplication().getActiveDocument();
            if(!doc) {
                FC_ERR("no active document");
                return;
            }
            std::string name = doc->getUniqueObjectName("Link");
            cmdAppDocument(doc, std::ostringstream() << "addObject('App::Link','" << name << "')");
            auto link = doc->getObject(name.c_str());
            if (container && link) {
                cmdAppObjectArgs(container, "addObject(%s)", link->getFullName(true));
                Selection().addSelection(App::SubObjectT(
                            topParent, (parentSub + name + ".").c_str()));
            } else
                Selection().addSelection(doc->getName(),name.c_str());
            Selection().selStackPush();
            return;
        }

        std::ostringstream ss;
        ss << std::setprecision(std::numeric_limits<double>::digits10 + 1);
        for(auto &sel : sels) {
            auto sobj = sel.getSubObject();
            if (!sobj) {
                FC_ERR("Failed to get sub-object " << sel.getSubObjectFullName());
                continue;
            }

            auto doc = App::GetApplication().getActiveDocument();
            if(!doc) {
                FC_ERR("no active document");
                return;
            }

            bool addToContainer = false;
            if (container && !inList.count(sobj)) {
                addToContainer = true;
                doc = container->getDocument();
            }

            std::string name = doc->getUniqueObjectName("Link");
            ss.str("");
            cmdAppDocument(doc, ss << "addObject('App::Link', '" << name << "').setLink("
                                   << sobj->getFullName(true) << ")");
            auto link = doc->getObject(name.c_str());

            if (link && addToContainer) {
                App::SubObjectT objT(topParent, parentSub.c_str());
                cmdAppObjectArgs(container, "addObject(%s)", link->getFullName(true));
                setLinkPlacement(ss, link, &sel, &objT);
                objT.setSubName(parentSub + name + ".");
                Selection().addSelection(objT);
            } else {
                setLinkPlacement(ss, link, &sel);
                Selection().addSelection(doc->getName(),name.c_str());
            }
        }
        Selection().selStackPush();
        updateActive();
    } catch (const Base::Exception& e) {
        committer.close(true);
        QMessageBox::critical(getMainWindow(), QObject::tr("Create link failed"),
            QString::fromUtf8(e.what()));
        e.ReportException();
    }
}

////////////////////////////////////////////////////////////////////////////////////////////

#define LINK_CMD_DEF(_name) \
class StdCmdLink##_name : public CheckableCommand \
{\
public:\
    StdCmdLink##_name();\
    virtual const char* className() const\
    { return "StdCmdLink" #_name; }\
protected: \
    virtual void setOption(bool checked) {\
        App::LinkParams::set##_name(checked);\
    }\
    virtual bool getOption(void) const {\
        return App::LinkParams::get##_name();\
    }\
};\
StdCmdLink##_name::StdCmdLink##_name():CheckableCommand("Std_Link" #_name)

LINK_CMD_DEF(CreateInPlace)
{
    sGroup        = "Link";
    sMenuText     = QT_TR_NOOP("Make link in place");
    sToolTipText  = QT_TR_NOOP("Enable this option to create a link with the same placement of the linked object");
    sWhatsThis    = "Std_LinkCreateInPlace";
    sStatusTip    = sToolTipText;
    eType         = NoDefaultAction | NoTransaction;
}


LINK_CMD_DEF(CreateInContainer)
{
    sGroup        = "Link";
    sMenuText     = QT_TR_NOOP("Make link in container");
    sToolTipText  = QT_TR_NOOP("Enable this option to create a link inside the active container");
    sWhatsThis    = "Std_LinkCreateInContainer";
    sStatusTip    = sToolTipText;
    eType         = NoDefaultAction | NoTransaction;
}

////////////////////////////////////////////////////////////////////////////////////////////

DEF_STD_CMD_A(StdCmdLinkMakeRelative)

StdCmdLinkMakeRelative::StdCmdLinkMakeRelative()
  : Command("Std_LinkMakeRelative")
{
    sGroup        = "Link";
    sMenuText     = QT_TR_NOOP("Make sub-link");
    sToolTipText  = QT_TR_NOOP("Create a sub-object or sub-element link");
    sWhatsThis    = "Std_LinkMakeRelative";
    sStatusTip    = sToolTipText;
    eType         = AlterDoc;
    sPixmap       = "LinkSub";
}

bool StdCmdLinkMakeRelative::isActive() {
    return Selection().hasSelection();
}

struct PrintElements {
    PrintElements(const std::vector<std::string> &elements)
        :elements(elements)
    {}

    friend std::ostream &operator << (std::ostream &os, const PrintElements &self) {
        os << "[";
        for (auto &e : self.elements)
            os << "'" << e << "',";
        os << "]";
        return os;
    }

    const std::vector<std::string> &elements;
};

void StdCmdLinkMakeRelative::activated(int) {
    std::set<App::DocumentObject*> inList;
    App::DocumentObject *topParent=nullptr;
    std::string parentSub;
    auto container = getActiveContainer(&topParent, &parentSub);
    if (container) {
        inList = container->getInListEx(true);
        inList.insert(container);
    }

    App::AutoTransaction committer(QT_TRANSLATE_NOOP("Command", "Make sub-link"));
    try {
        std::map<App::SubObjectT, std::vector<std::string> > linkInfo;
        for(auto &sel : Selection().getSelectionT("*", ResolveMode::NoResolve)) {
            auto element = sel.getElementName();
            if (!element || !element[0]) {
                linkInfo[sel];
                continue;
            }
            auto objT = sel;
            objT.setSubName(sel.getSubNameNoElement());
            linkInfo[objT].push_back(element);
        }

        Selection().selStackPush();
        Selection().clearCompleteSelection();

        std::ostringstream ss;
        ss << std::setprecision(std::numeric_limits<double>::digits10 + 1);

        for(auto &v : linkInfo) {
            auto &sel = v.first;
            auto &elements = v.second;

            auto doc = App::GetApplication().getActiveDocument();
            if(!doc) {
                FC_ERR("no active document");
                return;
            }

            bool addToContainer = false;
            auto linkSub = sel.getSubName();
            auto sobj = sel.getSubObject();
            if (!sobj) {
                FC_ERR("Failed to get sub-object " << sel.getSubObjectFullName());
                continue;
            }
            auto linkTarget = sel.getObject();
            if (container && !inList.count(sobj)) {
                auto sub = parentSub;
                addToContainer = topParent->resolveRelativeLink(sub, linkTarget, linkSub);
                if (addToContainer)
                    doc = container->getDocument();
            }

            std::string name = doc->getUniqueObjectName("Link");

            ss.str("");
            cmdAppDocument(doc, ss << "addObject('App::Link','" << name << "').setLink("
                    << linkTarget->getFullName(true) << ", u'" << linkSub << "', "
                    << PrintElements(elements) << ")");
            auto link = doc->getObject(name.c_str());
            cmdAppObject(link, "LinkTransform = True");
            if (addToContainer && link) {
                cmdAppObjectArgs(container, "addObject(%s)", link->getFullName(true));
                App::SubObjectT objT(topParent, parentSub.c_str());
                setLinkPlacement(ss, link, nullptr, &objT); 
                objT.setSubName(parentSub + name + ".");
                Selection().addSelection(objT);
            } else
                Selection().addSelection(doc->getName(),name.c_str());
        }
        Selection().selStackPush();
        updateActive();
    } catch (const Base::Exception& e) {
        committer.close(true);
        QMessageBox::critical(getMainWindow(), QObject::tr("Failed to create relative link"),
            QString::fromUtf8(e.what()));
        e.ReportException();
    }
    return;
}

/////////////////////////////////////////////////////////////////////////////////////

struct Info {
    bool inited = false;
    App::DocumentObjectT topParent;
    std::string subname;
    App::DocumentObjectT parent;
    App::DocumentObjectT obj;
};

static void linkConvert(bool unlink) {
    // We are trying to replace an object with a link (App::Link), or replace a
    // link back to its linked object (i.e. unlink). This is a very complex
    // operation. It works by reassign the link property of the parent of the
    // selected object(s) to a newly created link to the original object.
    // Everything should remain the same. This complexity is now largely handled
    // by ViewProviderDocumentObject::replaceObject(), which in turn relies on
    // PropertyLinkBase::CopyOnLinkReplace().

    std::map<std::pair<App::DocumentObject*,App::DocumentObject*>, Info> infos;
    for(const auto& sel : TreeWidget::getSelection()) {
        auto obj = sel.vp->getObject();
        auto parent = sel.parentVp;
        if(!parent) {
            FC_WARN("skip '" << obj->getFullName() << "' with no parent");
            continue;
        }
        auto parentObj = parent->getObject();
        auto &info = infos[std::make_pair(parentObj,obj)];
        if(info.inited)
            continue;
        info.inited = true;
        if(unlink) {
            auto linked = obj->getLinkedObject(false);
            if(!linked || !linked->isAttachedToDocument() || linked == obj) {
                FC_WARN("skip non link");
                continue;
            }
        }
        info.topParent = sel.topParent;
        info.parent = parentObj;
        info.obj = obj;
    }

    if(infos.empty())
        return;

    Selection().selStackPush();
    Selection().clearCompleteSelection();

    // now, do actual operation
    const char *transactionName = unlink?"Unlink":"Replace with link";
    App::AutoTransaction committer(QT_TRANSLATE_NOOP("Command", transactionName));
    try {
        std::unordered_map<App::DocumentObject*,App::DocumentObjectT> recomputeSet;
        for(auto &v : infos) {
            auto &info = v.second;
            auto parent = info.parent.getObject();
            auto parentVp = Base::freecad_dynamic_cast<ViewProviderDocumentObject>(
                    Application::Instance->getViewProvider(parent));
            auto obj = info.obj.getObject();
            if(!parent || !obj || !parentVp)
                continue;
            if(!recomputeSet.count(parent))
                recomputeSet.emplace(parent,parent);
            auto doc = parent->getDocument();
            App::DocumentObject *replaceObj;
            if(unlink) {
                replaceObj = obj->getLinkedObject(false);
                if(!replaceObj || !replaceObj->isAttachedToDocument() || replaceObj == obj)
                    continue;
            }else{
                auto name = doc->getUniqueObjectName("Link");
                auto link = static_cast<App::Link*>(doc->addObject("App::Link",name.c_str()));
                if(!link)
                    FC_THROWM(Base::RuntimeError,"Failed to create link");
                link->setLink(-1,obj);
                link->Label.setValue(obj->Label.getValue());
                auto pla = Base::freecad_dynamic_cast<App::PropertyPlacement>(
                        obj->getPropertyByName("Placement"));
                if(pla)
                    link->Placement.setValue(pla->getValue());
                else
                    link->LinkTransform.setValue(true);
                replaceObj = link;
            }

            // adjust subname for the new object
            auto pos = info.subname.rfind('.');
            if(pos==std::string::npos && pos)
                info.subname.clear();
            else {
                pos = info.subname.rfind('.',pos-1);
                if(pos==std::string::npos)
                    info.subname.clear();
                else {
                    info.subname.resize(pos+1);
                    info.subname += replaceObj->getNameInDocument();
                    info.subname += ".";
                }
            }

            // do the replacement operation
            if(parentVp->replaceObject(obj,replaceObj)<=0)
                FC_THROWM(Base::RuntimeError,
                        "Failed to change link for " << parent->getFullName());
        }

        Command::updateActive();

    } catch (const Base::Exception& e) {
        committer.close(true);
        auto title = unlink?QObject::tr("Unlink failed"):QObject::tr("Replace link failed");
        QMessageBox::critical(getMainWindow(), title, QString::fromUtf8(e.what()));
        e.ReportException();
        return;
    }
}

static bool linkConvertible(bool unlink) {
    // Don't waste too much time of large amount of selections
    if (Selection().size() > 50)
        return true;

    int count = 0;
    for(auto &sel : TreeWidget::getSelection()) {
        auto parent = sel.parentVp;
        if(!parent)
            return false;
        auto obj = sel.vp->getObject();
        if(unlink) {
            auto linked = obj->getLinkedObject(false);
            if(!linked || linked == obj || linked->getDocument() != obj->getDocument())
                return false;
        }
        ++count;
    }
    return count!=0;
}

////////////////////////////////////////////////////////////////////////////////////////////

DEF_STD_CMD_A(StdCmdLinkReplace)

StdCmdLinkReplace::StdCmdLinkReplace()
  : Command("Std_LinkReplace")
{
    sGroup        = "Link";
    sMenuText     = QT_TR_NOOP("Replace with link");
    sToolTipText  = QT_TR_NOOP("Replace the selected object(s) with link");
    sWhatsThis    = "Std_LinkReplace";
    sStatusTip    = sToolTipText;
    eType         = AlterDoc;
    sPixmap       = "LinkReplace";
}

bool StdCmdLinkReplace::isActive() {
    return linkConvertible(false);
}

void StdCmdLinkReplace::activated(int) {
    linkConvert(false);
}

////////////////////////////////////////////////////////////////////////////////////////////

DEF_STD_CMD_A(StdCmdLinkUnlink)

StdCmdLinkUnlink::StdCmdLinkUnlink()
  : Command("Std_LinkUnlink")
{
    sGroup        = "Link";
    sMenuText     = QT_TR_NOOP("Unlink");
    sToolTipText  = QT_TR_NOOP("Strip on level of link");
    sWhatsThis    = "Std_LinkUnlink";
    sStatusTip    = sToolTipText;
    eType         = AlterDoc;
    sPixmap       = "Unlink";
}

bool StdCmdLinkUnlink::isActive() {
    return linkConvertible(true);
}

void StdCmdLinkUnlink::activated(int) {
    linkConvert(true);
}

////////////////////////////////////////////////////////////////////////////////////////////

DEF_STD_CMD_A(StdCmdLinkImport)

StdCmdLinkImport::StdCmdLinkImport()
  : Command("Std_LinkImport")
{
    sGroup        = "Link";
    sMenuText     = QT_TR_NOOP("Import links");
    sToolTipText  = QT_TR_NOOP("Import selected external link(s)");
    sWhatsThis    = "Std_LinkImport";
    sStatusTip    = sToolTipText;
    eType         = AlterDoc;
    sPixmap       = "LinkImport";
}

static std::map<App::Document*, std::vector<App::DocumentObject*> > getLinkImportSelections()
{
    std::map<App::Document*, std::vector<App::DocumentObject*> > objMap;
    for(auto &sel : Selection().getCompleteSelection(ResolveMode::NoResolve)) {
        auto obj = sel.pObject->resolve(sel.SubName);
        if(!obj || !obj->isAttachedToDocument())
            continue;
        for(auto o : obj->getOutList()) {
            if(o && o->isAttachedToDocument() && o->getDocument()!=obj->getDocument()) {
                objMap[obj->getDocument()].push_back(obj);
                break;
            }
        }
    }
    return objMap;
}

bool StdCmdLinkImport::isActive() {
    auto links = getLinkImportSelections();
    if(links.empty())
        return false;
    for(auto &v : links) {
        if(v.first->testStatus(App::Document::PartialDoc))
            return false;
    }
    return true;
}

static void newImportGroup(std::vector<App::DocumentObject*> &&objs)
{
    for (auto it = objs.begin(); it != objs.end();) {
        if (App::GroupExtension::getAnyGroupOfObject(*it))
            it = objs.erase(it);
        else
            ++it;
    }

    if (objs.empty())
        return;

    auto doc = App::GetApplication().getActiveDocument();
    auto grp = static_cast<App::DocumentObjectGroup*>(doc->addObject("App::DocumentObjectGroup", "Imports"));
    grp->addObjects(objs);
    grp->Visibility.setValue(false);
}

void StdCmdLinkImport::activated(int) {
    App::AutoTransaction committer(QT_TRANSLATE_NOOP("Command", "Import links"));
    try {
        WaitCursor wc;
        wc.setIgnoreEvents(WaitCursor::NoEvents);
        std::vector<App::DocumentObject *> imports;
        for(auto &v : getLinkImportSelections()) {
            auto doc = v.first;
            auto objs = doc->importLinks(v.second);
            imports.insert(imports.end(), objs.begin(), objs.end());
        }
        newImportGroup(std::move(imports));

        updateActive();

    }catch (const Base::Exception& e) {
        committer.close(true);
        QMessageBox::critical(getMainWindow(), QObject::tr("Failed to import links"),
            QString::fromUtf8(e.what()));
        e.ReportException();
    }
}

////////////////////////////////////////////////////////////////////////////////////////////

DEF_STD_CMD_A(StdCmdLinkImportAll)

StdCmdLinkImportAll::StdCmdLinkImportAll()
  : Command("Std_LinkImportAll")
{
    sGroup        = "Link";
    sMenuText     = QT_TR_NOOP("Import all links");
    sToolTipText  = QT_TR_NOOP("Import all links of the active document");
    sWhatsThis    = "Std_LinkImportAll";
    sStatusTip    = sToolTipText;
    eType         = AlterDoc;
    sPixmap       = "LinkImportAll";
}

bool StdCmdLinkImportAll::isActive() {
    auto doc = App::GetApplication().getActiveDocument();
    return doc && !doc->testStatus(App::Document::PartialDoc) && App::PropertyXLink::hasXLink(doc);
}

void StdCmdLinkImportAll::activated(int) {
    App::AutoTransaction committer(QT_TRANSLATE_NOOP("Command", "Import all links"));
    try {
        WaitCursor wc;
        wc.setIgnoreEvents(WaitCursor::NoEvents);
        auto doc = App::GetApplication().getActiveDocument();
        if(doc)
            newImportGroup(doc->importLinks());
        updateActive();
    } catch (const Base::Exception& e) {
        committer.close(true);
        QMessageBox::critical(getMainWindow(), QObject::tr("Failed to import all links"),
            QString::fromUtf8(e.what()));
        Command::abortCommand();
        e.ReportException();
    }
}


////////////////////////////////////////////////////////////////////////////////////////////

DEF_STD_CMD_A(StdCmdLinkSelectLinked)

StdCmdLinkSelectLinked::StdCmdLinkSelectLinked()
  : Command("Std_LinkSelectLinked")
{
    sGroup        = "Link";
    sMenuText     = QT_TR_NOOP("Go to linked object");
    sToolTipText  = QT_TR_NOOP("Select the linked object and switch to its owner document");
    sWhatsThis    = "Std_LinkSelectLinked";
    sStatusTip    = sToolTipText;
    eType         = AlterSelection;
    sPixmap       = "LinkSelect";
    sAccel        = "S, G";
}

static App::DocumentObject *getSelectedLink(bool finalLink, std::string *subname=nullptr) {
    const auto &sels = Selection().getSelection("*", ResolveMode::NoResolve, true);
    if(sels.empty())
        return nullptr;
    auto sobj = sels[0].pObject->getSubObject(sels[0].SubName);
    if(!sobj)
        return nullptr;
    auto vp = Base::freecad_dynamic_cast<ViewProviderDocumentObject>(
            Application::Instance->getViewProvider(sobj));
    if(!vp)
        return nullptr;

    const char *element = Data::findElementName(sels[0].SubName);

    auto linkedVp = vp->getLinkedViewProvider(subname,finalLink);
    if(!linkedVp || linkedVp==vp) {
        if(sobj->getDocument()==sels[0].pObject->getDocument())
            return nullptr;
        for(const char *dot=strchr(sels[0].SubName,'.');dot;dot=strchr(dot+1,'.')) {
            std::string sub(sels[0].SubName,dot+1-sels[0].SubName);
            auto obj = sels[0].pObject->getSubObject(sub.c_str());
            if(!obj)
                break;
            obj = obj->getLinkedObject(true);
            if(obj->getDocument()!=sels[0].pObject->getDocument()) {
                if(finalLink) {
                    if(subname)
                        *subname = element;
                    return sobj==obj?nullptr:sobj;
                }
                if(subname)
                    *subname = std::string(dot+1);
                return obj;
            }
        }
        return finalLink?nullptr:sobj;
    }

    if(finalLink && linkedVp == vp->getLinkedViewProvider())
        return nullptr;

    auto linked = linkedVp->getObject();
    if(!linked || !linked->isAttachedToDocument())
        return nullptr;

    if(subname && sels[0].pObject!=sobj && sels[0].SubName) {
        bool found = false;
        int pre_len=0;
        std::size_t post_len=0;
        std::string prefix;
        std::string prefix2;
        // An object can be claimed by multiple objects. Let's try select one
        // that causes minimum jump in tree view, and prefer upper over lower
        // hierarchy (because of less depth/complexity of tree expansion)
        for(auto &v : linked->getParents()) {
            if(v.first != sels[0].pObject)
                continue;

            const char *sub = v.second.c_str();
            const char *dot = sub;
            for(const char *s=sels[0].SubName; *s && *sub==*s; ++s,++sub) {
                if(*sub == '.')
                    dot = sub;
            }
            found = true;
            if(dot-v.second.c_str() > pre_len
                    || (dot-v.second.c_str()==pre_len
                        && v.second.size()<post_len))
            {
                pre_len = dot-v.second.c_str();
                prefix = std::string(sels[0].SubName,pre_len) + (v.second.c_str()+pre_len);
                post_len = v.second.size();
            }else if(!pre_len) {
                if(prefix2.empty() || prefix2.size() > v.second.size())
                    prefix2 = v.second;
            }
        }

        if(found) {
            linked = sels[0].pObject;
            *subname = !prefix.empty()?prefix:prefix2 + *subname;
        }
    }

    return linked;
}

bool StdCmdLinkSelectLinked::isActive() {
    return getSelectedLink(false) != nullptr;
}

void StdCmdLinkSelectLinked::activated(int)
{
    std::string subname;
    auto linked = getSelectedLink(false,&subname);
    if(!linked){
        FC_WARN("invalid selection");
        return;
    }
    Selection().selStackPush();
    Selection().clearCompleteSelection();
    if(!subname.empty()) {
        Selection().addSelection(linked->getDocument()->getName(),linked->getNameInDocument(),subname.c_str());
        TreeWidget::scrollItemToTop();
    } else {
        TreeWidget::selectLinkedObject(linked);
    }
    Selection().selStackPush();
}

////////////////////////////////////////////////////////////////////////////////////////////

DEF_STD_CMD_A(StdCmdLinkSelectLinkedFinal)

StdCmdLinkSelectLinkedFinal::StdCmdLinkSelectLinkedFinal()
  : Command("Std_LinkSelectLinkedFinal")
{
    sGroup        = "Link";
    sMenuText     = QT_TR_NOOP("Go to the deepest linked object");
    sToolTipText  = QT_TR_NOOP("Select the deepest linked object and switch to its owner document");
    sWhatsThis    = "Std_LinkSelectLinkedFinal";
    sStatusTip    = sToolTipText;
    eType         = AlterSelection;
    sPixmap       = "LinkSelectFinal";
    sAccel        = "S, D";
}

bool StdCmdLinkSelectLinkedFinal::isActive() {
    return getSelectedLink(true) != nullptr;
}

void StdCmdLinkSelectLinkedFinal::activated(int) {
    std::string subname;
    auto linked = getSelectedLink(true, &subname);
    if(!linked){
        FC_WARN("invalid selection");
        return;
    }
    Selection().selStackPush();
    Selection().clearCompleteSelection();
    if(subname.size()) {
        Selection().addSelection(linked->getDocument()->getName(),linked->getNameInDocument(),subname.c_str());
        TreeWidget::scrollItemToTop();
    } else {
        TreeWidget::selectLinkedObject(linked);
    }
    Selection().selStackPush();
}

////////////////////////////////////////////////////////////////////////////////////////////

DEF_STD_CMD_A(StdCmdLinkSelectAllLinks)

StdCmdLinkSelectAllLinks::StdCmdLinkSelectAllLinks()
  : Command("Std_LinkSelectAllLinks")
{
    sGroup        = "Link";
    sMenuText     = QT_TR_NOOP("Select all links");
    sToolTipText  = QT_TR_NOOP("Select all links to the current selected object");
    sWhatsThis    = "Std_LinkSelectAllLinks";
    sStatusTip    = sToolTipText;
    eType         = AlterSelection;
    sPixmap       = "LinkSelectAll";
}

bool StdCmdLinkSelectAllLinks::isActive() {
    const auto &sels = Selection().getSelection("*", ResolveMode::OldStyleElement, true);
    if(sels.empty())
        return false;
    return App::GetApplication().hasLinksTo(sels[0].pObject);
}

void StdCmdLinkSelectAllLinks::activated(int)
{
    auto sels = Selection().getSelection("*", ResolveMode::OldStyleElement, true);
    if(sels.empty())
        return;
    Selection().selStackPush();
    Selection().clearCompleteSelection();
    TreeWidget::selectAllLinks(sels[0].pObject);
    Selection().selStackPush();
}


//======================================================================
// Std_LinkSelectActions
//===========================================================================

class StdCmdLinkSelectActions : public GroupCommand
{
public:
    StdCmdLinkSelectActions()
        : GroupCommand("Std_LinkSelectActions")
    {
        sGroup        = "View";
        sMenuText     = QT_TR_NOOP("Link navigation");
        sToolTipText  = QT_TR_NOOP("Link navigation actions");
        sWhatsThis    = "Std_LinkSelectActions";
        sStatusTip    = QT_TR_NOOP("Link navigation actions");
        eType         = AlterSelection;
        bCanLog       = false;

        addCommand(new StdCmdLinkSelectLinked());
        addCommand(new StdCmdLinkSelectLinkedFinal());
        addCommand(new StdCmdLinkSelectAllLinks());
        addCommand(Application::Instance->commandManager().getCommandByName("Std_TreeSelectAllInstances"), false);
    }

    const char* className() const override {return "StdCmdLinkSelectActions";}
};

//======================================================================
// Std_LinkActions
//===========================================================================

class StdCmdLinkActions : public GroupCommand
{
public:
    StdCmdLinkActions()
        : GroupCommand("Std_LinkActions")
    {
        sGroup        = "View";
        sMenuText     = QT_TR_NOOP("Link actions");
        sToolTipText  = QT_TR_NOOP("Actions that apply to link objects");
        sWhatsThis    = "Std_LinkMakeRelative";
        sStatusTip    = QT_TR_NOOP("Actions that apply to link objects");
        eType         = AlterDoc;
        bCanLog       = false;

        addCommand(new StdCmdLinkMake());
        addCommand(new StdCmdLinkMakeRelative());
        addCommand(new StdCmdLinkReplace());
        addCommand(new StdCmdLinkUnlink());
        addCommand(new StdCmdLinkImport());
        addCommand(new StdCmdLinkImportAll());
        addCommand();
        addCommand(new StdCmdLinkCreateInPlace());
        addCommand(new StdCmdLinkCreateInContainer());
    }

    const char* className() const override {return "StdCmdLinkActions";}
};

//===========================================================================
// Instantiation
//===========================================================================


namespace Gui {

void CreateLinkCommands()
{
    CommandManager &rcCmdMgr = Application::Instance->commandManager();
    rcCmdMgr.addCommand(new StdCmdLinkActions());
    rcCmdMgr.addCommand(new StdCmdLinkMakeGroup());
    rcCmdMgr.addCommand(new StdCmdLinkSelectActions());

}

} // namespace Gui

