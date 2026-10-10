// SPDX-License-Identifier: LGPL-2.1-or-later

// A picture on a 2D page is drawn from a coarser copy of itself when it comes
// out smaller than it is (docs/HandsOnQueue.md entry 46): the hatch of a cut
// face, lines a pixel wide in a picture read one pixel in five, came out as a
// few lines at full strength where the Qt page shows a fine pale pattern. The
// copies are made here, and a hatch is one tile laid side by side.

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

#include <Gui/Renderer/Page2D.h>
#include <Gui/Renderer/Page2DWire.h>

using Render::Page2D;

namespace
{

std::vector<uint8_t> filled(int w, int h, uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    std::vector<uint8_t> px((size_t)w * h * 4);
    for (size_t i = 0; i < px.size(); i += 4) {
        px[i] = r;
        px[i + 1] = g;
        px[i + 2] = b;
        px[i + 3] = a;
    }
    return px;
}

void put(std::vector<uint8_t>& px, int w, int x, int y, uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    uint8_t* p = px.data() + ((size_t)y * w + x) * 4;
    p[0] = r;
    p[1] = g;
    p[2] = b;
    p[3] = a;
}

double meanAlpha(const std::vector<uint8_t>& px)
{
    double sum = 0.0;
    for (size_t i = 3; i < px.size(); i += 4) {
        sum += px[i];
    }
    return sum / (px.size() / 4);
}

}  // namespace

TEST(Page2DImage, theCopiesHalveDownToOnePixel)
{
    const auto px = filled(8, 4, 10, 20, 30, 255);
    const auto levels = Page2D::coarserImages(px.data(), 8, 4);
    ASSERT_EQ(levels.size(), 3u);
    EXPECT_EQ(levels[0].size(), 4u * 2u * 4u);
    EXPECT_EQ(levels[1].size(), 2u * 1u * 4u);
    EXPECT_EQ(levels[2].size(), 1u * 1u * 4u);
    // the sizes a texture's levels have: halved and rounded down, never under one
    const auto odd = filled(5, 3, 1, 2, 3, 255);
    const auto oddLevels = Page2D::coarserImages(odd.data(), 5, 3);
    ASSERT_EQ(oddLevels.size(), 2u);
    EXPECT_EQ(oddLevels[0].size(), 2u * 1u * 4u);
    EXPECT_EQ(oddLevels[1].size(), 1u * 1u * 4u);
    EXPECT_TRUE(Page2D::coarserImages(px.data(), 1, 1).empty());
    EXPECT_TRUE(Page2D::coarserImages(nullptr, 8, 4).empty());
}

TEST(Page2DImage, aPictureOfOneColourStaysThatColour)
{
    for (int w : {1, 2, 5, 7, 64, 100}) {
        const auto px = filled(w, 9, 200, 100, 50, 128);
        for (const auto& level : Page2D::coarserImages(px.data(), (uint16_t)w, 9)) {
            for (size_t i = 0; i < level.size(); i += 4) {
                ASSERT_EQ(level[i], 200) << "width " << w;
                ASSERT_EQ(level[i + 1], 100);
                ASSERT_EQ(level[i + 2], 50);
                ASSERT_EQ(level[i + 3], 128);
            }
        }
    }
}

TEST(Page2DImage, thinLinesGetPalerNotFewer)
{
    // the hatch of entry 46 in small: lines one pixel wide, every sixteenth
    // column, on nothing
    const int n = 64;
    auto px = filled(n, n, 0, 0, 0, 0);
    for (int x = 0; x < n; x += 16) {
        for (int y = 0; y < n; ++y) {
            put(px, n, x, y, 0, 255, 0, 255);
        }
    }
    const double ink = meanAlpha(px);
    const auto levels = Page2D::coarserImages(px.data(), n, n);
    ASSERT_EQ(levels.size(), 6u);
    int size = n;
    for (size_t l = 0; l < levels.size(); ++l) {
        size /= 2;
        // as much ink at every size
        EXPECT_NEAR(meanAlpha(levels[l]), ink, 1.0) << "level " << l + 1;
        // and every line still there while the columns can tell them apart
        if (size >= 8) {
            int lines = 0;
            for (int x = 0; x < size; ++x) {
                lines += levels[l][(size_t)x * 4 + 3] > 0 ? 1 : 0;
            }
            EXPECT_EQ(lines, 4) << "level " << l + 1;
        }
    }
    // a line in the copy four times smaller: a quarter as strong, as green
    const auto& quarter = levels[1];
    EXPECT_NEAR(quarter[3], 64, 1);
    EXPECT_EQ(quarter[0], 0);
    EXPECT_EQ(quarter[1], 255);
    EXPECT_EQ(quarter[2], 0);
}

TEST(Page2DImage, whatIsTransparentLendsNoColour)
{
    // an opaque red pixel beside a transparent BLACK one: half as opaque, as red
    auto px = filled(2, 1, 0, 0, 0, 0);
    put(px, 2, 0, 0, 255, 0, 0, 255);
    const auto levels = Page2D::coarserImages(px.data(), 2, 1);
    ASSERT_EQ(levels.size(), 1u);
    EXPECT_EQ(levels[0][0], 255);
    EXPECT_EQ(levels[0][1], 0);
    EXPECT_NEAR(levels[0][3], 128, 1);
    // where all of it is transparent the colour left there is kept: a producer
    // puts its ink's colour in the empty pixels so that an enlarged edge blends
    // with that and not with black
    const auto empty = filled(4, 4, 0, 255, 0, 0);
    const auto kept = Page2D::coarserImages(empty.data(), 4, 4);
    ASSERT_EQ(kept.size(), 2u);
    EXPECT_EQ(kept[1][1], 255);
    EXPECT_EQ(kept[1][3], 0);
}

TEST(Page2DImage, anOddSizeIsAveragedOverAllOfIt)
{
    // three pixels into one: each a third of it, the last one too
    std::vector<uint8_t> px = filled(3, 1, 255, 255, 255, 0);
    put(px, 3, 2, 0, 255, 255, 255, 255);
    const auto levels = Page2D::coarserImages(px.data(), 3, 1);
    ASSERT_EQ(levels.size(), 1u);
    EXPECT_NEAR(levels[0][3], 85, 1);
    // five into two: 2.5 each, the middle pixel shared
    std::vector<uint8_t> five = filled(5, 1, 255, 255, 255, 0);
    put(five, 5, 2, 0, 255, 255, 255, 255);
    const auto two = Page2D::coarserImages(five.data(), 5, 1);
    ASSERT_EQ(two.size(), 2u);
    EXPECT_NEAR(two[0][3], 51, 1);
    EXPECT_NEAR(two[0][7], 51, 1);
}

TEST(Page2DImage, aTiledFillIsBoundedByThePathItFills)
{
    Page2D::Recorder rec;
    rec.beginPath();
    rec.moveTo(100.0f, 200.0f);
    rec.lineTo(300.0f, 200.0f);
    rec.lineTo(300.0f, 500.0f);
    rec.closePath();
    // the tile's own rect is far from the face: one copy of the pattern
    rec.fillImage(7, -1000.0f, -1000.0f, 64.0f, 64.0f, 0.5f, true);
    EXPECT_FALSE(rec.empty());
    float box[4] = {0, 0, 0, 0};
    ASSERT_TRUE(Page2D::opsBounds(rec.bytes(), box));
    EXPECT_FLOAT_EQ(box[0], 100.0f);
    EXPECT_FLOAT_EQ(box[1], 200.0f);
    EXPECT_FLOAT_EQ(box[2], 300.0f);
    EXPECT_FLOAT_EQ(box[3], 500.0f);
    // what follows the op is still read: the op has the size it says it has
    rec.beginPath();
    rec.moveTo(900.0f, 900.0f);
    rec.lineTo(950.0f, 950.0f);
    rec.stroke(0x000000ff, 1.0f);
    ASSERT_TRUE(Page2D::opsBounds(rec.bytes(), box));
    EXPECT_FLOAT_EQ(box[2], 950.0f);
    EXPECT_FLOAT_EQ(box[3], 950.0f);
}

TEST(Page2DImage, aPageWithATiledFillIsNotAVersionTwoPage)
{
    // a reader of version 2 stops at an op it does not know
    EXPECT_GE(Render::pageDumpVersion(), 3u);
}

TEST(Page2DImage, anImageIsKeptWithOrWithoutItsCoarserCopies)
{
    Page2D page;
    const auto px = filled(4, 4, 1, 2, 3, 255);
    page.setImage(1, 4, 4, px.data(), true);
    page.setImage(2, 4, 4, px.data(), false, false);
    uint16_t w = 0, h = 0;
    bool repeat = false;
    const std::vector<uint8_t>* pixels = nullptr;
    ASSERT_TRUE(page.imageInfo(1, w, h, repeat, &pixels));
    EXPECT_TRUE(repeat);
    ASSERT_TRUE(page.imageInfo(2, w, h, repeat, &pixels));
    EXPECT_FALSE(repeat);
    ASSERT_NE(pixels, nullptr);
    EXPECT_EQ(pixels->size(), px.size());
}
