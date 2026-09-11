// SPDX-License-Identifier: LGPL-2.1-or-later

#include "gtest/gtest.h"

#include <App/Application.h>
#include <App/ParamRegistry.h>
#include <Base/Parameter.h>
#include <src/App/InitApplication.h>

using App::ParamInfo;
using App::ParamRegistry;

namespace
{

// The registrar of App's own DocumentParams runs when the library loads,
// so the registry is populated before any test body.
const char* const DocumentPath = "User parameter:BaseApp/Preferences/Document";

class ParamRegistryTest: public ::testing::Test
{
protected:
    void SetUp() override
    {
        tests::initApplication();
    }
};

}  // namespace

TEST_F(ParamRegistryTest, populatedByGeneratedRegistrars)
{
    auto& reg = ParamRegistry::instance();
    EXPECT_FALSE(reg.entries().empty());

    // DocumentParams.py declares these under App::DocumentParams
    const ParamInfo* info = reg.find(DocumentPath, "CheckExtension");
    ASSERT_NE(info, nullptr);
    EXPECT_STREQ(info->nameSpace, "App");
    EXPECT_STREQ(info->className, "DocumentParams");
    EXPECT_EQ(info->type, ParamInfo::Bool);
    EXPECT_EQ(info->fullName(), "App::DocumentParams::CheckExtension");
    EXPECT_EQ(info->fullPath(), std::string(DocumentPath) + "/CheckExtension");
    EXPECT_EQ(info->displayPath(), "/Preferences/Document/CheckExtension");

    EXPECT_EQ(reg.find(DocumentPath, "NoSuchEntry"), nullptr);
    EXPECT_EQ(reg.find(nullptr, "CheckExtension"), nullptr);
}

TEST_F(ParamRegistryTest, displayPathDropsTheCommonRoot)
{
    ParamInfo a("T", "C", "User parameter:BaseApp/Preferences/View", "x", "x", ParamInfo::Bool, true);
    EXPECT_EQ(a.displayPath(), "/Preferences/View/x");
    ParamInfo b("T", "C", "User parameter:BaseApp", "x", "x", ParamInfo::Bool, true);
    EXPECT_EQ(b.displayPath(), "/x");
    ParamInfo c("T", "C", "System parameter:Other/Group", "x", "y", ParamInfo::Bool, true);
    EXPECT_EQ(c.displayPath(), "/Other/Group/y");
    ParamInfo d("T", "C", "BaseAppX/Group", "x", "x", ParamInfo::Bool, true);
    EXPECT_EQ(d.displayPath(), "/BaseAppX/Group/x");
    ParamInfo e("T", "C", "p", "x", "x", ParamInfo::Bool, true);
    EXPECT_EQ(e.displayPath(), "/p/x");
}

TEST_F(ParamRegistryTest, defaultsAreFormattedByType)
{
    ParamInfo b("T", "C", "p", "b", "b", ParamInfo::Bool, true);
    EXPECT_EQ(b.defaultValue, "true");
    ParamInfo i("T", "C", "p", "i", "i", ParamInfo::Int, -42);
    EXPECT_EQ(i.defaultValue, "-42");
    ParamInfo u("T", "C", "p", "u", "u", ParamInfo::UInt, 42u);
    EXPECT_EQ(u.defaultValue, "42");
    ParamInfo h("T", "C", "p", "h", "h", ParamInfo::Hex, 0xCCCCE6FFu);
    EXPECT_EQ(h.defaultValue, "0xCCCCE6FF");
    ParamInfo f("T", "C", "p", "f", "f", ParamInfo::Float, 0.5);
    EXPECT_EQ(f.defaultValue, "0.5");
    ParamInfo s("T", "C", "p", "s", "s", ParamInfo::String, "Tab");
    EXPECT_EQ(s.defaultValue, "Tab");

    // the builder
    ParamInfo c("T", "C", "p", "c", "c", ParamInfo::Int, 1);
    c.setTitle("A title").setDoc("Some doc").setProxy("SpinBox").setRange(0, 10, 2, 0).setOnChange();
    EXPECT_STREQ(c.title, "A title");
    EXPECT_STREQ(c.proxy, "SpinBox");
    EXPECT_EQ(c.maximum, 10);
    EXPECT_EQ(c.step, 2);
    EXPECT_TRUE(c.onChange);
    EXPECT_EQ(c.searchText(), "p/c T::C::c A title Some doc");
}

TEST(ParamRegistryKeywords, matchKeywords)
{
    EXPECT_TRUE(ParamRegistry::matchKeywords("anything", {}));
    EXPECT_TRUE(ParamRegistry::matchKeywords("User parameter:BaseApp/Preferences/TreeView/SyncView",
                                             {"tree", "SYNC"}));
    EXPECT_FALSE(ParamRegistry::matchKeywords("User parameter:BaseApp/Preferences/TreeView/SyncView",
                                              {"tree", "render"}));
    EXPECT_TRUE(ParamRegistry::matchKeywords("abc", {"", "B"}));

    auto keys = ParamRegistry::splitKeywords("  tree   sync\tview ");
    ASSERT_EQ(keys.size(), 3u);
    EXPECT_EQ(keys[0], "tree");
    EXPECT_EQ(keys[2], "view");
}

TEST_F(ParamRegistryTest, searchRunsOverPathNameAndDoc)
{
    auto& reg = ParamRegistry::instance();
    auto hits = reg.search({"Preferences/Document", "checkextension"});
    ASSERT_EQ(hits.size(), 1u);
    EXPECT_STREQ(hits[0]->name, "CheckExtension");

    // by class name, which is not part of the path
    hits = reg.search({"DocumentParams::"});
    EXPECT_GT(hits.size(), 1u);
    for (auto info : hits) {
        EXPECT_STREQ(info->className, "DocumentParams");
    }

    EXPECT_TRUE(reg.search({"no-such-keyword-anywhere"}).empty());
}

TEST_F(ParamRegistryTest, valuesRoundTripThroughTheParameterGroup)
{
    auto& reg = ParamRegistry::instance();
    const ParamInfo* b = reg.find(DocumentPath, "CheckExtension");
    ASSERT_NE(b, nullptr);

    const bool wasSet = reg.isSet(*b);
    const std::string before = reg.getValue(*b);

    EXPECT_TRUE(reg.setValue(*b, "false"));
    EXPECT_EQ(reg.getValue(*b), "false");
    EXPECT_TRUE(reg.isSet(*b));
    EXPECT_TRUE(reg.setValue(*b, "1"));
    EXPECT_EQ(reg.getValue(*b), "true");
    EXPECT_FALSE(reg.setValue(*b, "maybe"));
    EXPECT_EQ(reg.getValue(*b), "true");

    reg.reset(*b);
    EXPECT_FALSE(reg.isSet(*b));
    EXPECT_EQ(reg.getValue(*b), b->defaultValue);

    if (wasSet) {
        reg.setValue(*b, before);
    }

    // a numeric entry, and the parse failure modes
    ParamInfo tmp("T", "C", DocumentPath, "OmniTestFloat", "OmniTestFloat", ParamInfo::Float, 1.5);
    EXPECT_EQ(reg.getValue(tmp), "1.5");
    EXPECT_TRUE(reg.setValue(tmp, "2.25"));
    EXPECT_EQ(reg.getValue(tmp), "2.25");
    EXPECT_FALSE(reg.setValue(tmp, "2.25x"));
    EXPECT_EQ(reg.getValue(tmp), "2.25");
    reg.reset(tmp);
    EXPECT_EQ(reg.getValue(tmp), "1.5");

    ParamInfo hex("T", "C", DocumentPath, "OmniTestHex", "OmniTestHex", ParamInfo::Hex, 0xFFu);
    EXPECT_TRUE(reg.setValue(hex, "0x1234abcd"));
    EXPECT_EQ(reg.getValue(hex), "0x1234ABCD");
    reg.reset(hex);
    EXPECT_EQ(reg.getValue(hex), "0x000000FF");
}
