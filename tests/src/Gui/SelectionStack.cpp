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

#include <memory>
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

/// The same, but attached to whichever instance was current when it was
/// built -- what an edit mode's own observer does.
class CurrentObserver: public Gui::SelectionObserver
{
public:
    CurrentObserver()
        : Gui::SelectionObserver(false)
    {
        attachSelectionToCurrent();
    }
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
        Gui::SelectionSingleton::setAmbient(nullptr);
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

TEST_F(SelectionStackTest, anObserverBuiltInsideAViewsScopeBelongsToThatView)
{
    // Restated 2026-10-06 (docs/TaskPanelPerView.md sec 12). This case
    // pinned the opposite -- "an observer built inside a scope still hears
    // the room" -- from when the room was every panel's and a scope was
    // one replayed event. A task dialog now belongs to the view it is
    // opened for, and what is built while that view's instance is current
    // is built for that view: an edit is started inside its view's scope
    // exactly so that its task boxes hear what that view picks.
    Gui::SelectionSingleton& room = Gui::SelectionRoom();
    Gui::SelectionSingleton mirror;
    {
        Gui::SelectionScope scope(mirror);
        CountingObserver observer;
        ASSERT_TRUE(observer.isSelectionAttached());
        EXPECT_FALSE(observer.isFollowingSelection());

        mirror.signalSelectionChanged(Gui::SelectionChanges());
        EXPECT_EQ(observer.count, 1);

        room.signalSelectionChanged(Gui::SelectionChanges());
        EXPECT_EQ(observer.count, 1);
    }
}

TEST_F(SelectionStackTest, anObserverBuiltUnderTheRoomsScopeFollows)
{
    // A scope on the room says "no view's own": a desktop view that
    // shares the room opens one around its events, and an observer built
    // there is nobody's in particular.
    Gui::SelectionSingleton& room = Gui::SelectionRoom();
    Gui::SelectionScope scope(room);
    CountingObserver observer;
    EXPECT_TRUE(observer.isFollowingSelection());
}

TEST_F(SelectionStackTest, aFollowerMovesWithTheActiveViewsInstance)
{
    Gui::SelectionSingleton& room = Gui::SelectionRoom();
    Gui::SelectionSingleton own;

    CountingObserver follower;
    ASSERT_TRUE(follower.isFollowingSelection());
    CurrentObserver bound;  // to the room, and it stays there

    // Another view becomes the active one, with an instance of its own
    Gui::SelectionSingleton::setAmbient(&own);
    EXPECT_EQ(&Gui::Selection(), &own);
    // Told to read the selection again: no preselection, then all of it
    EXPECT_EQ(follower.count, 2);
    EXPECT_EQ(bound.count, 0);

    own.signalSelectionChanged(Gui::SelectionChanges());
    EXPECT_EQ(follower.count, 3);
    EXPECT_EQ(bound.count, 0);

    room.signalSelectionChanged(Gui::SelectionChanges());
    EXPECT_EQ(follower.count, 3);
    EXPECT_EQ(bound.count, 1);

    // A scope still wins over the active view, and closing it returns there
    {
        Gui::SelectionScope scope(room);
        EXPECT_EQ(&Gui::Selection(), &room);
    }
    EXPECT_EQ(&Gui::Selection(), &own);

    Gui::SelectionSingleton::setAmbient(nullptr);
    EXPECT_EQ(&Gui::Selection(), &room);
    EXPECT_EQ(follower.count, 5);
}

TEST_F(SelectionStackTest, theActiveViewChangesUnderAnOpenScope)
{
    // A view is activated from inside the handling of an event: what is
    // current stays the scope's, and what it returns to is the new view's.
    Gui::SelectionSingleton& room = Gui::SelectionRoom();
    Gui::SelectionSingleton scoped;
    Gui::SelectionSingleton own;
    {
        Gui::SelectionScope scope(scoped);
        Gui::SelectionSingleton::setAmbient(&own);
        EXPECT_EQ(&Gui::Selection(), &scoped);
    }
    EXPECT_EQ(&Gui::Selection(), &own);
    Gui::SelectionSingleton::setAmbient(nullptr);
    EXPECT_EQ(&Gui::Selection(), &room);
}

TEST_F(SelectionStackTest, anInstanceThatEndsHandsItsObserversBack)
{
    // A view's instance ends with its edit, and a task box of that edit
    // is deleted a little later: it must not be left pointing at it.
    Gui::SelectionSingleton& room = Gui::SelectionRoom();
    auto own = std::make_unique<Gui::SelectionSingleton>();
    std::unique_ptr<CountingObserver> observer;
    {
        Gui::SelectionScope scope(*own);
        observer = std::make_unique<CountingObserver>();
    }
    ASSERT_FALSE(observer->isFollowingSelection());

    own.reset();
    EXPECT_TRUE(observer->isSelectionAttached());
    EXPECT_TRUE(observer->isFollowingSelection());

    const int before = observer->count;
    room.signalSelectionChanged(Gui::SelectionChanges());
    EXPECT_EQ(observer->count, before + 1);
}

TEST_F(SelectionStackTest, theActiveViewsInstanceEndsAndTheRoomIsCurrent)
{
    Gui::SelectionSingleton& room = Gui::SelectionRoom();
    CountingObserver follower;
    {
        Gui::SelectionSingleton own;
        Gui::SelectionSingleton::setAmbient(&own);
        ASSERT_EQ(&Gui::Selection(), &own);
    }
    EXPECT_EQ(&Gui::Selection(), &room);
    const int before = follower.count;
    room.signalSelectionChanged(Gui::SelectionChanges());
    EXPECT_EQ(follower.count, before + 1);
}

TEST_F(SelectionStackTest, anInstanceRetiredInsideItsScopeOutlivesTheScope)
{
    // Leaving an edit gives the view's instance up from inside the scope
    // that made it current.
    Gui::SelectionSingleton& room = Gui::SelectionRoom();
    auto own = std::make_unique<Gui::SelectionSingleton>();
    Gui::SelectionSingleton* raw = own.get();
    {
        Gui::SelectionScope scope(*raw);
        Gui::SelectionSingleton::retire(std::move(own));
        ASSERT_EQ(&Gui::Selection(), raw);
        // Still whole: this would be a write into freed memory otherwise
        raw->setPreselectionText("still here");
        EXPECT_EQ(raw->getPreselectionText(), "still here");
    }
    EXPECT_EQ(&Gui::Selection(), &room);
}

TEST_F(SelectionStackTest, anAdoptedObserverAttachesAtItsHome)
{
    // What showing a task dialog does to the observers found in it
    Gui::SelectionSingleton& room = Gui::SelectionRoom();
    Gui::SelectionSingleton own;

    // One that only listens while a button of its box is down
    class LateObserver: public Gui::SelectionObserver
    {
    public:
        LateObserver()
            : Gui::SelectionObserver(false)
        {}
        int count = 0;

    protected:
        void onSelectionChanged(const Gui::SelectionChanges&) override
        {
            ++count;
        }
    };
    LateObserver late;
    late.adoptSelection(own);
    EXPECT_FALSE(late.isSelectionAttached());
    late.attachSelection();
    own.signalSelectionChanged(Gui::SelectionChanges());
    room.signalSelectionChanged(Gui::SelectionChanges());
    EXPECT_EQ(late.count, 1);

    // One that is listening already moves, and is told to read again
    CountingObserver listening;
    listening.adoptSelection(own);
    EXPECT_FALSE(listening.isFollowingSelection());
    EXPECT_EQ(listening.count, 2);
    own.signalSelectionChanged(Gui::SelectionChanges());
    room.signalSelectionChanged(Gui::SelectionChanges());
    EXPECT_EQ(listening.count, 3);
}

TEST_F(SelectionStackTest, anObserverOfTheCurrentInstanceFollowsTheScopeItWasBuiltIn)
{
    // The exception to the rule above, and the whole reason there is a
    // choice: an edit mode's own observer is what colours the geometry it
    // is editing, so it has to hear the instance that edit's picks go
    // into. It attaches to the current one deliberately, and -- this is
    // the part worth pinning -- it keeps hearing that instance after the
    // scope closes, because the session outlives any single replayed
    // event. docs/ThinClient.md section 8.4.
    Gui::SelectionSingleton& room = Gui::SelectionRoom();
    Gui::SelectionSingleton mirror;

    CurrentObserver observer;
    {
        Gui::SelectionScope scope(mirror);
        CurrentObserver inEdit;
        ASSERT_TRUE(inEdit.isSelectionAttached());

        mirror.signalSelectionChanged(Gui::SelectionChanges());
        EXPECT_EQ(inEdit.count, 1);
        // The one built outside is still the room's.
        EXPECT_EQ(observer.count, 0);

        room.signalSelectionChanged(Gui::SelectionChanges());
        EXPECT_EQ(inEdit.count, 1);
        EXPECT_EQ(observer.count, 1);

        // Out of the scope's extent but still in the session: this is
        // what a queued call or a second event looks like.
        {
            Gui::SelectionScope reopened(mirror);
            mirror.signalSelectionChanged(Gui::SelectionChanges());
        }
        EXPECT_EQ(inEdit.count, 2);
    }
    // And after the scope has gone for good, still that instance -- the
    // observer must not fall back to the room when its session ends,
    // because ending the session is what detaches it.
    CurrentObserver survivor;
    {
        Gui::SelectionScope scope(mirror);
        CurrentObserver bound;
        mirror.signalSelectionChanged(Gui::SelectionChanges());
        EXPECT_EQ(bound.count, 1);
    }
    room.signalSelectionChanged(Gui::SelectionChanges());
    EXPECT_EQ(survivor.count, 1);
}

TEST_F(SelectionStackTest, aDetachedObserverHearsNeither)
{
    Gui::SelectionSingleton& room = Gui::SelectionRoom();
    Gui::SelectionSingleton mirror;

    Gui::SelectionScope scope(mirror);
    CurrentObserver observer;
    observer.detachSelection();
    EXPECT_FALSE(observer.isSelectionAttached());

    mirror.signalSelectionChanged(Gui::SelectionChanges());
    room.signalSelectionChanged(Gui::SelectionChanges());
    EXPECT_EQ(observer.count, 0);
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
