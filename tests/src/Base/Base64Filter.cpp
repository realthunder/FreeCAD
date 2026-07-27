// SPDX-License-Identifier: LGPL-2.1-or-later

#include "gtest/gtest.h"

#include <sstream>
#include <string>

#include "Base/Base64.h"
#include "Base/Base64Filter.h"

// The streaming filters, as opposed to the base64_encode/base64_decode functions
// tested in Base64.cpp. These are what the document writer puts content through
// when it is asked for pure XML, so a length that does not fill the last group
// is the interesting case: the filter has to hold those bytes over and encode
// them exactly once.

namespace
{
std::string encodeThroughFilter(const std::string& data, std::size_t lineSize)
{
    std::ostringstream out;
    {
        auto stream = Base::create_base64_encoder(out, lineSize);
        *stream << data;
    }
    return out.str();
}

std::string stripWhitespace(const std::string& str)
{
    std::string result;
    for (char chr : str) {
        if (chr != '\n' && chr != '\r') {
            result += chr;
        }
    }
    return result;
}
}  // namespace

TEST(Base64Filter, encodeMatchesReference)
{
    // Every remainder of the three-byte group, and enough length to cross the
    // filter's line and buffer handling.
    for (std::size_t length = 0; length < 200; ++length) {
        std::string data;
        for (std::size_t i = 0; i < length; ++i) {
            data += static_cast<char>(i % 251);
        }
        const std::string expected =
            Base::base64_encode(reinterpret_cast<const unsigned char*>(data.data()), data.size());

        EXPECT_EQ(stripWhitespace(encodeThroughFilter(data, 80)), expected)
            << "length " << length;
    }
}

TEST(Base64Filter, roundTrip)
{
    for (std::size_t length = 0; length < 200; ++length) {
        std::string data;
        for (std::size_t i = 0; i < length; ++i) {
            data += static_cast<char>(i % 251);
        }

        std::istringstream in(encodeThroughFilter(data, 80));
        auto stream = Base::create_base64_decoder(in);
        std::ostringstream decoded;
        decoded << stream->rdbuf();

        EXPECT_EQ(decoded.str(), data) << "length " << length;
    }
}

TEST(Base64Filter, encodeUnsegmented)
{
    // line_size 0 disables segmenting; the group handling must not change.
    for (std::size_t length = 1; length < 10; ++length) {
        std::string data(length, 'x');
        const std::string expected =
            Base::base64_encode(reinterpret_cast<const unsigned char*>(data.data()), data.size());

        EXPECT_EQ(encodeThroughFilter(data, 0), expected) << "length " << length;
    }
}

TEST(Base64Filter, encodeInSmallWrites)
{
    // Content arrives one character at a time, so nearly every write leaves a
    // partial group behind.
    std::string data;
    for (std::size_t i = 0; i < 64; ++i) {
        data += static_cast<char>(i);
    }

    std::ostringstream out;
    {
        auto stream = Base::create_base64_encoder(out, 80);
        for (char chr : data) {
            stream->write(&chr, 1);
        }
    }

    const std::string expected =
        Base::base64_encode(reinterpret_cast<const unsigned char*>(data.data()), data.size());
    EXPECT_EQ(stripWhitespace(out.str()), expected);
}
