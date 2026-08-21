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


#ifndef APPLICATION_H
#define APPLICATION_H

#include <QPixmap>
#include <map>
#include <string>

#define  putpix()

#include <App/Application.h>

class QCloseEvent;
class SoNode;

namespace Py {
class Object;
}

namespace Gui{
class BaseView;
class CommandManager;
class Document;
class MacroManager;
class MDIView;
class MainWindow;
class MenuItem;
class PreferencePackManager;
namespace StyleParameters {
class ParameterManager;
}
class ViewProvider;
class ViewProviderDocumentObject;
enum  class HighlightMode;

/** The Application main class
 * This is the central class of the GUI
 * @author Jürgen Riegel, Werner Mayer
 */
class GuiExport Application
{
public:
    enum Status {
        UserInitiatedOpenDocument = 0
    };

    /// construction
    explicit Application(bool GUIenabled);
    /// destruction
    ~Application();

    /** @name methods for support of files */
    //@{
    /// open a file
    void open(const char* FileName, const char* Module);
    /// import a file into the document DocName
    void importFrom(const char* FileName, const char* DocName, const char* Module);
    /// Export objects from the document DocName to a single file
    void exportTo(const char* FileName, const char* DocName, const char* Module);
    /// Reload a partial opened document
    App::Document *reopen(App::Document *doc);
    //@}


    /** @name methods for View handling */
    //@{
    /// send Messages to the active view
    bool sendMsgToActiveView(const char* pMsg, const char** ppReturn=nullptr);
    /// send Messages test to the active view
    bool sendHasMsgToActiveView(const char* pMsg);
    /// send Messages to the focused view
    bool sendMsgToFocusView(const char* pMsg, const char** ppReturn=nullptr);
    /// send Messages test to the focused view
    bool sendHasMsgToFocusView(const char* pMsg);
    /// Attach a view (get called by the FCView constructor)
    void attachView(Gui::BaseView* pcView);
    /// Detach a view (get called by the FCView destructor)
    void detachView(Gui::BaseView* pcView);
    /// get called if a view gets activated, this manage the whole activation scheme
    void viewActivated(Gui::MDIView* pcView);
    /// call update to all documents and all views (costly!)
    void onUpdate();
    /// call update to all views of the active document
    void updateActive();
    /// call update to all command actions
    void updateActions(bool delay = false);
    //@}

    /** @name Signals of the Application */
    //@{
    /// signal on new Document
    fastsignals::signal<void (const Gui::Document&, bool)> signalNewDocument;
    /// signal on deleted Document
    fastsignals::signal<void (const Gui::Document&)> signalDeleteDocument;
    /// signal on relabeling Document
    fastsignals::signal<void (const Gui::Document&)> signalRelabelDocument;
    /// signal on renaming Document
    fastsignals::signal<void (const Gui::Document&)> signalRenameDocument;
    /// signal on activating Document
    fastsignals::signal<void (const Gui::Document&)> signalActiveDocument;
    /// signal on new Object
    fastsignals::signal<void (const Gui::ViewProvider&)> signalNewObject;
    /// signal on deleted Object
    fastsignals::signal<void (const Gui::ViewProvider&)> signalDeletedObject;
    /// signal on changed Object
    fastsignals::signal<void (const Gui::ViewProvider&, const App::Property&)> signalBeforeChangeObject;
    /// signal on changed object property
    fastsignals::signal<void (const Gui::ViewProvider&, const App::Property&)> signalChangedObject;
    /// signal on renamed Object
    fastsignals::signal<void (const Gui::ViewProvider&)> signalRelabelObject;
    /// signal on activated Object
    fastsignals::signal<void (const Gui::ViewProvider&)> signalActivatedObject;
    /// signal on activated workbench
    fastsignals::signal<void (const char*)> signalActivateWorkbench;
    /// signal on added/removed workbench
    fastsignals::signal<void ()> signalRefreshWorkbenches;
    /// signal on added workbench
    fastsignals::signal<void (const char*)> signalAddWorkbench;
    /// signal on removed workbench
    fastsignals::signal<void (const char*)> signalRemoveWorkbench;
    /// signal on show hidden items
    fastsignals::signal<void (const Gui::Document&)> signalShowHidden;
    /// signal on activating view
    fastsignals::signal<void (const Gui::MDIView*)> signalActivateView;
    /// signal on attaching new view
    mutable fastsignals::signal<void (const Gui::BaseView &, bool passive)> signalAttachView;
    /// signal on detaching view
    mutable fastsignals::signal<void (const Gui::BaseView &, bool passive)> signalDetachView;
    /// signal on changed view property
    mutable fastsignals::signal<void (const Gui::BaseView &, const App::Property &)> signalChangedView;
    /// signal on view override mode change
    fastsignals::signal<void (const Gui::MDIView*)> signalViewModeChanged;
    /// signal on entering in edit mode
    fastsignals::signal<void (const Gui::ViewProviderDocumentObject&)> signalInEdit;
    /// signal on leaving edit mode
    fastsignals::signal<void (const Gui::ViewProviderDocumentObject&)> signalResetEdit;
    /// signal on changed claimed children
    fastsignals::signal<void (const Gui::ViewProviderDocumentObject&)> signalChangedChildren;
    /// signal on changed Object, the 2nd argument is the highlight mode to use
    fastsignals::signal<void (const Gui::ViewProviderDocumentObject&, 
                                  const Gui::HighlightMode&, 
                                  bool,
                                  App::DocumentObject *parent, 
                                  const char *subname)> signalHighlightObject; 
    /// signal on changing user edit mode
    fastsignals::signal<void (int)> signalUserEditModeChanged;
    //@}

    /** @name methods for Document handling */
    //@{
protected:
    /// Observer message from the Application
    void slotNewDocument(const App::Document&,bool);
    void slotDeleteDocument(const App::Document&);
    void slotRelabelDocument(const App::Document&);
    void slotRenameDocument(const App::Document&);
    void slotActiveDocument(const App::Document&);
    void slotShowHidden(const App::Document&);
    void slotNewObject(const ViewProvider&);
    void slotDeletedObject(const ViewProvider&);
    void slotChangedObject(const ViewProvider&, const App::Property& Prop);
    void slotRelabelObject(const ViewProvider&);
    void slotActivatedObject(const ViewProvider&);
    void slotInEdit(const Gui::ViewProviderDocumentObject&);
    void slotResetEdit(const Gui::ViewProviderDocumentObject&);
    std::string initializeWorkbench(const char *name, Py::Object);

public:
    /// message when a GuiDocument is about to vanish
    void onLastWindowClosed(Gui::Document* pcDoc);
    /// Getter for the active document
    Gui::Document* activeDocument() const;
    /// Set the active document
    void setActiveDocument(Gui::Document* pcDocument);
    /// Getter for the editing document
    Gui::Document* editDocument() const;
    Gui::MDIView* editViewOfNode(SoNode *node) const;
    /// Set editing document, which will reset editing of all other document
    void setEditDocument(Gui::Document* pcDocument);
    /** Retrieves a pointer to the Gui::Document whose App::Document has the name \a name.
    * If no such document exists 0 is returned.
    */
    Gui::Document* getDocument(const char* name) const;
    /** Retrieves a pointer to the Gui::Document whose App::Document matches to \a pDoc.
    * If no such document exists 0 is returned.
    */
    Gui::Document* getDocument(const App::Document* pDoc) const;
    /// Getter for the active view of the active document or null
    Gui::MDIView* activeView() const;
    /// Activate a view of the given type of the active document
    void activateView(const Base::Type&, bool create=false);
    /// Shows the associated view provider of the given object
    void showViewProvider(const App::DocumentObject*);
    /// Hides the associated view provider of the given object
    void hideViewProvider(const App::DocumentObject*);
    /// Get the view provider of the given object
    Gui::ViewProvider* getViewProvider(const App::DocumentObject*) const;
    /// Auto pick a new active document
    void switchActiveDocument();
    //@}

    /// true when the application shutting down
    bool isClosing();

    /** @name Deferred visual builds */
    //@{
    /** Whether geometry is still being built into the views.
     *
     * The last phase of a progressive load, and the only one in which
     * geometry actually reaches a renderer: the App restore and the
     * deferred view-provider drain both finish with the 3D scene still
     * empty, and the visuals are built afterwards, a slice at a time
     * (ViewProviderPartExt::runDeferredVisualSlice). A consumer asking
     * "is a document still arriving?" that only reads the document
     * status bits and Document::isRestoringViewProviders() gets the
     * answer "no" for exactly the phase it cares about -- measured on
     * the 5455-object rack model, where the renderer's scene held 0
     * drawables until after both of those had cleared.
     *
     * Kept here rather than in the queue itself because the queue is
     * PartGui's and its readers are not: Gui must not depend on a
     * workbench. The owner sets it around its drain; anything else
     * reads it.
     */
    void setBuildingVisuals(bool building);
    bool isBuildingVisuals() const;
    //@}

    void checkForDeprecatedSettings();
    void checkForPreviousCrashes();

    /** @name workbench handling */
    //@{
    /// Activate a named workbench
    bool activateWorkbench(const char* name);
    bool initializeWorkbench(const char *name);
    const char *initializingWorkbench() const;
    QPixmap workbenchIcon(const QString&, QString *iconPath=nullptr) const;
    QString workbenchToolTip(const QString&) const;
    QString workbenchMenuText(const QString&) const;
    QStringList workbenches() const;
    void setupContextMenu(const char* recipient, MenuItem*) const;
    //@}

    /** @name Appearance */
    //@{
    /// Activate a stylesheet
    void setStyleSheet(const QString& qssFile, bool tiledBackground);
    /// Set the Qt widget style: "FreeCAD" is the bundled Fusion-based
    /// proxy style, "System" clears the override, anything else is
    /// looked up in QStyleFactory.
    void setStyle(const QString& name);
    QString replaceVariablesInQss(QString qssText);
    /** Apply the palette named by MainWindow/ColorScheme.
     *
     * "Light" or "Dark" pins Qt's palette; an empty value lets Qt follow the
     * system setting. Themes that ship no stylesheet (FreeCAD Classic) draw
     * entirely from this palette, so without a value Qt >= 6.8 would render
     * them dark on a dark desktop. A no-op before Qt 6.8, which has no API to
     * override the scheme.
     */
    static void applyColorScheme();
    /** Re-apply the theme the desktop calls for, for "Match Desktop".
     *
     * A no-op unless MainWindow/ThemeAuto is set. Called at startup and again
     * whenever the desktop's scheme changes, and re-entrant: applying a theme
     * pins the palette, which is itself a scheme change.
     */
    static void resolveAutoTheme();
    /// Whether the platform reports a dark system color scheme.
    static bool systemPrefersDarkScheme();
    /** Make widgets resolve against the current application palette again.
     *
     * Needed after a theme change: Qt restores a widget to the palette it held
     * when a stylesheet polished it, so widgets can be left holding colors from
     * the previous scheme. Widgets that set a palette of their own are kept.
     */
    static void refreshInheritedPalettes();
    /** @name The accent colors a stylesheet gets where the configuration names none.
     *
     * FreeCAD's blue and two shades of it, packed with an opaque alpha the way
     * the Themes parameters store them. The Theme preference page and the Start
     * wizard both carry these; they are here so that everything resolving
     * @ThemeAccentColor* agrees.
     *
     * The shipped sheets use the three slots for three different jobs, so they
     * must not be the same color -- which they were until the states they are
     * supposed to tell apart all came out identical:
     *
     *  - 1 is the highlight, on nearly a hundred rules: hover, selected, checked.
     *  - 2 is the engaged state: focus, pressed, a combo box that is open. It
     *    is deeper than 1 so that focus reads as more than hover.
     *  - 3 is only ever the far stop of a gradient whose near stop is 1, so it
     *    is a slightly darker 1 and gives the gradient somewhere to go.
     *
     * A dark theme wants 2 lifted rather than deepened; its preference pack
     * overrides these.
     */
    //@{
    static constexpr unsigned long DefaultAccentColor1 = 0x557BB6FFUL;
    static constexpr unsigned long DefaultAccentColor2 = 0x405C89FFUL;
    static constexpr unsigned long DefaultAccentColor3 = 0x4B6CA0FFUL;
    /// Slots 2 and 3 as a dark scheme wants them; the Dark pack carries the same values.
    static constexpr unsigned long DefaultDarkAccentColor2 = 0x88A3CCFFUL;
    static constexpr unsigned long DefaultDarkAccentColor3 = 0x466595FFUL;
    /// Backwards-compatible spelling of DefaultAccentColor1.
    static constexpr unsigned long DefaultAccentColor = DefaultAccentColor1;
    //@}
    //@}

    /** @name User Commands */
    //@{
    /// Get macro manager
    Gui::MacroManager *macroManager();
    /// Reference to the command manager
    Gui::CommandManager &commandManager();
    /// helper which create the commands
    void createStandardOperations();
    //@}

    Gui::PreferencePackManager* prefPackManager();

    /// The style parameter evaluator behind themed stylesheets
    Gui::StyleParameters::ParameterManager* styleParameterManager();
    void initStyleParameterManager();

    /** @name Init, Destruct an Access methods */
    //@{
    /// some kind of singleton
    static Application* Instance;
    static void initApplication();
    static void initTypes();
    static void initOpenInventor();
    static void runInitGuiScript();
    static void runApplication();
    static void restart(bool reset = false);
    static bool isRestarting();
    static bool checkRestart();
    void tryClose( QCloseEvent * e );
    //@}

    /// return the status bits
    bool testStatus(Status pos) const;
    /// set the status bits
    void setStatus(Status pos, bool on);

    /** @name User edit mode */
    //@{
protected:
    // the below std::map is a translation of 'EditMode' enum in ViewProvider.h
    // to add a new edit mode, it should first be added there
    // this is only used for GUI user interaction (menu, toolbar, Python API)
    const std::map<int, std::pair<std::string, std::string>> userEditModes {
        {0,
         std::make_pair(
             QT_TRANSLATE_NOOP("EditMode", "Default"),
             QT_TRANSLATE_NOOP("EditMode",
                               "The object will be edited using the mode defined internally to be "
                               "the most appropriate for the object type"))},
        {1,
         std::make_pair(QT_TRANSLATE_NOOP("EditMode", "Transform"),
                        QT_TRANSLATE_NOOP("EditMode",
                                          "The object will have its placement editable with the "
                                          "Std TransformManip command"))},
        {2,
         std::make_pair(QT_TRANSLATE_NOOP("EditMode", "Cutting"),
                        QT_TRANSLATE_NOOP("EditMode",
                                          "This edit mode is implemented as available but "
                                          "currently does not seem to be used by any object"))},
        {3,
         std::make_pair(QT_TRANSLATE_NOOP("EditMode", "Color"),
                        QT_TRANSLATE_NOOP("EditMode",
                                          "The object will have the color of its individual faces "
                                          "editable with the Part FaceColors command"))},
    };
    int userEditMode = userEditModes.begin()->first;

public:
    std::map <int, std::pair<std::string,std::string>> listUserEditModes() const { return userEditModes; }
    int getUserEditMode(const std::string &mode = "") const;
    std::pair<std::string,std::string> getUserEditModeUIStrings(int mode = -1) const;
    bool setUserEditMode(int mode);
    bool setUserEditMode(const std::string &mode);
    //@}

public:
    //---------------------------------------------------------------------
    // python exports goes here +++++++++++++++++++++++++++++++++++++++++++
    //---------------------------------------------------------------------
    // static python wrapper of the exported functions
    static PyObject* sActivateWorkbenchHandler (PyObject *self,PyObject *args); // activates a workbench object
    static PyObject* sAddWorkbenchHandler      (PyObject *self,PyObject *args); // adds a new workbench handler to a list
    static PyObject* sRemoveWorkbenchHandler   (PyObject *self,PyObject *args); // removes a workbench handler from the list
    static PyObject* sGetWorkbenchHandler      (PyObject *self,PyObject *args); // retrieves the workbench handler
    static PyObject* sListWorkbenchHandlers    (PyObject *self,PyObject *args); // retrieves a list of all workbench handlers
    static PyObject* sActiveWorkbenchHandler   (PyObject *self,PyObject *args); // retrieves the active workbench object
    static PyObject* sAddResPath               (PyObject *self,PyObject *args); // adds a path where to find resources
    static PyObject* sAddLangPath              (PyObject *self,PyObject *args); // adds a path to a qm file
    static PyObject* sAddIconPath              (PyObject *self,PyObject *args); // adds a path to an icon file
    static PyObject* sAddIcon                  (PyObject *self,PyObject *args); // adds an icon to the cache
    static PyObject* sGetIcon                  (PyObject *self,PyObject *args); // get an icon from the cache
    static PyObject* sGetIconContext           (PyObject *self,PyObject *args);
    static PyObject* sAddIconContext           (PyObject *self,PyObject *args);
    static PyObject* sIsIconCached             (PyObject *self,PyObject *args); // check if an icon is cached
    static PyObject* sGetIconNames             (PyObject *self,PyObject *args); // get all cached icon names

    static PyObject* sSendActiveView           (PyObject *self,PyObject *args);
    static PyObject* sSendFocusView            (PyObject *self,PyObject *args);

    static PyObject* sGetMainWindow            (PyObject *self,PyObject *args);
    static PyObject* sUpdateGui                (PyObject *self,PyObject *args);
    static PyObject* sServeDocument            (PyObject *self,PyObject *args);
    static PyObject* sServeClients             (PyObject *self,PyObject *args);
    static PyObject* sServeSetClientMode       (PyObject *self,PyObject *args);
    static PyObject* sServeKickClient          (PyObject *self,PyObject *args);
    static PyObject* sServeGrants              (PyObject *self,PyObject *args);
    static PyObject* sServeSetGrants           (PyObject *self,PyObject *args);
    static PyObject* sServeStop                (PyObject *self,PyObject *args);
    static PyObject* sUpdateLocale             (PyObject *self,PyObject *args);
    static PyObject* sGetLocale                (PyObject *self,PyObject *args);
    static PyObject* sSetLocale                (PyObject *self,PyObject *args);
    static PyObject* sSupportedLocales         (PyObject *self,PyObject *args);
    static PyObject* sCreateDialog             (PyObject *self,PyObject *args);
    static PyObject* sAddPreferencePage        (PyObject *self,PyObject *args);

    static PyObject* sRunCommand               (PyObject *self,PyObject *args);
    static PyObject* sAddCommand               (PyObject *self,PyObject *args);

    static PyObject* sHide                     (PyObject *self,PyObject *args); // deprecated
    static PyObject* sShow                     (PyObject *self,PyObject *args); // deprecated
    static PyObject* sHideObject               (PyObject *self,PyObject *args); // hide view provider object
    static PyObject* sShowObject               (PyObject *self,PyObject *args); // show view provider object

    static PyObject* sOpen                     (PyObject *self,PyObject *args); // open Python scripts
    static PyObject* sInsert                   (PyObject *self,PyObject *args); // open Python scripts
    static PyObject* sExport                   (PyObject *self,PyObject *args);
    static PyObject* sReload                   (PyObject *self,PyObject *args); // reload FCStd file
    static PyObject* sLoadFile                 (PyObject *self,PyObject *args,PyObject *kwd); // open all types of files

    static PyObject* sCoinRemoveAllChildren    (PyObject *self,PyObject *args);

    static PyObject* sActiveDocument           (PyObject *self,PyObject *args);
    static PyObject* sSetActiveDocument        (PyObject *self,PyObject *args);
    static PyObject* sActiveView               (PyObject *self,PyObject *args);
    static PyObject* sActivateView             (PyObject *self,PyObject *args);
    static PyObject* sGetDocument              (PyObject *self,PyObject *args);
    static PyObject* sEditDocument             (PyObject *self,PyObject *args);
    static PyObject* sResetEdit                (PyObject *self,PyObject *args);

    static PyObject* sDoCommand                (PyObject *self,PyObject *args);
    static PyObject* sDoCommandGui             (PyObject *self,PyObject *args);
    static PyObject* sAddModule                (PyObject *self,PyObject *args);

    static PyObject* sShowDownloads            (PyObject *self,PyObject *args);
    static PyObject* sShowPreferences          (PyObject *self,PyObject *args);

    static PyObject* sListThemes               (PyObject *self,PyObject *args);
    static PyObject* sApplyTheme               (PyObject *self,PyObject *args);
    static PyObject* sListConfigBackups        (PyObject *self,PyObject *args);
    static PyObject* sRevertConfig             (PyObject *self,PyObject *args);
    static PyObject* sListConfigUndos          (PyObject *self,PyObject *args);
    static PyObject* sUndoConfig               (PyObject *self,PyObject *args);

    static PyObject* sCreateViewer             (PyObject *self,PyObject *args);
    static PyObject* sGetMarkerIndex           (PyObject *self,PyObject *args);

    static PyObject* sAddDocObserver           (PyObject *self,PyObject *args);
    static PyObject* sRemoveDocObserver        (PyObject *self,PyObject *args);

    static PyObject* sAddWbManipulator         (PyObject *self,PyObject *args);
    static PyObject* sRemoveWbManipulator      (PyObject *self,PyObject *args);

    static PyObject* sListUserEditModes        (PyObject *self,PyObject *args);
    static PyObject* sGetUserEditMode          (PyObject *self,PyObject *args);
    static PyObject* sSetUserEditMode          (PyObject *self,PyObject *args);

    static PyObject* sSetExecFile              (PyObject *self,PyObject *args);

    static PyMethodDef    Methods[];

private:
    struct ApplicationP* d;
    /// workbench python dictionary
    PyObject*             _pcWorkbenchDictionary;
    std::map<std::string, std::string> _workbenchPaths;
    std::string _ExecFile;
};

} //namespace Gui

#endif
