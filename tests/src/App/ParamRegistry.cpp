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
    EXPECT_EQ(info->displayPath(), "Preferences/Document/CheckExtension");

    EXPECT_EQ(reg.find(DocumentPath, "NoSuchEntry"), nullptr);
    EXPECT_EQ(reg.find(nullptr, "CheckExtension"), nullptr);
}

TEST_F(ParamRegistryTest, displayPathDropsTheCommonRoot)
{
    ParamInfo a("T", "C", "User parameter:BaseApp/Preferences/View", "x", "x", ParamInfo::Bool, true);
    EXPECT_EQ(a.displayPath(), "Preferences/View/x");
    ParamInfo b("T", "C", "User parameter:BaseApp", "x", "x", ParamInfo::Bool, true);
    EXPECT_EQ(b.displayPath(), "x");
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

// A setting's documentation is what the omni search lists beside it, and its
// tool tip. Every setting has some, and none of it is a page: what belongs
// in a design document stays out of here (or above the setting, as a comment
// in its definition file). The Gui test asks the same of the Gui classes.
TEST_F(ParamRegistryTest, everySettingIsDocumentedBriefly)
{
    constexpr std::size_t maxLength = 400;
    const auto& entries = ParamRegistry::instance().entries();
    ASSERT_FALSE(entries.empty());
    for (const ParamInfo* info : entries) {
        const std::string doc = info->doc ? info->doc : "";
        EXPECT_FALSE(doc.empty()) << info->fullName() << " has no documentation";
        EXPECT_LE(doc.size(), maxLength)
            << info->fullName() << ": " << doc.size() << " characters of documentation";
    }
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

TEST(ParamRegistryValues, normalizeValue)
{
    std::string res;
    EXPECT_TRUE(ParamRegistry::normalizeValue(ParamInfo::Bool, "YES", res));
    EXPECT_EQ(res, "true");
    EXPECT_TRUE(ParamRegistry::normalizeValue(ParamInfo::Bool, "0", res));
    EXPECT_EQ(res, "false");
    EXPECT_FALSE(ParamRegistry::normalizeValue(ParamInfo::Bool, "", res));
    EXPECT_TRUE(ParamRegistry::normalizeValue(ParamInfo::Int, "-0x10", res));
    EXPECT_EQ(res, "-16");
    EXPECT_FALSE(ParamRegistry::normalizeValue(ParamInfo::Int, "3.5", res));
    EXPECT_TRUE(ParamRegistry::normalizeValue(ParamInfo::UInt, "42", res));
    EXPECT_EQ(res, "42");
    EXPECT_TRUE(ParamRegistry::normalizeValue(ParamInfo::Hex, "3425907456", res));
    EXPECT_EQ(res, "0xCC333300");
    EXPECT_FALSE(ParamRegistry::normalizeValue(ParamInfo::Hex, "red", res));
    EXPECT_TRUE(ParamRegistry::normalizeValue(ParamInfo::Float, "2.50", res));
    EXPECT_EQ(res, "2.5");
    EXPECT_FALSE(ParamRegistry::normalizeValue(ParamInfo::Float, "", res));
    EXPECT_TRUE(ParamRegistry::normalizeValue(ParamInfo::String, "", res));
    EXPECT_EQ(res, "");
}

// A setting described at run time -- what a module written in Python does
// through FreeCAD.registerParam() -- is an entry like any other, and the
// registry keeps the strings the description came with.
TEST_F(ParamRegistryTest, aSettingDescribedAtRunTime)
{
    auto& reg = ParamRegistry::instance();
    const std::size_t before = reg.entries().size();
    const ParamInfo* info = nullptr;
    {
        App::ParamSpec spec;
        spec.nameSpace = "Test";
        spec.className = "RunTimeParams";
        spec.path = DocumentPath;
        spec.entry = "OmniTestRunTimeChoice";
        spec.type = ParamInfo::Int;
        spec.defaultValue = "0x1";
        spec.title = "A run time choice";
        spec.doc = "Described by the registry's own test.";
        spec.proxy = "ComboBox";
        spec.items = {{"Ask", "", ""}, {"Always", "Every time", ""}, {"Never", "", ""}};
        info = reg.add(spec);
        ASSERT_NE(info, nullptr);

        // the first description stands
        spec.title = "Another";
        EXPECT_EQ(reg.add(spec), nullptr);
    }
    EXPECT_EQ(reg.entries().size(), before + 1);
    EXPECT_EQ(reg.entries().back(), info);
    EXPECT_EQ(reg.find(DocumentPath, "OmniTestRunTimeChoice"), info);

    EXPECT_EQ(info->fullName(), "Test::RunTimeParams::OmniTestRunTimeChoice");
    EXPECT_EQ(info->displayPath(), "Preferences/Document/OmniTestRunTimeChoice");
    EXPECT_STREQ(info->title, "A run time choice");
    EXPECT_EQ(info->type, ParamInfo::Int);
    EXPECT_EQ(info->defaultValue, "1");
    EXPECT_STREQ(info->proxy, "ComboBox");
    ASSERT_EQ(info->items.size(), 3u);
    EXPECT_STREQ(info->items[1].text, "Always");
    EXPECT_STREQ(info->items[1].tooltip, "Every time");
    EXPECT_EQ(info->items[1].data, nullptr);
    EXPECT_FALSE(info->comboDataIsString);

    auto hits = reg.search({"runtimeparams::", "run", "choice"});
    ASSERT_EQ(hits.size(), 1u);
    EXPECT_EQ(hits[0], info);

    EXPECT_EQ(reg.getValue(*info), "1");
    EXPECT_TRUE(reg.setValue(*info, "2"));
    EXPECT_EQ(reg.getValue(*info), "2");
    reg.reset(*info);
    EXPECT_FALSE(reg.isSet(*info));

    // not over a generated class's entry, not without a place, and not with
    // a default that is no value of the type
    App::ParamSpec bad;
    bad.path = DocumentPath;
    bad.entry = "CheckExtension";
    bad.defaultValue = "true";
    bad.doc = "x";
    EXPECT_EQ(reg.add(bad), nullptr);
    bad.entry = "";
    EXPECT_EQ(reg.add(bad), nullptr);
    bad.entry = "OmniTestRunTimeBad";
    bad.path = "";
    EXPECT_EQ(reg.add(bad), nullptr);
    bad.path = DocumentPath;
    bad.defaultValue = "maybe";
    EXPECT_EQ(reg.add(bad), nullptr);
    EXPECT_EQ(reg.entries().size(), before + 1);

    // a choice stored as text
    App::ParamSpec text;
    text.path = DocumentPath;
    text.entry = "OmniTestRunTimeText";
    text.name = "RunTimeText";
    text.type = ParamInfo::String;
    text.defaultValue = "b";
    text.doc = "Described by the registry's own test.";
    text.proxy = "ComboBox";
    text.comboDataIsString = true;
    text.items = {{"First", "", "a"}, {"Second", "", "b"}};
    const ParamInfo* choice = reg.add(text);
    ASSERT_NE(choice, nullptr);
    EXPECT_STREQ(choice->name, "RunTimeText");
    EXPECT_STREQ(choice->entry, "OmniTestRunTimeText");
    EXPECT_TRUE(choice->comboDataIsString);
    EXPECT_STREQ(choice->items[1].data, "b");
    EXPECT_EQ(reg.getValue(*choice), "b");
}
