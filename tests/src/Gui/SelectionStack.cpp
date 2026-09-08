// SPDX-License-Identifier: LGPL-2.1-or-later

// The selection instance stack (docs/ThinClient.md section 8.4): what
// Gui::Selection() resolves to, and who hears a selection change.
//
// Gui::Selection() used to be one static object. It is now the innermost
// open Gui::SelectionScope's instance, falling back to the room instance
// that the desktop and every panel share. The point of the indirection is
// that a browser client's replayed event can run against that client's own
// selection without any of the ~1500 call sites behind the accessor being
// touched -- so what this file pins down is the accessor, the nesting, the
// per-instance state, and the one thing that must NOT follow the current
// instance: an observer.
//
// No document and no Gui application here. Selection state that needs
// either -- addSelection reaches Application::Instance and the main window
// -- is out of scope; the members exercised below (the preselection text,
// the selection style) are plain per-instance state, which is exactly the
// property under test.

#include <gtest/gtest.h>

#include <App/Application.h>
#include <App/Document.h>
#include <App/DocumentObject.h>
#include <Gui/Selection/Selection.h>

// clang-format off

namespace
{

/// Counts what it is told, and remembers nothing else.
class CountingObserver: public Gui::SelectionObserver
{
public:
    CountingObserver() = default;
    int count = 0;

protected:
    void onSelectionChanged(const Gui::SelectionChanges&) override
    {
        ++count;
    }
};

}  // namespace

class SelectionStackTest: public ::testing::Test
{
protected:
    static void SetUpTestSuite()
    {
        App::Application::Config()["ExeName"] = "SelectionStack_tests_run";
        int argc = 1;
        char exename[] = "SelectionStack_tests_run";
        char* argv[] = {exename, nullptr};
        App::Application::init(argc, argv);
    }

    void SetUp() override
    {
        Gui::SelectionRoom().setPreselectionText(std::string());
        Gui::SelectionRoom().setSelectionStyle(
            Gui::SelectionSingleton::SelectionStyle::NormalSelection);
    }
};

TEST_F(SelectionStackTest, roomIsCurrentWithoutAScope)
{
    // The desktop's whole life: one instance, reached either way.
    EXPECT_EQ(&Gui::Selection(), &Gui::SelectionRoom());
}

TEST_F(SelectionStackTest, aScopeMakesItsInstanceCurrent)
{
    Gui::SelectionSingleton& room = Gui::SelectionRoom();
    Gui::SelectionSingleton mirror;
    ASSERT_NE(&mirror, &room);

    {
        Gui::SelectionScope scope(mirror);
        EXPECT_EQ(&Gui::Selection(), &mirror);
        // The room is still reachable, and is still the room.
        EXPECT_EQ(&Gui::SelectionRoom(), &room);
    }

    EXPECT_EQ(&Gui::Selection(), &room);
}

TEST_F(SelectionStackTest, scopesNestAndUnwindInOrder)
{
    Gui::SelectionSingleton& room = Gui::SelectionRoom();
    Gui::SelectionSingleton outer;
    Gui::SelectionSingleton inner;

    {
        Gui::SelectionScope scopeOuter(outer);
        EXPECT_EQ(&Gui::Selection(), &outer);
        {
            Gui::SelectionScope scopeInner(inner);
            EXPECT_EQ(&Gui::Selection(), &inner);
        }
        // Back to what the inner scope displaced, not to the room.
        EXPECT_EQ(&Gui::Selection(), &outer);
    }

    EXPECT_EQ(&Gui::Selection(), &room);
}

TEST_F(SelectionStackTest, sameInstancePushedTwiceUnwindsTwice)
{
    Gui::SelectionSingleton& room = Gui::SelectionRoom();
    Gui::SelectionSingleton mirror;

    {
        Gui::SelectionScope first(mirror);
        Gui::SelectionScope second(mirror);
        EXPECT_EQ(&Gui::Selection(), &mirror);
    }

    EXPECT_EQ(&Gui::Selection(), &room);
}

TEST_F(SelectionStackTest, stateIsPerInstance)
{
    Gui::SelectionSingleton& room = Gui::SelectionRoom();
    Gui::SelectionSingleton mirror;

    room.setPreselectionText("room");

    {
        Gui::SelectionScope scope(mirror);
        // The mirror starts blank rather than inheriting the room's state,
        // and writing through the accessor writes the mirror.
        EXPECT_EQ(Gui::Selection().getPreselectionText(), std::string());
        Gui::Selection().setPreselectionText("mirror");
        Gui::Selection().setSelectionStyle(
            Gui::SelectionSingleton::SelectionStyle::GreedySelection);
        EXPECT_EQ(Gui::Selection().getPreselectionText(), std::string("mirror"));
        // ... and leaves the room alone.
        EXPECT_EQ(room.getPreselectionText(), std::string("room"));
    }

    EXPECT_EQ(Gui::Selection().getPreselectionText(), std::string("room"));
    EXPECT_EQ(Gui::Selection().getSelectionStyle(),
              Gui::SelectionSingleton::SelectionStyle::NormalSelection);
    EXPECT_EQ(mirror.getPreselectionText(), std::string("mirror"));
    EXPECT_EQ(mirror.getSelectionStyle(),
              Gui::SelectionSingleton::SelectionStyle::GreedySelection);
}

TEST_F(SelectionStackTest, observersHearTheRoomOnly)
{
    Gui::SelectionSingleton& room = Gui::SelectionRoom();
    Gui::SelectionSingleton mirror;

    CountingObserver observer;
    ASSERT_TRUE(observer.isSelectionAttached());

    mirror.signalSelectionChanged(Gui::SelectionChanges());
    EXPECT_EQ(observer.count, 0);

    room.signalSelectionChanged(Gui::SelectionChanges());
    EXPECT_EQ(observer.count, 1);
}

TEST_F(SelectionStackTest, anObserverBuiltInsideAScopeStillHearsTheRoom)
{
    // The trap this closes: a task panel built while a client's event is
    // being replayed would otherwise spend its whole life attached to that
    // client's instance, and go deaf the moment the scope closed.
    Gui::SelectionSingleton& room = Gui::SelectionRoom();
    Gui::SelectionSingleton mirror;

    Gui::SelectionScope scope(mirror);
    CountingObserver observer;
    ASSERT_TRUE(observer.isSelectionAttached());

    mirror.signalSelectionChanged(Gui::SelectionChanges());
    EXPECT_EQ(observer.count, 0);

    room.signalSelectionChanged(Gui::SelectionChanges());
    EXPECT_EQ(observer.count, 1);
}

TEST_F(SelectionStackTest, aDestroyedInstanceLeavesNoSlotBehind)
{
    // Every instance subscribes to App's deleted-object signal. While one
    // instance lived as long as the process that connection never had to be
    // dropped; a mirror's does, and a slot bound to a destroyed instance is
    // a call into freed memory the next time any object goes away. Nothing
    // to assert but the absence of that call, so what this case buys is a
    // crash here rather than in a serving session.
    {
        Gui::SelectionSingleton mirror;
        Gui::SelectionScope scope(mirror);
    }

    auto doc = App::GetApplication().newDocument("SelectionStackDelete");
    ASSERT_NE(doc, nullptr);
    auto obj = doc->addObject("App::FeatureTest", "Subject");
    ASSERT_NE(obj, nullptr);
    doc->removeObject(obj->getNameInDocument());
    App::GetApplication().closeDocument(doc->getName());

    // The room outlived it, and is still the one the accessor finds.
    EXPECT_EQ(&Gui::Selection(), &Gui::SelectionRoom());
}

// clang-format on
