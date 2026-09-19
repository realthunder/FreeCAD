// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Base::Type::importModule imports the module a type string's prefix names
// ("Part::Feature" -> Part), and the string comes from document content (a
// saved property type, a PropertyPersistentObject).  User ruling 2026-09-05
// (docs/Sandbox.md 13): the import is allowed only for a module already
// loaded or one that resolves into a registered Mod root -- a stdlib or
// site-packages module a file happens to name is refused, natively.

#include "gtest/gtest.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

#include <Base/Exception.h>
#include <Base/Interpreter.h>
#include <Base/PyObjectBase.h>
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

class TypeImport: public ::testing::Test
{
protected:
    static void SetUpTestSuite()
    {
        tests::initApplication();
    }
    void SetUp() override
    {
        // a Mod root of this test's own, with one module in a workbench
        // directory, and a directory OUTSIDE any root with another; both
        // on sys.path, as FreeCADInit puts every Mod/<X> there
        namespace fs = std::filesystem;
        base = fs::path(std::tmpnam(nullptr)).string() + "-fcxtypeimport";
        modRoot = base + "/Mod";
        const std::string wb = modRoot + "/FcxWb";
        const std::string elsewhere = base + "/elsewhere";
        fs::create_directories(wb);
        fs::create_directories(elsewhere);
        std::ofstream(wb + "/fcxtypeimport_ok.py") << "MARK = 'ok'\n";
        fs::create_directories(wb + "/fcxtypeimport_pkg");
        std::ofstream(wb + "/fcxtypeimport_pkg/__init__.py") << "";
        std::ofstream(wb + "/fcxtypeimport_pkg/inner.py") << "MARK = 'inner'\n";
        std::ofstream(elsewhere + "/fcxtypeimport_outside.py") << "MARK = 'outside'\n";
        // a meta-path finder, FEM's migrate_app in miniature: it answers
        // for one virtual name with a spec that has no file location.
        // The same source in both places; which copy is imported decides
        // where the finder "lives".
        const char* finder =
            "import importlib.abc, importlib.util, sys, types\n"
            "class Finder(importlib.abc.MetaPathFinder, importlib.abc.Loader):\n"
            "    def __init__(self, name):\n"
            "        self.name = name\n"
            "    def find_spec(self, fullname, path=None, target=None):\n"
            "        if fullname == self.name:\n"
            "            return importlib.util.spec_from_loader(fullname, self)\n"
            "        return None\n"
            "    def create_module(self, spec):\n"
            "        return types.ModuleType(spec.name)\n"
            "    def exec_module(self, module):\n"
            "        module.MARK = 'virtual'\n"
            "def install(name):\n"
            "    f = Finder(name)\n"
            "    sys.meta_path.insert(0, f)\n"
            "    return f\n";
        std::ofstream(wb + "/fcxtypeimport_finder_in.py") << finder;
        std::ofstream(elsewhere + "/fcxtypeimport_finder_out.py") << finder;
        Base::Interpreter().addPythonPath(wb.c_str());
        Base::Interpreter().addPythonPath(elsewhere.c_str());
        Base::Type::addModuleRoot(modRoot);
    }
    void TearDown() override
    {
        std::error_code ec;
        std::filesystem::remove_all(base, ec);
    }
    std::string base;
    std::string modRoot;
};

}  // namespace

TEST_F(TypeImport, underAModRootImports)
{
    ASSERT_FALSE(inSysModules("fcxtypeimport_ok"));
    EXPECT_TRUE(Base::Type::moduleAllowed("fcxtypeimport_ok"));
    EXPECT_NO_THROW(Base::Type::importModule("fcxtypeimport_ok::Thing"));
    EXPECT_TRUE(inSysModules("fcxtypeimport_ok"));
}

TEST_F(TypeImport, onSysPathOutsideEveryRootIsRefused)
{
    ASSERT_FALSE(inSysModules("fcxtypeimport_outside"));
    EXPECT_FALSE(Base::Type::moduleAllowed("fcxtypeimport_outside"));
    EXPECT_THROW(Base::Type::importModule("fcxtypeimport_outside::Thing"), Base::RuntimeError);
    EXPECT_FALSE(inSysModules("fcxtypeimport_outside")) << "a refused module must not be imported";
}

TEST_F(TypeImport, stdlibIsRefused)
{
    // a document naming "<stdlib module>::Whatever": the stdlib is not a
    // Mod root.  Any module the init script has not already imported
    // will do; the first of these that is still unloaded is the probe.
    const char* probe = nullptr;
    for (const char* name : {"wave", "colorsys", "mailbox", "csv", "json"}) {
        if (!inSysModules(name)) {
            probe = name;
            break;
        }
    }
    ASSERT_NE(probe, nullptr) << "every candidate stdlib module is already loaded";
    EXPECT_FALSE(Base::Type::moduleAllowed(probe));
    EXPECT_THROW(Base::Type::importModule((std::string(probe) + "::Whatever").c_str()),
                 Base::RuntimeError);
    EXPECT_FALSE(inSysModules(probe)) << probe << " must not have been imported";
}

TEST_F(TypeImport, alreadyLoadedIsAllowed)
{
    // a module in sys.modules runs nothing new: allowed wherever it lives
    ASSERT_TRUE(inSysModules("sys"));
    EXPECT_TRUE(Base::Type::moduleAllowed("sys"));
    EXPECT_NO_THROW(Base::Type::importModule("sys::Thing"));
}

TEST_F(TypeImport, missingModuleFailsAsBefore)
{
    // no such module anywhere: not a refusal but the interpreter's own
    // error, as callers have always logged it
    EXPECT_TRUE(Base::Type::moduleAllowed("fcxtypeimport_nowhere"));
    EXPECT_THROW(Base::Type::importModule("fcxtypeimport_nowhere::Thing"), Base::PyException);
}

TEST_F(TypeImport, coreModulesSkipTheImport)
{
    EXPECT_NO_THROW(Base::Type::importModule("App::DocumentObject"));
    EXPECT_NO_THROW(Base::Type::importModule("Base::Persistence"));
}

TEST_F(TypeImport, dottedUnderAModRootImports)
{
    // a dotted name is walked one level at a time; every level resolves
    // under the root
    ASSERT_FALSE(inSysModules("fcxtypeimport_pkg.inner"));
    EXPECT_TRUE(Base::Type::moduleAllowed("fcxtypeimport_pkg.inner"));
    EXPECT_NO_THROW(Base::Type::importModule("fcxtypeimport_pkg.inner::Thing"));
    EXPECT_TRUE(inSysModules("fcxtypeimport_pkg.inner"));
}

TEST_F(TypeImport, dottedUnderALoadedStdlibParentIsRefused)
{
    // a loaded stdlib package does not admit an unloaded submodule of
    // its own: the level below it resolves into the stdlib, not a root
    Base::Interpreter().runString("import xml");
    ASSERT_TRUE(inSysModules("xml"));
    const char* probe = nullptr;
    for (const char* name :
         {"xml.sax.saxutils", "xml.dom.minidom", "xml.etree.ElementTree", "xml.parsers.expat"}) {
        if (!inSysModules(name)) {
            probe = name;
            break;
        }
    }
    ASSERT_NE(probe, nullptr) << "every candidate xml submodule is already loaded";
    EXPECT_FALSE(Base::Type::moduleAllowed(probe));
    EXPECT_THROW(Base::Type::importModule((std::string(probe) + "::Thing").c_str()),
                 Base::RuntimeError);
    EXPECT_FALSE(inSysModules(probe)) << probe << " must not have been imported";
}

TEST_F(TypeImport, aFinderUnderARootAdmitsItsVirtualModule)
{
    // FEM's migrate_app: a saved legacy name that no file answers for,
    // served by a finder defined under Mod/Fem
    Base::Interpreter().runString("import fcxtypeimport_finder_in\n"
                                  "_fcx_f = fcxtypeimport_finder_in.install('fcxtypeimport_virtual_in')\n");
    ASSERT_FALSE(inSysModules("fcxtypeimport_virtual_in"));
    EXPECT_TRUE(Base::Type::moduleAllowed("fcxtypeimport_virtual_in"));
    EXPECT_NO_THROW(Base::Type::importModule("fcxtypeimport_virtual_in::Thing"));
    EXPECT_TRUE(inSysModules("fcxtypeimport_virtual_in"));
    Base::Interpreter().runString("import sys\nsys.meta_path.remove(_fcx_f)\n");
}

TEST_F(TypeImport, aFinderOutsideEveryRootIsRefused)
{
    // the same finder imported from outside the roots: its answer is
    // refused, as the module it stands for would be
    Base::Interpreter().runString("import fcxtypeimport_finder_out\n"
                                  "_fcx_f = fcxtypeimport_finder_out.install('fcxtypeimport_virtual_out')\n");
    ASSERT_FALSE(inSysModules("fcxtypeimport_virtual_out"));
    EXPECT_FALSE(Base::Type::moduleAllowed("fcxtypeimport_virtual_out"));
    EXPECT_THROW(Base::Type::importModule("fcxtypeimport_virtual_out::Thing"), Base::RuntimeError);
    EXPECT_FALSE(inSysModules("fcxtypeimport_virtual_out"));
    Base::Interpreter().runString("import sys\nsys.meta_path.remove(_fcx_f)\n");
}
