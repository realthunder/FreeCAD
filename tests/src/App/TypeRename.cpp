// SPDX-License-Identifier: LGPL-2.1-or-later

/** Renaming a registered type without losing what already names it
 *
 * A type name is a wire format and a scripting API at once: it is the type=
 * attribute of every saved property and object, and the string every
 * addObject/addProperty/isDerivedFrom passes. Renaming the C++ class breaks
 * both, and both break SILENTLY -- fromName answers badType, a restore takes
 * the "type changed" branch and drops the value without a word.
 *
 * Base::Type::addLegacyName is what keeps them working, and this is what
 * holds it to account: the old spelling still resolves, a container restores
 * from a file that states the old spelling, and a save aimed at schema 4 --
 * upstream's format, which predates every rename here -- writes the old name
 * back out.
 */

#include <gtest/gtest.h>

#include <cstring>
#include <sstream>
#include <string>

#include <App/Application.h>
#include <App/Document.h>
#include <App/DocumentObject.h>
#include <App/DocumentObjectGroup.h>
#include <App/PropertyStandard.h>
#include <Base/Exception.h>
#include <Base/Reader.h>
#include <Base/Type.h>
#include <Base/Writer.h>

#include <src/App/InitApplication.h>

namespace App
{
/// A stand-in for a renamed property type: registered under a name of its
/// own, answering to a former one. Nothing in the tree is called either, so
/// the test says nothing about which real renames exist. It lives in App
/// so its registered name carries a namespace -- Type::importModule takes
/// everything before the "::" as a module to load, and App is one of the
/// three it knows not to; a bare name would have it try to load a module
/// called "".
class PropertyRenamedStandIn: public PropertyInteger
{
    TYPESYSTEM_HEADER_WITH_OVERRIDE();
};

TYPESYSTEM_SOURCE(App::PropertyRenamedStandIn, App::PropertyInteger)

}  // namespace App

namespace
{

constexpr const char* NewName = "App::PropertyRenamedStandIn";
constexpr const char* OldName = "App::PropertyStandInFormerName";

class TypeRenameTest: public ::testing::Test
{
protected:
    static void SetUpTestSuite()
    {
        tests::initApplication();
        if (Base::Type::fromName(NewName).isBad()) {
            App::PropertyRenamedStandIn::init();
            Base::Type::addLegacyName(App::PropertyRenamedStandIn::getClassTypeId(), OldName);
        }
    }
};

TEST_F(TypeRenameTest, oldNameResolvesToTheRenamedType)
{
    EXPECT_EQ(Base::Type::fromName(OldName), App::PropertyRenamedStandIn::getClassTypeId());
    EXPECT_EQ(Base::Type::fromName(NewName), App::PropertyRenamedStandIn::getClassTypeId());
}

TEST_F(TypeRenameTest, theTypeStillCallsItselfByItsCurrentName)
{
    // The alias is a way in, not a second identity: everything that asks the
    // type its name gets the current one.
    EXPECT_STREQ(App::PropertyRenamedStandIn::getClassTypeId().getName(), NewName);
    EXPECT_STREQ(App::PropertyRenamedStandIn::getClassTypeId().getLegacyName(), OldName);
}

TEST_F(TypeRenameTest, aTypeThatWasNeverRenamedHasNoLegacyName)
{
    EXPECT_EQ(App::PropertyInteger::getClassTypeId().getLegacyName(), nullptr);
}

TEST_F(TypeRenameTest, anAliasNeverShadowsALiveType)
{
    // Two types answering to one name would make fromName pick by
    // registration order. Asking for that is refused, and the name keeps
    // meaning what it meant.
    Base::Type::addLegacyName(App::PropertyRenamedStandIn::getClassTypeId(), "App::PropertyInteger");
    EXPECT_EQ(Base::Type::fromName("App::PropertyInteger"),
              App::PropertyInteger::getClassTypeId());
    EXPECT_STREQ(App::PropertyRenamedStandIn::getClassTypeId().getLegacyName(), OldName);
}

TEST_F(TypeRenameTest, schemaFourWritesTheFormerName)
{
    Base::StringWriter writer;

    // Upstream's format, and older than every rename this fork has made.
    writer.setSchemaVersion(4);
    EXPECT_STREQ(writer.typeName(App::PropertyRenamedStandIn::getClassTypeId()), OldName);

    // This fork's format, which was never released stating the old names.
    writer.setSchemaVersion(5);
    EXPECT_STREQ(writer.typeName(App::PropertyRenamedStandIn::getClassTypeId()), NewName);

    // No document schema resolved -- an object export, a content dump.
    // Nothing there is written for an older reader.
    writer.setSchemaVersion(0);
    EXPECT_STREQ(writer.typeName(App::PropertyRenamedStandIn::getClassTypeId()), NewName);
}

TEST_F(TypeRenameTest, schemaFourWritesTheCurrentNameOfAnUnrenamedType)
{
    Base::StringWriter writer;
    writer.setSchemaVersion(4);
    EXPECT_STREQ(writer.typeName(App::PropertyInteger::getClassTypeId()),
                 "App::PropertyInteger");
}

/** The one that matters: a document written before the rename still restores
 *
 * PropertyContainer::Restore used to compare the saved type name with the
 * property's own by strcmp, so a file naming the old type fell into the
 * "name matches but not the type" branch and handleChangedPropertyType threw
 * the value away in silence. Resolving the name instead is what this checks.
 */
TEST_F(TypeRenameTest, aContainerRestoresFromAFileStatingTheFormerName)
{
    const std::string docName = App::GetApplication().getUniqueDocumentName("typeRename");
    App::Document* doc = App::GetApplication().newDocument(docName.c_str(), "testUser");
    ASSERT_NE(doc, nullptr);

    App::DocumentObject* obj = doc->addObject("App::DocumentObjectGroup", "Obj");
    ASSERT_NE(obj, nullptr);

    auto* prop = static_cast<App::PropertyInteger*>(
        obj->addDynamicProperty(NewName, "StandIn", "Test"));
    ASSERT_NE(prop, nullptr);
    prop->setValue(42);

    std::stringstream xml;
    xml << R"(<?xml version="1.0" encoding="UTF-8"?><document>)"
        << "<Properties Count=\"1\" TransientCount=\"0\">"
        << "<Property name=\"StandIn\" type=\"" << OldName << "\" group=\"Test\">"
        << "<Integer value=\"7\"/>"
        << "</Property>"
        << "</Properties>"
        << "</document>";

    // PropertyContainer::Restore by name, not the object's own override: the
    // property block is what this is about, and DocumentObject::Restore
    // expects the rest of an object element around it.
    Base::XMLReader reader("<memory>", xml);
    try {
        obj->PropertyContainer::Restore(reader);
    }
    catch (const Base::Exception& e) {
        FAIL() << "Base::Exception: " << e.what();
    }
    catch (const std::exception& e) {
        FAIL() << "std::exception: " << e.what();
    }

    EXPECT_EQ(prop->getValue(), 7) << "the value was dropped: the saved type name "
                                      "was compared rather than resolved";

    App::GetApplication().closeDocument(docName.c_str());
}

}  // namespace
