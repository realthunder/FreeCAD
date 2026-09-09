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
# include <QCheckBox>
# include <QFileInfo>
# include <QHBoxLayout>
# include <QLabel>
# include <QMessageBox>
# include <QRadioButton>
# include <QTextStream>
# include <QTimer>
# include <QStatusBar>
# include <QStringList>
# include <QVBoxLayout>
# include <Inventor/actions/SoSearchAction.h>
# include <Inventor/nodes/SoSeparator.h>
#endif

#include <cctype>
#include <sstream>
#include <boost/algorithm/string/predicate.hpp>

#include <App/AutoTransaction.h>
#include <App/Document.h>
#include <App/DocumentObject.h>
#include <App/DocumentParams.h>
#include <App/DocumentObjectGroup.h>
#include <App/FileBlobManager.h>
#include <App/GeoFeatureGroupExtension.h>
#include <App/PropertyFile.h>
#include <App/Transactions.h>
#include <App/ElementNamingUtils.h>
#include <Base/Console.h>
#include <Base/Sequencer.h>
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
#include "DrainCursor.h"
#include "FileDialog.h"
#include "MainWindow.h"
#include "MDIView.h"
#include "NotificationArea.h"
#include "Selection.h"
#include "Thumbnail.h"
#include "Tree.h"
#include "View3DInventor.h"
#include "ViewArea.h"
#include "ViewPlacement.h"
#include "View3DInventorViewer.h"
#include "ViewerContext.h"
#include "RenderParams.h"
#include "ViewParams.h"
#include "ViewProviderDocumentObject.h"
#include "ViewProviderDocumentObjectGroup.h"
#include "ViewProviderLink.h"
#include "ViewProviderShaderObject.h"
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
    /// Answered "save anyway" to the warning that this save drops content
    /// only the compact format stores. Asked once per document, not once
    /// per press of Ctrl+S.
    bool       _schemaDowngradeAcked = false;
    bool       _isTransacting;
    bool       _hasExpansion;
    bool       _changeViewTouchDocument;
    int                         _editMode;
    int                         _editModePrevious = 0;
    CoinPtr<SoNode>             _editRootNode;
    ViewProvider*               _editViewProvider;
    ViewProvider*               _editViewProviderPrevious = nullptr;
    bool                        _editWantsRestore = false;
    bool                        _editWantsRestorePrevious = false;
    App::DocumentObject*        _editingObject;
    ViewProviderDocumentObject* _editViewProviderParent;
    std::string                 _editSubname;
    std::string                 _editSubElement;
    Base::Matrix4D              _editingTransform;
    ViewerContext*              _editingViewer;
    std::set<const App::DocumentObject*> _editObjs;

    std::vector<CameraInfo>     _savedViews;
    std::map<int, std::string>  _view3DContents;
    /// Saved split-view containers: the layout string plus the leaf
    /// token of the maximized cell (empty = none) -- docs/SplitViews.md
    /// sec 5.6.
    std::vector<std::pair<std::string, std::string>> _viewAreaLayouts;
    /// The name each of them was saved under, see Gui::BaseView.
    std::map<int, std::string>  _view3DNames;

    Application*    _pcAppWnd;
    // the doc/Document
    App::Document*  _pcDocument;
    /// List of all registered views
    std::list<Gui::BaseView*> baseViews;
    /// List of all registered views
    std::list<Gui::BaseView*> passiveViews;
    std::map<const App::DocumentObject*,ViewProviderDocumentObject*> _ViewProviderMap;
    /** Which object each split-XML entry of the save in progress is for.
     *
     * The entry used to be read back as `<object name>.Gui.xml`, which makes
     * the file name an identity -- and a name a file system will take is not
     * always the name an object has. See App::Document's own map.
     */
    std::map<std::string, const App::DocumentObject*> _splitXmlEntries;
    std::map<SoSeparator *,ViewProviderDocumentObject*> _CoinMap;
    std::map<std::string,ViewProvider*> _ViewProviderMapAnnotation;
    std::vector<const App::DocumentObject*> _redoObjects;

    // cache map from view provider to its 3D claimed children
    std::unordered_map<const ViewProvider*,std::vector<App::DocumentObject*> > _ChildrenMap;

    // Gui-side share of a document load: the per-object finishRestoring()
    // calls, counted so the App restore line can be read against them, and
    // split from the scene-graph work that follows each one.
    FC_DURATION _restoreVpTime {0};
    FC_DURATION _restoreSceneTime {0};
    std::size_t _restoreVpCount = 0;

    // The other Gui share of a load, the one hiding inside App's create
    // pass: what each slotNewObject spends building the view provider,
    // attaching it (which includes its Python binding), sweeping its
    // properties in updateView, inserting it into the 3D views, and
    // announcing it. Split, because deciding what a restore can defer
    // starts with knowing which of these the time is in.
    FC_DURATION _newObjInstTime {0};
    FC_DURATION _newObjAttachTime {0};
    FC_DURATION _newObjUpdateTime {0};
    FC_DURATION _newObjViewTime {0};
    FC_DURATION _newObjAnnounceTime {0};

    /** What a progressive load parked instead of building.
     *
     * A restore under ProgressiveLoad creates no view providers inside the
     * blocking window: the create pass skips them, and the GuiDocument.xml
     * pass captures each <ViewProvider> element verbatim into _deferBuf
     * instead of restoring it. After the load lets go, a slice loop walks
     * one progressive reader over the buffer and gives every object its
     * view provider -- built, restored, finished -- a budget at a time,
     * returning to the event loop in between.
     */
    std::string _deferBuf;
    std::string _deferScratch;
    std::size_t _deferCount = 0;
    int _deferFileVersion = 0;
    int _deferDocSchema = 0;
    /// Which release wrote the document, for the properties replayed later.
    /// A colour's alpha means different things across it
    /// (Base::alphaIsOpacity), so a reader that does not carry this reads
    /// every colour in the parked record by the wrong convention.
    std::string _deferProgramVersion;
    std::unique_ptr<std::istringstream> _deferStream;
    std::unique_ptr<Base::XMLReader> _deferReader;
    bool _deferVPs = false;       // this load parks its view providers
    bool _deferApplying = false;  // a drain slice is building them now
    bool _deferScheduled = false;
    // The drain runs in two phases: first every object gets its view
    // provider, then the parked record is replayed onto them. Creating
    // them all first restores the eager path's invariant -- a parent's
    // properties are never applied while its children have no view
    // provider -- which is what keeps the link machinery linear.
    bool _deferCreated = false;
    bool _deferPhase1 = false;    // creating, not yet restoring
    // Both phases walk the objects the document had when the drain
    // started, by name: the document is live between slices and an index
    // into its object array does not survive a deletion. See DrainCursor.h.
    DrainCursor _deferCreate;
    DrainCursor _deferFinish;
    std::size_t _deferSlices = 0;
    std::size_t _deferBuilt = 0;
    FC_DURATION _deferSpent {0};
    // The drain's progress indicator (KeepInteractive -- it reports, it
    // does not take the window away), one step per object unit over all
    // three phases. Without it the stretch between the open returning
    // and the visual build starting showed a dead status bar.
    std::unique_ptr<Base::SequencerLauncher> _deferSeq;
    // The drain's own split, next to the slotNewObject one (_newObj*,
    // which during a deferred load only the drain feeds): what the
    // property replay costs against what finishing the view provider does.
    FC_DURATION _deferReadTime {0};
    FC_DURATION _deferFinishTime {0};
    FC_DURATION _deferSweepTime {0};   // phase three's updateView share
    FC_DURATION _deferModeTime {0};    // ...and its setModeSwitch share

    /** What a <Defaults> block says a view provider class holds.
     *
     * Restored into a view provider built for the purpose, and reduced to the
     * properties the record actually moved off what this machine's
     * constructor produces. That list is usually empty -- the file was
     * written by a build and a preference set that agree with this one -- and
     * then a document's view providers cost nothing to default. When it is
     * not empty, those few properties are pasted onto every view provider of
     * the class, which is what keeps a document looking the same on a machine
     * whose preferences differ from the author's.
     */
    struct RestoreDefaults {
        std::unique_ptr<ViewProvider> proto;
        std::vector<std::string> names;
    };
    std::map<std::string, RestoreDefaults> _restoreDefaults;

    // Reference counted view providers that are 3D claimed by other object.
    // These view providers shouldn't appear at secen graph root.
    std::unordered_map<const ViewProvider*, int> _ClaimedViewProviders;

    using Connection = fastsignals::connection;
    // The two blockers below need connections that were made blockable at
    // connect time -- see Base::ConnectionBlocker.
    using AdvancedConnection = fastsignals::advanced_connection;
    Connection connectNewObject;
    Connection connectDelObject;
    Connection connectCngObject;
    Connection connectRenObject;
    AdvancedConnection connectActObject;
    Connection connectSaveDocument;
    Connection connectCollectFiles;
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
    Connection connectOnTopObject;
    AdvancedConnection connectChangeDocument;

    using ConnectionBlock = fastsignals::shared_connection_block;
    ConnectionBlock connectActObjectBlocker;
    ConnectionBlock connectChangeDocumentBlocker;

    /// The LEGACY on-top store (docs/CoinRetirement.md 5.12): a dynamic
    /// property on the APP document, keyed by a view id the next
    /// session renumbers from a counter. Read-only now -- the values
    /// live in each view's OnTopObjects property, and this is consulted
    /// once on restore to migrate an old file.
    App::PropertyStringList * getOnTopProperty(App::Document *doc) {
        try {
            if (!doc)
                return nullptr;
            auto prop = doc->getPropertyByName("OnTopObjects");
            if (prop && prop->isDerivedFrom(App::PropertyStringList::getClassTypeId()))
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
        (std::bind(&Gui::Document::slotNewObject, this, sp::_1), fastsignals::at_front);
    d->connectDelObject = pcDocument->signalDeletedObject.connect
        (std::bind(&Gui::Document::slotDeletedObject, this, sp::_1));
    d->connectCngObject = pcDocument->signalChangedObject.connect
        (std::bind(&Gui::Document::slotChangedObject, this, sp::_1, sp::_2));
    d->connectRenObject = pcDocument->signalRelabelObject.connect
        (std::bind(&Gui::Document::slotRelabelObject, this, sp::_1));
    d->connectActObject = pcDocument->signalActivatedObject.connect
        (std::bind(&Gui::Document::slotActivatedObject, this, sp::_1),
         fastsignals::advanced_tag {});
    d->connectActObjectBlocker = fastsignals::shared_connection_block
        (d->connectActObject, false);
    d->connectSaveDocument = pcDocument->signalSaveDocument.connect
        (std::bind(&Gui::Document::Save, this, sp::_1));
    d->connectCollectFiles = pcDocument->signalCollectFiles.connect
        (std::bind(&Gui::Document::collectFiles, this, sp::_1, sp::_2));
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
        (std::bind(&Gui::Document::slotChangePropertyEditor, this, sp::_1, sp::_2),
         fastsignals::advanced_tag {});
    d->connectChangeDocumentBlocker = fastsignals::shared_connection_block
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

    // The on-top set is stored in each view's own OnTopObjects property
    // (docs/CoinRetirement.md 5.12), so every change to it has to reach
    // that property -- this is the only writer.
    d->connectOnTopObject = signalOnTopObject.connect(
        [this](int, const App::SubObjectT &) { snapshotOnTopObjects(); });

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
    d->connectCollectFiles.disconnect();
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
    d->connectOnTopObject.disconnect();
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

    // The view this edit session belongs to. A replayed client event names
    // its own before it is handled, and there is no other way to know which
    // it was: reaching for the active window in a process serving several
    // browsers names either nothing or somebody else's (docs/ThinClient.md
    // sec 8.9). Nothing on the desktop opens such a scope, so there the
    // active 3D view is found and activated exactly as before -- and one is
    // created for the document if it has none, which is a thing only a
    // desktop may do.
    ViewerContext *editViewer = ViewerContext::current();
    View3DInventor *view3d = nullptr;
    if (!editViewer) {
        view3d = dynamic_cast<View3DInventor *>(getActiveView());
        // if the currently active view is not the 3d view search for it and activate it
        if (view3d)
            getMainWindow()->setActiveWindow(view3d);
        else
            view3d = dynamic_cast<View3DInventor *>(setActiveView(vp));
        if (view3d)
            editViewer = view3d->getViewer();
    }

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

    if(editViewer) {
        editViewer->setEditingViewProvider(d->_editViewProvider,ModNum);
        d->_editingViewer = editViewer;
        d->_editRootNode = editViewer->getEditRootNode();
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
    // The view the session is bound to, not the one that happens to be
    // active -- with two 3D views open those are the same view only until
    // someone clicks the other, and the editing transform belongs to the
    // one doing the editing. Outside a session there is nothing bound and
    // the active view is still the only candidate.
    if (d->_editingViewer)
        d->_editingViewer->setEditingTransform(mat);
    else if (auto activeView = dynamic_cast<View3DInventor *>(getActiveView()))
        activeView->getViewer()->setEditingTransform(mat);
}

void Document::resetEdit() {
    bool vpIsNotNull = d->_editViewProvider != nullptr;
    bool vpHasChanged = d->_editViewProvider != d->_editViewProviderPrevious;
    int modeToRestore = d->_editModePrevious;
    Gui::ViewProvider* vpToRestore = d->_editViewProviderPrevious;
    bool shouldRestorePrevious = d->_editWantsRestorePrevious;

    Application::Instance->setEditDocument(nullptr);

    // Re-enter the edit session that was interrupted by the one that just
    // ended, if it asked for that with setEditRestore(true) -- e.g. an
    // assembly whose edit was suspended to edit a sketch.
    if (vpIsNotNull && vpHasChanged && shouldRestorePrevious && vpToRestore) {
        setEdit(vpToRestore, modeToRestore);
    }
}

void Document::setEditRestore(bool askRestore)
{
    d->_editWantsRestore = askRestore;
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
        // A view that is not one of this document's -- a client's mirror,
        // which belongs to the serving source rather than to the document
        // -- is not in that list. Idempotent, so a desktop viewer the loop
        // above already reset is unharmed by being named twice.
        if (d->_editingViewer)
            d->_editingViewer->resetEditingViewProvider();

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
        d->_editViewProviderPrevious = d->_editViewProvider;
        d->_editModePrevious = d->_editMode;
        d->_editWantsRestorePrevious = d->_editWantsRestore;
        d->_editWantsRestore = false;
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
        if (activeView)
            return activeView->getViewer()->isEditingViewProvider()
                ? d->_editViewProvider : nullptr;
        // No 3D view of this document to ask. A served document has none --
        // its edit session is bound to a client's mirror instead
        // (docs/ThinClient.md sec 8.9) -- so ask the view the session was
        // actually bound to.
        if (d->_editingViewer && d->_editingViewer->isEditingViewProvider())
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
    // A progressive load builds no view provider inside the blocking
    // window. Everything the object needs from one -- its properties from
    // GuiDocument.xml, its place in the scene and the tree -- arrives with
    // the post-open drain, which comes back through here with
    // _deferApplying set.
    if (d->_deferVPs && !d->_deferApplying
            && d->_pcDocument->testStatus(App::Document::Restoring))
        return;

    auto pcProvider = static_cast<ViewProviderDocumentObject*>(getViewProvider(&Obj));
    if (!pcProvider) {
        FC_TIME_INIT(t);
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
            // Compared as types, not as spellings: a file written before a
            // view provider was renamed states its former name
            // (Base::Type::addLegacyName), which is the same type.
            else if (type != Base::Type::fromName(Obj.getViewProviderName())
                     && !pcProvider->allowOverride(Obj)) {
                FC_WARN("View provider type '" << cName << "' does not support " << Obj.getFullName());
                delete pcProvider;
                pcProvider = nullptr;
                cName = Obj.getViewProviderName();
            }
            else {
                break;
            }
        }

        // A drain slice materializing a load's parked view providers is
        // not a modification -- an eager restore's setModified(true) here
        // was reset when the load finished, which the drain outlives.
        if (!d->_deferApplying)
            setModified(true);
        d->_ViewProviderMap[&Obj] = pcProvider;
        d->_CoinMap[pcProvider->getRoot()] = pcProvider;
        pcProvider->setStatus(Gui::ViewStatus::TouchDocument, d->_changeViewTouchDocument);
        FC_DURATION_PLUS(d->_newObjInstTime, t);

        try {
            pcProvider->attachDocumentObject(const_cast<App::DocumentObject*>(&Obj));
            FC_DURATION_PLUS(d->_newObjAttachTime, t);
            // The drain's first phase only creates; the property sweep runs
            // after the parked record is applied, so the visual is queued
            // with its restored properties -- a color landing on a built
            // faceset costs a scene traversal that landing on an unbuilt
            // one does not.
            if (!d->_deferPhase1)
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
        FC_DURATION_PLUS(d->_newObjUpdateTime, t);
    }else{
        try {
            pcProvider->reattach(const_cast<App::DocumentObject*>(&Obj));
        } catch(Base::Exception &e){
            e.ReportException();
        }
    }

    if (pcProvider) {
        FC_TIME_INIT(t);
        std::list<Gui::BaseView*>::iterator vIt;
        // cycling to all views of the document
        for (vIt = d->baseViews.begin();vIt != d->baseViews.end();++vIt) {
            auto activeView = dynamic_cast<View3DInventor *>(*vIt);
            if (activeView)
                activeView->getViewer()->addViewProvider(pcProvider);
        }
        FC_DURATION_PLUS(d->_newObjViewTime, t);

        // adding to the tree
        signalNewObject(*pcProvider);
        pcProvider->pcDocument = this;

        // it is possible that a new viewprovider already claims children
        handleChildren3D(pcProvider);
        FC_DURATION_PLUS(d->_newObjAnnounceTime, t);
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

    if (d->_editViewProviderPrevious == viewProvider) {
        // never re-enter edit on a deleted object
        d->_editViewProviderPrevious = nullptr;
        d->_editWantsRestorePrevious = false;
    }
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
    // A closing document keeps none of its parked view providers; anything
    // that needed them to exist (a save) flushed before getting here.
    d->_deferVPs = false;
    d->_deferCount = 0;
    d->_deferReader.reset();
    d->_deferStream.reset();
    d->_deferBuf.clear();
    d->_deferCreate.clear();
    d->_deferFinish.clear();

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

namespace {
/** Whether anything in the document needs the blob store to keep its content.
 *
 * Asks the properties rather than looking for an appearance: what makes a
 * format choice more than a size trade is a referrer with no schema 4
 * spelling for its content, and only the property knows that
 * (App::BlobReferrerProperty::blobContentNeedsStore). View provider
 * properties are scanned too -- ShapeAppearance, today's only such
 * referrer, is one of them.
 */
bool needsBlobStore(App::Document *doc)
{
    auto scan = [](const App::PropertyContainer *container) {
        std::vector<App::Property*> props;
        container->getPropertyList(props);
        for (auto prop : props) {
            auto referrer = dynamic_cast<const App::BlobReferrerProperty*>(prop);
            if (referrer && referrer->blobContentNeedsStore())
                return true;
        }
        return false;
    };

    if (scan(doc))
        return true;
    auto gdoc = Application::Instance->getDocument(doc);
    for (auto obj : doc->getObjects()) {
        if (scan(obj))
            return true;
        if (gdoc) {
            if (auto vp = gdoc->getViewProvider(obj)) {
                if (scan(vp))
                    return true;
            }
        }
    }
    return false;
}

/// What a format prompt settles: the format to write, or nothing because the
/// save itself was called off.
enum class FormatAnswer { Compact, Standard, Cancel };

/** The explicit statement that the compact format is this fork's own.
 *
 * Shown when a save resolves to compact, once, with a way to turn it off --
 * not as a heading inside the file dialog. A warning that appears and
 * disappears as a radio button is clicked reads as a flicker, and the case
 * that matters most never toggles anything: a new document is compact
 * already, so nobody would ever have seen it.
 */
FormatAnswer confirmCompactFormat(ParameterGrp::handle hGrp)
{
    if (!hGrp->GetBool("WarnCompactFormat", true))
        return FormatAnswer::Compact;

    QMessageBox box(getMainWindow());
    box.setIcon(QMessageBox::Warning);
    box.setWindowTitle(QObject::tr("Incompatible file format"));
    box.setText(QObject::tr("<b>This file will not open in any other FreeCAD.</b>"));
    box.setInformativeText(QObject::tr(
                "The compact format is this FreeCAD's own. No other build reads it "
                "-- not upstream, not an older release of this fork; they report a "
                "broken file rather than a format they do not know.\n\n"
                "The standard format opens everywhere, and is the one to pick for a "
                "file that has to leave this machine."));
    auto keep = box.addButton(QObject::tr("Save compact"), QMessageBox::AcceptRole);
    auto standard = box.addButton(QObject::tr("Use standard format"), QMessageBox::ActionRole);
    box.addButton(QMessageBox::Cancel);
    box.setDefaultButton(keep);
    auto again = new QCheckBox(QObject::tr("Do not warn again"), &box);
    box.setCheckBox(again);
    box.exec();

    auto clicked = box.clickedButton();
    if (clicked != keep && clicked != standard)
        return FormatAnswer::Cancel;
    // The suppression is about the warning, not about the format chosen this
    // time round: someone who never wants to see it again means it whichever
    // button they leave by.
    if (again->isChecked())
        hGrp->SetBool("WarnCompactFormat", false);
    return clicked == keep ? FormatAnswer::Compact : FormatAnswer::Standard;
}

/** The other half: content the format about to be written cannot carry.
 *
 * Fires only when the scan found something that would actually be dropped,
 * so it is a decision and not a reminder, and offers the upgrade first
 * because the alternative loses data. `acked` comes back set when the user
 * asked not to be told again about this document.
 */
FormatAnswer confirmSchemaUpgrade(const QStringList &docs, bool *acked)
{
    QMessageBox box(getMainWindow());
    box.setIcon(QMessageBox::Warning);
    box.setWindowTitle(QObject::tr("File content will be dropped"));
    box.setText(QObject::tr("<b>The standard format cannot store the files this "
                            "document refers to.</b>"));
    QString detail = QObject::tr(
                "Stored file content -- today that is the texture images on a shape "
                "appearance -- lives inside the document in the compact format only. "
                "Saved as standard, the reference is kept and the image is not: "
                "reopening the file reports every one of them as missing.\n\n"
                "The compact format keeps them, and is read by this FreeCAD only.");
    if (docs.size() > 1)
        detail += QObject::tr("\n\nAffected documents: %1").arg(docs.join(QStringLiteral(", ")));
    box.setInformativeText(detail);
    auto upgrade = box.addButton(QObject::tr("Use compact format"), QMessageBox::AcceptRole);
    auto anyway = box.addButton(QObject::tr("Save anyway"), QMessageBox::DestructiveRole);
    box.addButton(QMessageBox::Cancel);
    box.setDefaultButton(upgrade);
    auto again = new QCheckBox(QObject::tr("Do not ask again for this document"), &box);
    box.setCheckBox(again);
    box.exec();

    auto clicked = box.clickedButton();
    if (clicked == upgrade)
        return FormatAnswer::Compact;
    if (clicked != anyway)
        return FormatAnswer::Cancel;
    *acked = again->isChecked();
    return FormatAnswer::Standard;
}

} // anonymous namespace

/* Offer the schema their own content needs to every document about to be
 * written, before the first of them is. A document that has never been saved
 * is skipped: it is going through the save dialog, which makes the same offer
 * with the format row in hand.
 */
bool Document::offerSchemaUpgrade(const std::vector<App::Document*> &docs)
{
    // The Gui documents, not the App ones: an answer of "save it away
    // anyway" is remembered on the document it was given for.
    std::vector<Document*> upgrade;
    QStringList names;
    for (auto doc : docs) {
        auto gdoc = Application::Instance->getDocument(doc);
        if (!gdoc || gdoc->d->_schemaDowngradeAcked
                || doc->testStatus(App::Document::PartialDoc)
                || doc->testStatus(App::Document::TempDoc)
                || !doc->isSaved()
                || doc->getSaveSchemaVersion() >= 5
                || !needsBlobStore(doc))
            continue;
        upgrade.push_back(gdoc);
        names << QString::fromUtf8(doc->Label.getValue());
    }
    if (upgrade.empty())
        return true;

    bool acked = false;
    switch (confirmSchemaUpgrade(names, &acked)) {
    case FormatAnswer::Compact:
        for (auto gdoc : upgrade)
            Command::doCommand(Command::Doc,
                    "App.getDocument(\"%s\").SaveSchemaVersion = %d",
                    gdoc->getDocument()->getName(),
                    (int)App::Document::getCurrentSchemaVersion());
        break;
    case FormatAnswer::Standard:
        for (auto gdoc : upgrade)
            gdoc->d->_schemaDowngradeAcked = acked;
        break;
    case FormatAnswer::Cancel:
        return false;
    }
    return true;
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

            // A plain save never opens the format dialog, and never touches
            // the schema -- so this is where a document that has GROWN
            // content the standard format cannot carry gets asked about it.
            // Silent otherwise: the scan only speaks when something would
            // actually be dropped.
            if (!offerSchemaUpgrade(docs))
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

namespace {
/** The document-format choice a save dialog carries.
 *
 * The choice is always on screen -- FileDialog gives the widget a full-width
 * row of its own and there is no toggle to fold it away, so what the save is
 * about to write can be read off the dialog at any moment. What compact
 * COSTS is stated by confirmCompactFormat() once the file name is in: a
 * warning heading that comes and goes with a radio button reads as a
 * flicker, and a preselected compact never toggles one. The result lands in
 * the caller's own struct -- the widget is reparented into the file dialog
 * and dies with it.
 */
class DocumentFormatOption : public QWidget
{
public:
    /** What the dialog decides, in one place.
     *
     * The format choice belongs to the document and is written to its
     * SaveSchemaVersion. The two storage choices do not: they are preferences
     * that apply to every save from here on, shown here because this is where
     * someone is already thinking about how the file will be written.
     */
    struct Choices
    {
        bool compact = false;
        bool dedupPCurves = true;
        bool dedupCongruent = true;
        bool dedupGeometry = false;
    };

    DocumentFormatOption(bool compact, Choices *result)
        : result(result)
    {
        auto layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 6, 0, 0);

        // Two rows, each one line: what the save writes, then how shapes are
        // stored in it. The labels are short enough to sit beside their
        // controls; what each one costs is one hover away, in the tooltip.
        auto formatRow = new QHBoxLayout;
        auto format = new QLabel(QObject::tr("Document format:"), this);
        format->setStyleSheet(QStringLiteral("font-weight:bold;"));
        formatRow->addWidget(format);

        standard = new QRadioButton(QObject::tr("Standard"), this);
        standard->setToolTip(QObject::tr(
                    "The format every FreeCAD version can open."));
        compactBtn = new QRadioButton(QObject::tr("Compact"), this);
        compactBtn->setToolTip(QObject::tr(
                    "Smaller files that load faster, readable by this "
                    "FreeCAD only."));
        formatRow->addWidget(standard);
        formatRow->addWidget(compactBtn);
        formatRow->addStretch();
        layout->addLayout(formatRow);

        compactBtn->setChecked(compact);
        standard->setChecked(!compact);

        // Separate, and deliberately apart from the format row: neither of
        // these costs compatibility. A file missing a pcurve a plane can rebuild, or
        // naming one table entry from two records, is ordinary BRep that every
        // FreeCAD has always read.
        auto storageRow = new QHBoxLayout;
        auto storage = new QLabel(QObject::tr("Shape storage:"), this);
        storage->setStyleSheet(QStringLiteral("font-weight:bold;"));
        storageRow->addWidget(storage);

        dedupPCurves = new QCheckBox(QObject::tr("2D curves"), this);
        dedupPCurves->setToolTip(QObject::tr(
                    "Store each 2D curve once, and leave out the ones loading "
                    "computes again. Same shape, and the file still opens "
                    "anywhere."));
        dedupCongruent = new QCheckBox(QObject::tr("Repeated parts"), this);
        dedupCongruent->setToolTip(QObject::tr(
                    "Store one copy of parts that are the same shape in "
                    "different places, with the motion between them recorded. "
                    "Merged only once that motion has been checked."));
        dedupGeometry = new QCheckBox(QObject::tr("Shared geometry"), this);
        dedupGeometry->setToolTip(QObject::tr(
                    "Let a part name surfaces and curves another part already "
                    "stores, saving about half of those tables. Off by "
                    "default: it makes one part depend on the other part's "
                    "file being there."));
        dedupPCurves->setChecked(App::DocumentParams::getDedupShapePCurves());
        dedupCongruent->setChecked(App::DocumentParams::getDedupCongruentShapes());
        dedupGeometry->setChecked(App::DocumentParams::getDedupCrossFileGeometry());
        storageRow->addWidget(dedupPCurves);
        storageRow->addWidget(dedupCongruent);
        storageRow->addWidget(dedupGeometry);
        storageRow->addStretch();
        layout->addSpacing(2);
        layout->addLayout(storageRow);

        apply();
        QObject::connect(compactBtn, &QRadioButton::toggled,
                         [this](bool) { apply(); });
        QObject::connect(dedupPCurves, &QCheckBox::toggled,
                         [this](bool) { apply(); });
        QObject::connect(dedupCongruent, &QCheckBox::toggled,
                         [this](bool) { apply(); });
        QObject::connect(dedupGeometry, &QCheckBox::toggled,
                         [this](bool) { apply(); });
    }

private:
    void apply()
    {
        result->compact = compactBtn->isChecked();
        result->dedupPCurves = dedupPCurves->isChecked();
        result->dedupCongruent = dedupCongruent->isChecked();
        result->dedupGeometry = dedupGeometry->isChecked();
    }

    Choices *result;
    QRadioButton *standard;
    QRadioButton *compactBtn;
    QCheckBox *dedupPCurves;
    QCheckBox *dedupCongruent;
    QCheckBox *dedupGeometry;
};
} // anonymous namespace

/// Save the document under a new file name
bool Document::saveAs()
{
    getMainWindow()->showMessage(QObject::tr("Save document under new filename..."));

    // The format is the document's own promise -- SaveSchemaVersion, shown
    // and changed here and nowhere quieter. Plain Save never touches it.
    //
    // A document that has been saved keeps its own. One that has not starts
    // COMPACT, this fork's own format being what a document written here
    // should be: that is the property's default, and the last choice made in
    // this dialog can hold it back but not push it -- a cap something
    // already lowered on purpose (a script, or the property editor) is not
    // for a remembered preference to raise.
    auto hGrp = App::GetApplication().GetParameterGroupByPath(
            "User parameter:BaseApp/Preferences/Document");
    const char *curFile = getDocument()->FileName.getValue();
    bool compact = getDocument()->getSaveSchemaVersion() >= 5;
    if (!(curFile && curFile[0]))
        compact = compact && hGrp->GetBool("PreferCompactFormat", true);
    DocumentFormatOption::Choices chosen;
    chosen.compact = compact;
    chosen.dedupPCurves = App::DocumentParams::getDedupShapePCurves();
    chosen.dedupCongruent = App::DocumentParams::getDedupCongruentShapes();
    chosen.dedupGeometry = App::DocumentParams::getDedupCrossFileGeometry();

    QString exe = qApp->applicationName();
    QString fn = FileDialog::getSaveFileName(getMainWindow(), QObject::tr("Save %1 Document").arg(exe),
        QString::fromUtf8(getDocument()->FileName.getValue()),
        QStringLiteral("%1 %2 (*.FCStd)").arg(exe).arg(QObject::tr("Document")),
        nullptr, QFileDialog::Options(), QFileDialog::AnyFile,
        new DocumentFormatOption(compact, &chosen));

    if (!fn.isEmpty()) {
        QFileInfo fi;
        fi.setFile(fn);

        const char * DocName = App::GetApplication().getDocumentName(getDocument());

        // The format is settled before a byte is written: what compact costs
        // said out loud, and -- for a standard save -- the offer of the
        // schema this document's own content needs. Either prompt can send
        // the save back the other way, and either can call it off.
        if (chosen.compact) {
            switch (confirmCompactFormat(hGrp)) {
            case FormatAnswer::Compact:
                break;
            case FormatAnswer::Standard:
                chosen.compact = false;
                break;
            case FormatAnswer::Cancel:
                getMainWindow()->showMessage(QObject::tr("Saving aborted"), 2000);
                return false;
            }
        }
        if (!chosen.compact && !d->_schemaDowngradeAcked && needsBlobStore(getDocument())) {
            bool acked = false;
            QStringList names;
            names << QString::fromUtf8(getDocument()->Label.getValue());
            switch (confirmSchemaUpgrade(names, &acked)) {
            case FormatAnswer::Compact:
                chosen.compact = true;
                break;
            case FormatAnswer::Standard:
                d->_schemaDowngradeAcked = acked;
                break;
            case FormatAnswer::Cancel:
                getMainWindow()->showMessage(QObject::tr("Saving aborted"), 2000);
                return false;
            }
        }

        // save as new file name
        try {
            Gui::WaitCursor wc;
            hGrp->SetBool("PreferCompactFormat", chosen.compact);
            // Preferences, so they are set before the save runs and stay set
            // for the next one. Written only when changed, to leave the
            // parameter file alone otherwise.
            if (chosen.dedupPCurves != App::DocumentParams::getDedupShapePCurves())
                App::DocumentParams::setDedupShapePCurves(chosen.dedupPCurves);
            if (chosen.dedupCongruent != App::DocumentParams::getDedupCongruentShapes())
                App::DocumentParams::setDedupCongruentShapes(chosen.dedupCongruent);
            if (chosen.dedupGeometry != App::DocumentParams::getDedupCrossFileGeometry())
                App::DocumentParams::setDedupCrossFileGeometry(chosen.dedupGeometry);
            if (chosen.compact != (getDocument()->getSaveSchemaVersion() >= 5))
                Command::doCommand(Command::Doc,
                        "App.getDocument(\"%s\").SaveSchemaVersion = %d", DocName,
                        chosen.compact ? (int)App::Document::getCurrentSchemaVersion() : 4);
            std::string literal = Base::Tools::pythonLiteral(fn);
            Command::doCommand(Command::Doc,"App.getDocument(\"%s\").saveAs(%s)"
                                           , DocName, literal.c_str());
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

    // Save All writes each document at whatever schema it had decided, so it
    // owes the same offer the plain Save path makes -- once, for all of them,
    // before the first one is written.
    if (!offerSchemaUpgrade(docs))
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
        std::string pyfn = Base::Tools::pythonLiteral(fn);
        Command::doCommand(Command::Doc,"App.getDocument(\"%s\").saveCopy(%s)"
                                       , DocName, pyfn.c_str());

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

void Document::collectFiles(App::FileBlobManager &manager,
                            const std::vector<App::DocumentObject*> &objs) const
{
    // The object is passed alongside: a view provider's properties are named
    // after, and belong to the generation of, the object it presents.
    auto collect = [&manager](const App::PropertyContainer *container,
                              const App::DocumentObject *object) {
        if (!container) {
            return;
        }
        std::vector<App::Property*> props;
        container->getPropertyList(props);
        for (auto prop : props) {
            if (auto owner = dynamic_cast<App::BlobReferrerProperty*>(prop)) {
                owner->collectBlobs(manager, object);
            }
        }
    };

    if (objs.empty()) {
        for (const auto &v : d->_ViewProviderMap) {
            collect(v.second, v.first);
        }
        // A view's own properties -- the embedded environment image lives
        // here. They are written into a string inside GuiDocument.xml and
        // replayed from memory, so they can never register an archive entry
        // themselves; this is the only place their content is picked up.
        for (auto view : d->baseViews) {
            collect(view, nullptr);
        }
    }
    else {
        // An export carries the selected objects only. Views belong to the
        // document, not to any object, so they stay behind.
        for (auto obj : objs) {
            auto it = d->_ViewProviderMap.find(obj);
            if (it != d->_ViewProviderMap.end()) {
                collect(it->second, obj);
            }
        }
    }
}

/**
 * Adds a separate XML file to the projects file that contains information about the view providers.
 */
void Document::Save (Base::Writer &writer) const
{
    // A save writes every view provider; whatever the load still has
    // parked must exist first, or the file would record defaults.
    const_cast<Document*>(this)->flushDeferredRestore();

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
    if (pObj) {
        // Whatever the shared default block moved off this build's own
        // defaults has to be put back before the object's own properties, so
        // that what the file states for this object still wins.
        applyDefaults(pObj);
        pObj->Restore(xmlReader);
    }
    if (pObj && expanded) {
        Gui::ViewProviderDocumentObject* vp = static_cast<Gui::ViewProviderDocumentObject*>(pObj);
        this->signalExpandObject(*vp, TreeItemMode::ExpandItem,0,0);
    }
}

// Deliberately still 1, and the defaults block of schema 5 (see
// App::Document::getWritableSchemaVersions) does not move it.
//
// RestoreDocFile below gates its whole body on `DocumentSchema == 1`, and so
// does every released build. Writing 2 would therefore not mean "an older
// FreeCAD reads what it can" -- it would mean an older FreeCAD reads *none*
// of this file: no view providers, no camera, no saved views. Left at 1, an
// older build walks past the block it does not recognise and still restores
// every property the file states per object, which is everything anyone
// actually changed. Verified by stripping the Defaults attribute from a
// written file and reading it back (harnesses/defaults_check.py).
//
// A future change that an old reader could genuinely mis-parse rather than
// skip is what should raise this.
static const int FC_GUI_SCHEMA_VER = 1;
static const char *FC_XML_GUI_POSTFIX = ".Gui.xml";
static const char *FC_ATTR_SPLIT_XML = "Split";
/** Whether this file lists the entry each view provider was written to.
 *
 * *** The reader must never re-derive a file name. A name is chosen by the
 * writer -- it has to be one the file system will take, which the name an
 * object carries need not be (Base::Tools::portableFileName) -- and a reader
 * that computes it again only works while both sides compute alike. Change
 * the rule, or the limit, and every file written before the change stops
 * loading. So the name is written down, and this says it was.
 */
static const char *FC_ATTR_SPLIT_FILES = "SplitFiles";
static const char *FC_ATTR_TREE_EXPANSION = "HasExpansion";

namespace {

// The element names of the shared default block, and the attribute on
// <ViewProviderData> that says how many entries it has. A reader that finds
// no attribute never looks for the block, which is what lets a file written
// without it be read by the same code.
const char *FC_ELEM_DEFAULTS = "Defaults";
const char *FC_ELEM_DEFAULT = "Default";
const char *FC_ATTR_DEFAULTS = "Defaults";

/** Build a view provider of the given class outside any document.
 *
 * What a class treats as a default lives in its constructor and nowhere
 * else -- there is no metadata to ask -- so the only way to find out is to
 * build one and read its properties. Returns null for a class that cannot be
 * instantiated, and the caller then writes everything as before.
 */
std::unique_ptr<ViewProvider> makeDefaultViewProvider(const char *typeName)
{
    auto type = Base::Type::fromName(typeName);
    if (type.isBad() || !type.isDerivedFrom(ViewProvider::getClassTypeId()))
        return {};
    std::unique_ptr<ViewProvider> res;
    try {
        res.reset(static_cast<ViewProvider*>(type.createInstance()));
    }
    catch (Base::Exception &e) {
        e.ReportException();
    }
    catch (const std::exception &e) {
        FC_ERR("Failed to build a default " << typeName << ": " << e.what());
    }
    if (!res)
        FC_LOG("No default view provider for " << typeName);
    return res;
}

} // anonymous namespace

void Document::restoreDefaults(Base::XMLReader &xmlReader, int count, int schemaVersion)
{
    d->_restoreDefaults.clear();
    if (count <= 0)
        return;

    // The stand-ins belong to no document, and a detached view provider's
    // reaction to a property changing is no more written for the occasion
    // than a detached object's. Same guard as the App reader; nothing here
    // is a change to anything, only a record being read.
    App::Document::RestoringDefaultsGuard restoringGuard;

    xmlReader.readElement(FC_ELEM_DEFAULTS);
    for (int i=0; i<count; ++i) {
        int guard;
        xmlReader.readElement(FC_ELEM_DEFAULT, &guard);
        std::string type = xmlReader.getAttribute("type");
        // The type's current name, for the same reason the App reader
        // resolves it: the block is looked up by the live type name, and a
        // file written before a rename states the former one.
        if (Base::Type resolved = Base::Type::fromName(type.c_str()); !resolved.isBad())
            type = resolved.getName();
        auto proto = makeDefaultViewProvider(type.c_str());
        // ⚠️ Two stand-ins, not one stand-in and a pile of Property::Copy().
        // A detached copy has no container, so enumerations compare by a
        // list they no longer have and the diff answers "differs" for
        // properties that are identical. Two live stand-ins, both
        // serialized the way the writer serialized, is the comparison the
        // writer made -- the App reader learned this first, and there is
        // nothing view-provider-shaped about the lesson.
        auto fresh = proto ? makeDefaultViewProvider(type.c_str()) : nullptr;
        if (proto && fresh) {
            // Names before the restore, lookups after it: a block written
            // by a different build may create or replace properties on the
            // way in.
            std::vector<std::string> candidates;
            {
                std::vector<App::Property*> props;
                proto->getPropertyList(props);
                for (auto prop : props)
                    if (App::SharedDefaults::eligible(*proto, *prop))
                        candidates.emplace_back(prop->getName());
            }

            proto->App::PropertyContainer::Restore(xmlReader);

            // Byte-diff, exactly as the writer elided: both sides through
            // serializeForCompare, status compared mod Touched. See the App
            // reader for why the file's literal text is not one of the
            // sides.
            const unsigned long touchedMask = 1UL << App::Property::Touched;
            DocumentP::RestoreDefaults entry;
            std::string recorded, built;
            for (const auto &name : candidates) {
                auto prop = proto->getPropertyByName(name.c_str());
                auto other = fresh->getPropertyByName(name.c_str());
                if (!prop || !other || prop->getTypeId() != other->getTypeId())
                    continue;
                bool differs = (prop->getStatus() & ~touchedMask)
                        != (other->getStatus() & ~touchedMask);
                if (!differs) {
                    if (!App::SharedDefaults::serializeForCompare(schemaVersion,
                                xmlReader.FileVersion, *prop, recorded)
                            || !App::SharedDefaults::serializeForCompare(schemaVersion,
                                xmlReader.FileVersion, *other, built))
                        continue;
                    differs = (recorded != built);
                }
                if (differs)
                    entry.names.emplace_back(name);
            }
            if (!entry.names.empty())
                FC_LOG("Default view provider " << type << " differs in "
                        << entry.names.size() << " properties");
            // Kept even when nothing differs: a property in the block may
            // have registered an archive entry against this stand-in, and the
            // reader will come looking for its owner later.
            entry.proto = std::move(proto);
            d->_restoreDefaults[type] = std::move(entry);
        }
        xmlReader.readEndElement(FC_ELEM_DEFAULT, &guard);
    }
    xmlReader.readEndElement(FC_ELEM_DEFAULTS);
}

void Document::applyDefaults(ViewProvider *vp)
{
    if (d->_restoreDefaults.empty())
        return;
    auto it = d->_restoreDefaults.find(vp->getTypeId().getName());
    if (it == d->_restoreDefaults.end() || it->second.names.empty())
        return;
    for (const auto &name : it->second.names) {
        auto prop = vp->getPropertyByName(name.c_str());
        auto other = it->second.proto->getPropertyByName(name.c_str());
        if (!prop || !other || prop->getTypeId() != other->getTypeId())
            continue;
        // Status first and value second, the order Restore itself uses,
        // behind the same kind of per-property net: one recorded default
        // that will not paste must cost that property, not the view file.
        try {
            App::Property::StatusBits status(other->getStatus());
            status.reset(App::Property::User1);
            status.reset(App::Property::User2);
            status.reset(App::Property::User3);
            prop->setStatusValue(status.to_ulong());
            prop->Paste(*other);
        }
        catch (Base::Exception &e) {
            e.ReportException();
            FC_ERR("Failed to apply default " << vp->getFullName()
                    << '.' << name);
        }
        catch (const std::exception &e) {
            FC_ERR("Failed to apply default " << vp->getFullName()
                    << '.' << name << ": " << e.what());
        }
        catch (...) {
            FC_ERR("Failed to apply default " << vp->getFullName()
                    << '.' << name);
        }
    }
}

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
    // This file states no program version of its own, and every property that
    // has to know which release wrote the document lives HERE -- the colours
    // and materials are view provider properties (Base::alphaIsOpacity). Left
    // unset the string is empty, which classifies as "newer than anything
    // named" and would convert colours in files that need no conversion.
    if (auto parent = reader.getParent())
        xmlReader.ProgramVersion = parent->ProgramVersion;
    else if (d->_pcDocument)
        xmlReader.ProgramVersion = d->_pcDocument->getProgramVersion();

    if(boost::ends_with(reader.getFileName(),FC_XML_GUI_POSTFIX)) {
        xmlReader.readElement("ViewProvider");
        readObject(xmlReader);
        return;
    }

    bool split = !!xmlReader.getAttributeAsInteger(FC_ATTR_SPLIT_XML,"0");
    bool splitFiles = !!xmlReader.getAttributeAsInteger(FC_ATTR_SPLIT_FILES,"0");

    d->_hasExpansion = !!xmlReader.getAttributeAsInteger(FC_ATTR_TREE_EXPANSION,"0");
    if(d->_hasExpansion)
        TreeWidget::restoreDocumentItem(this, xmlReader);

    // At this stage all the document objects and their associated view providers exist.
    // Now we must restore the properties of the view providers only.
    //
    // SchemeVersion "1"
    if (xmlReader.DocumentSchema == 1) {

        if (split && d->_deferVPs) {
            // Split view provider files are separate archive entries read
            // through the same forward walk as everything else; nothing
            // about them can be parked. Build every view provider now --
            // the document is still restoring, so this is the eager path
            // arriving one pass later -- and put them in the state the
            // eager Restore() put them in before the files are read.
            d->_deferVPs = false;
            Base::StateLocker guard(d->_deferApplying);
            for (auto obj : getDocument()->getObjects()) {
                if (!getViewProvider(obj))
                    slotNewObject(*obj);
            }
            for (auto &v : d->_ViewProviderMap) {
                v.second->startRestoring();
                v.second->setStatus(Gui::isRestoring, true);
            }
        }

        if(!split) {
            // read the viewproviders itself
            xmlReader.readElement("ViewProviderData");
            int Cnt = xmlReader.getAttributeAsInteger("Count");
            FC_TIME_INIT(t);
            auto stats = App::PropertyContainer::restoreStats;
            // The App document's schema, not this file's own SchemaVersion:
            // the blocks were recorded by the writer that produced the whole
            // archive, and the comparison has to serialize at its settings.
            restoreDefaults(xmlReader,
                    xmlReader.getAttributeAsInteger(FC_ATTR_DEFAULTS,"0"),
                    reader.getDocumentSchema());
            if (d->_deferVPs) {
                d->_deferBuf = "<ViewProviderData>";
                d->_deferFileVersion = xmlReader.FileVersion;
                d->_deferDocSchema = xmlReader.DocumentSchema;
                d->_deferProgramVersion = xmlReader.ProgramVersion;
            }
            for (int i=0; i<Cnt; i++) {
                int guard;
                xmlReader.readElement("ViewProvider",&guard);
                if (d->_deferVPs) {
                    // Park the element, verbatim, for the post-open drain
                    // -- unless it references content that only the load
                    // holds open. An archive entry (` file="`) is consumed by
                    // the forward walk, in registration order, before any
                    // drain runs. A blob reference (` hash="`, schema 5) is
                    // served out of FileBlobManager's restore hold, which is
                    // dropped as soon as the finish-restore signal returns
                    // (App::Document::afterRestore). Either way the drain is
                    // too late and the property would come back empty, so
                    // such a view provider restores now, exactly as the eager
                    // path would have. The marker is exact for attributes: a
                    // quote inside a value is re-escaped by the capture, so a
                    // literal ` file="` can only be markup.
                    std::string &captured = d->_deferScratch;
                    captured.clear();
                    xmlReader.captureElement(captured);
                    if (captured.find(" file=\"") != std::string::npos
                            || captured.find(" hash=\"") != std::string::npos) {
                        restoreCapturedViewProvider(captured, xmlReader);
                    } else {
                        d->_deferBuf += captured;
                        ++d->_deferCount;
                    }
                }
                else
                    readObject(xmlReader);
                xmlReader.readEndElement("ViewProvider",&guard);
            }
            if (d->_deferVPs)
                d->_deferBuf += "</ViewProviderData>";
            // This one archive entry is the single biggest thing a large
            // document load parses -- larger than the document itself -- and
            // the view providers in it are almost all default. Report what
            // the properties cost against what reading them cost, because a
            // fix on the writing side and a fix in the reader are different
            // work.
            stats = App::PropertyContainer::restoreStats - stats;
            FC_LOG("restore " << getDocument()->getName() << " gui xml: " << Cnt
                    << " view providers, " << stats.count << " properties ("
                    << stats.unmatched << " unmatched), property "
                    << stats.total.count() << "s (value " << stats.value.count()
                    << "s), total " << Base::GetDuration(t).count() << 's');
            xmlReader.readEndElement("ViewProviderData");
        } else if (splitFiles) {
            // The entries as the writer recorded them. Read, never re-derived:
            // the writer is free to change how it makes a name into one the
            // file system will take, and a file written before it changed has
            // to go on loading.
            xmlReader.readElement("ViewProviderFiles");
            const int count = xmlReader.getAttributeAsInteger("Count");
            for (int i = 0; i < count; ++i) {
                xmlReader.readElement("File");
                xmlReader.addFile(xmlReader.getAttribute("name"),this);
            }
            xmlReader.readEndElement("ViewProviderFiles");
        } else {
            // Written before the entries were recorded, when the name was the
            // object's name and the postfix -- which is what those files hold,
            // whatever the writer does now.
            for(const auto &v : d->_ViewProviderMap)
                xmlReader.addFile(std::string(v.first->getNameInDocument())+FC_XML_GUI_POSTFIX,this);
        }

        // read camera settings
        xmlReader.readElement("Camera");

        int cameraExtra = xmlReader.getAttributeAsInteger("extra", "0");
        int cameraBinding = xmlReader.getAttributeAsInteger("binding", "0");
        int cameraId = xmlReader.getAttributeAsInteger("id", "0");
        int view3dCount = xmlReader.getAttributeAsInteger("view3d", "0");
        int viewAreaCount = xmlReader.getAttributeAsInteger("viewareas", "0");

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
        d->_view3DNames.clear();
        for (int i=0; i<view3dCount; ++i) {
            xmlReader.readElement("View3D");
            int id = xmlReader.getAttributeAsInteger("id");
            // Read before the characters are drained, and absent from files
            // written before views had a name -- those views keep the one
            // they were handed when they were created.
            d->_view3DNames[id] = xmlReader.getAttribute("name", "");
            d->_view3DContents[id] = xmlReader.readCharacters();
        }

        d->_viewAreaLayouts.clear();
        for (int i=0; i<viewAreaCount; ++i) {
            xmlReader.readElement("ViewArea");
            d->_viewAreaLayouts.emplace_back(
                    xmlReader.getAttribute("layout", ""),
                    xmlReader.getAttribute("maximized", ""));
        }
    }

    xmlReader.readEndElement("Document");

    // In the file GuiDocument.xml new data files might be added
    if (!xmlReader.getFilenames().empty())
        xmlReader.readFiles();

    // The default stand-ins have served their purpose and nothing points at
    // them any more -- including the reader, which has just drained whatever
    // they registered. Unless the view providers were parked: the drain
    // still needs applyDefaults(), and clears this when it is done.
    if (!d->_deferVPs)
        d->_restoreDefaults.clear();

    // reset modified flag
    setModified(false);
}

void Document::slotStartRestoreDocument(const App::Document& doc)
{
    if (d->_pcDocument != &doc)
        return;
    // disable this signal while loading a document
    d->connectActObjectBlocker.block();
    d->_restoreVpTime = d->_restoreSceneTime = FC_DURATION(0);
    d->_restoreVpCount = 0;
    d->_newObjInstTime = d->_newObjAttachTime = d->_newObjUpdateTime
        = d->_newObjViewTime = d->_newObjAnnounceTime = FC_DURATION(0);

    // Whether this load parks its view providers for the post-open drain.
    // A leftover drain from a previous restore into this document (a
    // partial-document reload) is dropped: the new file speaks for every
    // object now.
    d->_deferVPs = Gui::RenderParams::getProgressiveLoad();
    d->_deferBuf.clear();
    d->_deferCount = 0;
    d->_deferReader.reset();
    d->_deferStream.reset();
    d->_deferCreated = false;
    // Not snapshotted here: the objects this load owes work to do not all
    // exist yet. The first slice takes it, once the load has let go.
    d->_deferCreate.clear();
    d->_deferFinish.clear();
    d->_deferSlices = d->_deferBuilt = 0;
    d->_deferSpent = FC_DURATION(0);
    d->_deferReadTime = d->_deferFinishTime = FC_DURATION(0);
    d->_deferSweepTime = d->_deferModeTime = FC_DURATION(0);
    ViewProvider::VisualBuildTime = ViewProvider::VisualMeshTime = FC_DURATION(0);
    ViewProvider::VisualBuildCount = 0;

    // The open is about to claim the application's input filter, and it
    // pumps events while it holds it -- so the regime that lets the pointer
    // through has to be in place before the first of them, not after. Named
    // explicitly because App has not set the Restoring bit yet.
    Application::Instance->refreshLiveLoad(&doc);
}

void Document::slotFinishRestoreObject(const App::DocumentObject &obj) {
    auto vpd = Base::freecad_dynamic_cast<ViewProviderDocumentObject>(getViewProvider(&obj));
    if(vpd) {
        FC_TIME_INIT(t);
        vpd->setStatus(Gui::isRestoring,false);
        vpd->finishRestoring();
        FC_DURATION_PLUS(d->_restoreVpTime, t);
        if(!vpd->canAddToSceneGraph())
            toggleInSceneGraph(vpd);
        else if (vpd->Visibility.getValue())
            vpd->setModeSwitch();
        FC_DURATION_PLUS(d->_restoreSceneTime, t);
        ++d->_restoreVpCount;
    }
}

void Document::slotFinishRestoreDocument(const App::Document& doc)
{
    if (d->_pcDocument != &doc)
        return;

    FC_TIME_INIT(t);
    // With the view providers parked there is nothing to refresh yet; the
    // drain runs this same refresh once, after the last of them exists.
    if (!d->_deferVPs) {
        slotFinishImportObjects(doc.getObjects());
        // No drain to carry the serve phase (shape parking and the view
        // provider drain are separate preferences, and either can be off
        // while the other is on), so start one for the entries alone.
        if (doc.hasDeferredFiles())
            scheduleDeferredRestore();
    }
    // The two Gui costs a load carries inside App's 'after' stage: the
    // per-object finishRestoring() calls that ran as the objects were
    // signalled, and this showable/children refresh over the whole document.
    FC_LOG("restore " << doc.getName() << " gui: " << d->_restoreVpCount
            << " view providers " << d->_restoreVpTime.count()
            << "s (visual build " << ViewProvider::VisualBuildCount << '/'
            << ViewProvider::VisualBuildTime.count() << "s, of which mesh "
            << ViewProvider::VisualMeshTime.count() << "s)"
            << ", scene " << d->_restoreSceneTime.count()
            << "s, refresh " << Base::GetDuration(t).count() << 's');
    // The share of App's create pass that slotNewObject is; reads against
    // the [addObject Ns] split on the App restore line.
    FC_LOG("restore " << doc.getName() << " gui new objects: instantiate "
            << d->_newObjInstTime.count() << "s, attach "
            << d->_newObjAttachTime.count() << "s, update "
            << d->_newObjUpdateTime.count() << "s, views "
            << d->_newObjViewTime.count() << "s, announce "
            << d->_newObjAnnounceTime.count() << 's');

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
        // With saved split view layouts the extra views are created
        // bare; applyViewAreaLayouts below places them into cells.
        bool useLayouts = !d->_viewAreaLayouts.empty()
            && ViewParams::getUseViewArea();
        while(views.size() < d->_savedViews.size()) {
            // Restore never consults the placement policy (docs/
            // ViewPlacement.md sec 3.3): without saved layouts the
            // extra views open as their own tabs, as they were saved.
            auto view3D = createView3D();
            if (view3D && !useLayouts)
                ViewPlacement::placeTab(view3D, this);
            views.push_back(view3D);
        }

        size_t i=0;
        // The LEGACY on-top store (docs/CoinRetirement.md 5.12): "<view
        // id>:<name>.<subname>" strings on the APP document. Split back
        // into per-view path lists here, applied to each view below,
        // and the property is dropped so the next save writes only the
        // views' own OnTopObjects.
        std::map<int, std::vector<std::string>> onTopObjs;
        if (auto prop = d->getOnTopProperty(getDocument())) {
            for (const auto &entry : prop->getValues()) {
                const auto colon = entry.find(':');
                if (colon == std::string::npos)
                    continue;
                try {
                    const int id = std::stoi(entry.substr(0, colon));
                    onTopObjs[id].push_back(entry.substr(colon + 1));
                } catch (const std::exception &) {
                    continue;
                }
            }
            getDocument()->removeDynamicProperty("OnTopObjects");
        }

        // Names first, before anything is restored into a view. A view
        // created for this load was handed an auto name out of the same
        // "View<n>" pool, so a name coming back from the file can be held
        // by the wrong view; it is taken from whoever has it, and every
        // view left nameless is given a free one at the end.
        size_t named = 0;
        for (auto v : views) {
            if (named == d->_savedViews.size())
                break;
            auto &info = d->_savedViews[named++];
            auto it = d->_view3DNames.find(info.id);
            if (it == d->_view3DNames.end() || it->second.empty())
                continue;
            for (auto other : getViews()) {
                if (other != v && other->getPersistentName() == it->second)
                    other->setPersistentName(std::string());
            }
            v->setPersistentName(it->second);
        }
        for (auto v : getViews()) {
            if (v->getPersistentName().empty())
                v->setPersistentName(uniqueViewName(v));
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
            // The legacy store fills in only where the view's own
            // property carried nothing: a file written before the move
            // has no OnTopObjects on the view at all, and one written
            // after has the authoritative copy. Applied after Restore
            // for that reason -- before it, the view's own (possibly
            // empty) value would overwrite what was just migrated.
            auto it = onTopObjs.find(info.id);
            if (it != onTopObjs.end() && view->OnTopObjects.getSize() == 0)
                view->OnTopObjects.setValues(std::move(it->second));
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

        if (useLayouts && !d->_deferVPs) {
            // With parked view providers the object views (TechDraw
            // pages) cannot materialize yet; finishDeferredRestore
            // applies the layouts after the drain.
            applyViewAreaLayouts(views);
            d->_viewAreaLayouts.clear();
        }
        else if (!useLayouts)
            d->_viewAreaLayouts.clear();
    }
    else
        d->_viewAreaLayouts.clear();

    // reset modified flag
    setModified(doc.testStatus(App::Document::LinkStampChanged));

    // The load is out of the way; hand the parked view providers to the
    // event loop. Also with nothing parked: the drain's finish pass is
    // what defaults the objects the file carried no record for.
    if (d->_deferVPs)
        scheduleDeferredRestore();

    // With nothing parked this is where the load ends, so the claim on the
    // input has to be given back here or it is never given back at all.
    // With view providers parked it is not the end, and refreshLiveLoad()
    // reads that off the drain rather than being told.
    Application::Instance->refreshLiveLoad();
}

void Document::applyViewAreaLayouts(const std::list<MDIView*> &views)
{
    std::vector<MDIView*> ordered(views.begin(), views.end());
    std::set<MDIView*> used;
    std::set<ViewArea*> donors;

    // A 3D-view leaf: by persistent name (N:<name>) as saved now, or by
    // save order (L<i>) as files saved before views had names carry.
    auto resolve3D = [&ordered](const std::string &token) -> MDIView* {
        if (token.compare(0, 2, "N:") == 0) {
            for (auto v : ordered) {
                if (v->getPersistentName() == token.substr(2))
                    return v;
            }
        }
        else if (token.size() > 1 && token[0] == 'L') {
            size_t idx = strtoul(token.c_str() + 1, nullptr, 10);
            if (idx < ordered.size())
                return ordered[idx];
        }
        return nullptr;
    };

    for (const auto &entry : d->_viewAreaLayouts) {
        const std::string &layout = entry.first;
        // A single-leaf layout whose view is already hosted alone in a
        // container (the default hosting) needs no rebuild.
        if (layout.find('{') == std::string::npos) {
            MDIView *lone = resolve3D(layout);
            if (lone && !used.count(lone)) {
                if (auto host = ViewArea::areaOf(lone)) {
                    if (host->cellCount() == 1) {
                        used.insert(lone);
                        continue;
                    }
                }
            }
        }
        auto area = new ViewArea(this, getMainWindow());
        QString title = QStringLiteral("%1 : %2[*]")
            .arg(QString::fromUtf8(getDocument()->Label.getValue()))
            .arg(d->_iWinCount++);
        area->setWindowTitle(title);

        bool ok = area->applyLayout(layout,
                [&](const std::string &token) -> MDIView* {
            MDIView *view = resolve3D(token);
            if (!view && token.compare(0, 2, "O:") == 0) {
                auto obj = getDocument()->getObject(token.c_str() + 2);
                if (obj) {
                    if (auto vp = getViewProvider(obj)) {
                        view = vp->getMDIView();
                        if (!view) {
                            vp->show();
                            view = vp->getMDIView();
                        }
                    }
                }
            }
            if (!view || used.count(view))
                return nullptr;
            if (auto donor = ViewArea::areaOf(view))
                donors.insert(donor);
            used.insert(view);
            return view;
        });
        if (!ok) {
            area->deleteSelf();
            continue;
        }
        if (MDIView *front = area->activeSubView()) {
            if (front != area)
                area->setWindowIcon(front->windowIcon());
        }
        getMainWindow()->addWindow(area);

        // A maximize saved with the layout comes back maximized; the
        // resolver here is read-only (the leaf already resolved and is
        // hosted in a cell of this very container). Deferred to the
        // event loop: toggleMaximizeCell records the pre-maximize
        // splitter state to restore later, and recording it before the
        // fresh container has laid out captures degenerate sizes --
        // un-maximizing then lost the saved proportions to a 50/50.
        if (!entry.second.empty()) {
            MDIView *view = resolve3D(entry.second);
            if (!view && entry.second.compare(0, 2, "O:") == 0) {
                if (auto obj = getDocument()->getObject(
                            entry.second.c_str() + 2)) {
                    if (auto vp = getViewProvider(obj))
                        view = vp->getMDIView();
                }
            }
            if (view) {
                if (auto cell = area->cellOf(view))
                    area->setPendingMaximize(cell);
            }
        }
    }

    // A donor container whose only view moved into a rebuilt layout is
    // an empty shell now.
    for (auto donor : donors) {
        bool any = false;
        for (auto cell : donor->cells()) {
            if (cell->childView()) {
                any = true;
                break;
            }
        }
        if (!any)
            donor->deleteSelf();
    }

    // Bare-created views no layout referenced still need a home.
    for (auto view : ordered) {
        if (!used.count(view) && !view->parentWidget())
            getMainWindow()->addWindow(view);
    }
}

void Document::slotShowHidden(const App::Document& doc)
{
    if (d->_pcDocument != &doc)
        return;

    Application::Instance->signalShowHidden(*this);
}

bool Document::isRestoringViewProviders() const
{
    return d->_deferVPs;
}

void Document::restoreCapturedViewProvider(const std::string &xml,
        Base::XMLReader &archiveReader)
{
    // Inside the load, so App::Document::Restoring is already giving the
    // attach its restore semantics; only the new-object skip has to know
    // this creation is deliberate.
    Base::StateLocker applying(d->_deferApplying);
    std::istringstream str(xml);
    Base::XMLReader reader("GuiDocument.xml", str);
    reader.FileVersion = archiveReader.FileVersion;
    reader.DocumentSchema = archiveReader.DocumentSchema;
    reader.ProgramVersion = archiveReader.ProgramVersion;
    reader.readElement("ViewProvider");
    auto obj = d->_pcDocument->getObject(reader.getAttribute("name",""));
    if (obj && !getViewProvider(obj))
        slotNewObject(*obj);
    if (auto vpd = obj ? Base::freecad_dynamic_cast<ViewProviderDocumentObject>(
                getViewProvider(obj)) : nullptr) {
        // finishRestoring() arrives with signalFinishRestoreObject, like
        // any eagerly restored view provider.
        vpd->startRestoring();
        vpd->setStatus(Gui::isRestoring, true);
    }
    readObject(reader);
    // The files the restore just asked for belong to the archive walk.
    for (const auto &e : reader.getFileList())
        archiveReader.addFile(e.FileName, e.Object);
}

void Document::scheduleDeferredRestore(int delayMs)
{
    if (d->_deferScheduled)
        return;
    d->_deferScheduled = true;
    // Resolved by name when the timer fires: the document may be gone by
    // then, and a name that no longer answers means nothing to do.
    std::string name = d->_pcDocument->getName();
    QTimer::singleShot(delayMs, QCoreApplication::instance(), [name]() {
        if (auto doc = Application::Instance->getDocument(name.c_str()))
            doc->runDeferredRestoreSlice();
    });
}

void Document::runDeferredRestoreSlice()
{
    d->_deferScheduled = false;
    // A slice can run Python (a ViewProviderPythonFeature's attach), and
    // whatever that does must not re-enter the reader mid-element.
    if (d->_deferApplying)
        return;
    if (!d->_deferVPs) {
        // The parked shape entries outlive the view provider drain, so the
        // serve phase has to as well: a document whose visuals are all built
        // -- or which never had any to build -- gets no slice from the
        // visual queue, and would fault its entries in one at a time while
        // holding the archive index open for as long as it stays loaded.
        // Its own timer serves them, rather than some object of it having to
        // reach the head of a queue shared with every other document.
        runDeferredServeSlice();
        return;
    }
    // Not while this document is inside another load (a partial reload can
    // follow the open that parked these); ask again shortly.
    if (d->_pcDocument->testStatus(App::Document::Restoring)) {
        scheduleDeferredRestore(100);
        return;
    }

    const double budget =
        std::max(1L, Gui::RenderParams::getProgressiveLoadBudgetMS()) / 1000.0;
    auto start = std::chrono::high_resolution_clock::now();
    auto elapsed = [&start]() {
        return std::chrono::duration<double>(
                std::chrono::high_resolution_clock::now() - start);
    };
    ++d->_deferSlices;

    // The set of objects this drain owes work to, fixed here: everything
    // the load created (afterRestore's additions included) exists by now,
    // and from here on the document is live between slices -- see
    // DrainCursor.h for why neither phase may index the object array.
    if (!d->_deferCreate.ready()) {
        // Phase one in the document's own order: its eager counterpart is
        // slotNewObject() riding the create pass, which is that order.
        d->_deferCreate.snapshot(d->_pcDocument);
        // Phase three is not. Eagerly, finishRestoring() rides
        // signalFinishRestoreObject, which App::Document::afterRestore()
        // emits from its *dependency-sorted* walk -- so an object's view
        // provider is always finished before that of anything depending on
        // it. Finish handlers rely on it: ViewProviderLink's reaches the
        // linked objects' view providers, ViewProviderPart's applyColors()
        // walks its children's. Creation order agrees with dependency order
        // often enough to hide this, but not always -- what an
        // afterRestore() created (an App::Part's Origin above all) lands at
        // the end of the object array no matter what depends on it.
        d->_deferFinish.snapshot(d->_pcDocument,
                App::Document::getDependencyList(d->_pcDocument->getObjects(),
                                                 App::Document::DepSort));
    }
    if (!d->_deferSeq) {
        const std::size_t total = d->_deferCreate.size()
            + std::size_t(d->_deferCount) + d->_deferFinish.size();
        if (total)
            d->_deferSeq = std::make_unique<Base::SequencerLauncher>(
                    "Restoring view providers...", total,
                    Base::SequencerLauncher::KeepInteractive);
    }

    // Restore semantics for everything a slice builds: attach() must not
    // overwrite the visibility the object restored with, the property
    // changes are a record being read rather than edits, and a fresh
    // visual parks itself in the progressive-load queue instead of
    // building inside the slice.
    Base::StateLocker applying(d->_deferApplying);
    d->_pcDocument->setStatus(App::Document::Restoring, true);
    // The global answer too, not just this document's bit: what keys off
    // isAnyRestoring() -- the tree's status pass, the origin group's
    // relocation, the link machinery -- treated these same properties as a
    // restore when they were read eagerly, and each of them charges real
    // time per property when told otherwise.
    App::Document::RestoringScopeGuard restoringScope;
    // And the half that isAnyRestoring() cannot say. Phase three drops the
    // view provider's restore status before sweeping its properties, because
    // with the guards on the handlers render nothing at all -- so they run
    // here as they never ran eagerly: past afterRestore()'s purge, where a
    // handler that writes back while rendering leaves the document needing a
    // recompute merely because it was opened. The whole slice is inside the
    // scope, not just that sweep: every phase of the drain is the file's own
    // record being replayed, and none of it is an edit.
    App::Document::RestoreDrainGuard drainScope(d->_pcDocument);
    try {
        // Phase zero: the parked shape archive entries
        // (docs/DocumentLoad.md §14). Serving them before any view
        // provider work reproduces the eager load's order -- every
        // record applied below reads a shape that is already there.
        // Left to the per-access fault-in instead, the records below
        // pull the shapes one at a time from inside the property
        // applications, which turns this drain into minutes.
        if (d->_pcDocument->serveDeferredFiles(budget)) {
            d->_pcDocument->setStatus(App::Document::Restoring, false);
            d->_deferSpent += elapsed();
            scheduleDeferredRestore();
            return;
        }
        // Phase one: every object gets its view provider before any of
        // them gets its record.
        if (!d->_deferCreated) {
            Base::StateLocker phase1(d->_deferPhase1);
            while (auto obj = d->_deferCreate.next(d->_pcDocument)) {
                if (d->_deferSeq)
                    d->_deferSeq->next();
                if (!getViewProvider(obj)) {
                    slotNewObject(*obj);
                    if (auto vpd = Base::freecad_dynamic_cast<
                            ViewProviderDocumentObject>(getViewProvider(obj))) {
                        vpd->startRestoring();
                        vpd->setStatus(Gui::isRestoring, true);
                    }
                }
                if (elapsed().count() >= budget)
                    break;
            }
            if (!d->_deferCreate.done()) {
                d->_pcDocument->setStatus(App::Document::Restoring, false);
                d->_deferSpent += elapsed();
                scheduleDeferredRestore();
                return;
            }
            d->_deferCreated = true;
        }
        if (d->_deferCount && !d->_deferReader) {
            d->_deferStream = std::make_unique<std::istringstream>(
                    std::move(d->_deferBuf));
            d->_deferBuf.clear();
            d->_deferReader = std::make_unique<Base::XMLReader>(
                    "GuiDocument.xml", *d->_deferStream);
            d->_deferReader->FileVersion = d->_deferFileVersion;
            d->_deferReader->DocumentSchema = d->_deferDocSchema;
            d->_deferReader->ProgramVersion = d->_deferProgramVersion;
            d->_deferReader->readElement("ViewProviderData");
        }
        while (d->_deferCount) {
            --d->_deferCount;
            if (d->_deferSeq)
                d->_deferSeq->next();
            auto &xmlReader = *d->_deferReader;
            int guard;
            xmlReader.readElement("ViewProvider",&guard);
            auto obj = d->_pcDocument->getObject(xmlReader.getAttribute("name",""));
            ViewProviderDocumentObject *vpd = nullptr;
            if (obj) {
                if (!getViewProvider(obj))
                    slotNewObject(*obj);
                vpd = Base::freecad_dynamic_cast<ViewProviderDocumentObject>(
                        getViewProvider(obj));
            }
            if (vpd && !vpd->testStatus(Gui::isRestoring)) {
                vpd->startRestoring();
                vpd->setStatus(Gui::isRestoring, true);
            }
            FC_TIME_INIT(tSplit);
            readObject(xmlReader);
            {
                auto dRead = Base::GetDuration(tSplit);
                d->_deferReadTime += dRead;
                // Self-selecting: whatever class is dragging the drain out
                // names itself here, with the time it took.
                if (dRead.count() > 0.005)
                    FC_LOG("slow deferred restore " << (obj?obj->getFullName():"?")
                            << " (" << (vpd?vpd->getTypeId().getName():"?")
                            << "): " << dRead.count() << 's');
            }
            xmlReader.readEndElement("ViewProvider",&guard);
            ++d->_deferBuilt;
            if (elapsed().count() >= budget)
                break;
        }
        // Phase three, once every record is in: the property sweep phase
        // one held back, then finishRestoring -- for all view providers,
        // in one pass, exactly as the eager load ran them. Not per
        // element inside phase two: a link element finished against a
        // link whose own record is still parked settles on the record's
        // stale visibility, which the link machinery only corrects once
        // the whole web is restored.
        if (!d->_deferCount) {
            d->_deferReader.reset();
            d->_deferStream.reset();
            while (auto obj = d->_deferFinish.next(d->_pcDocument)) {
                if (d->_deferSeq)
                    d->_deferSeq->next();
                auto vpd = Base::freecad_dynamic_cast<ViewProviderDocumentObject>(
                        getViewProvider(obj));
                bool fresh = false;
                if (!vpd) {
                    // Never in GuiDocument.xml: a file saved without a Gui
                    // document, or an object afterRestore created -- an
                    // App::Part's origin above all.
                    slotNewObject(*obj);
                    vpd = Base::freecad_dynamic_cast<ViewProviderDocumentObject>(
                            getViewProvider(obj));
                    fresh = true;
                }
                if (vpd && (fresh || vpd->testStatus(Gui::isRestoring))) {
                    FC_TIME_INIT(tFinish);
                    bool held = vpd->testStatus(Gui::isRestoring);
                    // isRestoring drops before the sweep: the handlers take
                    // their slow restore branches otherwise, and with every
                    // record already in there is nothing left to protect.
                    vpd->setStatus(Gui::isRestoring, false);
                    if (held)
                        vpd->updateView();
                    auto dSweep = Base::GetDuration(tFinish);
                    d->_deferSweepTime += dSweep;
                    vpd->finishRestoring();
                    auto dRest = Base::GetDuration(tFinish);
                    if (!vpd->canAddToSceneGraph())
                        toggleInSceneGraph(vpd);
                    else if (vpd->Visibility.getValue())
                        vpd->setModeSwitch();
                    auto dMode = Base::GetDuration(tFinish);
                    d->_deferModeTime += dMode;
                    d->_deferFinishTime += dSweep + dRest + dMode;
                    if (dSweep + dRest + dMode > FC_DURATION(0.005))
                        FC_LOG("slow deferred finish " << obj->getFullName()
                                << " (" << vpd->getTypeId().getName()
                                << "): sweep " << dSweep.count()
                                << "s + finish " << dRest.count()
                                << "s + mode " << dMode.count() << 's');
                }
                if (elapsed().count() >= budget)
                    break;
            }
            if (!d->_deferFinish.done()) {
                d->_pcDocument->setStatus(App::Document::Restoring, false);
                d->_deferSpent += elapsed();
                if ((d->_deferSlices % 50) == 0)
                    FC_LOG("progressive restore " << d->_pcDocument->getName()
                            << ": finishing " << d->_deferFinish.position()
                            << " of " << d->_deferFinish.size() << ", "
                            << d->_deferSlices
                            << " slices " << d->_deferSpent.count()
                            << "s (sweep " << d->_deferSweepTime.count()
                            << "s, mode " << d->_deferModeTime.count()
                            << "s, of finish " << d->_deferFinishTime.count()
                            << "s)");
                scheduleDeferredRestore();
                return;
            }
        }
    }
    // A record that cannot be read is given up on -- but only the record.
    // The parked buffer goes, and phase three still runs over every object,
    // which is what "falls back to defaults" has to mean: a view provider
    // abandoned mid-drain keeps Gui::isRestoring set, never gets its mode
    // switch, and shows nothing at all.
    catch (Base::Exception &e) {
        e.ReportException();
        FC_ERR("restore " << d->_pcDocument->getName()
                << ": deferred view provider restore aborted, "
                << d->_deferCount << " objects fall back to defaults");
        d->_deferCount = 0;
        d->_deferReader.reset();
        d->_deferStream.reset();
    }
    catch (const std::exception &e) {
        FC_ERR("restore " << d->_pcDocument->getName()
                << ": deferred view provider restore aborted (" << e.what()
                << "), " << d->_deferCount << " objects fall back to defaults");
        d->_deferCount = 0;
        d->_deferReader.reset();
        d->_deferStream.reset();
    }
    d->_pcDocument->setStatus(App::Document::Restoring, false);
    d->_deferSpent += elapsed();

    // Phase three is unfinished here only when a phase above threw out of
    // its slice; the normal budget exit reports and reschedules in place.
    if (d->_deferCount || !d->_deferFinish.done()) {
        // A load this size drains for a while; say how it is going, and
        // with the same split the finish line reports, so a stall in here
        // names its stage.
        if (d->_deferCount && (d->_deferSlices % 50) == 0)
            FC_LOG("progressive restore " << d->_pcDocument->getName() << ": "
                    << d->_deferBuilt << " view providers, " << d->_deferCount
                    << " left, " << d->_deferSlices << " slices "
                    << d->_deferSpent.count() << "s (new object "
                    << (d->_newObjInstTime + d->_newObjAttachTime
                        + d->_newObjUpdateTime + d->_newObjViewTime
                        + d->_newObjAnnounceTime).count()
                    << "s, restore " << d->_deferReadTime.count()
                    << "s of which property "
                    << App::PropertyContainer::restoreStats.total.count()
                    << "s/value "
                    << App::PropertyContainer::restoreStats.value.count()
                    << "s, finish " << d->_deferFinishTime.count() << "s)");
        scheduleDeferredRestore();
        return;
    }
    finishDeferredRestore();
}

void Document::runDeferredServeSlice()
{
    if (!d->_pcDocument->hasDeferredFiles())
        return;
    // Not while a load is writing into this document: the entries it parks
    // are still arriving, and a partial reload may yet replace them.
    if (d->_pcDocument->testStatus(App::Document::Restoring)) {
        scheduleDeferredRestore(100);
        return;
    }
    const double budget =
        std::max(1L, Gui::RenderParams::getProgressiveLoadBudgetMS()) / 1000.0;
    // Deliberately without the drain's restore semantics: phase zero serves
    // entries a parked record is about to read back, while this serves them
    // into a document that is fully live -- an ordinary property change
    // arriving late, which is exactly how its consumers must see it.
    if (d->_pcDocument->serveDeferredFiles(budget))
        scheduleDeferredRestore();
}

void Document::finishDeferredRestore()
{
    d->_deferSeq.reset();
    d->_deferReader.reset();
    d->_deferStream.reset();
    d->_deferBuf.clear();
    d->_deferBuf.shrink_to_fit();
    d->_deferCreate.clear();
    d->_deferFinish.clear();
    d->_restoreDefaults.clear();
    d->_deferVPs = false;
    // One of the two phases that outlive the blocking open; the visual drain
    // reports the other through Application::setBuildingVisuals().
    Application::Instance->refreshLiveLoad();

    // The showable/children refresh the load skipped, over the full set,
    // and the active-object highlight the tree never got.
    slotFinishImportObjects(d->_pcDocument->getObjects());
    if (auto act = d->_pcDocument->getActiveObject()) {
        auto vpd = Base::freecad_dynamic_cast<ViewProviderDocumentObject>(
                getViewProvider(act));
        if (vpd)
            signalActivatedObject(*vpd);
    }

    // What the drain rebuilt is the file's own record, not an edit; leave
    // the document as slotFinishRestoreDocument left it.
    setModified(d->_pcDocument->testStatus(App::Document::LinkStampChanged));

    // The App side of the same statement, and the one that names names: what
    // a handler tried to write into the document while the drain replayed it
    // (App::Document::RestoreDrainGuard). Suppressed, so nothing here is the
    // user's problem -- but a handler writing on a render is a bug of its
    // own, and this is what points at it without a debugger.
    const auto &drain = d->_pcDocument->getRestoreDrainReport();
    if (drain.count) {
        std::string names;
        for (const auto &name : drain.names)
            names += (names.empty() ? "" : ", ") + name;
        if (drain.truncated)
            names += ", ...";
        FC_WARN("progressive restore " << d->_pcDocument->getName() << ": "
                << drain.count << " document changes suppressed while replaying"
                   " the view providers (" << names << ')');
        d->_pcDocument->clearRestoreDrainReport();
    }

    // Split view layouts wait for the drain: only now can an object
    // view (a TechDraw page) be materialized and placed into its cell.
    if (!d->_viewAreaLayouts.empty()) {
        auto views = getMDIViewsOfType(View3DInventor::getClassTypeId());
        applyViewAreaLayouts(views);
        d->_viewAreaLayouts.clear();
    }

    // Whatever the drain's phase zero did not get through is this document's
    // own business from here on -- see runDeferredServeSlice().
    if (d->_pcDocument->hasDeferredFiles())
        scheduleDeferredRestore();

    FC_LOG("progressive restore " << d->_pcDocument->getName() << ": "
            << d->_deferBuilt << " view providers in " << d->_deferSlices
            << " slices, " << d->_deferSpent.count() << "s (instantiate "
            << d->_newObjInstTime.count() << "s, attach "
            << d->_newObjAttachTime.count() << "s, update "
            << d->_newObjUpdateTime.count() << "s, views "
            << d->_newObjViewTime.count() << "s, announce "
            << d->_newObjAnnounceTime.count() << "s, restore "
            << d->_deferReadTime.count() << "s, finish "
            << d->_deferFinishTime.count() << "s)");
}

void Document::flushDeferredRestore()
{
    // Whoever asks needs every view provider to exist right now. During a
    // restore the parked record is still being written, and inside a drain
    // slice the reader is mid-element; both wait for the drain like
    // everyone else.
    while (d->_deferVPs && !d->_deferApplying
            && !d->_pcDocument->testStatus(App::Document::Restoring))
        runDeferredRestoreSlice();
}

void Document::buildDefaults(Base::Writer &writer,
        std::map<std::string, App::SharedDefaults> &defaults) const
{
    // One gate, the same one the App side answers to: the resolved schema.
    // The user chooses the compact format per document in the save dialog;
    // no preference of this machine outranks what that document promised.
    if (writer.getSchemaVersion() < 5)
        return;

    // A default block is one class's whole property set, so it only pays for
    // itself once enough view providers can leave that set out. Two roughly
    // break even; below that a document would come out larger than if nothing
    // had been shared at all.
    const std::size_t minInstances = 3;

    std::map<std::string, std::size_t> counts;
    for (const auto &v : d->_ViewProviderMap)
        ++counts[v.second->getTypeId().getName()];
    for (const auto &v : counts) {
        if (v.second < minInstances)
            continue;
        // The stand-in lives exactly as long as this recording. What the
        // view providers compare against, and what the file will carry, are
        // the bytes SharedDefaults took down.
        if (auto proto = makeDefaultViewProvider(v.first.c_str())) {
            App::SharedDefaults record;
            record.build(*proto, writer);
            if (!record.empty())
                defaults.emplace(v.first, std::move(record));
        }
    }
}

void Document::saveDefaults(Base::Writer &writer,
        const std::map<std::string, App::SharedDefaults> &defaults) const
{
    if (defaults.empty())
        return;

    writer.Stream() << writer.ind() << '<' << FC_ELEM_DEFAULTS << " Count=\""
                    << defaults.size() << "\">\n";
    writer.incInd();
    for (const auto &v : defaults) {
        writer.Stream() << writer.ind() << '<' << FC_ELEM_DEFAULT << " type=\""
                        << v.first << "\">\n";
        // Properties only, and only the recorded ones -- eligibility was
        // settled when the record was built (mustSave already kept
        // Visibility and DisplayMode out), and the bytes going out here are
        // the same bytes every elision was decided against.
        v.second.save(writer);
        writer.Stream() << writer.ind() << "</" << FC_ELEM_DEFAULT << ">\n";
    }
    writer.decInd();
    writer.Stream() << writer.ind() << "</" << FC_ELEM_DEFAULTS << ">\n";
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
void Document::snapshotOnTopObjects()
{
    // Every mutation of a viewer's on-top group lands here through
    // signalOnTopObject, and puts the view's OnTopObjects property back
    // in step (docs/CoinRetirement.md 5.12). The signal does not say
    // WHICH view changed, so all of them are compared -- each set is a
    // handful of entries, and an unchanged list writes nothing.
    //
    // User1 is the direction latch shared with View3DInventor::
    // onChanged: a view applying its property is mutating the group
    // this would snapshot, and re-writing the list mid-apply would
    // truncate it to whatever has been applied so far.
    foreachView<View3DInventor>([](View3DInventor* view) {
        auto viewer = view->getViewer();
        if (!viewer || view->OnTopObjects.testStatus(App::Property::User1))
            return;
        std::vector<std::string> values;
        values.reserve(viewer->getObjectsOnTop().size());
        for (const auto &objT : viewer->getObjectsOnTop())
            values.push_back(objT.getSubNameNoElement(true));
        if (values == view->OnTopObjects.getValues())
            return;
        Base::ObjectStatusLocker<App::Property::Status, App::Property> guard(
                App::Property::User1, &view->OnTopObjects);
        view->OnTopObjects.setValues(std::move(values));
    });
}

void Document::SaveDocFile (Base::Writer &writer) const
{
    writer.Stream() << "<?xml version='1.0' encoding='utf-8'?>\n"
                    << "<!--\n"
                    << " FreeCAD Document, see http://www.freecad.org for more information...\n"
                    << "-->\n";

    const std::string &name = writer.getCurrentFileName();
    auto entry = d->_splitXmlEntries.find(name);
    if(entry != d->_splitXmlEntries.end()
            || boost::ends_with(name,FC_XML_GUI_POSTFIX)) {
        static const std::size_t plen = std::strlen(FC_XML_GUI_POSTFIX);
        std::string objName = entry != d->_splitXmlEntries.end()
            ? std::string(entry->second->getNameInDocument())
            : name.substr(0,name.size()-plen);
        auto obj = entry != d->_splitXmlEntries.end()
            ? entry->second
            : getDocument()->getObject(objName.c_str());
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
    if (writer.isSplitXML())
        writer.Stream() << ' ' << FC_ATTR_SPLIT_FILES << "=\"1\"";

    if (!TreeWidget::saveDocumentItem(this, writer, FC_ATTR_TREE_EXPANSION))
        writer.Stream() << ">\n";

    if(writer.isSplitXML()) {
        d->_splitXmlEntries.clear();
        writer.incInd();
        writer.Stream() << writer.ind() << "<ViewProviderFiles Count=\""
                        << d->_ViewProviderMap.size() << "\">\n";
        writer.incInd();
        for(const auto &v : d->_ViewProviderMap) {
            // What the writer settled on, which is not always what was asked
            // for: it makes the name one a file system will take. Written
            // down, because the reader must take the name from here rather
            // than work it out again -- and kept, so that SaveDocFile answers
            // with the object rather than reading its name back out of a file
            // name.
            const std::string& entry = writer.addFile(
                    std::string(v.first->getNameInDocument())+FC_XML_GUI_POSTFIX,this);
            d->_splitXmlEntries[entry] = v.first;
            writer.Stream() << writer.ind() << "<File obj=\""
                            << encodeAttribute(v.first->getNameInDocument())
                            << "\" name=\"" << encodeAttribute(entry) << "\"/>\n";
        }
        writer.decInd();
        writer.Stream() << writer.ind() << "</ViewProviderFiles>\n";
        writer.decInd();
    } else {
        writer.incInd(); 

        std::map<const App::DocumentObject*,ViewProviderDocumentObject*>::const_iterator it;

        // View providers of one class are nearly identical: a colour here, a
        // display mode there, and everything else is what the constructor
        // gave them. Write that constructor state once per class and let each
        // view provider save only its difference from it -- on a large
        // assembly that is the difference between a view file twice the size
        // of the document and one a small fraction of it, and the load has
        // that many fewer properties to restore. The defaults are written
        // out, not implied, so the document still looks the same opened on a
        // machine whose preferences differ from the author's.
        std::map<std::string, App::SharedDefaults> defaults;
        buildDefaults(writer, defaults);

        // writing the view provider names itself
        writer.Stream() << writer.ind() << "<ViewProviderData Count=\""
                        << d->_ViewProviderMap.size() << '"';
        if (!defaults.empty())
            writer.Stream() << ' ' << FC_ATTR_DEFAULTS << "=\"" << defaults.size() << '"';
        writer.Stream() << ">\n";

        writer.incInd(); // indentation for 'ViewProvider name'
        saveDefaults(writer, defaults);
        auto elided = App::PropertyContainer::savedDefaults;
        // Point every view provider at its class record for the duration of
        // the write, and at nothing again after it -- the records do not
        // outlive this call.
        auto pointAtDefaults = [&](bool on) {
            for (const auto &v : d->_ViewProviderMap) {
                auto def = defaults.find(v.second->getTypeId().getName());
                v.second->setSaveDefaults(
                        on && def != defaults.end() ? &def->second : nullptr);
            }
        };
        pointAtDefaults(true);
        try {
            for(it = d->_ViewProviderMap.begin(); it != d->_ViewProviderMap.end(); ++it)
                writeObject(writer,it->first, it->second);
        }
        catch (...) {
            pointAtDefaults(false);
            throw;
        }
        pointAtDefaults(false);
        FC_LOG("save " << getDocument()->getName() << " gui: "
                << d->_ViewProviderMap.size() << " view providers, "
                << defaults.size() << " class defaults, "
                << (App::PropertyContainer::savedDefaults - elided)
                << " properties left out");
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

    // Split view container layouts: leaves name a 3D view by its
    // persistent name (N:<name>, see Gui::BaseView) or an
    // object-provided view (a TechDraw page) by its object name
    // (O:<name>) -- docs/SplitViews.md sec 5.6. The reader still takes
    // the positional form (L<i>, the view's save order above) that
    // files saved before views had names carry.
    std::vector<std::pair<std::string, std::string>> areaLayouts;
    for (const auto & v : mdi) {
        auto area = qobject_cast<ViewArea*>(v);
        if (!area)
            continue;
        auto leafToken = [&](MDIView *child) -> std::string {
            for (size_t i = 0; i < view3Ds.size(); ++i) {
                if (view3Ds[i] != child)
                    continue;
                const std::string &name = child->getPersistentName();
                if (!name.empty())
                    return "N:" + name;
                return "L" + std::to_string(i);
            }
            // Object views name their object as the widget objectName
            // (MDIViewPage::setDocumentObject); prefer that -- a map
            // scan can hit a subsidiary provider first (a TechDraw
            // template reports its page's view too) whose show() could
            // not recreate the view on restore.
            QByteArray objName = child->objectName().toUtf8();
            if (!objName.isEmpty()) {
                if (auto obj = getDocument()->getObject(objName.constData())) {
                    auto vp = getViewProvider(obj);
                    if (vp && vp->getMDIView() == child)
                        return std::string("O:") + objName.constData();
                }
            }
            return {};
        };
        std::string layout = area->layoutString(leafToken);
        if (layout.empty())
            continue;
        // The maximized cell rides along as its leaf token; the layout
        // itself keeps the underlying proportions (preMaximizeSizes).
        std::string maximized;
        if (auto mc = area->maximizedCell()) {
            if (mc->childView())
                maximized = leafToken(mc->childView());
        }
        areaLayouts.emplace_back(std::move(layout), std::move(maximized));
    }

    writer.Stream() << writer.ind() << "<Camera";
    if(cameraInfo.size())
        writer.Stream() << " extra=\"" << cameraInfo.size()-1 << "\" id=\""
            << cameraInfo[0].id << "\" binding=\"" << cameraInfo[0].binding << "\""
            << " view3d=\"" << view3Ds.size() << "\""
            << " viewareas=\"" << areaLayouts.size() << "\"";
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

    // A view saves into a string that is embedded in GuiDocument.xml and
    // replayed from memory on restore (see slotFinishRestoreDocument), so a
    // view property cannot use the writer's separate-file channel — there is
    // no zip on either side of that string. Force the inline XML form, which
    // App::PropertyFileIncluded answers with base64 content the memory reader
    // can restore.
    //
    // Included files are the exception: the document's blob store has already
    // written their content as its own archive entries (the save-time collect
    // pass reaches views for exactly this reason), so passing the schema on
    // lets those properties store a hash instead of a copy of the file.
    Base::StringWriter stringWriter;
    stringWriter.setForceXML(4);
    stringWriter.setSchemaVersion(writer.getSchemaVersion());
    for (auto view : view3Ds) {
        // The name is the view's identity across the save; the id only
        // correlates this file's own <Camera>/<View3D> entries, and comes
        // from a counter the next session starts over.
        writer.Stream() << writer.ind() << "<View3D id=\"" << view->getID()
            << "\" name=\"" << encodeAttribute(view->getPersistentName()) << "\">";
        stringWriter.clear();
        view->Save(stringWriter);
        writer.beginCharStream() << '\n' << stringWriter.getString();
        writer.endCharStream() << '\n' << writer.ind() << "</View3D>\n";
    }

    for (const auto &layout : areaLayouts) {
        writer.Stream() << writer.ind() << "<ViewArea layout=\""
            << encodeAttribute(layout.first) << "\"";
        if (!layout.second.empty())
            writer.Stream() << " maximized=\""
                << encodeAttribute(layout.second) << "\"";
        writer.Stream() << "/>\n";
    }

    writer.decInd(); // indentation for camera settings

    writer.Stream() << "</Document>\n";
}

void Document::exportObjects(const std::vector<App::DocumentObject*>& obj, Base::Writer& writer)
{
    flushDeferredRestore();
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
    flushDeferredRestore();
    // We must create an XML parser to read from the input stream
    Base::XMLReader xmlReader(reader);
    xmlReader.readElement("Document");
    long scheme = xmlReader.getAttributeAsInteger("SchemaVersion");
    // Imported objects come out of someone else's document, and its version
    // is what says how to read their colours (Base::alphaIsOpacity).
    if (auto parent = reader.getParent())
        xmlReader.ProgramVersion = parent->ProgramVersion;

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

View3DInventor *Document::createView3D()
{
    std::list<MDIView*> theViews =
        this->getMDIViewsOfType(View3DInventor::getClassTypeId());
    {

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

        setModified(false);
        ViewProviderShaderBinding::onViewCreated(getDocument());
        return view3D;
    }
}

MDIView *Document::createView(const Base::Type& typeId)
{
    if (!typeId.isDerivedFrom(MDIView::getClassTypeId()))
        return nullptr;

    if (typeId == View3DInventor::getClassTypeId()) {
        // An additional view of a document that already shows one is
        // placed by the DocView policy (a split by default); the first
        // view is the Document category (a tab by default). See
        // docs/ViewPlacement.md.
        bool additional = !getMDIViews().empty();
        auto view3D = createView3D();
        if (!view3D)
            return nullptr;
        ViewPlacement::place(view3D,
                additional ? ViewPlacement::Category::DocView
                           : ViewPlacement::Category::Document, this);
        return view3D;
    }
    return nullptr;
}

Gui::MDIView* Document::cloneView(Gui::MDIView* oldview, bool transferEdit)
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

        // View provider editing: callers REPLACING the original view
        // move the active edit to the clone; a split keeping both
        // (ViewArea::cloneChildFor) leaves it where the user works --
        // the fresh cell must not steal the dragger out from under an
        // ongoing edit.
        if (transferEdit && d->_editViewProvider) {
            firstView->getViewer()->resetEditingViewProvider();
            view3D->getViewer()->setEditingViewProvider(d->_editViewProvider, d->_editMode);
        }

        ViewProviderShaderBinding::onViewCreated(getDocument());
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

std::string Document::uniqueViewName(const Gui::BaseView *except) const
{
    std::set<std::string> taken;
    for (auto view : d->baseViews) {
        if (view != except) {
            taken.insert(view->getPersistentName());
        }
    }
    for (auto view : d->passiveViews) {
        if (view != except) {
            taken.insert(view->getPersistentName());
        }
    }
    for (int n = 1;; ++n) {
        std::string name = "View" + std::to_string(n);
        if (taken.find(name) == taken.end()) {
            return name;
        }
    }
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

