// SPDX-License-Identifier: LGPL-2.1-or-later

// The dashes of a dashed line on a 2D page are worked out for the zoom the
// page is drawn at, by Qt's rules (docs/HandsOnQueue.md entry 36): the
// backend's page cut its dashes once, in page units, so zoomed out they
// closed up until a hidden line, a section line and a view's frame looked
// continuous, where the Qt page keeps dashes that can be told apart.

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include <Gui/Renderer/Page2D.h>
#include <Gui/Renderer/Page2DWire.h>

using Render::Page2D;

namespace
{

// A straight line from 0 to 100 along x
const float kLine[] = {0.0f, 0.0f, 100.0f, 0.0f};

struct Run
{
    float from;
    float to;
};

// The dashes along kLine, as [from, to] in x
std::vector<Run> along(const std::vector<float>& pattern, float unit, float offset, bool cap,
                       float band, bool* thin = nullptr)
{
    std::vector<Run> res;
    for (const auto& run : Page2D::dashRuns(kLine, 2, pattern.data(), (uint32_t)pattern.size(),
                                            unit, offset, cap, band, 1.0f, thin)) {
        EXPECT_EQ(run.size(), 4u);
        res.push_back({run.front(), run[run.size() - 2]});
    }
    return res;
}

void expectRuns(const std::vector<Run>& got, const std::vector<Run>& want)
{
    ASSERT_EQ(got.size(), want.size());
    for (size_t i = 0; i < want.size(); ++i) {
        EXPECT_NEAR(got[i].from, want[i].from, 1.0e-3f) << "dash " << i;
        EXPECT_NEAR(got[i].to, want[i].to, 1.0e-3f) << "dash " << i;
    }
}

}  // namespace

TEST(Page2DDash, aPenAPixelWideOrMoreCountsItsPatternInItsWidth)
{
    bool thin = true;
    // 12 on, 3 off, a pen 2 wide: 24 and 6
    expectRuns(along({12.0f, 3.0f}, 2.0f, 0.0f, false, 1.0f, &thin),
               {{0, 24}, {30, 54}, {60, 84}, {90, 100}});
    EXPECT_FALSE(thin);
}

TEST(Page2DDash, capsLengthenEveryDashByHalfAWidthAtEachEnd)
{
    // as Qt's round and square caps do; clipped at the ends of the line
    expectRuns(along({12.0f, 3.0f}, 2.0f, 0.0f, true, 1.0f),
               {{0, 25}, {29, 55}, {59, 85}, {89, 100}});
}

TEST(Page2DDash, aPenThinnerThanAPixelCountsInPixels)
{
    // The same pattern on a pen a fifth of a pixel wide. Counted in its own
    // width the dashes would be 2.4 long and 0.6 apart -- a continuous line
    // on screen, which is what the backend's page showed zoomed out.
    bool thin = false;
    const auto runs = along({12.0f, 3.0f}, 0.2f, 0.0f, true, 1.0f, &thin);
    EXPECT_TRUE(thin);
    ASSERT_EQ(runs.size(), 7u);
    EXPECT_NEAR(runs[0].to - runs[0].from, 12.0f, 1.0e-3f);  // 12 pixels, no caps
    EXPECT_NEAR(runs[1].from - runs[0].to, 3.0f, 1.0e-3f);
}

TEST(Page2DDash, zoomedInTheSamePenCountsInItsWidthAgain)
{
    // band 8: the pen is 1.6 pixels wide
    bool thin = true;
    const auto runs = along({12.0f, 3.0f}, 0.2f, 0.0f, false, 8.0f, &thin);
    EXPECT_FALSE(thin);
    ASSERT_EQ(runs.size(), 34u);
    EXPECT_NEAR(runs[0].to - runs[0].from, 2.4f, 1.0e-3f);
}

TEST(Page2DDash, aCosmeticPenCountsInPixelsAtAnyZoom)
{
    // unit 0: three pixels on, three off, whatever the band. On the page
    // that is 3 / band.
    for (float band : {0.25f, 1.0f, 4.0f, 16.0f}) {
        bool thin = false;
        const auto runs = along({3.0f, 3.0f}, 0.0f, 0.0f, true, band, &thin);
        EXPECT_TRUE(thin) << band;
        ASSERT_GE(runs.size(), 2u) << band;
        EXPECT_NEAR((runs[0].to - runs[0].from) * band, 3.0f, 1.0e-3f) << band;
        EXPECT_NEAR((runs[1].from - runs[0].to) * band, 3.0f, 1.0e-3f) << band;
    }
}

TEST(Page2DDash, theOffsetIsWhereInThePatternTheLineStarts)
{
    // half way into the first dash
    expectRuns(along({12.0f, 3.0f}, 1.0f, 6.0f, false, 1.0f),
               {{0, 6}, {9, 21}, {24, 36}, {39, 51}, {54, 66}, {69, 81}, {84, 96}, {99, 100}});
    // at the gap: a pattern that starts on a gap is given as its dash
    // first and this offset (TechDraw::LineGenerator::getLinePen)
    const auto runs = along({12.0f, 3.0f}, 1.0f, 12.0f, false, 1.0f);
    ASSERT_FALSE(runs.empty());
    EXPECT_NEAR(runs[0].from, 3.0f, 1.0e-3f);
    EXPECT_NEAR(runs[0].to, 15.0f, 1.0e-3f);
}

TEST(Page2DDash, aPatternOfOddLengthChangesSidesEachTimeRound)
{
    expectRuns(along({30.0f}, 1.0f, 0.0f, false, 1.0f), {{0, 30}, {60, 90}});
}

TEST(Page2DDash, aGapTheCapsCloseJoinsItsDashes)
{
    // dashes of 8 two apart, caps of 1 at each end: nothing left between;
    // the line ends on the last dash's cap, a unit short of its own end
    expectRuns(along({4.0f, 1.0f}, 2.0f, 0.0f, true, 1.0f), {{0, 99}});
}

TEST(Page2DDash, aPatternWithNoLengthIsTheWholeLine)
{
    expectRuns(along({0.0f, 0.0f}, 2.0f, 0.0f, false, 1.0f), {{0, 100}});
}

TEST(Page2DDash, aDashGoesRoundACorner)
{
    const float corner[] = {0.0f, 0.0f, 10.0f, 0.0f, 10.0f, 10.0f};
    const float pattern[] = {15.0f, 3.0f};
    const auto runs = Page2D::dashRuns(corner, 3, pattern, 2, 1.0f, 0.0f, false, 1.0f, 1.0f);
    ASSERT_EQ(runs.size(), 2u);
    // the first: along x, the corner, five up
    ASSERT_EQ(runs[0].size(), 6u);
    EXPECT_NEAR(runs[0][2], 10.0f, 1.0e-3f);
    EXPECT_NEAR(runs[0][3], 0.0f, 1.0e-3f);
    EXPECT_NEAR(runs[0][4], 10.0f, 1.0e-3f);
    EXPECT_NEAR(runs[0][5], 5.0f, 1.0e-3f);
    // the second: the rest of the upright, from 8 up
    ASSERT_EQ(runs[1].size(), 4u);
    EXPECT_NEAR(runs[1][0], 10.0f, 1.0e-3f);
    EXPECT_NEAR(runs[1][1], 8.0f, 1.0e-3f);
    EXPECT_NEAR(runs[1][3], 10.0f, 1.0e-3f);
}

TEST(Page2DDash, theOpIsPartOfTheItemsBounds)
{
    Page2D::Recorder rec;
    const float xy[] = {5.0f, 7.0f, 40.0f, 7.0f, 40.0f, 30.0f};
    const float pattern[] = {12.0f, 3.0f};
    rec.beginPath();
    rec.dashedPolyline(xy, 3, pattern, 2, 1.0f, 0.0f, true);
    rec.stroke(0x000000ff, 1.0f);
    float box[4] = {0, 0, 0, 0};
    ASSERT_TRUE(Page2D::opsBounds(rec.bytes(), box));
    EXPECT_LE(box[0], 5.0f);
    EXPECT_LE(box[1], 7.0f);
    EXPECT_GE(box[2], 40.0f);
    EXPECT_GE(box[3], 30.0f);
    EXPECT_LT(box[2], 45.0f);
}

TEST(Page2DDash, aPageWithTheOpIsNotAVersionOnePage)
{
    // a reader of version 1 stops at an op it does not know
    EXPECT_GE(Render::pageDumpVersion(), 2u);
}

TEST(Page2DDash, theTrimPullsInDashEndsButNotTheLinesOwn)
{
    // what the replay asks for, so that vg's anti-aliased ends leave the gap
    const float pattern[] = {12.0f, 3.0f};
    const auto runs = Page2D::dashRuns(kLine, 2, pattern, 2, 2.0f, 0.0f, false, 1.0f, 1.0f,
                                       nullptr, 0.5f);
    ASSERT_EQ(runs.size(), 4u);
    EXPECT_NEAR(runs[0][0], 0.0f, 1.0e-3f);   // the line's start stays
    EXPECT_NEAR(runs[0][2], 23.5f, 1.0e-3f);
    EXPECT_NEAR(runs[1][0], 30.5f, 1.0e-3f);
    EXPECT_NEAR(runs[3][2], 100.0f, 1.0e-3f); // and its end
    // a dot is not trimmed away: a quarter of it at most
    const float dots[] = {1.0f, 3.0f};
    const auto small = Page2D::dashRuns(kLine, 2, dots, 2, 1.0f, 0.0f, false, 1.0f, 1.0f,
                                        nullptr, 0.5f);
    ASSERT_GE(small.size(), 2u);
    EXPECT_NEAR(small[1][2] - small[1][0], 0.5f, 1.0e-3f);
}
