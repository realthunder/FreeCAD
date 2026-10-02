/***************************************************************************
 *   Copyright (c) 2026 FreeCAD contributors                               *
 *                                                                         *
 *   This file is part of the FreeCAD CAx development system.              *
 *                                                                         *
 *   This library is free software; you can redistribute it and/or         *
 *   modify it under the terms of the GNU Library General Public           *
 *   License as published by the Free Software Foundation; either          *
 *   version 2 of the License, or (at your option) any later version.      *
 *                                                                         *
 *   This library  is distributed in the hope that it will be useful,      *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU Library General Public License for more details.                  *
 *                                                                         *
 *   You should have received a copy of the GNU Library General Public     *
 *   License along with this library; see the file COPYING.LIB. If not,    *
 *   write to the Free Software Foundation, Inc., 59 Temple Place,         *
 *   Suite 330, Boston, MA  02111-1307, USA                                *
 *                                                                         *
 ***************************************************************************/

/// The on-view parameters an edit mode opens in a view with no widget
/// (docs/ThinClient.md sec 8.7).
///
/// The claim under test is the one the whole design rests on: the entry
/// box is the same QuantitySpinBox on both tiers, and on a mirror it is
/// simply never shown -- a text model driven by replayed key events, whose
/// text is streamed to the client instead of painted. If that holds, the
/// parsing, the units and the rule for which keys a box claims have one
/// implementation and the browser cannot drift from the desktop.
///
/// So there is a QApplication here, and there is not a window: the widgets
/// are built, typed into and read back without ever being shown, which is
/// exactly what a serving process does with them.

#include <gtest/gtest.h>

#include <array>
#include <memory>

#include <QApplication>
#include <QEvent>
#include <QFont>
#include <QFontDatabase>
#include <QKeyEvent>
#include <QObject>
#include <QString>

#include <Inventor/SoDB.h>
#include <Inventor/SoInteraction.h>
#include <Inventor/events/SoKeyboardEvent.h>
#include <Inventor/nodes/SoCube.h>
#include <Inventor/nodes/SoEventCallback.h>
#include <Inventor/nodes/SoSeparator.h>

#include <App/Application.h>
#include <Base/Placement.h>

#include <Gui/EditableDatumLabel.h>
#include <Gui/Inventor/SoAutoZoomTranslation.h>
#include <Gui/Inventor/SoToggleMarker.h>
#include <Gui/MirrorViewer.h>
#include <Gui/PatternWidgets.h>
#include <Gui/SoDatumLabel.h>
#include <Gui/ViewerContext.h>

namespace
{

constexpr float kEye = 60.0F;
constexpr float kHeightAngle = 0.7853981634F;   // 45 degrees

Gui::MirrorViewer::Camera stateCamera(short width, short height)
{
    Gui::MirrorViewer::Camera camera;
    camera.perspective = true;
    camera.position.setValue(0, 0, kEye);
    camera.orientation = SbRotation::identity();
    camera.heightOrAngle = kHeightAngle;
    camera.nearDistance = 10.0F;
    camera.farDistance = 200.0F;
    camera.aspectRatio = float(width) / float(height);
    camera.sizePixels.setValue(width, height);
    camera.pickRadius = 5.0F;
    return camera;
}

/// One key frame as the wire carries it (sec 8.5): an X11 keysym, and the
/// character it produced in the slot a wheel would otherwise use.
Gui::MirrorViewer::Input keyPress(int keysym, int character)
{
    Gui::MirrorViewer::Input input;
    input.kind = Gui::MirrorViewer::Input::KeyDown;
    input.code = keysym;
    input.delta = character;
    input.time = 2.0;
    return input;
}

class OnViewParameterTest: public ::testing::Test
{
protected:
    static void SetUpTestSuite()
    {
        if (!SoDB::isInitialized()) {
            SoDB::init();
            SoInteraction::init();
        }
        // The label's own nodes: without a type no action reaches them, and
        // a click is an action like any other
        if (Gui::SoDatumLabel::getClassTypeId() == SoType::badType()) {
            Gui::SoDatumLabel::initClass();
            Gui::SoAutoZoomTranslation::initClass();
        }
        if (Gui::SoToggleMarker::getClassTypeId() == SoType::badType()) {
            Gui::SoToggleMarker::initClass();
        }
        App::Application::Config()["ExeName"] = "OnViewParameter_tests_run";
        int argc = 1;
        static std::array<char, 32> exename {"OnViewParameter_tests_run"};
        std::array<char*, 2> argv {exename.data(), nullptr};
        App::Application::init(argc, argv.data());

        // No display, and none wanted: an entry box on a mirror is never
        // shown. The platform has to be chosen before the QApplication.
        qputenv("QT_QPA_PLATFORM", "offscreen");
        static int qargc = 1;
        static std::array<char, 32> qexe {"OnViewParameter_tests_run"};
        static std::array<char*, 2> qargv {qexe.data(), nullptr};
        app = new QApplication(qargc, qargv.data());

        // Some platform has no font at all (offscreen on Windows), and a
        // label's number is drawn in one; any family it asks for falls
        // back to the application's
        if (QFontDatabase::families().isEmpty()) {
            int id = QFontDatabase::addApplicationFont(QStringLiteral(FC_TEST_FONT));
            auto families = QFontDatabase::applicationFontFamilies(id);
            if (!families.isEmpty()) {
                QApplication::setFont(QFont(families.front()));
            }
        }
    }

    static void TearDownTestSuite()
    {
        delete app;
        app = nullptr;
    }

    void SetUp() override
    {
        scene = new SoSeparator;
        scene->ref();
        mirror = std::make_unique<Gui::MirrorViewer>(nullptr, scene, nullptr,
                                                     nullptr);
        mirror->setCamera(stateCamera(800, 600));
        changes = 0;
        mirror->setOnViewParametersCallback([this]() { ++changes; });
    }

    void TearDown() override
    {
        labels.clear();
        mirror.reset();
        scene->unref();
    }

    /// A parameter as a tool would open one: a straight dimension from the
    /// origin along +x, in edit so that it is on screen.
    Gui::EditableDatumLabel* addLabel(double value = 0.0)
    {
        auto label = std::make_unique<Gui::EditableDatumLabel>(
            mirror.get(), Base::Placement(), SbColor(1, 1, 1),
            /*autoDistance =*/false, /*avoidMouseCursor =*/false);
        Gui::EditableDatumLabel* raw = label.get();
        labels.push_back(std::move(label));
        raw->activate();
        raw->setPoints(Base::Vector3d(0, 0, 0), Base::Vector3d(10, 0, 0));
        raw->startEdit(value);
        return raw;
    }

    static QApplication* app;
    SoSeparator* scene = nullptr;
    std::unique_ptr<Gui::MirrorViewer> mirror;
    std::vector<std::unique_ptr<Gui::EditableDatumLabel>> labels;
    int changes = 0;
};

QApplication* OnViewParameterTest::app = nullptr;

TEST_F(OnViewParameterTest, aViewWithNoToolRunningHasNoEntryBoxes)
{
    EXPECT_TRUE(mirror->onViewParameters().empty());
}

TEST_F(OnViewParameterTest, aBoxIsReportedOnlyWhileItIsOnScreen)
{
    Gui::EditableDatumLabel* label = addLabel();
    EXPECT_EQ(mirror->onViewParameters().size(), 1U);

    // The controller stops the edit on the boxes that do not belong to
    // the tool's current mode, and the client must stop showing them at
    // the same moment.
    label->stopEdit();
    EXPECT_TRUE(mirror->onViewParameters().empty());
}

TEST_F(OnViewParameterTest, theBoxCarriesTextEvenWithNothingToPaintItOn)
{
    addLabel(12.5);
    const auto params = mirror->onViewParameters();
    ASSERT_EQ(params.size(), 1U);
    // Whatever the locale and the decimals preference make of it, the
    // digits the user would read on a desktop are there -- the box is a
    // real QuantitySpinBox that simply has no window.
    EXPECT_NE(params[0].text.find("12.5"), std::string::npos)
        << "text was '" << params[0].text << "'";
}

TEST_F(OnViewParameterTest, theAnchorIsAWorldPointTheClientCanProject)
{
    addLabel();
    const auto params = mirror->onViewParameters();
    ASSERT_EQ(params.size(), 1U);
    // A distance label from (0,0,0) to (10,0,0) with no offset centres its
    // text on the midpoint. The client projects this with the camera of
    // the frame it is drawing, which is why it goes up in world units and
    // not in pixels (sec 8.7).
    EXPECT_NEAR(params[0].anchor[0], 5.0F, 1e-4F);
    EXPECT_NEAR(params[0].anchor[1], 0.0F, 1e-4F);
    EXPECT_NEAR(params[0].anchor[2], 0.0F, 1e-4F);
}

TEST_F(OnViewParameterTest, focusIsTheViewsAndOnlyOneBoxHasIt)
{
    Gui::EditableDatumLabel* first = addLabel();
    Gui::EditableDatumLabel* second = addLabel();

    first->setFocusToSpinbox();
    auto params = mirror->onViewParameters();
    ASSERT_EQ(params.size(), 2U);
    EXPECT_TRUE(params[0].focus);
    EXPECT_FALSE(params[1].focus);

    // Qt's own focus cannot say this: a widget that is never shown is
    // never focused, so the view keeps the record and moving it must move
    // it away from whoever had it.
    second->setFocusToSpinbox();
    params = mirror->onViewParameters();
    EXPECT_FALSE(params[0].focus);
    EXPECT_TRUE(params[1].focus);
}

TEST_F(OnViewParameterTest, aReplayedKeyIsTypedIntoTheBoxThatHasTheKeys)
{
    Gui::EditableDatumLabel* first = addLabel();
    addLabel();
    first->setFocusToSpinbox();

    const auto before = mirror->onViewParameters();
    ASSERT_EQ(before.size(), 2U);

    // This is the claim the design rests on: a key frame off the wire
    // edits a widget that has no window, through the same code path a
    // keystroke takes on the desktop.
    ASSERT_TRUE(mirror->handleInput(keyPress('7', '7')));

    const auto after = mirror->onViewParameters();
    ASSERT_EQ(after.size(), 2U);
    EXPECT_NE(after[0].text, before[0].text);
    EXPECT_NE(after[0].text.find('7'), std::string::npos)
        << "text was '" << after[0].text << "'";
    EXPECT_EQ(after[1].text, before[1].text)
        << "the key reached a box that did not have the keys";
}

namespace
{

/// What the sketcher's DrawSketchKeyboardManager does with a key an entry
/// box does not claim: hand it back to the view being handled.
///
/// The manager itself lives in SketcherGui and is not linkable here, but
/// the two lines that matter are these, and they are what makes a
/// single-letter tool shortcut still work while a box holds the keyboard.
class HandBack: public QObject
{
protected:
    bool eventFilter(QObject* object, QEvent* event) override
    {
        if (event->type() != QEvent::KeyPress
            && event->type() != QEvent::KeyRelease) {
            return QObject::eventFilter(object, event);
        }
        Gui::ViewerContext* view = Gui::ViewerContext::current();
        return view && view->sendKeyEvent(static_cast<QKeyEvent*>(event));
    }
};

struct SceneKeys
{
    int count = 0;
    int key = 0;
};

void recordKey(void* data, SoEventCallback* node)
{
    auto* seen = static_cast<SceneKeys*>(data);
    ++seen->count;
    const SoEvent* event = node->getEvent();
    if (event->isOfType(SoKeyboardEvent::getClassTypeId())) {
        seen->key = int(static_cast<const SoKeyboardEvent*>(event)->getKey());
    }
}

}  // namespace

TEST_F(OnViewParameterTest, aKeyWithNoBoxOpenGoesToTheScene)
{
    SceneKeys seen;
    mirror->addEventCallback(SoKeyboardEvent::getClassTypeId(), recordKey,
                             &seen);
    mirror->handleInput(keyPress('g', 'g'));
    EXPECT_EQ(seen.count, 1) << "an ordinary key stopped reaching the scene";
    EXPECT_EQ(seen.key, int('g'));
    mirror->removeEventCallback(SoKeyboardEvent::getClassTypeId(), recordKey,
                                &seen);
}

TEST_F(OnViewParameterTest, aBoxThatHandsAKeyBackSendsItToTheScene)
{
    // A box takes the keyboard focus the moment it opens -- that is the
    // desktop's own behaviour, and it is why the manager exists: without
    // the hand-back, no sketch shortcut would work again for as long as a
    // tool was running.
    HandBack manager;
    auto label = std::make_unique<Gui::EditableDatumLabel>(
        mirror.get(), Base::Placement(), SbColor(1, 1, 1), false, false);
    label->activate();
    label->setPoints(Base::Vector3d(0, 0, 0), Base::Vector3d(10, 0, 0));
    label->startEdit(0.0, &manager);
    ASSERT_EQ(mirror->onViewParameters().size(), 1U);
    ASSERT_TRUE(mirror->onViewParameters()[0].focus);

    SceneKeys seen;
    mirror->addEventCallback(SoKeyboardEvent::getClassTypeId(), recordKey,
                             &seen);
    mirror->handleInput(keyPress('g', 'g'));

    // The key went to the box, the box gave it back, and what reached the
    // scene is the frame as it ARRIVED -- not a Qt event translated back
    // into a Coin one and rounded through two tables on the way.
    EXPECT_EQ(seen.count, 1) << "a key the box declined never reached the scene";
    EXPECT_EQ(seen.key, int('g'));
    mirror->removeEventCallback(SoKeyboardEvent::getClassTypeId(), recordKey,
                                &seen);
    label.reset();
}

TEST_F(OnViewParameterTest, aStaleIndexFromAClientIsRefused)
{
    addLabel();
    EXPECT_TRUE(mirror->focusOnViewParameter(0));
    // The client names a box by its position in the set, and the set turns
    // over whenever a tool changes mode -- so an index from a client whose
    // set has moved on must be refused rather than applied to whichever
    // box now sits there.
    EXPECT_FALSE(mirror->focusOnViewParameter(1));
    EXPECT_FALSE(mirror->focusOnViewParameter(-1));
}

TEST_F(OnViewParameterTest, aClientIndexCountsOnlyTheBoxesOnScreen)
{
    // A label shown and not in edit, as a pattern's gaps are until one is
    // clicked, is in the view's set but not in the feed. The client's
    // index is the feed's, so it must land on the box it was given for.
    auto shown = std::make_unique<Gui::EditableDatumLabel>(
        mirror.get(), Base::Placement(), SbColor(1, 1, 1), false, false);
    shown->activate();
    addLabel();
    addLabel();   // opened last, so it has the keys

    const auto before = mirror->onViewParameters();
    ASSERT_EQ(before.size(), 2U);
    EXPECT_FALSE(before[0].focus);
    EXPECT_TRUE(mirror->focusOnViewParameter(0));
    EXPECT_TRUE(mirror->onViewParameters()[0].focus);
    EXPECT_FALSE(mirror->onViewParameters()[1].focus);
    EXPECT_FALSE(mirror->focusOnViewParameter(2));
    shown.reset();
}

namespace
{

Gui::MirrorViewer::Input pointer(Gui::MirrorViewer::Input::Kind kind, int x, int y)
{
    Gui::MirrorViewer::Input input;
    input.kind = kind;
    input.x = x;
    input.y = y;
    input.code = 1;
    input.time = 1.0;
    return input;
}

}  // namespace

TEST_F(OnViewParameterTest, aClickReachesAPickableLabelThroughTheMirror)
{
    // A pattern's label is clicked to be edited, and on a served view the
    // click is a replayed event through the scene -- so the label's own
    // callback has to hear it there, and pick its number without a GL pass
    // ever having sized it.
    auto label = std::make_unique<Gui::EditableDatumLabel>(
        mirror.get(), Base::Placement(), SbColor(1, 1, 1), false, false);
    label->setLabelType(Gui::SoDatumLabel::DISTANCE,
                        Gui::EditableDatumLabel::Function::Dimensioning);
    label->activate();
    label->setPoints(Base::Vector3d(0, 0, 0), Base::Vector3d(10, 0, 0));
    label->label->string = "10 mm";
    int clicks = 0;
    QObject::connect(label.get(), &Gui::EditableDatumLabel::clicked,
                     [&clicks](Gui::EditableDatumLabel*) { ++clicks; });

    // The number sits on the midpoint, (5,0,0): 5 / (60 tan 22.5deg) of
    // the half height right of the centre of an 800x600 canvas
    const int x = 400 + int(5.0 / (60.0 * 0.41421356) * 300.0 + 0.5);
    const int y = 300;
    using Kind = Gui::MirrorViewer::Input::Kind;

    // Not pickable, not clicked: the sketcher's labels are left alone
    mirror->handleInput(pointer(Kind::Press, x, y));
    mirror->handleInput(pointer(Kind::Release, x, y));
    EXPECT_EQ(clicks, 0);

    label->setPickable(true);
    mirror->handleInput(pointer(Kind::Press, x, y));
    mirror->handleInput(pointer(Kind::Release, x, y));
    EXPECT_EQ(clicks, 1);

    // And a click beside it is not one on it
    mirror->handleInput(pointer(Kind::Press, x, y + 150));
    mirror->handleInput(pointer(Kind::Release, x, y + 150));
    EXPECT_EQ(clicks, 1);
}

TEST_F(OnViewParameterTest, theWholeNumberIsPickable)
{
    // The number's pick quad was a bowtie: its top was not pickable and its
    // centre lay on an edge, so a browser's click on the middle of a drawn
    // label went through it
    auto label = std::make_unique<Gui::EditableDatumLabel>(
        mirror.get(), Base::Placement(), SbColor(1, 1, 1), false, false);
    label->setLabelType(Gui::SoDatumLabel::DISTANCE,
                        Gui::EditableDatumLabel::Function::Dimensioning);
    label->activate();
    label->setPoints(Base::Vector3d(0, 0, 0), Base::Vector3d(10, 0, 0));
    label->label->string = "10 mm";
    label->setPickable(true);
    int clicks = 0;
    QObject::connect(label.get(), &Gui::EditableDatumLabel::clicked,
                     [&clicks](Gui::EditableDatumLabel*) { ++clicks; });

    const int x = 400 + int(5.0 / (60.0 * 0.41421356) * 300.0 + 0.5);
    using Kind = Gui::MirrorViewer::Input::Kind;
    int expected = 0;
    for (int dy : {-5, 5}) {
        mirror->handleInput(pointer(Kind::Press, x, 300 + dy));
        mirror->handleInput(pointer(Kind::Release, x, 300 + dy));
        EXPECT_EQ(clicks, ++expected) << "a click " << dy << " px off the centre row";
    }
}

TEST_F(OnViewParameterTest, aLabelOutlivesItsView)
{
    // A closed document takes its view first and the panel holding the
    // labels after: the view takes their dimensions out of its graph as it
    // goes, and a label is not to reach it afterwards
    addLabel();
    ASSERT_GT(scene->getNumChildren(), 0);
    mirror.reset();
    EXPECT_EQ(scene->getNumChildren(), 0);
    labels.clear();
}

TEST_F(OnViewParameterTest, theBoxTakesTheLabelsSize)
{
    Gui::EditableDatumLabel* label = addLabel();
    ASSERT_EQ(mirror->onViewParameters().size(), 1U);
    EXPECT_EQ(mirror->onViewParameters()[0].pointSize, label->getFontPointSize());
    EXPECT_GT(mirror->onViewParameters()[0].pointSize, 0.0);
}

TEST_F(OnViewParameterTest, theViewIsToldWheneverTheSetMoves)
{
    const int quiet = changes;
    Gui::EditableDatumLabel* label = addLabel();
    EXPECT_GT(changes, quiet) << "opening a box said nothing";

    const int opened = changes;
    label->setPoints(Base::Vector3d(0, 0, 0), Base::Vector3d(20, 0, 0));
    EXPECT_GT(changes, opened) << "moving a box said nothing";
}

TEST_F(OnViewParameterTest, aBoxThatGoesAwayTakesTheFocusWithIt)
{
    Gui::EditableDatumLabel* label = addLabel();
    label->setFocusToSpinbox();
    ASSERT_TRUE(mirror->onViewParameters()[0].focus);

    labels.clear();   // the controller's set is rebuilt on every mode change
    EXPECT_TRUE(mirror->onViewParameters().empty());
    // And a key arriving after it must not be routed to freed memory.
    EXPECT_NO_FATAL_FAILURE(mirror->handleInput(keyPress('7', '7')));
}

// ----------------------------------------------------------------------------
// A pattern's instance toggles (Gui::PatternInstanceMarkers): scene nodes, so
// that a served view has them as it has the labels

/// Where a world point on the z = 0 plane lands on the 800x600 canvas
int canvasX(double x)
{
    return 400 + int(x / (60.0 * 0.41421356) * 300.0 + 0.5);
}

std::vector<Gui::PatternInstanceMarkers::Instance> twoInstances()
{
    std::vector<Gui::PatternInstanceMarkers::Instance> instances(2);
    instances[0].index = 0;
    instances[1].index = 1;
    instances[1].center = Base::Vector3d(10, 0, 0);
    instances[1].suppressed = true;
    return instances;
}

TEST_F(OnViewParameterTest, aMarkerPaintsWhatAClickDoes)
{
    // The glyph is the image a capture takes, so it is there before any GL
    // pass, and it changes with the state it shows
    auto marker = new Gui::SoToggleMarker;
    marker->ref();
    SbVec2s size;
    int nc = 0;
    const unsigned char* bytes = marker->image.getValue(size, nc);
    ASSERT_NE(bytes, nullptr);
    EXPECT_EQ(size[0], 22);
    EXPECT_EQ(nc, 4);
    std::vector<unsigned char> in(bytes, bytes + size[0] * size[1] * nc);

    marker->active = FALSE;
    bytes = marker->image.getValue(size, nc);
    std::vector<unsigned char> out(bytes, bytes + size[0] * size[1] * nc);
    EXPECT_NE(in, out);

    marker->markerSize = 48;
    marker->image.getValue(size, nc);
    EXPECT_EQ(size[0], 48);
    marker->unref();
}

TEST_F(OnViewParameterTest, aClickOnAMarkerAsksToToggleItsInstance)
{
    Gui::PatternInstanceMarkers markers;
    std::vector<std::pair<int, bool>> asked;
    QObject::connect(&markers, &Gui::PatternInstanceMarkers::toggleRequested,
                     [&asked](int index, bool suppress) { asked.emplace_back(index, suppress); });
    markers.show(mirror.get(), twoInstances());
    ASSERT_NE(markers.getMarker(1), nullptr);
    EXPECT_TRUE(markers.getMarker(0)->active.getValue());
    EXPECT_FALSE(markers.getMarker(1)->active.getValue());

    using Kind = Gui::MirrorViewer::Input::Kind;
    // The one left out is brought back, the one in is left out
    mirror->handleInput(pointer(Kind::Press, canvasX(10), 300));
    mirror->handleInput(pointer(Kind::Release, canvasX(10), 300));
    mirror->handleInput(pointer(Kind::Press, canvasX(0), 300));
    mirror->handleInput(pointer(Kind::Release, canvasX(0), 300));
    ASSERT_EQ(asked.size(), 2U);
    EXPECT_EQ(asked[0], std::make_pair(1, false));
    EXPECT_EQ(asked[1], std::make_pair(0, true));

    // Beside one is no click, nor a press on one released on another
    mirror->handleInput(pointer(Kind::Press, canvasX(5), 300));
    mirror->handleInput(pointer(Kind::Release, canvasX(5), 300));
    mirror->handleInput(pointer(Kind::Press, canvasX(0), 300));
    mirror->handleInput(pointer(Kind::Release, canvasX(10), 300));
    EXPECT_EQ(asked.size(), 2U);

    // The panel shows them again as the pattern has them now
    auto instances = twoInstances();
    instances[1].suppressed = false;
    markers.show(mirror.get(), instances);
    EXPECT_TRUE(markers.getMarker(1)->active.getValue());
}

TEST_F(OnViewParameterTest, aMarkerIsCentredOnItsPoint)
{
    // An SoImage hangs off its point to the upper right unless told
    // otherwise, and was clicked there and not where the instance is
    Gui::PatternInstanceMarkers markers;
    int clicks = 0;
    QObject::connect(&markers, &Gui::PatternInstanceMarkers::toggleRequested,
                     [&clicks](int, bool) { ++clicks; });
    markers.show(mirror.get(), twoInstances());

    using Kind = Gui::MirrorViewer::Input::Kind;
    int expected = 0;
    for (int dx : {-8, 8}) {
        for (int dy : {-8, 8}) {
            mirror->handleInput(pointer(Kind::Press, canvasX(0) + dx, 300 + dy));
            mirror->handleInput(pointer(Kind::Release, canvasX(0) + dx, 300 + dy));
            EXPECT_EQ(clicks, ++expected) << "a click at " << dx << "," << dy;
        }
    }
}

TEST_F(OnViewParameterTest, aMarkerIsPickedThroughTheInstanceAroundIt)
{
    // A marker sits at the middle of its instance, inside the solid: the
    // solid's face is nearer, and must not take the click
    auto cube = new SoCube;
    cube->width = 4;
    cube->height = 4;
    cube->depth = 4;
    scene->addChild(cube);

    Gui::PatternInstanceMarkers markers;
    int clicks = 0;
    QObject::connect(&markers, &Gui::PatternInstanceMarkers::toggleRequested,
                     [&clicks](int, bool) { ++clicks; });
    markers.show(mirror.get(), twoInstances());

    using Kind = Gui::MirrorViewer::Input::Kind;
    mirror->handleInput(pointer(Kind::Press, canvasX(0), 300));
    mirror->handleInput(pointer(Kind::Release, canvasX(0), 300));
    EXPECT_EQ(clicks, 1);
}

TEST_F(OnViewParameterTest, theMarkerUnderThePointerIsHighlighted)
{
    Gui::PatternInstanceMarkers markers;
    markers.show(mirror.get(), twoInstances());
    using Kind = Gui::MirrorViewer::Input::Kind;
    mirror->handleInput(pointer(Kind::Move, canvasX(10), 300));
    EXPECT_FALSE(markers.getMarker(0)->highlighted.getValue());
    EXPECT_TRUE(markers.getMarker(1)->highlighted.getValue());
    mirror->handleInput(pointer(Kind::Move, canvasX(5), 300));
    EXPECT_FALSE(markers.getMarker(1)->highlighted.getValue());
}

TEST_F(OnViewParameterTest, theMarkersGoWhenCleared)
{
    Gui::PatternInstanceMarkers markers;
    const int before = scene->getNumChildren();
    markers.show(mirror.get(), twoInstances());
    EXPECT_EQ(scene->getNumChildren(), before + 1);
    // One instance is nothing to leave out
    markers.show(mirror.get(), {});
    EXPECT_EQ(scene->getNumChildren(), before);
    EXPECT_EQ(markers.getMarker(0), nullptr);
}

TEST_F(OnViewParameterTest, theMarkersOutliveTheirView)
{
    // A document closed under an open panel takes the view first
    auto markers = std::make_unique<Gui::PatternInstanceMarkers>();
    markers->show(mirror.get(), twoInstances());
    mirror.reset();
    EXPECT_NO_FATAL_FAILURE(markers.reset());
}

}  // namespace
