// SPDX-License-Identifier: LGPL-2.1-or-later

#include <gtest/gtest.h>
#include "src/App/InitApplication.h"

#include <Base/Exception.h>
#include <Base/Interpreter.h>

// What a failing call of the Import module raises. Its functions caught a
// C++ failure with _PY_CATCH_OCC(return Py::None()): the error was set and
// a value returned with it, which Python turns into SystemError ("returned
// a result with an exception set"), the real exception only its cause --
// and that took the module's own IOError for an unknown file type with it.
// Each case runs in Python and fails the run on the wrong type.

class ImportErrorsTest: public ::testing::Test
{
protected:
    static void SetUpTestSuite()
    {
        tests::initApplication();
        // A test binary is not where FreeCAD looks for its modules; see this
        // suite's CMakeLists.
        Base::Interpreter().runString("import sys; sys.path[:0] = ['" FC_BUILD_LIB_DIR
                                     "', '" FC_BUILD_MOD_PART_DIR
                                     "', '" FC_BUILD_MOD_MATERIAL_DIR
                                     "', '" FC_BUILD_MOD_IMPORT_DIR "']");
        Base::Interpreter().runString(
            "import os, tempfile, FreeCAD, Part, Import\n"
            "_ie_tmp = tempfile.mkdtemp(prefix='fc_import_errors_')\n"
            "def _ie_raises(fn, kind):\n"
            "    try:\n"
            "        fn()\n"
            "    except kind:\n"
            "        return\n"
            "    except Exception as e:\n"
            "        raise AssertionError('raised %s, not %s: %s'\n"
            "                             % (type(e).__name__, kind.__name__, e))\n"
            "    raise AssertionError('raised nothing, not ' + kind.__name__)\n");
    }

    static void expectRaises(const char* call, const char* kind)
    {
        std::string code = "_ie_raises(lambda: ";
        code += call;
        code += ", ";
        code += kind;
        code += ")";
        try {
            Base::Interpreter().runString(code.c_str());
        }
        catch (const Base::Exception& e) {
            ADD_FAILURE() << call << ": " << e.what();
        }
    }
};

TEST_F(ImportErrorsTest, insertOfAnUnknownTypeIsIOError)
{
    expectRaises("Import.insert(os.path.join(_ie_tmp, 'x.foo'))", "OSError");
}

TEST_F(ImportErrorsTest, insertOfAMissingFileIsIOError)
{
    expectRaises("Import.insert(os.path.join(_ie_tmp, 'none', 'x.step'))", "OSError");
    expectRaises("Import.insert(os.path.join(_ie_tmp, 'none', 'x.iges'))", "OSError");
}

TEST_F(ImportErrorsTest, writeDXFShapeOfNoShapeIsTypeError)
{
    expectRaises("Import.writeDXFShape([123], os.path.join(_ie_tmp, 'y.dxf'))", "TypeError");
}

// The writer only noted the file it could not open: nothing was written and
// nothing said.
TEST_F(ImportErrorsTest, writeDXFShapeToNoDirectoryIsIOError)
{
    expectRaises("Import.writeDXFShape([Part.makeBox(1, 1, 1)],"
                 " os.path.join(_ie_tmp, 'none', 'y.dxf'))",
                 "OSError");
}
