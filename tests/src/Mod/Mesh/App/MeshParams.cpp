#include "gtest/gtest.h"

#include <string>

#include <App/Application.h>
#include <Mod/Mesh/App/MeshParams.h>
#include <src/App/InitApplication.h>

// A generated setting may be stored under another name than its own: the
// asymptote size is AsymptoteWidth to the program and the key "Width" of the
// sub-group Asymptote in the parameters. The generated class looked for a
// change, and removed the key, under the setting's name -- so a change of
// "Width" never reached it, and removing it removed nothing.
class MeshParamsTest: public ::testing::Test
{
protected:
    static void SetUpTestSuite()
    {
        tests::initApplication();
    }
};

// NOLINTBEGIN(cppcoreguidelines-*,readability-*)
TEST_F(MeshParamsTest, aSettingStoredUnderAnotherNameIsFollowedAndRemoved)
{
    auto group = App::GetApplication().GetParameterGroupByPath(
        "User parameter:BaseApp/Preferences/Mod/Mesh/Asymptote");
    const std::string fallback = Mesh::MeshParams::defaultAsymptoteWidth();
    group->RemoveASCII("Width");
    ASSERT_EQ(Mesh::MeshParams::getAsymptoteWidth(), fallback);

    group->SetASCII("Width", "777");
    EXPECT_EQ(Mesh::MeshParams::getAsymptoteWidth(), "777");

    Mesh::MeshParams::removeAsymptoteWidth();
    EXPECT_EQ(group->GetASCII("Width", "gone"), "gone");
    EXPECT_EQ(Mesh::MeshParams::getAsymptoteWidth(), fallback);

    group->RemoveASCII("Width");
}
// NOLINTEND(cppcoreguidelines-*,readability-*)
