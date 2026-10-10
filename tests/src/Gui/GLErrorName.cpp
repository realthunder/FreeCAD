// SPDX-License-Identifier: LGPL-2.1-or-later

#include <gtest/gtest.h>

#include <cstring>

#include <Gui/Renderer/GLErrorName.h>

// The renderer's GL error check named the error out of a table of four,
// indexed with min(code - 0x0500, 4): three real codes and one "Unknown",
// and index 4 for everything from GL_STACK_UNDERFLOW up -- one past the end.
// Reporting such an error crashed the program instead.
TEST(GLErrorName, everyCodeHasAName)
{
    EXPECT_STREQ(Render::glErrorName(0x0500), "GL_INVALID_ENUM");
    EXPECT_STREQ(Render::glErrorName(0x0501), "GL_INVALID_VALUE");
    EXPECT_STREQ(Render::glErrorName(0x0502), "GL_INVALID_OPERATION");
    // the ones that indexed past the table
    EXPECT_STREQ(Render::glErrorName(0x0503), "GL_STACK_OVERFLOW");
    EXPECT_STREQ(Render::glErrorName(0x0504), "GL_STACK_UNDERFLOW");
    EXPECT_STREQ(Render::glErrorName(0x0505), "GL_OUT_OF_MEMORY");
    EXPECT_STREQ(Render::glErrorName(0x0506), "GL_INVALID_FRAMEBUFFER_OPERATION");
    EXPECT_STREQ(Render::glErrorName(0x0507), "GL_CONTEXT_LOST");
}

TEST(GLErrorName, anyOtherValueIsAnsweredToo)
{
    // below the range (the old index wrapped to a huge unsigned), above it,
    // and whatever a driver might invent
    for (unsigned int code : {0x0001U, 0x04ffU, 0x0508U, 0x8000U, 0xffffffffU}) {
        const char* name = Render::glErrorName(code);
        ASSERT_NE(name, nullptr);
        EXPECT_GT(std::strlen(name), 0U);
    }
    EXPECT_STREQ(Render::glErrorName(0), "GL_NO_ERROR");
}
