/***************************************************************************
 *   Copyright (c) 2004 Jürgen Riegel <juergen.riegel@web.de>              *
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
#include "Renderer/Renderer.h"

#ifndef _PreComp_
# include <mutex>
# include <QApplication>
# include <QFileInfo>
# include <QMessageBox>
# include <QTextStream>
# include <QTimer>
# include <QStatusBar>
# include <Inventor/actions/SoSearchAction.h>
# include <Inventor/nodes/SoSeparator.h>
#endif

#include <cctype>
#include <boost/algorithm/string/predicate.hpp>

#include <App/AutoTransaction.h>
#include <App/Document.h>
#include <App/DocumentObject.h>
#include <App/DocumentObjectGroup.h>
#include <App/GeoFeatureGroupExtension.h>
#include <App/Transactions.h>
#include <App/ElementNamingUtils.h>
#include <Base/Console.h>
#include <Base/Exception.h>
#include <Base/Matrix.h>
#include <Base/Reader.h>
#include <Base/Writer.h>
#include <Base/Tools.h>

#include "Document.h"
#include "DocumentPy.h"
#include "Application.h"
#include "Command.h"
#include "Control.h"
#include "FileDialog.h"
#include "MainWindow.h"
#include "MDIView.h"
#include "NotificationArea.h"
#include "Selection.h"
#include "Thumbnail.h"
#include "Tree.h"
#include "View3DInventor.h"
#include "View3DInventorViewer.h"
#include "ViewParams.h"
#include "ViewProviderDocumentObject.h"
#include "ViewProviderDocumentObjectGroup.h"
#include "ViewProviderLink.h"
#include "WaitCursor.h"


FC_LOG_LEVEL_INIT("Gui", true, true)

using namespace Gui;
namespace sp = std::placeholders;

namespace Gui {

struct CameraInfo {
    int id;
    int binding;
    std::string settings;

    CameraInfo(int i, int b, std::string &&s)
        :id(i), binding(b), settings(std::move(s))
    {}
};

// Pimpl class
struct DocumentP
{
    Thumbnail thumb;
    int        _iWinCount;
    int        _iDocId;
    bool       _isClosing;
    bool       _isModified;
    bool       _isTransacting;
    bool       _hasExpansion;
    bool       _changeViewTouchDocument;
    int                         _editMode;
    CoinPtr<SoNode>             _editRootNode;
    ViewProvider*               _editViewProvider;
    App::DocumentObject*        _editingObject;
    ViewProviderDocumentObject* _editViewProviderParent;
    std::string                 _editSubname;
    std::string                 _editSubElement;
    Base::Matrix4D              _editingTransform;
    View3DInventorViewer*       _editingViewer;
    std::set<const App::DocumentObject*> _editObjs;

    std::vector<CameraInfo>     _savedViews;
    std::map<int, std::string>  _view3DContents;

    Application*    _pcAppWnd;
    // the doc/Document
    App::Document*  _pcDocument;
    /// List of all registered views
    std::list<Gui::BaseView*> baseViews;
    /// List of all registered views
    std::list<Gui::BaseView*> passiveViews;
    std::map<const App::DocumentObject*,ViewProviderDocumentObject*> _ViewProviderMap;
    std::map<SoSeparator *,ViewProviderDocumentObject*> _CoinMap;
    std::map<std::string,ViewProvider*> _ViewProviderMapAnnotation;
    std::vector<const App::DocumentObject*> _redoObjects;

    // cache map from view provider to its 3D claimed children
    std::unordered_map<const ViewProvider*,std::vector<App::DocumentObject*> > _ChildrenMap;

    // Reference counted view providers that are 3D claimed by other object.
    // These view providers shouldn't appear at secen graph root.
    std::unordered_map<const ViewProvider*, int> _ClaimedViewProviders;

    using Connection = boost::signals2::connection;
    Connection connectNewObject;
    Connection connectDelObject;
    Connection connectCngObject;
    Connection connectRenObject;
    Connection connectActObject;
    Connection connectSaveDocument;
    Connection connectRestDocument;
    Connection connectStartLoadDocument;
    Connection connectFinishLoadDocument;
    Connection connectShowHidden;
    Connection connectFinishRestoreObject;
    Connection connectExportObjects;
    Connection connectImportObjects;
    Connection connectFinishImportObjects;
    Connection connectUndoDocument;
    Connection connectRedoDocument;
    Connection connectRecomputed;
    Connection connectSkipRecompute;
    Connection connectTransactionAppend;
    Connection connectTransactionRemove;
    Connection connectTouchedObject;
    Connection connectPurgeTouchedObject;
    Connection connectChangePropertyEditor;
    Connection connectStartSave;
    Connection connectChangeDocument;

    using ConnectionBlock = boost::signals2::shared_connection_block;
    ConnectionBlock connectActObjectBlocker;
    ConnectionBlock connectChangeDocumentBlocker;

    App::PropertyStringList * getOnTopProperty(App::Document *doc, bool create) {
        try {
            if (!doc)
                return nullptr;
            auto prop = doc->getPropertyByName("OnTopObjects");
            if (!prop || !prop->isDerivedFrom(App::PropertyStringList::getClassTypeId())) {
                if (!create)
                    return nullptr;
                if (prop)
                    doc->removeDynamicProperty("OnTopObjects");
                prop = doc->addDynamicProperty("App::PropertyStringList", "OnTopObjects", "Views");
            }
            return static_cast<App::PropertyStringList*>(prop);
        } catch (Base::Exception &e) {
            e.ReportException();
        }
        return nullptr;
    }
};
} // namespace Gui

/* TRANSLATOR Gui::Document */

/// @namespace Gui @class Document

int Document::_iDocCount = 0;

Document::Document(App::Document* pcDocument,Application * app)
{
    d = new DocumentP;
    d->_iWinCount = 1;
    // new instance
    d->_iDocId = (++_iDocCount);
    d->_isClosing = false;
    d->_isModified = false;
    d->_isTransacting = false;
    d->_hasExpansion = false;
    d->_pcAppWnd = app;
    d->_pcDocument = pcDocument;
    d->thumb.setUpdateOnSave(pcDocument->SaveThumbnail.getValue());
    d->_editViewProvider = nullptr;
    d->_editRootNode = nullptr;
    d->_editingObject = nullptr;
    d->_editViewProviderParent = nullptr;
    d->_editingViewer = nullptr;
    d->_editMode = 0;

    //NOLINTBEGIN
    // Setup the connections
    d->connectNewObject = pcDocument->signalNewObject.connect
        (std::bind(&Gui::Document::slotNewObject, this, sp::_1), boost::signals2::at_front);
    d->connectDelObject = pcDocument->signalDeletedObject.connect
        (std::bind(&Gui::Document::slotDeletedObject, this, sp::_1));
    d->connectCngObject = pcDocument->signalChangedObject.connect
        (std::bind(&Gui::Document::slotChangedObject, this, sp::_1, sp::_2));
    d->connectRenObject = pcDocument->signalRelabelObject.connect
        (std::bind(&Gui::Document::slotRelabelObject, this, sp::_1));
    d->connectActObject = pcDocument->signalActivatedObject.connect
        (std::bind(&Gui::Document::slotActivatedObject, this, sp::_1));
    d->connectActObjectBlocker = boost::signals2::shared_connection_block
        (d->connectActObject, false);
    d->connectSaveDocument = pcDocument->signalSaveDocument.connect
        (std::bind(&Gui::Document::Save, this, sp::_1));
    d->connectRestDocument = pcDocument->signalRestoreDocument.connect
        (std::bind(&Gui::Document::Restore, this, sp::_1));
    d->connectStartLoadDocument = App::GetApplication().signalStartRestoreDocument.connect
        (std::bind(&Gui::Document::slotStartRestoreDocument, this, sp::_1));
    d->connectFinishLoadDocument = App::GetApplication().signalFinishRestoreDocument.connect
        (std::bind(&Gui::Document::slotFinishRestoreDocument, this, sp::_1));
    d->connectShowHidden = App::GetApplication().signalShowHidden.connect
        (std::bind(&Gui::Document::slotShowHidden, this, sp::_1));

    d->connectChangePropertyEditor = pcDocument->signalChangePropertyEditor.connect
        (std::bind(&Gui::Document::slotChangePropertyEditor, this, sp::_1, sp::_2));
    d->connectChangeDocument = d->_pcDocument->signalChanged.connect // use the same slot function
        (std::bind(&Gui::Document::slotChangePropertyEditor, this, sp::_1, sp::_2));
    d->connectChangeDocumentBlocker = boost::signals2::shared_connection_block
        (d->connectChangeDocument, true);
    d->connectFinishRestoreObject = pcDocument->signalFinishRestoreObject.connect
        (std::bind(&Gui::Document::slotFinishRestoreObject, this, sp::_1));
    d->connectExportObjects = pcDocument->signalExportViewObjects.connect
        (std::bind(&Gui::Document::exportObjects, this, sp::_1, sp::_2));
    d->connectImportObjects = pcDocument->signalImportViewObjects.connect
        (std::bind(&Gui::Document::importObjects, this, sp::_1, sp::_2, sp::_3));
    d->connectFinishImportObjects = pcDocument->signalFinishImportObjects.connect
        (std::bind(&Gui::Document::slotFinishImportObjects, this, sp::_1));

    d->connectUndoDocument = pcDocument->signalUndo.connect
        (std::bind(&Gui::Document::slotUndoDocument, this, sp::_1));
    d->connectRedoDocument = pcDocument->signalRedo.connect
        (std::bind(&Gui::Document::slotRedoDocument, this, sp::_1));
    d->connectRecomputed = pcDocument->signalRecomputed.connect
        (std::bind(&Gui::Document::slotRecomputed, this, sp::_1, sp::_2));
    d->connectSkipRecompute = pcDocument->signalSkipRecompute.connect
        (std::bind(&Gui::Document::slotSkipRecompute, this, sp::_1, sp::_2));
    d->connectTouchedObject = pcDocument->signalTouchedObject.connect
        (std::bind(&Gui::Document::slotTouchedObject, this, sp::_1));
    d->connectPurgeTouchedObject = pcDocument->signalPurgeTouchedObject.connect
        (std::bind(&Gui::Document::slotTouchedObject, this, sp::_1));

    d->connectTransactionAppend = pcDocument->signalTransactionAppend.connect
        (std::bind(&Gui::Document::slotTransactionAppend, this, sp::_1, sp::_2));
    d->connectTransactionRemove = pcDocument->signalTransactionRemove.connect
        (std::bind(&Gui::Document::slotTransactionRemove, this, sp::_1, sp::_2));
    //NOLINTEND

    d->connectStartSave = pcDocument->signalStartSave.connect(
        [this](const App::Document &doc, const std::string &) {
            try {
                std::vector<std::string> values;
                std::ostringstream ss;
                foreachView<View3DInventor>([&](View3DInventor* view) {
                    for (auto &objT : view->getViewer()->getObjectsOnTop()) {
                        ss.str("");
                        ss << view->getID() << ":" << objT.getSubNameNoElement(true);
                        values.push_back(ss.str());
                    }
                });
                // First try to get existing on top property
                if (auto prop = d->getOnTopProperty(& const_cast<App::Document&>(doc), false)) {
                    prop->setValues(std::move(values));
                }
                else if (values.size()) {
                    // If cannot, then only create the property if there is on
                    // top object
                    if (auto prop = d->getOnTopProperty(& const_cast<App::Document&>(doc), true))
                        prop->setValues(std::move(values));
                }

            } catch (Base::Exception &e) {
                e.ReportException();
            }
        });

    // pointer to the python class
    // NOTE: As this Python object doesn't get returned to the interpreter we
    // mustn't increment it (Werner Jan-12-2006)
    _pcDocPy = new Gui::DocumentPy(this);

    ParameterGrp::handle hGrp = App::GetApplication().GetParameterGroupByPath("User parameter:BaseApp/Preferences/Document");
    if (hGrp->GetBool("UsingUndo",true)) {
        d->_pcDocument->setUndoMode(1);
        // set the maximum stack size
        d->_pcDocument->setMaxUndoStackSize(hGrp->GetInt("MaxUndoSize",20));
    }

    d->_changeViewTouchDocument = hGrp->GetBool("ChangeViewProviderTouchDocument", true);
}

Document::~Document()
{
    // disconnect everything to avoid to be double-deleted
    // in case an exception is raised somewhere
    d->connectNewObject.disconnect();
    d->connectDelObject.disconnect();
    d->connectCngObject.disconnect();
    d->connectRenObject.disconnect();
    d->connectActObject.disconnect();
    d->connectSaveDocument.disconnect();
    d->connectRestDocument.disconnect();
    d->connectStartLoadDocument.disconnect();
    d->connectFinishLoadDocument.disconnect();
    d->connectShowHidden.disconnect();
    d->connectFinishRestoreObject.disconnect();
    d->connectExportObjects.disconnect();
    d->connectImportObjects.disconnect();
    d->connectFinishImportObjects.disconnect();
    d->connectUndoDocument.disconnect();
    d->connectRedoDocument.disconnect();
    d->connectRecomputed.disconnect();
    d->connectSkipRecompute.disconnect();
    d->connectTransactionAppend.disconnect();
    d->connectTransactionRemove.disconnect();
    d->connectTouchedObject.disconnect();
    d->connectPurgeTouchedObject.disconnect();
    d->connectChangePropertyEditor.disconnect();
    d->connectStartSave.disconnect();
    d->connectChangeDocument.disconnect();

    // e.g. if document gets closed from within a Python command
    d->_isClosing = true;
    // calls Document::detachView() and alter the view list
    std::list<Gui::BaseView*> temp = d->baseViews;
    for(auto & it : temp)
        it->deleteSelf();

    std::map<const App::DocumentObject*,ViewProviderDocumentObject*>::iterator jt;
    for (jt = d->_ViewProviderMap.begin();jt != d->_ViewProviderMap.end(); ++jt)
        delete jt->second;
    std::map<std::string,ViewProvider*>::iterator it2;
    for (it2 = d->_ViewProviderMapAnnotation.begin();it2 != d->_ViewProviderMapAnnotation.end(); ++it2)
        delete it2->second;

    // remove the reference from the object
    Base::PyGILStateLocker lock;
    _pcDocPy->setInvalid();
    _pcDocPy->DecRef();
    delete d;
}

//*****************************************************************************************************
// 3D viewer handling
//*****************************************************************************************************

struct EditDocumentGuard {
    EditDocumentGuard():active(true) {}

    ~EditDocumentGuard() {
        if(active)
            Application::Instance->setEditDocument(0);
    }

    bool active;
};

bool Document::setEdit(Gui::ViewProvider* p, int ModNum, const char *subname)
{
    static bool _Busy;
    if (_Busy) {
        if (FC_LOG_INSTANCE.isEnabled(FC_LOGLEVEL_LOG))
            FC_WARN("Ignore recursive call to Document::setEdit()");
        return false;
    }
    Base::StateLocker lock(_Busy);

    auto vp = dynamic_cast<ViewProviderDocumentObject*>(p);
    if (!vp) {
        FC_ERR("cannot edit non ViewProviderDocumentObject");
        return false;
    }

    // Fix regression: https://forum.freecad.org/viewtopic.php?f=19&t=43629&p=371972#p371972
    // When an object is already in edit mode a subsequent call for editing is only possible
    // when resetting the currently edited object.
    if (d->_editViewProvider) {
        _resetEdit();
    }

    auto obj = vp->getObject();
    if(!obj->isAttachedToDocument()) {
        FC_ERR("cannot edit detached object");
        return false;
    }

    std::string _subname;
    if(!subname || !subname[0]) {
        // No subname reference is given, we try to extract one from the current
        // selection in order to obtain the correct transformation matrix below
        App::DocumentObject *parentObj = nullptr;
        auto ctxobj = Gui::Selection().getContext().getSubObject();
        bool found = false;
        if (ctxobj && (ctxobj == obj || ctxobj->getLinkedObject(true) == obj)) {
            parentObj = Gui::Selection().getContext().getObject();
            _subname = Gui::Selection().getContext().getSubName();
            found = true;
        } else {
            auto sels = Gui::Selection().getCompleteSelection(ResolveMode::NoResolve);
            for(auto &sel : sels) {
                if(!sel.pObject || !sel.pObject->isAttachedToDocument())
                    continue;
                if(!parentObj)
                    parentObj = sel.pObject;
                else if(parentObj!=sel.pObject) {
                    FC_LOG("Cannot deduce subname for editing, more than one parent?");
                    parentObj = nullptr;
                    break;
                }
                auto sobj = parentObj->getSubObject(sel.SubName);
                if(!sobj || (sobj!=obj && sobj->getLinkedObject(true)!= obj)) {
                    FC_LOG("Cannot deduce subname for editing, subname mismatch");
                    parentObj = nullptr;
                    break;
                }
                _subname = sel.SubName;
                found = true;
            }
        }
        if (!found) {
            parentObj = obj;
            Gui::Selection().checkTopParent(parentObj, _subname);
        }
        if(parentObj) {
            FC_LOG("deduced editing reference " << parentObj->getFullName() << '.' << _subname);
            subname = _subname.c_str();
            obj = parentObj;
            vp = dynamic_cast<ViewProviderDocumentObject*>(
                    Application::Instance->getViewProvider(obj));
            if(!vp || !vp->getDocument()) {
                FC_ERR("invalid view provider for parent object");
                return false;
            }
            if(vp->getDocument()!=this)
                return vp->getDocument()->setEdit(vp,ModNum,subname);
        }
    }

    if (d->_ViewProviderMap.find(obj) == d->_ViewProviderMap.end()) {
        // We can actually support editing external object, by calling
        // View3DInventViewer::setupEditingRoot() before exiting from
        // ViewProvider::setEditViewer(), which transfer all child node of the view
        // provider into an editing node inside the viewer of this document. And
        // that's may actually be the case, as the subname referenced sub object
        // is allowed to be in other documents.
        //
        // We just disabling editing external parent object here, for bug
        // tracking purpose. Because, bringing an unrelated external object to
        // the current view for editing will confuse user, and is certainly a
        // bug. By right, the top parent object should always belong to the
        // editing document, and the actually editing sub object can be
        // external.
        //
        // So, you can either call setEdit() with subname set to 0, which cause
        // the code above to auto detect selection context, and dispatch the
        // editing call to the correct document. Or, supply subname yourself,
        // and make sure you get the document right.
        //
        FC_ERR("cannot edit object '" << obj->getNameInDocument() << "': not found in document "
                << "'" << getDocument()->getName() << "'");
        return false;
    }

    d->_editingTransform = Base::Matrix4D();
    // Geo feature group now handles subname like link group. So no need of the
    // following code.
    //
    // if(!subname || !subname[0]) {
    //     auto group = App::GeoFeatureGroupExtension::getGroupOfObject(obj);
    //     if(group) {
    //         auto ext = group->getExtensionByType<App::GeoFeatureGroupExtension>();
    //         d->_editingTransform = ext->globalGroupPlacement().toMatrix();
    //     }
    // }
    d->_editSubElement.clear();
    d->_editSubname.clear();
    if (subname) {
        const char *element = Data::findElementName(subname);
        if (element) {
            d->_editSubname = std::string(subname,element-subname);
            d->_editSubElement = element;
        }
        else {
            d->_editSubname = subname;
        }
    }
    auto sobj = obj->getSubObject(d->_editSubname.c_str(),nullptr,&d->_editingTransform);
    if(!sobj || !sobj->isAttachedToDocument()) {
        FC_ERR("Invalid sub object '" << obj->getFullName()
                << '.' << (subname?subname:"") << "'");
        return false;
    }
    auto svp = vp;
    if(sobj!=obj) {
        svp = dynamic_cast<ViewProviderDocumentObject*>(
                Application::Instance->getViewProvider(sobj));
        if(!svp) {
            FC_ERR("Cannot edit '" << sobj->getFullName() << "' without view provider");
            return false;
        }
    }

    auto view3d = dynamic_cast<View3DInventor *>(getActiveView());
    // if the currently active view is not the 3d view search for it and activate it
    if (view3d)
        getMainWindow()->setActiveWindow(view3d);
    else
        view3d = dynamic_cast<View3DInventor *>(setActiveView(vp));

    EditDocumentGuard guard;
    Application::Instance->setEditDocument(this);

    d->_editViewProviderParent = vp;

    auto sobjs = obj->getSubObjectList(subname);
    d->_editObjs.clear();
    d->_editObjs.insert(sobjs.begin(),sobjs.end());
    d->_editingObject = sobj;

    Base::ObjectStatusLocker<App::ObjectStatus, App::DocumentObject> 
        guard2(App::ObjEditing, sobj);

    d->_editMode = ModNum;
    d->_editViewProvider = svp->startEditing(ModNum);
    if(!d->_editViewProvider) {
        d->_editViewProviderParent = nullptr;
        d->_editObjs.clear();
        d->_editingObject = nullptr;
        FC_LOG("object '" << sobj->getFullName() << "' refuse to edit");
        // Some code somewhere may try to delete the editing object (e.g.
        // undo), but couldn't because of the App::ObjEditing status we set
        // here. We will reset the App::ObjEditing status to flush any pending
        // removal.
        guard2.detach();
        App::Document::clearPendingRemove();
        return false;
    } else if (Application::Instance->editDocument() != this) {
        // It is possible when calling startEditing() above show triggers some
        // code to call resetEdit(), which prematrually clears the
        // Application::editDocument(). In this cause we shall abort.
        _resetEdit();
        guard2.detach();
        App::Document::clearPendingRemove();
        return false;
    }

    if(view3d) {
        view3d->getViewer()->setEditingViewProvider(d->_editViewProvider,ModNum);
        d->_editingViewer = view3d->getViewer();
        d->_editRootNode = view3d->getViewer()->getEditRootNode();
    }
    Gui::TaskView::TaskDialog* dlg = Gui::Control().activeDialog();
    if (dlg)
        dlg->setDocumentName(this->getDocument()->getName());
    if (d->_editViewProvider->isDerivedFrom(ViewProviderDocumentObject::getClassTypeId())) {
        auto vpd = static_cast<ViewProviderDocumentObject*>(d->_editViewProvider);
        vpd->getDocument()->signalInEdit(*vpd);
    }
    guard.active = false;
    App::AutoTransaction::setEnable(false);
    guard2.detach(/*reset*/d->_editViewProvider==nullptr);
    App::Document::clearPendingRemove();
    return d->_editViewProvider != nullptr;
}

const Base::Matrix4D &Document::getEditingTransform() const {
    return d->_editingTransform;
}

void Document::setEditingTransform(const Base::Matrix4D &mat) {
    d->_editObjs.clear();
    d->_editingTransform = mat;
    auto activeView = dynamic_cast<View3DInventor *>(getActiveView());
    if (activeView)
        activeView->getViewer()->setEditingTransform(mat);
}

void Document::resetEdit() {
    Application::Instance->setEditDocument(nullptr);
}

void Document::_resetEdit()
{
    std::list<Gui::BaseView*>::iterator it;
    if (d->_editViewProvider) {
        for (it = d->baseViews.begin();it != d->baseViews.end();++it) {
            auto activeView = dynamic_cast<View3DInventor *>(*it);
            if (activeView)
                activeView->getViewer()->resetEditingViewProvider();
        }

        if (d->_editingObject)
            d->_editingObject->setStatus(App::ObjEditing, false);
        d->_editViewProvider->finishEditing();

        // Have to check d->_editViewProvider below, because there is a chance
        // the editing object gets deleted inside the above call to
        // 'finishEditing()', which will trigger our slotDeletedObject(), which
        // nullifies _editViewProvider.
        if (d->_editViewProvider && d->_editViewProvider->isDerivedFrom(ViewProviderDocumentObject::getClassTypeId())) {
            auto vpd = static_cast<ViewProviderDocumentObject*>(d->_editViewProvider);
            vpd->getDocument()->signalResetEdit(*vpd);
        }
        d->_editViewProvider = nullptr;

        // The logic below is not necessary anymore, because this method is
        // changed into a private one,  _resetEdit(). And the exposed
        // resetEdit() above calls into Application->setEditDocument(0) which
        // will prevent recursive calling.

        App::GetApplication().closeActiveTransaction();
    }
    d->_editViewProviderParent = nullptr;
    d->_editingViewer = nullptr;
    d->_editObjs.clear();
    d->_editingObject = nullptr;
    d->_editRootNode.reset();
    if(Application::Instance->editDocument() == this)
        Application::Instance->setEditDocument(nullptr);
}

App::SubObjectT Document::getInEditT(int *mode) const
{
    ViewProviderDocumentObject *parentVp = nullptr;
    std::string subname;
    auto vp = getInEdit(&parentVp, &subname, mode);
    if (!parentVp)
        parentVp = Base::freecad_dynamic_cast<ViewProviderDocumentObject>(vp);
    if (parentVp)
        return App::SubObjectT(parentVp->getObject(), subname.c_str());
    return App::SubObjectT();
}

ViewProvider *Document::getInEdit(ViewProviderDocumentObject **parentVp,
        std::string *subname, int *mode, std::string *subelement) const
{
    if(parentVp) *parentVp = d->_editViewProviderParent;
    if(subname) *subname = d->_editSubname;
    if(subelement) *subelement = d->_editSubElement;
    if(mode) *mode = d->_editMode;

    if (d->_editViewProvider) {
        // there is only one 3d view which is in edit mode
        auto activeView = dynamic_cast<View3DInventor *>(getActiveView());
        if (activeView && activeView->getViewer()->isEditingViewProvider())
            return d->_editViewProvider;
    }

    return nullptr;
}

void Document::setInEdit(ViewProviderDocumentObject *parentVp, const char *subname) {
    if (d->_editViewProvider) {
        d->_editViewProviderParent = parentVp;
        d->_editSubname = subname?subname:"";
    }
}

void Document::setAnnotationViewProvider(const char* name, ViewProvider *pcProvider)
{
    std::list<Gui::BaseView*>::iterator vIt;

    // already in ?
    std::map<std::string,ViewProvider*>::iterator it = d->_ViewProviderMapAnnotation.find(name);
    if (it != d->_ViewProviderMapAnnotation.end())
        removeAnnotationViewProvider(name);

    // add
    d->_ViewProviderMapAnnotation[name] = pcProvider;

    // cycling to all views of the document
    for (vIt = d->baseViews.begin();vIt != d->baseViews.end();++vIt) {
        auto activeView = dynamic_cast<View3DInventor *>(*vIt);
        if (activeView)
            activeView->getViewer()->addViewProvider(pcProvider);
    }
}

ViewProvider * Document::getAnnotationViewProvider(const char* name) const
{
    std::map<std::string,ViewProvider*>::const_iterator it = d->_ViewProviderMapAnnotation.find(name);
    return ( (it != d->_ViewProviderMapAnnotation.end()) ? it->second : 0 );
}

void Document::removeAnnotationViewProvider(const char* name)
{
    std::map<std::string,ViewProvider*>::iterator it = d->_ViewProviderMapAnnotation.find(name);
    std::list<Gui::BaseView*>::iterator vIt;

    // cycling to all views of the document
    for (vIt = d->baseViews.begin();vIt != d->baseViews.end();++vIt) {
        auto activeView = dynamic_cast<View3DInventor *>(*vIt);
        if (activeView)
            activeView->getViewer()->removeViewProvider(it->second);
    }

    delete it->second;
    d->_ViewProviderMapAnnotation.erase(it);
}


ViewProvider* Document::getViewProvider(const App::DocumentObject* Feat) const
{
    std::map<const App::DocumentObject*,ViewProviderDocumentObject*>::const_iterator
    it = d->_ViewProviderMap.find( Feat );
    return ( (it != d->_ViewProviderMap.end()) ? it->second : 0 );
}

std::vector<ViewProvider*> Document::getViewProvidersOfType(const Base::Type& typeId) const
{
    std::vector<ViewProvider*> Objects;
    for (std::map<const App::DocumentObject*,ViewProviderDocumentObject*>::const_iterator it =
         d->_ViewProviderMap.begin(); it != d->_ViewProviderMap.end(); ++it ) {
        if (it->second->getTypeId().isDerivedFrom(typeId))
            Objects.push_back(it->second);
    }
    return Objects;
}

ViewProvider *Document::getViewProviderByName(const char* name) const
{
    // first check on feature name
    App::DocumentObject *pcFeat = getDocument()->getObject(name);

    if (pcFeat)
    {
        std::map<const App::DocumentObject*,ViewProviderDocumentObject*>::const_iterator
        it = d->_ViewProviderMap.find( pcFeat );

        if (it != d->_ViewProviderMap.end())
            return it->second;
    } else {
        // then try annotation name
        std::map<std::string,ViewProvider*>::const_iterator it2 = d->_ViewProviderMapAnnotation.find( name );

        if (it2 != d->_ViewProviderMapAnnotation.end())
            return it2->second;
    }

    return nullptr;
}

bool Document::isShow(const char* name)
{
    ViewProvider* pcProv = getViewProviderByName(name);
    return pcProv ? pcProv->isShow() : false;
}

/// put the feature in show
void Document::setShow(const char* name)
{
    ViewProvider* pcProv = getViewProviderByName(name);

    if (pcProv && pcProv->isDerivedFrom<ViewProviderDocumentObject>()) {
        static_cast<ViewProviderDocumentObject*>(pcProv)->Visibility.setValue(true);
    }
}

/// set the feature in Noshow
void Document::setHide(const char* name)
{
    ViewProvider* pcProv = getViewProviderByName(name);

    if (pcProv && pcProv->isDerivedFrom<ViewProviderDocumentObject>()) {
        static_cast<ViewProviderDocumentObject*>(pcProv)->Visibility.setValue(false);
    }
}

/// set the feature in Noshow
void Document::setPos(const char* name, const Base::Matrix4D& rclMtrx)
{
    ViewProvider* pcProv = getViewProviderByName(name);
    if (pcProv)
        pcProv->setTransformation(rclMtrx);

}

//*****************************************************************************************************
// Document
//*****************************************************************************************************
void Document::slotNewObject(const App::DocumentObject& Obj)
{
    auto pcProvider = static_cast<ViewProviderDocumentObject*>(getViewProvider(&Obj));
    if (!pcProvider) {
        std::string cName = Obj.getViewProviderNameStored();
        for(;;) {
            if (cName.empty()) {
                // handle document object with no view provider specified
                FC_LOG(Obj.getFullName() << " has no view provider specified");
                return;
            }
            Base::Type type = Base::Type::getTypeIfDerivedFrom(cName.c_str(), ViewProviderDocumentObject::getClassTypeId(), true);
            pcProvider = static_cast<ViewProviderDocumentObject*>(type.createInstance());
            // createInstance could return a null pointer
            if (!pcProvider) {
                // type not derived from ViewProviderDocumentObject!!!
                FC_ERR("Invalid view provider type '" << cName << "' for " << Obj.getFullName());
                return;
            }
            else if (cName!=Obj.getViewProviderName() && !pcProvider->allowOverride(Obj)) {
                FC_WARN("View provider type '" << cName << "' does not support " << Obj.getFullName());
                delete pcProvider;
                pcProvider = nullptr;
                cName = Obj.getViewProviderName();
            }
            else {
                break;
            }
        }

        setModified(true);
        d->_ViewProviderMap[&Obj] = pcProvider;
        d->_CoinMap[pcProvider->getRoot()] = pcProvider;
        pcProvider->setStatus(Gui::ViewStatus::TouchDocument, d->_changeViewTouchDocument);

        try {
            pcProvider->attachDocumentObject(const_cast<App::DocumentObject*>(&Obj));
            pcProvider->updateView();
            pcProvider->setActiveMode();
        }
        catch(const Base::MemoryException& e){
            FC_ERR("Memory exception in " << Obj.getFullName() << " thrown: " << e.what());
        }
        catch(Base::Exception &e){
            e.ReportException();
        }
#ifndef FC_DEBUG
        catch(...){
            FC_ERR("Unknown exception in Feature " << Obj.getFullName() << " thrown");
        }
#endif
    }else{
        try {
            pcProvider->reattach(const_cast<App::DocumentObject*>(&Obj));
        } catch(Base::Exception &e){
            e.ReportException();
        }
    }

    if (pcProvider) {
        std::list<Gui::BaseView*>::iterator vIt;
        // cycling to all views of the document
        for (vIt = d->baseViews.begin();vIt != d->baseViews.end();++vIt) {
            auto activeView = dynamic_cast<View3DInventor *>(*vIt);
            if (activeView)
                activeView->getViewer()->addViewProvider(pcProvider);
        }

        // adding to the tree
        signalNewObject(*pcProvider);
        pcProvider->pcDocument = this;

        // it is possible that a new viewprovider already claims children
        handleChildren3D(pcProvider);
        if (d->_isTransacting) {
            d->_redoObjects.push_back(&Obj);
        }
    }
}

void Document::slotDeletedObject(const App::DocumentObject& Obj)
{
    std::list<Gui::BaseView*>::iterator vIt;
    setModified(true);

    // cycling to all views of the document
    ViewProvider* viewProvider = getViewProvider(&Obj);
    if(!viewProvider)
        return;

    if (d->_editViewProvider==viewProvider || d->_editViewProviderParent==viewProvider)
        _resetEdit();
    else if(Application::Instance->editDocument()) {
        auto editDoc = Application::Instance->editDocument();
        if(editDoc->d->_editViewProvider==viewProvider ||
           editDoc->d->_editViewProviderParent==viewProvider)
            Application::Instance->setEditDocument(nullptr);
    }

    handleChildren3D(viewProvider,true);

    if (viewProvider && viewProvider->getTypeId().isDerivedFrom
        (ViewProviderDocumentObject::getClassTypeId())) {
        // go through the views
        for (vIt = d->baseViews.begin();vIt != d->baseViews.end();++vIt) {
            auto activeView = dynamic_cast<View3DInventor *>(*vIt);
            if (activeView)
                activeView->getViewer()->removeViewProvider(viewProvider);
        }

        // removing from tree
        signalDeletedObject(*(static_cast<ViewProviderDocumentObject*>(viewProvider)));
    }

    viewProvider->beforeDelete();
}

void Document::beforeDelete() {
    auto editDoc = Application::Instance->editDocument();
    if(editDoc) {
        auto vp = dynamic_cast<ViewProviderDocumentObject*>(editDoc->d->_editViewProvider);
        auto vpp = dynamic_cast<ViewProviderDocumentObject*>(editDoc->d->_editViewProviderParent);
        if(editDoc == this ||
           (vp && vp->getDocument()==this) ||
           (vpp && vpp->getDocument()==this))
        {
            Application::Instance->setEditDocument(nullptr);
        }
    }
    for(auto &v : d->_ViewProviderMap) {
        v.second->childSet.clear();
        v.second->parentSet.clear();
        v.second->beforeDelete();
    }

    d->_isClosing = true;
    std::list<Gui::BaseView*> temp = d->baseViews;
    for(std::list<Gui::BaseView*>::iterator it=temp.begin();it!=temp.end();++it)
        (*it)->deleteSelf();
    d->baseViews.clear();
}

void Document::slotChangedObject(const App::DocumentObject& Obj, const App::Property& Prop)
{
    ViewProvider* viewProvider = getViewProvider(&Obj);
    if (viewProvider) {
        ViewProvider::clearBoundingBoxCache();
        try {
            viewProvider->update(&Prop);
            if(d->_editingViewer
                    && d->_editingObject
                    && d->_editViewProviderParent
                    && (Prop.isDerivedFrom(App::PropertyPlacement::getClassTypeId())
                        // Issue ID 0004230 : getName() can return null in which case strstr() crashes
                        || (Prop.getName() && strstr(Prop.getName(),"Scale")))
                    && d->_editObjs.count(&Obj))
            {
                Base::Matrix4D mat;
                auto sobj = d->_editViewProviderParent->getObject()->getSubObject(
                                                        d->_editSubname.c_str(),nullptr,&mat);
                if(sobj == d->_editingObject && d->_editingTransform!=mat) {
                    d->_editingTransform = mat;
                    d->_editingViewer->setEditingTransform(d->_editingTransform);
                    signalEditingTransformChanged(*this);
                }
            }
        }
        catch(const Base::MemoryException& e) {
            FC_ERR("Memory exception in " << Obj.getFullName() << " thrown: " << e.what());
        }
        catch(Base::Exception& e){
            e.ReportException();
        }
        catch(const std::exception& e){
            FC_ERR("C++ exception in " << Obj.getFullName() << " thrown " << e.what());
        }
        catch (...) {
            FC_ERR("Cannot update representation for " << Obj.getFullName());
        }

        handleChildren3D(viewProvider);

        if (viewProvider->isDerivedFrom(ViewProviderDocumentObject::getClassTypeId()))
            signalChangedObject(static_cast<ViewProviderDocumentObject&>(*viewProvider), Prop);
    }

    // a property of an object has changed
    if(!Prop.testStatus(App::Property::NoModify) && !isModified()) {
        FC_LOG(Prop.getFullName() << " modified");
        setModified(true);
    }

    getMainWindow()->updateActions(true);
}

void Document::slotRelabelObject(const App::DocumentObject& Obj)
{
    ViewProvider* viewProvider = getViewProvider(&Obj);
    if (viewProvider && viewProvider->isDerivedFrom(ViewProviderDocumentObject::getClassTypeId())) {
        signalRelabelObject(*(static_cast<ViewProviderDocumentObject*>(viewProvider)));
    }
}

void Document::slotTransactionAppend(const App::DocumentObject& obj, App::Transaction* transaction)
{
    ViewProvider* viewProvider = getViewProvider(&obj);
    if (viewProvider && viewProvider->isDerivedFrom(ViewProviderDocumentObject::getClassTypeId())) {
        transaction->addObjectDel(viewProvider);
    }
}

void Document::slotTransactionRemove(const App::DocumentObject& obj, App::Transaction* transaction)
{
    std::map<const App::DocumentObject*,ViewProviderDocumentObject*>::const_iterator
    it = d->_ViewProviderMap.find(&obj);
    if (it != d->_ViewProviderMap.end()) {
        ViewProvider* viewProvider = it->second;

        auto itC = d->_CoinMap.find(viewProvider->getRoot());
        if(itC != d->_CoinMap.end())
            d->_CoinMap.erase(itC);

        d->_ViewProviderMap.erase(&obj);
        // transaction being a nullptr indicates that undo/redo is off and the object
        // can be safely deleted
        if (transaction)
            transaction->addObjectNew(viewProvider);
        else if (!App::TransactionGuard::addPendingRemove(viewProvider))
            delete viewProvider;
    }
}

void Document::slotActivatedObject(const App::DocumentObject& Obj)
{
    ViewProvider* viewProvider = getViewProvider(&Obj);
    if (viewProvider && viewProvider->isDerivedFrom(ViewProviderDocumentObject::getClassTypeId())) {
        signalActivatedObject(*(static_cast<ViewProviderDocumentObject*>(viewProvider)));
    }
}

void Document::slotUndoDocument(const App::Document& doc)
{
    if (d->_pcDocument != &doc)
        return;

    signalUndoDocument(*this);
    getMainWindow()->updateActions();
}

void Document::slotRedoDocument(const App::Document& doc)
{
    if (d->_pcDocument != &doc)
        return;

    signalRedoDocument(*this);
    getMainWindow()->updateActions();
}

void Document::slotRecomputed(const App::Document& doc,
        const std::vector<App::DocumentObject*> &objs)
{
    if (d->_pcDocument != &doc)
        return;

    for (auto obj : objs) {
        auto vp  = Base::freecad_dynamic_cast<ViewProviderDocumentObject>(
                Application::Instance->getViewProvider(obj));
        if (vp)
            vp->updateChildren(false);
    }

    getMainWindow()->updateActions();
    TreeWidget::updateStatus();
}

// This function is called when some asks to recompute a document that is marked
// as 'SkipRecompute'. We'll check if we are the current document, and if either
// not given an explicit recomputing object list, or the given single object is
// the eidting object or the active object. If the conditions are met, we'll
// force recompute only that object and all its dependent objects.
void Document::slotSkipRecompute(const App::Document& doc, const std::vector<App::DocumentObject*> &objs)
{
    if (d->_pcDocument != &doc)
        return;
    if(objs.size()>1 ||
       App::GetApplication().getActiveDocument()!=&doc ||
       !doc.testStatus(App::Document::AllowPartialRecompute))
        return;
    App::DocumentObject *obj = nullptr;
    auto editDoc = Application::Instance->editDocument();
    if(editDoc) {
        auto vp = dynamic_cast<ViewProviderDocumentObject*>(editDoc->getInEdit());
        if(vp)
            obj = vp->getObject();
    }
    if(!obj)
        obj = doc.getActiveObject();
    if(!obj || !obj->isAttachedToDocument() || (!objs.empty() && objs.front()!=obj))
        return;
    obj->recomputeFeature(true);
}

void Document::slotTouchedObject(const App::DocumentObject &Obj)
{
    getMainWindow()->updateActions(true);
    if(!isModified()) {
        FC_LOG(Obj.getFullName() << (Obj.isTouched()?" touched":" purged"));
        setModified(true);
    }
    if (Obj.isTouched()) {
        auto it = d->_ViewProviderMap.find(&Obj);
        if (it !=d->_ViewProviderMap.end())
            it->second->updateChildren(true);
    }
}

void Document::addViewProvider(Gui::ViewProviderDocumentObject* vp)
{
    // Hint: The undo/redo first adds the view provider to the Gui
    // document before adding the objects to the App document.

    // the view provider is added by TransactionViewProvider and an
    // object can be there only once
    assert(d->_ViewProviderMap.find(vp->getObject()) == d->_ViewProviderMap.end());
    vp->setStatus(Detach, false);
    d->_ViewProviderMap[vp->getObject()] = vp;
    d->_CoinMap[vp->getRoot()] = vp;
}

void Document::setModified(bool b)
{
    if(d->_isModified == b)
        return;
    d->_isModified = b;

    std::list<MDIView*> mdis = getMDIViews();
    for (auto & mdi : mdis) {
        mdi->setWindowModified(b);
    }

    signalChangedModified(*this);
}

bool Document::isModified() const
{
    return d->_isModified;
}


ViewProviderDocumentObject* Document::getViewProviderByPathFromTail(SoPath * path) const
{
    // Get the lowest root node in the pick path!
    for (int i = 0; i < path->getLength(); i++) {
        SoNode *node = path->getNodeFromTail(i);
        if (node->isOfType(SoSeparator::getClassTypeId())) {
            auto it = d->_CoinMap.find(static_cast<SoSeparator*>(node));
            if(it!=d->_CoinMap.end())
                return it->second;
        }
    }

    return nullptr;
}

ViewProviderDocumentObject* Document::getViewProviderByPathFromHead(SoPath * path) const
{
    for (int i = 0; i < path->getLength(); i++) {
        SoNode *node = path->getNode(i);
        if (node->isOfType(SoSeparator::getClassTypeId())) {
            if (node == d->_editRootNode && d->_editViewProvider)
                return Base::freecad_dynamic_cast<ViewProviderDocumentObject>(d->_editViewProvider);
            auto it = d->_CoinMap.find(static_cast<SoSeparator*>(node));
            if(it!=d->_CoinMap.end())
                return it->second;
        }
    }

    return nullptr;
}

ViewProviderDocumentObject *Document::getViewProvider(SoNode *node) const {
    if(!node || !node->isOfType(SoSeparator::getClassTypeId()))
        return nullptr;
    auto it = d->_CoinMap.find(static_cast<SoSeparator*>(node));
    if(it!=d->_CoinMap.end())
        return it->second;
    return nullptr;
}

std::vector<std::pair<ViewProviderDocumentObject*,int> > Document::getViewProvidersByPath(SoPath * path) const
{
    std::vector<std::pair<ViewProviderDocumentObject*,int> > ret;
    for (int i = 0; i < path->getLength(); i++) {
        SoNode *node = path->getNodeFromTail(i);
        if (node->isOfType(SoSeparator::getClassTypeId())) {
            auto it = d->_CoinMap.find(static_cast<SoSeparator*>(node));
            if(it!=d->_CoinMap.end())
                ret.emplace_back(it->second,i);
        }
    }
    return ret;
}

App::Document* Document::getDocument() const
{
    return d->_pcDocument;
}

static bool checkCanonicalPath(const std::map<App::Document*, bool> &docs)
{
    std::map<QString, std::vector<App::Document*> > paths;
    bool warn = false;
    for (auto doc : App::GetApplication().getDocuments()) {
        QFileInfo info(QString::fromUtf8(doc->FileName.getValue()));
        auto &d = paths[info.canonicalFilePath()];
        d.push_back(doc);
        if (!warn && d.size() > 1) {
            if (docs.count(d.front()) || docs.count(d.back()))
                warn = true;
        }
    }
    if (!warn)
        return true;
    QString msg;
    QTextStream ts(&msg);
    ts << QObject::tr("Identical physical path detected. It may cause unwanted overwrite of existing document!\n\n")
       << QObject::tr("Are you sure you want to continue?");

    auto docName = [](App::Document *doc) -> QString {
        if (doc->Label.getStrValue() == doc->getName())
            return QString::fromUtf8(doc->getName());
        return QStringLiteral("%1 (%2)").arg(QString::fromUtf8(doc->Label.getValue()),
                                             QString::fromUtf8(doc->getName()));
    };
    int count = 0;
    for (auto &v : paths) {
        if (v.second.size() <= 1) continue;
        for (auto doc : v.second) {
            if (docs.count(doc)) {
                FC_WARN("Physical path: " << v.first.toUtf8().constData());
                for (auto d : v.second)
                    FC_WARN("  Document: " << docName(d).toUtf8().constData()
                            << ": " << d->FileName.getValue());
                if (count == 3) {
                    ts << "\n\n"
                    << QObject::tr("Please check report view for more...");
                } else if (count < 3) {
                    ts << "\n\n"
                    << QObject::tr("Physical path:") << ' ' << v.first
                    << "\n"
                    << QObject::tr("Document:") << ' ' << docName(doc)
                    << "\n  "
                    << QObject::tr("Path:") << ' ' << QString::fromUtf8(doc->FileName.getValue());
                    for (auto d : v.second) {
                        if (d == doc) continue;
                        ts << "\n"
                        << QObject::tr("Document:") << ' ' << docName(d)
                        << "\n  "
                        << QObject::tr("Path:") << ' ' << QString::fromUtf8(d->FileName.getValue());
                    }
                }
                ++count;
                break;
            }
        }
    }
    int ret = QMessageBox::warning(getMainWindow(),
            QObject::tr("Identical physical path"), msg, QMessageBox::Yes, QMessageBox::No);
    return ret == QMessageBox::Yes;
}

bool Document::askIfSavingFailed(const QString& error)
{
    int ret = QMessageBox::question(
        getMainWindow(),
        QObject::tr("Could not save document"),
        QObject::tr("There was an issue trying to save the file. "
                    "This may be because some of the parent folders do not exist, "
                    "or you do not have sufficient permissions, "
                    "or for other reasons. Error details:\n\n\"%1\"\n\n"
                    "Would you like to save the file with a different name?")
        .arg(error),
        QMessageBox::Yes, QMessageBox::No);

    if (ret == QMessageBox::No) {
        // TODO: Understand what exactly is supposed to be returned here
        getMainWindow()->showMessage(QObject::tr("Saving aborted"), 2000);
        return false;
    }
    else if (ret == QMessageBox::Yes) {
        return saveAs();
    }

    return false;
}

/// Save the document
bool Document::save()
{
    if (d->_pcDocument->isSaved()) {
        try {
            std::vector<App::Document*> docs;
            std::map<App::Document*,bool> dmap;
            try {
                docs = getDocument()->getDependentDocuments();
                for (auto it=docs.begin(); it!=docs.end();) {
                    App::Document *doc = *it;
                    if (doc == getDocument()) {
                        dmap[doc] = doc->mustExecute();
                        ++it;
                        continue;
                    }
                    auto gdoc = Application::Instance->getDocument(doc);
                    if ((gdoc && !gdoc->isModified())
                            || doc->testStatus(App::Document::PartialDoc)
                            || doc->testStatus(App::Document::TempDoc))
                    {
                        it = docs.erase(it);
                        continue;
                    }
                    dmap[doc] = doc->mustExecute();
                    ++it;
                }
            }
            catch (const Base::RuntimeError &e) {
                e.ReportException();
                docs = {getDocument()};
                dmap.clear();
                dmap[getDocument()] = getDocument()->mustExecute();
            }
            if(dmap.size() > 1) {
                int ret = QMessageBox::question(getMainWindow(),
                        QObject::tr("Save dependent files"),
                        QObject::tr("The file contains external dependencies. "
                        "Do you want to save the dependent files, too?"),
                        QMessageBox::Yes,QMessageBox::No);

                if (ret != QMessageBox::Yes) {
                    docs = {getDocument()};
                    dmap.clear();
                    dmap[getDocument()] = getDocument()->mustExecute();
                }
            }

            if (!checkCanonicalPath(dmap))
                return false;

            Gui::WaitCursor wc;
            // save all documents
            for (auto doc : docs) {
                // Changed 'mustExecute' status may be triggered by saving external document
                if (!dmap[doc] && doc->mustExecute()) {
                    App::AutoTransaction trans("Recompute");
                    Command::doCommand(Command::Doc,"App.getDocument(\"%s\").recompute()",doc->getName());
                }

                Command::doCommand(Command::Doc,"App.getDocument(\"%s\").save()",doc->getName());
                auto gdoc = Application::Instance->getDocument(doc);
                if (gdoc)
                    gdoc->setModified(false);
            }

            // empty file name signals the intention to rebuild recent file
            // list without changing the list content.
            getMainWindow()->appendRecentFile(QString());
        }
        catch (const Base::FileException& e) {
            e.ReportException();
            return askIfSavingFailed(QString::fromUtf8(e.what()));
        }
        catch (const Base::Exception& e) {
            QMessageBox::critical(getMainWindow(), QObject::tr("Saving document failed"),
                QString::fromUtf8(e.what()));
            return false;
        }
        return true;
    }
    else {
        return saveAs();
    }
}

/// Save the document under a new file name
bool Document::saveAs()
{
    getMainWindow()->showMessage(QObject::tr("Save document under new filename..."));

    QString exe = qApp->applicationName();
    QString fn = FileDialog::getSaveFileName(getMainWindow(), QObject::tr("Save %1 Document").arg(exe),
        QString::fromUtf8(getDocument()->FileName.getValue()),
        QStringLiteral("%1 %2 (*.FCStd)").arg(exe).arg(QObject::tr("Document")));

    if (!fn.isEmpty()) {
        QFileInfo fi;
        fi.setFile(fn);

        const char * DocName = App::GetApplication().getDocumentName(getDocument());

        // save as new file name
        try {
            Gui::WaitCursor wc;
            std::string escapedstr = Base::Tools::escapeEncodeFilename(fn).toUtf8().constData();
            Command::doCommand(Command::Doc,"App.getDocument(\"%s\").saveAs(u\"%s\")"
                                           , DocName, escapedstr.c_str());
            // App::Document::saveAs() may modify the passed file name
            fi.setFile(QString::fromUtf8(d->_pcDocument->FileName.getValue()));
            setModified(false);

            // Some (Linux) system file dialog do not append default extension
            // name.  which will cause an invalid recent file entry. Use the
            // 'FileName' property instead.
            //
            // getMainWindow()->appendRecentFile(fi.filePath());
            getMainWindow()->appendRecentFile(QString::fromUtf8(
                        getDocument()->FileName.getValue()));
        }
        catch (const Base::FileException& e) {
            e.ReportException();
            return askIfSavingFailed(QString::fromUtf8(e.what()));
        }
        catch (const Base::Exception& e) {
            QMessageBox::critical(getMainWindow(), QObject::tr("Saving document failed"),
                QString::fromUtf8(e.what()));
        }
        return true;
    }
    else {
        getMainWindow()->showMessage(QObject::tr("Saving aborted"), 2000);
        return false;
    }
}

void Document::saveAll()
{
    std::vector<App::Document*> docs;
    try {
        docs = App::Document::getDependentDocuments(App::GetApplication().getDocuments(),true);
    }
    catch(Base::Exception &e) {
        e.ReportException();
        int ret = QMessageBox::critical(getMainWindow(), QObject::tr("Failed to save document"),
                QObject::tr("Documents contains cyclic dependencies. Do you still want to save them?"),
                QMessageBox::Yes,QMessageBox::No);
        if (ret != QMessageBox::Yes)
            return;
        docs = App::GetApplication().getDocuments();
    }

    std::map<App::Document *, bool> dmap;
    for(auto doc : docs) {
        if (doc->testStatus(App::Document::PartialDoc) || doc->testStatus(App::Document::TempDoc))
            continue;
        dmap[doc] = doc->mustExecute();
    }

    if (!checkCanonicalPath(dmap))
        return;

    for(auto doc : docs) {
        if (doc->testStatus(App::Document::PartialDoc) || doc->testStatus(App::Document::TempDoc))
            continue;
        auto gdoc = Application::Instance->getDocument(doc);
        if(!gdoc)
            continue;
        if(!doc->isSaved()) {
            if(!gdoc->saveAs())
                break;
        }
        Gui::WaitCursor wc;

        try {
            // Changed 'mustExecute' status may be triggered by saving external document
            if(!dmap[doc] && doc->mustExecute()) {
                App::AutoTransaction trans("Recompute");
                Command::doCommand(Command::Doc,"App.getDocument('%s').recompute()",doc->getName());
            }
            Command::doCommand(Command::Doc,"App.getDocument('%s').save()",doc->getName());
            gdoc->setModified(false);
        }
        catch (const Base::Exception& e) {
            QMessageBox::critical(getMainWindow(),
                    QObject::tr("Failed to save document") +
                        QStringLiteral(": %1").arg(QString::fromUtf8(doc->getName())),
                    QString::fromUtf8(e.what()));
            break;
        }
    }
    // empty file name signals the intention to rebuild recent file
    // list without changing the list content.
    getMainWindow()->appendRecentFile(QString());
}

/// Save a copy of the document under a new file name
bool Document::saveCopy()
{
    getMainWindow()->showMessage(QObject::tr("Save a copy of the document under new filename..."));

    QString exe = qApp->applicationName();
    QString fn = FileDialog::getSaveFileName(getMainWindow(), QObject::tr("Save %1 Document").arg(exe),
                                             QString::fromUtf8(getDocument()->FileName.getValue()),
                                             QObject::tr("%1 document (*.FCStd)").arg(exe));
    if (!fn.isEmpty()) {
        const char * DocName = App::GetApplication().getDocumentName(getDocument());

        // save as new file name
        Gui::WaitCursor wc;
        QString pyfn = Base::Tools::escapeEncodeFilename(fn);
        Command::doCommand(Command::Doc,"App.getDocument(\"%s\").saveCopy(\"%s\")"
                                       , DocName, (const char*)pyfn.toUtf8());

        return true;
    }
    else {
        getMainWindow()->showMessage(QObject::tr("Saving aborted"), 2000);
        return false;
    }
}

unsigned int Document::getMemSize () const
{
    unsigned int size = 0;

    // size of the view providers in the document
    std::map<const App::DocumentObject*,ViewProviderDocumentObject*>::const_iterator it;
    for (it = d->_ViewProviderMap.begin(); it != d->_ViewProviderMap.end(); ++it)
        size += it->second->getMemSize();
    return size;
}

/**
 * Adds a separate XML file to the projects file that contains information about the view providers.
 */
void Document::Save (Base::Writer &writer) const
{
    writer.addFile("GuiDocument.xml", this);

    d->thumb.setViewer(nullptr);
    for (auto v : d->baseViews) {
        if (auto view = Base::freecad_dynamic_cast<View3DInventor>(v)) {
            if (view->ThumbnailView.getValue()) {
                d->thumb.setViewer(view->getViewer());
                break;
            } else if (!d->thumb.getViewer()) {
                d->thumb.setViewer(view->getViewer());
            }
        }
    }
    d->thumb.setFileName(d->_pcDocument->FileName.getValue());
    d->thumb.Save(writer);
}

/**
 * Loads a separate XML file from the projects file with information about the view providers.
 */
void Document::Restore(Base::XMLReader &reader)
{
    reader.addFile("GuiDocument.xml",this);
    d->thumb.Restore(reader);

    // hide all elements to avoid to update the 3d view when loading data files
    // RestoreDocFile then restores the visibility status again
    std::map<const App::DocumentObject*,ViewProviderDocumentObject*>::iterator it;
    for (it = d->_ViewProviderMap.begin(); it != d->_ViewProviderMap.end(); ++it) {
        it->second->startRestoring();
        it->second->setStatus(Gui::isRestoring,true);
    }
}

void Document::readObject(Base::XMLReader &xmlReader) {
    std::string name = xmlReader.getAttribute("name");
    bool expanded = !d->_hasExpansion && !!xmlReader.getAttributeAsInteger("expanded","0");
    ViewProvider* pObj = getViewProviderByName(name.c_str());
    if (pObj) // check if this feature has been registered
        pObj->Restore(xmlReader);
    if (pObj && expanded) {
        Gui::ViewProviderDocumentObject* vp = static_cast<Gui::ViewProviderDocumentObject*>(pObj);
        this->signalExpandObject(*vp, TreeItemMode::ExpandItem,0,0);
    }
}

static const int FC_GUI_SCHEMA_VER = 1;
static const char *FC_XML_GUI_POSTFIX = ".Gui.xml";
static const char *FC_ATTR_SPLIT_XML = "Split";
static const char *FC_ATTR_TREE_EXPANSION = "HasExpansion";

/**
 * Restores the properties of the view providers.
 */
void Document::RestoreDocFile(Base::Reader &reader)
{
    Base::XMLReader xmlReader(reader);
    xmlReader.readElement("Document");
    xmlReader.DocumentSchema = xmlReader.getAttributeAsInteger("SchemaVersion","");
    if(!xmlReader.DocumentSchema)
        xmlReader.DocumentSchema = reader.getDocumentSchema();
    xmlReader.FileVersion = xmlReader.getAttributeAsInteger("FileVersion","");
    if(!xmlReader.FileVersion)
        xmlReader.FileVersion = reader.getFileVersion();

    if(boost::ends_with(reader.getFileName(),FC_XML_GUI_POSTFIX)) {
        xmlReader.readElement("ViewProvider");
        readObject(xmlReader);
        return;
    }

    bool split = !!xmlReader.getAttributeAsInteger(FC_ATTR_SPLIT_XML,"0");

    d->_hasExpansion = !!xmlReader.getAttributeAsInteger(FC_ATTR_TREE_EXPANSION,"0");
    if(d->_hasExpansion)
        TreeWidget::restoreDocumentItem(this, xmlReader);

    // At this stage all the document objects and their associated view providers exist.
    // Now we must restore the properties of the view providers only.
    //
    // SchemeVersion "1"
    if (xmlReader.DocumentSchema == 1) {

        if(!split) {
            // read the viewproviders itself
            xmlReader.readElement("ViewProviderData");
            int Cnt = xmlReader.getAttributeAsInteger("Count");
            for (int i=0; i<Cnt; i++) {
                int guard;
                xmlReader.readElement("ViewProvider",&guard);
                readObject(xmlReader);
                xmlReader.readEndElement("ViewProvider",&guard);
            }
            xmlReader.readEndElement("ViewProviderData");
        } else {
            for(const auto &v : d->_ViewProviderMap)
                xmlReader.addFile(std::string(v.first->getNameInDocument())+FC_XML_GUI_POSTFIX,this);
        }

        // read camera settings
        xmlReader.readElement("Camera");

        int cameraExtra = xmlReader.getAttributeAsInteger("extra", "0");
        int cameraBinding = xmlReader.getAttributeAsInteger("binding", "0");
        int cameraId = xmlReader.getAttributeAsInteger("id", "0");
        int view3dCount = xmlReader.getAttributeAsInteger("view3d", "0");

        cameraSettings.clear();
        if(xmlReader.hasAttribute("settings"))
            saveCameraSettings(xmlReader.getAttribute("settings"));
        else
            saveCameraSettings(xmlReader.readCharacters().c_str());

        d->_savedViews.clear();
        d->_savedViews.emplace_back(cameraId, cameraBinding, std::string(cameraSettings));

        if(cameraExtra) {
            for(int i=0; i<cameraExtra; ++i) {
                xmlReader.readElement("CameraExtra");
                int id = xmlReader.getAttributeAsInteger("id");
                int binding = xmlReader.getAttributeAsInteger("binding", "0");
                std::string settings;
                saveCameraSettings(xmlReader.readCharacters().c_str(),&settings);
                d->_savedViews.emplace_back(id, binding, std::move(settings));
            }
        }

        d->_view3DContents.clear();
        for (int i=0; i<view3dCount; ++i) {
            xmlReader.readElement("View3D");
            int id = xmlReader.getAttributeAsInteger("id");
            d->_view3DContents[id] = xmlReader.readCharacters();
        }
    }

    xmlReader.readEndElement("Document");

    // In the file GuiDocument.xml new data files might be added
    if (!xmlReader.getFilenames().empty())
        xmlReader.readFiles();

    // reset modified flag
    setModified(false);
}

void Document::slotStartRestoreDocument(const App::Document& doc)
{
    if (d->_pcDocument != &doc)
        return;
    // disable this signal while loading a document
    d->connectActObjectBlocker.block();
}

void Document::slotFinishRestoreObject(const App::DocumentObject &obj) {
    auto vpd = Base::freecad_dynamic_cast<ViewProviderDocumentObject>(getViewProvider(&obj));
    if(vpd) {
        vpd->setStatus(Gui::isRestoring,false);
        vpd->finishRestoring();
        if(!vpd->canAddToSceneGraph())
            toggleInSceneGraph(vpd);
        else if (vpd->Visibility.getValue())
            vpd->setModeSwitch();
    }
}

void Document::slotFinishRestoreDocument(const App::Document& doc)
{
    if (d->_pcDocument != &doc)
        return;

    slotFinishImportObjects(doc.getObjects());

    d->connectActObjectBlocker.unblock();
    App::DocumentObject* act = doc.getActiveObject();
    if (act) {
        ViewProvider* viewProvider = getViewProvider(act);
        if (viewProvider && viewProvider->isDerivedFrom(ViewProviderDocumentObject::getClassTypeId())) {
            signalActivatedObject(*(static_cast<ViewProviderDocumentObject*>(viewProvider)));
        }
    }

    auto views = getMDIViewsOfType(View3DInventor::getClassTypeId());
    if(views.size()) {
        while(views.size() < d->_savedViews.size())
            views.push_back(createView(View3DInventor::getClassTypeId()));

        size_t i=0;
        std::map<int, std::vector<std::pair<std::string,std::string>>> onTopObjs;
        if (auto prop = d->getOnTopProperty(getDocument(), false)) {
            for (const auto &path : prop->getValues()) {
                std::istringstream iss(path);
                int id = -1;
                std::string name, subname;
                char c;
                if (iss >> id >> c) {
                    if (std::getline(iss, name, '.') && std::getline(iss, subname))
                        onTopObjs[id].emplace_back(std::move(name), std::move(subname));
                }
            }
        }

        std::map<int,View3DInventor*> viewMap;
        for(auto v : views) {
            if(i == d->_savedViews.size())
                break;
            auto &info = d->_savedViews[i++];
            auto view = static_cast<View3DInventor*>(v);
            const char *ppReturn = 0;
            view->onMsg(info.settings.c_str(), &ppReturn);
            viewMap[info.id] = view;
            auto it = onTopObjs.find(info.id);
            if (it != onTopObjs.end()) {
                const char *docName = getDocument()->getName();
                for (auto &v : it->second) {
                    view->getViewer()->checkGroupOnTop(SelectionChanges(
                                SelectionChanges::AddSelection,
                                docName, v.first.c_str(), v.second.c_str()), true);
                }
            }
            auto iter = d->_view3DContents.find(info.id);
            if (iter != d->_view3DContents.end()) {
                try {
                    std::istringstream in(iter->second);
                    Base::XMLReader reader("<memory>", in);
                    view->Restore(reader);
                } catch (Base::Exception &e) {
                    e.ReportException();
                }
            }
        }
        i=0;
        for(auto v : views) {
            if(i == d->_savedViews.size())
                break;
            auto &info = d->_savedViews[i++];
            auto view = static_cast<View3DInventor*>(v);
            auto it = viewMap.find(info.binding);
            if(it != viewMap.end())
                view->bindCamera(it->second->getCamera());
        }
    }

    // reset modified flag
    setModified(doc.testStatus(App::Document::LinkStampChanged));
}

void Document::slotShowHidden(const App::Document& doc)
{
    if (d->_pcDocument != &doc)
        return;

    Application::Instance->signalShowHidden(*this);
}

void Document::writeObject(Base::Writer &writer, 
        const App::DocumentObject *doc, const ViewProvider *obj) const
{
    writer.Stream() << writer.ind() << "<ViewProvider name=\"" 
        << doc->getExportName() << "\" expanded=\"" 
        << (doc->testStatus(App::Expand) ? 1:0) << "\"";

    if (obj->canSaveExtension())
        writer.Stream() << " Extensions=\"True\"";

    writer.Stream() << ">\n";
    obj->Save(writer);
    writer.Stream() << writer.ind() << "</ViewProvider>\n";
}

/**
 * Saves the properties of the view providers.
 */
void Document::SaveDocFile (Base::Writer &writer) const
{
    writer.Stream() << "<?xml version='1.0' encoding='utf-8'?>\n"
                    << "<!--\n"
                    << " FreeCAD Document, see http://www.freecad.org for more information...\n"
                    << "-->\n";

    if(boost::ends_with(writer.getCurrentFileName(),FC_XML_GUI_POSTFIX)) {
        const std::string &name = writer.getCurrentFileName();
        static const std::size_t plen = std::strlen(FC_XML_GUI_POSTFIX);
        std::string objName = name.substr(0,name.size()-plen);
        auto obj = getDocument()->getObject(objName.c_str());
        auto it = d->_ViewProviderMap.find(obj);
        if(it == d->_ViewProviderMap.end())
            FC_ERR("View object not found: " << getDocument()->getName() << '#' << objName);
        else {
            writer.Stream() << "<!-- FreeCAD ViewProvider -->\n"
                << "<Document SchemaVersion=\"" << FC_GUI_SCHEMA_VER 
                << "\" FileVersion=\"" << writer.getFileVersion() 
                << "\">\n";
            writeObject(writer,it->first,it->second);
            writer.Stream() << "</Document>\n";
        }
        return;
    }

    writer.Stream() << "<!--\n"
                    << " FreeCAD Document, see http://www.freecadweb.org for more information..."
                    << "\n-->\n";

    writer.Stream() << "<Document SchemaVersion=\"" << FC_GUI_SCHEMA_VER 
        << "\" FileVersion=\"" << writer.getFileVersion() << "\" "
        << FC_ATTR_SPLIT_XML << "=\"" << (writer.isSplitXML()?1:0) << "\"";

    if (!TreeWidget::saveDocumentItem(this, writer, FC_ATTR_TREE_EXPANSION))
        writer.Stream() << ">\n";

    if(writer.isSplitXML()) {
        for(const auto &v : d->_ViewProviderMap)
            writer.addFile(std::string(v.first->getNameInDocument())+FC_XML_GUI_POSTFIX,this);
    } else {
        writer.incInd(); 

        std::map<const App::DocumentObject*,ViewProviderDocumentObject*>::const_iterator it;

        // writing the view provider names itself
        writer.Stream() << writer.ind() << "<ViewProviderData Count=\""
                        << d->_ViewProviderMap.size() <<"\">\n";

        writer.incInd(); // indentation for 'ViewProvider name'
        for(it = d->_ViewProviderMap.begin(); it != d->_ViewProviderMap.end(); ++it)
            writeObject(writer,it->first, it->second);
        writer.decInd(); // indentation for 'ViewProvider name'
        writer.Stream() << writer.ind() << "</ViewProviderData>\n";
        writer.decInd();  // indentation for 'ViewProviderData Count'
    }

    writer.incInd(); // indentation for camera settings

    // save camera settings
    std::list<MDIView*> mdi = getMDIViews();
    std::vector<CameraInfo> cameraInfo;
    std::vector<View3DInventor*> view3Ds;
    bool first = true;
    for (const auto & v : mdi) {
        if (v->onHasMsg("GetCamera")) {
            const char* ppReturn=0;
            v->onMsg("GetCamera",&ppReturn);

            std::string settings;
            if(!saveCameraSettings(ppReturn, &settings))
                continue;
            if(first) {
                first = false;
                cameraSettings = settings;
            }

            auto view = Base::freecad_dynamic_cast<View3DInventor>(v);
            if(!view)
                continue;
            auto binding = view->boundView();
            cameraInfo.emplace_back(view->getID(),
                    binding?binding->getID():0, std::move(settings));
            view3Ds.push_back(view);
        }
    }

    writer.Stream() << writer.ind() << "<Camera";
    if(cameraInfo.size())
        writer.Stream() << " extra=\"" << cameraInfo.size()-1 << "\" id=\""
            << cameraInfo[0].id << "\" binding=\"" << cameraInfo[0].binding << "\""
            << " view3d=\"" << view3Ds.size() << "\"";
    if(writer.getFileVersion() > 1) {
        writer.Stream() << ">\n";
        writer.beginCharStream() << '\n' << getCameraSettings();
        writer.endCharStream() << '\n' << writer.ind() << "</Camera>\n";
    } else {
        writer.Stream() << " settings=\"" 
            << encodeAttribute(getCameraSettings()) << "\"/>\n";
    }
    if(cameraInfo.size()>1) {
        for(size_t i=1; i<cameraInfo.size(); ++i) {
            auto &info = cameraInfo[i];
            writer.Stream() << writer.ind() << "<CameraExtra id=\""
                << info.id << "\" binding=\"" << info.binding << "\">\n";
            writer.beginCharStream() << '\n' << getCameraSettings(&info.settings);
            writer.endCharStream() << '\n' << writer.ind() << "</CameraExtra>\n";
        }
    }
    d->_savedViews = std::move(cameraInfo);

    Base::StringWriter stringWriter;
    for (auto view : view3Ds) {
        writer.Stream() << writer.ind() << "<View3D id=\"" << view->getID() << "\">";
        stringWriter.clear();
        view->Save(stringWriter);
        writer.beginCharStream() << '\n' << stringWriter.getString();
        writer.endCharStream() << '\n' << writer.ind() << "</View3D>\n";
    }

    writer.decInd(); // indentation for camera settings

    writer.Stream() << "</Document>\n";
}

void Document::exportObjects(const std::vector<App::DocumentObject*>& obj, Base::Writer& writer)
{
    writer.Stream() << "<?xml version='1.0' encoding='utf-8'?>\n";
    writer.Stream() << "<Document SchemaVersion=\"" << FC_GUI_SCHEMA_VER << "\">\n";

    std::map<const App::DocumentObject*,ViewProvider*> views;
    for (const auto & it : obj) {
        Document* doc = Application::Instance->getDocument(it->getDocument());
        if (doc) {
            ViewProvider* vp = doc->getViewProvider(it);
            if (vp) views[it] = vp;
        }
    }

    // writing the view provider names itself
    writer.incInd(); // indentation for 'ViewProviderData Count'
    writer.Stream() << writer.ind() << "<ViewProviderData Count=\""
                    << views.size() <<"\">\n";

    writer.incInd(); // indentation for 'ViewProvider name'
    std::map<const App::DocumentObject*,ViewProvider*>::const_iterator jt;
    for (jt = views.begin(); jt != views.end(); ++jt)
        writeObject(writer,jt->first, jt->second);

    writer.decInd(); // indentation for 'ViewProvider name'
    writer.Stream() << writer.ind() << "</ViewProviderData>\n";
    writer.decInd();  // indentation for 'ViewProviderData Count'
    writer.incInd(); // indentation for camera settings
    writer.Stream() << writer.ind() << "<Camera settings=\"\"/>\n";
    writer.decInd(); // indentation for camera settings
    writer.Stream() << "</Document>\n";
}

void Document::importObjects(const std::vector<App::DocumentObject*>& obj, Base::Reader& reader,
                             const std::map<std::string, std::string>& nameMapping)
{
    // We must create an XML parser to read from the input stream
    Base::XMLReader xmlReader(reader);
    xmlReader.readElement("Document");
    long scheme = xmlReader.getAttributeAsInteger("SchemaVersion");

    // At this stage all the document objects and their associated view providers exist.
    // Now we must restore the properties of the view providers only.
    //
    // SchemeVersion "1"
    if (scheme == 1) {
        // read the viewproviders itself
        xmlReader.readElement("ViewProviderData");
        int Cnt = xmlReader.getAttributeAsInteger("Count");
        auto it = obj.begin();
        for (int i=0;i<Cnt&&it!=obj.end();++i,++it) {
            // The stored name usually doesn't match with the current name anymore
            // thus we try to match by type. This should work because the order of
            // objects should not have changed
            xmlReader.readElement("ViewProvider");
            std::string name = xmlReader.getAttribute("name");
            auto jt = nameMapping.find(name);
            if (jt != nameMapping.end())
                name = jt->second;
            bool expanded = false;
            if (xmlReader.hasAttribute("expanded")) {
                const char* attr = xmlReader.getAttribute("expanded");
                if (strcmp(attr,"1") == 0) {
                    expanded = true;
                }
            }
            Gui::ViewProvider* pObj = this->getViewProviderByName(name.c_str());
            if (pObj) {
                pObj->setStatus(Gui::isRestoring,true);
                auto vpd = Base::freecad_dynamic_cast<ViewProviderDocumentObject>(pObj);
                if(vpd) vpd->startRestoring();
                pObj->Restore(xmlReader);
                if (expanded && vpd)
                    this->signalExpandObject(*vpd, TreeItemMode::ExpandItem,0,0);
            }
            xmlReader.readEndElement("ViewProvider");
            if (it == obj.end())
                break;
        }
        xmlReader.readEndElement("ViewProviderData");
    }

    xmlReader.readEndElement("Document");

    // In the file GuiDocument.xml new data files might be added
    if (!xmlReader.getFilenames().empty())
        xmlReader.readFiles();
}

void Document::slotFinishImportObjects(const std::vector<App::DocumentObject*> &objs) {
    // Refresh ViewProviderDocumentObject isShowable status. Since it is
    // calculated based on parent status, so must call it reverse dependency
    // order.
    auto sorted = App::Document::getDependencyList(objs,App::Document::DepSort);
    for (auto rit=sorted.rbegin(); rit!=sorted.rend(); ++rit) {
        auto obj = *rit;
        if(obj->getDocument() != d->_pcDocument)
            continue;
        auto vpd = Base::freecad_dynamic_cast<ViewProviderDocumentObject>(getViewProvider(*rit));
        if(vpd) {
            vpd->isShowable(true);
            vpd->updateChildren(false);
        }
    }
}

void Document::addRootObjectsToGroup(const std::vector<App::DocumentObject*>& objs, App::DocumentObject* grp)
{
    if (!grp)
        return;
    auto grpExt = grp->getExtensionByType<App::GroupExtension>(true);
    if (!grpExt)
        return;

    auto geoGrp = grp->getExtensionByType<App::GeoFeatureGroupExtension>(true);
    if (!geoGrp) {
        if (auto geoGrpObj = App::GeoFeatureGroupExtension::getGroupOfObject(grp))
            geoGrp = geoGrpObj->getExtensionByType<App::GeoFeatureGroupExtension>(true);
    }

    std::vector<App::DocumentObject *> geoNewObjects;
    std::vector<App::DocumentObject *> grpNewObjects;
    std::set<App::DocumentObject *> objSet;

    for (auto obj : objs) {
        if (!obj || !objSet.insert(obj).second)
            continue;
        if (geoGrp) {
            if (!App::GeoFeatureGroupExtension::getGroupOfObject(obj))
                geoNewObjects.push_back(obj);
        }
        if (grpExt == geoGrp)
            continue;

        if (auto vp = Base::freecad_dynamic_cast<ViewProviderDocumentObject>(
                    Application::Instance->getViewProvider(obj)))
        {
            if (vp->claimedBy().empty())
                grpNewObjects.push_back(obj);
        }
    }

    if (geoGrp)
        geoGrp->addObjects(geoNewObjects);
    grpExt->addObjects(grpNewObjects);
}

MDIView *Document::createView(const Base::Type& typeId)
{
    if (!typeId.isDerivedFrom(MDIView::getClassTypeId()))
        return nullptr;

    std::list<MDIView*> theViews = this->getMDIViewsOfType(typeId);
    if (typeId == View3DInventor::getClassTypeId()) {

        QtGLWidget* shareWidget = nullptr;
        // VBO rendering doesn't work correctly when we don't share the OpenGL widgets
        if (!theViews.empty()) {
            auto firstView = static_cast<View3DInventor*>(theViews.front());
            shareWidget = qobject_cast<QtGLWidget*>(firstView->getViewer()->getGLWidget());

            const char *ppReturn = nullptr;
            firstView->onMsg("GetCamera",&ppReturn);
            saveCameraSettings(ppReturn);
        }

        auto view3D = new View3DInventor(this, getMainWindow(), shareWidget);

        // Views can now have independent draw styles (i.e. override modes)
        //
        // if (!theViews.empty()) {
        //     auto firstView = static_cast<View3DInventor*>(theViews.front());
        //     std::string overrideMode = firstView->getViewer()->getOverrideMode();
        //     view3D->getViewer()->setOverrideMode(overrideMode);
        // }

        std::map<const App::DocumentObject*,ViewProviderDocumentObject*>::const_iterator It1;
        for (It1=d->_ViewProviderMap.begin();It1!=d->_ViewProviderMap.end();++It1) {
            view3D->getViewer()->addViewProvider(It1->second);
        }
        std::map<std::string,ViewProvider*>::const_iterator It2;
        for (It2=d->_ViewProviderMapAnnotation.begin();It2!=d->_ViewProviderMapAnnotation.end();++It2) {
            view3D->getViewer()->addViewProvider(It2->second);
        }

        const char* name = getDocument()->Label.getValue();
        QString title = QStringLiteral("%1 : %2[*]")
            .arg(QString::fromUtf8(name)).arg(d->_iWinCount++);

        view3D->setWindowTitle(title);
        view3D->setWindowModified(this->isModified());
        view3D->resize(400, 300);

        if (!cameraSettings.empty()) {
            const char *ppReturn = nullptr;
            view3D->onMsg(cameraSettings.c_str(),&ppReturn);
        }
        else if (ViewParams::getDefaultDrawStyle() != 0) {
            if (auto mode = drawStyleNameFromIndex(ViewParams::getDefaultDrawStyle()))
                view3D->getViewer()->setOverrideMode(mode);
        }

        getMainWindow()->addWindow(view3D);
        setModified(false);
        return view3D;
    }
    return nullptr;
}

Gui::MDIView* Document::cloneView(Gui::MDIView* oldview)
{
    if (!oldview)
        return nullptr;

    if (oldview->is<View3DInventor>()) {
        auto view3D = new View3DInventor(this, getMainWindow());

        auto firstView = static_cast<View3DInventor*>(oldview);
        std::string overrideMode = firstView->getViewer()->getOverrideMode();
        view3D->getViewer()->setOverrideMode(overrideMode);

        view3D->getViewer()->setAxisCross(firstView->getViewer()->hasAxisCross());

        std::map<const App::DocumentObject*,ViewProviderDocumentObject*>::const_iterator It1;
        for (It1=d->_ViewProviderMap.begin();It1!=d->_ViewProviderMap.end();++It1) {
            view3D->getViewer()->addViewProvider(It1->second);
        }
        std::map<std::string,ViewProvider*>::const_iterator It2;
        for (It2=d->_ViewProviderMapAnnotation.begin();It2!=d->_ViewProviderMapAnnotation.end();++It2) {
            view3D->getViewer()->addViewProvider(It2->second);
        }

        view3D->setWindowTitle(oldview->windowTitle());
        view3D->setWindowModified(oldview->isWindowModified());
        view3D->setWindowIcon(oldview->windowIcon());
        view3D->resize(oldview->size());

        // FIXME: Add parameter to define behaviour by the calling instance
        // View provider editing
        if (d->_editViewProvider) {
            firstView->getViewer()->resetEditingViewProvider();
            view3D->getViewer()->setEditingViewProvider(d->_editViewProvider, d->_editMode);
        }

        return view3D;
    }

    return nullptr;
}

const char *Document::getCameraSettings(const std::string *settings) const {
    if(!settings)
        settings = &cameraSettings;
    return settings->size()>10?settings->c_str()+10:settings->c_str();
}

bool Document::saveCameraSettings(const char *settings, std::string *dst) const {
    if(!settings)
        return false;

    if(!dst)
        dst = &cameraSettings;

    // skip starting comment lines
    bool skipping = false;
    char c = *settings;
    for(;c;c=*(++settings)) {
        if(skipping) {
            if(c == '\n')
                skipping = false;
        } else if(c == '#')
            skipping = true;
        else if(!std::isspace(static_cast<unsigned char>(c)))
            break;
    }

    if(!c)
        return false;

    *dst = std::string("SetCamera ") + settings;
    return true;
}

void Document::attachView(Gui::BaseView* pcView, bool bPassiv)
{
    if (!bPassiv)
        d->baseViews.push_back(pcView);
    else
        d->passiveViews.push_back(pcView);
    signalAttachView(*pcView, bPassiv);
    Application::Instance->signalAttachView(*pcView, bPassiv);
}

void Document::detachView(Gui::BaseView* pcView, bool bPassiv)
{
    if (bPassiv) {
        if (find(d->passiveViews.begin(),d->passiveViews.end(),pcView)
            != d->passiveViews.end())
        {
            signalDetachView(*pcView, true);
            Application::Instance->signalDetachView(*pcView, true);
            d->passiveViews.remove(pcView);
        }
    }
    else {
        if (find(d->baseViews.begin(),d->baseViews.end(),pcView)
            != d->baseViews.end())
        {
            signalDetachView(*pcView, false);
            Application::Instance->signalDetachView(*pcView, false);
            d->baseViews.remove(pcView);
        }

        // last view?
        if (d->baseViews.empty()) {
            // decouple a passive view
            std::list<Gui::BaseView*>::iterator it = d->passiveViews.begin();
            while (it != d->passiveViews.end()) {
                (*it)->setDocument(nullptr);
                it = d->passiveViews.begin();
            }

            // is already closing the document, and is not linked by other documents
            if (!d->_isClosing &&
                App::PropertyXLink::getDocumentInList(getDocument()).empty())
            {
                d->_pcAppWnd->onLastWindowClosed(this);
            }
        }
    }
}

void Document::onUpdate()
{
#ifdef FC_LOGUPDATECHAIN
    Base::Console().Log("Acti: Gui::Document::onUpdate()");
#endif

    std::list<Gui::BaseView*>::iterator it;

    for (it = d->baseViews.begin();it != d->baseViews.end();++it) {
        (*it)->onUpdate();
    }

    for (it = d->passiveViews.begin();it != d->passiveViews.end();++it) {
        (*it)->onUpdate();
    }
}

void Document::onRelabel()
{
#ifdef FC_LOGUPDATECHAIN
    Base::Console().Log("Acti: Gui::Document::onRelabel()");
#endif

    std::list<Gui::BaseView*>::iterator it;

    for (it = d->baseViews.begin();it != d->baseViews.end();++it) {
        (*it)->onRelabel(this);
    }

    for (it = d->passiveViews.begin();it != d->passiveViews.end();++it) {
        (*it)->onRelabel(this);
    }

    d->connectChangeDocumentBlocker.unblock();
}

bool Document::isLastView()
{
    if (d->baseViews.size() <= 1)
        return true;
    return false;
}

/**
 *  This method checks if the document can be closed. It checks on
 *  the save state of the document and is able to abort the closing.
 */
bool Document::canClose (bool checkModify, bool checkLink)
{
    if (d->_isClosing)
        return true;
    if (!getDocument()->isClosable()) {
        QMessageBox::warning(getActiveView(),
            QObject::tr("Document not closable"),
            QObject::tr("The document is not closable for the moment."));
        return false;
    }
    //else if (!Gui::Control().isAllowedAlterDocument()) {
    //    std::string name = Gui::Control().activeDialog()->getDocumentName();
    //    if (name == this->getDocument()->getName()) {
    //        QMessageBox::warning(getActiveView(),
    //            QObject::tr("Document not closable"),
    //            QObject::tr("The document is in editing mode and thus cannot be closed for the moment.\n"
    //                        "You either have to finish or cancel the editing in the task panel."));
    //        Gui::TaskView::TaskDialog* dlg = Gui::Control().activeDialog();
    //        if (dlg) Gui::Control().showDialog(dlg);
    //        return false;
    //    }
    //}

    if (checkLink && !App::PropertyXLink::getDocumentInList(getDocument()).empty())
        return true;

    if (getDocument()->testStatus(App::Document::TempDoc))
        return true;

    if (getDocument()->testStatus(App::Document::TempDoc))
        return true;

    bool ok = true;
    if (checkModify && isModified() && !getDocument()->testStatus(App::Document::PartialDoc)) {
        const char *docName = getDocument()->Label.getValue();
        int res = getMainWindow()->confirmSave(docName, getActiveView());
        switch (res)
        {
        case MainWindow::ConfirmSaveResult::Cancel:
            ok = false;
            break;
        case MainWindow::ConfirmSaveResult::SaveAll:
        case MainWindow::ConfirmSaveResult::Save:
            ok = save();
            if (!ok) {
                int ret = QMessageBox::question(
                    getActiveView(),
                    QObject::tr("Document not saved"),
                    QObject::tr("The document%1 could not be saved. Do you want to cancel closing it?")
                    .arg(docName?(QString::fromUtf8(" ")+QString::fromUtf8(docName)):QString()),
                    QMessageBox::Discard | QMessageBox::Cancel,
                    QMessageBox::Discard);
                if (ret == QMessageBox::Discard)
                    ok = true;
            }
            break;
        case MainWindow::ConfirmSaveResult::DiscardAll:
        case MainWindow::ConfirmSaveResult::Discard:
            ok = true;
            break;
        }
    }

    if (ok) {
        // If a task dialog is open that doesn't allow other commands to modify
        // the document it must be closed by resetting the edit mode of the
        // corresponding view provider.
        if (!Gui::Control().isAllowedAlterDocument()) {
            std::string name = Gui::Control().activeDialog()->getDocumentName();
            if (name == this->getDocument()->getName()) {
                // getInEdit() only checks if the currently active MDI view is
                // a 3D view and that it is in edit mode. However, when closing a
                // document then the edit mode must be reset independent of the
                // active view.
                if (d->_editViewProvider)
                    this->_resetEdit();
            }
        }
    }

    return ok;
}

const std::list<BaseView*> &Document::getViews() const {
    return d->baseViews;
}

std::list<MDIView*> Document::getMDIViews() const
{
    std::list<MDIView*> views;
    for (std::list<BaseView*>::const_iterator it = d->baseViews.begin();
         it != d->baseViews.end(); ++it) {
        auto view = dynamic_cast<MDIView*>(*it);
        if (view)
            views.push_back(view);
    }

    return views;
}

BaseView *Document::getViewByID(int id) const
{
    for (auto view : d->baseViews) {
        if (view->getID() == id)
            return view;
    }
    return nullptr;
}

std::list<MDIView*> Document::getMDIViewsOfType(const Base::Type& typeId) const
{
    std::list<MDIView*> views;
    for (std::list<BaseView*>::const_iterator it = d->baseViews.begin();
         it != d->baseViews.end(); ++it) {
        auto view = dynamic_cast<MDIView*>(*it);
        if (view && view->isDerivedFrom(typeId))
            views.push_back(view);
    }

    return views;
}

/// send messages to the active view
bool Document::sendMsgToViews(const char* pMsg)
{
    std::list<Gui::BaseView*>::iterator it;
    const char** pReturnIgnore=nullptr;

    for (it = d->baseViews.begin();it != d->baseViews.end();++it) {
        if ((*it)->onMsg(pMsg,pReturnIgnore)) {
            return true;
        }
    }

    for (it = d->passiveViews.begin();it != d->passiveViews.end();++it) {
        if ((*it)->onMsg(pMsg,pReturnIgnore)) {
            return true;
        }
    }

    return false;
}

bool Document::sendMsgToFirstView(const Base::Type& typeId, const char* pMsg, const char** ppReturn)
{
    // first try the active view
    Gui::MDIView* view = getActiveView();
    if (view && view->isDerivedFrom(typeId)) {
        if (view->onMsg(pMsg, ppReturn))
            return true;
    }

    // now try the other views
    std::list<Gui::MDIView*> views = getMDIViewsOfType(typeId);
    for (const auto & it : views) {
        if ((it != view) && it->onMsg(pMsg, ppReturn)) {
            return true;
        }
    }

    return false;
}

/// Getter for the active view
MDIView* Document::getActiveView() const
{
    // get the main window's active view
    MDIView* active = getMainWindow()->activeWindow();

    // get all MDI views of the document
    std::list<MDIView*> mdis = getMDIViews();

    // check whether the active view is part of this document
    bool ok=false;
    for (const auto & mdi : mdis) {
        if (mdi == active) {
            ok = true;
            break;
        }
    }

    if (ok)
        return active;

    // the active view is not part of this document, just use the last view
    const auto &windows = Gui::getMainWindow()->windows();
    for(auto rit=mdis.rbegin();rit!=mdis.rend();++rit) {
        // Some view is removed from window list for some reason, e.g. TechDraw
        // hidden page has view but not in the list. By right, the view will
        // self delete, but not the case for TechDraw, especially during
        // document restore.
        if(windows.contains(*rit) || (*rit)->isDerivedFrom(View3DInventor::getClassTypeId()))
            return *rit;
    }
    return nullptr;
}

MDIView *Document::setActiveView(ViewProviderDocumentObject *vp, Base::Type typeId)
{
    MDIView *view = nullptr;
    if (!vp) {
        view = getActiveView();
    }
    else {
        view = vp->getMDIView();
        if (!view) {
            auto obj = vp->getObject();
            if (!obj) {
                view = getActiveView();
            }
            else {
                auto linked = obj->getLinkedObject(true);
                if (linked!=obj) {
                    auto vpLinked = dynamic_cast<ViewProviderDocumentObject*>(
                                Application::Instance->getViewProvider(linked));
                    if (vpLinked)
                        view = vpLinked->getMDIView();
                }

                if (!view && typeId.isBad()) {
                    MDIView* active = getActiveView();
                    if (active && active->containsViewProvider(vp))
                        view = active;
                    else
                        typeId = View3DInventor::getClassTypeId();
                }
            }
        }
    }

    if (!view || (!typeId.isBad() && !view->isDerivedFrom(typeId))) {
        view = nullptr;
        for (auto *v : d->baseViews) {
            if (v->isDerivedFrom(MDIView::getClassTypeId()) &&
               (typeId.isBad() || v->isDerivedFrom(typeId))) {
                view = static_cast<MDIView*>(v);
                break;
            }
        }
    }

    if (!view && !typeId.isBad())
        view = createView(typeId);

    if (view)
        getMainWindow()->setActiveWindow(view);

    return view;
}

/**
 * @brief Document::setActiveWindow
 * If this document is active and the view is part of it then it will be
 * activated. If the document is not active of the view is already active
 * nothing is done.
 * @param view
 */
void Document::setActiveWindow(Gui::MDIView* view)
{
    if (!view || view->getGuiDocument() != this)
        return;

    // get the main window's active view
    MDIView* active = getMainWindow()->activeWindow();

    // view is already active
    if (active == view)
        return;

    // this document is not active
    if (Application::Instance->activeDocument() != this)
        return;

    getMainWindow()->setActiveWindow(view);
}

Gui::MDIView* Document::getViewOfNode(SoNode* node) const
{
    for(auto v : getViews()) {
        auto view = Base::freecad_dynamic_cast<View3DInventor>(v);
        if (view && view->getViewer()->searchNode(node))
            return view;
    }

    return nullptr;
}

Gui::MDIView* Document::getViewOfViewProvider(const Gui::ViewProvider* vp) const
{
    return getViewOfNode(vp->getRoot());
}

Gui::MDIView* Document::getEditingViewOfViewProvider(Gui::ViewProvider* vp) const
{
    (void)vp;
    return getEditingView();
}

Gui::MDIView* Document::getEditingView() const
{
    for (auto v : getViews()) {
        auto view = Base::freecad_dynamic_cast<View3DInventor>(v);
        // there is only one 3d view which is in edit mode
        if (view && view->getViewer()->isEditingViewProvider())
            return view;
    }

    return nullptr;
}

//--------------------------------------------------------------------------
// UNDO REDO transaction handling
//--------------------------------------------------------------------------
/** Open a new Undo transaction on the active document
 *  This method opens a new UNDO transaction on the active document. This transaction
 *  will later appear in the UNDO/REDO dialog with the name of the command. If the user
 *  recall the transaction everything changed on the document between OpenCommand() and
 *  CommitCommand will be undone (or redone). You can use an alternative name for the
 *  operation default is the command name.
 *  @see CommitCommand(),AbortCommand()
 */
void Document::openCommand(const char* sName)
{
    getDocument()->openTransaction(sName);
}

void Document::commitCommand()
{
    getDocument()->commitTransaction();
}

void Document::abortCommand()
{
    getDocument()->abortTransaction();
}

bool Document::hasPendingCommand() const
{
    return getDocument()->hasPendingTransaction();
}

/// Get a string vector with the 'Undo' actions
std::vector<std::string> Document::getUndoVector() const
{
    return getDocument()->getAvailableUndoNames();
}

/// Get a string vector with the 'Redo' actions
std::vector<std::string> Document::getRedoVector() const
{
    return getDocument()->getAvailableRedoNames();
}

bool Document::checkTransactionID(bool undo, int iSteps) {
    if(!iSteps)
        return false;

    std::vector<int> ids;
    for (int i=0;i<iSteps;i++) {
        int id = getDocument()->getTransactionID(undo,i);
        if(!id) break;
        ids.push_back(id);
    }
    std::set<App::Document*> prompts;
    std::map<App::Document*,int> dmap;
    for(auto doc : App::GetApplication().getDocuments()) {
        if(doc == getDocument())
            continue;
        for(auto id : ids) {
            int steps = undo?doc->getAvailableUndos(id):doc->getAvailableRedos(id);
            if(!steps) continue;
            int &currentSteps = dmap[doc];
            if(currentSteps+1 != steps)
                prompts.insert(doc);
            if(currentSteps < steps)
                currentSteps = steps;
        }
    }
    if(!prompts.empty()) {
        std::ostringstream str;
        int i=0;
        for(auto doc : prompts) {
            if(i++==5) {
                str << "...\n";
                break;
            }
            str << "    " << doc->getName() << "\n";
        }
        int ret = QMessageBox::warning(getMainWindow(),
                    undo?QObject::tr("Undo"):QObject::tr("Redo"),
                    QStringLiteral("%1,\n%2%3")
                        .arg(QObject::tr(
                            "There are grouped transactions in the following documents with "
                            "other preceding transactions"))
                        .arg(QString::fromUtf8(str.str().c_str()))
                        .arg(QObject::tr("Choose 'Yes' to roll back all preceding transactions.\n"
                                         "Choose 'No' to roll back in the active document only.\n"
                                         "Choose 'Abort' to abort")),
                    QMessageBox::Yes|QMessageBox::No|QMessageBox::Abort, QMessageBox::Yes);
        if (ret == QMessageBox::Abort)
            return false;
        if (ret == QMessageBox::No)
            return true;
    }
    for(auto &v : dmap) {
        for(int i=0;i<v.second;++i) {
            if(undo)
                v.first->undo();
            else
                v.first->redo();
        }
    }
    return true;
}

bool Document::isPerformingTransaction() const {
    return d->_isTransacting;
}

/// Will UNDO one or more steps
void Document::undo(int iSteps)
{
    Base::FlagToggler<> flag(d->_isTransacting);

    Gui::Selection().clearCompleteSelection();

    {
        App::TransactionGuard guard(App::TransactionGuard::Undo);

        if(!checkTransactionID(true,iSteps))
            return;

        for (int i=0;i<iSteps;i++) {
            getDocument()->undo();
        }
    }
}

/// Will REDO one or more steps
void Document::redo(int iSteps)
{
    Base::FlagToggler<> flag(d->_isTransacting);

    Gui::Selection().clearCompleteSelection();

    {
        App::TransactionGuard guard(App::TransactionGuard::Redo);

        if(!checkTransactionID(false,iSteps))
            return;

        for (int i=0;i<iSteps;i++) {
            getDocument()->redo();
        }
    }

    // Use index for iteration so that we can handle possible changes of _redoObjects while iterating.
    for (size_t i=0; i < d->_redoObjects.size(); ++i) {
        if (auto vp = Application::Instance->getViewProvider(d->_redoObjects[i])) {
            handleChildren3D(vp);
        }
    }
    d->_redoObjects.clear();
}

PyObject* Document::getPyObject()
{
    _pcDocPy->IncRef();
    return _pcDocPy;
}

void Document::handleChildren3D(ViewProvider* viewProvider, bool deleting)
{
    if(!viewProvider)
        return;
    SoGroup* childGroup =  viewProvider->getChildRoot();
    if(!childGroup)
        return;

    SoGroup* frontGroup = viewProvider->getFrontRoot();
    SoGroup* backGroup = viewProvider->getFrontRoot();

    std::vector<App::DocumentObject*> children, *childCache;

    if(deleting) {
        // When we are deleting this view provider, do not call
        // claimChildren3D(), but fetch the last claimed result from cache.
        auto it = d->_ChildrenMap.find(viewProvider);
        childCache = &children;
        if(it != d->_ChildrenMap.end()) {
            children = std::move(it->second);
            d->_ChildrenMap.erase(it);
        }
    } else {
        // If not deleting, check if children have changed
        children = viewProvider->claimChildren3D();
        childCache = &d->_ChildrenMap[viewProvider];
        if(children == *childCache)
            return;
    }

    // Obtained the old view provider
    std::set<ViewProviderDocumentObject*> oldChildren;
    for(auto child : *childCache) {
        auto vp = Base::freecad_dynamic_cast<ViewProviderDocumentObject>(getViewProvider(child));
        if(vp)
            oldChildren.insert(vp);
    }

    if(deleting)  {
        Gui::coinRemoveAllChildren(childGroup);
        Gui::coinRemoveAllChildren(frontGroup);
        Gui::coinRemoveAllChildren(backGroup);
    } else {

        bool handled = viewProvider->handleChildren3D(children);
        if(!handled) {
            Gui::coinRemoveAllChildren(childGroup);
            Gui::coinRemoveAllChildren(frontGroup);
            Gui::coinRemoveAllChildren(backGroup);
        }

        for(auto it=children.begin();it!=children.end();) {
            auto child = *it;
            auto vp = Base::freecad_dynamic_cast<ViewProviderDocumentObject>(getViewProvider(child));
            if(!vp || !vp->getRoot()) {
                it = children.erase(it);
                continue;
            }
            ++it;

            if(!handled) {
                // If the view provider does not handle its own children, we do it by
                // simply adding the child root node to child group node.
                childGroup->addChild(vp->getRoot());

                if (frontGroup) {
                    if (SoSeparator* childFrontNode = vp->getFrontRoot()){
                        frontGroup->addChild(childFrontNode);
                    }
                }

                if (backGroup) {
                    if (SoSeparator* childBackNode = vp->getBackRoot()) {
                        backGroup->addChild(childBackNode);
                    }
                }
            }

            auto iter = oldChildren.find(vp);
            if(iter!=oldChildren.end())
                oldChildren.erase(iter);
            else if(++d->_ClaimedViewProviders[vp] == 1) {
                foreachView<View3DInventor>([=](View3DInventor* view){
                    view->getViewer()->toggleViewProvider(vp);
                });
            }
        }

        *childCache = std::move(children);
    }

    for(auto it=oldChildren.begin();it!=oldChildren.end();) {
        auto iter = d->_ClaimedViewProviders.find(*it);
        if(iter != d->_ClaimedViewProviders.end()) {
            if(--iter->second > 0) {
                it = oldChildren.erase(it);
                continue;
            } else
                d->_ClaimedViewProviders.erase(iter);
        }
        ++it;
    }

    // add the remaining old children back to toplevel invertor node
    foreachView<View3DInventor>([&](View3DInventor *view) {
        for(auto vpd : oldChildren) {
            auto obj = vpd->getObject();
            if(obj && obj->getNameInDocument())
                view->getViewer()->toggleViewProvider(vpd);
        }
    });
}

bool Document::isClaimed3D(ViewProvider *vp) const {
    return d->_ClaimedViewProviders.count(vp)!=0;
}

void Document::toggleInSceneGraph(ViewProvider *vp) {
    foreachView<View3DInventor>([&](View3DInventor *view) {
        view->getViewer()->toggleViewProvider(vp);
    });
}

void Document::slotChangePropertyEditor(const App::Document &doc, const App::Property &Prop) {
    if(getDocument() == &doc) {
        FC_LOG(Prop.getFullName() << " editor changed");
        setModified(true);

        if (&Prop == &doc.ThumbnailFile) {
            if (!doc.testStatus(App::Document::Restoring))
                d->thumb.setImageFile(doc.ThumbnailFile.getValue());
            if (!doc.ThumbnailFile.getStrValue().empty())
                getDocument()->SaveThumbnail.setValue(false);
        } else if (&Prop == &doc.SaveThumbnail) {
            if (doc.SaveThumbnail.getValue() &&
                !doc.ThumbnailFile.getStrValue().empty())
            {
                getDocument()->ThumbnailFile.setValue("");
            }
            d->thumb.setUpdateOnSave(doc.SaveThumbnail.getValue());
        } else if (&Prop == &doc.UnitSystem) {
            getMainWindow()->setUserSchema(doc.UnitSystem.getValue());
        }
    }
}

