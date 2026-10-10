// SPDX-License-Identifier: LGPL-2.1-or-later

#include <gtest/gtest.h>
#include "src/App/InitApplication.h"

#include <Base/Exception.h>
#include <Base/FileInfo.h>
#include <Base/Interpreter.h>
#include <Mod/Import/App/dxf/ImpExpDxf.h>

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

// The DXF writer only noted the file it could not open: nothing was written
// and nothing said -- through Draft's DXF export (Import.writeDXFObject) and
// TechDraw's writeDXFPage/writeDXFView alike, which all build this writer.
TEST_F(ImportErrorsTest, dxfWriterThrowsOnAFileItCannotOpen)
{
    auto dir = Base::FileInfo::getTempPath() + "fc_import_errors_no_such_dir";
    Base::FileInfo(dir).deleteDirectoryRecursive();
    EXPECT_THROW(Import::ImpExpDxfWrite writer(dir + "/x.dxf"), Base::FileException);

    auto file = Base::FileInfo::getTempFileName("fc_import_errors", nullptr) + ".dxf";
    EXPECT_NO_THROW({
        Import::ImpExpDxfWrite writer(file);
        writer.init();
        writer.endRun();
    });
    EXPECT_TRUE(Base::FileInfo(file).exists());
    Base::FileInfo(file).deleteFile();
}

// Not an error raised but an option not taken: the writer was pointed at
// Preferences/Mod/Import for its options, where nothing stores them, so what
// the DXF preference page says about the export (Mod/Draft) never reached it.
// An ellipse is written as an ELLIPSE, and as a polyline with "Treat ellipses
// and splines as polylines" on.
TEST_F(ImportErrorsTest, writeDXFShapeTakesTheOptionsOfTheDxfPage)
{
    try {
        Base::Interpreter().runString(
            "_ie_p = FreeCAD.ParamGet('User parameter:BaseApp/Preferences/Mod/Draft')\n"
            "_ie_had = 'DiscretizeEllipses' in _ie_p.GetBools()\n"
            "_ie_old = _ie_p.GetBool('DiscretizeEllipses', False)\n"
            "def _ie_kinds(flag):\n"
            "    _ie_p.SetBool('DiscretizeEllipses', flag)\n"
            "    name = os.path.join(_ie_tmp, 'ellipse%d.dxf' % flag)\n"
            "    shape = Part.Ellipse(FreeCAD.Vector(0, 0, 0), 20, 10).toShape()\n"
            "    Import.writeDXFShape([shape], name)\n"
            "    with open(name, errors='replace') as f:\n"
            "        lines = [line.strip() for line in f]\n"
            "    return set(lines[i + 1] for i in range(len(lines) - 1) if lines[i] == '0')\n"
            "try:\n"
            "    _ie_off, _ie_on = _ie_kinds(False), _ie_kinds(True)\n"
            "finally:\n"
            "    if _ie_had:\n"
            "        _ie_p.SetBool('DiscretizeEllipses', _ie_old)\n"
            "    else:\n"
            "        _ie_p.RemBool('DiscretizeEllipses')\n"
            "assert 'ELLIPSE' in _ie_off, sorted(_ie_off)\n"
            "assert 'ELLIPSE' not in _ie_on, sorted(_ie_on)\n"
            "assert 'LWPOLYLINE' in _ie_on or 'POLYLINE' in _ie_on, sorted(_ie_on)\n");
    }
    catch (const Base::Exception& e) {
        ADD_FAILURE() << e.what();
    }
}

TEST_F(ImportErrorsTest, writeDXFShapeToNoDirectoryIsIOError)
{
    expectRaises("Import.writeDXFShape([Part.makeBox(1, 1, 1)],"
                 " os.path.join(_ie_tmp, 'none', 'y.dxf'))",
                 "OSError");
}
