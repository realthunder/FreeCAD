#include "gtest/gtest.h"

#include "App/PropertyLinks.h"
#include <src/App/InitApplication.h>

// The Application has to exist before a property may be touched: this fork's
// Property::touch() asks GetApplication().isClosingAll() before it does
// anything, and GetApplication() hands back a null reference until then. The
// other suites in this directory take the same step for the same reason.
class PropertyLink: public ::testing::Test
{
protected:
    static void SetUpTestSuite()
    {
        tests::initApplication();
    }
};

TEST_F(PropertyLink, TestSetValues)
{
    App::PropertyLinkSubList prop;
    std::vector<App::DocumentObject*> objs {nullptr, nullptr};
    std::vector<const char*> subs {"Sub1", "Sub2"};
    prop.setValues(objs, subs);
    const auto& sub = prop.getSubValues();
    EXPECT_EQ(sub.size(), 2);
    EXPECT_EQ(sub[0], "Sub1");
    EXPECT_EQ(sub[1], "Sub2");
}
