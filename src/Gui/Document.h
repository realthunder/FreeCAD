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

#ifndef GUI_DOCUMENT_H
#define GUI_DOCUMENT_H

#include <list>
#include <map>
#include <memory>
#include <string>
#include <fastsignals/signal.h>
#include <QString>

#include <App/Document.h>
#include <Base/Persistence.h>
#include <Gui/TreeItemMode.h>

#include "MDIView.h"

class SoNode;
class SoPath;

namespace Base {
class Matrix4D;
}

namespace App {
class Document;
class DocumentObject;
class DocumentObjectGroup;
class SubObjectT;
class Property;
class Transaction;
}

namespace Gui {

class BaseView;
class MDIView;
class View3DInventor;
class ViewProvider;
class ViewerContext;
class ViewProviderDocumentObject;
class Application;
class DocumentPy;
class TransactionViewProvider;

/** The Gui Document
 *  This is the document on GUI level. Its main responsibility is keeping
 *  track off open windows for a document and warning on unsaved closes.
 *  All handled views on the document must inherit from MDIView
 *  @see App::Document
 *  @see MDIView
 *  @author Jürgen Riegel
 */
class GuiExport Document : public Base::Persistence
{
public:
    Document(App::Document* pcDocument, Application * app);
    ~Document() override;

protected:
    /** @name I/O of the document */
    //@{
    /// This slot is connected to the App::Document::signalNewObject(...)
    void slotNewObject(const App::DocumentObject&);
    void slotDeletedObject(const App::DocumentObject&);
    void slotChangedObject(const App::DocumentObject&, const App::Property&);
    void slotRelabelObject(const App::DocumentObject&);
    void slotTransactionAppend(const App::DocumentObject&, App::Transaction*);
    void slotTransactionRemove(const App::DocumentObject&, App::Transaction*);
    void slotActivatedObject(const App::DocumentObject&);
    void slotStartRestoreDocument(const App::Document&);
    void slotFinishRestoreDocument(const App::Document&);
    void slotUndoDocument(const App::Document&);
    void slotRedoDocument(const App::Document&);
    void slotShowHidden(const App::Document&);
    void slotFinishImportObjects(const std::vector<App::DocumentObject*> &);
    void slotFinishRestoreObject(const App::DocumentObject &obj);
    void slotRecomputed(const App::Document&, const std::vector<App::DocumentObject*> &);
    void slotSkipRecompute(const App::Document &doc, const std::vector<App::DocumentObject*> &objs);
    void slotTouchedObject(const App::DocumentObject &);
    void slotChangePropertyEditor(const App::Document&, const App::Property &);
    //@}

    void addViewProvider(Gui::ViewProviderDocumentObject*);

public:
    /** @name Signals of the document */
    //@{
    /// signal on new Object
    mutable fastsignals::signal<void (const Gui::ViewProviderDocumentObject&)> signalNewObject;
    /// signal on deleted Object
    mutable fastsignals::signal<void (const Gui::ViewProviderDocumentObject&)> signalDeletedObject;
    /** signal on changed Object, the 2nd argument is the changed property
        of the referenced document object, not of the view provider */
    mutable fastsignals::signal<void (const Gui::ViewProviderDocumentObject&,
                                          const App::Property&)>                   signalChangedObject;
    /// signal on renamed Object
    mutable fastsignals::signal<void (const Gui::ViewProviderDocumentObject&)> signalRelabelObject;
    /// signal on activated Object
    mutable fastsignals::signal<void (const Gui::ViewProviderDocumentObject&)> signalActivatedObject;
    /// signal on entering in edit mode
    /// signal on activated object in the tree (bold item)
    mutable fastsignals::signal<void (const Gui::ViewProviderDocumentObject*, const char*)>
        signalActivatedViewProvider;
    mutable fastsignals::signal<void (const Gui::ViewProviderDocumentObject&)> signalInEdit;
    /// signal on leaving edit mode
    mutable fastsignals::signal<void (const Gui::ViewProviderDocumentObject&)> signalResetEdit;
    /// signal on changed Object, the 2nd argument is the highlight mode to use
    mutable fastsignals::signal<void (const Gui::ViewProviderDocumentObject&,
                                          const Gui::TreeItemMode&,
                                          App::DocumentObject *parent,
                                          const char *subname)> signalExpandObject;
    /// signal on changed ShowInTree property in view provider
    mutable fastsignals::signal<void (const Gui::ViewProviderDocumentObject&)> signalShowItem;
    /// signal on scrolling to an object
    mutable fastsignals::signal<void (const Gui::ViewProviderDocumentObject&)> signalScrollToObject;
    /// signal on undo Document
    mutable fastsignals::signal<void (const Gui::Document& doc)> signalUndoDocument;
    /// signal on redo Document
    mutable fastsignals::signal<void (const Gui::Document& doc)> signalRedoDocument;
    /// signal on deleting Document
    mutable fastsignals::signal<void (const Gui::Document& doc)> signalDeleteDocument;
    /// signal on change of document's modified status
    mutable fastsignals::signal<void (const Gui::Document& doc)> signalChangedModified;
    /// signal on attaching new view
    mutable fastsignals::signal<void (const BaseView &, bool passive)> signalAttachView;
    /// signal on detaching view
    mutable fastsignals::signal<void (const BaseView &, bool passive)> signalDetachView;
    /// signal on changed view property
    mutable fastsignals::signal<void (const Gui::BaseView &, const App::Property &)> signalChangedView;
    /// signal on changes in show on top objects
    mutable fastsignals::signal<void (int, const App::SubObjectT &)> signalOnTopObject;
    /// signal on changes in accumulated editing transformation
    mutable fastsignals::signal<void (const Gui::Document &)> signalEditingTransformChanged;
    //@}

    /** @name I/O of the document */
    //@{
    unsigned int getMemSize () const override;
    /// Save the document
    bool save();
    /// Save the document under a new file name
    bool saveAs();
    /// Save a copy of the document under a new file name
    bool saveCopy();
    /// Save all open document
    static void saveAll();
    /** Report the included files of the view tier to the document's store.
     *
     * View provider and view properties are written after their content is,
     * and a view's properties are not reachable from the App document at all,
     * so a save asks for them up front through App::Document::signalCollectFiles.
     */
    void collectFiles(App::FileBlobManager &manager,
                      const std::vector<App::DocumentObject*> &objs) const;
    /// This method is used to save properties or very small amounts of data to an XML document.
    void Save (Base::Writer &writer) const override;
    /// This method is used to restore properties from an XML document.
    void Restore(Base::XMLReader &reader) override;
    /// This method is used to save large amounts of data to a binary file.
    void SaveDocFile (Base::Writer &writer) const override;
    /// This method is used to restore large amounts of data from a binary file.
    void RestoreDocFile(Base::Reader &reader) override;
    void exportObjects(const std::vector<App::DocumentObject*>&, Base::Writer&);
    void importObjects(const std::vector<App::DocumentObject*>&, Base::Reader&,
                       const std::map<std::string, std::string>& nameMapping);
    void readObject(Base::XMLReader &reader);
    void writeObject(Base::Writer &writer,
            const App::DocumentObject *doc, const ViewProvider *obj) const;

    /** @name The shared view provider defaults
     *
     * View providers of one class are nearly identical, so the file carries
     * one default block per class and each view provider only its difference
     * from it. See Gui::Document::saveDefaults for what that buys.
     */
    //@{
    /// Record one class default block (App::SharedDefaults) per view provider
    /// class present in this document, or none at all when the document is
    /// being written as an older schema. The stand-in each record is taken
    /// from does not outlive the recording.
    void buildDefaults(Base::Writer &writer,
            std::map<std::string, App::SharedDefaults> &defaults) const;
    /// Write those records as the block the objects refer to, byte for byte.
    void saveDefaults(Base::Writer &writer,
            const std::map<std::string, App::SharedDefaults> &defaults) const;
    /// Read the block back and keep whatever it says that this build does
    /// not, comparing at the archive's schema (not this file's own).
    void restoreDefaults(Base::XMLReader &reader, int count, int schemaVersion);
    /// Put that difference on one view provider, before its own properties.
    void applyDefaults(ViewProvider *vp);
    //@}

    /** @name Deferred view provider restore (progressive load) */
    //@{
    /// True while a progressive load's view providers are still being built
    bool isRestoringViewProviders() const;
    /** Build and restore whatever the load deferred, now.
     *
     * A save, an export, or anything else that needs every view provider to
     * exist calls this; it is a no-op once the drain has finished.
     */
    void flushDeferredRestore();
    //@}
    /// Add all root objects of the given array to a group
    void addRootObjectsToGroup(const std::vector<App::DocumentObject*>&, App::DocumentObject*);
    //@}

    /// Observer message from the App doc
    void setModified(bool);
    bool isModified() const;

    /// Getter for the App Document
    App::Document*  getDocument() const;

    /** @name methods for View handling */
    //@{
    /// Getter for the active view
    Gui::MDIView* getActiveView() const;
    void setActiveWindow(Gui::MDIView* view);
    Gui::MDIView* getEditingViewOfViewProvider(Gui::ViewProvider*) const;
    Gui::MDIView* getViewOfViewProvider(const Gui::ViewProvider*) const;
    Gui::MDIView* getViewOfNode(SoNode*) const;
    Gui::MDIView* getEditingView(void) const;
    /// Create a new view
    MDIView *createView(const Base::Type& typeId);
    /** Create a 3D view without hosting it anywhere -- the layout
     * restore places these into split view cells itself. createView is
     * this plus the default hosting.
     */
    View3DInventor *createView3D();
    /** Create a clone of the given view.
     * With \a transferEdit (the default) an active editing view
     * provider moves to the clone -- what the callers replacing the
     * original view want. A split keeping both views passes false so
     * the edit stays where the user is working.
     */
    Gui::MDIView* cloneView(Gui::MDIView*, bool transferEdit = true);
    /** send messages to the active view
     * Send a specific massage to the active view and is able to receive a
     * return message
     */
    /// send Messages to all views
    bool sendMsgToViews(const char* pMsg);
    /** Sends the message \a pMsg to the views of type \a typeid and stops with
     * the first view that supports the message and returns \a ppReturn. The very
     * first checked view is the current active view.
     * If a view supports the message true is returned and false otherwise.
     */
    bool sendMsgToFirstView(const Base::Type& typeId, const char* pMsg, const char** ppReturn);
    /// Attach a view (get called by the MDIView constructor)
    void attachView(Gui::BaseView* pcView, bool bPassiv=false);
    /// Detach a view (get called by the MDIView destructor)
    void detachView(Gui::BaseView* pcView, bool bPassiv=false);
    /** A view name no view of this document holds, "View<n>".
     *
     * Handed out when a view joins the document and kept by it from then on
     * -- see Gui::BaseView::getPersistentName(). @a except is left out of
     * the search, so a view can ask whether it may keep the name it has.
     */
    std::string uniqueViewName(const Gui::BaseView *except = nullptr) const;
    /// helper for selection
    ViewProviderDocumentObject* getViewProviderByPathFromTail(SoPath * path) const;
    /// helper for selection
    ViewProviderDocumentObject* getViewProviderByPathFromHead(SoPath * path) const;
    /// Get all view providers along the path and the corresponding node index in the path
    std::vector<std::pair<ViewProviderDocumentObject*,int> > getViewProvidersByPath(SoPath * path) const;
    /// call update on all attached views
    void onUpdate();
    /// call relabel to all attached views
    void onRelabel();
    /// returns a list of all attached MDI views
    std::list<MDIView*> getMDIViews() const;
    /// returns a list of all MDI views of a certain type
    std::list<MDIView*> getMDIViewsOfType(const Base::Type& typeId) const;
    /// return all non-passive views
    const std::list<BaseView*> &getViews() const;
    /// convenience function to iterate views of a give type
    template<class T, class F>
    void foreachView(F f) const {
        for(auto view : getViews()) {            
            if(view->isDerivedFrom(T::getClassTypeId()))
                f(static_cast<T*>(view));
        }
    }
    /// convenience function to iterate views of a give type
    template<class F>
    void foreachView(Base::Type typeId, F f) const {
        for(auto view : getViews()) {            
            if(view->isDerivedFrom(typeId))
                f(view);
        }
    }
    BaseView *getViewByID(int id) const;
    //@}

    MDIView *setActiveView(ViewProviderDocumentObject *vp=nullptr, Base::Type typeId = Base::Type());

    /** @name View provider handling  */
    //@{
    /// Get the view provider for that object
    ViewProvider* getViewProvider(const App::DocumentObject *) const;
    ViewProviderDocumentObject *getViewProvider(SoNode *node) const;
    /// set an annotation view provider
    void setAnnotationViewProvider(const char* name, ViewProvider *pcProvider);
    /// get an annotation view provider
    ViewProvider * getAnnotationViewProvider(const char* name) const;
    /// remove an annotation view provider
    void removeAnnotationViewProvider(const char* name);
    /// test if the feature is in show
    bool isShow(const char* name);
    /// put the feature in show
    void setShow(const char* name);
    /// set the feature in Noshow
    void setHide(const char* name);
    /// set the feature transformation (only viewing)
    void setPos(const char* name, const Base::Matrix4D& rclMtrx);
    std::vector<ViewProvider*> getViewProvidersOfType(const Base::Type& typeId) const;
    ViewProvider *getViewProviderByName(const char* name) const;
    /// set the ViewProvider in special edit mode
    bool setEdit(Gui::ViewProvider* p, int ModNum=0, const char *subname=nullptr);
    const Base::Matrix4D &getEditingTransform() const;
    void setEditingTransform(const Base::Matrix4D &mat);
    /** The view the current edit session is bound to, or null.
     *
     * Asked by a view that is going away while an edit is running in
     * it: a client's mirror dies with its connection, and the document
     * must not be left pointing at it.
     */
    ViewerContext *editingViewer() const;
    /// reset from edit mode, this cause all document to reset edit
    void resetEdit();
    /** Set whether leaving edit mode should restore the previous edit session
     *
     * When an edit session interrupts another one (e.g. editing a sketch from
     * within an assembly edit), the interrupted session is re-entered when the
     * interrupting one ends, provided it called setEditRestore(true). The flag
     * belongs to the current edit session and is cleared when it ends.
     */
    void setEditRestore(bool askRestore);
    /// reset edit of this document
    void _resetEdit();
    /// get the in edit ViewProvider or NULL
    ViewProvider *getInEdit(ViewProviderDocumentObject **parentVp=nullptr,
            std::string *subname=nullptr, int *mode=nullptr, std::string *subElement=nullptr) const;
    /// get the in edit ViewProvider or NULL
    App::SubObjectT getInEditT(int *mode=0) const;
    /// set the in edit ViewProvider subname reference
    void setInEdit(ViewProviderDocumentObject *parentVp, const char *subname);
    /** Add or remove view provider from scene graphs of all views
     *
     * It calls ViewProvider::canAddToSceneGraph() to decide whether to add the
     * view provider or remove it
     */
    void toggleInSceneGraph(ViewProvider *vp);
    //@}

    /** @name methods for the UNDO REDO handling */
    //@{
    /// Open a new Undo transaction on the document
    void openCommand(const char* sName=nullptr);
    /// Commit the Undo transaction on the document
    void commitCommand();
    /// Abort the Undo transaction on the document
    void abortCommand();
    /// Check if an Undo transaction is open
    bool hasPendingCommand() const;
    /// Get an Undo string vector with the Undo names
    std::vector<std::string> getUndoVector() const;
    /// Get an Redo string vector with the Redo names
    std::vector<std::string> getRedoVector() const;
    /// Will UNDO one or more steps
    void undo(int iSteps);
    /// Will REDO one or more steps
    void redo(int iSteps) ;
    /** Check if the document is performing undo/redo transaction
     *
     * Unlike App::Document::isPerformingTransaction(), Gui::Document will
     * report transacting when triggering grouped undo/redo in other documents
     */
    bool isPerformingTransaction() const;
    //@}

    /// handles the application close event
    bool canClose(bool checkModify=true, bool checkLink=false);
    bool isLastView();

    /// called by Application before being deleted
    void beforeDelete();

    PyObject *getPyObject() override;

    const char *getCameraSettings(const std::string *settings=nullptr) const;
    bool saveCameraSettings(const char *, std::string *dst=nullptr) const;

    /// check if a view provider is 3D claimed by other
    bool isClaimed3D(ViewProvider *) const;

protected:
    // pointer to the python class
    Gui::DocumentPy *_pcDocPy;

private:
    //handles the scene graph nodes to correctly group child and parents
    void handleChildren3D(ViewProvider* viewProvider, bool deleting=false);

    /// Rebuild the saved split view containers on restore, placing the
    /// (bare-created) 3D views and object views into cells
    void applyViewAreaLayouts(const std::list<MDIView*> &views);

    /// Put every 3D view's OnTopObjects property back in step with its
    /// viewer's on-top group (docs/CoinRetirement.md 5.12). Connected
    /// to signalOnTopObject -- the property is the store, so it is
    /// written whenever the group moves, not gathered at save time.
    void snapshotOnTopObjects();

    /// Build and restore one captured view provider during the load itself,
    /// handing its archive file requests to the archive's reader
    void restoreCapturedViewProvider(const std::string &xml,
            Base::XMLReader &archiveReader);
    /// Post a drain slice for the deferred view provider restore
    void scheduleDeferredRestore(int delayMs=0);
    /// Build and restore parked view providers for one budget's worth
    void runDeferredRestoreSlice();
    /// Serve parked archive entries for one budget's worth, once the view
    /// provider drain that used to carry them has finished (or never ran)
    void runDeferredServeSlice();
    /// The drain has emptied: default what was never recorded, then refresh
    void finishDeferredRestore();

    /** Offer every document about to be written the schema its own content
     * needs, and answer false when the user called the save off.
     *
     * Static, because Save All is: the answer belongs to each document
     * rather than to whoever pressed the button, and a document told to
     * save its content away anyway remembers that for the session.
     */
    static bool offerSchemaUpgrade(const std::vector<App::Document*> &docs);

    /// Check other documents for the same transaction ID
    bool checkTransactionID(bool undo, int iSteps);
    /// Ask for user interaction if saving has failed
    bool askIfSavingFailed(const QString&);

    struct DocumentP* d;
    static int _iDocCount;

    mutable std::string cameraSettings;

    /** @name attributes for the UNDO REDO facility
     */
    //@{
    /// undo names list
    std::list<std::string> listUndoNames;
    /// redo names list
    std::list<std::string> listRedoNames;
    //@}

    friend class TransactionViewProvider;
};

} // namespace Gui


#endif // GUI_DOCUMENT_H
