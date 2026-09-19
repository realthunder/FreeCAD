// Tests for the pyodide bootstrap facts (App::ExpressionSandbox::Pyodide,
// docs/PyodideHost.md sec 12): the pinned release table, the per-user
// layout and the resolve order the runtime derives from it, the path
// scoping the guest's reader is confined by, the lock-file package
// lookup, and -- when a pyodide runtime is present on the box -- the
// package offer end to end: an import the guest cannot satisfy records
// a pending pkg.install request, and a package placed into the user's
// set (from a local mirror, no network) is there at the next boot.

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include <App/ExpressionImageHost.h>
#include <App/ExpressionPyodide.h>
#include <App/ExpressionSecurityRuntime.h>

#include "InitApplication.h"

namespace fs = std::filesystem;
using json = nlohmann::json;
namespace Pyodide = App::ExpressionSandbox::Pyodide;
using App::ExpressionSandbox::ImageHost;

namespace
{

/// Set an environment variable for the test and put it back afterwards.
class EnvGuard
{
public:
    EnvGuard(const char* name, const std::string& value)
        : name(name)
    {
        const char* old = std::getenv(name);
        had = old != nullptr;
        if (had)
            previous = old;
        setenv(name, value.c_str(), 1);
    }
    ~EnvGuard()
    {
        if (had)
            setenv(name.c_str(), previous.c_str(), 1);
        else
            unsetenv(name.c_str());
    }

private:
    std::string name;
    std::string previous;
    bool had = false;
};

fs::path scratchDir(const std::string& name)
{
    fs::path base = fs::temp_directory_path() / ("fc-pyodide-test-" + std::to_string(getpid()));
    fs::path dir = base / name;
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);
    return dir;
}

void writeFile(const fs::path& p, const std::string& text)
{
    fs::create_directories(p.parent_path());
    std::ofstream out(p, std::ios::binary);
    out << text;
}

/// A runtime directory that looks installed to the layout scan (the
/// three files it checks), without being a real pyodide.
void fakeRuntime(const fs::path& dir, const std::string& version)
{
    writeFile(dir / "pyodide.js", "// fake\n");
    writeFile(dir / "pyodide.asm.wasm", std::string("\0asm", 4));
    writeFile(dir / "python_stdlib.zip", "PK");
    writeFile(dir / "package.json", "{\"name\": \"pyodide\", \"version\": \"" + version + "\"}\n");
}

}  // namespace

class ExpressionPyodideTest: public ::testing::Test
{
protected:
    void SetUp() override
    {
        tests::initApplication();
    }
    void TearDown() override
    {
        ImageHost::instance().reset();
    }
};

// ---- the table

TEST_F(ExpressionPyodideTest, tablePins3140_6)
{
    const Pyodide::Release* r = Pyodide::releaseFor("314.0.6");
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(r->abi, "2026_0");
    EXPECT_EQ(r->python, "3.14.2");
    ASSERT_EQ(r->files.size(), 6U);
    bool wasm = false;
    for (const auto& f : r->files) {
        EXPECT_EQ(f.sha256.size(), 64U) << f.name;
        if (f.name == "pyodide.asm.wasm")
            wasm = true;
    }
    EXPECT_TRUE(wasm);
    EXPECT_EQ(r->coreTarball, "pyodide-core-314.0.6.tar.bz2");
    EXPECT_EQ(r->coreSha256.size(), 64U);
    EXPECT_EQ(Pyodide::releaseFor("0.27.0"), nullptr);
    EXPECT_FALSE(Pyodide::releases().empty());
}

// ---- verification

TEST_F(ExpressionPyodideTest, verifyRefusesUnpinnedAndTampered)
{
    fs::path dir = scratchDir("verify");
    bool pinned = true;
    std::string reason = Pyodide::verifyDirectory(dir.string(), &pinned);
    EXPECT_FALSE(reason.empty());
    EXPECT_FALSE(pinned);

    fakeRuntime(dir / "0.27.0", "0.27.0");
    reason = Pyodide::verifyDirectory((dir / "0.27.0").string(), &pinned);
    EXPECT_NE(reason.find("not a version"), std::string::npos) << reason;
    EXPECT_FALSE(pinned);

    // the right version with the wrong bytes: pinned, but refused
    fakeRuntime(dir / "314.0.6", "314.0.6");
    reason = Pyodide::verifyDirectory((dir / "314.0.6").string(), &pinned);
    EXPECT_TRUE(pinned);
    EXPECT_FALSE(reason.empty());
    EXPECT_EQ(Pyodide::directoryVersion((dir / "314.0.6").string()), "314.0.6");
    EXPECT_EQ(Pyodide::directoryAbi((dir / "314.0.6").string()), "2026_0");
}

TEST_F(ExpressionPyodideTest, verifyPassesOnARealRuntime)
{
    // The staged distribution of a dev tree (or an installed one): the
    // pinned bytes, so the check must pass.  Skipped where there is none.
    auto where = ImageHost::instance().location();
    if (ImageHost::instance().runtime() != "pyodide" || !fs::is_regular_file(fs::path(where.stdlib) / "pyodide.asm.wasm"))
        GTEST_SKIP() << "no pyodide runtime on this box";
    bool pinned = false;
    std::string reason = Pyodide::verifyDirectory(where.stdlib, &pinned);
    EXPECT_TRUE(pinned);
    EXPECT_TRUE(reason.empty()) << reason;
}

// ---- scoping

TEST_F(ExpressionPyodideTest, askedToPathStripsUrlForms)
{
    EXPECT_EQ(Pyodide::askedToPath("file:///opt/pyodide/pyodide.asm.wasm"), "/opt/pyodide/pyodide.asm.wasm");
    EXPECT_EQ(Pyodide::askedToPath("/opt/pyodide/x"), "/opt/pyodide/x");
    EXPECT_EQ(Pyodide::askedToPath("pyodide.asm.wasm"), "pyodide.asm.wasm");
    // the Windows shape pyodide's URL arithmetic produces
    EXPECT_EQ(Pyodide::askedToPath("file:///C:/Users/x/Pyodide/314.0.6/pyodide.js"),
              "C:/Users/x/Pyodide/314.0.6/pyodide.js");
    EXPECT_EQ(Pyodide::askedToPath("/C:/x"), "C:/x");
    // a bare slash before something that is not a drive letter stays
    EXPECT_EQ(Pyodide::askedToPath("/c/x"), "/c/x");
}

TEST_F(ExpressionPyodideTest, scopeConfinesToRootsComponentWise)
{
    fs::path base = scratchDir("scope");
    fs::path runtime = base / "Pyodide" / "314.0.6";
    fs::path packages = base / "Pyodide" / "packages";
    fs::path sibling = base / "Pyodide" / "314.0.6-other";
    fs::create_directories(runtime);
    fs::create_directories(packages);
    fs::create_directories(sibling);
    writeFile(runtime / "pyodide.js", "x");
    writeFile(packages / "manifest.json", "{}");
    writeFile(sibling / "secret", "x");
    writeFile(base / "outside", "x");
    const std::string canonRuntime = fs::weakly_canonical(runtime).string();
    const std::string canonPackages = fs::weakly_canonical(packages).string();
    std::vector<std::string> roots {canonRuntime, canonPackages};

    // in
    EXPECT_FALSE(Pyodide::scopePath(roots, (runtime / "pyodide.js").string(), canonRuntime).empty());
    EXPECT_FALSE(Pyodide::scopePath(roots, "file://" + (runtime / "pyodide.js").string(), canonRuntime).empty());
    EXPECT_FALSE(Pyodide::scopePath(roots, "pyodide.js", canonRuntime).empty());
    EXPECT_FALSE(Pyodide::scopePath(roots, (packages / "manifest.json").string(), canonRuntime).empty());
    // the loader's own relative form for the package set
    EXPECT_FALSE(Pyodide::scopePath(roots, "../packages/manifest.json", canonRuntime).empty());
    // out
    EXPECT_TRUE(Pyodide::scopePath(roots, (base / "outside").string(), canonRuntime).empty());
    EXPECT_TRUE(Pyodide::scopePath(roots, "../outside", canonRuntime).empty());
    EXPECT_TRUE(Pyodide::scopePath(roots, "../../outside", canonRuntime).empty());
    EXPECT_TRUE(Pyodide::scopePath(roots, "/etc/passwd", canonRuntime).empty());
    // a sibling that shares the root's prefix as a STRING is outside
    EXPECT_TRUE(Pyodide::scopePath(roots, (sibling / "secret").string(), canonRuntime).empty());
    EXPECT_TRUE(Pyodide::scopePath(roots, "", canonRuntime).empty());
    // the root itself is in
    EXPECT_FALSE(Pyodide::scopePath(roots, canonRuntime, canonRuntime).empty());
    // a symlink inside pointing outside is followed and refused
    std::error_code ec;
    fs::create_symlink(base / "outside", runtime / "link", ec);
    if (!ec)
        EXPECT_TRUE(Pyodide::scopePath(roots, (runtime / "link").string(), canonRuntime).empty());

    // a bare name the base lacks is looked up in the fallbacks (the
    // shell-mode loader's form for a lock-file wheel), in the roots only
    writeFile(packages / "numpy-1.whl", "x");
    writeFile(base / "outside.whl", "x");
    EXPECT_EQ(Pyodide::scopePath(roots, "numpy-1.whl", canonRuntime, {canonPackages}),
              (fs::path(canonPackages) / "numpy-1.whl").string());
    // without fallbacks the base form is the (absent) answer
    EXPECT_EQ(Pyodide::scopePath(roots, "numpy-1.whl", canonRuntime),
              (fs::path(canonRuntime) / "numpy-1.whl").string());
    EXPECT_TRUE(Pyodide::scopePath(roots, "outside.whl", canonRuntime,
                                   {fs::weakly_canonical(base).string()}).empty());
    // the base wins when it has the name
    writeFile(runtime / "numpy-1.whl", "y");
    EXPECT_EQ(Pyodide::scopePath(roots, "numpy-1.whl", canonRuntime, {canonPackages}),
              (fs::path(canonRuntime) / "numpy-1.whl").string());
}

// ---- the layout and the resolve order

TEST_F(ExpressionPyodideTest, layoutFindsInstalledRuntimesAndCurrent)
{
    fs::path user = scratchDir("layout");
    EnvGuard userEnv("FCX_PYODIDE_USER", user.string());
    EnvGuard pkgEnv("FCX_PYODIDE_PACKAGES", "");

    Pyodide::Layout empty = Pyodide::layout();
    EXPECT_EQ(empty.userDir, user.string());
    EXPECT_EQ(empty.packages, (user / "packages").string());
    EXPECT_EQ(empty.manifest, (user / "packages" / "manifest.json").string());
    EXPECT_TRUE(empty.installed.empty());
    EXPECT_TRUE(empty.current.empty());

    fakeRuntime(user / "314.0.6", "314.0.6");
    fakeRuntime(user / "315.0.0", "315.0.0");
    writeFile(user / "not-a-runtime" / "readme", "x");
    writeFile(user / "current", "315.0.0\n");
    Pyodide::Layout l = Pyodide::layout();
    ASSERT_EQ(l.installed.size(), 2U);
    EXPECT_EQ(l.installed[0], "314.0.6");
    EXPECT_EQ(l.installed[1], "315.0.0");
    EXPECT_EQ(l.current, "315.0.0");

    // a marker naming something that is not there, or a path, is ignored
    writeFile(user / "current", "316.0.0");
    EXPECT_TRUE(Pyodide::layout().current.empty());
    writeFile(user / "current", "../314.0.6");
    EXPECT_TRUE(Pyodide::layout().current.empty());
}

TEST_F(ExpressionPyodideTest, resolveOrderPrefersEnvThenUserDataThenBundle)
{
    if (ImageHost::instance().runtime() != "pyodide")
        GTEST_SKIP() << "the selected runtime is not pyodide";
    fs::path user = scratchDir("resolve");
    fakeRuntime(user / "314.0.6", "314.0.6");
    writeFile(user / "current", "314.0.6");
    EnvGuard userEnv("FCX_PYODIDE_USER", user.string());
    EnvGuard pkgEnv("FCX_PYODIDE_PACKAGES", "");

    // the user-data runtime, when nothing more explicit is named
    {
        EnvGuard dirEnv("FCX_PYODIDE", "");
        ImageHost::instance().configure("", "");
        auto where = ImageHost::instance().location();
        EXPECT_EQ(where.stdlib, (user / "314.0.6").string());
        EXPECT_EQ(where.packages, (user / "packages").string());
    }
    // the environment beats it
    {
        fs::path other = scratchDir("resolve-env");
        EnvGuard dirEnv("FCX_PYODIDE", other.string());
        ImageHost::instance().configure("", "");
        EXPECT_EQ(ImageHost::instance().location().stdlib, other.string());
    }
    // no marker: the bundle under <datadir>/Pyodide
    {
        fs::remove(user / "current");
        EnvGuard dirEnv("FCX_PYODIDE", "");
        ImageHost::instance().configure("", "");
        auto where = ImageHost::instance().location();
        EXPECT_NE(where.stdlib.find("Pyodide"), std::string::npos);
        EXPECT_EQ(where.stdlib.find(user.string()), std::string::npos);
    }
    ImageHost::instance().configure("", "");
}

// ---- the lock

TEST_F(ExpressionPyodideTest, lockLookupByImportAndByName)
{
    auto where = ImageHost::instance().location();
    if (ImageHost::instance().runtime() != "pyodide" || !fs::is_regular_file(fs::path(where.stdlib) / "pyodide-lock.json"))
        GTEST_SKIP() << "no pyodide lock file on this box";
    auto numpy = Pyodide::lookupImport(where.stdlib, "numpy");
    ASSERT_TRUE(numpy.has_value());
    EXPECT_EQ(numpy->name, "numpy");
    EXPECT_FALSE(numpy->version.empty());
    EXPECT_EQ(numpy->sha256.size(), 64U);
    EXPECT_NE(numpy->fileName.find("numpy-"), std::string::npos);
    auto scipy = Pyodide::lookupPackage(where.stdlib, "scipy");
    ASSERT_TRUE(scipy.has_value());
    EXPECT_FALSE(scipy->depends.empty());
    EXPECT_FALSE(Pyodide::lookupImport(where.stdlib, "no_such_module_anywhere").has_value());
    // an import name that differs from the package name
    auto yaml = Pyodide::lookupImport(where.stdlib, "yaml");
    if (yaml)
        EXPECT_EQ(yaml->name, "pyyaml");
}

TEST_F(ExpressionPyodideTest, manifestGivesLoadOrderAndRefusesOtherAbi)
{
    fs::path pk = scratchDir("manifest");
    writeFile(pk / "manifest.json",
              R"({"version": 1, "abi": "2026_0", "packages": {"scipy": {}, "numpy": {}},
                  "load_order": ["numpy", "scipy"]})");
    auto names = Pyodide::manifestPackages(pk.string(), "2026_0");
    ASSERT_EQ(names.size(), 2U);
    EXPECT_EQ(names[0], "numpy");
    EXPECT_EQ(names[1], "scipy");
    EXPECT_TRUE(Pyodide::manifestPackages(pk.string(), "2027_0").empty());
    // no ABI known on either side: load what is listed
    EXPECT_EQ(Pyodide::manifestPackages(pk.string(), "").size(), 2U);
    EXPECT_TRUE(Pyodide::manifestPackages((pk / "nowhere").string(), "2026_0").empty());
    writeFile(pk / "manifest.json", "not json");
    EXPECT_TRUE(Pyodide::manifestPackages(pk.string(), "2026_0").empty());
}

// ---- the offer, end to end (needs a real pyodide on the box)

class ExpressionPyodideOfferTest: public ExpressionPyodideTest
{
protected:
    void SetUp() override
    {
        ExpressionPyodideTest::SetUp();
        if (ImageHost::instance().runtime() != "pyodide")
            GTEST_SKIP() << "the selected runtime is not pyodide";
        auto where = ImageHost::instance().location();
        if (!fs::is_regular_file(fs::path(where.stdlib) / "pyodide.asm.wasm")
                || !fs::is_regular_file(where.image))
            GTEST_SKIP() << "no pyodide runtime on this box";
        // Boot from a COPY of the runtime holding only the pinned files:
        // a dev tree stages the numpy wheel beside the runtime, and a
        // guest that found it there would pass this test without the
        // package set ever being read.
        mirror = where.stdlib;
        fs::path copy = scratchDir("offer-runtime");
        const Pyodide::Release* rel = Pyodide::releaseFor(Pyodide::directoryVersion(mirror));
        if (!rel)
            GTEST_SKIP() << "the runtime at " << mirror << " is not a pinned version";
        for (const auto& f : rel->files)
            fs::copy_file(fs::path(mirror) / f.name, copy / f.name);
        fs::copy_file(fs::path(mirror) / "package.json", copy / "package.json");
        runtimeDir = copy.string();
        packages = scratchDir("offer-packages");
        dirEnv = std::make_unique<EnvGuard>("FCX_PYODIDE", runtimeDir);
        wheelEnv = std::make_unique<EnvGuard>("FCX_PYODIDE_WHEEL", where.image);
        pkgEnv = std::make_unique<EnvGuard>("FCX_PYODIDE_PACKAGES", packages.string());
        // the paths changed: start from a fresh instance
        ImageHost::instance().configure("", "");
    }
    void TearDown() override
    {
        ImageHost::instance().reset();
        App::ExpressionSecurity::Runtime::instance().clearPending(
                "session", App::ExpressionSecurity::Permission::PkgInstall, "numpy");
        pkgEnv.reset();
        wheelEnv.reset();
        dirEnv.reset();
        ImageHost::instance().configure("", "");
        ExpressionPyodideTest::TearDown();
    }

    static json value(const App::ExpressionSandbox::ImageResult& res)
    {
        return json::from_cbor(res.value.begin(), res.value.end());
    }

    std::string mirror;      ///< the staged runtime, which also holds the wheels
    std::string runtimeDir;  ///< the copy the guest boots from
    fs::path packages;
    std::unique_ptr<EnvGuard> dirEnv;
    std::unique_ptr<EnvGuard> wheelEnv;
    std::unique_ptr<EnvGuard> pkgEnv;
};

TEST_F(ExpressionPyodideOfferTest, missingImportIsOfferedThenLoadedAfterInstall)
{
    using App::ExpressionSecurity::Permission;
    auto& sec = App::ExpressionSecurity::Runtime::instance();
    sec.clearPending("session", Permission::PkgInstall, "numpy");

    // 1. an empty package set: the import is an OFFER, recorded for the
    //    principal (session: no evaluation scope in a raw eval)
    auto res = ImageHost::instance().eval("__import__('numpy').sqrt(4.0)", {});
    ASSERT_FALSE(res.ok);
    EXPECT_EQ(res.excType, "ModuleNotFoundError");
    EXPECT_NE(res.message.find("can install numpy"), std::string::npos) << res.message;
    bool recorded = false;
    for (const auto& p : sec.pendingRequests()) {
        if (p.permission == Permission::PkgInstall && p.target == "numpy")
            recorded = true;
    }
    EXPECT_TRUE(recorded);
    // ... and repeats collapse into the same entry
    ImageHost::instance().eval("__import__('numpy')", {});
    int entries = 0;
    for (const auto& p : sec.pendingRequests())
        if (p.permission == Permission::PkgInstall && p.target == "numpy")
            ++entries;
    EXPECT_EQ(entries, 1);
    // an import nothing knows stays the ordinary error
    res = ImageHost::instance().eval("__import__('no_such_module_anywhere')", {});
    ASSERT_FALSE(res.ok);
    EXPECT_EQ(res.excType, "ModuleNotFoundError");
    EXPECT_EQ(res.message.find("can install"), std::string::npos) << res.message;

    // 2. "install" from a local mirror: the wheel the lock names, placed
    //    into the package set with a manifest -- what freecad.pyodide
    //    does after downloading and verifying
    auto info = Pyodide::lookupImport(runtimeDir, "numpy");
    ASSERT_TRUE(info.has_value());
    fs::path wheel = fs::path(mirror) / info->fileName;
    if (!fs::is_regular_file(wheel))
        GTEST_SKIP() << "no local numpy wheel beside the staged runtime: " << wheel.string();
    fs::copy_file(wheel, packages / info->fileName, fs::copy_options::overwrite_existing);
    json manifest = {{"version", 1},
                     {"abi", Pyodide::directoryAbi(runtimeDir)},
                     {"packages", {{"numpy", {{"version", info->version}, {"file_name", info->fileName}}}}},
                     {"load_order", {"numpy"}}};
    writeFile(packages / "manifest.json", manifest.dump());

    // 3. the live guest does not have it: INSTALLED, and a fresh
    //    instance is scheduled for after this trip
    const std::size_t before = ImageHost::instance().evalCount();
    res = ImageHost::instance().eval("__import__('numpy')", {});
    ASSERT_FALSE(res.ok);
    EXPECT_NE(res.message.find("next evaluation"), std::string::npos) << res.message;

    // 4. the next evaluation boots with the package set: the value
    res = ImageHost::instance().eval("float(__import__('numpy').sqrt(4.0))", {});
    ASSERT_TRUE(res.ok) << res.excType << ": " << res.message;
    EXPECT_DOUBLE_EQ(value(res).get<double>(), 2.0);
    EXPECT_GT(ImageHost::instance().evalCount(), before);
    // ... and the reader never left its roots for it: the package set
    // is one of them, and nothing else was needed
}
