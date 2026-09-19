#include "gtest/gtest.h"

#include <cstdio>
#include <fstream>

#include "App/Application.h"
#include "App/Document.h"
#include "App/DocumentObject.h"
#include "App/Expression.h"
#include "App/ExpressionSecurityRuntime.h"
#include "App/ObjectIdentifier.h"
#include "App/PropertyStandard.h"
#include "Base/Interpreter.h"
#include "InitApplication.h"

// Enforcement runtime tests (expression sandbox phase 1 step 3b). The
// runtime is a process singleton; each test uses its own permission targets
// so the in-memory answer maps cannot alias across tests. The persisted
// store is redirected to a policy file (read-only) so no test ever touches
// the user's grants.json.

using namespace App::ExpressionSecurity;

class ExpressionSecurityRuntimeTest: public ::testing::Test
{
protected:
    static void SetUpTestSuite()
    {
        tests::initApplication();
        // never read or write the real user grant store from tests
        static bool redirected;
        if (!redirected) {
            redirected = true;
            _policyPath = std::string(std::tmpnam(nullptr)) + "-policy.json";
            Runtime::instance().setPolicyFile(_policyPath);
        }
    }

    void SetUp() override
    {
        _docName = App::GetApplication().getUniqueDocumentName("sectest");
        _doc = App::GetApplication().newDocument(_docName.c_str(), "testUser");
        _obj = _doc->addObject("App::FeatureTest", "obj");
    }

    void TearDown() override
    {
        App::GetApplication().closeDocument(_docName.c_str());
    }

    static std::string _policyPath;
    std::string _docName;
    App::Document *_doc = nullptr;
    App::DocumentObject *_obj = nullptr;
};

std::string ExpressionSecurityRuntimeTest::_policyPath;

TEST_F(ExpressionSecurityRuntimeTest, noScopeNoCheck)
{
    // host code outside any evaluation is not gated
    ASSERT_FALSE(Runtime::scopeActive());
    EXPECT_NO_THROW(checkPermission(Permission::Gui));
}

// ---- the host file and code chokepoints (F1, docs/Sandbox.md 7.29) ----

TEST_F(ExpressionSecurityRuntimeTest, hostPathNormalization)
{
    // One spelling per file, so an answer is keyed to the FILE and not
    // to whichever spelling reached the primitive.
    EXPECT_EQ(normalizeHostPath("/tmp/fcx-f1/./a.FCStd"), normalizeHostPath("/tmp/fcx-f1/a.FCStd"));
    EXPECT_EQ(normalizeHostPath("/tmp/fcx-f1/x/../a.FCStd"),
              normalizeHostPath("/tmp/fcx-f1/a.FCStd"));
    // a trailing separator would make one directory two targets
    EXPECT_EQ(normalizeHostPath("/tmp/fcx-f1/"), normalizeHostPath("/tmp/fcx-f1"));
    EXPECT_TRUE(normalizeHostPath("").empty());
    // a file being SAVED to is named before it exists, and must still
    // normalize the way a later read of it will
    EXPECT_EQ(normalizeHostPath("/tmp/fcx-f1-absent/./b.FCStd"),
              normalizeHostPath("/tmp/fcx-f1-absent/b.FCStd"));
}

TEST_F(ExpressionSecurityRuntimeTest, blessedPathsAreKeyedToTheFile)
{
    clearBlessedPaths();
    EXPECT_FALSE(pathBlessed("/tmp/fcx-f1/picked.FCStd"));
    blessPath("/tmp/fcx-f1/./picked.FCStd");
    EXPECT_TRUE(pathBlessed("/tmp/fcx-f1/picked.FCStd"));
    EXPECT_TRUE(pathBlessed("/tmp/fcx-f1/x/../picked.FCStd"));
    EXPECT_FALSE(pathBlessed("/tmp/fcx-f1/other.FCStd"));
    blessPath("");  // blesses nothing
    EXPECT_FALSE(pathBlessed(""));
    clearBlessedPaths();
    EXPECT_FALSE(pathBlessed("/tmp/fcx-f1/picked.FCStd"));
}

TEST_F(ExpressionSecurityRuntimeTest, hostFileChokepointsUnderADocumentScope)
{
    clearBlessedPaths();
    const std::string path = "/tmp/fcx-f1-doc/secret.FCStd";
    // host code outside any evaluation reaches every file, as it always did
    ASSERT_FALSE(Runtime::scopeActive());
    EXPECT_NO_THROW(checkHostPath(Permission::FsRead, path));
    EXPECT_NO_THROW(checkHostPath(Permission::FsWrite, path));
    EXPECT_NO_THROW(checkHostPath(Permission::HostExec, path));

    Runtime::Scope scope(_obj);
    // a document's own code reaches no host file, and the catalog does
    // not offer to lift it: DENY, not promptable
    EXPECT_THROW(checkHostPath(Permission::FsWrite, path), PermissionNeededException);
    EXPECT_THROW(checkHostPath(Permission::HostExec, path), PermissionNeededException);
    try {
        checkHostPath(Permission::FsRead, path);
        FAIL() << "a document principal read a host file";
    }
    catch (const PermissionNeededException &e) {
        EXPECT_FALSE(e.isPromptable());
        // the refusal names the file, normalized
        EXPECT_EQ(e.getTarget(), normalizeHostPath(path));
    }
}

TEST_F(ExpressionSecurityRuntimeTest, aBlessedPathPassesTheChokepoint)
{
    clearBlessedPaths();
    const std::string picked = "/tmp/fcx-f1-doc/picked.FCStd";
    blessPath(picked);
    {
        Runtime::Scope scope(_obj);
        // consent stays a capability (S1): the file the user chose in a
        // picker that ran inside this guest passes with no grant at all,
        // while a path the guest made up does not
        EXPECT_NO_THROW(checkHostPath(Permission::FsWrite, picked));
        EXPECT_NO_THROW(checkHostPath(Permission::FsRead, "/tmp/fcx-f1-doc/./picked.FCStd"));
        EXPECT_THROW(checkHostPath(Permission::FsWrite, "/tmp/fcx-f1-doc/madeup.FCStd"),
                     PermissionNeededException);
    }
    clearBlessedPaths();
}

TEST_F(ExpressionSecurityRuntimeTest, saveAsIsGatedAtThePrimitive)
{
    clearBlessedPaths();
    const std::string path = std::string(std::tmpnam(nullptr)) + "-f1.FCStd";
    {
        Runtime::Scope scope(_obj);
        EXPECT_THROW(_doc->saveAs(path.c_str()), PermissionNeededException);
    }
    // refused before anything was written
    std::ifstream written(path.c_str());
    EXPECT_FALSE(written.good());
}

TEST_F(ExpressionSecurityRuntimeTest, theInterpreterFileGuardRefusesHostCode)
{
    // Base cannot see this runtime, so App hands runFile a guard.  It is
    // installed here rather than leaned on from the application's own
    // startup, so what the case proves is the seam itself.
    Base::InterpreterSingleton::setFileGuard(
        [](const char *f) { checkHostPath(Permission::HostExec, f ? f : ""); });
    const std::string script = std::string(std::tmpnam(nullptr)) + "-f1.py";
    {
        std::ofstream out(script.c_str());
        out << "raise SystemExit\n";
    }
    {
        Runtime::Scope scope(_obj);
        EXPECT_THROW(Base::Interpreter().runFile(script.c_str(), true), PermissionNeededException);
    }
    // Put back a guard that checks the same thing, rather than clearing
    // it: Application::init installs one, and leaving this suite with
    // none would quietly un-gate runFile for every later case in this
    // binary.
    Base::InterpreterSingleton::setFileGuard(
        [](const char *f) { checkHostPath(Permission::HostExec, f ? f : ""); });
    std::remove(script.c_str());
}

TEST_F(ExpressionSecurityRuntimeTest, documentScopeCatalogDefaults)
{
    Runtime::Scope scope(_obj);
    ASSERT_TRUE(Runtime::scopeActive());
    // frozen catalog for document principals
    EXPECT_NO_THROW(checkPermission(Permission::DocReadSelf));
    EXPECT_NO_THROW(checkPermission(Permission::GeomCall));
    EXPECT_THROW(checkPermission(Permission::DocForeign, "OtherDoc"),
            PermissionNeededException);
    EXPECT_THROW(checkPermission(Permission::Gui), PermissionNeededException);
    EXPECT_THROW(checkPermission(Permission::UnsafeGetattr),
            PermissionNeededException);
    // gui is not even promptable for documents
    try {
        checkPermission(Permission::Gui);
        FAIL() << "expected PermissionNeededException";
    } catch (PermissionNeededException &e) {
        EXPECT_FALSE(e.isPromptable());
    }
    // doc.foreign is promptable
    try {
        checkPermission(Permission::DocForeign, "OtherDoc");
        FAIL() << "expected PermissionNeededException";
    } catch (PermissionNeededException &e) {
        EXPECT_TRUE(e.isPromptable());
        EXPECT_EQ(e.getPermission(), Permission::DocForeign);
        EXPECT_EQ(e.getTarget(), "OtherDoc");
    }
}

TEST_F(ExpressionSecurityRuntimeTest, clientScope)
{
    // a remote client on a served document (docs/Sandbox.md 7.20, C3)
    RemoteClient client;
    client.principal = clientPrincipalId("", 0, 900001);
    client.context = "#900001 'rt-client'";
    {
        // the client form pushes whatever is active, as a chain call does
        Runtime::Scope outer("session");
        Runtime::Scope scope(_doc, client);
        EXPECT_EQ(Runtime::instance().currentPrincipal(), "client:conn:900001");
        EXPECT_NO_THROW(checkPermission(Permission::DocReadSelf));
        EXPECT_NO_THROW(checkPermission(Permission::DocWriteSelf));
        EXPECT_NO_THROW(checkPermission(Permission::AppQuery));
        try {
            checkPermission(Permission::AppWrite);
            FAIL() << "expected PermissionNeededException";
        }
        catch (PermissionNeededException &e) {
            EXPECT_FALSE(e.isPromptable());
            EXPECT_EQ(e.getPrincipal(), "client:conn:900001");
            EXPECT_NE(std::string(e.what()).find("this client"), std::string::npos) << e.what();
        }
        try {
            checkPermission(Permission::HostImport, "rt_client_module");
            FAIL() << "expected PermissionNeededException";
        }
        catch (PermissionNeededException &e) {
            EXPECT_TRUE(e.isPromptable());
        }
    }
    Runtime::instance().clearPending(client.principal, Permission::HostImport, "*");

    // a view-only connection: doc.write.self refused, not promptable,
    // even with a grant in hand
    RemoteClient viewer = client;
    viewer.readOnly = true;
    Runtime::instance().grant(viewer.principal, Permission::DocWriteSelf, "*", true, "session");
    {
        Runtime::Scope scope(_doc, viewer);
        EXPECT_NO_THROW(checkPermission(Permission::DocReadSelf));
        try {
            checkPermission(Permission::DocWriteSelf);
            FAIL() << "expected PermissionNeededException";
        }
        catch (PermissionNeededException &e) {
            EXPECT_FALSE(e.isPromptable());
        }
    }
    Runtime::instance().revoke(viewer.principal, Permission::DocWriteSelf, "*");

    // gui and unsafe.getattr are not grantable to a client at all, and a
    // run-local client id is never persisted
    EXPECT_THROW(Runtime::instance().grant(client.principal, Permission::Gui, "*", true, "once"),
                 Base::ValueError);
    EXPECT_THROW(Runtime::instance().grant(client.principal, Permission::HostImport, "m", true,
                                           "always"),
                 Base::ValueError);
    EXPECT_EQ(Runtime::instance().resolve(client.principal, Permission::UnsafeGetattr, "*"),
              Decision::Deny);

    // a null document still pushes: an empty stack would be host code
    {
        Runtime::Scope scope(static_cast<const App::Document *>(nullptr), client);
        EXPECT_TRUE(Runtime::scopeActive());
        EXPECT_THROW(checkPermission(Permission::Gui), PermissionNeededException);
    }
    EXPECT_FALSE(Runtime::scopeActive());
}

TEST_F(ExpressionSecurityRuntimeTest, promptRecordsPendingAndGrantClears)
{
    auto &rt = Runtime::instance();
    std::string principal = rt.documentPrincipal(_doc);
    {
        Runtime::Scope scope(_obj);
        EXPECT_THROW(checkPermission(Permission::DocForeign, "PendingDoc"),
                PermissionNeededException);
    }
    bool found = false;
    for (auto &r : rt.pendingRequests(_docName)) {
        if (r.permission == Permission::DocForeign && r.target == "PendingDoc") {
            found = true;
            EXPECT_EQ(r.principal, principal);
            EXPECT_EQ(r.objectName, "obj");
        }
    }
    EXPECT_TRUE(found);
    auto objs = rt.pendingObjects(principal);
    ASSERT_FALSE(objs.empty());
    EXPECT_EQ(objs[0].second, "obj");

    // grant -> works -> revoke -> fails (session scope)
    rt.grant(principal, Permission::DocForeign, "PendingDoc", true, "session");
    for (auto &r : rt.pendingRequests(_docName))
        EXPECT_FALSE(r.permission == Permission::DocForeign
                && r.target == "PendingDoc");
    {
        Runtime::Scope scope(_obj);
        EXPECT_NO_THROW(checkPermission(Permission::DocForeign, "PendingDoc"));
    }
    EXPECT_EQ(rt.revoke(principal, Permission::DocForeign, "PendingDoc"), 1u);
    {
        Runtime::Scope scope(_obj);
        EXPECT_THROW(checkPermission(Permission::DocForeign, "PendingDoc"),
                PermissionNeededException);
    }
    rt.clearPending(principal, Permission::DocForeign, "*");
}

TEST_F(ExpressionSecurityRuntimeTest, onceAnswerAndClearOnce)
{
    auto &rt = Runtime::instance();
    std::string principal = rt.documentPrincipal(_doc);
    rt.grant(principal, Permission::DocForeign, "OnceDoc", true, "once");
    {
        Runtime::Scope scope(_obj);
        EXPECT_NO_THROW(checkPermission(Permission::DocForeign, "OnceDoc"));
    }
    rt.clearOnce();
    {
        Runtime::Scope scope(_obj);
        EXPECT_THROW(checkPermission(Permission::DocForeign, "OnceDoc"),
                PermissionNeededException);
    }
    rt.clearPending(principal, Permission::DocForeign, "*");
}

TEST_F(ExpressionSecurityRuntimeTest, hostImportAncestorTargets)
{
    auto &rt = Runtime::instance();
    rt.grant("session", Permission::HostImport, "acme", true, "session");
    EXPECT_EQ(rt.resolve("session", Permission::HostImport, "acme.sub.mod"),
            Decision::Allow);
    EXPECT_EQ(rt.resolve("session", Permission::HostImport, "acmex"),
            Decision::Prompt);
    rt.revoke("session", Permission::HostImport, "acme");
}

TEST_F(ExpressionSecurityRuntimeTest, sessionScopeWinsOverDocumentEntry)
{
    // an explicit outer session scope must not be displaced by the
    // evaluation-entry push
    Runtime::Scope outer("session");
    Runtime::Scope inner(_obj);  // no-op: stack not empty
    EXPECT_EQ(Runtime::instance().currentPrincipal(), "session");
    // session catalog: doc.foreign is ALLOW
    EXPECT_NO_THROW(checkPermission(Permission::DocForeign, "AnyDoc"));
    // host.import stays PROMPT for the session principal
    EXPECT_THROW(checkPermission(Permission::HostImport, "acme.only.session"),
            PermissionNeededException);
    Runtime::instance().clearPending("session", Permission::HostImport, "*");
}

TEST_F(ExpressionSecurityRuntimeTest, documentPrincipalFollowsExpressionEdits)
{
    auto &rt = Runtime::instance();
    std::string before = rt.documentPrincipal(_doc);
    EXPECT_EQ(before.compare(0, 16, "document:sha256:"), 0);
    // stable while unchanged
    EXPECT_EQ(rt.documentPrincipal(_doc), before);

    // binding an expression changes the code set, so the principal changes
    auto obj = _obj;
    App::ObjectIdentifier path(*obj->getPropertyByName("Float"));
    obj->ExpressionEngine.setValue(path,
            std::shared_ptr<App::Expression>(
                App::Expression::parse(obj, "1 + 1").release()));
    std::string after = rt.documentPrincipal(_doc);
    EXPECT_NE(after, before);

    // removing it again returns to the empty-set hash
    obj->ExpressionEngine.setValue(path, std::shared_ptr<App::Expression>());
    EXPECT_EQ(rt.documentPrincipal(_doc), before);
}

TEST_F(ExpressionSecurityRuntimeTest, evaluationCrossDocumentPrompts)
{
    auto &rt = Runtime::instance();
    // second document to reference
    std::string otherName = App::GetApplication().getUniqueDocumentName("secother");
    auto other = App::GetApplication().newDocument(otherName.c_str(), "testUser");
    auto otherObj = other->addObject("App::FeatureTest", "objB");
    (void)otherObj;

    auto obj = _obj;
    App::ObjectIdentifier path(*obj->getPropertyByName("Float"));
    std::string exprStr = otherName + "#objB.Float + 1";
    obj->ExpressionEngine.setValue(path,
            std::shared_ptr<App::Expression>(
                App::Expression::parse(obj, exprStr).release()));

    auto exprs = obj->ExpressionEngine.getExpressions();
    ASSERT_EQ(exprs.size(), 1u);
    const App::Expression *bound = exprs.begin()->second;

    // document principal: doc.foreign prompts
    EXPECT_THROW(bound->getValueAsAny(), PermissionNeededException);

    // grant to THIS document's principal (hash includes the expression)
    std::string principal = rt.documentPrincipal(_doc);
    rt.grant(principal, Permission::DocForeign, otherName, true, "session");
    EXPECT_NO_THROW(bound->getValueAsAny());
    rt.revoke(principal, Permission::DocForeign, otherName);
    rt.clearPending(principal, Permission::DocForeign, "*");

    App::GetApplication().closeDocument(otherName.c_str());
}

TEST_F(ExpressionSecurityRuntimeTest, policyDefaultsApplyAfterGrants)
{
    // write a policy: default deny for geom.call, but a grant allowing a
    // specific host.import ancestor -- the grant must win over the default
    auto &rt = Runtime::instance();
    std::string path = std::string(std::tmpnam(nullptr)) + "-p2.json";
    {
        std::ofstream f(path);
        f << "{\"version\":1,\"defaults\":{\"geom.call\":\"deny\","
             "\"host.import\":\"deny\"},"
             "\"grants\":[{\"principal\":\"session\","
             "\"permission\":\"host.import\",\"target\":\"polmod\","
             "\"decision\":\"allow\",\"scope\":\"always\","
             "\"granted_utc\":\"2026-08-30T00:00:00Z\"}]}";
    }
    rt.setPolicyFile(path);
    EXPECT_EQ(rt.resolve("session", Permission::GeomCall, "*"), Decision::Deny);
    EXPECT_EQ(rt.resolve("session", Permission::HostImport, "polmod.sub"),
            Decision::Allow);
    EXPECT_EQ(rt.resolve("session", Permission::HostImport, "unrelated"),
            Decision::Deny);
    // restore the suite-wide empty policy
    rt.setPolicyFile(_policyPath);
    std::remove(path.c_str());
}
