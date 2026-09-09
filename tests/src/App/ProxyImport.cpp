// SPDX-License-Identifier: LGPL-2.1-or-later
//
// PropertyPythonObject::Restore imports the module a saved Proxy names
// (<Python module="..." class="..."/>, or the legacy pickle header), and
// the name comes from the document.  docs/Sandbox.md sec 11 item 1: the
// native import follows Base::Type::importModule's rule -- a module
// already loaded, or one that resolves into a registered Mod root; any
// other name is refused before importing and the object is left without
// a Proxy.  Both containers take the same path: a document object's
// Proxy here, and a property whose container is not a document object,
// which is what a view provider's Proxy is to this code.

#include "gtest/gtest.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include <App/Application.h>
#include <App/Document.h>
#include <App/DocumentObject.h>
#include <Base/Parameter.h>
#include <App/PropertyPythonObject.h>
#include <Base/Interpreter.h>
#include <Base/PyObjectBase.h>
#include <Base/Reader.h>
#include <Base/Type.h>
#include <src/App/InitApplication.h>

namespace
{

bool inSysModules(const char* name)
{
    Base::PyGILStateLocker lock;
    PyObject* mods = PyImport_GetModuleDict();
    return mods && PyDict_GetItemString(mods, name) != nullptr;
}

/// Restore `prop` from the element a saved Proxy of `module`.`cls` writes.
void restoreProxy(App::Property& prop, const std::string& module, const std::string& cls)
{
    std::string doc = R"(<?xml version="1.0" encoding="UTF-8"?><document><Python module=")"
        + module + R"(" class=")" + cls + R"("/></document>)";
    std::istringstream stream(doc);
    Base::XMLReader reader("Document.xml", stream);
    prop.Restore(reader);
}

/// Restore `prop` from the legacy pickle form, whose header names the module.
void restorePickle(App::Property& prop, const std::string& module, const std::string& cls)
{
    std::string doc = R"(<?xml version="1.0" encoding="UTF-8"?><document><Python value="(i)"
        + module + "&#10;" + cls + R"(&#10;"/></document>)";
    std::istringstream stream(doc);
    Base::XMLReader reader("Document.xml", stream);
    prop.Restore(reader);
}

/// "module.Class" of the restored object, "None" without one.
std::string proxyOf(App::Property& prop)
{
    Base::PyGILStateLocker lock;
    Py::Object obj = static_cast<App::PropertyPythonObject&>(prop).getValue();
    if (obj.isNone()) {
        return "None";
    }
    Py::Object cls = obj.getAttr("__class__");
    return std::string(Py::String(cls.getAttr("__module__"))) + "."
        + std::string(Py::String(cls.getAttr("__name__")));
}

class ProxyImport: public ::testing::Test
{
protected:
    static void SetUpTestSuite()
    {
        tests::initApplication();
    }
    void SetUp() override
    {
        // the native path: a routed restore would allocate in the guest
        // (the preference is dropped again in TearDown, as the routing
        // suites do)
        param = App::GetApplication().GetParameterGroupByPath(
            "User parameter:BaseApp/Preferences/Expression/Sandbox");
        param->SetBool("Evaluate", false);
        // a Mod root of this test's own with a workbench directory holding
        // a module and a package, and a directory OUTSIDE any root with
        // another module; all on sys.path, as FreeCADInit puts every
        // Mod/<X> there
        namespace fs = std::filesystem;
        base = fs::path(std::tmpnam(nullptr)).string() + "-fcxproxyimport";
        modRoot = base + "/Mod";
        const std::string wb = modRoot + "/FcxWb";
        const std::string elsewhere = base + "/elsewhere";
        fs::create_directories(wb + "/fcxproxyimport_pkg");
        fs::create_directories(elsewhere);
        std::ofstream(wb + "/fcxproxyimport_ok.py") << "class Thing:\n    pass\n";
        std::ofstream(wb + "/fcxproxyimport_pkg/__init__.py") << "";
        std::ofstream(wb + "/fcxproxyimport_pkg/inner.py") << "class Thing:\n    pass\n";
        std::ofstream(elsewhere + "/fcxproxyimport_outside.py") << "class Thing:\n    pass\n";
        Base::Interpreter().addPythonPath(wb.c_str());
        Base::Interpreter().addPythonPath(elsewhere.c_str());
        Base::Type::addModuleRoot(modRoot);
        // a module that is loaded and lives nowhere on disk
        Base::Interpreter().runString("import sys, types\n"
                                      "_m = types.ModuleType('fcxproxyimport_loaded')\n"
                                      "class Thing:\n"
                                      "    pass\n"
                                      "Thing.__module__ = 'fcxproxyimport_loaded'\n"
                                      "_m.Thing = Thing\n"
                                      "sys.modules['fcxproxyimport_loaded'] = _m\n");
        doc = App::GetApplication().newDocument("FcxProxyImport");
        owner = doc->addObject("App::FeaturePython", "Owner");
        ASSERT_NE(owner, nullptr);
        ownerProxy = owner->getPropertyByName("Proxy");
        ASSERT_NE(ownerProxy, nullptr);
        // the other container: not a document object, as a view
        // provider is not
        bare.setContainer(doc);
    }
    void TearDown() override
    {
        if (param)
            param->RemoveBool("Evaluate");
        param = ParameterGrp::handle();
        App::GetApplication().closeDocument(doc->getName());
        std::error_code ec;
        std::filesystem::remove_all(base, ec);
    }
    std::string base;
    std::string modRoot;
    App::Document* doc = nullptr;
    App::DocumentObject* owner = nullptr;
    App::Property* ownerProxy = nullptr;
    App::PropertyPythonObject bare;
    ParameterGrp::handle param;
};

}  // namespace

TEST_F(ProxyImport, underAModRootRestoresOnBothContainers)
{
    ASSERT_FALSE(inSysModules("fcxproxyimport_ok"));
    restoreProxy(*ownerProxy, "fcxproxyimport_ok", "Thing");
    EXPECT_EQ(proxyOf(*ownerProxy), "fcxproxyimport_ok.Thing");
    EXPECT_TRUE(inSysModules("fcxproxyimport_ok"));
    restoreProxy(bare, "fcxproxyimport_ok", "Thing");
    EXPECT_EQ(proxyOf(bare), "fcxproxyimport_ok.Thing");
}

TEST_F(ProxyImport, dottedUnderAModRootRestores)
{
    ASSERT_FALSE(inSysModules("fcxproxyimport_pkg.inner"));
    restoreProxy(*ownerProxy, "fcxproxyimport_pkg.inner", "Thing");
    EXPECT_EQ(proxyOf(*ownerProxy), "fcxproxyimport_pkg.inner.Thing");
    EXPECT_TRUE(inSysModules("fcxproxyimport_pkg.inner"));
}

TEST_F(ProxyImport, outsideEveryRootIsRefusedOnBothContainers)
{
    ASSERT_FALSE(inSysModules("fcxproxyimport_outside"));
    restoreProxy(*ownerProxy, "fcxproxyimport_outside", "Thing");
    EXPECT_EQ(proxyOf(*ownerProxy), "None") << "a document object's Proxy";
    EXPECT_FALSE(inSysModules("fcxproxyimport_outside")) << "a refused module must not be imported";
    restoreProxy(bare, "fcxproxyimport_outside", "Thing");
    EXPECT_EQ(proxyOf(bare), "None") << "a property on another container";
    EXPECT_FALSE(inSysModules("fcxproxyimport_outside"));
}

TEST_F(ProxyImport, stdlibIsRefused)
{
    // any stdlib module the init script has not already imported will do
    const char* probe = nullptr;
    for (const char* name : {"wave", "colorsys", "mailbox", "csv", "netrc"}) {
        if (!inSysModules(name)) {
            probe = name;
            break;
        }
    }
    ASSERT_NE(probe, nullptr) << "every candidate stdlib module is already loaded";
    restoreProxy(*ownerProxy, probe, "Whatever");
    EXPECT_EQ(proxyOf(*ownerProxy), "None");
    EXPECT_FALSE(inSysModules(probe)) << probe << " must not have been imported";
    restoreProxy(bare, probe, "Whatever");
    EXPECT_EQ(proxyOf(bare), "None");
    EXPECT_FALSE(inSysModules(probe));
}

TEST_F(ProxyImport, alreadyLoadedRestoresWhereverItLives)
{
    ASSERT_TRUE(inSysModules("fcxproxyimport_loaded"));
    restoreProxy(*ownerProxy, "fcxproxyimport_loaded", "Thing");
    EXPECT_EQ(proxyOf(*ownerProxy), "fcxproxyimport_loaded.Thing");
    restoreProxy(bare, "fcxproxyimport_loaded", "Thing");
    EXPECT_EQ(proxyOf(bare), "fcxproxyimport_loaded.Thing");
}

TEST_F(ProxyImport, missingModuleFailsAsBefore)
{
    // no such module anywhere: the interpreter's own error, logged, and
    // no Proxy -- as it has always been
    restoreProxy(*ownerProxy, "fcxproxyimport_nowhere", "Thing");
    EXPECT_EQ(proxyOf(*ownerProxy), "None");
    restoreProxy(bare, "fcxproxyimport_nowhere", "Thing");
    EXPECT_EQ(proxyOf(bare), "None");
}

TEST_F(ProxyImport, legacyPickleNameFeedsTheSameCheck)
{
    ASSERT_FALSE(inSysModules("fcxproxyimport_outside"));
    restorePickle(*ownerProxy, "fcxproxyimport_outside", "Thing");
    EXPECT_EQ(proxyOf(*ownerProxy), "None");
    EXPECT_FALSE(inSysModules("fcxproxyimport_outside"));
    restorePickle(bare, "fcxproxyimport_loaded", "Thing");
    EXPECT_EQ(proxyOf(bare), "fcxproxyimport_loaded.Thing");
}
