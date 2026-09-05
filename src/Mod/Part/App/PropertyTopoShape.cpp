/***************************************************************************
 *   Copyright (c) 2002 Jürgen Riegel <juergen.riegel@web.de>              *
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

#include "PreCompiled.h"

#ifndef _PreComp_
# include <charconv>
# include <memory>
# include <mutex>
# include <sstream>
# include <unordered_map>
# include <Bnd_Box.hxx>
# include <BRepBndLib.hxx>
# include <BRepBuilderAPI_Copy.hxx>
# include <BRepBuilderAPI_Transform.hxx>
# include <BRepTools.hxx>
# include <BRep_Builder.hxx>
# include <BRepTools_ShapeSet.hxx>
# include <OSD_OpenFile.hxx>
# include <TopExp.hxx>
# include <Standard_Failure.hxx>
# include <Standard_Version.hxx>
# include <gp_GTrsf.hxx>
# include <gp_Trsf.hxx>
# include <BRepBuilderAPI_MakeShape.hxx>
# include <TopLoc_Location.hxx>
# include <TopTools_ListOfShape.hxx>
# include <TopTools_IndexedMapOfShape.hxx>

# include <TopoDS.hxx>
#endif // _PreComp_

#include <App/Application.h>
#include <App/Document.h>
#include <App/DocumentParams.h>
#include <App/DocumentObject.h>
#include <App/ObjectIdentifier.h>
#include <Base/Console.h>
#include <Base/Exception.h>
#include <Base/FileInfo.h>
#include <Base/Interpreter.h>
#include <Base/QuantityPy.h>
#include <Base/Reader.h>
#include <Base/Stream.h>
#include <Base/Tools.h>
#include <Base/Writer.h>
#include <App/GeoFeature.h>

#include "PartFeature.h"
#include "PartParams.h"
#include "PartPyCXX.h"
#include "PropertyShapeStore.h"
#include "PropertyTopoShape.h"
#include "ShapeCongruence.h"
#include "ShapeRefSet.h"
#include "TopoShapePy.h"

namespace sp = std::placeholders;

FC_LOG_LEVEL_INIT("PropShape",true,true);

using namespace Part;

TYPESYSTEM_SOURCE(Part::PropertyPartShape , App::PropertyComplexGeoData)

PropertyPartShape::PropertyPartShape() = default;

PropertyPartShape::~PropertyPartShape()
{
    // A referrer queued for content that has not arrived outlives nothing:
    // the manager would dispatch onto freed memory.
    if (_PendingManager)
        _PendingManager->removePendingReferrer(this);
}

namespace
{

/** The shortest text that reads back as the very same double.
 *
 * Exactness is not decoration here: an inexactly restored location is a
 * different shape, and the whole point of taking the location out of the
 * geometry is a file that does not change when nothing did.
 */
void writeReal(std::ostream& out, double value)
{
    char buf[40];
    auto res = std::to_chars(buf, buf + sizeof(buf), value);
    out.write(buf, res.ptr - buf);
}

/** Whether a location moves anything at all.
 *
 * TopLoc_Location::IsIdentity() answers whether there is a datum, not whether
 * that datum does anything: setting a placement of zero builds a location that
 * holds an identity transformation, and every Part::Feature whose placement was
 * ever touched carries one. Comparing exactly is right -- such a location
 * contributes nothing to the geometry, so dropping it is lossless, and writing
 * it would be an attribute that says nothing and a file that churns.
 */
bool isIdentityLocation(const TopLoc_Location& loc)
{
    if (loc.IsIdentity())
        return true;
    const gp_Trsf& trsf = loc.Transformation();
    for (int row = 1; row <= 3; ++row) {
        for (int col = 1; col <= 4; ++col) {
            if (trsf.Value(row, col) != (row == col ? 1.0 : 0.0))
                return false;
        }
    }
    return true;
}

/// A location as the 3x4 of its transformation, row major -- scale included.
std::string locationToString(const TopLoc_Location& loc)
{
    std::ostringstream str;
    const gp_Trsf& trsf = loc.Transformation();
    for (int row = 1; row <= 3; ++row) {
        for (int col = 1; col <= 4; ++col) {
            if (row != 1 || col != 1)
                str << ' ';
            writeReal(str, trsf.Value(row, col));
        }
    }
    return str.str();
}

bool locationFromString(const std::string& text, TopLoc_Location& loc)
{
    double v[12];
    std::istringstream str(text);
    for (double& value : v) {
        if (!(str >> value)) {
            FC_ERR("Truncated shape location '" << text << '\'');
            return false;
        }
    }
    try {
        gp_Trsf trsf;
        trsf.SetValues(v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7], v[8], v[9], v[10], v[11]);
        loc = TopLoc_Location(trsf);
    }
    catch (const Standard_Failure& e) {
        FC_ERR("Invalid shape location '" << text << "': " << e.GetMessageString());
        return false;
    }
    return true;
}

/** A shape file as it came out of the parse.
 *
 * More than the shape it is: another file may borrow a sub-shape of this one
 * by index (docs/SharedShapeStorage.md sec 11.6), so the table this file's
 * numbering addresses has to survive the parse, and the plan says what this
 * file itself borrowed -- which is what a later save compares against before
 * it can leave the file alone.
 */
struct ParsedShape
{
    TopoDS_Shape root;
    /** The set the file was parsed into, kept whole rather than as a copy of
     * its shape table.
     *
     * Another file addresses this one by position, and with cross-file
     * geometry on it addresses the surface and curve tables that way too --
     * so what has to survive the parse is the set, not one map out of it.
     * Shared rather than owned: what the resolver hands out lives as long as
     * the cache entry, and the entry lives as long as the file.
     */
    std::shared_ptr<ShapeRefSet> tables;
    std::string plan;
};

/** One parse per file, however many properties refer to it.
 *
 * Blobs are content, so two objects whose geometry serializes to the same
 * bytes -- with the location canonicalized out, every pair of equal parts --
 * hold the *same* blob. Parsing it once and handing the same TopoDS_Shape to
 * both is what restores the TShape sharing the central store used to provide,
 * and it is why equal-but-unshared duplicates cost one parse and one
 * tessellation instead of N (docs/SharedShapeStorage.md sec 12.5).
 *
 * Keyed on the blob rather than on its hash: a blob belongs to one document,
 * and sharing a TShape across documents is not this cache's decision to make.
 * The weak handle is what says an entry has outlived its content.
 */
class ShapeParseCache
{
public:
    static ShapeParseCache& instance()
    {
        static ShapeParseCache cache;
        return cache;
    }

    /** The parse of this file, or null.
     *
     * The pointer is into the map's own node, so it survives anything but the
     * erasure of this entry -- which the sweep only does once the blob has
     * expired, i.e. once the caller has stopped holding it.
     */
    const ParsedShape* get(const App::FileBlobHandle& blob) const
    {
        std::lock_guard<std::mutex> guard(_mutex);
        auto found = _entries.find(blob.get());
        if (found == _entries.end() || found->second.blob.expired())
            return nullptr;
        return &found->second.parsed;
    }

    const ParsedShape* put(const App::FileBlobHandle& blob, ParsedShape parsed)
    {
        std::lock_guard<std::mutex> guard(_mutex);
        // Swept here rather than on every read: an entry costs a handle and a
        // shape, and the sweep is what keeps a document's whole geometry from
        // being held alive by files nothing refers to any more.
        if (_entries.size() >= _sweepAt) {
            for (auto it = _entries.begin(); it != _entries.end();) {
                it = it->second.blob.expired() ? _entries.erase(it) : std::next(it);
            }
            _sweepAt = std::max<std::size_t>(64, _entries.size() * 2);
        }
        Entry& entry = _entries[blob.get()];
        entry.blob = blob;
        entry.parsed = std::move(parsed);
        return &entry.parsed;
    }

private:
    struct Entry
    {
        std::weak_ptr<App::FileBlob> blob;
        ParsedShape parsed;
    };
    mutable std::mutex _mutex;
    std::unordered_map<const App::FileBlob*, Entry> _entries;
    std::size_t _sweepAt {64};
};

/** Parse a stored geometry file, resolving whatever it borrows.
 *
 * A file that names other files pulls them in transitively, each through this
 * same cache -- so a shape borrowed by twenty objects is parsed once and comes
 * back as one TShape, which is the sharing the whole design exists for.
 */
const ParsedShape* parseBlob(App::FileBlobManager& manager,
                             const App::FileBlobHandle& blob,
                             int depth = 0)
{
    if (!blob)
        return nullptr;
    if (const ParsedShape* cached = ShapeParseCache::instance().get(blob))
        return cached;
    if (depth > 1024) {
        // The writer only ever borrows from files written before this one, so
        // the graph is a DAG by construction. A file that says otherwise was
        // not written by this build.
        // *** Deep, because a chain here is files and not sub-shapes: with
        // cross-file geometry a file names whichever earlier file first wrote
        // each entry, and that file may name an earlier one still. The guard
        // is against a cycle a foreign writer could produce, not against the
        // depth a real project reaches.
        FC_ERR("Geometry file " << blob->path() << " is nested past any depth a save writes");
        return nullptr;
    }

    ParsedShape parsed;
    Base::FileInfo file(blob->path());
    // Held for the length of the read: the tables the resolver hands back live
    // in the cache, and only a live handle keeps an entry from being swept.
    std::vector<App::FileBlobHandle> sources;
    try {
        Base::ifstream in(file, std::ios::in | std::ios::binary);
        if (!in) {
            FC_ERR("Cannot read the geometry in " << blob->path());
        }
        else if (file.hasExtension("bin")) {
            // The binary format has no way to name another file, so nothing
            // written through it borrows anything.
            TopoShape shape;
            shape.importBinary(in);
            parsed.root = shape.getShape();
        }
        else {
            BRep_Builder builder;
            auto set = std::make_shared<ShapeRefSet>(builder);
            set->setResolver([&](const std::string& hash) -> const ShapeRefSet* {
                App::FileBlobHandle source = manager.find(hash);
                if (!source) {
                    return nullptr;
                }
                sources.push_back(source);
                const ParsedShape* borrowed = parseBlob(manager, source, depth + 1);
                return borrowed ? borrowed->tables.get() : nullptr;
            });
            parsed.root = set->read(in);
            parsed.plan = set->plan();
            // The resolver closes over this frame, so it does not outlive it.
            // read() has already dropped what it borrowed from.
            set->setResolver(nullptr);
            parsed.tables = std::move(set);
        }
    }
    catch (const Standard_Failure& e) {
        FC_ERR("Failed to read the geometry in " << blob->path() << ": " << e.GetMessageString());
        parsed = ParsedShape();
    }
    // Cached even when the parse failed: a file that cannot be read does not
    // get read again once per referrer.
    return ShapeParseCache::instance().put(blob, std::move(parsed));
}

/** The owner table for the save in progress.
 *
 * One table, not a registry: a save runs on its own -- Base::Writer is not
 * shared either -- and the generation is what says the last one is over. The
 * table holds no shape handles (ShapeOwnerTable), so what it leaves behind
 * between saves costs a pointer per TShape and pins nothing.
 *
 * *** Keyed on the generation alone, and it has to be: the generation is
 * counted across the process (FileBlobManager::saveGeneration), so every save
 * of every document gets its own number, and the table cannot survive into a
 * save it was not built for. Keying on the document instead would not do --
 * a closed document's address is reused by the next one, and the stale table
 * then answers with TShape addresses that have since been freed and reissued.
 */
ShapeOwnerTable* saveOwnerTable(uint64_t generation)
{
    static uint64_t built = 0;
    static ShapeOwnerTable owners;
    if (built != generation) {
        built = generation;
        owners.clear();
    }
    return &owners;
}

}  // namespace

bool PropertyPartShape::stripsLocation(Base::Writer& writer)
{
    // Schema 5 and nothing below it, for the same reason the shape store has
    // that gate: a schema-4 document written by this build must come out
    // exactly as upstream writes it. It must, here, for a harder reason than
    // taste -- an older reader ignores the `loc=` attribute, and would then
    // announce the geometry at the identity and zero every placement.
    return writer.getSchemaVersion() >= 5;
}

TopoDS_Shape PropertyPartShape::shapeForSave(Base::Writer& writer) const
{
    ensureRestored();
    const TopoDS_Shape& shape = _Shape.getShape();
    if (!stripsLocation(writer) || shape.IsNull() || shape.Location().IsIdentity())
        return shape;
    // Stripped even when the location does nothing (isIdentityLocation), so
    // that the geometry of a placed part and of one that was never touched
    // come out as the same bytes. Shares the TShape -- this is a handle swap,
    // not a copy of any geometry.
    return shape.Located(TopLoc_Location());
}

TopoDS_Shape PropertyPartShape::locatedForRestore(const TopoDS_Shape& shape) const
{
    TopoDS_Shape geometry = shape;
    // *** Baked into the geometry, not carried as a location, and this is
    // the whole reason the file this came from could be shared at all. A
    // restored shape's location is the object's Placement -- model data
    // someone reads. An App::Link that replaces its source's placement with
    // its own reads exactly that, and would then draw this geometry where
    // the instance it was borrowed from sits, which is what a first attempt
    // at this did to 1146 links. So the motion costs a copy of the geometry
    // and leaves the location meaning what it has always meant.
    if (!_RestoreMotion.IsIdentity() && !geometry.IsNull()) {
        BRepBuilderAPI_Transform moved(geometry, _RestoreMotion.Transformation(), Standard_True);
        if (moved.IsDone())
            geometry = moved.Shape();
    }
    if (_RestoreLoc.IsIdentity() || geometry.IsNull())
        return geometry;
    return geometry.Located(_RestoreLoc);
}

App::FileBlobManager& PropertyPartShape::blobManager() const
{
    if (auto container = getContainer()) {
        if (auto doc = container->getOwnerDocument())
            return doc->getFileBlobManager();
    }
    return App::FileBlobManager::defaultManager();
}

bool PropertyPartShape::usesBlob(Base::Writer& writer) const
{
    // The manager decided this once for the whole save, and the answer has to
    // be the same one: it is what says whether there will be entries for the
    // geometry to be in. Above ForceXML level 3 the caller asked for a
    // document that carries everything inside its XML, and the geometry goes
    // inline exactly as it always has.
    (void)writer;
    return blobManager().blobFormat() == App::FileBlobManager::BlobFormat::Entries;
}

/// Extension a save under this writer stores the geometry under, no leading dot.
static const char* shapeBlobExtension(Base::Writer& writer)
{
    return writer.getMode("BinaryBrep") ? "bin" : "brp";
}

void PropertyPartShape::makeBlob(Base::Writer& writer) const
{
    auto& manager = blobManager();
    const char* ext = shapeBlobExtension(writer);
    if (_blob) {
        // Still the right content only if it was written for this document
        // and in this format. A copied object carries a handle on another
        // document's file, and PreferBinary can change between saves.
        if (_blob->owner() != &manager
                || !Base::FileInfo(_blob->path()).hasExtension(ext)) {
            _blob.reset();
            _blobPlan.clear();
        }
    }

    if (writer.getMode("BinaryBrep")) {
        // References are an extension of the ASCII format and of nothing else:
        // the binary encoding has no way to name another file, so a document
        // written binary shares nothing between its files.
        if (!_blob)
            storeBlob(writer, nullptr);
        return;
    }

    auto owner = Base::freecad_dynamic_cast<App::DocumentObject>(getContainer());
    App::Document* doc = owner ? owner->getDocument() : nullptr;
    // Without a document there is nothing to share with: a property standing
    // on its own writes plain BRep, as it always did.
    // Without the owner table nothing is borrowed and nothing is published, so
    // the switch turns this file back into the whole-shape file sec 12.3 wrote.
    // It is what an A/B against that format is measured with.
    ShapeOwnerTable* owners = (doc && PartParams::getShareStoredSubShapes())
        ? saveOwnerTable(manager.saveGeneration())
        : nullptr;
    // Sharing a moved part rides on the same table -- it names the file to
    // share -- so it is off wherever that is.
    CongruenceIndex* congruent = (owners && App::DocumentParams::getDedupCongruentShapes())
        ? CongruenceIndex::forSave(manager.saveGeneration())
        : nullptr;

    // The analysis runs whatever happens to the file. It is what tells later
    // objects that this file holds these sub-shapes, and it is cheap next to
    // serialization -- walking TShapes and mapping pointers
    // (docs/SharedShapeStorage.md sec 11.7).
    const TopoDS_Shape root = shapeForSave(writer);
    ShapeRefSet refs;
    // Before build(), which is what fills the geometry tables.
    TopoShape::applyStorageOptions(refs, true);
    refs.setOwners(owners);
    // Rides on the same table, and is off wherever that is: a geometry entry
    // names a file, and the table is what says which files there are.
    const bool geometry = owners && App::DocumentParams::getDedupCrossFileGeometry();
    refs.setGeometrySharing(geometry);
    // Below a face the association *is* the identity of a geometry object, so
    // this is sound only while that object can be named across files: off
    // wherever the geometry is not shared, whatever the setting says.
    refs.setSubFaceBorrowing(geometry ? PartParams::getBorrowBelowFace() : 0);
    refs.build(root);
    const std::string plan = refs.plan();

    // *** A shape that is another file's whole root is not borrowed, it is
    // shared. Writing a reference to it would produce a file holding nothing
    // but that reference -- and would cost the one thing content addressing
    // was already getting right, because the two objects had identical bytes
    // and so were one file. Borrowing pays below a root, where there is real
    // geometry to leave out; at the root it is pure indirection.
    if (owners && !root.IsNull() && refs.rootIndex() == 0) {
        if (const int held = owners->rootOwner(root)) {
            if (App::FileBlobHandle shared = manager.find(owners->file(held).hash)) {
                _blob = shared;
                _blobPlan = owners->file(held).plan;
                _blobMotion = TopLoc_Location();
                return;
            }
        }
    }

    // The same part written again somewhere else. Content addressing cannot
    // see it when the exporter multiplied the placement into the coordinates,
    // and neither can the table above, which matches on TShape identity. What
    // is left is to recognize the shape by its geometry and record the motion
    // -- which needs no format change at all, because the location this
    // property already writes is where the motion goes
    // (docs/SharedShapeStorage.md sec 12.12).
    // Not gated on rootIndex(), unlike the check above: that one asks whether
    // this file borrowed its root, which is the one case a congruent shape is
    // never in -- the owner table matches TShapes, and a part written again
    // elsewhere is a different TShape holding different numbers.
    if (owners && congruent && !root.IsNull()) {
        CongruenceIndex::Match match;
        if (congruent->find(root, match)) {
            if (App::FileBlobHandle shared = manager.find(owners->file(match.slot).hash)) {
                _blob = shared;
                _blobPlan = owners->file(match.slot).plan;
                _blobMotion = TopLoc_Location(match.motion);
                return;
            }
        }
    }

    // The shape not having changed is what kept _blob (dropBlob), but a file's
    // bytes are the shape *and* what the save borrowed. An object that used to
    // borrow from a file which has since gone still holds a valid shape, and
    // its old file would still name a file this save does not write.
    if (!_blob || plan != _blobPlan) {
        storeBlob(writer, &refs);
        _blobPlan = plan;
        _blobMotion = TopLoc_Location();
    }

    // A retained generation (Feature::materializeShapeVersions) borrows but
    // never publishes: a later object that borrowed from it would have to
    // be rewritten the day the generation is dropped.
    if (_blob && owners && _publishes) {
        ShapeOwnerTable::File entry;
        entry.hash = _blob->hash();
        entry.plan = plan;
        entry.root = refs.rootIndex();
        entry.orientation = root.IsNull() ? 0 : root.Orientation();
        const int file = owners->addFile(std::move(entry));
        refs.publish(file, *owners);
        // Every file written, whatever it borrows: what a later instance
        // needs is a file that reads back as this shape, and borrowing is
        // internal to the file.
        if (congruent)
            congruent->add(root, file);
    }
}

void PropertyPartShape::storeBlob(Base::Writer& writer, ShapeRefSet* refs) const
{
    auto& manager = blobManager();
    const char* ext = shapeBlobExtension(writer);
    const std::string path = manager.uniquePath(std::string("shape.") + ext);
    try {
        {
            Base::ofstream out(Base::FileInfo(path),
                               std::ios::out | std::ios::binary | std::ios::trunc);
            if (!out) {
                FC_ERR("Cannot write the geometry of " << getFullName() << " to " << path);
                return;
            }
            // Even a null shape is written, for the same reason SaveDocFile
            // writes one: an empty member is an error to whatever reads it.
            const TopoDS_Shape shape = shapeForSave(writer);
            if (!refs)
                TopoShape(shape).exportBinary(out, true);
            else
                refs->write(shape, out);
        }
        _blob = manager.adoptFile(path.c_str(), ext);
    }
    catch (const Base::Exception& e) {
        FC_ERR("Failed to store the geometry of " << getFullName() << ": " << e.what());
        Base::FileInfo(path).deleteFile();
    }
    catch (const Standard_Failure& e) {
        FC_ERR("Failed to serialize the geometry of " << getFullName() << ": "
                                                      << e.GetMessageString());
        Base::FileInfo(path).deleteFile();
    }
}

void PropertyPartShape::noteBlob(Base::Writer& writer) const
{
    if (!_blob)
        return;
    auto referrer = App::FileBlobManager::referrerOf(this);
    // The extension is the property's to give: nothing about the content says
    // whether it was written as ASCII BRep or as binary.
    referrer.ext = std::string(".") + shapeBlobExtension(writer);
    blobManager().noteReferenced(_blob, referrer);
}

void PropertyPartShape::dropBlob(const TopoDS_Shape& next)
{
    if (!_blob)
        return;
    const TopoDS_Shape& current = _Shape.getShape();
    // The location is canonicalized out of the file, so a shape over the same
    // TShape serializes to the same bytes wherever it sits. That is what makes
    // moving an object cost no write: the geometry it already stored is still
    // exactly what the next save would produce.
    if (!current.IsNull() && !next.IsNull() && current.IsPartner(next)
            && current.Orientation() == next.Orientation()) {
        return;
    }
    _blob.reset();
    _blobPlan.clear();
    _blobMotion = TopLoc_Location();
}

void PropertyPartShape::assignRestoredBlob(const App::FileBlobHandle& blob)
{
    // No value change and no parse: this hands over the file, and the geometry
    // inside it is read on first use. That is the deferred read the shape
    // count makes necessary -- see ensureRestored().
    _blob = blob;
    _PendingManager = nullptr;
}

void PropertyPartShape::serveFromBlob()
{
    // Cleared first, for the same reason the pending flag is: anything the
    // setValue below reaches must find a settled property, not a second serve.
    _RestoreHash.clear();
    // Held across the setValue below: the value about to be announced is
    // exactly this file's content, so the property keeps the file and the
    // next save has nothing to serialize.
    const App::FileBlobHandle blob = _blob;
    if (!blob)
        return;
    auto owner = Base::freecad_dynamic_cast<App::DocumentObject>(getContainer());

    FC_TRACE(getFullName() << " parsing " << blob->path());
    const ParsedShape* parsed = parseBlob(blobManager(), blob);
    const TopoDS_Shape geometry = parsed ? parsed->root : TopoDS_Shape();
    // What the file says it borrows, taken from the file itself. Without this
    // a reopened document could not tell whether its own files are still what
    // the next save would write, and would rewrite all of them.
    //
    // *** Kept aside rather than assigned here, because the setValue() below
    // goes through dropBlob(), which drops everything that describes the file
    // -- the plan and the motion along with the handle. Only the handle used
    // to be put back, and a stale empty plan is not a harmless one: it reads
    // as "this file borrows nothing", so a save that now has nothing to
    // borrow leaves the file alone with its references still in it.
    const std::string plan = parsed ? parsed->plan : std::string();

    const bool wasTouched = owner && owner->isTouched();
    {
        // Load-time conditions, exactly as serveFromStore() reproduces them:
        // observers see a restoring object, and an object does not come out
        // touched by having been served. A property with no owning object --
        // there is no document to mark modified -- needs neither.
        std::unique_ptr<Base::ObjectStatusLocker<App::ObjectStatus, App::DocumentObject>> guard;
        if (owner) {
            guard = std::make_unique<
                    Base::ObjectStatusLocker<App::ObjectStatus, App::DocumentObject>>(
                    App::ObjectStatus::Restore, owner);
        }
        auto elementMap = _Shape.resetElementMap();
        auto hasher = _Shape.Hasher;
        std::string ver = _Ver;

        TopoShape shape(locatedForRestore(geometry));
        shape.Hasher = hasher;
        shape.resetElementMap(elementMap);
        setValue(shape);
        _Ver = ver;
    }
    _blob = blob;
    _blobPlan = plan;
    // The three together are what says the file on disk is this value: the
    // shape held now is that file's geometry moved by the motion it was
    // restored with, so that is the motion a save keeping the file must
    // write again.
    _blobMotion = _RestoreMotion;
    if (owner && !wasTouched)
        owner->purgeTouched();
}

void PropertyPartShape::ensureRestored() const
{
    if (!_RestorePending)
        return;
    auto self = const_cast<PropertyPartShape*>(this);
    auto owner = Base::freecad_dynamic_cast<App::DocumentObject>(getContainer());
    if (!_RestoreHash.empty()) {
        // The blob arrives with the archive entries, which are drained after
        // the whole XML pass -- and the XML pass asks for this shape itself,
        // through the element map version check at the end of Restore(). Stay
        // pending until the file is here, exactly as the store branch does:
        // the property then reads as the null shape it is.
        //
        // No owning object is needed for this branch: the manager is the
        // document's, reached through whatever container the property has,
        // and a property the document itself owns (ForeignBaseShapes) comes
        // back this way.
        if (!_blob)
            return;
        // Cleared before serving: whatever runs below reads the property
        // again, and must find a settled state instead of re-entering.
        self->_RestorePending = false;
        self->serveFromBlob();
    }
    else if (!owner || !owner->getDocument()) {
        self->_RestorePending = false;
        return;
    }
    else if (_StorePos != PropertyShapeStore::NoPosition) {
        // The store arrives with the archive entries, which are drained
        // after the whole XML pass -- and the XML pass asks for this shape
        // itself, through the element map version check at the end of
        // Restore(). Stay pending until the store is here: the property then
        // reads as the null shape it is, which is exactly what a deferred
        // archive entry reads as at that moment (its pending flag is not
        // armed until the same drain).
        auto store = PropertyShapeStore::find(owner->getDocument());
        if (!store || !store->hasContent())
            return;
        // Cleared before serving: whatever runs below reads the property
        // again, and must find a settled state instead of re-entering.
        self->_RestorePending = false;
        self->serveFromStore();
    }
    else {
        self->_RestorePending = false;
        owner->getDocument()->restoreDeferredFile(self);
    }
    // The shape-content expansion Feature::onDocumentRestored() left for
    // the shape's arrival.
    if (auto feat = Base::freecad_dynamic_cast<Feature>(owner))
        feat->restoreShapeContents();
}

void PropertyPartShape::serveFromStore()
{
    const uint64_t pos = _StorePos;
    // Cleared first, for the same reason the pending flag is: anything the
    // setValue below reaches must find a settled property, not a second serve.
    _StorePos = PropertyShapeStore::NoPosition;
    auto owner = Base::freecad_dynamic_cast<App::DocumentObject>(getContainer());
    if (!owner || !owner->getDocument())
        return;
    auto store = PropertyShapeStore::find(owner->getDocument());
    if (!store) {
        FC_ERR("No shape store to serve " << getFullName() << " from");
        return;
    }

    // Load-time conditions, as the deferred archive serve reproduces them
    // (Document::restoreDeferredFile): observers see a restoring object, and
    // an object does not come out touched by having been served.
    const bool wasTouched = owner->isTouched();
    {
        Base::ObjectStatusLocker<App::ObjectStatus, App::DocumentObject> guard(
                App::ObjectStatus::Restore, owner);
        // The element map belongs to the property and not to the geometry:
        // two objects over one shared TShape carry different mapped names, so
        // it is taken off and put back around the value change.
        auto elementMap = _Shape.resetElementMap();
        auto hasher = _Shape.Hasher;
        std::string ver = _Ver;

        TopoShape shape(locatedForRestore(store->readShape(pos)));
        shape.Hasher = hasher;
        shape.resetElementMap(elementMap);
        setValue(shape);
        _Ver = ver;
    }
    if (!wasTouched)
        owner->purgeTouched();
}

void PropertyPartShape::cancelRestorePending()
{
    if (!_RestorePending)
        return;
    _RestorePending = false;
    // A store position is dropped the same way an archive entry is: the value
    // it would have produced has just been overwritten. So is a blob still
    // waiting to be handed over -- and the manager has to be told, or it
    // dispatches onto a property that has moved on.
    _StorePos = PropertyShapeStore::NoPosition;
    _RestoreHash.clear();
    if (_PendingManager) {
        _PendingManager->removePendingReferrer(this);
        _PendingManager = nullptr;
    }
    auto owner = Base::freecad_dynamic_cast<App::DocumentObject>(getContainer());
    if (owner && owner->getDocument())
        owner->getDocument()->cancelDeferredFile(this);
}

void PropertyPartShape::validateShape(App::DocumentObject *obj)
{
    // isRestoring(): a deferred serve (docs/DocumentLoad.md §14) runs
    // under the object's Restore status and must skip here exactly like
    // the load-time restore it stands in for.
    if (!obj || !obj->getDocument() || obj->isRestoring()
             || obj->getDocument()->testStatus(App::Document::Restoring))
        return;
    // A retained generation is evidence, not the owner's geometry: it is
    // neither fixed nor allowed to flag the owner invalid.
    if (Feature::isBaseShapeVersion(this))
        return;
    if (auto feat = Base::freecad_dynamic_cast<Part::Feature>(obj)) {
        if (_Shape.isNull()) {
            feat->InvalidShape.setValue(false);
            return;
        }
        if (feat->FixShape.getValue() != 0) {
            if (feat->FixShape.getValue() == 1) {
                if (_Shape.isValid()) {
                    feat->InvalidShape.setValue(false);
                    return;
                }
            }
            _Shape.fix();
        }
        if (feat->ValidateShape.getValue() && !_Shape.isValid())
            feat->InvalidShape.setValue(true);
        else
            feat->InvalidShape.setValue(false);
    }
}

void PropertyPartShape::setValue(const TopoShape& sh)
{
    // An unserved parked entry is dead: the value it would bring is
    // being overwritten. Never serve it after this.
    cancelRestorePending();
    // Announced before the blob is dropped: the owner's onBeforeChange may
    // retain the outgoing shape as a generation, and the file the last
    // save wrote for it goes along (Feature::onBeforeChange). The stand-in
    // the transaction takes there is a Copy(), which carries no blob.
    aboutToSetValue();
    dropBlob(sh.getShape());
    _Shape = sh;
    _ShapeNoName.setShape(sh.getShape(), true);
    _ShapeNoName.Tag = -1;
    auto obj = Base::freecad_dynamic_cast<App::DocumentObject>(getContainer());
    if(obj) {
        auto tag = obj->getID();
        if(_Shape.Tag && tag!=_Shape.Tag) {
            auto hasher = _Shape.Hasher?_Shape.Hasher:obj->getDocument()->getStringHasher();
            _Shape.reTagElementMap(tag,hasher);
        } else
            _Shape.Tag = obj->getID();
        if (!_Shape.Hasher && _Shape.hasChildElementMap()) {
            _Shape.Hasher = obj->getDocument()->getStringHasher();
            _Shape.hashChildMaps();
        }
        validateShape(obj);
    }
    hasSetValue();
    _Ver.clear();
}

void PropertyPartShape::setValue(const TopoDS_Shape& sh, bool resetElementMap)
{
    cancelRestorePending();
    // Announced first, see setValue(const TopoShape&).
    aboutToSetValue();
    dropBlob(sh);
    auto obj = dynamic_cast<App::DocumentObject*>(getContainer());
    if(obj)
        _Shape.Tag = obj->getID();
    _Shape.setShape(sh,resetElementMap);
    _ShapeNoName.setShape(sh, true);
    _ShapeNoName.Tag = -1;
    validateShape(obj);
    hasSetValue();
    _Ver.clear();
}

const TopoDS_Shape& PropertyPartShape::getValue() const
{
    ensureRestored();
    return _Shape.getShape();
}

TopoShape PropertyPartShape::getShape() const
{
    ensureRestored();
    _Shape.initCache(-1);
    auto res = _Shape;
    if (Feature::isElementMappingDisabled(getContainer()))
        return _ShapeNoName;
    else if (!res.Tag) {
        if (auto parent = Base::freecad_dynamic_cast<App::DocumentObject>(getContainer()))
            res.Tag = parent->getID();
    }
    return res;
}

const Data::ComplexGeoData* PropertyPartShape::getComplexData() const
{
    ensureRestored();
    _Shape.initCache(-1);
    if (Feature::isElementMappingDisabled(getContainer()))
        return &_ShapeNoName;
    return &_Shape;
}

Base::BoundBox3d PropertyPartShape::getBoundingBox() const
{
    ensureRestored();
    Base::BoundBox3d box;
    if (_Shape.getShape().IsNull())
        return box;
    try {
        // If the shape is empty an exception may be thrown
        Bnd_Box bounds;
        BRepBndLib::Add(_Shape.getShape(), bounds);
        bounds.SetGap(0.0);
        Standard_Real xMin, yMin, zMin, xMax, yMax, zMax;
        bounds.Get(xMin, yMin, zMin, xMax, yMax, zMax);

        box.MinX = xMin;
        box.MaxX = xMax;
        box.MinY = yMin;
        box.MaxY = yMax;
        box.MinZ = zMin;
        box.MaxZ = zMax;
    }
    catch (Standard_Failure&) {
    }

    return box;
}

void PropertyPartShape::setTransform(const Base::Matrix4D &rclTrf)
{
    ensureRestored();
    _Shape.setTransform(rclTrf);
}

Base::Matrix4D PropertyPartShape::getTransform() const
{
    ensureRestored();
    return _Shape.getTransform();
}

void PropertyPartShape::transformGeometry(const Base::Matrix4D &rclTrf)
{
    ensureRestored();
    // Unlike a placement this rewrites the geometry itself, so whatever was
    // stored for it no longer describes the value.
    _blob.reset();
    aboutToSetValue();
    _Shape.transformGeometry(rclTrf);
    hasSetValue();
}

PyObject *PropertyPartShape::getPyObject()
{
    Base::PyObjectBase* prop = static_cast<Base::PyObjectBase*>(getShape().getPyObject());
    if (prop)
        prop->setConst();
    return prop;
}

void PropertyPartShape::setPyObject(PyObject *value)
{
    if (PyObject_TypeCheck(value, &(TopoShapePy::Type))) {
        auto shape = *static_cast<TopoShapePy*>(value)->getTopoShapePtr();
        auto owner = dynamic_cast<App::DocumentObject*>(getContainer());
        if(owner && owner->getDocument()) {
            if(shape.Tag || shape.getElementMapSize()) {
                // We can't trust the meaning of the input shape tag, so we
                // remap anyway
                TopoShape res(owner->getID(),owner->getDocument()->getStringHasher(),shape.getShape());
                res.mapSubElement(shape);
                shape = res;
            }else{
                shape.Tag = owner->getID();
                shape.Hasher.reset();
            }
        }
        setValue(shape);
    }
    else {
        std::string error = std::string("type must be 'Shape', not ");
        error += value->ob_type->tp_name;
        THROWM(Base::TypeError, error)
    }
}

App::Property *PropertyPartShape::Copy() const
{
    ensureRestored();
    PropertyPartShape *prop = new PropertyPartShape();

    if (PartParams::getShapePropertyCopy()) {
        // makECopy() consume too much memory for complex geometry.
        prop->_Shape = this->_Shape.makECopy();
    } else
        prop->_Shape = this->_Shape;
    prop->_Ver = this->_Ver;
    return prop;
}

void PropertyPartShape::Paste(const App::Property &from)
{
    auto prop = Base::freecad_dynamic_cast<const PropertyPartShape>(&from);
    if(prop) {
        setValue(prop->_Shape);
        _Ver = prop->_Ver;
    }
}

unsigned int PropertyPartShape::getMemSize () const
{
    return _Shape.getMemSize();
}

void PropertyPartShape::getPaths(std::vector<App::ObjectIdentifier> &paths) const
{
    // The paths below seem to only there for expression completer. They are no
    // longer required because the completer will now dig into all Python attributes.
    (void)paths;

    // paths.push_back(App::ObjectIdentifier(getContainer()) << App::ObjectIdentifier::Component::SimpleComponent(getName())
    //                 << App::ObjectIdentifier::Component::SimpleComponent(App::ObjectIdentifier::String("ShapeType")));
    // paths.push_back(App::ObjectIdentifier(getContainer()) << App::ObjectIdentifier::Component::SimpleComponent(getName())
    //                 << App::ObjectIdentifier::Component::SimpleComponent(App::ObjectIdentifier::String("Orientation")));
    // paths.push_back(App::ObjectIdentifier(getContainer()) << App::ObjectIdentifier::Component::SimpleComponent(getName())
    //                 << App::ObjectIdentifier::Component::SimpleComponent(App::ObjectIdentifier::String("Length")));
    // paths.push_back(App::ObjectIdentifier(getContainer()) << App::ObjectIdentifier::Component::SimpleComponent(getName())
    //                 << App::ObjectIdentifier::Component::SimpleComponent(App::ObjectIdentifier::String("Area")));
    // paths.push_back(App::ObjectIdentifier(getContainer()) << App::ObjectIdentifier::Component::SimpleComponent(getName())
    //                 << App::ObjectIdentifier::Component::SimpleComponent(App::ObjectIdentifier::String("Volume")));
}

void PropertyPartShape::beforeSave(Base::Writer &writer) const
{
    ensureRestored();
    // Whatever position a previous save left is about to be answered by this
    // one -- and no save writes a store any more, so the answer is always the
    // blob below. A stale position would send Save() to a store this file
    // does not have. The ensureRestored() above is what took the geometry out
    // of the store this document may have been read from.
    _StorePos = PropertyShapeStore::NoPosition;
    auto owner = Base::freecad_dynamic_cast<App::DocumentObject>(getContainer());

    // The geometry as a file in the blob store, which is where it goes from
    // schema 5 on (docs/SharedShapeStorage.md sec 12.3). Done here rather than
    // in Save() because the entries are planned, named and pruned as a set --
    // this pass is the last point at which the set can still grow.
    if (usesBlob(writer)) {
        makeBlob(writer);
        noteBlob(writer);
    }

    _HasherIndex = 0;
    _SaveHasher = false;
    if(owner && !_Shape.isNull() && _Shape.getElementMapSize()>0) {
        auto ret = owner->getDocument()->addStringHasher(_Shape.Hasher);
        _HasherIndex = ret.second;
        _SaveHasher = ret.first;
        _Shape.beforeSave();
    }
}

void PropertyPartShape::Save (Base::Writer &writer) const
{
    ensureRestored();
    //See SaveDocFile(), RestoreDocFile()
    writer.Stream() << writer.ind() << "<Part";
    auto owner = dynamic_cast<App::DocumentObject*>(getContainer());
    if(owner && !_Shape.isNull()
             && _Shape.getElementMapSize()>0
             && !_Shape.Hasher.isNull()) {
        writer.Stream() << " HasherIndex=\"" << _HasherIndex << '"';
        if(_SaveHasher)
            writer.Stream() << " SaveHasher=\"1\"";
    }
    std::string version;
    // If exporting, do not export mapped element name, but still make a mark
    if(owner) {
        if(!owner->isExporting())
            version = _Ver.size()?_Ver:owner->getElementMapVersion(this);
    }else
        version = _Ver.size()?_Ver:_Shape.getElementMapVersion();
    writer.Stream() << " ElementMap=\"" << version << '"';

    // The top level location, taken off the geometry below and written here
    // instead (docs/SharedShapeStorage.md sec 11.4). Absent means the geometry
    // carries its own location, which is what every schema below 5 writes.
    const TopLoc_Location loc = stripsLocation(writer) ? _Shape.getShape().Location()
                                                       : TopLoc_Location();
    if (!isIdentityLocation(loc))
        writer.Stream() << " loc=\"" << locationToString(loc) << '"';
    // Present only when this shape borrows the file of another instance of
    // the same part: the motion from that instance's geometry to this one's,
    // applied to the geometry on the way in. Absent is the ordinary case and
    // costs nothing.
    if (!isIdentityLocation(_blobMotion))
        writer.Stream() << " motion=\"" << locationToString(_blobMotion) << '"';

    bool binary = writer.getMode("BinaryBrep");
    bool toXML = writer.getFileVersion()>1 && writer.isForceXML()>=(binary?3:2);
    if(_blob && usesBlob(writer)) {
        // The geometry is a file of its own in the blob store, named after
        // this property and shared by content. Noted again here for the same
        // reason PropertyFileIncluded does: it costs nothing, and a property
        // written through a path the pre-save pass does not walk would
        // otherwise lose its content.
        noteBlob(writer);
        writer.Stream() << " hash=\"" << _blob->hash() << "\"/>\n";
    } else if(_StorePos != PropertyShapeStore::NoPosition) {
        // The geometry is already in the document's store, written there by
        // the collect pass with every other shape; all that is left to say is
        // where. Read for the documents that have one; no save writes one any
        // more (docs/SharedShapeStorage.md sec 12.3).
        writer.Stream() << " store=\"" << PropertyShapeStore::propertyName()
            << "\" pos=\"" << _StorePos << "\"/>\n";
    } else if(!toXML) {
        writer.Stream() << " file=\""
            << writer.addFile(getFileName(binary?".bin":".brp"), this)
            << "\"/>\n";
    } else if(binary) {
        writer.Stream() << " binary=\"1\">\n";
        TopoShape(shapeForSave(writer)).exportBinary(writer.beginBase64Stream(), true);
        writer.endCharStream() <<  writer.ind() << "</Part>\n";
    } else {
        writer.Stream() << " brep=\"1\">\n";
        TopoShape(shapeForSave(writer)).exportBrep(writer.beginCharStream()<<'\n', true);
        writer.endCharStream() << '\n' << writer.ind() << "</Part>\n";
    }

    if(_SaveHasher) {
        if(!toXML && writer.getFileVersion()>1)
            _Shape.Hasher->setPersistenceFileName(getFileName(".Table").c_str());
        else
            _Shape.Hasher->setPersistenceFileName(0);
        _Shape.Hasher->Save(writer);
    }
    if(version.size()) {
        if(!toXML && writer.getFileVersion()>1)
            _Shape.setPersistenceFileName(getFileName(".Map").c_str());
        else
            _Shape.setPersistenceFileName(0);
        _Shape.Save(writer);
    }
}

std::string PropertyPartShape::getElementMapVersion(bool restored) const {
    if(restored)
        return _Ver;
    return PropertyComplexGeoData::getElementMapVersion(false);
}

void PropertyPartShape::Restore(Base::XMLReader &reader)
{
    reader.readElement("Part");

    auto owner = Base::freecad_dynamic_cast<App::DocumentObject>(getContainer());
    _Ver = "?";
    bool has_ver = reader.hasAttribute("ElementMap");
    if(has_ver)
        _Ver = reader.getAttribute("ElementMap");

    int hasher_idx = reader.getAttributeAsInteger("HasherIndex","-1");
    int save_hasher = reader.getAttributeAsInteger("SaveHasher","");

    // The location the geometry was written without. It outlives this call:
    // the geometry may be a deferred archive member or a position in the
    // store, and is put back wherever it does arrive.
    _RestoreLoc = TopLoc_Location();
    _RestoreMotion = TopLoc_Location();
    if (reader.hasAttribute("loc"))
        locationFromString(reader.getAttribute("loc"), _RestoreLoc);
    if (reader.hasAttribute("motion"))
        locationFromString(reader.getAttribute("motion"), _RestoreMotion);

    // Cleared before the branch below chooses: a stale hash would send
    // ensureRestored() looking for a file this restore never asked for.
    _RestoreHash.clear();

    TopoShape shape;
    // Whether this restore parked a value to be served later. Read at the end
    // instead of _RestorePending, which by then may already have been answered:
    // the element map check below asks for the shape, and a blob already in the
    // store is served on the spot.
    bool parked = false;

    if(reader.hasAttribute("hash")) {
        // The geometry is a file the manager owns. It hands the file over
        // once the entries have been drained (assignRestoredBlob), and the
        // parse waits for the first use -- which is what keeps a document
        // whose entry count is dominated by shapes cheap to open.
        _RestoreHash = reader.getAttribute("hash");
        if (!_RestoreHash.empty()) {
            FC_TRACE(getFullName() << " restores from blob " << _RestoreHash);
            _PendingManager = &blobManager();
            _PendingManager->addPendingReferrer(_RestoreHash, this);
            _RestorePending = true;
            parked = true;
        }
    } else if(reader.hasAttribute("store")) {
        // The shape is a position in the document's shared store, and the
        // store is a file that outlives the restore -- so nothing is read
        // here. ensureRestored() seeks to it the first time the value is
        // asked for, which is what keeps a large document's open cheap.
        _StorePos = reader.getAttributeAsUnsigned("pos");
        _RestorePending = true;
        parked = true;
    } else if(reader.hasAttribute("file")) {
        std::string file = reader.getAttribute("file");
        if (!file.empty()) {
            // initiate a file read
            reader.addFile(file.c_str(),this);
            parked = true;
        }
    } else if(reader.getAttributeAsInteger("binary","")) {
        shape.importBinary(reader.beginBase64Stream());
    } else if(reader.getAttributeAsInteger("brep","")) {
        shape.importBrep(reader.beginCharStream());
    }

    reader.readEndElement("Part");

    if(owner && hasher_idx>=0) {
        _Shape.Hasher = owner->getDocument()->getStringHasher(hasher_idx);
        if(save_hasher)
            _Shape.Hasher->Restore(reader);
    }

    if(has_ver) {
        // The file name here is not used for restore, but just a way to get
        // more useful error message if something wrong when restoring
        _Shape.setPersistenceFileName(getFileName().c_str());
        if(owner && owner->getDocument()->testStatus(App::Document::PartialDoc))
            _Shape.Restore(reader);
        else if(_Ver == "?" || _Ver.empty()) {
            // This indicate the shape is saved by legacy version without
            // element map info.
            if(owner) {
                // This will ask user for recompute after import
                owner->getDocument()->addRecomputeObject(owner);
            }
        }else{
            _Shape.Restore(reader);
            if (owner ? owner->checkElementMapVersion(this, _Ver.c_str())
                      : _Shape.checkElementMapVersion(_Ver.c_str())) {
                auto ver = owner?owner->getElementMapVersion(this):_Shape.getElementMapVersion();
                if(!owner || !owner->getNameInDocument() || !_Shape.getElementMapSize()) {
                    _Ver = ver;
                } else {
                    // version mismatch, signal for regenerating.
                    static const char *warnedDoc=0;
                    if(warnedDoc != owner->getDocument()->getName()) {
                        warnedDoc = owner->getDocument()->getName();
                        FC_WARN("Recomputation required for document '" << warnedDoc 
                                << "' on geo element version change in " << getFullName()
                                << ": " << _Ver << " -> " << ver);
                    }
                    owner->getDocument()->addRecomputeObject(owner);
                }
            }
        }
    } else if(owner && !owner->getDocument()->testStatus(App::Document::PartialDoc)) {
        static int buildElementMap = -1;
        if(buildElementMap<0) {
            static ParameterGrp::handle hGrp;
            if (!hGrp)
               hGrp = App::GetApplication().GetParameterGroupByPath(
                    "User parameter:BaseApp/Preferences/Mod/Part/General");
            buildElementMap = hGrp->GetBool("AutoElementMap",true)?1:0;
        }
        if(buildElementMap) {
            FC_WARN("Pending recompute for generating element map: " << owner->getFullName());
            owner->getDocument()->addRecomputeObject(owner);
        }
    }

    // Not when this restore parked something. The local shape is null on every
    // parking branch, so announcing it would either run cancelRestorePending()
    // and throw away the blob, store position or archive entry just
    // registered, or -- when the parked value has already been served above --
    // wipe the value that was just restored.
    if (!parked && (!shape.isNull() || !_Shape.isNull())) {
        setValue(locatedForRestore(shape.getShape()), false);
    }
}

void PropertyPartShape::afterRestore()
{
    if (_Shape.isRestoreFailed()) {
        // this cause GeoFeature::updateElementReference() to call
        // PropertyLinkBase::updateElementReferences() with reverse = true, in
        // order to try to regenerate the element map
        _Ver = "?";
    }
    else if (_Shape.getElementMapSize() == 0)
        _Shape.Hasher.reset();
    // What PropertyComplexGeoData::afterRestore() does, against the same
    // data getComplexData() would pick -- but without the ensureRestored()
    // that accessor runs: the restore-failure flag comes from the XML map
    // restore, and a shape parked by the deferred restore (§14) must not
    // be read from the archive just to check it.
    auto data = Feature::isElementMappingDisabled(getContainer())
        ? static_cast<Data::ComplexGeoData*>(&_ShapeNoName) : &_Shape;
    if (data->isRestoreFailed()) {
        data->resetRestoreFailure();
        auto owner = Base::freecad_dynamic_cast<App::DocumentObject>(getContainer());
        if (owner && owner->getDocument()
                  && !owner->getDocument()->testStatus(App::Document::PartialDoc))
            owner->getDocument()->addRecomputeObject(owner);
    }
    App::PropertyGeometry::afterRestore();
}

// The following function is copied from OCCT BRepTools.cxx and modified
// to disable saving of triangulation
//

static Standard_Boolean  BRepTools_Write(const TopoDS_Shape& Sh, const Standard_CString File)
{
  std::ofstream os;
  OSD_OpenStream(os, File, std::ios::out);

  if (!os.rdbuf()->is_open())
      return Standard_False;

  Standard_Boolean isGood = (os.good() && !os.eof());
  if(!isGood)
    return isGood;

  // See TopTools_FormatVersion of OCCT 7.6
  enum {
      VERSION_1 = 1,
      VERSION_2 = 2,
      VERSION_3 = 3
  };

  BRepTools_ShapeSet SS(Standard_False);
  SS.SetFormatNb(VERSION_1);
  // SS.SetProgress(PR);
  SS.Add(Sh);

  os << "DBRep_DrawableShape\n";  // for easy Draw read
  SS.Write(os);
  isGood = os.good();
  if(isGood )
    SS.Write(Sh,os);
  os.flush();
  isGood = os.good();

  errno = 0;
  os.close();
  isGood = os.good() && isGood && !errno;

  return isGood;
}

void PropertyPartShape::saveToFile(Base::Writer &writer) const
{
    // create a temporary file and copy the content to the zip stream
    // once the tmp. filename is known use always the same because otherwise
    // we may run into some problems on the Linux platform
    static Base::FileInfo fi(App::Application::getTempFileName());

    TopoDS_Shape myShape = _Shape.getShape();
    if (!BRepTools_Write(myShape,static_cast<Standard_CString>(fi.filePath().c_str()))) {
        // Note: Do NOT throw an exception here because if the tmp. file could
        // not be created we should not abort.
        // We only print an error message but continue writing the next files to the
        // stream...
        App::PropertyContainer* father = this->getContainer();
        if (father && father->isDerivedFrom(App::DocumentObject::getClassTypeId())) {
            App::DocumentObject* obj = static_cast<App::DocumentObject*>(father);
            Base::Console().Error("Shape of '%s' cannot be written to BRep file '%s'\n",
                obj->Label.getValue(),fi.filePath().c_str());
        }
        else {
            Base::Console().Error("Cannot save BRep file '%s'\n", fi.filePath().c_str());
        }

        std::stringstream ss;
        ss << "Cannot save BRep file '" << fi.filePath() << "'";
        writer.addError(ss.str());
    }

    Base::ifstream file(fi, std::ios::in | std::ios::binary);
    if (file) {
        std::streambuf* buf = file.rdbuf();
        writer.Stream() << buf;
    }

    file.close();
    // remove temp file
    fi.deleteFile();
}

TopoDS_Shape PropertyPartShape::loadFromFile(Base::Reader &reader)
{
    BRep_Builder builder;
    // create a temporary file and copy the content from the zip stream
    Base::FileInfo fi(App::Application::getTempFileName());

    // read in the ASCII file and write back to the file stream
    Base::ofstream file(fi, std::ios::out | std::ios::binary);
    unsigned long ulSize = 0;
    if (reader) {
        std::streambuf* buf = file.rdbuf();
        reader >> buf;
        file.flush();
        ulSize = buf->pubseekoff(0, std::ios::cur, std::ios::in);
    }
    file.close();

    // Read the shape from the temp file, if the file is empty the stored shape was already empty.
    // If it's still empty after reading the (non-empty) file there must occurred an error.
    TopoDS_Shape shape;
    if (ulSize > 0) {
        if (!BRepTools::Read(shape, static_cast<Standard_CString>(fi.filePath().c_str()), builder)) {
            // Note: Do NOT throw an exception here because if the tmp. created file could
            // not be read it's NOT an indication for an invalid input stream 'reader'.
            // We only print an error message but continue reading the next files from the
            // stream...
            App::PropertyContainer* father = this->getContainer();
            if (father && father->isDerivedFrom(App::DocumentObject::getClassTypeId())) {
                App::DocumentObject* obj = static_cast<App::DocumentObject*>(father);
                Base::Console().Error("BRep file '%s' with shape of '%s' seems to be empty\n",
                    fi.filePath().c_str(),obj->Label.getValue());
            }
            else {
                Base::Console().Warning("Loaded BRep file '%s' seems to be empty\n", fi.filePath().c_str());
            }
        }
    }

    // delete the temp file
    fi.deleteFile();
    return shape;
}

TopoDS_Shape PropertyPartShape::loadFromStream(Base::Reader &reader)
{
    TopoDS_Shape shape;
    try {
        reader.exceptions(std::istream::failbit | std::istream::badbit);
        BRep_Builder builder;
        BRepTools::Read(shape, reader, builder);
    }
    catch (const std::exception&) {
        if (!reader.eof())
            Base::Console().Warning("Failed to load BRep file %s\n", reader.getFileName().c_str());
    }
    return shape;
}

void PropertyPartShape::SaveDocFile (Base::Writer &writer) const
{
    ensureRestored();
    // Even if the shape is null, we shall still save it, so that there is
    // some content inside the file, or else, we'll get some annoying error
    // message when restoring.
    //
    // if (_Shape.getShape().IsNull())
    //     return;

    // Written at the identity from schema 5 on, with the location in the XML
    // instead: a move then leaves this member byte-identical, which the blob
    // manager's hash skip turns into no write at all.
    const TopoShape shape(shapeForSave(writer));
    Base::FileInfo finfo(writer.getCurrentFileName());
    if (finfo.hasExtension("bin")) {
        shape.exportBinary(writer.Stream(), true);
    }
    else {
        shape.exportBrep(writer.Stream(), true);
    }
}

void PropertyPartShape::RestoreDocFile(Base::Reader &reader)
{
    // Import-vs-setValue attribution, reported through the restore log
    // (the deferred serve showed the same call costing 18x more in a
    // GUI process than a console one -- this split is what names the
    // half that grew).
    static FC_DURATION dImport {0};
    static FC_DURATION dSet {0};
    static std::size_t nCalls;
    FC_TIME_INIT(tRestore);

    // save the element map
    auto elementMap = _Shape.resetElementMap();
    auto hasher = _Shape.Hasher;

    Base::FileInfo brep(reader.getFileName());
    TopoShape shape;
    if (brep.hasExtension("bin")) {
        shape.importBinary(reader);
    }
    else {
        shape.importBrep(reader);
    }
    // Back on before anything can see the value: outside a recompute
    // Feature::onChanged reads Placement out of the shape's own transform,
    // so geometry announced at the identity zeroes the placement.
    if (!_RestoreLoc.IsIdentity())
        shape.setShape(locatedForRestore(shape.getShape()), false);
    FC_DURATION_PLUS(dImport, tRestore);

    std::string ver = _Ver;
    // restore the element map
    shape.Hasher = hasher;
    shape.resetElementMap(elementMap);
    setValue(shape);
    _Ver = ver;
    FC_DURATION_PLUS(dSet, tRestore);
    if ((++nCalls % 2000) == 0)
        FC_LOG("shape restore split after " << nCalls << ": import "
                << dImport.count() << "s, setValue " << dSet.count() << 's');
}

// -------------------------------------------------------------------------

ShapeHistory::ShapeHistory(BRepBuilderAPI_MakeShape& mkShape, TopAbs_ShapeEnum type,
                           const TopoDS_Shape& newS, const TopoDS_Shape& oldS)
{
    reset(mkShape,type,newS,oldS);
}

void ShapeHistory::reset(BRepBuilderAPI_MakeShape& mkShape, TopAbs_ShapeEnum type,
                                 const TopoDS_Shape& newS, const TopoDS_Shape& oldS)
{
    shapeMap.clear();
    this->type = type;

    TopTools_IndexedMapOfShape newM, oldM;
    TopExp::MapShapes(newS, type, newM); // map containing all old objects of type "type"
    TopExp::MapShapes(oldS, type, oldM); // map containing all new objects of type "type"

    // Look at all objects in the old shape and try to find the modified object in the new shape
    for (int i=1; i<=oldM.Extent(); i++) {
        bool found = false;
        TopTools_ListIteratorOfListOfShape it;
        // Find all new objects that are a modification of the old object (e.g. a face was resized)
        for (it.Initialize(mkShape.Modified(oldM(i))); it.More(); it.Next()) {
            found = true;
            for (int j=1; j<=newM.Extent(); j++) { // one old object might create several new ones!
                if (newM(j).IsPartner(it.Value())) {
                    shapeMap[i-1].push_back(j-1); // adjust indices to start at zero
                    break;
                }
            }
        }

        // Find all new objects that were generated from an old object (e.g. a face generated from an edge)
        for (it.Initialize(mkShape.Generated(oldM(i))); it.More(); it.Next()) {
            found = true;
            for (int j=1; j<=newM.Extent(); j++) {
                if (newM(j).IsPartner(it.Value())) {
                    shapeMap[i-1].push_back(j-1);
                    break;
                }
            }
        }

        if (!found) {
            // Find all old objects that don't exist any more (e.g. a face was completely cut away)
            if (mkShape.IsDeleted(oldM(i))) {
                shapeMap[i-1] = std::vector<int>();
            }
            else {
                // Mop up the rest (will this ever be reached?)
                for (int j=1; j<=newM.Extent(); j++) {
                    if (newM(j).IsPartner(oldM(i))) {
                        shapeMap[i-1].push_back(j-1);
                        break;
                    }
                }
            }
        }
    }
}

void ShapeHistory::join(const ShapeHistory& newH)
{
    ShapeHistory join;

    for (ShapeHistory::MapList::const_iterator it = shapeMap.begin(); it != shapeMap.end(); ++it) {
        int old_shape_index = it->first;
        if (it->second.empty())
            join.shapeMap[old_shape_index] = ShapeHistory::List();
        for (ShapeHistory::List::const_iterator jt = it->second.begin(); jt != it->second.end(); ++jt) {
            ShapeHistory::MapList::const_iterator kt = newH.shapeMap.find(*jt);
            if (kt != newH.shapeMap.end()) {
                ShapeHistory::List& ary = join.shapeMap[old_shape_index];
                ary.insert(ary.end(), kt->second.begin(), kt->second.end());
            }
        }
    }

    shapeMap.swap(join.shapeMap);
}

// -------------------------------------------------------------------------

TYPESYSTEM_SOURCE(Part::PropertyShapeHistory , App::PropertyLists)

PropertyShapeHistory::PropertyShapeHistory() = default;

PropertyShapeHistory::~PropertyShapeHistory() = default;

void PropertyShapeHistory::setValue(const ShapeHistory& sh)
{
    aboutToSetValue();
    _lValueList.resize(1);
    _lValueList[0] = sh;
    hasSetValue();
}

void PropertyShapeHistory::setValues(const std::vector<ShapeHistory>& values)
{
    aboutToSetValue();
    _lValueList = values;
    hasSetValue();
}

PyObject *PropertyShapeHistory::getPyObject()
{
    return Py::new_reference_to(Py::None());
}

void PropertyShapeHistory::setPyObject(PyObject *)
{
}

void PropertyShapeHistory::Save (Base::Writer &) const
{
}

void PropertyShapeHistory::Restore(Base::XMLReader &)
{
}

void PropertyShapeHistory::SaveDocFile (Base::Writer &) const
{
}

void PropertyShapeHistory::RestoreDocFile(Base::Reader &)
{
}

App::Property *PropertyShapeHistory::Copy() const
{
    PropertyShapeHistory *p= new PropertyShapeHistory();
    p->_lValueList = _lValueList;
    return p;
}

void PropertyShapeHistory::Paste(const Property &from)
{
    aboutToSetValue();
    _lValueList = dynamic_cast<const PropertyShapeHistory&>(from)._lValueList;
    hasSetValue();
}

// -------------------------------------------------------------------------

TYPESYSTEM_SOURCE(Part::PropertyFilletEdges , App::PropertyLists)

PropertyFilletEdges::PropertyFilletEdges() = default;

PropertyFilletEdges::~PropertyFilletEdges() = default;

void PropertyFilletEdges::setValue(int id, double r1, double r2)
{
    setValue(FilletElement(id,r1,r2));
}

PyObject *PropertyFilletEdges::getPyObject()
{
    Py::List list(getSize());
    std::vector<FilletElement>::const_iterator it;
    int index = 0;
    for (it = _lValueList.begin(); it != _lValueList.end(); ++it) {
        Py::Tuple ent(3);
        ent.setItem(0, Py::Long(it->edgeid));
        ent.setItem(1, Py::Float(it->radius1));
        ent.setItem(2, Py::Float(it->radius2));
        list[index++] = ent;
    }

    return Py::new_reference_to(list);
}

FilletElement PropertyFilletEdges::getPyValue(PyObject *item) const
{
    FilletElement fe;
    if(!PyObject_TypeCheck(item, &PyTuple_Type))
        throw Base::TypeError();

    try {
        Py::Tuple ent(item);
        fe.edgeid = (int)Py::Long(ent.getItem(0));
        fe.radius1 = (double)Py::Float(ent.getItem(1));
        fe.radius2 = (double)Py::Float(ent.getItem(2));
    } catch (Py::Exception &) {
        Base::PyException::ThrowException();
    }
    return fe;
}

bool PropertyFilletEdges::saveXML(Base::Writer &writer) const {
    writer.Stream() << ">\n";
    for(auto &v : _lValueList)
        writer.Stream() << v.edgeid << ' ' << v.radius1 << ' ' << v.radius2 << '\n';
    return false;
}

void PropertyFilletEdges::restoreXML(Base::XMLReader &reader)
{
    unsigned count = reader.getAttributeAsUnsigned("count");
    auto &s = reader.beginCharStream();
    std::vector<FilletElement> values(count);
    for(auto &v : values) 
        s >> v.edgeid >> v.radius1 >> v.radius2;
    reader.endCharStream();
    setValue(std::move(values));
}

void PropertyFilletEdges::saveStream(Base::OutputStream &str) const
{
    for (const auto & it : _lValueList) {
        str << it.edgeid << it.radius1 << it.radius2;
    }
}

void PropertyFilletEdges::restoreStream(Base::InputStream &str, unsigned uCt)
{
    std::vector<FilletElement> values(uCt);
    for (auto & it : values) {
        str >> it.edgeid >> it.radius1 >> it.radius2;
    }
    setValue(std::move(values));
}

App::Property *PropertyFilletEdges::Copy() const
{
    PropertyFilletEdges *p= new PropertyFilletEdges();
    p->_lValueList = _lValueList;
    return p;
}

void PropertyFilletEdges::Paste(const Property &from)
{
    setValue(dynamic_cast<const PropertyFilletEdges&>(from)._lValueList);
}

// -------------------------------------------------------------------------

TYPESYSTEM_SOURCE(Part::PropertyShapeCache, App::Property);

App::Property *PropertyShapeCache::Copy(void) const {
    return new PropertyShapeCache();
}

void PropertyShapeCache::Paste(const App::Property &) {
    cache.clear();
}

void PropertyShapeCache::Save (Base::Writer &) const
{
}

void PropertyShapeCache::Restore(Base::XMLReader &)
{
}

PyObject *PropertyShapeCache::getPyObject() {
    Py::List res;
    for(auto &v : cache)
        res.append(Py::TupleN(Py::String(v.first),shape2pyshape(v.second)));
    return Py::new_reference_to(res);
}

void PropertyShapeCache::setPyObject(PyObject *value) {
    if(!value)
        return;
    if(value == Py_None) {
        cache.clear();
        return;
    }
    App::PropertyStringList prop;
    prop.setPyObject(value);
    for(const auto &sub : prop.getValues())
        cache.erase(sub);
}

#define SHAPE_CACHE_NAME "_Part_ShapeCache"
PropertyShapeCache *PropertyShapeCache::get(const App::DocumentObject *obj, bool create) {
    auto prop = Base::freecad_dynamic_cast<PropertyShapeCache>(
            obj->getDynamicPropertyByName(SHAPE_CACHE_NAME));
    if(prop && prop->getContainer()==obj)
        return prop;
    if(!create)
        return 0;

    prop = static_cast<PropertyShapeCache*>(
            const_cast<App::DocumentObject*>(obj)->addDynamicProperty("Part::PropertyShapeCache",
                SHAPE_CACHE_NAME,"Part","Shape cache",
                App::Prop_NoPersist|App::Prop_Output|App::Prop_Hidden));
    if(!prop) 
        FC_ERR("Failed to add shape cache for " << obj->getFullName());
    else
        prop->connChanged = const_cast<App::DocumentObject*>(obj)->signalEarlyChanged.connect(
                std::bind(&PropertyShapeCache::slotChanged,prop,sp::_1,sp::_2));
    return prop;
}

bool PropertyShapeCache::getShape(const App::DocumentObject *obj, TopoShape &shape, const char *subname) {
    if (PartParams::getDisableShapeCache())
        return false;
    auto prop = get(obj,false);
    if(!prop)
        return false;
    if(!subname) subname = "";
    auto it = prop->cache.find(subname);
    if(it!=prop->cache.end()) {
        shape = it->second;
        return !shape.isNull();
    }
    return false;
}

void PropertyShapeCache::setShape(
        const App::DocumentObject *obj, const TopoShape &shape, const char *subname) 
{
    if (PartParams::getDisableShapeCache())
        return;
    auto prop = get(obj,true);
    if(!prop)
        return;
    if(!subname) subname = "";
    prop->cache[subname] = shape;
}

void PropertyShapeCache::slotChanged(const App::DocumentObject &, const App::Property &prop) {
    auto propName = prop.getName();
    if(!propName) return;
    if(strcmp(propName,"Group")==0 || 
            strcmp(propName,"Shape")==0 ||
            strstr(propName,"Touched")!=0)
    {
        FC_LOG("clear shape cache on changed " << prop.getFullName());
        cache.clear();
    }
}

