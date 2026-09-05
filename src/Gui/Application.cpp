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

#ifndef _PreComp_
# include <boost/interprocess/sync/file_lock.hpp>
# include <Inventor/errors/SoDebugError.h>
# include <Inventor/errors/SoError.h>
# include <QCloseEvent>
# include <QDir>
# include <QFile>
# include <QFileInfo>
# include <QImageReader>
# include <QLocale>
# include <QMessageBox>
# include <QMessageLogContext>
# include <QPainter>
# include <QProcess>
# include <QProxyStyle>
# include <QRegularExpression>
# include <QRegularExpressionMatch>
# include <QStatusBar>
# include <QStyle>
# include <QStyleFactory>
# include <QStyleHints>
# include <QStyleOptionMenuItem>
# include <QSurfaceFormat>
# include <QTextStream>
# include <QTimer>
# include <QWindow>
# include <QOpenGLWidget>
#endif

#include <Inventor/CoinFork.h>

#ifndef _WIN32
# include <dlfcn.h>
#endif

// Qt6 removed the QtPlatformHeaders module; see the use site below for why the
// workaround it provided is not carried over.
# if QT_VERSION >= 0x050600 && QT_VERSION < 0x060000 && defined(Q_OS_WIN32)
#  include <QtPlatformHeaders/QWindowsWindowFunctions>
# endif

#include <QLoggingCategory>

#include <App/Document.h>
#include <App/DocumentObjectPy.h>
#include <App/DocumentParams.h>
#include <Base/Console.h>
#include <Base/Interpreter.h>
#include <Base/Exception.h>
#include <Base/FileInfo.h>
#include <Base/Parameter.h>
#include <Base/Stream.h>
#include <Base/Tools.h>
#include <Base/QtTools.h>

#include <Base/UnitsApi.h>

#include <Language/Translator.h>
#include "Renderer/Renderer.h"
#include <Quarter/Quarter.h>

#include "Application.h"
#include "AutoSaver.h"
#include "AxisOriginPy.h"
#include "BitmapFactory.h"
#include "Command.h"
#include "SandboxGui.h"
#include "CommandActionPy.h"
#include "CommandPy.h"
#include "Control.h"
#include "PreferencePages/DlgSettingsCacheDirectory.h"
#include "DlgCheckableMessageBox.h"
#include "DocumentPy.h"
#include "DocumentRecovery.h"
#include "EditorView.h"
#include "ExpressionBindingPy.h"
#include "FileDialog.h"
#include "FileDialogPy.h"
#include "GuiApplication.h"
#include "GuiInitScript.h"
#include "LinkViewPy.h"
#include "InputHintPy.h"
#include "LiveViewInteraction.h"
#include "MainWindow.h"
#include "Macro.h"
#include "MDIViewWithCamera.h"
#include "PreferencePackManager.h"
#include "StyleParameters/ParameterManager.h"
#include "FreeCADStyle.h"
#include "ParamHandler.h"
#include <ranges>
#include <Base/ServiceProvider.h>
#include "PythonConsolePy.h"
#include "PythonDebugger.h"
#include "RenderParams.h"
#include "ViewParams.h"
#include "MainWindowPy.h"
#include "SoFCDB.h"
#include "Selection.h"
#include "SelectionFilterPy.h"
#include "SoQtOffscreenRendererPy.h"
#include "SplitView3DInventor.h"
#include "ViewArea.h"
#include "TaskView/TaskView.h"
#include "TaskView/TaskDialogPython.h"
#include "ToolBarManager.h"
#include "TransactionObject.h"
#include "TextDocumentEditorView.h"
#include "UiLoader.h"
#include "View3DViewerPy.h"
#include "View3DInventor.h"
#include "ViewProviderAnnotation.h"
#include "ViewProviderDocumentObject.h"
#include "ViewProviderDocumentObjectGroup.h"
#include "ViewProviderDragger.h"
#include "ViewProviderExtension.h"
#include "ViewProviderExtern.h"
#include "ViewProviderFeature.h"
#include "ViewProviderGeoFeatureGroup.h"
#include "ViewProviderGeometryObject.h"
#include "ViewProviderGroupExtension.h"
#include "ViewProviderSuppressibleExtension.h"
#include "ViewProviderImagePlane.h"
#include "ViewProviderInventorObject.h"
#include "ViewProviderLine.h"
#include "ViewProviderLink.h"
#include "ViewProviderLinkPy.h"
#include "ViewProviderMaterialObject.h"
#include "ViewProviderMeasureDistance.h"
#include "ViewProviderShaderObject.h"
#include "ViewProviderOrigin.h"
#include "ViewProviderOriginFeature.h"
#include "ViewProviderOriginGroup.h"
#include "ViewProviderPlacement.h"
#include "ViewProviderPlane.h"
#include "ViewProviderPart.h"
#include "ViewProviderFeaturePython.h"
#include "ViewProviderTextDocument.h"
#include "ViewProviderSavedView.h"
#include "ViewProviderSavedViewPy.h"
#include "ViewProviderDatum.h"

#include "ViewProviderVRMLObject.h"
#include "WaitCursor.h"
#include "Workbench.h"
#include "WorkbenchManager.h"
#include "WorkbenchManipulator.h"
#include "WidgetFactory.h"


using namespace Gui;
using namespace Gui::DockWnd;
using namespace std;
namespace sp = std::placeholders;


Application* Application::Instance = nullptr;
bool _ApplicationStartUp;

namespace Gui {

class ViewProviderMap {
    std::unordered_map<const App::DocumentObject *, ViewProvider *> map;

public:
    void newObject(const ViewProvider& vp)
    {
        auto vpd =
            Base::freecad_dynamic_cast<ViewProviderDocumentObject>(const_cast<ViewProvider*>(&vp));
        if (vpd && vpd->getObject())
            map[vpd->getObject()] = vpd;
    }
    bool deleteObject(const ViewProvider& vp)
    {
        auto vpd =
            Base::freecad_dynamic_cast<ViewProviderDocumentObject>(const_cast<ViewProvider*>(&vp));
        if (vpd && vpd->getObject())
            return map.erase(vpd->getObject());
        return false;
    }
    bool deleteObject(const App::DocumentObject *obj)
    {
        return map.erase(obj);
    }
    void deleteDocument(const App::Document& doc) {
        for (auto obj : doc.getObjects())
            map.erase(obj);
    }
    Gui::ViewProvider* getViewProvider(const App::DocumentObject* obj) const
    {
        auto it = map.find(obj);
        if (it == map.end())
            return nullptr;
        return it->second;
    }
};

// Pimpl class
struct ApplicationP
{
    explicit ApplicationP(bool GUIenabled)
        : startingUp(_ApplicationStartUp)
    {
        startingUp = true;

        // create the macro manager
        if (GUIenabled)
            macroMngr = new MacroManager();
        else
            macroMngr = nullptr;

        // Create the Theme Manager
        prefPackManager = new PreferencePackManager();
        styleParameterManager = new StyleParameters::ParameterManager();
        timer.setSingleShot(true);
        QObject::connect(&timer, &QTimer::timeout, [this](){onTimer();});
    }

    ~ApplicationP()
    {
        delete macroMngr;
        delete prefPackManager;
        delete styleParameterManager;
    }

    void onTimer() {
        for (const auto &v : documents) {
            v.second->foreachView<View3DInventor>([](View3DInventor *view) {
                view->getViewer()->refreshGroupOnTop();
            });
        }
    }

    /// list of all handled documents
    std::map<const App::Document*, Gui::Document*> documents;
    /// Active document
    Gui::Document*   activeDocument{nullptr};
    Gui::Document*  editDocument{nullptr};
    MacroManager*  macroMngr;
    PreferencePackManager* prefPackManager;
    /// Evaluates the style parameters themed stylesheets substitute
    StyleParameters::ParameterManager* styleParameterManager;
    /// The parameter source fed from the active theme's YAML file;
    /// setStyleSheet() re-points it whenever the theme changes
    StyleParameters::YamlParameterSource* themeParametersSource = nullptr;
    /// List of all registered views
    std::list<Gui::BaseView*> passive;
    bool isClosing{false};
    bool &startingUp;
    /// Handles all commands
    CommandManager commandManager;
    std::string initWorkbench;
    /// Handlers whose Initialize() has been run, so it is run only once
    std::set<std::string> initializedWorkbenches;
    QTimer timer;
    ViewProviderMap viewproviderMap;
    std::bitset<32> StatusBits;
};

static PyObject *
FreeCADGui_subgraphFromObject(PyObject * /*self*/, PyObject *args)
{
    PyObject *o;
    if (!PyArg_ParseTuple(args, "O!",&(App::DocumentObjectPy::Type), &o))
        return nullptr;
    App::DocumentObject* obj = static_cast<App::DocumentObjectPy*>(o)->getDocumentObjectPtr();
    std::string vp = obj->getViewProviderName();
    SoNode* node = nullptr;
    try {
        auto base =
            static_cast<Base::BaseClass*>(Base::Type::createInstanceByName(vp.c_str(), true));
        if (base
            && base->isDerivedFrom<Gui::ViewProviderDocumentObject>()) {
            std::unique_ptr<Gui::ViewProviderDocumentObject> vp(
                static_cast<Gui::ViewProviderDocumentObject*>(base));
            std::map<std::string, App::Property*> Map;
            obj->getPropertyMap(Map);
            vp->attach(obj);

            // this is needed to initialize Python-based view providers
            App::Property* pyproxy = vp->getPropertyByName("Proxy");
            if (pyproxy && pyproxy->is<App::PropertyPythonObject>()) {
                static_cast<App::PropertyPythonObject*>(pyproxy)->setValue(Py::Long(1));
            }

            for (const auto& it : Map) {
                vp->updateData(it.second);
            }

            std::vector<std::string> modes = vp->getDisplayModes();
            if (!modes.empty())
                vp->setDisplayMode(modes.front().c_str());
            node = vp->getRoot()->copy();
            node->ref();
            std::string prefix = "So";
            std::string type = node->getTypeId().getName().getString();
            // doesn't start with the prefix 'So'
            if (type.rfind("So", 0) != 0) {
                type = prefix + type;
            }
            else if (type == "SoFCSelectionRoot") {
                type = "SoSeparator";
            }

            type += " *";
            PyObject* proxy = nullptr;
            proxy = Base::Interpreter().createSWIGPointerObj(
                "pivy.coin", type.c_str(), static_cast<void*>(node), 1);
            return Py::new_reference_to(Py::Object(proxy, true));
        }
    }
    catch (const Base::Exception& e) {
        if (node) node->unref();
        PyErr_SetString(PyExc_RuntimeError, e.what());
        return nullptr;
    }

    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject *
FreeCADGui_exportSubgraph(PyObject * /*self*/, PyObject *args)
{
    const char* format = "VRML";
    PyObject* proxy;
    PyObject* output;
    if (!PyArg_ParseTuple(args, "OO|s", &proxy, &output, &format))
        return nullptr;

    void* ptr = nullptr;
    try {
        Base::Interpreter().convertSWIGPointerObj("pivy.coin", "SoNode *", proxy, &ptr, 0);
        auto node = static_cast<SoNode*>(ptr);
        if (node) {
            std::string formatStr(format);
            std::string buffer;

            if (formatStr == "VRML") {
                SoFCDB::writeToVRML(node, buffer);
            }
            else if (formatStr == "IV") {
                buffer = SoFCDB::writeNodesToString(node);
            }
            else {
                THROWM(Base::ValueError, "Unsupported format")
            }

            Base::PyStreambuf buf(output);
            std::ostream str(nullptr);
            str.rdbuf(&buf);
            str << buffer;
        }

        Py_INCREF(Py_None);
        return Py_None;
    }
    catch (const Base::Exception& e) {
        PyErr_SetString(PyExc_RuntimeError, e.what());
        return nullptr;
    }
}

static PyObject *
FreeCADGui_getSoDBVersion(PyObject * /*self*/, PyObject *args)
{
    if (!PyArg_ParseTuple(args, ""))
        return nullptr;
    return PyUnicode_FromString(SoDB::getVersion());
}

struct PyMethodDef FreeCADGui_methods[] = {
    {"subgraphFromObject",FreeCADGui_subgraphFromObject,METH_VARARGS,
     "subgraphFromObject(object) -> Node\n\n"
     "Return the Inventor subgraph to an object"},
    {"exportSubgraph",FreeCADGui_exportSubgraph,METH_VARARGS,
     "exportSubgraph(Node, File or Buffer, [Format='VRML']) -> None\n\n"
     "Exports the sub-graph in the requested format"
     "The format string can be VRML or IV"},
    {"getSoDBVersion",FreeCADGui_getSoDBVersion,METH_VARARGS,
     "getSoDBVersion() -> String\n\n"
     "Return a text string containing the name\n"
     "of the Coin library and version information"},
    {nullptr, nullptr, 0, nullptr}  /* sentinel */
};

} // namespace Gui

namespace {
    void setImportImageFormats()
    {
        QList<QByteArray> supportedFormats = QImageReader::supportedImageFormats();
        std::stringstream str;
        str << "Image formats (";
        for (const auto& ext : supportedFormats) {
            str << "*." << ext.constData() << " *." << ext.toUpper().constData() << " ";
        }
        str << ")";

        std::string filter = str.str();
        App::GetApplication().addImportType(filter.c_str(), "FreeCADGui");
    }
}

namespace {

// The parameter file the active theme reads its style parameters from:
// an explicit override wins, otherwise the theme's own file next to the
// stylesheets (the "qss" search path set up in initApplication()).
std::string styleParametersFilePath()
{
    const auto hGrp = App::GetApplication().GetParameterGroupByPath(
        "User parameter:BaseApp/Preferences/MainWindow");
    if (const std::string& path = hGrp->GetASCII("ThemeStyleParametersFile");
        !path.empty()) {
        return path;
    }
    return "qss:parameters/" + hGrp->GetASCII("Theme", "Classic") + ".yaml";
}

}  // anonymous namespace

void Application::initStyleParameterManager()
{
    // Upstream also registers parameter-change handlers here; this fork
    // already reapplies the stylesheet through the delayed handlers in
    // DlgSettingsTheme::attachObserver, and setStyleSheet() re-derives the
    // theme parameter file on every apply, so only the sources are wired.
    Base::registerServiceImplementation<StyleParameters::ParameterSource>(
        new StyleParameters::BuiltInParameterSource(
            {.name = QT_TR_NOOP("Built-in Parameters")}));

    // Upstream keeps a "Theme Parameters - Fallback" source reading
    // Themes/UserTokens, marked in their code for removal before release;
    // it is not inherited here.

    d->themeParametersSource = new StyleParameters::YamlParameterSource(
        styleParametersFilePath(),
        {.name = QT_TR_NOOP("Theme Parameters"),
         .options = StyleParameters::ParameterSourceOption::UserEditable});
    Base::registerServiceImplementation<StyleParameters::ParameterSource>(
        d->themeParametersSource);

    Base::registerServiceImplementation<StyleParameters::ParameterSource>(
        new StyleParameters::UserParameterSource(
            App::GetApplication().GetParameterGroupByPath(
                "User parameter:BaseApp/Preferences/Themes/UserParameters"),
            {.name = QT_TR_NOOP("User Parameters"),
             .options = StyleParameters::ParameterSource::UserEditable}));

    // Registration pushed each source to the provider's front, so walking
    // the provided list in reverse hands addSource() the least important
    // source first -- the manager, in turn, prefers the source added last.
    const auto sources =
        Base::provideServiceImplementations<StyleParameters::ParameterSource>();
    for (auto* source : std::views::all(sources) | std::views::reverse) {
        d->styleParameterManager->addSource(source);
    }

    Base::registerServiceImplementation(d->styleParameterManager);
}

Application::Application(bool GUIenabled)
{
    //App::GetApplication().Attach(this);
    if (GUIenabled) {
        // the sandbox guest's FreeCADGui reaches the host through the
        // gui.* bridge ops (docs/Sandbox.md 7.9)
        SandboxGui::registerOps();
        //NOLINTBEGIN
        App::GetApplication().signalNewDocument.connect(
            std::bind(&Gui::Application::slotNewDocument, this, sp::_1, sp::_2));
        App::GetApplication().signalDeleteDocument.connect(
            std::bind(&Gui::Application::slotDeleteDocument, this, sp::_1));
        App::GetApplication().signalRenameDocument.connect(
            std::bind(&Gui::Application::slotRenameDocument, this, sp::_1));
        App::GetApplication().signalActiveDocument.connect(
            std::bind(&Gui::Application::slotActiveDocument, this, sp::_1));
        App::GetApplication().signalRelabelDocument.connect(
            std::bind(&Gui::Application::slotRelabelDocument, this, sp::_1));
        App::GetApplication().signalShowHidden.connect(
            std::bind(&Gui::Application::slotShowHidden, this, sp::_1));
        //NOLINTEND

        App::GetApplication().signalFinishOpenDocument.connect([]() {
            std::vector<App::Document*> docs;
            for(auto doc : App::GetApplication().getDocuments()) {
                if(doc->testStatus(App::Document::RecomputeOnRestore)) {
                    docs.push_back(doc);
                    doc->setStatus(App::Document::RecomputeOnRestore, false);
                }
            }
            if(docs.empty() || !App::DocumentParams::getWarnRecomputeOnRestore())
                return;
            WaitCursor wc;
            wc.restoreCursor();
            auto res = QMessageBox::warning(getMainWindow(), QObject::tr("Recompution required"),
                QObject::tr("Some document(s) require recomputation for migration purpose. "
                            "It is highly recommended to perform a recomputation before "
                            "any modification to avoid compatibility problem.\n\n"
                            "Do you want to recompute now?"),
                QMessageBox::Yes|QMessageBox::No, QMessageBox::Yes);
            if(res != QMessageBox::Yes)
                return;
            bool hasError = false;
            for(auto doc : App::Document::getDependentDocuments(docs,true)) {
                try {
                    doc->recompute({},false,&hasError);
                } catch (Base::Exception &e) {
                    e.ReportException();
                    hasError = true;
                }
            }
            if(hasError)
                QMessageBox::critical(getMainWindow(), QObject::tr("Recompute error"),
                        QObject::tr("Failed to recompute some document(s).\n"
                                    "Please check report view for more details."));
        });

        // install the last active language
        ParameterGrp::handle hPGrp =
            App::GetApplication().GetUserParameter().GetGroup("BaseApp");
        hPGrp = hPGrp->GetGroup("Preferences")->GetGroup("General");
        QString lang = QLocale::languageToString(QLocale().language());
        Translator::instance()->activateLanguage(
            hPGrp->GetASCII("Language", (const char*)lang.toUtf8()).c_str());
        GetWidgetFactorySupplier();

        // Coin3d disabled VBO support for all Intel drivers but in the meantime they have improved
        // so we can try to override the workaround by setting COIN_VBO
        ParameterGrp::handle hViewGrp = App::GetApplication().GetParameterGroupByPath(
            "User parameter:BaseApp/Preferences/View");
        if (hViewGrp->GetBool("UseVBO",false)) {
            (void)coin_setenv("COIN_VBO", "-1", true);
        }

        // Check for the symbols for group separator and decimal point. They must be different
        // otherwise Qt doesn't work properly.
#if defined(Q_OS_WIN32)
        if (QLocale().groupSeparator() == QLocale().decimalPoint()) {
            QMessageBox::critical(
                0,
                QStringLiteral("Invalid system settings"),
                QStringLiteral(
                    "Your system uses the same symbol for decimal point and group separator.\n\n"
                    "This causes serious problems and makes the application fail to work "
                    "properly.\n"
                    "Go to the system configuration panel of the OS and fix this issue, please."));
            THROWM(Base::RuntimeError, "Invalid system settings")
        }
#endif

        // setting up Python binding
        Base::PyGILStateLocker lock;

        PyDoc_STRVAR(
            FreeCADGui_doc,
            "The functions in the FreeCADGui module allow working with GUI documents,\n"
            "view providers, views, workbenches and much more.\n\n"
            "The FreeCADGui instance provides a list of references of GUI documents which\n"
            "can be addressed by a string. These documents contain the view providers for\n"
            "objects in the associated App document. An App and GUI document can be\n"
            "accessed with the same name.\n\n"
            "The FreeCADGui module also provides a set of functions to work with so called\n"
            "workbenches.");

        // if this returns a valid pointer then the 'FreeCADGui' Python module was loaded,
        // otherwise the executable was launched
        PyObject* modules = PyImport_GetModuleDict();
        PyObject* module = PyDict_GetItemString(modules, "FreeCADGui");
        if (!module) {
            static struct PyModuleDef FreeCADGuiModuleDef = {
                PyModuleDef_HEAD_INIT,
                "FreeCADGui", FreeCADGui_doc, -1,
                Application::Methods,
                nullptr, nullptr, nullptr, nullptr
            };
            module = PyModule_Create(&FreeCADGuiModuleDef);

            PyDict_SetItemString(modules, "FreeCADGui", module);
        }
        else {
            // extend the method list
            PyModule_AddFunctions(module, Application::Methods);
        }
        Py::Module(module).setAttr(std::string("ActiveDocument"),Py::None());

        UiLoaderPy::init_type();
        Base::Interpreter().addType(UiLoaderPy::type_object(),
            module,"UiLoader");
        PyResource::init_type();

        // PySide additions
        PyModule_AddObject(module, "PySideUic", Base::Interpreter().addModule(new PySideUicModule));

        ExpressionBindingPy::init_type();
        Base::Interpreter().addType(ExpressionBindingPy::type_object(),
            module,"ExpressionBinding");

        //insert Selection module
        static struct PyModuleDef SelectionModuleDef = {
            PyModuleDef_HEAD_INIT,
            "Selection", "Selection module", -1,
            SelectionSingleton::Methods,
            nullptr, nullptr, nullptr, nullptr
        };
        PyObject* pSelectionModule = PyModule_Create(&SelectionModuleDef);
        Py_INCREF(pSelectionModule);
        PyModule_AddObject(module, "Selection", pSelectionModule);

        SelectionFilterPy::init_type();
        Base::Interpreter().addType(SelectionFilterPy::type_object(),
            pSelectionModule,"Filter");

        Gui::TaskView::ControlPy::init_type();
        Py::Module(module).setAttr(std::string("Control"),
            Py::Object(Gui::TaskView::ControlPy::getInstance(), true));
        Gui::TaskView::TaskDialogPy::init_type();

        registerUserInputEnumInPython(module);

        CommandActionPy::init_type();
        Base::Interpreter().addType(CommandActionPy::type_object(),
            module, "CommandAction");

        Base::Interpreter().addType(&LinkViewPy::Type, module, "LinkView");
        Base::Interpreter().addType(&AxisOriginPy::Type, module, "AxisOrigin");
        Base::Interpreter().addType(&CommandPy::Type, module, "Command");
        Base::Interpreter().addType(&FileDialogPy::Type,module, "FileDialog");
        Base::Interpreter().addType(&DocumentPy::Type, module, "Document");
        Base::Interpreter().addType(&ViewProviderPy::Type, module, "ViewProvider");
        Base::Interpreter().addType(
            &ViewProviderDocumentObjectPy::Type, module, "ViewProviderDocumentObject");
        Base::Interpreter().addType(&ViewProviderLinkPy::Type, module, "ViewProviderLink");
        Base::Interpreter().addType(&ViewProviderSavedViewPy::Type, module, "ViewProviderSavedView");
    }

    Base::PyGILStateLocker lock;
    PyObject *module = PyImport_AddModule("FreeCADGui");
    PyMethodDef *meth = FreeCADGui_methods;
    PyObject *dict = PyModule_GetDict(module);
    for (; meth->ml_name != nullptr; meth++) {
        PyObject *descr;
        descr = PyCFunction_NewEx(meth,nullptr,nullptr);
        if (!descr)
            break;
        if (PyDict_SetItemString(dict, meth->ml_name, descr) != 0)
            break;
        Py_DECREF(descr);
    }

    SoQtOffscreenRendererPy::init_type();
    Base::Interpreter().addType(SoQtOffscreenRendererPy::type_object(),
        module,"SoQtOffscreenRenderer");

    App::Application::Config()["COIN_VERSION"] = COIN_VERSION;

    // Python console binding
    PythonDebugModule           ::init_module();
    PythonStdout                ::init_type();
    PythonStderr                ::init_type();
    OutputStdout                ::init_type();
    OutputStderr                ::init_type();
    PythonStdin                 ::init_type();
    MainWindowPy                ::init_type();
    View3DInventorViewerPy      ::init_type();

    d = new ApplicationP(GUIenabled);

    if (GUIenabled) {
        initStyleParameterManager();
    }

    // global access
    Instance = this;

    // instantiate the workbench dictionary
    _pcWorkbenchDictionary = PyDict_New();

    if (GUIenabled) {
        createStandardOperations();
        MacroCommand::load();
    }
}

Application::~Application()
{
    Base::Console().Log("Destruct Gui::Application\n");
    WorkbenchManager::destruct();
    WorkbenchManipulator::removeAll();
    SelectionSingleton::destruct();
    Translator::destruct();
    WidgetFactorySupplier::destruct();
    BitmapFactoryInst::destruct();

    Base::PyGILStateLocker lock;
    Py_DECREF(_pcWorkbenchDictionary);

    // save macros
    try {
        MacroCommand::save();
    }
    catch (const Base::Exception& e) {
        std::cerr << "Saving macros failed: " << e.what() << std::endl;
    }

    delete d;
    Instance = nullptr;
}


//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
// creating std commands
//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

void Application::open(const char* FileName, const char* Module)
{
    WaitCursor wc;
    wc.setIgnoreEvents(WaitCursor::NoEvents);
    Base::FileInfo File(FileName);
    string te = File.extension();
    string unicodepath = Base::Tools::pythonLiteral(File.filePath());

    // if the active document is empty and not modified, close it
    // in case of an automatically created empty document at startup
    App::Document* act = App::GetApplication().getActiveDocument();
    Gui::Document* gui = this->getDocument(act);
    if (act && act->countObjects() == 0 && gui && !gui->isModified()){
        Command::doCommand(Command::App, "App.closeDocument('%s')", act->getName());
        qApp->processEvents(); // an update is needed otherwise the new view isn't shown
    }

    if (Module) {
        try {
            if (File.isDir() || File.hasExtension("FCStd")) {
                bool handled = false;
                std::string filepath = File.filePath();
                for (auto &v : d->documents) {
                    auto doc = v.second->getDocument();
                    std::string fi = Base::FileInfo(doc->FileName.getValue()).filePath();
                    if (filepath == fi) {
                        handled = true;
                        Command::doCommand(Command::App, "FreeCADGui.reload('%s')", doc->getName());
                        break;
                    }
                }

                if (!handled)
                    Command::doCommand(
                        Command::App, "FreeCAD.openDocument(%s)", unicodepath.c_str());
            }
            else {
                // issue module loading
                Command::doCommand(Command::App, "import %s", Module);

                // load the file with the module
                Command::doCommand(Command::App, "%s.open(%s)", Module, unicodepath.c_str());

                // ViewFit
                if (sendHasMsgToActiveView("ViewFit")) {
                    ParameterGrp::handle hGrp = App::GetApplication().GetParameterGroupByPath
                        ("User parameter:BaseApp/Preferences/View");
                    if (hGrp->GetBool("AutoFitToView", true))
                        Command::doCommand(Command::Gui, "Gui.SendMsgToActiveView(\"ViewFit\")");
                }
            }

            // the original file name is required
            QString filename = QString::fromUtf8(File.filePath().c_str());
            getMainWindow()->appendRecentFile(filename);
            FileDialog::setWorkingDirectory(filename);
        }
        catch (const Base::PyException& e){
            // Usually thrown if the file is invalid somehow
            e.ReportException();
        }
    }
    else {
        wc.restoreCursor();
        QMessageBox::warning(getMainWindow(), QObject::tr("Unknown filetype"),
            QObject::tr("Cannot open unknown filetype: %1").arg(QString::fromUtf8(te.c_str())));
        wc.setWaitCursor();
        return;
    }
}

void Application::importFrom(const char* FileName, const char* DocName, const char* Module)
{
    WaitCursor wc;
    wc.setIgnoreEvents(WaitCursor::NoEvents);
    Base::FileInfo File(FileName);
    std::string te = File.extension();
    string unicodepath = Base::Tools::pythonLiteral(File.filePath());

    if (Module) {
        try {
            // issue module loading
            Command::doCommand(Command::App, "import %s", Module);

            // load the file with the module
            if (File.hasExtension("FCStd")) {
                Command::doCommand(Command::App, "%s.open(%s)"
                                               , Module, unicodepath.c_str());
                if (activeDocument())
                    activeDocument()->setModified(false);
            }
            else {
                // Open transaction when importing a file
                Gui::Document* doc = DocName ? getDocument(DocName) : activeDocument();
                bool pendingCommand = false;
                App::Document *appDoc;
                if (!doc) {
                    appDoc = App::GetApplication().newDocument();
                } else {
                    appDoc = doc->getDocument();
                    pendingCommand = doc->hasPendingCommand();
                    if (!pendingCommand)
                        doc->openCommand(QT_TRANSLATE_NOOP("Command", "Import"));
                }

                std::string dname = appDoc->getName();
                std::set<long> ids;
                for (auto obj : appDoc->getObjects())
                    ids.insert(obj->getID());
                {
                    Base::ObjectStatusLocker<App::Document::Status, App::Document>
                        guard(App::Document::Restoring, appDoc);
                    if (DocName) {
                        Command::doCommand(Command::App, "%s.insert(%s,\"%s\")"
                                                    , Module, unicodepath.c_str(), DocName);
                    }
                    else {
                        Command::doCommand(Command::App, "%s.insert(%s)"
                                                    , Module, unicodepath.c_str());
                    }
                }

                appDoc = App::GetApplication().getDocument(dname.c_str());
                if (appDoc) {
                    // Must copy the returned object array, because calling
                    // 'afterImport' below may possibily add new objects, thus
                    // invalidating the returned constant object array.
                    auto objs = appDoc->getObjects();

                    if (!doc && objs.empty())
                        App::GetApplication().closeDocument(dname.c_str());
                    else {
                        auto gdoc = getDocument(appDoc);
                        for (auto obj : objs) {
                            if (!ids.count(obj->getID())) {
                                appDoc->afterImport(obj);
                                auto vp = Base::freecad_dynamic_cast<ViewProviderDocumentObject>(gdoc->getViewProvider(obj));
                                if (vp)
                                    vp->finishRestoring();
                            }
                        }
                    }
                }

                // Commit the transaction
                if (doc && !pendingCommand) {
                    doc->commitCommand();
                }

                // It's possible that before importing a file the document with the
                // given name doesn't exist or there is no active document.
                // The import function then may create a new document.
                if (!doc) {
                    doc = activeDocument();
                }

                if (doc) {
                    doc->setModified(true);

                    ParameterGrp::handle hGrp = App::GetApplication().GetParameterGroupByPath
                        ("User parameter:BaseApp/Preferences/View");
                    if (hGrp->GetBool("AutoFitToView", true)) {
                        MDIView* view = doc->getActiveView();
                        if (view) {
                            const char* ret = nullptr;
                            if (view->onMsg("ViewFit", &ret))
                                updateActions(true);
                        }
                    }
                }
            }

            // the original file name is required
            QString filename = QString::fromUtf8(File.filePath().c_str());
            auto parameterGroup = App::GetApplication().GetParameterGroupByPath(
                "User parameter:BaseApp/Preferences/General");
            bool addToRecent = parameterGroup->GetBool("RecentIncludesImported", true);
            parameterGroup->SetBool("RecentIncludesImported",
                                    addToRecent);// Make sure it gets added to the parameter list
            if (addToRecent) {
                getMainWindow()->appendRecentFile(filename);
            }
            FileDialog::setWorkingDirectory(filename);
        }
        catch (const Base::PyException& e){
            // Usually thrown if the file is invalid somehow
            e.ReportException();
        }
    }
    else {
        wc.restoreCursor();
        QMessageBox::warning(getMainWindow(), QObject::tr("Unknown filetype"),
            QObject::tr("Cannot open unknown filetype: %1").arg(QString::fromUtf8(te.c_str())));
        wc.setWaitCursor();
    }
}

void Application::exportTo(const char* FileName, const char* DocName, const char* Module)
{
    WaitCursor wc;
    wc.setIgnoreEvents(WaitCursor::NoEvents);
    Base::FileInfo File(FileName);
    std::string te = File.extension();
    string unicodepath = Base::Tools::pythonLiteral(File.filePath());

    if (Module) {
        try {
            std::vector<App::DocumentObject*> sel = Gui::Selection().getObjectsOfType
                (App::DocumentObject::getClassTypeId(),DocName);
            if (sel.empty()) {
                App::Document* doc = App::GetApplication().getDocument(DocName);
                sel = doc->getObjectsOfType(App::DocumentObject::getClassTypeId());
            }

            std::stringstream str;
            std::set<App::DocumentObject*> unique_objs;

            str << "import " << Module << "\n"
                << "__objs__=[]\n"
                << "if hasattr(" << Module << ", 'exportSelection'):\n"
                << "    __objs__=" << Module << ".exportSelection(" << unicodepath << ")\n"
                << "else:\n";

            for (std::vector<App::DocumentObject*>::iterator it = sel.begin(); it != sel.end(); ++it) {
                if (unique_objs.insert(*it).second)
                    str << "    __objs__.append(" << (*it)->getFullName(true) << ")\n";
            }
            str << "    if hasattr(" << Module << ", \"exportOptions\"):\n"
                << "        options = " << Module << ".exportOptions(" << unicodepath << ")\n"
                << "        " << Module << ".export(__objs__, " << unicodepath << ", options)\n"
                << "    else:\n"
                << "        " << Module << ".export(__objs__, " << unicodepath << ")\n";

            std::string code = str.str();
            // the original file name is required

            Gui::Command::runCommand(Gui::Command::App, code.c_str());

            auto parameterGroup = App::GetApplication().GetParameterGroupByPath(
                "User parameter:BaseApp/Preferences/General");
            bool addToRecent = parameterGroup->GetBool("RecentIncludesExported", false);
            parameterGroup->SetBool("RecentIncludesExported",
                                    addToRecent);// Make sure it gets added to the parameter list
            if (addToRecent) {
                // search for a module that is able to open the exported file because otherwise
                // it doesn't need to be added to the recent files list (#0002047)
                std::map<std::string, std::string> importMap =
                    App::GetApplication().getImportFilters(te.c_str());
                if (!importMap.empty())
                    getMainWindow()->appendRecentFile(QString::fromUtf8(File.filePath().c_str()));
            }
            // allow exporters to pass _objs__ to submodules before deleting it
            Gui::Command::runCommand(Gui::Command::App, "del __objs__");
        }
        catch (const Base::PyException& e){
            // Usually thrown if the file is invalid somehow
            e.ReportException();
            wc.restoreCursor();
            QMessageBox::critical(getMainWindow(), QObject::tr("Export failed"),
                QString::fromUtf8(e.what()));
            wc.setWaitCursor();
        }
    }
    else {
        wc.restoreCursor();
        QMessageBox::warning(getMainWindow(), QObject::tr("Unknown filetype"),
            QObject::tr("Cannot save to unknown filetype: %1").arg(QString::fromUtf8(te.c_str())));
        wc.setWaitCursor();
    }
}

void Application::createStandardOperations()
{
    // register the application Standard commands from CommandStd.cpp
    Gui::CreateStdCommands();
    Gui::CreateDocCommands();
    Gui::CreateFeatCommands();
    Gui::CreateMacroCommands();
    Gui::CreateViewStdCommands();
    Gui::CreateWindowStdCommands();
    Gui::CreateStructureCommands();
    Gui::CreateTestCommands();
    Gui::CreateLinkCommands();
}

void Application::slotNewDocument(const App::Document& Doc, bool isMainDoc)
{
#ifdef FC_DEBUG
    std::map<const App::Document*, Gui::Document*>::const_iterator it = d->documents.find(&Doc);
    assert(it==d->documents.end());
#endif
    auto pDoc = new Gui::Document(const_cast<App::Document*>(&Doc),this);
    d->documents[&Doc] = pDoc;

    //NOLINTBEGIN
    // connect the signals to the application for the new document
    pDoc->signalNewObject.connect(std::bind(&Gui::Application::slotNewObject, this, sp::_1));
    pDoc->signalDeletedObject.connect(std::bind(&Gui::Application::slotDeletedObject,
        this, sp::_1));
    pDoc->signalChangedObject.connect(std::bind(&Gui::Application::slotChangedObject,
        this, sp::_1, sp::_2));
    pDoc->signalRelabelObject.connect(std::bind(&Gui::Application::slotRelabelObject,
        this, sp::_1));
    pDoc->signalActivatedObject.connect(std::bind(&Gui::Application::slotActivatedObject,
        this, sp::_1));
    pDoc->signalInEdit.connect(std::bind(&Gui::Application::slotInEdit, this, sp::_1));
    pDoc->signalResetEdit.connect(std::bind(&Gui::Application::slotResetEdit, this, sp::_1));
    //NOLINTEND

    signalNewDocument(*pDoc, isMainDoc);
    if (isMainDoc)
        pDoc->createView(View3DInventor::getClassTypeId());
}

void Application::slotDeleteDocument(const App::Document& Doc)
{
    std::map<const App::Document*, Gui::Document*>::iterator doc = d->documents.find(&Doc);
    if (doc == d->documents.end()) {
        Base::Console().Log("GUI document '%s' already deleted\n", Doc.getName());
        return;
    }

    // If the active window does not belong to the active view, then the
    // MainWindow will not receive active view change signal, and there will be
    // no auto switching new active document. We need to do it manually.
    bool tryNewActiveDocument = (doc->second->getActiveView() != getMainWindow()->activeWindow());

    // Inside beforeDelete() a view provider may finish editing mode
    // and therefore can alter the selection.
    doc->second->beforeDelete();

    // We must clear the selection here to notify all observers.
    // And because of possible cross document link, better clear all selection
    // to be safe
    Gui::Selection().clearCompleteSelection();
    doc->second->signalDeleteDocument(*doc->second);
    signalDeleteDocument(*doc->second);

    // If the active document gets destructed we must set it to 0. If there are further existing
    // documents then the view that becomes active sets the active document again. So, we needn't
    // worry about this.
    if (d->activeDocument == doc->second)
        setActiveDocument(nullptr);

    d->viewproviderMap.deleteDocument(Doc);

    for (auto obj : Doc.getObjects())
        d->viewproviderMap.deleteObject(obj);

    // For exception-safety use a smart pointer
    unique_ptr<Document> delDoc (doc->second);
    d->documents.erase(doc);

    if(tryNewActiveDocument)
        switchActiveDocument();
}

void Application::slotRelabelDocument(const App::Document& Doc)
{
    std::map<const App::Document*, Gui::Document*>::iterator doc = d->documents.find(&Doc);
#ifdef FC_DEBUG
    assert(doc!=d->documents.end());
#endif

    signalRelabelDocument(*doc->second);
    doc->second->onRelabel();
}

void Application::slotRenameDocument(const App::Document& Doc)
{
    std::map<const App::Document*, Gui::Document*>::iterator doc = d->documents.find(&Doc);
#ifdef FC_DEBUG
    assert(doc!=d->documents.end());
#endif

    signalRenameDocument(*doc->second);
}

void Application::slotShowHidden(const App::Document& Doc)
{
    std::map<const App::Document*, Gui::Document*>::iterator doc = d->documents.find(&Doc);
#ifdef FC_DEBUG
    assert(doc!=d->documents.end());
#endif

    signalShowHidden(*doc->second);
}

void Application::slotActiveDocument(const App::Document& Doc)
{
    std::map<const App::Document*, Gui::Document*>::iterator doc = d->documents.find(&Doc);
    // this can happen when closing a document with two views opened
    if (doc != d->documents.end()) {
        // this can happen when calling App.setActiveDocument directly from Python
        // because no MDI view will be activated
        if (d->activeDocument != doc->second) {
            d->activeDocument = doc->second;
            if (d->activeDocument) {
                Base::PyGILStateLocker lock;
                Py::Object active(d->activeDocument->getPyObject(), true);
                Py::Module("FreeCADGui").setAttr(std::string("ActiveDocument"),active);

                auto view = getMainWindow()->activeWindow();
                if(!view || view->getAppDocument()!=&Doc) {
                    Gui::MDIView* view = d->activeDocument->getActiveView();
                    getMainWindow()->setActiveWindow(view);
                }
            }
            else {
                Base::PyGILStateLocker lock;
                Py::Module("FreeCADGui").setAttr(std::string("ActiveDocument"),Py::None());
            }
        }

        // Update the application to show the unit change
        ParameterGrp::handle hGrp = App::GetApplication().GetParameterGroupByPath
            ("User parameter:BaseApp/Preferences/Units");
        if( Doc.FileName.getValue()[0] != '\0' &&  ! hGrp->GetBool("IgnoreProjectSchema")) {
            int userSchema = Doc.UnitSystem.getValue();
            Base::UnitsApi::setSchema(static_cast<Base::UnitSystem>(userSchema));
            getMainWindow()->setUserSchema(userSchema);
            Application::Instance->onUpdate();
        }else{// set up Unit system default
			Base::UnitsApi::setSchema((Base::UnitSystem)hGrp->GetInt("UserSchema",0));
			Base::UnitsApi::setDecimals(hGrp->GetInt("Decimals", Base::UnitsApi::getDecimals()));
        }
        signalActiveDocument(*doc->second);
        updateActions();
    }
}

void Application::slotNewObject(const ViewProvider& vp)
{
    d->viewproviderMap.newObject(vp);
    this->signalNewObject(vp);
}

void Application::slotDeletedObject(const ViewProvider& vp)
{
    this->signalDeletedObject(vp);
    if (d->viewproviderMap.deleteObject(vp))
        d->timer.start(100);
}

void Application::slotChangedObject(const ViewProvider& vp, const App::Property& prop)
{
    this->signalChangedObject(vp,prop);
    updateActions(true);
}

void Application::slotRelabelObject(const ViewProvider& vp)
{
    this->signalRelabelObject(vp);
}

void Application::slotActivatedObject(const ViewProvider& vp)
{
    this->signalActivatedObject(vp);
    updateActions();
}

void Application::slotInEdit(const Gui::ViewProviderDocumentObject& vp)
{
    this->signalInEdit(vp);
}

void Application::slotResetEdit(const Gui::ViewProviderDocumentObject& vp)
{
    this->signalResetEdit(vp);
}

void Application::switchActiveDocument()
{
    if (App::GetApplication().isClosingAll() || d->activeDocument || d->documents.empty())
        return;
    Document *gdoc = 0;
    for(auto &v : d->documents) {
        if(v.second->getDocument()->testStatus(App::Document::TempDoc))
            continue;
        else if (!gdoc)
            gdoc = v.second;
        Gui::MDIView* view = v.second->getActiveView();
        if(view) {
            setActiveDocument(v.second);
            getMainWindow()->setActiveWindow(view);
            return;
        }
    }
    if(gdoc) {
        setActiveDocument(gdoc);
        activateView(View3DInventor::getClassTypeId(),true);
    }
}

void Application::onLastWindowClosed(Gui::Document* pcDoc)
{
    try {
        if (!d->isClosing && pcDoc) {
            // Call the closing mechanism from Python. This also checks whether pcDoc is the last open document.
            Command::doCommand(Command::Doc, "App.closeDocument(\"%s\")", pcDoc->getDocument()->getName());
            switchActiveDocument();
        }
    }
    catch (const Base::Exception& e) {
        e.ReportException();
    }
    catch (const Py::Exception&) {
        Base::PyException e;
        e.ReportException();
    }
    catch (const std::exception& e) {
        Base::Console().Error(
            "Unhandled std::exception caught in Application::onLastWindowClosed.\n"
            "The error message is: %s\n",
            e.what());
    }
    catch (...) {
        Base::Console().Error(
            "Unhandled unknown exception caught in Application::onLastWindowClosed.\n");
    }
}

/// send Messages to the active view
bool Application::sendMsgToActiveView(const char* pMsg, const char** ppReturn)
{
    MDIView* pView = getMainWindow()->activeWindow();
    bool res = pView ? pView->onMsg(pMsg,ppReturn) : false;
    updateActions(true);
    return res;
}

bool Application::sendHasMsgToActiveView(const char* pMsg)
{
    MDIView* pView = getMainWindow()->activeWindow();
    return pView ? pView->onHasMsg(pMsg) : false;
}

/// send Messages to the active view
bool Application::sendMsgToFocusView(const char* pMsg, const char** ppReturn)
{
    MDIView* pView = getMainWindow()->activeWindow();
    if(!pView)
        return false;
    for(auto focus=qApp->focusWidget();focus;focus=focus->parentWidget()) {
        if(focus == pView) {
            bool res = pView->onMsg(pMsg,ppReturn);
            updateActions(true);
            return res;
        }
    }
    return false;
}

bool Application::sendHasMsgToFocusView(const char* pMsg)
{
    MDIView* pView = getMainWindow()->activeWindow();
    if(!pView)
        return false;
    for(auto focus=qApp->focusWidget();focus;focus=focus->parentWidget()) {
        if(focus == pView)
            return pView->onHasMsg(pMsg);
    }
    return false;
}

Gui::MDIView* Application::activeView() const
{
    if (activeDocument())
        return activeDocument()->getActiveView();
    else
        return nullptr;
}

/**
 * @brief Application::activateView
 * Activates a view of the given type of the active document.
 * If a view of this type doesn't exist and \a create is true
 * a new view of this type will be created.
 * @param type
 * @param create
 */
void Application::activateView(const Base::Type& type, bool create)
{
    Document* doc = activeDocument();
    if (doc) {
        MDIView* mdiView = doc->getActiveView();
        if (mdiView && mdiView->isDerivedFrom(type)) {
            doc->setActiveWindow(mdiView);
            return;
        }
        std::list<MDIView*> mdiViews = doc->getMDIViewsOfType(type);
        if (!mdiViews.empty())
            doc->setActiveWindow(mdiViews.back());
        else if (create)
            doc->createView(type);
    }
}

/// Getter for the active view
Gui::Document* Application::activeDocument() const
{
    return d->activeDocument;
}

Gui::Document* Application::editDocument() const
{
    return d->editDocument;
}

Gui::MDIView* Application::editViewOfNode(SoNode *node) const
{
    // getViewOfNode() searches the scene graph that can be slow. It can be
    // simplified here since we only allow one editing view at the moment.
    //
    // return d->editDocument?d->editDocument->getViewOfNode(node):nullptr;
    
    (void)node;
    return d->editDocument?d->editDocument->getEditingViewOfViewProvider(nullptr):nullptr;
}

void Application::setEditDocument(Gui::Document *doc) {
    if(doc == d->editDocument)
        return;
    if(!doc)
        d->editDocument = nullptr;
    for(auto &v : d->documents)
        v.second->_resetEdit();
    d->editDocument = doc;
    updateActions();
}

void Application::setActiveDocument(Gui::Document* pcDocument)
{
    if (d->activeDocument == pcDocument)
        return; // nothing needs to be done

    updateActions();

    if (pcDocument) {
        // This happens if a document with more than one view is about being
        // closed and a second view is activated. The document is still not
        // removed from the map.
        App::Document* doc = pcDocument->getDocument();
        if (d->documents.find(doc) == d->documents.end())
            return;
    }
    d->activeDocument = pcDocument;
    std::string nameApp, nameGui;

    // This adds just a line to the macro file but does not set the active document
    // Macro recording of this is problematic, thus it's written out as comment.
    if (pcDocument){
        nameApp += "App.setActiveDocument(\"";
        nameApp += pcDocument->getDocument()->getName();
        nameApp +=  "\")\n";
        nameApp += "App.ActiveDocument=App.getDocument(\"";
        nameApp += pcDocument->getDocument()->getName();
        nameApp +=  "\")";
        macroManager()->addLine(MacroManager::Cmt,nameApp.c_str());
        nameGui += "Gui.ActiveDocument=Gui.getDocument(\"";
        nameGui += pcDocument->getDocument()->getName();
        nameGui +=  "\")";
        macroManager()->addLine(MacroManager::Cmt,nameGui.c_str());
    }
    else {
        nameApp += "App.setActiveDocument(\"\")\n";
        nameApp += "App.ActiveDocument=None";
        macroManager()->addLine(MacroManager::Cmt,nameApp.c_str());
        nameGui += "Gui.ActiveDocument=None";
        macroManager()->addLine(MacroManager::Cmt,nameGui.c_str());
    }

    // Sets the currently active document
    try {
        Base::Interpreter().runString(nameApp.c_str());
        Base::Interpreter().runString(nameGui.c_str());
    }
    catch (const Base::Exception& e) {
        Base::Console().Warning(e.what());
        return;
    }

#ifdef FC_DEBUG
    // May be useful for error detection
    if (d->activeDocument) {
        App::Document* doc = d->activeDocument->getDocument();
        Base::Console().Log(
            "Active document is %s (at %p)\n", doc->getName(), static_cast<void*>(doc));
    }
    else {
        Base::Console().Log("No active document\n");
    }
#endif

    // notify all views attached to the application (not views belong to a special document)
    for(list<Gui::BaseView*>::iterator It=d->passive.begin();It!=d->passive.end();++It)
        (*It)->setDocument(pcDocument);
}

Gui::Document* Application::getDocument(const char* name) const
{
    App::Document* pDoc = App::GetApplication().getDocument( name );
    std::map<const App::Document*, Gui::Document*>::const_iterator it = d->documents.find(pDoc);
    if ( it!=d->documents.end() )
        return it->second;
    else
        return nullptr;
}

Gui::Document* Application::getDocument(const App::Document* pDoc) const
{
    std::map<const App::Document*, Gui::Document*>::const_iterator it = d->documents.find(pDoc);
    if ( it!=d->documents.end() )
        return it->second;
    else
        return nullptr;
}

void Application::showViewProvider(const App::DocumentObject* obj)
{
    ViewProvider* vp = getViewProvider(obj);
    if (vp) vp->show();
}

void Application::hideViewProvider(const App::DocumentObject* obj)
{
    ViewProvider* vp = getViewProvider(obj);
    if (vp) vp->hide();
}

Gui::ViewProvider* Application::getViewProvider(const App::DocumentObject* obj) const
{
    return d->viewproviderMap.getViewProvider(obj);
}

void Application::attachView(Gui::BaseView* pcView)
{
    d->passive.push_back(pcView);
}

void Application::detachView(Gui::BaseView* pcView)
{
    d->passive.remove(pcView);
}

void Application::onUpdate()
{
    // update all documents
    std::map<const App::Document*, Gui::Document*>::iterator It;
    for (It = d->documents.begin();It != d->documents.end();++It)
        It->second->onUpdate();
    // update all the independent views
    for (std::list<Gui::BaseView*>::iterator It2 = d->passive.begin();It2 != d->passive.end();++It2)
        (*It2)->onUpdate();
}

/// Gets called if a view gets activated, this manages the whole activation scheme
void Application::viewActivated(MDIView* pcView)
{
#ifdef FC_DEBUG
    // May be useful for error detection
    Base::Console().Log("Active view is %s (at %p)\n",
                 (const char*)pcView->windowTitle().toUtf8(),static_cast<void *>(pcView));
#endif

    signalActivateView(pcView);

    // The DisplayModeInView rows present the ACTIVE 3D view's
    // ObjectDisplayModes entries (docs/CoinRetirement.md 5.9), so a
    // view change re-reads them. Every open document, because an
    // object shown here through a Link keeps its row in the document
    // it belongs to (docs/CoinRetirement.md 5.14 fixed that for a
    // table change and left the activation sweep behind). A NON-3D
    // activation sweeps too, with no view: there is then no entry to
    // present -- and no view for the write path to write to either --
    // so the rows read "Use View Mode" instead of continuing to show
    // the last 3D view's state as if it were still in force.
    ViewProviderDocumentObject::syncDisplayModeInViewAll(
            Base::freecad_dynamic_cast<View3DInventor>(pcView));

    // Set the new active document which is taken of the activated view. If, however,
    // this view is passive we let the currently active document unchanged as we would
    // have no document active which is causing a lot of trouble.
    if (!pcView->isPassive())
        setActiveDocument(pcView->getGuiDocument());
}


void Application::updateActive()
{
    activeDocument()->onUpdate();
}

void Application::updateActions(bool delay)
{
    getMainWindow()->updateActions(delay);
}

void Application::tryClose(QCloseEvent * e)
{
    e->setAccepted(getMainWindow()->closeAllDocuments(false));
    if(!e->isAccepted())
        return;

    // ask all passive views if closable
    for (std::list<Gui::BaseView*>::iterator It = d->passive.begin();It!=d->passive.end();++It) {
        e->setAccepted((*It)->canClose());
        if (!e->isAccepted())
            return;
    }

    if (e->isAccepted()) {
        d->isClosing = true;

        std::map<const App::Document*, Gui::Document*>::iterator It;

        //detach the passive views
        //SetActiveDocument(0);
        std::list<Gui::BaseView*>::iterator itp = d->passive.begin();
        while (itp != d->passive.end()) {
            (*itp)->onClose();
            itp = d->passive.begin();
        }

        App::GetApplication().closeAllDocuments();
    }
}

int Application::getUserEditMode(const std::string &mode) const
{
    if (mode.empty()) {
        return userEditMode;
    }
    for (auto const &uem : userEditModes) {
        if (uem.second.first == mode) {
            return uem.first;
        }
    }
    return -1;
}

std::pair<std::string,std::string> Application::getUserEditModeUIStrings(int mode) const
{
    if (mode == -1) {
        return userEditModes.at(userEditMode);
    }
    if (userEditModes.find(mode) != userEditModes.end()) {
        return userEditModes.at(mode);
    }
    return std::make_pair(std::string(), std::string());
}

bool Application::setUserEditMode(int mode)
{
    if (userEditModes.find(mode) != userEditModes.end() && userEditMode != mode) {
        userEditMode = mode;
        this->signalUserEditModeChanged(userEditMode);
        return true;
    }
    return false;
}

bool Application::setUserEditMode(const std::string &mode)
{
    for (auto const &uem : userEditModes) {
        if (uem.second.first == mode) {
            return setUserEditMode(uem.first);
        }
    }
    return false;
}

bool Application::initializeWorkbench(const char *name)
{
    Workbench* wb = WorkbenchManager::instance()->getWorkbench(name);
    if (wb)
        return true; // already initialized

    Base::PyGILStateLocker lock;
    PyObject* pcWorkbench = nullptr;
    pcWorkbench = PyDict_GetItemString(_pcWorkbenchDictionary, name);
    if (!pcWorkbench)
        return false;

    try {
        initializeWorkbench(name, Py::Object(pcWorkbench));
    } catch (Base::Exception &e) {
        e.ReportException();
        return false;
    }
    return true;
}

struct ExecFileGuard
{
    ExecFileGuard(std::string &s, std::string *wb=nullptr, const char *initWb=nullptr)
        :execFile(s), saved(s), wb(wb)
    {
        if (wb) *wb = initWb;
    }

    ~ExecFileGuard()
    {
        execFile = saved;
        if (wb) wb->clear();
    }

    std::string &execFile;
    std::string saved;
    std::string *wb;
};

const char *Application::initializingWorkbench() const
{
    return d->initWorkbench.size()?d->initWorkbench.c_str():nullptr;
}

std::string Application::initializeWorkbench(const char *name, Py::Object handler)
{
    ExecFileGuard guard(_ExecFile, &d->initWorkbench, name);
    try {
        std::string type;
        if (!handler.hasAttr(std::string("__Workbench__"))) {
            WaitCursor wc;

            // call its GetClassName method if possible
            Py::Callable method(handler.getAttr(std::string("GetClassName")));
            Py::Tuple args;
            Py::String result(method.apply(args));
            type = result.as_std_string("ascii");
            if (Base::Type::fromName(type.c_str())
                    .isDerivedFrom(Gui::PythonBaseWorkbench::getClassTypeId())) {
                Workbench* wb = WorkbenchManager::instance()->createWorkbench(name, type);
                if (!wb)
                    throw Py::RuntimeError("Failed to instantiate workbench of type " + type);
                handler.setAttr(std::string("__Workbench__"), Py::Object(wb->getPyObject(), true));
            }

            auto iter = _workbenchPaths.find(name);
            if (iter == _workbenchPaths.end())
                _ExecFile.clear();
            else
                _ExecFile = iter->second;

            // Import the matching module first -- once. Neither guard above
            // stops a C++ workbench getting here twice: __Workbench__ is set
            // only once the workbench has actually been activated, and
            // WorkbenchManager holds nothing under this name until then. So a
            // module imported by Initialize() that asks whether one of its own
            // commands exists -- InvoluteGearFeature.py does exactly that --
            // reaches Command::get(), which resolves the name through
            // Preferences/Commands and calls straight back in here, running the
            // whole of Initialize() a second time and doubling every message it
            // prints. Command.cpp's own _sPendingWorkbench guard does not cover
            // it, because the outer call came from activateWorkbench().
            if (d->initializedWorkbenches.insert(name).second) {
                try {
                    Py::Callable activate(handler.getAttr(std::string("Initialize")));
                    activate.apply(args);
                }
                catch (...) {
                    // an Initialize() that failed must stay retryable
                    d->initializedWorkbenches.erase(name);
                    throw;
                }
            }

            // Dependent on the implementation of a workbench handler the type
            // can be defined after the call of Initialize()
            if (type.empty()) {
                Py::String result(method.apply(args));
                type = result.as_std_string("ascii");
            }
        }

        return type;
    }
    catch (Py::Exception&) {
        Base::PyException e;
        if (!d->startingUp)
            Base::Console().Error("%s\n", e.getStackTrace().c_str());
        else
            Base::Console().Log("%s\n", e.getStackTrace().c_str());
        throw e;
    }
}

/**
 * Activate the matching workbench to the registered workbench handler with name \a name.
 * The handler must be an instance of a class written in Python.
 * Normally, if a handler gets activated a workbench with the same name gets created unless it
 * already exists.
 *
 * The old workbench gets deactivated before. If the workbench to the handler is already
 * active or if the switch fails false is returned.
 */
bool Application::activateWorkbench(const char* name)
{
    bool ok = false;
    Workbench* oldWb = WorkbenchManager::instance()->active();
    if (oldWb && oldWb->name() == name)
        return false; // already active

    Base::PyGILStateLocker lock;
    // we check for the currently active workbench and call its 'Deactivated'
    // method, if available
    PyObject* pcOldWorkbench = nullptr;
    if (oldWb) {
        pcOldWorkbench = PyDict_GetItemString(_pcWorkbenchDictionary, oldWb->name().c_str());
    }

    // get the python workbench object from the dictionary
    PyObject* pcWorkbench = nullptr;
    pcWorkbench = PyDict_GetItemString(_pcWorkbenchDictionary, name);
    // test if the workbench exists
    if (!pcWorkbench)
        return false;

    std::string errMsg;

    try {
        Py::Object handler(pcWorkbench);
        std::string type = initializeWorkbench(name, handler);

        WaitCursor wc;

        // does the Python workbench handler have changed the workbench?
        Workbench* curWb = WorkbenchManager::instance()->active();
        if (curWb && curWb->name() == name)
            ok = true; // already active
        // now try to create and activate the matching workbench object
        else if (WorkbenchManager::instance()->activate(name, type)) {
            getMainWindow()->activateWorkbench(QString::fromUtf8(name));
            this->signalActivateWorkbench(name);
            ok = true;
        }

        // if we still not have this member then it must be built-in C++ workbench
        // which could be created after loading the appropriate module
        if (!handler.hasAttr(std::string("__Workbench__"))) {
            Workbench* wb = WorkbenchManager::instance()->getWorkbench(name);
            if (wb)
                handler.setAttr(std::string("__Workbench__"), Py::Object(wb->getPyObject(), true));
        }


        // If the method Deactivate is available we call it
        if (pcOldWorkbench) {
            Py::Object handler(pcOldWorkbench);
            if (handler.hasAttr(std::string("Deactivated"))) {
                Py::Object method(handler.getAttr(std::string("Deactivated")));
                if (method.isCallable()) {
                    Py::Tuple args;
                    Py::Callable activate(method);
                    activate.apply(args);
                }
            }
        }

        if (oldWb)
            oldWb->deactivated();

        // If the method Activate is available we call it
        if (handler.hasAttr(std::string("Activated"))) {
            Py::Object method(handler.getAttr(std::string("Activated")));
            if (method.isCallable()) {
                Py::Tuple args;
                Py::Callable activate(method);
                activate.apply(args);
            }
        }

        // now get the newly activated workbench
        Workbench* newWb = WorkbenchManager::instance()->active();
        if (newWb) {
            if (!Instance->d->startingUp) {
                std::string nameWb = newWb->name();
                App::GetApplication()
                    .GetParameterGroupByPath("User parameter:BaseApp/Preferences/General")
                    ->SetASCII("LastModule", nameWb.c_str());
            }
            newWb->activated();
        }
    }
    catch (Py::Exception&) {
        Base::PyException e; // extract the Python error text
        if (!d->startingUp)
            Base::Console().Error("%s\n", e.getStackTrace().c_str());
        else
            Base::Console().Log("%s\n", e.getStackTrace().c_str());
        errMsg = e.what();
    }
    catch (Base::Exception &e) {
        errMsg = e.what();
    }

    if (errMsg.size()) {
        QString msg = QString::fromUtf8(errMsg.c_str());
        QRegularExpression rx;
        // ignore '<type 'exceptions.ImportError'>' prefixes
        rx.setPattern(QStringLiteral("^\\s*<type 'exceptions.ImportError'>:\\s*"));
        auto match = rx.match(msg);
        while (match.hasMatch()) {
            msg = msg.mid(match.capturedLength());
            match = rx.match(msg);
        }

        Base::Console().Error("%s\n", (const char*)msg.toUtf8());

        if (!d->startingUp) {
            QMessageBox::critical(getMainWindow(), QObject::tr("Workbench failure"),
                QObject::tr("%1").arg(msg));
        }
    }
    return ok;
}

QPixmap Application::workbenchIcon(const QString& wb, QString *iconPath) const
{
    Base::PyGILStateLocker lock;
    // get the python workbench object from the dictionary
    PyObject* pcWorkbench = PyDict_GetItemString(_pcWorkbenchDictionary, wb.toUtf8());
    // test if the workbench exists
    if (pcWorkbench) {
        // make a unique icon name
        std::stringstream str;
        str << "Icon_" << wb.toUtf8().constData();
        std::string iconName = str.str();
        QPixmap icon;
        std::string path;
        if (BitmapFactory().findPixmapInCache(iconName.c_str(),
                                              icon,
                                              nullptr,
                                              iconPath ? &path : nullptr))
        {
            if (iconPath)
                *iconPath = QString::fromUtf8(path.c_str());
            return icon;
        }

        // get its Icon member if possible
        try {
            Py::Object handler(pcWorkbench);
            if (handler.hasAttr(std::string("Icon"))) {
                Py::Object member = handler.getAttr(std::string("Icon"));
                Py::String data(member);
                std::string content = data.as_std_string("utf-8");
                std::string path;

                // test if in XPM format
                if (strstr(content.c_str(), "/* XPM */") != nullptr) {
                    QByteArray ary(content.c_str());
                    // Make sure to remove crap around the XPM data
                    QList<QByteArray> lines = ary.split('\n');
                    QByteArray buffer;
                    buffer.reserve(ary.size()+lines.size());
                    for (QList<QByteArray>::iterator it = lines.begin(); it != lines.end(); ++it) {
                        QByteArray trim = it->trimmed();
                        if (!trim.isEmpty()) {
                            buffer.append(trim);
                            buffer.append('\n');
                        }
                    }
                    icon.loadFromData(buffer, "XPM");
                }
                else {
                    // is it a file name...
                    QString file = QString::fromUtf8(content.c_str());
                    icon.load(file);
                    if (icon.isNull()) {
                        // ... or the name of another icon?
                        icon = BitmapFactory().pixmap(file.toUtf8(), false, nullptr, &path);
                    } else
                        path = content;
                }

                if (!icon.isNull()) {
                    BitmapFactory().addPixmapToCache(iconName.c_str(), icon, path.c_str());
                    if (iconPath)
                        *iconPath = QString::fromUtf8(path.c_str());
                }

                return icon;
            }
        }
        catch (Py::Exception& e) {
            e.clear();
        }
    }

    QIcon icon = QApplication::windowIcon();
    if (!icon.isNull()) {
        QList<QSize> s = icon.availableSizes();
        if (!s.isEmpty())
            return icon.pixmap(s[0]);
    }
    return {};
}

QString Application::workbenchToolTip(const QString& wb) const
{
    // get the python workbench object from the dictionary
    Base::PyGILStateLocker lock;
    PyObject* pcWorkbench = PyDict_GetItemString(_pcWorkbenchDictionary, wb.toUtf8());
    // test if the workbench exists
    if (pcWorkbench) {
        // get its ToolTip member if possible
        try {
            Py::Object handler(pcWorkbench);
            Py::Object member = handler.getAttr(std::string("ToolTip"));
            if (member.isString()) {
                Py::String tip(member);
                return QString::fromUtf8(tip.as_std_string("utf-8").c_str());
            }
        }
        catch (Py::Exception& e) {
            e.clear();
        }
    }

    return {};
}

QString Application::workbenchMenuText(const QString& wb) const
{
    // get the python workbench object from the dictionary
    Base::PyGILStateLocker lock;
    PyObject* pcWorkbench = PyDict_GetItemString(_pcWorkbenchDictionary, wb.toUtf8());
    // test if the workbench exists
    if (pcWorkbench) {
        // get its ToolTip member if possible
        Base::PyGILStateLocker locker;
        try {
            Py::Object handler(pcWorkbench);
            Py::Object member = handler.getAttr(std::string("MenuText"));
            if (member.isString()) {
                Py::String tip(member);
                return QString::fromUtf8(tip.as_std_string("utf-8").c_str());
            }
        }
        catch (Py::Exception& e) {
            e.clear();
        }
    }

    return {};
}

QStringList Application::workbenches() const
{
    // If neither 'HiddenWorkbench' nor 'ExtraWorkbench' is set then all workbenches are returned.
    const std::map<std::string,std::string>& config = App::Application::Config();
    auto ht = config.find("HiddenWorkbench");
    auto et = config.find("ExtraWorkbench");
    auto st = config.find("StartWorkbench");
    const char* start = (st != config.end() ? st->second.c_str() : "<none>");
    QStringList hidden, extra;
    if (ht != config.end()) {
        QString items = QString::fromUtf8(ht->second.c_str());
        hidden = items.split(QLatin1Char(';'), Qt::SkipEmptyParts);
        if (hidden.isEmpty())
            hidden.push_back(QStringLiteral(""));
    }
    if (et != config.end()) {
        QString items = QString::fromUtf8(et->second.c_str());
        extra = items.split(QLatin1Char(';'), Qt::SkipEmptyParts);
        if (extra.isEmpty())
            extra.push_back(QStringLiteral(""));
    }

    PyObject *key, *value;
    Py_ssize_t pos = 0;
    QStringList wb;
    // insert all items
    while (PyDict_Next(_pcWorkbenchDictionary, &pos, &key, &value)) {
        /* do something interesting with the values... */
        const char* wbName = PyUnicode_AsUTF8(key);
        // add only allowed workbenches
        bool ok = true;
        if (!extra.isEmpty()&&ok) {
            ok = (extra.indexOf(QString::fromUtf8(wbName)) != -1);
        }
        if (!hidden.isEmpty()&&ok) {
            ok = (hidden.indexOf(QString::fromUtf8(wbName)) == -1);
        }

        // okay the item is visible
        if (ok)
            wb.push_back(QString::fromUtf8(wbName));
        // also allow start workbench in case it is hidden
        else if (strcmp(wbName, start) == 0)
            wb.push_back(QString::fromUtf8(wbName));
    }

    return wb;
}

void Application::setupContextMenu(const char* recipient, MenuItem* items) const
{
    Workbench* actWb = WorkbenchManager::instance()->active();
    if (actWb) {
        // when populating the context-menu of a Python workbench invoke the method
        // 'ContextMenu' of the handler object
        if (actWb->isDerivedFrom<PythonWorkbench>()) {
            static_cast<PythonWorkbench*>(actWb)->clearContextMenu();
            Base::PyGILStateLocker lock;
            PyObject* pWorkbench = nullptr;
            pWorkbench = PyDict_GetItemString(_pcWorkbenchDictionary, actWb->name().c_str());

            try {
                // call its GetClassName method if possible
                Py::Object handler(pWorkbench);
                Py::Callable method(handler.getAttr(std::string("ContextMenu")));
                Py::Tuple args(1);
                args.setItem(0, Py::String(recipient));
                method.apply(args);
            }
            catch (Py::Exception& e) {
                Py::Object o = Py::type(e);
                e.clear();
                if (o.isString()) {
                    Py::String s(o);
                    std::clog << "Application::setupContextMenu: " << s.as_std_string("utf-8")
                              << std::endl;
                }
            }
        }
        actWb->createContextMenu(recipient, items);
    }
}

bool Application::isClosing()
{
    return d->isClosing;
}

// A plain static, not a member of the pimpl: the owner is a workbench
// whose queue is a function-local static of its own, and this only has
// to outlive the drains that set it. False is the honest default for a
// build with no such workbench loaded -- nothing is deferring anything.
static bool s_buildingVisuals = false;

namespace
{
/// What a progressive LOAD holds while it runs, the counterpart of what
/// Gui.setLiveImport() gives a progressive IMPORT.
struct LiveLoad
{
    /// Alive while any document is loading; this is what the progress
    /// bar's and the wait cursor's filters consult before swallowing
    /// pointer input.
    std::unique_ptr<LiveViewInteraction> navigable;
    /// The documents this turned App::Document::LiveImport on for, so the
    /// bit is cleared on exactly the ones that were claimed -- a document
    /// that had it set by an actual import must keep it.
    std::set<std::string> claimed;
};

LiveLoad& liveLoad()
{
    static LiveLoad live;
    return live;
}
}  // namespace

void Application::refreshLiveLoad(const App::Document* starting)
{
    auto& live = liveLoad();

    // Recomputed from the live state every time rather than counted up and
    // down, so any call repairs a claim that a failed or abandoned load left
    // behind. A stuck claim is not cosmetic: it refuses every AlterDoc
    // command for the rest of the session.
    std::set<std::string> loading;
    if (starting) {
        // Its Restoring bit is not set yet -- the signal that brings us here
        // is emitted one line before it -- so this is the only evidence that
        // a load is beginning, and the blocking open that claims the input
        // filter is about to start.
        loading.insert(starting->getName());
    }
    for (auto doc : App::GetApplication().getDocuments()) {
        if (doc->testStatus(App::Document::Restoring)) {
            loading.insert(doc->getName());
            continue;
        }
        // The deferred view-provider drain is still the load: its slices run
        // with the Restoring bit clear between them.
        auto guiDoc = getDocument(doc);
        if (guiDoc && guiDoc->isRestoringViewProviders()) {
            loading.insert(doc->getName());
        }
    }
    if (s_buildingVisuals) {
        // The visual drain is the last phase of a load and the only one in
        // which geometry reaches the view, but it is published as one flag
        // for all documents rather than per document. So while it runs, the
        // honest answer is that the documents which were loading still are --
        // minus any that has been closed since.
        for (const auto& name : live.claimed) {
            if (App::GetApplication().getDocument(name.c_str())) {
                loading.insert(name);
            }
        }
    }

    for (const auto& name : live.claimed) {
        if (loading.count(name)) {
            continue;
        }
        if (auto doc = App::GetApplication().getDocument(name.c_str())) {
            doc->setStatus(App::Document::LiveImport, false);
        }
    }
    for (auto it = loading.begin(); it != loading.end();) {
        if (live.claimed.count(*it)) {
            ++it;
            continue;
        }
        auto doc = App::GetApplication().getDocument(it->c_str());
        if (!doc || doc->testStatus(App::Document::LiveImport)) {
            // Already live for a reason of its own -- an import writing into
            // the document this load is reading. Not ours to set, so not ours
            // to clear when the load ends.
            it = loading.erase(it);
            continue;
        }
        doc->setStatus(App::Document::LiveImport, true);
        ++it;
    }
    live.claimed = std::move(loading);

    if (live.claimed.empty()) {
        live.navigable.reset();
    }
    else if (!live.navigable) {
        live.navigable = std::make_unique<LiveViewInteraction>();
    }
}

bool Application::isLiveLoad(const App::Document* doc) const
{
    return doc && liveLoad().claimed.count(doc->getName()) != 0;
}

void Application::setBuildingVisuals(bool building)
{
    s_buildingVisuals = building;
    // The drain emptying is the end of the load, and the only notice of it
    // this side gets.
    refreshLiveLoad();
}

bool Application::isBuildingVisuals() const
{
    return s_buildingVisuals;
}

MacroManager *Application::macroManager()
{
    return d->macroMngr;
}

CommandManager &Application::commandManager()
{
    return d->commandManager;
}

Gui::PreferencePackManager* Application::prefPackManager()
{
    return d->prefPackManager;
}

Gui::StyleParameters::ParameterManager* Application::styleParameterManager()
{
    return d->styleParameterManager;
}

void Application::setStyle(const QString& name)
{
    // The style in effect before any theme touched it, so "System"
    // can go back to it. Upstream returns nullptr there and leaves
    // whatever style the previous theme set -- a one-way door once a
    // pack has asked for "FreeCAD".
    static const QString platformStyle = qApp->style()->objectName();

    const auto createStyleFromName = [](const QString& name) -> QStyle* {
        if (name == QStringLiteral("FreeCAD")) {
            return new FreeCADStyle();
        }

        if (name.compare(QStringLiteral("System"), Qt::CaseInsensitive) == 0) {
            return QStyleFactory::create(platformStyle);
        }

        return QStyleFactory::create(name);
    };

    const auto requiresEventFilter = [](QStyle* style) {
        // for now only FreeCAD style requires additional event processing
        return qobject_cast<FreeCADStyle*>(style) != nullptr;
    };

    if (auto* current = qApp->style(); current && requiresEventFilter(current)) {
        qApp->removeEventFilter(current);
    }

    if (auto* style = createStyleFromName(name)) {
        qApp->setStyle(style);

        if (requiresEventFilter(style)) {
            qApp->installEventFilter(style);
        }
    }
}


//**************************************************************************
// Init, Destruct and singleton

namespace {
void setCategoryFilterRules()
{
    QString filter;
    QTextStream stream(&filter);
    stream << "qt.qpa.xcb.warning=false\n";
    stream << "qt.qpa.mime.warning=false\n";
    stream << "qt.svg.warning=false\n";
    stream << "qt.xkb.compose.warning=false\n";
    stream.flush();
    QLoggingCategory::setFilterRules(filter);
}
}

using _qt_msg_handler_old = void (*)(QtMsgType, const QMessageLogContext &, const QString &);
_qt_msg_handler_old old_qtmsg_handler = nullptr;

void messageHandler(QtMsgType type, const QMessageLogContext &context, const QString &msg)
{
    QByteArray output;
    if (context.category && strcmp(context.category, "default") != 0) {
        output.append('(');
        output.append(context.category);
        output.append(')');
        output.append(' ');
    }

    output.append(msg.toUtf8());

    switch (type)
    {
    case QtInfoMsg:
    case QtDebugMsg:
#ifdef FC_DEBUG
        Base::Console().Message("%s\n", output.constData());
#else
        // do not stress user with Qt internals but write to log file if enabled
        Base::Console().Log("%s\n", output.constData());
#endif
        break;
    case QtWarningMsg:
        Base::Console().Warning("%s\n", output.constData());
        break;
    case QtCriticalMsg:
        Base::Console().Error("%s\n", output.constData());
        break;
    case QtFatalMsg:
        Base::Console().Error("%s\n", output.constData());
        abort();                    // deliberately core dump
    }
#ifdef FC_OS_WIN32
    if (old_qtmsg_handler) {
        (*old_qtmsg_handler)(type, context, msg);
    }
#endif
}

#ifdef FC_DEBUG // redirect Coin messages to FreeCAD
void messageHandlerCoin(const SoError * error, void * /*userdata*/)
{
    if (error && error->getTypeId() == SoDebugError::getClassTypeId()) {
        const SoDebugError* dbg = static_cast<const SoDebugError*>(error);
        const char* msg = error->getDebugString().getString();
        switch (dbg->getSeverity())
        {
        case SoDebugError::INFO:
            Base::Console().Message("%s\n", msg);
            break;
        case SoDebugError::WARNING:
            Base::Console().Warning("%s\n", msg);
            break;
        default: // error
            Base::Console().Error("%s\n", msg);
            break;
        }
#ifdef FC_OS_WIN32
    if (old_qtmsg_handler)
        (*old_qtmsg_handler)(QtDebugMsg, QMessageLogContext(), QString::fromUtf8(msg));
#endif
    }
    else if (error) {
        const char* msg = error->getDebugString().getString();
        Base::Console().Log( msg );
    }
}

#endif

// To fix bug #0000345 move Q_INIT_RESOURCE() outside initApplication()
static void init_resources()
{
    // init resources
    Q_INIT_RESOURCE(resource);
    Q_INIT_RESOURCE(translation);
    Q_INIT_RESOURCE(FreeCAD_translation);
}

void Application::initApplication()
{
    static bool init = false;
    if (init) {
        Base::Console().Error("Tried to run Gui::Application::initApplication() twice!\n");
        return;
    }

    try {
        initTypes();
        // Move the pre-split render engine parameter keys into
        // Preferences/View/Render before anything reads them.
        RenderParams::migrate();
        ViewParams::migrate();
        // Which render path this session draws with, decided here rather
        // than read from the configuration.
        RenderParams::selectRenderPath();
        new Base::ScriptProducer( "FreeCADGuiInit", FreeCADGuiInit );
        init_resources();
        setCategoryFilterRules();
        old_qtmsg_handler = qInstallMessageHandler(messageHandler);
        init = true;
    }
    catch (...) {
        // force to flush the log
        App::Application::destructObserver();
        throw;
    }
}

void Application::initTypes()
{
    // views
    Gui::BaseView                               ::init();
    Gui::MDIView                                ::init();
    Gui::MDIViewWithCamera                      ::init();
    Gui::View3DInventor                         ::init();
    Gui::AbstractSplitView                      ::init();
    Gui::SplitView3DInventor                    ::init();
    Gui::ViewArea                               ::init();
    Gui::TextDocumentEditorView                 ::init();
    Gui::EditorView                             ::init();
    Gui::PythonEditorView                       ::init();
    // View Provider
    // Properties whose storage redirects into ShapeAppearance
    Gui::PropertyShapeColor                     ::init();
    Gui::PropertyShapeMaterial                  ::init();

    Gui::ViewProvider                           ::init();
    Gui::ViewProviderExtension                  ::init();
    Gui::ViewProviderExtensionPython            ::init();
    Gui::ViewProviderGroupExtension             ::init();
    Gui::ViewProviderGroupExtensionPython       ::init();
    Gui::ViewProviderSuppressibleExtension      ::init();
    Gui::ViewProviderSuppressibleExtensionPython::init();
    Gui::ViewProviderGeoFeatureGroupExtension   ::init();
    Gui::ViewProviderGeoFeatureGroupExtensionPython::init();
    Gui::ViewProviderOriginGroupExtension       ::init();
    Gui::ViewProviderOriginGroupExtensionPython ::init();
    Gui::ViewProviderExtern                     ::init();
    Gui::ViewProviderDocumentObject             ::init();
    Gui::ViewProviderFeature                    ::init();
    Gui::ViewProviderDocumentObjectGroup        ::init();
    Gui::ViewProviderDocumentObjectGroupPython  ::init();
    Gui::ViewProviderDragger                    ::init();
    Gui::ViewProviderGeometryObject             ::init();
    Gui::ViewProviderImagePlane                 ::init();
    Gui::ViewProviderInventorObject             ::init();
    Gui::ViewProviderVRMLObject                 ::init();
    Gui::ViewProviderAnnotation                 ::init();
    Gui::ViewProviderAnnotationLabel            ::init();
    Gui::ViewProviderPointMarker                ::init();
    Gui::ViewProviderMeasureDistance            ::init();
    Gui::ViewProviderFeaturePython              ::init();
    Gui::ViewProviderGeometryPython             ::init();
    // The fork's older type NAMES stay resolvable. A view provider is created
    // by name -- from getViewProviderName(), from a document's ViewType
    // attribute when it overrides the default, and from Python -- so renaming
    // the class alone would make those lookups fail and silently leave objects
    // with no view provider. These register the old spellings against the same
    // factory; instances still report the new type as their own.
    Base::Type::createType(Gui::ViewProviderFeaturePython::getClassTypeId(),
                           "Gui::ViewProviderPythonFeature",
                           &Gui::ViewProviderFeaturePython::create);
    Base::Type::createType(Gui::ViewProviderGeometryPython::getClassTypeId(),
                           "Gui::ViewProviderPythonGeometry",
                           &Gui::ViewProviderGeometryPython::create);
    Gui::ViewProviderPlacement                  ::init();
    Gui::ViewProviderPlacementPython            ::init();
    Gui::ViewProviderOriginFeature              ::init();
    Gui::ViewProviderPlane                      ::init();
    Gui::ViewProviderLine                       ::init();
    Gui::ViewProviderGeoFeatureGroup            ::init();
    Gui::ViewProviderGeoFeatureGroupPython      ::init();
    Gui::ViewProviderOriginGroup                ::init();
    Gui::ViewProviderPart                       ::init();
    Gui::ViewProviderOrigin                     ::init();
    Gui::ViewProviderMaterialObject             ::init();
    Gui::ViewProviderMaterialObjectPython       ::init();
    Gui::ViewProviderTextDocument               ::init();
    Gui::ViewProviderLinkObserver               ::init();
    Gui::LinkView                               ::init();
    Gui::ViewProviderLink                       ::init();
    Gui::ViewProviderLinkPython                 ::init();
    // ViewProviderAppearance derives ViewProviderLink — init after it
    Gui::ViewProviderShaderProgram              ::init();
    Gui::ViewProviderShaderProgramPython        ::init();
    Gui::ViewProviderShader                     ::init();
    Gui::ViewProviderShaderPython               ::init();
    Gui::ViewProviderAppearance                 ::init();
    Gui::ViewProviderAppearancePython           ::init();
    Gui::AxisOrigin                             ::init();
    Gui::ViewProviderSavedView                  ::init();
    Gui::ViewProviderDatum                      ::init();

    // Workbench
    Gui::Workbench                              ::init();
    Gui::StdWorkbench                           ::init();
    Gui::BlankWorkbench                         ::init();
    Gui::NoneWorkbench                          ::init();
    Gui::TestWorkbench                          ::init();
    Gui::PythonBaseWorkbench                    ::init();
    Gui::PythonBlankWorkbench                   ::init();
    Gui::PythonWorkbench                        ::init();

    // register transaction type
    new App::TransactionProducer<TransactionViewProvider>
            (ViewProviderDocumentObject::getClassTypeId());
}

void Application::initOpenInventor()
{
    // The Coin fork ships a deliberately divergent ABI under its own binary
    // name (libCoinRT). The rename keeps stock libCoin out; this check
    // catches the remaining hazard: a stale build of the fork itself, where
    // the loaded library's object layouts differ from the headers this
    // binary was compiled against and every virtual call is a coin toss.
    if (coin_fork_abi() != COIN_FORK_ABI_VERSION) {
        const char *libpath = "<unknown>";
#ifndef _WIN32
        Dl_info info;
        if (dladdr(reinterpret_cast<void*>(&coin_fork_abi), &info) && info.dli_fname)
            libpath = info.dli_fname;
#endif
        Base::Console().Error(
            "Coin library ABI mismatch: compiled against fork ABI %d, but the "
            "loaded library (%s) reports ABI %d. Rebuild/reinstall the Coin "
            "fork and anything linking it (pivy), then rebuild FreeCAD.\n",
            COIN_FORK_ABI_VERSION, libpath, coin_fork_abi());
        throw Base::RuntimeError("Coin library ABI mismatch, refusing to start "
                                 "(see the log for the loaded library path)");
    }

    // init the Inventor subsystem
    SoDB::init();
    SIM::Coin3D::Quarter::Quarter::init();
    SoFCDB::init();
}

void Application::runInitGuiScript()
{
    Base::Interpreter().runString(Base::ScriptFactory().ProduceScript("FreeCADGuiInit"));
}

namespace Gui {

GuiExport void preAppSetup();
GuiExport void postAppSetup();
GuiExport void postMainWindowSetup(MainWindow &mw);

void preAppSetup()
{
    QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);

    // FC_SWAP_INTERVAL: how many display refreshes a buffer swap waits
    // for. Qt's default is 1, which pins the frame to the vblank grid --
    // a frame whose real cost is 19.5 ms is presented in 33.3 ms on a
    // 60 Hz screen (two intervals) and 25.0 ms on a 120 Hz one (three).
    // That wait is idle, and it lands in the frame line's `outside`
    // term, where it reads as though the application were spending the
    // time. Set 0 to measure what a frame costs rather than when it is
    // shown. Not a preference: it is a measurement knob, off the
    // parameter tree on purpose so no user session inherits a busy-loop.
    //
    // ! __GL_SYNC_TO_VBLANK=0 does NOT substitute for this. It works
    // (glxgears goes 60 -> 12984 fps) and still leaves this application
    // vblank-locked, because the swap that waits is the one Qt makes for
    // the composited top-level window, not the one the driver variable
    // reaches.
    if (const char *iv = getenv("FC_SWAP_INTERVAL")) {
        QSurfaceFormat fmt = QSurfaceFormat::defaultFormat();
        fmt.setSwapInterval(std::atoi(iv));
        QSurfaceFormat::setDefaultFormat(fmt);
    }

#if (QT_VERSION >= QT_VERSION_CHECK(5, 12, 0))
    QCoreApplication::setAttribute(Qt::AA_UseDesktopOpenGL);
#endif

#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
    // The 3D view is a QOpenGLWidget, and Qt6 composites a top-level window through QRhi as
    // soon as one is present. A QQuickWidget in the same window then decides the backend --
    // and in Qt6 QWebEngineView *is* a QQuickWidget, so opening the Start page switches the
    // main window to the scene graph's default, which on Windows is D3D11. QOpenGLWidget
    // refuses to initialize into that window:
    //   "The top-level window is not using OpenGL for composition, 'D3D11' is not compatible
    //    with QOpenGLWidget"
    // followed by "No valid GL context found!" and a black 3D view. Pin both composition
    // paths to OpenGL, but let the environment win so the choice stays overridable.
    if (qEnvironmentVariableIsEmpty("QT_WIDGETS_RHI_BACKEND")) {
        qputenv("QT_WIDGETS_RHI_BACKEND", "opengl");  // read by QtGui, widget composition
    }
    if (qEnvironmentVariableIsEmpty("QSG_RHI_BACKEND")) {
        qputenv("QSG_RHI_BACKEND", "opengl");  // read by QtQuick, scene graph
    }
#endif

#if defined(FC_OS_LINUX)
    // Under WSLg, prefer xcb over Wayland.
    //
    // WSLg does not run an ordinary compositor: it runs weston with the rdprail
    // shell, where every Wayland window becomes its own Windows window streamed
    // over RDP. A destroyed popup's pixels are left on screen there. Measured
    // 2026-08-19: picking an entry in any combo box leaves the drop-down list
    // painted, with the widget reporting hidden, its QWindow reporting hidden,
    // activePopupWidget() null and nothing holding a grab -- the popup is gone
    // and only the image remains. Because Qt puts a non-editable combo's popup
    // over the combo itself, the next click lands on the combo underneath and
    // opens the list again, so it reads as a drop-down that refuses to close.
    // A bare Qt dialog with one QComboBox reproduces it, so no application code
    // is involved; the same build on xcb does not.
    //
    // The swap is free: the platform plugin has no bearing on GL here. Measured
    // on both plugins, a bare launch gets llvmpipe and the d3d12 driver env gets
    // D3D12, with the adapter decided by MESA_D3D12_DEFAULT_ADAPTER_NAME -- the
    // same renderer string either way.
    //
    // Overridable two ways, so this can be dropped when WSLg fixes it: an
    // explicit QT_QPA_PLATFORM, or the parameter. NOT by Qt's -platform switch
    // -- FreeCAD's own option parser rejects it and the process exits before
    // Qt sees the argument, verified.
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")
            && !qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY")
            && !qEnvironmentVariableIsEmpty("DISPLAY")) {
        bool underWsl = !qEnvironmentVariableIsEmpty("WSL_DISTRO_NAME")
                     || !qEnvironmentVariableIsEmpty("WSL_INTEROP");
        if (!underWsl) {
            // The environment carries WSL_* only for a shell-launched process,
            // so ask the kernel as well.
            QFile release(QStringLiteral("/proc/sys/kernel/osrelease"));
            if (release.open(QFile::ReadOnly | QFile::Text)) {
                underWsl = QString::fromLatin1(release.readAll())
                               .contains(QStringLiteral("microsoft"), Qt::CaseInsensitive);
            }
        }

        ParameterGrp::handle hGen = App::GetApplication().GetParameterGroupByPath(
            "User parameter:BaseApp/Preferences/General");
        if (underWsl && hGen->GetBool("PreferXcbOnWsl", true)) {
            qputenv("QT_QPA_PLATFORM", "xcb");
            Base::Console().Log("Init: WSL detected, using the xcb platform "
                                "plugin (PreferXcbOnWsl)\n");
        }
    }
#endif

    // Automatic scaling for legacy apps (disable once all parts of GUI are aware of HiDpi)
    ParameterGrp::handle hDPI =
        App::GetApplication().GetParameterGroupByPath("User parameter:BaseApp/Preferences/HighDPI");
    bool disableDpiScaling = hDPI->GetBool("DisableDpiScaling", false);
    if (disableDpiScaling) {
#ifdef FC_OS_WIN32
        SetProcessDPIAware(); // call before the main event loop
#endif
#if QT_VERSION < QT_VERSION_CHECK(6,0,0)
        QApplication::setAttribute(Qt::AA_DisableHighDpiScaling);
#endif
    }
    else if (!getenv("QT_AUTO_SCREEN_SCALE_FACTOR")) {
        // Enable automatic scaling based on pixel density of display (added in Qt 5.6)
#if QT_VERSION < QT_VERSION_CHECK(6,0,0)
        QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
#endif
#if QT_VERSION >= QT_VERSION_CHECK(5,14,0) && defined(Q_OS_WIN)
        QGuiApplication::setHighDpiScaleFactorRoundingPolicy(Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);
#endif
    }

#if QT_VERSION < QT_VERSION_CHECK(6,0,0)
    //Enable support for highres images (added in Qt 5.1, but off by default)
    QCoreApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
#endif

    // Use software rendering for OpenGL
    ParameterGrp::handle hOpenGL =
        App::GetApplication().GetParameterGroupByPath("User parameter:BaseApp/Preferences/OpenGL");
    bool useSoftwareOpenGL = hOpenGL->GetBool("UseSoftwareOpenGL", false);
    if (useSoftwareOpenGL) {
        QApplication::setAttribute(Qt::AA_UseSoftwareOpenGL);
    }

    #if (QT_VERSION >= QT_VERSION_CHECK(5, 10, 0))
        // By default (on platforms that support it, see docs for
        // Qt::AA_CompressHighFrequencyEvents) QT applies compression
        // for high frequency events (mouse move, touch, window resizes)
        // to keep things smooth even when handling the event takes a
        // while (e.g. to calculate snapping).
        // However, tablet pen move events (and mouse move events
        // synthesised from those) are not compressed by default (to
        // allow maximum precision when e.g. hand-drawing curves),
        // leading to unacceptable slowdowns using a tablet pen. Enable
        // compression for tablet events here to solve that.
        QCoreApplication::setAttribute(Qt::AA_CompressTabletEvents);
    #endif
}

void postAppSetup()
{
    // http://forum.freecad.org/viewtopic.php?f=3&t=15540
    qApp->setAttribute(Qt::AA_DontShowIconsInMenus, false);

    // Make sure that we use '.' as decimal point. See also
    // http://bugs.debian.org/cgi-bin/bugreport.cgi?bug=559846
    // and issue #0002891
    // http://doc.qt.io/qt-5/qcoreapplication.html#locale-settings
    setlocale(LC_NUMERIC, "C");

    // set application icon and window title
    
    const auto & cfg = App::Application::Config();
    auto it = cfg.find("Application");
    if (it != cfg.end()) {
        qApp->setApplicationName(QString::fromUtf8(it->second.c_str()));
    }
    else {
        qApp->setApplicationName(QString::fromStdString(App::GetApplication().getExecutableName()));
    }
#ifndef Q_OS_MACX
    qApp->setWindowIcon(
        Gui::BitmapFactory().pixmap(App::Application::Config()["AppIcon"].c_str()));
#endif

    QString plugin;
    plugin = QString::fromStdString(App::GetApplication().getHomePath());
    plugin += QStringLiteral("/plugins");
    QCoreApplication::addLibraryPath(plugin);

    Render::RendererFactory::setResourcePath(App::Application::getResourceDir() + "Renderer");

    // setup the search paths for Qt style sheets
    QStringList qssPaths;
    qssPaths << QString::fromUtf8(
        (App::Application::getUserAppDataDir() + "Gui/Stylesheets/").c_str())
             << QString::fromUtf8((App::Application::getResourceDir() + "Gui/Stylesheets/").c_str())
             << QStringLiteral(":/stylesheets");
    QDir::setSearchPaths(QStringLiteral("qss"), qssPaths);

    // setup the search paths for Qt overlay style sheets
    QStringList qssOverlayPaths;
    qssOverlayPaths << QString::fromUtf8((App::Application::getUserAppDataDir()
                        + "Gui/Stylesheets/overlay").c_str())
                    << QString::fromUtf8((App::Application::getResourceDir()
                        + "Gui/Stylesheets/overlay").c_str());
    QDir::setSearchPaths(QStringLiteral("overlay"), qssOverlayPaths);

    // setup the search paths for view menu style sheets
    QStringList qssMenuPaths;
    qssMenuPaths << QString::fromUtf8((App::Application::getUserAppDataDir()
                        + "Gui/Stylesheets/menu").c_str())
                 << QString::fromUtf8((App::Application::getResourceDir()
                        + "Gui/Stylesheets/menu").c_str());
    QDir::setSearchPaths(QStringLiteral("qssm"), qssMenuPaths);

    QStringList iconSetPaths;
    iconSetPaths << QString::fromUtf8((App::Application::getUserAppDataDir()
                        + "Gui/IconSets").c_str());
    QDir::setSearchPaths(QStringLiteral("iconset"), iconSetPaths);

    // set search paths for images
    QStringList imagePaths;
    imagePaths << QString::fromUtf8((App::Application::getUserAppDataDir() + "Gui/images").c_str())
               << QString::fromUtf8((App::Application::getUserAppDataDir() + "pixmaps").c_str())
               << QStringLiteral(":/icons");
    QDir::setSearchPaths(QStringLiteral("images"), imagePaths);

    // register action style event type
    ActionStyleEvent::EventType = QEvent::registerEventType(QEvent::User + 1);

    ParameterGrp::handle hTheme = App::GetApplication().GetParameterGroupByPath(
        "User parameter:BaseApp/Preferences/Bitmaps/Theme");
#if !defined(Q_OS_LINUX)
    QIcon::setThemeSearchPaths(QIcon::themeSearchPaths() << QStringLiteral(":/icons/FreeCAD-default"));
    QIcon::setThemeName(QStringLiteral("FreeCAD-default"));
#else
    // Option to opt-out from using a Linux desktop icon theme.
    // https://forum.freecad.org/viewtopic.php?f=4&t=35624
    //
    // bool themePaths = hTheme->GetBool("ThemeSearchPaths",true);
    //
    // Disable system theme by default, as it rarely works with FreeCAD, because
    // there are so many icons can't be find in common themes.
    bool themePaths = hTheme->GetBool("_ThemeSearchPaths",false);
    if (!themePaths) {
        QStringList searchPaths;
        searchPaths.prepend(QString::fromUtf8(":/icons"));
        QIcon::setThemeSearchPaths(searchPaths);
        QIcon::setThemeName(QStringLiteral("FreeCAD-default"));
    }
#endif

    std::string searchpath = hTheme->GetASCII("SearchPath");
    if (!searchpath.empty()) {
        QStringList searchPaths = QIcon::themeSearchPaths();
        searchPaths.prepend(QString::fromUtf8(searchpath.c_str()));
        QIcon::setThemeSearchPaths(searchPaths);
    }

    std::string name = hTheme->GetASCII("Name");
    if (!name.empty()) {
        QIcon::setThemeName(QString::fromUtf8(name.c_str()));
    }

#if defined(FC_OS_LINUX)
    // See #0001588
    QString path = FileDialog::restoreLocation();
    FileDialog::setWorkingDirectory(QDir::currentPath());
    FileDialog::saveLocation(path);
#else
    FileDialog::setWorkingDirectory(FileDialog::restoreLocation());
#endif
}

void postMainWindowSetup(MainWindow &mw)
{
    // allow to disable version number
    ParameterGrp::handle hGen =
        App::GetApplication().GetParameterGroupByPath("User parameter:BaseApp/Preferences/General");
    bool showVersion = hGen->GetBool("ShowVersionInTitle", true);

    if (showVersion) {
        // set main window title with FreeCAD Version
        std::map<std::string, std::string>& config = App::Application::Config();
        QString major  = QString::fromUtf8(config["BuildVersionMajor"].c_str());
        QString minor  = QString::fromUtf8(config["BuildVersionMinor"].c_str());
        QString point = QString::fromUtf8(config["BuildVersionPoint"].c_str());
        QString suffix = QString::fromUtf8(config["BuildVersionSuffix"].c_str());
        QString title =
            QStringLiteral("%1 %2.%3.%4%5").arg(qApp->applicationName(), major, minor, point, suffix);
        mw.setWindowTitle(title);
    } else {
        mw.setWindowTitle(qApp->applicationName());
    }

    QObject::connect(qApp, SIGNAL(messageReceived(const QList<QByteArray> &)),
                     &mw, SLOT(processMessages(const QList<QByteArray> &)));

    ParameterGrp::handle hDocGrp = WindowParameter::getDefaultParameter()->GetGroup("Document");
    int timeout = hDocGrp->GetInt("AutoSaveTimeout", 15); // 15 min
    if (!hDocGrp->GetBool("AutoSaveEnabled", true))
        timeout = 0;
    AutoSaver::instance()->setTimeout(timeout * 60000);
    AutoSaver::instance()->setCompressed(hDocGrp->GetBool("AutoSaveCompressed", true));

    // set toolbar icon size
    ParameterGrp::handle hGrp = WindowParameter::getDefaultParameter()->GetGroup("General");
    int size = ToolBarManager::getInstance()->toolBarIconSize();
    mw.setIconSize(QSize(size,size));

    // filter wheel events for combo boxes
    if (hGrp->GetBool("ComboBoxWheelEventFilter", false)) {
        WheelEventFilter* filter = new WheelEventFilter(qApp);
        qApp->installEventFilter(filter);
    }
    
    // For values different to 1 and 2 use the OS locale settings
    auto localeFormat = hGrp->GetInt("UseLocaleFormatting", 0);
    if (localeFormat == 1) {
        Translator::instance()->setLocale(
            hGrp->GetASCII("Language", Translator::instance()->activeLanguage().c_str()));
    }
    else if (localeFormat == 2) {
        Translator::instance()->setLocale("C");
    }

    // set text cursor blinking state
    int blinkTime = hGrp->GetBool("EnableCursorBlinking", true) ? -1 : 0;
    qApp->setCursorFlashTime(blinkTime);

    {
        QWindow window;
        window.setSurfaceType(QWindow::OpenGLSurface);
        window.create();

        QOpenGLContext context;
        if (context.create()) {
            context.makeCurrent(&window);
            if (!context.functions()->hasOpenGLFeature(QOpenGLFunctions::Framebuffers)) {
                Base::Console().Log("This system does not support framebuffer objects\n");
            }
            if (!context.functions()->hasOpenGLFeature(QOpenGLFunctions::NPOTTextures)) {
                Base::Console().Log("This system does not support NPOT textures\n");
            }

            int major = context.format().majorVersion();
            int minor = context.format().minorVersion();

#ifdef NDEBUG
            // In release mode, issue a warning to users that their version of OpenGL is
            // potentially going to cause problems
            if (major < 2) {
                auto message =
                    QObject::tr("This system is running OpenGL %1.%2. "
                                "FreeCAD requires OpenGL 2.0 or above. "
                                "Please upgrade your graphics driver and/or card as required.")
                        .arg(major)
                        .arg(minor)
                    + QStringLiteral("\n");
                Base::Console().Warning(message.toStdString().c_str());
                Dialog::DlgCheckableMessageBox::showMessage(
                    Gui::GUISingleApplication::applicationName() + QStringLiteral(" - ")
                        + QObject::tr("Invalid OpenGL Version"),
                    message);
            }
#endif
            const char* glVersion = reinterpret_cast<const char*>(glGetString(GL_VERSION));
            Base::Console().Log("OpenGL version is: %d.%d (%s)\n", major, minor, glVersion);

            // The version string cannot answer "did this session get the GPU?".
            // A Mesa software context and a Mesa hardware context both report
            // "Mesa <x.y>", so a timing taken on the rasterizer is indistinguishable
            // in the log from one taken on the GPU. The renderer string is what
            // separates them, so record it and say plainly which one this is.
            const char* glRenderer = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
            const char* glVendor = reinterpret_cast<const char*>(glGetString(GL_VENDOR));
            Base::Console().Log("OpenGL renderer is: %s\n", glRenderer ? glRenderer : "<unknown>");
            Base::Console().Log("OpenGL vendor is: %s\n", glVendor ? glVendor : "<unknown>");

            // Software renderers by platform, matched case-insensitively:
            //   Mesa (Linux, and Windows/macOS builds of Mesa) - llvmpipe,
            //     softpipe, swrast
            //   ANGLE / Chromium-derived stacks - SwiftShader
            //   Windows - "GDI Generic" (the OpenGL 1.1 fallback when no ICD is
            //     installed) and "Microsoft Basic Render Driver" (WARP, which
            //     ANGLE reports inside its own renderer string)
            //   macOS - "Apple Software Renderer", covered by "software renderer"
            const QString renderer = QString::fromLatin1(glRenderer ? glRenderer : "");
            static const char* const softwareMarkers[] = {"llvmpipe",
                                                          "softpipe",
                                                          "swrast",
                                                          "swiftshader",
                                                          "basic render",
                                                          "gdi generic",
                                                          "software renderer",
                                                          "software rasterizer"};
            bool software = false;
            for (const char* marker : softwareMarkers) {
                if (renderer.contains(QLatin1String(marker), Qt::CaseInsensitive)) {
                    software = true;
                    break;
                }
            }
            Base::Console().Log("OpenGL acceleration: %s\n",
                                software ? "SOFTWARE rasterizer (timings are not "
                                           "representative of GPU performance)"
                                         : "hardware");
        }
    }

    Application::initOpenInventor();

    QString home = QString::fromStdString(App::Application::getHomePath());

    const auto & cfg = App::Application::Config();
    auto it = cfg.find("WindowTitle");
    if (it != cfg.end()) {
        QString title = QString::fromUtf8(it->second.c_str());
        mw.setWindowTitle(title);
    }
    it = cfg.find("WindowIcon");
    if (it != cfg.end()) {
        QString path = QString::fromUtf8(it->second.c_str());
        if (QDir(path).isRelative()) {
            path = QFileInfo(QDir(home), path).absoluteFilePath();
        }
        QApplication::setWindowIcon(QIcon(path));
    }
    it = cfg.find("ProgramLogo");
    if (it != cfg.end()) {
        QString path = QString::fromUtf8(it->second.c_str());
        if (QDir(path).isRelative()) {
            path = QFileInfo(QDir(home), path).absoluteFilePath();
        }
        QPixmap px(path);
        if (!px.isNull()) {
            auto logo = new QLabel();
            logo->setPixmap(px.scaledToHeight(32));
            mw.statusBar()->addPermanentWidget(logo, 0);
            logo->setFrameShape(QFrame::NoFrame);
        }
    }
    bool hidden = false;
    it = cfg.find("StartHidden");
    if (it != cfg.end()) {
        hidden = true;
    }

    // show splasher while initializing the GUI
    if (!hidden)
        mw.startSplasher();

    // running the GUI init script
    try {
        Base::Console().Log("Run Gui init script\n");
        Application::runInitGuiScript();
        setImportImageFormats();
    }
    catch (const Base::Exception& e) {
        Base::Console().Error("Error in FreeCADGuiInit.py: %s\n", e.what());
        mw.stopSplasher();
        throw;
    }

    // Bring the render backend up while the splasher is still showing.
    //
    // Backend startup -- a GL context, its offscreen surface, the
    // device, and the shader programs -- is one-time and per process,
    // but it used to be paid by whoever created the first 3D view: the
    // first New Document of a session cost about a second where every
    // later one cost a fifth, and the plain GL path is flat at a tenth.
    // So none of it is document, Gui document or 3D view construction.
    // Measured with fcad-probes/newdoc_delay_probe.py.
    //
    // Here, rather than after the window is up: this is the stage that
    // exists for one-time cost, and a second of it under a splash
    // screen is a second nobody is waiting through. Only when a backend
    // is configured -- render cache 3 with a real type -- so a session
    // that will never use one pays nothing.
    if (ViewParams::getRenderCache() == 3) {
        const std::string rtype = RenderParams::getType();
        // The widget MainWindow keeps to settle the window's surface
        // type is exactly what this needs: a QOpenGLWidget whose format
        // the backend's own context can be built from.
        auto glw = mw.findChild<QOpenGLWidget*>(
                QStringLiteral("GLSurfaceWarmup"));
        // Seeded before the backend comes up, because the view id budget
        // is a startup option: this warm-up IS the startup for a session
        // that has one, so a value pushed later would never be read.
        Render::RendererFactory::setMaxViewIds(
                int(RenderParams::getMaxViewIds()));
        Render::RendererLib::WarmupTiming t;
        if (glw && Render::RendererFactory::warmup(rtype, glw, &t)) {
            Base::Console().Log(
                "Init: render backend '%s' warmed up in %.0f ms"
                " (context %.0f, device %.0f, programs %.0f, flush %.0f)\n",
                rtype.c_str(), t.total, t.context, t.device,
                t.programs, t.flush);
        }
    }

    // stop splash screen and set immediately the active window that may be of interest
    // for scripts using Python binding for Qt
    mw.stopSplasher();
    qApp->setActiveWindow(&mw);

    // Activate the correct workbench
    std::string start = App::Application::Config()["StartWorkbench"];
    Base::Console().Log("Init: Activating default workbench %s\n", start.c_str());
    std::string autoload =
        App::GetApplication()
            .GetParameterGroupByPath("User parameter:BaseApp/Preferences/General")
            ->GetASCII("AutoloadModule", start.c_str());
    if ("$LastModule" == autoload) {
        start = App::GetApplication()
                    .GetParameterGroupByPath("User parameter:BaseApp/Preferences/General")
                    ->GetASCII("LastModule", start.c_str());
    }
    else {
        start = autoload;
    }
    // if the auto workbench is not visible then force to use the default workbech
    // and replace the wrong entry in the parameters
    QStringList wb = Application::Instance->workbenches();
    if (!wb.contains(QString::fromUtf8(start.c_str()))) {
        start = App::Application::Config()["StartWorkbench"];
        if ("$LastModule" == autoload) {
            App::GetApplication()
                .GetParameterGroupByPath("User parameter:BaseApp/Preferences/General")
                ->SetASCII("LastModule", start.c_str());
        }
        else {
            App::GetApplication()
                .GetParameterGroupByPath("User parameter:BaseApp/Preferences/General")
                ->SetASCII("AutoloadModule", start.c_str());
        }
    }

    hGrp = App::GetApplication().GetParameterGroupByPath(
        "User parameter:BaseApp/Preferences/MainWindow");

    // The "Auto" theme follows the desktop, so re-apply the matching preference
    // pack whenever the system scheme changed since the last run. This has to
    // happen before the stylesheet is read below, because the pack sets it.
    Application::resolveAutoTheme();

#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    // ...and while running, so switching the desktop to dark in the evening is
    // not something FreeCAD needs restarting to notice.
    QObject::connect(qGuiApp->styleHints(),
                     &QStyleHints::colorSchemeChanged,
                     qGuiApp,
                     [](Qt::ColorScheme) {
                         Application::resolveAutoTheme();
                     });
#endif

    // Pin the palette before any of the widgets below exist. Qt hands a widget
    // the palette that is in effect when it is polished and keeps giving it
    // back, so anything built while the desktop scheme still applies stays that
    // colour for the rest of the session however often the application palette
    // changes afterwards. Pinning after activateWorkbench() left the workbench
    // tab bar drawing its tabs dark under a light theme.
    Application::applyColorScheme();

    // Call this before showing the main window because otherwise:
    // 1. it shows a white window for a few seconds which doesn't look nice
    // 2. the layout of the toolbars is completely broken
    Application::Instance->activateWorkbench(start.c_str());

    // show the main window
    if (!hidden) {
        Base::Console().Log("Init: Showing main window\n");
        mw.loadWindowSettings();
    }

    // The Qt widget style the theme asks for. Applied before the
    // stylesheet, which paints over whichever style it was written for;
    // an empty or unknown name leaves the platform style alone.
    {
        static ParamHandlers qtStyleHandlers;
        auto applyQtStyle = [](ParameterGrp::handle grp) {
            Application::Instance->setStyle(
                QString::fromUtf8(grp->GetASCII("QtStyle").c_str()));
        };
        qtStyleHandlers.addDelayedHandler("BaseApp/Preferences/MainWindow",
                                          "QtStyle", applyQtStyle);
        applyQtStyle(hGrp);
    }

    std::string style = hGrp->GetASCII("StyleSheet");
    if (style.empty()) {
        // check the branding settings
        const auto& config = App::Application::Config();
        auto it = config.find("StyleSheet");
        if (it != config.end())
            style = it->second;
    }

    Application::Instance->setStyleSheet(QString::fromUtf8(style.c_str()),
            hGrp->GetBool("TiledBackground", false));

#if QT_VERSION >= 0x050600 && QT_VERSION < 0x060000 && defined(Q_OS_WIN32)
    // Fix menu not shown when in full screen on windows.
    // See https://doc.qt.io/qt-5/windows-issues.html#fullscreen-opengl-based-windows
    QWindowsWindowFunctions::setHasBorderInFullScreen(getMainWindow()->windowHandle(),true);
#endif
    // Qt6 has no public replacement: QWindowsWindowFunctions is gone with the
    // QtPlatformHeaders module, and the equivalent is
    // QNativeInterface::Private::QWindowsWindow::setHasBorderInFullScreen(),
    // reachable only through a private QPA header and Qt6::GuiPrivate. That is
    // an ABI-unstable dependency to take on for a cosmetic fix, so the
    // workaround is left to Qt5. Worth re-checking whether Qt6 still shows the
    // bug (menu hidden in fullscreen on an OpenGL window) before deciding.

    //initialize spaceball.
    if (auto app = qobject_cast<GUIApplicationNativeEventAware*>(qApp)) {
        app->initSpaceball(&mw);
    }

#ifdef FC_DEBUG // redirect Coin messages to FreeCAD
    SoDebugError::setHandlerCallback( messageHandlerCoin, 0 );
#endif

    // Now run the background autoload, for workbenches that should be loaded at startup, but not
    // displayed to the user immediately
    std::string autoloadCSV =
        App::GetApplication()
            .GetParameterGroupByPath("User parameter:BaseApp/Preferences/General")
            ->GetASCII("BackgroundAutoloadModules", "");

    // Tokenize the comma-separated list and load the requested workbenches if they exist in this
    // installation
    std::vector<std::string> backgroundAutoloadedModules;
    std::stringstream stream(autoloadCSV);
    std::string workbench;
    while (std::getline(stream, workbench, ','))
        if (wb.contains(QString::fromUtf8(workbench.c_str())))
            Application::Instance->initializeWorkbench(workbench.c_str());

    _ApplicationStartUp = false;

    // Belt to the braces above: anything that still got built before the colour
    // scheme was settled -- an autoloaded workbench, a plugin, a dialog created
    // during init -- is holding the palette that was in effect at the time.
    // setStyleSheet() does this after every theme change but skips it while
    // starting up, so nothing had ever done it for the widgets init leaves
    // behind.
    Application::refreshInheritedPalettes();

    // gets called once we start the event loop
    QTimer::singleShot(0, &mw, SLOT(delayedStartup()));

    // run the Application event loop
    Base::Console().Log("Init: Entering event loop\n");

    // boot phase reference point
    // https://forum.freecad.org/viewtopic.php?f=10&t=21665
    Gui::getMainWindow()->setProperty("eventLoop", true);
}

} // namespace Gui

namespace {
enum RestartMode {
    RestartNone = 0,
    RestartNormal = 1,
    RestartReset = 2,
};
RestartMode _RestartMode = RestartNone;
QString _AppPath;
QStringList _AppArgs;
std::string _AppConf;
}

bool Application::isRestarting()
{
    return _RestartMode != RestartNone;
}

void Application::restart(bool reset)
{
    if (_RestartMode != RestartNone)
        return;
    _AppPath = QApplication::applicationFilePath();
    if (_AppPath.isEmpty())
        return;
    _AppArgs = QApplication::arguments().mid(1);
    _AppConf = App::GetApplication().Config()["UserParameter"];
    if (getMainWindow()->close())
        _RestartMode = reset ? RestartReset : RestartNormal;
}

bool Application::checkRestart() {
    if (Instance || _AppPath.isEmpty())
        return false;
    if (_RestartMode == RestartReset)
        Base::FileInfo(_AppConf).deleteFile();
    QProcess::startDetached(_AppPath, _AppArgs);
    _AppPath.clear();
    return true;
}

namespace {

/*!
 * Corrections to the platform style that belong to the style rather than to a
 * theme, so that they hold for the themes and for Classic alike -- Classic
 * ships no style sheet, and a widget style sheet would take the menu bar away
 * from whatever theme is loaded. A theme still layers on top of this: setting
 * an application style sheet wraps the application style in a
 * QStyleSheetStyle, which delegates anything the sheet does not decide back
 * here.
 */
class ApplicationStyle: public QProxyStyle
{
public:
    explicit ApplicationStyle(QStyle* base)
        : QProxyStyle(base)
    {}

    QSize sizeFromContents(ContentsType type,
                           const QStyleOption* option,
                           const QSize& contentsSize,
                           const QWidget* widget) const override
    {
        // Qt 6's windows11 style spends 17px either side of a menu bar label
        // and stands the item 32px tall, which is what makes the menu bar read
        // as mostly empty space. Nothing between the label and the metric can
        // be reached from a style sheet: PM_MenuBarItemSpacing, HMargin and
        // VMargin are all 0, and the padding is inside the item's own size.
        if (type == CT_MenuBarItem && !contentsSize.isEmpty()) {
            return {contentsSize.width() + 2 * menuBarItemHPadding,
                    contentsSize.height() + 2 * menuBarItemVPadding};
        }
        return QProxyStyle::sizeFromContents(type, option, contentsSize, widget);
    }

    void drawControl(ControlElement element,
                     const QStyleOption* option,
                     QPainter* painter,
                     const QWidget* widget) const override
    {
        // The windows11 style does mark the item under the pointer, but with a
        // near-white rounded fill that is invisible against a menu bar which is
        // already off-white -- so Classic looks like it has no hover at all
        // while a theme, which names its own accent, looks fine.
        //
        // Borrow the shading rather than name a colour, so it follows the style
        // and the palette. The one to borrow is a *split* tool button's: a tool
        // button with a menu shades light blue on hover, a plain one only gets
        // a pale outline. Both switches matter -- the style wants
        // QStyleOptionToolButton::Menu and State_AutoRaise together, and drops
        // to the pale grey if either is missing.
        const auto* item = qstyleoption_cast<const QStyleOptionMenuItem*>(option);
        if (element == CE_MenuBarItem && item && (item->state & State_Selected)) {
            QStyleOptionToolButton panel;
            static_cast<QStyleOption&>(panel) = *item;
            panel.state |= State_MouseOver | State_AutoRaise | State_Raised;
            panel.features = QStyleOptionToolButton::Menu;
            panel.subControls = SC_ToolButton;
            panel.activeSubControls = SC_ToolButton;
            panel.toolButtonStyle = Qt::ToolButtonIconOnly;
            panel.arrowType = Qt::NoArrow;

            // Only the button half is drawn, and the style takes the arrow's
            // width off the right before drawing it. Hand it a rect that is
            // wider by exactly that, and the half that does get drawn lands on
            // the item, corners and all.
            panel.rect.adjust(0, 0,
                              proxy()->pixelMetric(PM_MenuButtonIndicator, &panel, widget), 0);

            painter->save();
            painter->setClipRect(item->rect);
            proxy()->drawComplexControl(CC_ToolButton, &panel, painter, widget);
            painter->restore();

            // Let the style draw the label as if nothing were selected, so the
            // text is laid out exactly as it is the rest of the time. Drawing
            // it here instead moved it: a menu title must not resize under the
            // pointer.
            QStyleOptionMenuItem label(*item);
            label.state &= ~(State_Selected | State_MouseOver | State_Sunken);
            QProxyStyle::drawControl(element, &label, painter, widget);
            return;
        }
        QProxyStyle::drawControl(element, option, painter, widget);
    }

private:
    static constexpr int menuBarItemHPadding = 12;
    static constexpr int menuBarItemVPadding = 4;
};

} // anonymous namespace

void Application::runApplication(void)
{
    preAppSetup();

    // A new QApplication
    Base::Console().Log("Init: Creating Gui::Application and QApplication\n");

    // if application not yet created by the splasher
    int argc = App::Application::GetARGC();
    GUISingleApplication mainApp(argc, App::Application::GetARGV());

    // check if a single or multiple instances can run
    const auto & cfg = App::Application::Config();
    auto it = cfg.find("SingleInstance");
    if (it != cfg.end() && mainApp.isRunning()) {
        // send the file names to be opened to the server application so that this
        // opens them
        QDir cwd = QDir::current();
        std::list<std::string> files = App::Application::getCmdLineFiles();
        for (std::list<std::string>::iterator jt = files.begin(); jt != files.end(); ++jt) {
            QString fn = QString::fromUtf8(jt->c_str(), static_cast<int>(jt->size()));
            QFileInfo fi(fn);
            // if path name is relative make it absolute because the running instance
            // cannot determine the full path when trying to load the file
            if (fi.isRelative()) {
                fn = cwd.absoluteFilePath(fn);
                fn = QDir::cleanPath(fn);
            }

            QByteArray msg = fn.toUtf8();
            msg.prepend("OpenFile:");
            if (!mainApp.sendMessage(msg)) {
                qWarning("Failed to send message to server");
                break;
            }
        }
        return;
    }

    postAppSetup();

    // Before any widget exists, so nothing has to be re-polished afterwards.
    QApplication::setStyle(new ApplicationStyle(QApplication::style()));

    Application app(true);
    MainWindow mw;
    mw.setProperty("QuitOnClosed", true);

    postMainWindowSetup(mw);

    //initialize spaceball.
    mainApp.initSpaceball(&mw);

    // run the Application event loop
    Base::Console().Log("Init: Entering event loop\n");

    try {
        std::stringstream s;
        s << App::Application::getUserCachePath() << App::Application::getExecutableName()
          << "_" << QCoreApplication::applicationPid() << ".lock";
        // open a lock file with the PID
        Base::FileInfo fi(s.str());
        Base::ofstream lock(fi);

        // In case the file_lock cannot be created start FreeCAD without IPC support.
#if !defined(FC_OS_WIN32) || (BOOST_VERSION < 107600)
        std::string filename = s.str();
#else
        std::wstring filename = fi.toStdWString();
#endif
        std::unique_ptr<boost::interprocess::file_lock> flock;
        try {
            flock = std::make_unique<boost::interprocess::file_lock>(filename.c_str());
            flock->lock();
        }
        catch (const boost::interprocess::interprocess_exception& e) {
            QString msg = QString::fromLocal8Bit(e.what());
            Base::Console().Warning("Failed to create a file lock for the IPC: %s\n",
                                    msg.toUtf8().constData());
        }

        Base::Console().Log("Init: Executing event loop...\n");
        mainApp.exec();

        // Qt can't handle exceptions thrown from event handlers, so we need
        // to manually rethrow SystemExitExceptions.
        if (mainApp.caughtException.get())
            THROWM(Base::SystemExitException, *mainApp.caughtException.get())

        // close the lock file, in case of a crash we can see the existing lock file
        // on the next restart and try to repair the documents, if needed.
        if (flock.get())
            flock->unlock();
        lock.close();
        fi.deleteFile();
    }
    catch (const Base::SystemExitException&) {
        Base::Console().Message("System exit\n");
        throw;
    }
    catch (const std::exception& e) {
        // catching nasty stuff coming out of the event loop
        Base::Console().Error("Event loop left through unhandled exception: %s\n", e.what());
        App::Application::destructObserver();
        throw;
    }
    catch (...) {
        // catching nasty stuff coming out of the event loop
        Base::Console().Error("Event loop left through unknown unhandled exception\n");
        App::Application::destructObserver();
        throw;
    }

    Base::Console().Log("Finish: Event loop left\n");
}

bool Application::testStatus(Status pos) const
{
    return d->StatusBits.test((size_t)pos);
}

void Application::setStatus(Status pos, bool on)
{
    d->StatusBits.set((size_t)pos, on);
}

bool Application::systemPrefersDarkScheme()
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    if (!qGuiApp) {
        return false;
    }
    // colorScheme() reports the scheme in effect, which is our own pin whenever
    // MainWindow/ColorScheme names one -- asking while pinned just reads the pin
    // back. Drop it long enough to see what the desktop says (unsetColorScheme()
    // updates the value synchronously, so nothing repaints in between), then let
    // applyColorScheme() restore whatever the parameter asks for.
    auto* styleHints = qGuiApp->styleHints();
    styleHints->unsetColorScheme();
    const bool dark = styleHints->colorScheme() == Qt::ColorScheme::Dark;
    applyColorScheme();
    return dark;
#elif QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    // 6.5 reports the system scheme but cannot override it, so it is never pinned.
    return qGuiApp && qGuiApp->styleHints()->colorScheme() == Qt::ColorScheme::Dark;
#else
    // Before 6.5 Qt does not report the system scheme at all, and its styles
    // never followed it, so a light desktop is the only thing we can assume.
    return false;
#endif
}

bool Application::isDarkTheme()
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    // The scheme in effect: the theme's own pin, or the desktop's answer
    // when the theme follows it. Never unset here -- unlike
    // systemPrefersDarkScheme(), the pin is exactly what is being asked.
    if (qGuiApp) {
        return qGuiApp->styleHints()->colorScheme() == Qt::ColorScheme::Dark;
    }
#endif
    // No scheme API (or no GUI yet): fall back to the filename sniff this
    // helper exists to replace.
    const std::string sheet = App::GetApplication()
                                  .GetParameterGroupByPath(
                                      "User parameter:BaseApp/Preferences/MainWindow")
                                  ->GetASCII("StyleSheet");
    std::string lower = sheet;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) {
        return std::tolower(c);
    });
    return lower.find("dark") != std::string::npos;
}

void Application::resolveAutoTheme()
{
    // Pinning the palette emits colorSchemeChanged, and reading the desktop
    // scheme unpins and repins it, so this is called back into while it runs.
    static bool resolving = false;
    if (resolving) {
        return;
    }

    auto hGrp = App::GetApplication().GetParameterGroupByPath(
        "User parameter:BaseApp/Preferences/MainWindow");
    if (!hGrp->GetBool("ThemeAuto", false)) {
        return;  // the user picked a theme outright; the desktop is not its business
    }

    Base::StateLocker lock(resolving);

    const char* wanted = Application::systemPrefersDarkScheme() ? "Dark" : "Light";
    if (hGrp->GetASCII("ThemeAutoApplied") == wanted) {
        return;
    }

    Application::Instance->prefPackManager()->apply(wanted);
    // The pack itself has no notion of Auto; restore the marker it just
    // overwrote so this keeps following the desktop.
    hGrp->SetBool("ThemeAuto", true);
    hGrp->SetASCII("ThemeAutoApplied", wanted);
}

void Application::applyColorScheme()
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    if (!qGuiApp) {
        return;
    }
    auto hGrp = App::GetApplication().GetParameterGroupByPath(
        "User parameter:BaseApp/Preferences/MainWindow");

    // "Match Desktop" means the desktop decides, so nothing is pinned: an
    // override does not merely fix the palette, it silences
    // colorSchemeChanged, and that signal is what tells us the desktop moved.
    // Pinning here would be following the desktop only until it changed.
    if (hGrp->GetBool("ThemeAuto", false)) {
        qGuiApp->styleHints()->unsetColorScheme();
        return;
    }

    // Absent, not empty, is the default: a configuration that has never had a
    // theme applied gets Classic's palette, which is what a fresh install is
    // supposed to look like. Qt's windows11 style follows the desktop when
    // nothing is pinned, so leaving this unset came up black on a dark Windows.
    // An explicit empty value still means "follow the desktop" -- that is what
    // the Match desktop entry writes.
    const std::string scheme = hGrp->GetASCII("ColorScheme", "Light");

    if (scheme == "Light") {
        qGuiApp->styleHints()->setColorScheme(Qt::ColorScheme::Light);
    }
    else if (scheme == "Dark") {
        qGuiApp->styleHints()->setColorScheme(Qt::ColorScheme::Dark);
    }
    else {
        qGuiApp->styleHints()->unsetColorScheme();
    }
#endif
}

void Application::setStyleSheet(const QString& qssFile, bool tiledBackground)
{
    Gui::MainWindow* mw = getMainWindow();
    auto mdi = mw->findChild<QMdiArea*>();
    mdi->setProperty("showImage", tiledBackground);

    // Qt's style sheet doesn't support it to define the link color of a QLabel
    // or in the property editor when an expression is set because therefore the
    // link color of the application's palette is used.
    // A workaround is to set a user-defined property to e.g. a QLabel and then
    // define it in the .qss file.
    //
    // Example:
    // QLabel label;
    // label.setProperty("haslink", QByteArray("true"));
    // label.show();
    // QColor link = label.palette().color(QPalette::Text);
    //
    // The .qss file must define it with:
    // QLabel[haslink="true"] {
    //     color: #rrggbb;
    // }
    //
    // See https://stackoverflow.com/questions/5497799/how-do-i-customise-the-appearance-of-links-in-qlabels-using-style-sheets
    // and https://forum.freecad.org/viewtopic.php?f=34&t=50744
    static bool init = true;
    if (init) {
        init = false;
        mw->setProperty("fc_originalLinkCoor", qApp->palette().color(QPalette::Link));
    }
    else {
        QPalette newPal(qApp->palette());
        newPal.setColor(QPalette::Link, mw->property("fc_originalLinkCoor").value<QColor>());
        qApp->setPalette(newPal);
    }

    mw->setProperty("fc_currentStyleSheet", qssFile);

    // The theme may have changed along with the stylesheet; follow it
    // before any substitution below, and drop values resolved under the
    // previous theme.
    if (d->themeParametersSource) {
        d->themeParametersSource->changeFilePath(styleParametersFilePath());
        d->styleParameterManager->reload();
    }

    auto hGrp = App::GetApplication().GetParameterGroupByPath("User parameter:BaseApp/Preferences/MainWindow");
    QString iconSet = QString::fromUtf8(hGrp->GetASCII("IconSet").c_str());
    if (!iconSet.isEmpty())
        getMainWindow()->setOverrideExtraIcons(iconSet);

    // Styles every theme shares (defaults.qss): our own widgets' bits
    // that should not depend on which sheet is active, preincluded
    // ahead of the theme sheet exactly as upstream does -- and served
    // even with no sheet at all, which is how the Classic theme gets
    // them.
    const QString defaultStyleSheet = [this]() {
        QFile f(QStringLiteral("qss:defaults.qss"));
        if (!f.open(QFile::ReadOnly)) {
            return QString();
        }
        QTextStream in(&f);
        return replaceVariablesInQss(in.readAll());
    }();

    if (!qssFile.isEmpty()) {
        // Search for stylesheet in user-defined search paths.
        // For qss they are set-up in runApplication() with the prefix "qss"
        QString prefix(QStringLiteral("qss:"));

        QFile f;
        if (QFile::exists(qssFile)) {
            f.setFileName(qssFile);
        }
        else if (QFile::exists(prefix + qssFile)) {
            f.setFileName(prefix + qssFile);
        }

        if (!f.fileName().isEmpty() && f.open(QFile::ReadOnly | QFile::Text)) {
            mdi->setBackground(QBrush(Qt::NoBrush));
            QTextStream str(&f);

            QString styleSheetContent = replaceVariablesInQss(str.readAll());

            qApp->setStyleSheet(defaultStyleSheet + QStringLiteral("\n")
                                + styleSheetContent);

            ActionStyleEvent e(ActionStyleEvent::Clear);
            qApp->sendEvent(mw, &e);

            // This is a way to retrieve the link color of a .qss file when it's defined there.
            // The color will then be set to the application's palette.
            // Limitation: it doesn't work if the .qss file on purpose sets the same color as
            // for normal text. In this case the default link color is used.
            {
                QLabel l1, l2;
                l2.setProperty("haslink", QByteArray("true"));

                l1.show();
                l2.show();
                QColor text = l1.palette().color(QPalette::Text);
                QColor link = l2.palette().color(QPalette::Text);

                if (text != link) {
                    QPalette newPal(qApp->palette());
                    newPal.setColor(QPalette::Link, link);
                    qApp->setPalette(newPal);
                }
            }
        }
    }
    else {
        if (tiledBackground) {
            qApp->setStyleSheet(defaultStyleSheet);
            ActionStyleEvent e(ActionStyleEvent::Restore);
            qApp->sendEvent(getMainWindow(), &e);
            mdi->setBackground(QPixmap(QStringLiteral("images:background.png")));
        }
        else {
            qApp->setStyleSheet(defaultStyleSheet);
            ActionStyleEvent e(ActionStyleEvent::Restore);
            qApp->sendEvent(getMainWindow(), &e);
            mdi->setBackground(QBrush(QColor(160,160,160)));
        }
    }

    // At startup time unpolish() mustn't be executed because otherwise the QSint widget
    // appear incorrect due to an outdated cache.
    // See https://doc.qt.io/qt-5/qstyle.html#unpolish-1
    // See https://forum.freecad.org/viewtopic.php?f=17&t=50783
    if (!d->startingUp) {
        if (mdi->style())
            mdi->style()->unpolish(qApp);

        refreshInheritedPalettes();
    }
}

void Application::refreshInheritedPalettes()
{
    // Leaving a themed stylesheet does not return widgets to the current
    // palette: Qt restores each one to the palette it held when the stylesheet
    // polished it, which came from the *previous* color scheme. Switching Dark
    // -> Classic therefore left docked panels and combo boxes painted dark on a
    // light UI, with no stylesheet in play to explain it.
    //
    // Re-assigning a default palette makes a widget resolve against the
    // application palette again. Only widgets that never set a palette of their
    // own are touched -- a deliberate one (an invalid-input SpinBox, a tooltip,
    // a notification) carries a non-zero resolve mask and must survive.
    for (QWidget* widget : qApp->allWidgets()) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        const auto resolved = widget->palette().resolveMask();
#else
        const auto resolved = widget->palette().resolve();
#endif
        if (resolved == 0) {
            widget->setPalette(QPalette());
        }
    }
}

QString Application::replaceVariablesInQss(QString qssText)
{
    // The ulong carries an alpha channel, so eight hex digits where a
    // stylesheet wants six.
    auto asColor = [](unsigned long packed) {
        return QStringLiteral("#%1").arg(packed, 8, 16, QLatin1Char('0')).toUpper().mid(0, 7);
    };

    ParameterGrp::handle hGrp =
        App::GetApplication().GetParameterGroupByPath("User parameter:BaseApp/Preferences/Themes");

    // The three accent colors predate the Variables group and shipped
    // stylesheets name them, so they keep the place they have always had.
    //
    // Defaulted to FreeCAD's blue, not to zero: no theme pack declares these,
    // and every sheet reaches for @ThemeAccentColor1 to paint a selection --
    // a checked tool button, a highlighted row, the current theme's button in
    // the Start wizard. A zero default painted all of them black on any
    // configuration that had not been through the Start wizard, which is the
    // only place that ever wrote the keys.
    // Each slot has its own default, because the sheets give the three of them
    // three different jobs -- see DefaultAccentColor1..3. One shared default
    // collapsed focus onto hover and flattened every accent gradient.
    std::vector<std::pair<std::string, QString>> variables;
    for (const auto& [name, fallback] :
         {std::pair {"ThemeAccentColor1", DefaultAccentColor1},
          std::pair {"ThemeAccentColor2", DefaultAccentColor2},
          std::pair {"ThemeAccentColor3", DefaultAccentColor3}}) {
        variables.emplace_back(name, asColor(hGrp->GetUnsigned(name, fallback)));
    }

    // Everything in Themes/Variables substitutes for @<name>, typed by how it
    // is stored. This is what lets a theme ship one parameterised stylesheet
    // and a group of values: recoloring it is then a preference edit rather
    // than an edit of the .qss.
    if (hGrp->HasGroup("Variables")) {
        auto hVars = hGrp->GetGroup("Variables");
        for (const auto& entry : hVars->GetUnsignedMap()) {
            variables.emplace_back(entry.first, asColor(entry.second));
        }
        for (const auto& entry : hVars->GetASCIIMap()) {
            variables.emplace_back(entry.first, QString::fromStdString(entry.second));
        }
        for (const auto& entry : hVars->GetIntMap()) {
            variables.emplace_back(entry.first, QString::number(entry.second));
        }
        for (const auto& entry : hVars->GetFloatMap()) {
            variables.emplace_back(entry.first, QString::number(entry.second));
        }
    }

    // Two derived shades of accent 1, for states that must not paint the same
    // fill as a selection. Hover is the case that matters: a hovered row that
    // reaches for @ThemeAccentColor1 is indistinguishable from a selected one,
    // and the three accent parameters all default to the same color, so a
    // sheet cannot tell them apart by reaching for accent 2 instead.
    //
    // Each sheet picks the shade that moves away from its own background --
    // Light.qss the pale one, Dark.qss the deep one -- which is knowledge only
    // the sheet has. A theme that defines either name itself wins; these are
    // only filled in where it did not.
    const unsigned long accent = hGrp->GetUnsigned("ThemeAccentColor1", DefaultAccentColor1);
    auto blend = [accent](int towards, double ratio) {
        auto mix = [towards, ratio](unsigned long channel) {
            const double from = static_cast<double>(channel);
            return static_cast<int>(from + (towards - from) * ratio + 0.5);
        };
        // The parameter packs the color as 0xRRGGBBAA; alpha is dropped.
        return QStringLiteral("#%1%2%3")
            .arg(mix((accent >> 24) & 0xFF), 2, 16, QLatin1Char('0'))
            .arg(mix((accent >> 16) & 0xFF), 2, 16, QLatin1Char('0'))
            .arg(mix((accent >> 8) & 0xFF), 2, 16, QLatin1Char('0'))
            .toUpper();
    };
    auto defined = [&variables](const char* name) {
        return std::any_of(variables.begin(), variables.end(), [name](const auto& variable) {
            return variable.first == name;
        });
    };
    if (!defined("ThemeAccentColorLight")) {
        variables.emplace_back("ThemeAccentColorLight", blend(0xFF, 0.45));
    }
    if (!defined("ThemeAccentColorDark")) {
        variables.emplace_back("ThemeAccentColorDark", blend(0x00, 0.35));
    }

    // Longest name first, or "@Accent" would eat the front of "@AccentDark".
    std::sort(variables.begin(), variables.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.first.size() > rhs.first.size();
    });

    for (const auto& variable : variables) {
        qssText.replace(QLatin1Char('@') + QString::fromStdString(variable.first),
                        variable.second);
    }

    // The legacy pass above serves the per-theme sheets and runs first, so
    // everything it defines is already text by now. What remains goes to
    // the style parameter evaluator: @Name looked up across the parameter
    // sources (theme YAML, user overrides, built-ins) and @{expression}
    // evaluated, which is the whole vocabulary of FreeCAD.qss. A name
    // neither pass knows is substituted empty, with a warning naming it.
    return QString::fromStdString(
        d->styleParameterManager->replacePlaceholders(qssText.toStdString()));
}

void Application::checkForDeprecatedSettings()
{
    // The Start wizard wrote all three accent slots with one shared color, and
    // it wrote them for everyone who ever passed through it -- so the per-slot
    // defaults (DefaultAccentColor1..3) can never reach an existing
    // configuration, and focus keeps painting exactly what hover paints.
    //
    // Three identical accents is that wizard's signature, not a choice: the
    // Theme page offers the slots separately and nothing else sets them
    // together. Where the stored trio still matches the old shared value, give
    // slots 2 and 3 the shades the sheets expect, following the scheme the
    // preference packs record so a dark theme lifts rather than deepens.
    ParameterGrp::handle hThemes =
        App::GetApplication().GetParameterGroupByPath("User parameter:BaseApp/Preferences/Themes");
    const bool untouchedTrio =
        hThemes->GetUnsigned("ThemeAccentColor1", 0) == DefaultAccentColor1
        && hThemes->GetUnsigned("ThemeAccentColor2", 0) == DefaultAccentColor1
        && hThemes->GetUnsigned("ThemeAccentColor3", 0) == DefaultAccentColor1;
    if (untouchedTrio) {
        const bool dark = App::GetApplication()
                              .GetParameterGroupByPath("User parameter:BaseApp/Preferences/"
                                                       "MainWindow")
                              ->GetASCII("ColorScheme")
            == "Dark";
        hThemes->SetUnsigned("ThemeAccentColor2",
                             dark ? DefaultDarkAccentColor2 : DefaultAccentColor2);
        hThemes->SetUnsigned("ThemeAccentColor3",
                             dark ? DefaultDarkAccentColor3 : DefaultAccentColor3);
    }

    // From 0.21, `FCBak` will be the intended default backup format
    bool makeBackups = App::GetApplication()
                           .GetParameterGroupByPath("User parameter:BaseApp/Preferences/Document")
                           ->GetBool("CreateBackupFiles", true);
    if (makeBackups) {
        bool useFCBakExtension =
            App::GetApplication()
                .GetParameterGroupByPath("User parameter:BaseApp/Preferences/Document")
                ->GetBool("UseFCBakExtension", true);
        if (!useFCBakExtension) {
            // TODO: This should be translated
            Base::Console().Warning("The `.FCStd#` backup format is deprecated as of v0.21 and may "
                                    "be removed in future versions.\n"
                                    "To update, check the 'Preferences->General->Document->Use "
                                    "date and FCBak extension' option.\n");
        }
    }
}

void Application::checkForPreviousCrashes()
{
    try {
        Gui::Dialog::DocumentRecoveryFinder finder;
        if (!finder.checkForPreviousCrashes()) {

            // If the recovery dialog wasn't shown check the cache size periodically
            Gui::Dialog::ApplicationCache cache;
            cache.applyUserSettings();
            if (cache.periodicCheckOfSize()) {
                qint64 total = cache.size();
                cache.performAction(total);
            }
        }
    }
    catch (const boost::interprocess::interprocess_exception& e) {
        QString msg = QString::fromLocal8Bit(e.what());
        Base::Console().Warning("Failed check for previous crashes because of IPC error: %s\n",
                                msg.toUtf8().constData());
    }
}

App::Document *Application::reopen(App::Document *doc) {
    if(!doc)
        return nullptr;
    std::string name = doc->FileName.getValue();
    std::set<const Gui::Document*> untouchedDocs;
    for(auto &v : d->documents) {
        if(!v.second->isModified() && !v.second->getDocument()->isTouched())
            untouchedDocs.insert(v.second);
    }

    WaitCursor wc;
    wc.setIgnoreEvents(WaitCursor::NoEvents);

    if(doc->testStatus(App::Document::PartialDoc)
            || doc->testStatus(App::Document::PartialRestore))
    {
        App::GetApplication().openDocument(name.c_str());
    } else {
        std::vector<std::string> docs;
        for(auto d : doc->getDependentDocuments(true)) {
            if(d->testStatus(App::Document::PartialDoc)
                    || d->testStatus(App::Document::PartialRestore) )
                docs.emplace_back(d->FileName.getValue());
        }

        if(docs.empty()) {
            Document *gdoc = getDocument(doc);
            if(gdoc) {
                setActiveDocument(gdoc);
                if(!gdoc->setActiveView())
                    gdoc->setActiveView(nullptr,View3DInventor::getClassTypeId());
            }
            return doc;
        }

        for(auto &file : docs)
            App::GetApplication().openDocument(file.c_str(),false);
    }

    doc = nullptr;
    for(auto &v : d->documents) {
        if(name == v.first->FileName.getValue())
            doc = const_cast<App::Document*>(v.first);
        if(untouchedDocs.count(v.second)) {
            if(!v.second->isModified()) continue;
            bool reset = true;
            for(auto obj : v.second->getDocument()->getObjects()) {
                if(!obj->isTouched())
                    continue;
                std::vector<App::Property*> props;
                obj->getPropertyList(props);
                for(auto prop : props){
                    auto link = dynamic_cast<App::PropertyLinkBase*>(prop);
                    if(link && link->checkRestore()) {
                        reset = false;
                        break;
                    }
                }
                if(!reset)
                    break;
            }
            if(reset) {
                v.second->getDocument()->purgeTouched();
                v.second->setModified(false);
            }
        }
    }
    return doc;
}
