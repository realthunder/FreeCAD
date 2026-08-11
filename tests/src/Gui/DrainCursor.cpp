// SPDX-License-Identifier: LGPL-2.1-or-later

/** The deferred view-provider drain's walk over a live document.
 *
 * The drain (docs/DocumentLoad.md §13) runs in budgeted slices across
 * event-loop turns, so the document is live between them: with the window
 * already open, the user can delete or create objects while half the view
 * providers are still parked. Gui::DrainCursor is what makes the walk
 * survive that, and these tests are the reason it is a class instead of an
 * index -- an index into the document's object array steps over an object
 * whenever an earlier one is deleted, and that object is then never handed
 * to the phase that owed it work.
 *
 * The cursor is header-only over App, so this suite needs no GUI, no
 * window and no view provider.
 */

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include <App/Application.h>
#include <App/Document.h>
#include <App/DocumentObject.h>
#include <Gui/DrainCursor.h>

#include <src/App/InitApplication.h>

namespace
{

class DrainCursorTest: public ::testing::Test
{
protected:
    static void SetUpTestSuite()
    {
        tests::initApplication();
    }

    void SetUp() override
    {
        _name = App::GetApplication().getUniqueDocumentName("drain");
        _doc = App::GetApplication().newDocument(_name.c_str(), "test");
        for (int i = 0; i < 5; ++i) {
            _doc->addObject("App::FeatureTest", name(i).c_str());
        }
    }

    void TearDown() override
    {
        App::GetApplication().closeDocument(_name.c_str());
    }

    static std::string name(int i)
    {
        return "obj" + std::to_string(i);
    }
    App::DocumentObject* obj(int i) const
    {
        return _doc->getObject(name(i).c_str());
    }
    void remove(int i)
    {
        _doc->removeObject(name(i).c_str());
    }
    /// Everything the cursor still has to give, in order.
    std::vector<std::string> drain(Gui::DrainCursor& cursor)
    {
        std::vector<std::string> seen;
        while (auto o = cursor.next(_doc)) {
            seen.emplace_back(o->getNameInDocument());
        }
        return seen;
    }

    App::Document* _doc {nullptr};
    std::string _name;
};

TEST_F(DrainCursorTest, WalksEverySnapshottedObjectOnce)
{
    Gui::DrainCursor cursor;
    EXPECT_FALSE(cursor.ready());
    cursor.snapshot(_doc);
    EXPECT_TRUE(cursor.ready());
    EXPECT_FALSE(cursor.done());
    EXPECT_EQ(cursor.size(), 5u);

    EXPECT_EQ(drain(cursor), (std::vector<std::string> {"obj0", "obj1", "obj2", "obj3", "obj4"}));
    EXPECT_TRUE(cursor.done());
    EXPECT_EQ(cursor.next(_doc), nullptr);
}

/** The regression this class exists for.
 *
 * Deleting an object the walk has not reached must cost that object its
 * turn and nothing else. An index into getObjects() would have every later
 * object shift down one, so the walk skips the deleted object's successor
 * -- which, in phase three, is a view provider left with Gui::isRestoring
 * set and no mode switch: an object that loaded and will not show.
 */
TEST_F(DrainCursorTest, DeletingAnObjectDoesNotSkipItsSuccessor)
{
    Gui::DrainCursor cursor;
    cursor.snapshot(_doc);

    ASSERT_EQ(cursor.next(_doc), obj(0));
    remove(1);

    EXPECT_EQ(drain(cursor), (std::vector<std::string> {"obj2", "obj3", "obj4"}));
    EXPECT_TRUE(cursor.done());
}

TEST_F(DrainCursorTest, DeletingTheObjectAboutToBeVisited)
{
    Gui::DrainCursor cursor;
    cursor.snapshot(_doc);
    ASSERT_EQ(cursor.next(_doc), obj(0));
    ASSERT_EQ(cursor.next(_doc), obj(1));
    remove(2);
    EXPECT_EQ(cursor.next(_doc), obj(3));
}

TEST_F(DrainCursorTest, DeletingAnAlreadyVisitedObjectChangesNothing)
{
    Gui::DrainCursor cursor;
    cursor.snapshot(_doc);
    ASSERT_EQ(cursor.next(_doc), obj(0));
    ASSERT_EQ(cursor.next(_doc), obj(1));
    remove(0);
    EXPECT_EQ(drain(cursor), (std::vector<std::string> {"obj2", "obj3", "obj4"}));
}

TEST_F(DrainCursorTest, SeveralDeletionsAtOnce)
{
    Gui::DrainCursor cursor;
    cursor.snapshot(_doc);
    ASSERT_EQ(cursor.next(_doc), obj(0));
    remove(1);
    remove(2);
    remove(3);
    EXPECT_EQ(drain(cursor), (std::vector<std::string> {"obj4"}));
}

TEST_F(DrainCursorTest, EveryRemainingObjectDeleted)
{
    Gui::DrainCursor cursor;
    cursor.snapshot(_doc);
    ASSERT_EQ(cursor.next(_doc), obj(0));
    for (int i = 1; i < 5; ++i) {
        remove(i);
    }
    EXPECT_EQ(cursor.next(_doc), nullptr);
    EXPECT_TRUE(cursor.done());
}

/** Objects created after the snapshot get their view provider from
 * slotNewObject() at creation, so the drain must not hand them a second
 * one -- and must not extend its own walk to chase them.
 */
TEST_F(DrainCursorTest, IgnoresObjectsAddedAfterTheSnapshot)
{
    Gui::DrainCursor cursor;
    cursor.snapshot(_doc);
    ASSERT_EQ(cursor.next(_doc), obj(0));

    _doc->addObject("App::FeatureTest", "late");
    _doc->addObject("App::FeatureTest", "later");

    EXPECT_EQ(drain(cursor), (std::vector<std::string> {"obj1", "obj2", "obj3", "obj4"}));
    EXPECT_EQ(cursor.size(), 5u);
}

TEST_F(DrainCursorTest, AddAndDeleteInterleaved)
{
    Gui::DrainCursor cursor;
    cursor.snapshot(_doc);
    ASSERT_EQ(cursor.next(_doc), obj(0));
    _doc->addObject("App::FeatureTest", "late");
    remove(2);
    ASSERT_EQ(cursor.next(_doc), obj(1));
    _doc->removeObject("late");
    remove(4);
    EXPECT_EQ(drain(cursor), (std::vector<std::string> {"obj3"}));
}

/** Phase three walks the same set phase one created, after phase two has
 * run: one snapshot, two passes.
 */
TEST_F(DrainCursorTest, RewindRepeatsTheSameSet)
{
    Gui::DrainCursor cursor;
    cursor.snapshot(_doc);
    auto first = drain(cursor);
    ASSERT_EQ(first.size(), 5u);

    cursor.rewind();
    EXPECT_FALSE(cursor.done());
    EXPECT_EQ(drain(cursor), first);

    // ...and a deletion between the passes is still respected.
    cursor.rewind();
    remove(3);
    EXPECT_EQ(drain(cursor), (std::vector<std::string> {"obj0", "obj1", "obj2", "obj4"}));
}

/** A name freed by a deletion can be handed to a new object. Resolving it
 * yields that live object, which is exactly what a phase should act on --
 * phase three does nothing to a view provider that is not mid-restore.
 */
TEST_F(DrainCursorTest, ReusedNameResolvesToTheLiveObject)
{
    Gui::DrainCursor cursor;
    cursor.snapshot(_doc);
    ASSERT_EQ(cursor.next(_doc), obj(0));

    remove(2);
    auto replacement = _doc->addObject("App::FeatureTest", "obj2");
    ASSERT_NE(replacement, nullptr);
    ASSERT_STREQ(replacement->getNameInDocument(), "obj2");

    ASSERT_EQ(cursor.next(_doc), obj(1));
    EXPECT_EQ(cursor.next(_doc), replacement);
}

TEST_F(DrainCursorTest, EmptyDocumentIsReadyAndDone)
{
    auto name = App::GetApplication().getUniqueDocumentName("drainempty");
    auto empty = App::GetApplication().newDocument(name.c_str(), "test");
    Gui::DrainCursor cursor;
    cursor.snapshot(empty);
    EXPECT_TRUE(cursor.ready());
    EXPECT_TRUE(cursor.done());
    EXPECT_EQ(cursor.size(), 0u);
    EXPECT_EQ(cursor.next(empty), nullptr);
    App::GetApplication().closeDocument(name.c_str());
}

TEST_F(DrainCursorTest, ClearForgetsTheSnapshot)
{
    Gui::DrainCursor cursor;
    cursor.snapshot(_doc);
    ASSERT_TRUE(cursor.ready());
    cursor.clear();
    EXPECT_FALSE(cursor.ready());
    EXPECT_TRUE(cursor.done());
    EXPECT_EQ(cursor.size(), 0u);
    // A second load into the same document takes its own snapshot.
    cursor.snapshot(_doc);
    EXPECT_TRUE(cursor.ready());
    EXPECT_EQ(cursor.size(), 5u);
}

TEST_F(DrainCursorTest, NullDocumentIsSurvivable)
{
    Gui::DrainCursor cursor;
    cursor.snapshot(nullptr);
    EXPECT_TRUE(cursor.ready());
    EXPECT_TRUE(cursor.done());

    cursor.snapshot(_doc);
    EXPECT_EQ(cursor.next(nullptr), nullptr);
    EXPECT_TRUE(cursor.done());
}

/// position() counts names consumed -- including the ones that resolved to
/// nothing -- so a progress line never stalls on deleted objects.
TEST_F(DrainCursorTest, PositionCountsConsumedNames)
{
    Gui::DrainCursor cursor;
    cursor.snapshot(_doc);
    EXPECT_EQ(cursor.position(), 0u);
    cursor.next(_doc);
    EXPECT_EQ(cursor.position(), 1u);
    remove(1);
    remove(2);
    EXPECT_EQ(cursor.next(_doc), obj(3));
    EXPECT_EQ(cursor.position(), 4u);
    EXPECT_LE(cursor.position(), cursor.size());
}

}  // namespace
