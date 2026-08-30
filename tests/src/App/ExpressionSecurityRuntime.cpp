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
