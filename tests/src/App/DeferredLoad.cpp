// SPDX-License-Identifier: LGPL-2.1-or-later

/** Deferred archive-entry restore (docs/DocumentLoad.md §14).
 *
 * The document parks the archive entry of any property that opts in and
 * serves it on first real use. What these tests hold to account is the
 * document half of that contract -- park, serve, cancel, flush, and the
 * cases where the thing being served has moved or vanished underneath --
 * with a stand-in property instead of Part's shape, so the suite links no
 * geometry kernel and states exactly what App promises on its own.
 *
 * The corner cases are the point: a load that spans event-loop turns is a
 * load the user can interrupt, and objects come and go while entries are
 * still parked.
 */

#include <gtest/gtest.h>

#include <functional>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include <App/Application.h>
#include <App/Document.h>
#include <App/DocumentObject.h>
#include <App/DocumentParams.h>
#include <App/PropertyStandard.h>
#include <Base/BaseClass.h>
#include <Base/FileInfo.h>
#include <Base/Reader.h>
#include <Base/Sequencer.h>
#include <Base/Writer.h>

#include <src/App/InitApplication.h>

// In namespace App because the type system reads a registered name as
// <module>::<class> and tries to import the module for anything else.
namespace App
{

/** A property whose payload lives in its own archive entry, like a shape.
 *
 * Deliberately minimal: a string written to a file entry, read back on
 * demand. text()/assign() stand in for the shape accessors -- every read
 * serves a parked entry first, every write cancels one rather than racing
 * it (PropertyPartShape::ensureRestored / cancelRestorePending).
 */
class TestPayloadProperty: public App::PropertyString
{
    TYPESYSTEM_HEADER_WITH_OVERRIDE();

public:
    /// Fired from RestoreDocFile, so a test can make a serve reenter the
    /// document the way a real consumer's change notification can.
    static std::function<void()> onRestore;
    /// Counts actual archive reads, so a test can prove a value came from
    /// memory rather than from the file.
    static int restoreCount;

    /// Reading it serves the parked entry first.
    std::string text() const
    {
        ensureRestored();
        return getStrValue();
    }
    /// Writing it drops the parked entry unserved.
    void assign(const char* v)
    {
        cancelRestorePending();
        setValue(v);
    }
    /// The raw member, without the fault-in -- for asserting that a value
    /// really is still absent.
    const std::string& rawText() const
    {
        return getStrValue();
    }

    bool canDeferRestore() const override
    {
        return true;
    }
    /// Its value is an archive entry, never a shareable class default.
    bool canShareDefault() const override
    {
        return false;
    }
    bool isRestorePending() const override
    {
        return _pending;
    }
    void setRestorePending(bool on) override
    {
        _pending = on;
    }

    void Save(Base::Writer& writer) const override
    {
        ensureRestored();
        writer.Stream() << writer.ind() << "<TestPayload file=\""
                        << writer.addFile("payload.txt", this) << "\"/>\n";
    }
    void Restore(Base::XMLReader& reader) override
    {
        reader.readElement("TestPayload");
        if (reader.hasAttribute("file")) {
            std::string file(reader.getAttribute("file"));
            if (!file.empty()) {
                reader.addFile(file.c_str(), this);
            }
        }
    }
    void SaveDocFile(Base::Writer& writer) const override
    {
        ensureRestored();
        writer.Stream() << getStrValue();
    }
    void RestoreDocFile(Base::Reader& reader) override
    {
        ++restoreCount;
        std::string content((std::istreambuf_iterator<char>(reader)),
                            std::istreambuf_iterator<char>());
        setValue(content);
        if (onRestore) {
            onRestore();
        }
    }

private:
    void ensureRestored() const
    {
        if (!_pending) {
            return;
        }
        // Cleared before serving: whatever runs below reads this property
        // again and must find a settled state instead of re-entering.
        auto self = const_cast<TestPayloadProperty*>(this);
        self->_pending = false;
        auto owner = Base::freecad_dynamic_cast<App::DocumentObject>(getContainer());
        if (owner && owner->getDocument()) {
            owner->getDocument()->restoreDeferredFile(self);
        }
    }
    void cancelRestorePending()
    {
        if (!_pending) {
            return;
        }
        _pending = false;
        auto owner = Base::freecad_dynamic_cast<App::DocumentObject>(getContainer());
        if (owner && owner->getDocument()) {
            owner->getDocument()->cancelDeferredFile(this);
        }
    }

    mutable bool _pending {false};
};

std::function<void()> TestPayloadProperty::onRestore;
int TestPayloadProperty::restoreCount = 0;

/// An object carrying one such property.
class TestPayloadFeature: public App::DocumentObject
{
    PROPERTY_HEADER_WITH_OVERRIDE(TestPayloadFeature);

public:
    TestPayloadFeature()
    {
        ADD_PROPERTY_TYPE(Payload, (""), "Test", App::Prop_None, "deferred payload");
    }
    TestPayloadProperty Payload;

    const char* getViewProviderName() const override
    {
        return "";
    }
};

}  // namespace App

TYPESYSTEM_SOURCE(App::TestPayloadProperty, App::PropertyString)
PROPERTY_SOURCE(App::TestPayloadFeature, App::DocumentObject)

namespace
{

using App::TestPayloadFeature;
using App::TestPayloadProperty;

class DeferredLoadTest: public ::testing::Test
{
protected:
    static void SetUpTestSuite()
    {
        tests::initApplication();
        // Quiet: the serve and the archive walk both run sequences, and
        // ConsoleSequencer would print a percentage line for each.
        static Base::EmptySequencer quiet;
        (void)quiet;
        TestPayloadProperty::init();
        TestPayloadFeature::init();
    }

    void SetUp() override
    {
        _archiveOn = App::DocumentParams::getArchiveRandomAccess();
        _deferOn = App::DocumentParams::getDeferShapeLoad();
        App::DocumentParams::setArchiveRandomAccess(true);
        App::DocumentParams::setDeferShapeLoad(true);
        TestPayloadProperty::onRestore = {};
        TestPayloadProperty::restoreCount = 0;
        _file = Base::FileInfo::getTempFileName();
        _file += ".FCStd";
    }

    void TearDown() override
    {
        TestPayloadProperty::onRestore = {};
        if (_doc) {
            App::GetApplication().closeDocument(_doc->getName());
            _doc = nullptr;
        }
        App::DocumentParams::setArchiveRandomAccess(_archiveOn);
        App::DocumentParams::setDeferShapeLoad(_deferOn);
        removeArchiveAndBackups();
    }

    /// The archive and whatever a save left beside it -- the backup naming
    /// depends on preferences this suite does not fix.
    void removeArchiveAndBackups() const
    {
        Base::FileInfo archive(_file);
        const std::string prefix = archive.fileNamePure();
        Base::FileInfo dir(archive.dirPath());
        for (const auto& entry : dir.getDirectoryContent()) {
            if (entry.isFile() && entry.fileName().compare(0, prefix.size(), prefix) == 0) {
                removeFile(entry.filePath());
            }
        }
    }

    static void removeFile(const std::string& path)
    {
        Base::FileInfo fi(path);
        if (fi.exists()) {
            try {
                fi.deleteFile();
            }
            catch (...) {
            }
        }
    }

    /// A saved document with \a count payload objects, closed again.
    /// Payload of object i is "payload-<i>" padded so entries differ in size.
    void writeArchive(int count)
    {
        auto name = App::GetApplication().getUniqueDocumentName("deferwrite");
        auto doc = App::GetApplication().newDocument(name.c_str(), "test");
        for (int i = 0; i < count; ++i) {
            auto obj = static_cast<TestPayloadFeature*>(
                doc->addObject("App::TestPayloadFeature", ("obj" + std::to_string(i)).c_str()));
            obj->Payload.assign(payloadFor(i).c_str());
        }
        ASSERT_TRUE(doc->saveAs(_file.c_str()));
        App::GetApplication().closeDocument(doc->getName());
    }

    /// Reopen it with the entries parked.
    App::Document* openArchive()
    {
        _doc = App::GetApplication().openDocument(_file.c_str(), false);
        return _doc;
    }

    static std::string payloadFor(int i)
    {
        return "payload-" + std::to_string(i) + std::string(size_t(i) * 7 + 3, 'x');
    }

    TestPayloadFeature* feature(int i) const
    {
        return static_cast<TestPayloadFeature*>(
            _doc->getObject(("obj" + std::to_string(i)).c_str()));
    }

    App::Document* _doc {nullptr};
    std::string _file;

private:
    bool _archiveOn {true};
    bool _deferOn {true};
};

TEST_F(DeferredLoadTest, ParkedEntriesServeOnFirstAccess)
{
    writeArchive(4);
    auto doc = openArchive();
    ASSERT_NE(doc, nullptr);

    // Nothing was read during the load...
    EXPECT_EQ(TestPayloadProperty::restoreCount, 0);
    for (int i = 0; i < 4; ++i) {
        EXPECT_TRUE(feature(i)->Payload.isRestorePending()) << "object " << i;
        EXPECT_TRUE(doc->hasDeferredFile(&feature(i)->Payload));
        EXPECT_TRUE(feature(i)->Payload.rawText().empty());
    }

    // ...and asking one for its value reads exactly that one.
    EXPECT_EQ(feature(2)->Payload.text(), payloadFor(2));
    EXPECT_EQ(TestPayloadProperty::restoreCount, 1);
    EXPECT_FALSE(doc->hasDeferredFile(&feature(2)->Payload));
    EXPECT_TRUE(doc->hasDeferredFile(&feature(0)->Payload));

    // A second read is free.
    EXPECT_EQ(feature(2)->Payload.text(), payloadFor(2));
    EXPECT_EQ(TestPayloadProperty::restoreCount, 1);

    // The bulk serve takes the rest, and reports when it is done.
    EXPECT_FALSE(doc->serveDeferredFiles(60.0));
    EXPECT_EQ(TestPayloadProperty::restoreCount, 4);
    for (int i = 0; i < 4; ++i) {
        EXPECT_FALSE(feature(i)->Payload.isRestorePending());
        EXPECT_EQ(feature(i)->Payload.rawText(), payloadFor(i)) << "object " << i;
    }
    // Serving must not leave the objects looking edited.
    for (int i = 0; i < 4; ++i) {
        EXPECT_FALSE(feature(i)->isTouched()) << "object " << i;
    }
}

TEST_F(DeferredLoadTest, ServeSliceHonoursItsBudget)
{
    writeArchive(6);
    auto doc = openArchive();
    ASSERT_NE(doc, nullptr);
    // A zero budget still makes progress -- one item, the granularity of a
    // slice -- and says there is more to come.
    EXPECT_TRUE(doc->serveDeferredFiles(0.0));
    EXPECT_EQ(TestPayloadProperty::restoreCount, 1);
    while (doc->serveDeferredFiles(0.0)) {}
    EXPECT_EQ(TestPayloadProperty::restoreCount, 6);
}

TEST_F(DeferredLoadTest, OverwritingCancelsTheParkedEntry)
{
    writeArchive(3);
    auto doc = openArchive();
    ASSERT_NE(doc, nullptr);

    feature(1)->Payload.assign("written over");
    EXPECT_FALSE(feature(1)->Payload.isRestorePending());
    EXPECT_FALSE(doc->hasDeferredFile(&feature(1)->Payload));

    // The drain must not resurrect what was overwritten.
    doc->serveDeferredFiles(60.0);
    EXPECT_EQ(feature(1)->Payload.rawText(), "written over");
    EXPECT_EQ(TestPayloadProperty::restoreCount, 2);
}

TEST_F(DeferredLoadTest, DeletedOwnerDropsItsEntry)
{
    writeArchive(4);
    auto doc = openArchive();
    ASSERT_NE(doc, nullptr);

    doc->removeObject(feature(1)->getNameInDocument());
    doc->removeObject(feature(3)->getNameInDocument());

    EXPECT_FALSE(doc->serveDeferredFiles(60.0));
    EXPECT_EQ(TestPayloadProperty::restoreCount, 2);
    EXPECT_EQ(feature(0)->Payload.rawText(), payloadFor(0));
    EXPECT_EQ(feature(2)->Payload.rawText(), payloadFor(2));
}

TEST_F(DeferredLoadTest, ObjectAddedMidDrainDoesNotDisturbTheServe)
{
    writeArchive(4);
    auto doc = openArchive();
    ASSERT_NE(doc, nullptr);

    EXPECT_TRUE(doc->serveDeferredFiles(0.0));
    auto fresh =
        static_cast<TestPayloadFeature*>(doc->addObject("App::TestPayloadFeature", "fresh"));
    fresh->Payload.assign("brand new");
    // A live creation owns no archive entry and never gets one.
    EXPECT_FALSE(fresh->Payload.isRestorePending());
    EXPECT_FALSE(doc->hasDeferredFile(&fresh->Payload));

    while (doc->serveDeferredFiles(0.0)) {}
    EXPECT_EQ(TestPayloadProperty::restoreCount, 4);
    EXPECT_EQ(fresh->Payload.rawText(), "brand new");
    for (int i = 0; i < 4; ++i) {
        EXPECT_EQ(feature(i)->Payload.rawText(), payloadFor(i)) << "object " << i;
    }
}

/** A serve that cancels the last remaining entry must not take the slice
 * down with it: the sequence reporting the drain is dropped by the cancel
 * that empties the map, and the loop still has to finish the item in hand.
 */
TEST_F(DeferredLoadTest, ServeThatCancelsTheLastEntrySurvives)
{
    writeArchive(2);
    auto doc = openArchive();
    ASSERT_NE(doc, nullptr);

    // Serving whichever entry comes first overwrites the other, which is
    // what a consumer reacting to a change notification can do.
    TestPayloadProperty::onRestore = [this]() {
        for (int i = 0; i < 2; ++i) {
            if (feature(i)->Payload.isRestorePending()) {
                feature(i)->Payload.assign("cancelled by a peer");
            }
        }
    };

    EXPECT_NO_THROW(doc->serveDeferredFiles(60.0));
    EXPECT_EQ(TestPayloadProperty::restoreCount, 1);
    int cancelled = 0;
    for (int i = 0; i < 2; ++i) {
        EXPECT_FALSE(feature(i)->Payload.isRestorePending());
        cancelled += feature(i)->Payload.rawText() == "cancelled by a peer" ? 1 : 0;
    }
    EXPECT_EQ(cancelled, 1);
}

/** The archive is the backing store for everything still parked. If it
 * cannot be opened, the serve reports and gives up -- it does not throw,
 * because it runs inside an event-loop slice and inside const accessors,
 * neither of which may unwind.
 */
TEST_F(DeferredLoadTest, VanishedArchiveFailsQuietly)
{
    writeArchive(3);
    auto doc = openArchive();
    ASSERT_NE(doc, nullptr);

    removeFile(_file);

    EXPECT_NO_THROW({
        std::string text = feature(0)->Payload.text();
        EXPECT_TRUE(text.empty());
    });
    EXPECT_NO_THROW(doc->serveDeferredFiles(60.0));
    for (int i = 0; i < 3; ++i) {
        EXPECT_FALSE(feature(i)->Payload.isRestorePending());
        EXPECT_FALSE(doc->hasDeferredFile(&feature(i)->Payload));
    }
}

TEST_F(DeferredLoadTest, TruncatedArchiveFailsQuietly)
{
    writeArchive(3);
    auto doc = openArchive();
    ASSERT_NE(doc, nullptr);

    // Same file name, contents nothing like a zip: every open of a parked
    // entry now throws from inside zipios.
    {
        std::ofstream out(_file.c_str(), std::ios::binary | std::ios::trunc);
        out << std::string(4096, '\0');
    }

    EXPECT_NO_THROW(doc->serveDeferredFiles(60.0));
    for (int i = 0; i < 3; ++i) {
        EXPECT_FALSE(doc->hasDeferredFile(&feature(i)->Payload));
    }
}

/** A save must leave nothing parked. The archive it indexes is renamed to
 * a backup (or deleted) as the last step of the save, so an entry served
 * afterwards would seek a stale offset into a different file -- and the
 * property accessors do not cover it on their own: a Transient property is
 * skipped by beforeSave() and Save() alike.
 */
TEST_F(DeferredLoadTest, SaveFlushesEntriesNoPropertyWouldAskFor)
{
    writeArchive(3);
    auto doc = openArchive();
    ASSERT_NE(doc, nullptr);

    // The save will not touch this one.
    feature(1)->Payload.setStatus(App::Property::Transient, true);
    ASSERT_TRUE(feature(1)->Payload.isRestorePending());

    ASSERT_TRUE(doc->save());

    // Nothing may still point into an archive that is no longer there.
    for (int i = 0; i < 3; ++i) {
        EXPECT_FALSE(doc->hasDeferredFile(&feature(i)->Payload)) << "object " << i;
        EXPECT_FALSE(feature(i)->Payload.isRestorePending()) << "object " << i;
    }
    EXPECT_EQ(TestPayloadProperty::restoreCount, 3);
    EXPECT_EQ(feature(1)->Payload.rawText(), payloadFor(1));

    // And the value survives the file it came from going away entirely:
    // it was read while the source archive was still on disk.
    removeArchiveAndBackups();
    EXPECT_EQ(feature(1)->Payload.text(), payloadFor(1));
    EXPECT_EQ(TestPayloadProperty::restoreCount, 3);
}

TEST_F(DeferredLoadTest, SaveWithEverythingPendingRoundTrips)
{
    writeArchive(5);
    auto doc = openArchive();
    ASSERT_NE(doc, nullptr);
    ASSERT_TRUE(doc->save());
    App::GetApplication().closeDocument(doc->getName());
    _doc = nullptr;

    auto reopened = openArchive();
    ASSERT_NE(reopened, nullptr);
    for (int i = 0; i < 5; ++i) {
        EXPECT_EQ(feature(i)->Payload.text(), payloadFor(i)) << "object " << i;
    }
}

/// With random access off there is nothing to defer from: the forward-only
/// walk serves every entry during the load, as it always did.
TEST_F(DeferredLoadTest, ForwardOnlyArchiveServesEagerly)
{
    writeArchive(3);
    App::DocumentParams::setArchiveRandomAccess(false);
    auto doc = openArchive();
    ASSERT_NE(doc, nullptr);
    EXPECT_EQ(TestPayloadProperty::restoreCount, 3);
    for (int i = 0; i < 3; ++i) {
        EXPECT_FALSE(feature(i)->Payload.isRestorePending());
        EXPECT_EQ(feature(i)->Payload.rawText(), payloadFor(i)) << "object " << i;
    }
    EXPECT_FALSE(doc->serveDeferredFiles(60.0));
}

TEST_F(DeferredLoadTest, DeferOffServesEagerly)
{
    writeArchive(3);
    App::DocumentParams::setDeferShapeLoad(false);
    auto doc = openArchive();
    ASSERT_NE(doc, nullptr);
    EXPECT_EQ(TestPayloadProperty::restoreCount, 3);
    for (int i = 0; i < 3; ++i) {
        EXPECT_FALSE(feature(i)->Payload.isRestorePending());
        EXPECT_EQ(feature(i)->Payload.rawText(), payloadFor(i)) << "object " << i;
    }
}

}  // namespace
