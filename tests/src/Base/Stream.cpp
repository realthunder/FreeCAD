// SPDX-License-Identifier: LGPL-2.1-or-later

/** Base::OutputStream and Base::InputStream, in both of their modes.
 *
 * The pair is one thing, not two: whatever the writer emits the reader has
 * to take back, in binary and in text alike, for every width the class
 * offers. That is the whole contract and it is the only thing tested here.
 *
 * It is worth having because the two halves are written apart, a hundred
 * lines and one class boundary from each other, with a width's write and
 * its read never adjacent. The 8-bit case was broken exactly that way: the
 * writer emitted the character an int8_t stands for while the reader
 * expected the number.
 */

#include <gtest/gtest.h>

#include <limits>
#include <sstream>
#include <string>
#include <vector>

#include <Base/Stream.h>

namespace
{

/// Every value of every width, written and read back in one mode
class StreamRoundTrip: public ::testing::TestWithParam<bool>
{
protected:
    bool binary() const
    {
        return GetParam();
    }
};

}  // namespace

TEST_P(StreamRoundTrip, integersOfEveryWidth)
{
    std::stringstream buffer;
    {
        Base::OutputStream out(buffer, binary());
        out << static_cast<int8_t>(-128) << static_cast<int8_t>(0)
            << static_cast<int8_t>(127);
        out << static_cast<uint8_t>(0) << static_cast<uint8_t>(200)
            << static_cast<uint8_t>(255);
        out << static_cast<int16_t>(-32768) << static_cast<uint16_t>(65535);
        out << static_cast<int32_t>(-2147483647) << static_cast<uint32_t>(4294967295U);
        out << static_cast<int64_t>(-1234567890123LL) << static_cast<uint64_t>(1234567890123ULL);
    }

    Base::InputStream in(buffer, binary());
    int8_t i8 = 0;
    uint8_t u8 = 0;
    in >> i8;
    EXPECT_EQ(i8, -128);
    in >> i8;
    EXPECT_EQ(i8, 0);
    in >> i8;
    EXPECT_EQ(i8, 127);
    in >> u8;
    EXPECT_EQ(u8, 0);
    in >> u8;
    EXPECT_EQ(u8, 200);
    in >> u8;
    EXPECT_EQ(u8, 255);

    int16_t i16 = 0;
    uint16_t u16 = 0;
    in >> i16;
    EXPECT_EQ(i16, -32768);
    in >> u16;
    EXPECT_EQ(u16, 65535);

    int32_t i32 = 0;
    uint32_t u32 = 0;
    in >> i32;
    EXPECT_EQ(i32, -2147483647);
    in >> u32;
    EXPECT_EQ(u32, 4294967295U);

    int64_t i64 = 0;
    uint64_t u64 = 0;
    in >> i64;
    EXPECT_EQ(i64, -1234567890123LL);
    in >> u64;
    EXPECT_EQ(u64, 1234567890123ULL);
}

TEST_P(StreamRoundTrip, boolsAndFloats)
{
    std::stringstream buffer;
    {
        Base::OutputStream out(buffer, binary());
        out << true << false;
        out << 0.5F << -1.25F;
        out << 0.5 << -1.25;
    }

    Base::InputStream in(buffer, binary());
    bool b = false;
    in >> b;
    EXPECT_TRUE(b);
    in >> b;
    EXPECT_FALSE(b);

    float f = 0;
    in >> f;
    EXPECT_FLOAT_EQ(f, 0.5F);
    in >> f;
    EXPECT_FLOAT_EQ(f, -1.25F);

    double d = 0;
    in >> d;
    EXPECT_DOUBLE_EQ(d, 0.5);
    in >> d;
    EXPECT_DOUBLE_EQ(d, -1.25);
}

TEST_P(StreamRoundTrip, aRunOfBytesKeepsItsPlace)
{
    // the shape a field mask and a small enum table take in a doc file: a
    // byte, then values that have to line up behind it
    std::stringstream buffer;
    const std::vector<uint8_t> bytes {1, 2, 250, 0, 7};
    {
        Base::OutputStream out(buffer, binary());
        out << static_cast<uint8_t>(bytes.size());
        for (uint8_t value : bytes) {
            out << value;
        }
        out << static_cast<uint32_t>(0xdeadbeef);
    }

    Base::InputStream in(buffer, binary());
    uint8_t count = 0;
    in >> count;
    ASSERT_EQ(count, bytes.size());
    for (uint8_t expected : bytes) {
        uint8_t value = 0;
        in >> value;
        EXPECT_EQ(value, expected);
    }
    uint32_t tail = 0;
    in >> tail;
    EXPECT_EQ(tail, 0xdeadbeefU);
}

INSTANTIATE_TEST_SUITE_P(BothModes, StreamRoundTrip, ::testing::Values(true, false),
                         [](const ::testing::TestParamInfo<bool>& info) {
                             return info.param ? "binary" : "text";
                         });
