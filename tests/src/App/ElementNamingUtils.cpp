// SPDX-License-Identifier: LGPL-2.1-or-later

#include <gtest/gtest.h>

#include <App/ElementNamingUtils.h>

// NOLINTBEGIN(readability-magic-numbers)

// From upstream 5568b40a07, which made findElementName() skip leading dots.
// The fork's version already returned the element after a leading dot; this
// pins it, and the other shapes of subname upstream checks.
TEST(ElementNamingUtilsTest, findElementName)
{
    // Act
    auto name1 = Data::findElementName("Edge1");
    auto name2 = Data::findElementName(";g5v2;SKT;:Had6,V;:G;OFS;:Had6:7,V;:G;OFS;:Had6:7,V;WIR;:"
                                       "Had6:4,V;:G;XTR;:Had6:7,E;:H,E.Face1.Edge2");
    auto name3 = Data::findElementName("An.Example.Assembly.Edge3");
    auto name4 = Data::findElementName(".Edge4");

    // Assert
    EXPECT_STREQ(name1, "Edge1");
    EXPECT_STREQ(name2,
                 ";g5v2;SKT;:Had6,V;:G;OFS;:Had6:7,V;:G;OFS;:Had6:7,V;WIR;:"
                 "Had6:4,V;:G;XTR;:Had6:7,E;:H,E.Face1.Edge2");
    EXPECT_STREQ(name3, "Edge3");
    EXPECT_STREQ(name4, "Edge4");
}

// NOLINTEND(readability-magic-numbers)
