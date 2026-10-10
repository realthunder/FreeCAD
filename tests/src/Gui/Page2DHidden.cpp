// SPDX-License-Identifier: LGPL-2.1-or-later

// An item of a 2D page can be left out of the drawing without being taken
// off the page: what a TechDraw page drawn by the backend does with an edge
// while the same edge in the highlight's colour is laid over it, so that the
// line under a highlight never shows through the highlight's soft edge
// ("make sure the highlight shows the same width"). It is the host's own
// state of its page: the content, the order and the wire journal do not
// know of it, and defining the item again shows it.

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

#include <Gui/Renderer/Page2D.h>

using Render::Page2D;

namespace
{

Page2D::Recorder line(float y)
{
    Page2D::Recorder rec;
    rec.beginPath();
    rec.moveTo(0.0f, y);
    rec.lineTo(10.0f, y);
    rec.stroke(0x000000ff, 3.5f);
    return rec;
}

}  // namespace

TEST(Page2DHidden, anItemThatIsNotThereCannotBeHidden)
{
    Page2D page;
    EXPECT_FALSE(page.setItemHidden(7, true));
    EXPECT_FALSE(page.itemHidden(7));
    EXPECT_FALSE(page.hasItem(7));
}

TEST(Page2DHidden, hidingKeepsTheItemAndItsContent)
{
    Page2D page;
    page.setItem(7, Page2D::Kind::Edge, 1, line(2.0f));
    Page2D::Kind kind = Page2D::Kind::Face;
    uint32_t layer = 0;
    const std::vector<uint8_t>* ops = nullptr;
    ASSERT_TRUE(page.itemInfo(7, kind, layer, &ops));
    const std::vector<uint8_t> before = *ops;
    ASSERT_FALSE(before.empty());

    EXPECT_TRUE(page.setItemHidden(7, true));
    EXPECT_TRUE(page.itemHidden(7));
    EXPECT_TRUE(page.hasItem(7));
    ASSERT_TRUE(page.itemInfo(7, kind, layer, &ops));
    EXPECT_EQ(kind, Page2D::Kind::Edge);
    EXPECT_EQ(layer, 1u);
    EXPECT_EQ(*ops, before);

    EXPECT_TRUE(page.setItemHidden(7, false));
    EXPECT_FALSE(page.itemHidden(7));
}

TEST(Page2DHidden, hidingIsNotJournaled)
{
    // a mirror of the page (the wire's consumer) draws the item: nothing
    // would tell it when the highlight ends
    Page2D page;
    page.setItem(7, Page2D::Kind::Edge, 1, line(2.0f));
    Page2D::Changes changes;
    page.takeChanges(changes);
    ASSERT_EQ(changes.items.size(), 1u);

    page.setItemHidden(7, true);
    Page2D::Changes after;
    page.takeChanges(after);
    EXPECT_TRUE(after.empty());
}

TEST(Page2DHidden, definingTheItemAgainShowsIt)
{
    // what was hidden was the content the new one replaces
    Page2D page;
    page.setItem(7, Page2D::Kind::Edge, 1, line(2.0f));
    page.setItemHidden(7, true);
    page.setItem(7, Page2D::Kind::Edge, 1, line(4.0f));
    EXPECT_FALSE(page.itemHidden(7));
}
