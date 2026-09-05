#include "gtest/gtest.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>

#include "App/ExpressionSecurity.h"

// The expression permission service is a frozen contract
// (docs/ExpressionSandboxPhase0.md sec 6); these tests pin the contract, so
// a failure here means the CONTRACT broke, not just an implementation detail.

using namespace App::ExpressionSecurity;

TEST(ExpressionSecurity, sha256Vectors)
{
    // FIPS 180-4 test vectors.
    EXPECT_EQ(sha256Hex("", 0),
        "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    EXPECT_EQ(sha256Hex("abc", 3),
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    const char *msg = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    EXPECT_EQ(sha256Hex(msg, strlen(msg)),
        "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    // One-byte and block-boundary lengths keep the padding honest.
    std::string a64(64, 'a');
    EXPECT_EQ(sha256Hex(a64.data(), a64.size()),
        "ffe054fe7ae0cb6dc65c3af9b61d5209f439851db43d0ba5997337df154668eb");
    std::string a55(55, 'a');
    EXPECT_EQ(sha256Hex(a55.data(), a55.size()),
        "9f4390f8d30c2dd92ec9f095b65e2b9ae9b0a925a5258e241c9f1e910f734318");
}

TEST(ExpressionSecurity, permissionNames)
{
    // Round trip every catalog name.
    for (Permission perm : {Permission::DocReadSelf, Permission::DocWriteSelf,
            Permission::DocForeign, Permission::GeomCall, Permission::AppQuery,
            Permission::PrefsRead, Permission::PrefsWrite, Permission::Gui, Permission::HostImport,
            Permission::UnsafeGetattr}) {
        auto parsed = permissionFromName(permissionName(perm));
        ASSERT_TRUE(parsed.has_value()) << permissionName(perm);
        EXPECT_EQ(*parsed, perm);
    }

    // The parameterized form splits into permission + target.
    std::string target;
    auto perm = permissionFromName("host.import:Part", &target);
    ASSERT_TRUE(perm.has_value());
    EXPECT_EQ(*perm, Permission::HostImport);
    EXPECT_EQ(target, "Part");

    // Only host.import is parameterized.
    EXPECT_FALSE(permissionFromName("doc.foreign:Other").has_value());
    EXPECT_FALSE(permissionFromName("fs").has_value());
    EXPECT_FALSE(permissionFromName("net").has_value());
    EXPECT_FALSE(permissionFromName("").has_value());
}

TEST(ExpressionSecurity, principalClasses)
{
    std::string hash(64, 'a');
    EXPECT_EQ(principalClass("document:sha256:" + hash), PrincipalClass::Document);
    EXPECT_EQ(principalClass("session"), PrincipalClass::Session);
    EXPECT_EQ(principalClass("addon:Draft"), PrincipalClass::Addon);
    EXPECT_FALSE(principalClass("document:sha256:short").has_value());
    EXPECT_FALSE(principalClass("addon:").has_value());
    EXPECT_FALSE(principalClass("").has_value());
    EXPECT_FALSE(principalClass("somebody").has_value());
}

TEST(ExpressionSecurity, catalogDefaults)
{
    // The frozen v1 table, row by row: document / session / addon.
    struct Row {
        Permission perm;
        Decision doc;
        Decision session;
    };
    const Row rows[] = {
        {Permission::DocReadSelf,   Decision::Allow,  Decision::Allow},
        {Permission::DocWriteSelf,  Decision::Allow,  Decision::Allow},
        {Permission::DocForeign,    Decision::Prompt, Decision::Allow},
        {Permission::GeomCall,      Decision::Allow,  Decision::Allow},
        {Permission::AppQuery,      Decision::Prompt, Decision::Allow},
        {Permission::PrefsRead,     Decision::Allow,  Decision::Allow},
        {Permission::Gui,           Decision::Deny,   Decision::Allow},
        {Permission::HostImport,    Decision::Prompt, Decision::Prompt},
        {Permission::UnsafeGetattr, Decision::Deny,   Decision::Prompt},
    };
    for (const auto &row : rows) {
        EXPECT_EQ(catalogDefault(PrincipalClass::Document, row.perm), row.doc)
            << permissionName(row.perm);
        EXPECT_EQ(catalogDefault(PrincipalClass::Session, row.perm), row.session)
            << permissionName(row.perm);
        EXPECT_EQ(catalogDefault(PrincipalClass::Addon, row.perm), Decision::Allow)
            << permissionName(row.perm);
    }

    // Exactly one not-promptable cell: (document, gui).
    EXPECT_FALSE(isPromptable(PrincipalClass::Document, Permission::Gui));
    EXPECT_TRUE(isPromptable(PrincipalClass::Session, Permission::Gui));
    EXPECT_TRUE(isPromptable(PrincipalClass::Document, Permission::UnsafeGetattr));
}

TEST(ExpressionSecurity, pseudoPropertyMapping)
{
    std::string module;
    EXPECT_EQ(pseudoPropertyPermission("_pla"), Permission::DocReadSelf);
    EXPECT_EQ(pseudoPropertyPermission("_matrix"), Permission::DocReadSelf);
    EXPECT_EQ(pseudoPropertyPermission("__pla"), Permission::DocReadSelf);
    EXPECT_EQ(pseudoPropertyPermission("__matrix"), Permission::DocReadSelf);
    EXPECT_EQ(pseudoPropertyPermission("_ref"), Permission::DocReadSelf);
    EXPECT_EQ(pseudoPropertyPermission("_self"), Permission::UnsafeGetattr);
    EXPECT_EQ(pseudoPropertyPermission("_shape"), Permission::GeomCall);
    EXPECT_EQ(pseudoPropertyPermission("_app"), Permission::AppQuery);
    EXPECT_EQ(pseudoPropertyPermission("_gui"), Permission::Gui);

    EXPECT_EQ(pseudoPropertyPermission("_part", &module), Permission::HostImport);
    EXPECT_EQ(module, "Part");
    EXPECT_EQ(pseudoPropertyPermission("_cq", &module), Permission::HostImport);
    EXPECT_EQ(module, "freecad.fc_cadquery");

    // Ring-0 in-image modules need no permission.
    EXPECT_FALSE(pseudoPropertyPermission("_math").has_value());
    EXPECT_FALSE(pseudoPropertyPermission("_re").has_value());
    EXPECT_FALSE(pseudoPropertyPermission("_coll").has_value());
    EXPECT_FALSE(pseudoPropertyPermission("_py").has_value());
    EXPECT_FALSE(pseudoPropertyPermission("_unknown").has_value());
}

TEST(ExpressionSecurity, documentHashCanonicalization)
{
    // The empty document: hash of the bare version prefix.
    DocumentHashBuilder empty;
    std::string prefix("fcexpr-v1");
    prefix.push_back('\0');
    EXPECT_EQ(empty.principalId(),
        "document:sha256:" + sha256Hex(prefix.data(), prefix.size()));

    // Order independent: serialization order must not matter.
    DocumentHashBuilder ab;
    ab.addExpression("Box.Length + 1");
    ab.addExpression("Sketch.Radius * 2");
    DocumentHashBuilder ba;
    ba.addExpression("Sketch.Radius * 2");
    ba.addExpression("Box.Length + 1");
    EXPECT_EQ(ab.principalId(), ba.principalId());

    // Duplicates collapse: a second copy of granted code does not re-prompt.
    DocumentHashBuilder dup;
    dup.addExpression("Box.Length + 1");
    dup.addExpression("Box.Length + 1");
    dup.addExpression("Sketch.Radius * 2");
    EXPECT_EQ(dup.principalId(), ab.principalId());

    // Editing any code voids the identity.
    DocumentHashBuilder edited;
    edited.addExpression("Box.Length + 2");
    edited.addExpression("Sketch.Radius * 2");
    EXPECT_NE(edited.principalId(), ab.principalId());

    // The item kind is part of the canonical form: the same string as a
    // cell formula is a different document than as a bound expression.
    DocumentHashBuilder asExpr;
    asExpr.addExpression("A1 + 1");
    DocumentHashBuilder asCell;
    asCell.addCell("A1 + 1");
    EXPECT_NE(asExpr.principalId(), asCell.principalId());

    // Scripts carry class and code.
    DocumentHashBuilder script1, script2;
    script1.addScript("mod.Cls", "code");
    script2.addScript("mod.Cls", "code2");
    EXPECT_NE(script1.principalId(), script2.principalId());

    EXPECT_TRUE(empty.empty());
    EXPECT_FALSE(ab.empty());
}

TEST(ExpressionSecurity, grantLookupPrecedence)
{
    const std::string principal = "document:sha256:" + std::string(64, 'a');
    GrantStore store;

    // Nothing configured: no answer (caller falls back to the catalog).
    EXPECT_FALSE(store.lookup(principal, Permission::DocForeign, "Other").has_value());

    // A wildcard allow answers any target.
    Grant allowAll;
    allowAll.principal = principal;
    allowAll.permission = "doc.foreign";
    allowAll.target = "*";
    allowAll.allow = true;
    store.add(allowAll);
    EXPECT_EQ(store.lookup(principal, Permission::DocForeign, "Other"), Decision::Allow);
    EXPECT_EQ(store.lookup(principal, Permission::DocForeign, "Third"), Decision::Allow);

    // An explicit deny outranks any allow, even the wildcard.
    Grant denyOne;
    denyOne.principal = principal;
    denyOne.permission = "doc.foreign";
    denyOne.target = "Other";
    denyOne.allow = false;
    store.add(denyOne);
    EXPECT_EQ(store.lookup(principal, Permission::DocForeign, "Other"), Decision::Deny);
    EXPECT_EQ(store.lookup(principal, Permission::DocForeign, "Third"), Decision::Allow);

    // Another principal is unaffected.
    const std::string other = "document:sha256:" + std::string(64, 'b');
    EXPECT_FALSE(store.lookup(other, Permission::DocForeign, "Other").has_value());

    // Store-level defaults answer when no grant matches.
    store.defaults()["unsafe.getattr"] = Decision::Deny;
    EXPECT_EQ(store.lookup(principal, Permission::UnsafeGetattr, "*"), Decision::Deny);

    // The parameterized permission form normalizes into the target.
    Grant hostImport;
    hostImport.principal = principal;
    hostImport.permission = "host.import:Part";
    hostImport.allow = true;
    store.add(hostImport);
    EXPECT_EQ(store.lookup(principal, Permission::HostImport, "Part"), Decision::Allow);
    EXPECT_FALSE(store.lookup(principal, Permission::HostImport, "os").has_value());

    EXPECT_EQ(store.removePrincipal(principal), 3u);
    EXPECT_FALSE(store.lookup(principal, Permission::DocForeign, "Other").has_value());
}

TEST(ExpressionSecurity, grantStoreRoundTrip)
{
    const std::string path = testing::TempDir() + "/fc_expr_grants_test.json";
    std::remove(path.c_str());

    const std::string principal = "document:sha256:" + std::string(64, 'c');
    {
        GrantStore store;
        store.defaults()["app.query"] = Decision::Prompt;
        Grant grant;
        grant.principal = principal;
        grant.permission = "doc.foreign";
        grant.target = "Other";
        grant.allow = true;
        grant.displayLabel = "widget.FCStd";
        grant.displayPath = "/tmp/widget.FCStd";
        store.add(grant);
        std::string err;
        ASSERT_TRUE(store.save(path, &err)) << err;
    }
    {
        GrantStore store;
        std::string err;
        ASSERT_TRUE(store.load(path, &err)) << err;
        ASSERT_EQ(store.grants().size(), 1u);
        const Grant &grant = store.grants().front();
        EXPECT_EQ(grant.principal, principal);
        EXPECT_EQ(grant.permission, "doc.foreign");
        EXPECT_EQ(grant.target, "Other");
        EXPECT_TRUE(grant.allow);
        EXPECT_FALSE(grant.grantedUtc.empty());  // stamped by add()
        EXPECT_EQ(grant.displayLabel, "widget.FCStd");
        EXPECT_EQ(store.defaults().at("app.query"), Decision::Prompt);
        EXPECT_EQ(store.lookup(principal, Permission::DocForeign, "Other"),
                  Decision::Allow);
    }
    std::remove(path.c_str());

    // A missing file is an empty store, not an error.
    GrantStore store;
    std::string err;
    EXPECT_TRUE(store.load(path, &err)) << err;
    EXPECT_TRUE(store.grants().empty());
}

TEST(ExpressionSecurity, grantStoreRejectsNewerSchema)
{
    const std::string path = testing::TempDir() + "/fc_expr_grants_v99.json";
    {
        std::ofstream out(path.c_str());
        out << "{\"version\": 99, \"grants\": []}\n";
    }
    GrantStore store;
    std::string err;
    EXPECT_FALSE(store.load(path, &err));
    EXPECT_FALSE(err.empty());
    std::remove(path.c_str());

    // Malformed JSON also fails, with a message.
    const std::string bad = testing::TempDir() + "/fc_expr_grants_bad.json";
    {
        std::ofstream out(bad.c_str());
        out << "{not json";
    }
    err.clear();
    EXPECT_FALSE(store.load(bad, &err));
    EXPECT_FALSE(err.empty());
    std::remove(bad.c_str());
}

TEST(ExpressionSecurity, auditLogAppends)
{
    const std::string path = testing::TempDir() + "/fc_expr_audit_test.log";
    std::remove(path.c_str());

    AuditLog log(path);
    const std::string principal = "document:sha256:" + std::string(64, 'd');
    ASSERT_TRUE(log.append(principal, Permission::DocForeign, "Other",
                           Decision::Deny, "headless"));
    ASSERT_TRUE(log.append("session", Permission::HostImport, "Part",
                           Decision::Allow));

    std::ifstream in(path.c_str());
    std::string line1, line2, extra;
    ASSERT_TRUE(std::getline(in, line1));
    ASSERT_TRUE(std::getline(in, line2));
    EXPECT_FALSE(std::getline(in, extra));

    // One JSON object per line carrying the decision tuple.
    EXPECT_NE(line1.find("\"decision\":\"deny\""), std::string::npos);
    EXPECT_NE(line1.find("\"permission\":\"doc.foreign\""), std::string::npos);
    EXPECT_NE(line1.find("\"context\":\"headless\""), std::string::npos);
    EXPECT_NE(line1.find(principal), std::string::npos);
    EXPECT_NE(line2.find("\"decision\":\"allow\""), std::string::npos);
    EXPECT_NE(line2.find("\"target\":\"Part\""), std::string::npos);
    std::remove(path.c_str());
}

TEST(ExpressionSecurity, utcNowShape)
{
    const std::string now = utcNow();
    ASSERT_EQ(now.size(), 20u);
    EXPECT_EQ(now[4], '-');
    EXPECT_EQ(now[7], '-');
    EXPECT_EQ(now[10], 'T');
    EXPECT_EQ(now[13], ':');
    EXPECT_EQ(now[16], ':');
    EXPECT_EQ(now.back(), 'Z');
}
