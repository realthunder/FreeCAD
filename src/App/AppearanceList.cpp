/***************************************************************************
 *   Copyright (c) 2026 Zheng, Lei <realthunder.dev@gmail.com>              *
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

#include <algorithm>
#include <map>
#include <set>

#include <Base/Exception.h>
#include <Base/FileInfo.h>

#include "AppearanceList.h"
#include "MaterialXDocument.h"

using namespace App;
using namespace Base;

// ==================== helpers ====================
namespace {

/** Swap a colour between the two meanings of its alpha component
 *
 * In memory the component is an opacity, as upstream defines it; in a
 * document it is a transparency, in every file written before upstream
 * 1.1. Its own inverse, so the same call serves both directions and only
 * which side of a restore it sits on says which.
 */
void convertAlpha(Color &color)
{
    color.a = 1.0F - color.a;
}

/// What a string field costs, its contents included
std::size_t stringsMemSize(const std::vector<std::string> &field)
{
    std::size_t size = field.size() * sizeof(std::string);
    for (const auto &value : field) {
        size += value.size();
    }
    return size;
}

} // namespace

/// A palette's own bytes plus the hashes its records hold
std::size_t App::texturesMemSize(const std::vector<SurfaceTexture> &palette)
{
    std::size_t size = palette.size() * sizeof(SurfaceTexture);
    for (const auto &texture : palette) {
        for (const auto &hash : texture.maps)
            size += hash.size();
    }
    return size;
}

namespace {

//--------------------------------------------------------------------------
// The sparse fields
//
// An array is 0 or |overrides| long and sits in overrides order, so nothing
// here is indexed by a FACE number: a face is first turned into a position
// with overridePos(), which answers -1 for a face that takes the base.
//--------------------------------------------------------------------------

/// Resolve one entry of a sparse field. \a pos is a position among the
/// overrides, not a face.
template<class T>
inline const T &sparseAt(const std::vector<T> &values, int pos, const T &base)
{
    if (pos < 0 || values.empty())
        return base;
    return values[static_cast<std::size_t>(pos)];
}

/// Every entry of a sparse field, resolved against the base
template<class T>
std::vector<T> expandSparse(int count, const std::vector<uint32_t> &overrides,
                            const std::vector<T> &values, const T &base)
{
    std::vector<T> dense(static_cast<std::size_t>(count < 0 ? 0 : count), base);
    if (values.empty())
        return dense;
    for (std::size_t pos = 0; pos < overrides.size() && pos < values.size(); ++pos) {
        if (overrides[pos] < dense.size())
            dense[overrides[pos]] = values[pos];
    }
    return dense;
}

/// Materialise a sparse field so that one override can differ from the rest
template<class T>
void statedSparse(std::vector<T> &values, std::size_t n, const T &base)
{
    if (values.size() != n)
        values.assign(n, base);
}

/// Follow an override arriving at \a pos, in the arrays that state anything
template<class T>
void insertSparse(std::vector<T> &values, int pos, const T &base)
{
    if (!values.empty())
        values.insert(values.begin() + pos, base);
}

/// ... and one leaving
template<class T>
void eraseSparse(std::vector<T> &values, int pos)
{
    if (!values.empty())
        values.erase(values.begin() + pos);
}

/// Whether every override states the base's value, which is an array that
/// says nothing at all
template<class T>
bool saysNothing(const std::vector<T> &values, const T &base)
{
    for (const auto &value : values) {
        if (!(value == base))
            return false;
    }
    return true;
}

/** Keep the positions \a keep marks, in place
 *
 * The out != i guard is not an optimisation. Nothing is dropped until the
 * first false, so up to that point out IS i, and `s = std::move(s)` on a
 * std::string is a self-move: valid, unspecified, and in libstdc++ it
 * leaves the string EMPTY. Every field survived normalisation except the
 * three string ones, which silently lost their first entries.
 */
template<class T>
void compactSparse(std::vector<T> &values, const std::vector<bool> &keep)
{
    if (values.empty())
        return;
    std::size_t out = 0;
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (!keep[i])
            continue;
        if (out != i)
            values[out] = std::move(values[i]);
        ++out;
    }
    values.resize(out);
}

/// Write one entry of a sparse field, stating the array only if the value
/// is new. \a pos is a position, and the override is already there.
template<class T>
bool setSparseAt(std::vector<T> &values, int pos, std::size_t n, const T &value, const T &base)
{
    if (sparseAt(values, pos, base) == value)
        return false;
    statedSparse(values, n, base);
    values[static_cast<std::size_t>(pos)] = value;
    return true;
}

//--------------------------------------------------------------------------
// The dense form
//
// What every encoding that states one entry at a time lands, and what the
// compatible ones are written from. Fields are 0, 1 or count long here.
//--------------------------------------------------------------------------

/// Resolve one entry of a field that may be 0, 1 or count long
template<class T>
inline const T &fieldAt(const std::vector<T> &values, int idx, const T &def)
{
    if (values.empty())
        return def;
    return values.size() == 1 ? values.front() : values[idx];
}

/// Collapse a dense field to the smallest of 0, 1 and its current length
template<class T>
void collapseField(std::vector<T> &values, const T &def)
{
    if (values.empty())
        return;
    const T &first = values.front();
    for (std::size_t i = 1; i < values.size(); ++i) {
        if (!(values[i] == first))
            return;
    }
    // swap rather than resize: a field that has just been read from a
    // 10,000 entry document should give the memory back, not merely stop
    // counting it
    if (first == def) {
        std::vector<T>().swap(values);
    }
    else if (values.size() > 1) {
        std::vector<T>(1, first).swap(values);
    }
}

//--------------------------------------------------------------------------
// The palette+index field
//
// The same operations the sparse helpers above provide, over a pair of
// vectors instead of one. The invariant every one of them restores: an
// empty index says every override takes the BASE's texture (and then the
// palette is empty too), and a stated one is exactly |overrides| long with
// every value addressing the palette.
//--------------------------------------------------------------------------

/// The slot holding \a value, appending it if the palette does not have it
uint16_t paletteSlot(std::vector<SurfaceTexture> &palette, const SurfaceTexture &value)
{
    for (std::size_t i = 0; i < palette.size(); ++i) {
        if (palette[i] == value)
            return static_cast<uint16_t>(i);
    }
    if (palette.size() >= AppearanceList::MaxPaletteSize)
        throw Base::ValueError("too many distinct textures");
    palette.push_back(value);
    return static_cast<uint16_t>(palette.size() - 1);
}

/// Resolve one override's texture; \a pos is a position, not a face
const SurfaceTexture &sparseTextureAt(const std::vector<SurfaceTexture> &palette,
                                      const std::vector<uint16_t> &index,
                                      int pos, const SurfaceTexture &base)
{
    if (pos < 0 || index.empty())
        return base;
    if (pos >= static_cast<int>(index.size()))
        return base;
    const std::size_t slot = index[static_cast<std::size_t>(pos)];
    return slot < palette.size() ? palette[slot] : base;
}

/// State the index so that one override can differ from the rest
void statedTexture(std::vector<SurfaceTexture> &palette, std::vector<uint16_t> &index,
                   std::size_t n, const SurfaceTexture &base)
{
    if (index.size() == n)
        return;
    // by value into the palette first: the slot is appended, and the assign
    // below must not read a reference into a buffer it reallocates
    const uint16_t slot = paletteSlot(palette, base);
    index.assign(n, slot);
}

/// Rebuild the pair into its normal form: first-use order, and gone
/// entirely when every override takes the base's texture
void collapseTexture(std::vector<SurfaceTexture> &palette, std::vector<uint16_t> &index,
                     std::size_t n, const SurfaceTexture &base)
{
    if (index.empty() || n == 0) {
        // swap rather than clear: a palette read from a large document
        // should give the memory back, not merely stop counting it
        std::vector<SurfaceTexture>().swap(palette);
        std::vector<uint16_t>().swap(index);
        return;
    }
    // Renumber into first-use order, which drops both the slots nothing
    // points at any more and any duplicate a caller wrote
    std::vector<SurfaceTexture> used;
    std::vector<uint16_t> renumbered;
    renumbered.reserve(index.size());
    bool saysSomething = false;
    for (uint16_t slot : index) {
        const SurfaceTexture &value = slot < palette.size() ? palette[slot] : base;
        if (!(value == base))
            saysSomething = true;
        renumbered.push_back(paletteSlot(used, value));
    }
    if (!saysSomething) {
        std::vector<SurfaceTexture>().swap(palette);
        std::vector<uint16_t>().swap(index);
        return;
    }
    used.swap(palette);
    renumbered.swap(index);
}

/// Follow an override arriving at \a pos, in an index that states anything
void insertTexture(std::vector<SurfaceTexture> &palette, std::vector<uint16_t> &index,
                   int pos, const SurfaceTexture &base)
{
    if (index.empty())
        return;
    const uint16_t slot = paletteSlot(palette, base);
    index.insert(index.begin() + pos, slot);
}

/// Lay a whole run in, building the palette from what is distinct in it
void assignPalette(std::vector<SurfaceTexture> &palette, std::vector<uint16_t> &index,
                   const std::vector<SurfaceTexture> &values)
{
    std::vector<SurfaceTexture>().swap(palette);
    std::vector<uint16_t>().swap(index);
    index.reserve(values.size());
    for (const auto &value : values)
        index.push_back(paletteSlot(palette, value));
}

/// The DENSE pair collapsed to the 0/1/count form the compatible encodings
/// and every document written before the base state it in
void collapseDenseTexture(std::vector<SurfaceTexture> &palette, std::vector<uint16_t> &index,
                          int count, const SurfaceTexture &def)
{
    if (palette.empty() || count == 0) {
        std::vector<SurfaceTexture>().swap(palette);
        std::vector<uint16_t>().swap(index);
        return;
    }
    if (!index.empty()) {
        std::vector<SurfaceTexture> used;
        std::vector<uint16_t> renumbered;
        renumbered.reserve(index.size());
        for (uint16_t slot : index)
            renumbered.push_back(paletteSlot(used, slot < palette.size() ? palette[slot] : def));
        used.swap(palette);
        renumbered.swap(index);
        if (palette.size() > 1)
            return;   // genuinely varies: the index earns its two bytes
        std::vector<uint16_t>().swap(index);
    }
    if (palette.front() == def)
        std::vector<SurfaceTexture>().swap(palette);
    else if (palette.size() > 1)
        std::vector<SurfaceTexture>(1, palette.front()).swap(palette);
}

} // namespace

/// The diffuse colour a whole material stores: its transparency field is the
/// truth and the alpha holds its complement (see the storage note in the
/// header).
Color AppearanceList::storedDiffuse(const MaterialAppearance &mat)
{
    Color color = mat.diffuseColor;
    color.setTransparency(mat.transparency);
    return color;
}

/// The finish a whole material stores: clamped, so that everything reading
/// this property back gets a record it can actually draw, whatever a caller
/// assembled by hand or a file happened to state
SurfaceFinish AppearanceList::storedFinish(const SurfaceFinish &finish)
{
    SurfaceFinish stored = finish;
    stored.normalize();
    return stored;
}

/// The texture a whole material stores, clamped for the same reason -- and
/// so that two records differing only in a transform nobody stated land in
/// one palette slot rather than two
SurfaceTexture AppearanceList::storedTexture(const SurfaceTexture &texture)
{
    SurfaceTexture stored = texture;
    stored.normalize();
    return stored;
}

// ==================== blobs ====================
std::string AppearanceList::insertTextureFile(const char *path, const char *extension)
{
    if (!path || !path[0]) {
        return {};
    }
    FileBlobManager &manager =
        _manager ? *_manager : FileBlobManager::defaultManager();
    FileBlobHandle blob = manager.insertFile(path, extension);
    if (!blob) {
        return {};
    }
    // Held here, keyed by what a slot will name it by. Content already in
    // the store comes back as the same handle, so importing one image twice
    // costs one file and one entry.
    const std::string hash = blob->hash();
    bd().textureBlobs[hash] = std::move(blob);
    return hash;
}

std::string AppearanceList::getTextureFile(const std::string &hash) const
{
    const auto it = rd().textureBlobs.find(hash);
    return it == rd().textureBlobs.end() ? std::string() : it->second->path();
}

void AppearanceList::noteTextureBlobs(FileBlobManager &manager,
                                            const BlobReferrer &referrer) const
{
    ensureNormalized();
    // The base's record and the overriding faces': a uniform texture lives
    // in the base and in no palette slot at all, so a save that walked only
    // the palette would leave its files behind
    std::vector<SurfaceTexture> named = rd().texturePalette;
    named.push_back(rd().base.texture);
    for (const auto &value : named) {
        for (uint8_t slot = 0; slot < SurfaceTexture::SlotCount; ++slot) {
            const auto it = rd().textureBlobs.find(value.maps[slot]);
            if (value.maps[slot].empty() || it == rd().textureBlobs.end()) {
                continue;
            }
            // The referrer NAMES the file, so each slot passes its own --
            // "Box.ShapeAppearance.normal" rather than a numbered collision.
            // Noting the same blob again adds to its referrer list, which is
            // what two slots over one content should do.
            BlobReferrer named = referrer;
            if (!named.name.empty()) {
                named.name += ".";
                named.name += SurfaceTexture::slotName(slot);
            }
            manager.noteReferenced(it->second, named);
        }
    }
    // The MaterialX manifests and every file they name. A manifest is held
    // like a map is, and its children are only known through it, so an
    // entry whose manifest has not arrived has nothing more to note -- and
    // nothing more to lose, since it cannot have been saved from here.
    for (const auto &hash : materialXHashes()) {
        const auto it = rd().textureBlobs.find(hash);
        if (it == rd().textureBlobs.end()) {
            continue;
        }
        BlobReferrer named = referrer;
        if (!named.name.empty()) {
            named.name += ".materialx";
        }
        named.ext = ".manifest";
        manager.noteReferenced(it->second, named);
        MaterialXDocument manifest;
        if (!MaterialXDocument::readFile(it->second->path(), manifest)) {
            continue;
        }
        for (const auto &file : manifest.files) {
            const auto child = rd().textureBlobs.find(file.hash);
            if (file.hash.empty() || child == rd().textureBlobs.end()) {
                continue;
            }
            // Named after what the document calls the file, so an unpacked
            // project shows brass_color.jpg and not a hash
            BlobReferrer entry = referrer;
            Base::FileInfo fi(file.name);
            const std::string stem = fi.fileNamePure();
            if (!entry.name.empty() && !stem.empty()) {
                entry.name += "." + stem;
            }
            const std::string ext = fi.extension();
            entry.ext = ext.empty() ? std::string() : "." + ext;
            manager.noteReferenced(child->second, entry);
        }
    }
}

std::vector<std::string> AppearanceList::materialXHashes() const
{
    std::vector<std::string> hashes;
    auto add = [&hashes](const std::string &hash) {
        if (!hash.empty() && std::find(hashes.begin(), hashes.end(), hash) == hashes.end()) {
            hashes.push_back(hash);
        }
    };
    add(rd().base.materialx);
    for (const auto &hash : rd().materialx) {
        add(hash);
    }
    return hashes;
}

std::vector<std::string> AppearanceList::materialXChildren(const std::string &manifestHash) const
{
    const auto it = rd().textureBlobs.find(manifestHash);
    if (it == rd().textureBlobs.end()) {
        return {};
    }
    MaterialXDocument manifest;
    if (!MaterialXDocument::readFile(it->second->path(), manifest)) {
        return {};
    }
    return manifest.hashes();
}

void AppearanceList::assignRestoredBlob(const FileBlobHandle &blob)
{
    // No value change: this completes the restore of a value the document
    // already had, and touching it here would mark a document modified just
    // by being opened.
    //
    // BY HASH, never by arrival order. addPendingReferrer serves a hash
    // whose content has already been read IMMEDIATELY and queues the rest,
    // so a referrer asking for an unread h1 and then an already-read h2 is
    // handed h2 first -- invisible with one blob per property, a silent
    // mis-assignment with five (docs/ShapeAppearanceDesign.md 10.2). Two
    // slots over one content share one hash and both are served, which is
    // what makes a repeated call harmless.
    if (!blob) {
        return;
    }
    bd().textureBlobs[blob->hash()] = blob;
}

/// Whether content is held for every hash the palette names, which is what
/// tells a restore it has nothing left queued
bool AppearanceList::holdsEveryNamedBlob() const
{
    std::vector<SurfaceTexture> named = rd().texturePalette;
    named.push_back(rd().base.texture);
    for (const auto &value : named) {
        for (const auto &hash : value.maps) {
            if (!hash.empty() && rd().textureBlobs.find(hash) == rd().textureBlobs.end()) {
                return false;
            }
        }
    }
    for (const auto &hash : materialXHashes()) {
        if (rd().textureBlobs.find(hash) == rd().textureBlobs.end()) {
            return false;
        }
        // The manifest is here, so its children are known; they count too
        for (const auto &child : materialXChildren(hash)) {
            if (rd().textureBlobs.find(child) == rd().textureBlobs.end()) {
                return false;
            }
        }
    }
    return true;
}

void AppearanceList::holdTextureBlob(const FileBlobHandle &blob)
{
    if (blob) {
        bd().textureBlobs[blob->hash()] = blob;
    }
}

std::vector<std::string> AppearanceList::getTextureHashes() const
{
    std::vector<std::string> hashes;
    hashes.reserve(rd().textureBlobs.size());
    for (const auto &entry : rd().textureBlobs) {
        hashes.push_back(entry.first);
    }
    return hashes;
}

// ==================== prune ====================
/** Drop the handles no palette slot names any more
 *
 * Deliberately NOT called from normalize(). A caller has to insert content
 * before it can name the hash, so between insertTextureFile() and the write
 * that names it there is always a handle no slot points at -- and normalize
 * runs on any read, including one inside that window. So this is called
 * only where the palette has just been stated in full: the whole-list
 * assignments and the restore.
 */
void AppearanceList::pruneTextureBlobs()
{
    if (rd().textureBlobs.empty()) {
        return;
    }
    std::set<std::string> named;
    std::vector<SurfaceTexture> records = rd().texturePalette;
    // The base's record names content exactly as an override's does
    records.push_back(rd().base.texture);
    for (const auto &value : records) {
        for (const auto &hash : value.maps) {
            if (!hash.empty()) {
                named.insert(hash);
            }
        }
    }
    for (const auto &hash : materialXHashes()) {
        named.insert(hash);
        for (const auto &child : materialXChildren(hash)) {
            named.insert(child);
        }
    }
    Data &d = bd();
    for (auto it = d.textureBlobs.begin(); it != d.textureBlobs.end();) {
        it = named.count(it->first) ? std::next(it) : d.textureBlobs.erase(it);
    }
}

// ==================== default ====================
const MaterialAppearance &AppearanceList::defaultMaterial()
{
    static const MaterialAppearance def;
    return def;
}

// ==================== the fields, as stored ====================

int AppearanceList::getSize() const
{
    return rd().count;
}

bool AppearanceList::isPBR() const
{
    return rd().pbr;
}

bool AppearanceList::isFollowingMaterial() const
{
    return rd().follow;
}

const std::vector<Color> &AppearanceList::getAmbientOverrides() const
{
    ensureNormalized();
    return rd().ambient;
}

const std::vector<Color> &AppearanceList::getDiffuseOverrides() const
{
    ensureNormalized();
    return rd().diffuse;
}

const std::vector<Color> &AppearanceList::getSpecularOverrides() const
{
    ensureNormalized();
    return rd().specular;
}

const std::vector<Color> &AppearanceList::getEmissiveOverrides() const
{
    ensureNormalized();
    return rd().emissive;
}

const std::vector<float> &AppearanceList::getShininessOverrides() const
{
    ensureNormalized();
    return rd().shininess;
}

const std::vector<std::string> &AppearanceList::getImageOverrides() const
{
    ensureNormalized();
    return rd().image;
}

const std::vector<std::string> &AppearanceList::getImagePathOverrides() const
{
    ensureNormalized();
    return rd().imagePath;
}

const std::vector<std::string> &AppearanceList::getUuidOverrides() const
{
    ensureNormalized();
    return rd().uuid;
}

const std::vector<std::string> &AppearanceList::getMaterialXOverrides() const
{
    ensureNormalized();
    return rd().materialx;
}

const std::vector<int8_t> &AppearanceList::getTypeOverrides() const
{
    ensureNormalized();
    return rd().type;
}

const std::vector<SurfaceFinish> &AppearanceList::getFinishOverrides() const
{
    ensureNormalized();
    return rd().finish;
}

const std::vector<SurfaceTexture> &AppearanceList::getTexturePalette() const
{
    ensureNormalized();
    return rd().texturePalette;
}

const std::vector<uint16_t> &AppearanceList::getTextureIndex() const
{
    ensureNormalized();
    return rd().textureIndex;
}

// ==================== the fields, resolved ====================
//
// A fresh vector every time, so these are what a consumer of a whole field
// asks for ONCE. Everything that reads a single entry goes to the indexed
// getters, which touch no memory that is not already there.

std::vector<Color> AppearanceList::getAmbientColors() const
{
    ensureNormalized();
    return expandSparse(rd().count, rd().overrides, rd().ambient, rd().base.ambientColor);
}

std::vector<Color> AppearanceList::getDiffuseColors() const
{
    ensureNormalized();
    return expandSparse(rd().count, rd().overrides, rd().diffuse, rd().base.diffuseColor);
}

std::vector<Color> AppearanceList::getSpecularColors() const
{
    ensureNormalized();
    return expandSparse(rd().count, rd().overrides, rd().specular, rd().base.specularColor);
}

std::vector<Color> AppearanceList::getEmissiveColors() const
{
    ensureNormalized();
    return expandSparse(rd().count, rd().overrides, rd().emissive, rd().base.emissiveColor);
}

std::vector<float> AppearanceList::getShininessValues() const
{
    ensureNormalized();
    return expandSparse(rd().count, rd().overrides, rd().shininess, rd().base.shininess);
}

std::vector<std::string> AppearanceList::getImages() const
{
    ensureNormalized();
    return expandSparse(rd().count, rd().overrides, rd().image, rd().base.image);
}

std::vector<std::string> AppearanceList::getImagePaths() const
{
    ensureNormalized();
    return expandSparse(rd().count, rd().overrides, rd().imagePath, rd().base.imagePath);
}

std::vector<std::string> AppearanceList::getUuids() const
{
    ensureNormalized();
    return expandSparse(rd().count, rd().overrides, rd().uuid, rd().base.uuid);
}

std::vector<std::string> AppearanceList::getMaterialXs() const
{
    ensureNormalized();
    return expandSparse(rd().count, rd().overrides, rd().materialx, rd().base.materialx);
}

std::vector<int8_t> AppearanceList::getTypes() const
{
    ensureNormalized();
    return expandSparse(rd().count, rd().overrides, rd().type,
                        static_cast<int8_t>(rd().base.getType()));
}

std::vector<SurfaceFinish> AppearanceList::getFinishes() const
{
    ensureNormalized();
    return expandSparse(rd().count, rd().overrides, rd().finish, rd().base.finish);
}

void AppearanceList::getTextures(std::vector<SurfaceTexture> &palette,
                               std::vector<uint16_t> &index) const
{
    ensureNormalized();
    std::vector<SurfaceTexture>().swap(palette);
    std::vector<uint16_t>().swap(index);
    if (rd().count == 0)
        return;
    if (rd().textureIndex.empty()) {
        // Uniform: the base's record, in the 0-or-1 palette shape the
        // compatible encodings and the companion element expect
        if (!(rd().base.texture == defaultMaterial().texture))
            palette.push_back(rd().base.texture);
        return;
    }
    // One slot per ENTRY, built by walking the overrides in step with the
    // faces they name
    const uint16_t baseSlot = paletteSlot(palette, rd().base.texture);
    index.assign(static_cast<std::size_t>(rd().count), baseSlot);
    for (std::size_t pos = 0; pos < rd().overrides.size(); ++pos) {
        const std::size_t face = rd().overrides[pos];
        if (face < index.size()) {
            index[face] = paletteSlot(
                    palette, sparseTextureAt(rd().texturePalette, rd().textureIndex,
                                             static_cast<int>(pos), rd().base.texture));
        }
    }
    collapseDenseTexture(palette, index, rd().count, defaultMaterial().texture);
}

bool AppearanceList::hasTextureOrCard() const
{
    ensureNormalized();
    return hasImage() || !rd().base.uuid.empty() || !rd().uuid.empty();
}

bool AppearanceList::hasImage() const
{
    ensureNormalized();
    return !rd().base.image.empty() || !rd().image.empty()
        || !rd().base.imagePath.empty() || !rd().imagePath.empty();
}

bool AppearanceList::hasFinish() const
{
    // Normalised, so a finish written and then cleared answers false rather
    // than "there is still an array there"
    ensureNormalized();
    return !(rd().base.finish == defaultMaterial().finish) || !rd().finish.empty();
}

bool AppearanceList::hasTexture() const
{
    // A palette entry survives collapse only while some override resolves
    // to it, so the base and an empty palette are the whole answer
    ensureNormalized();
    return !(rd().base.texture == defaultMaterial().texture) || !rd().texturePalette.empty();
}

bool AppearanceList::hasMaterialX() const
{
    ensureNormalized();
    return !rd().base.materialx.empty() || !rd().materialx.empty();
}

// ==================== storage ====================
void AppearanceList::touchFields()
{
    wd().normalized = false;
}

void AppearanceList::normalize() const
{
    // Nothing stored is already the normal form, and writing through nd()
    // here would scribble on the shared empty value it answers with
    if (_data.isNull()) {
        return;
    }
    Data &d = nd();
    if (d.count == 0) {
        d.base = defaultMaterial();
        d.base.pbr = d.pbr;
        d.base.specularColor = specularDefault();
        d.base.shininess = shininessDefault();
        std::vector<uint32_t>().swap(d.overrides);
        std::vector<Color>().swap(d.ambient);
        std::vector<Color>().swap(d.diffuse);
        std::vector<Color>().swap(d.specular);
        std::vector<Color>().swap(d.emissive);
        std::vector<float>().swap(d.shininess);
        std::vector<std::string>().swap(d.image);
        std::vector<std::string>().swap(d.imagePath);
        std::vector<std::string>().swap(d.uuid);
        std::vector<std::string>().swap(d.materialx);
        std::vector<int8_t>().swap(d.type);
        std::vector<SurfaceFinish>().swap(d.finish);
        std::vector<SurfaceTexture>().swap(d.texturePalette);
        std::vector<uint16_t>().swap(d.textureIndex);
        d.normalized = true;
        return;
    }

    // Every entry an override AND every one of them agreeing is the base
    // saying it: a field nothing disagrees about belongs to the object
    // rather than to each of its faces, and that is what keeps a whole
    // field landed dense from costing one value per face.
    //
    // Before the compaction below, not after: folding a field into the base
    // is exactly what can leave an override with nothing left to say.
    const bool everyFace = d.overrides.size() == static_cast<std::size_t>(d.count);
    auto fold = [everyFace](auto &field, auto &baseValue) {
        if (!everyFace || field.empty())
            return;
        for (std::size_t i = 1; i < field.size(); ++i) {
            if (!(field[i] == field.front()))
                return;
        }
        baseValue = field.front();
        field.clear();
    };
    fold(d.ambient, d.base.ambientColor);
    fold(d.diffuse, d.base.diffuseColor);
    fold(d.specular, d.base.specularColor);
    fold(d.emissive, d.base.emissiveColor);
    fold(d.shininess, d.base.shininess);
    fold(d.image, d.base.image);
    fold(d.imagePath, d.base.imagePath);
    fold(d.uuid, d.base.uuid);
    fold(d.materialx, d.base.materialx);
    if (everyFace && !d.type.empty()) {
        bool uniform = true;
        for (std::size_t i = 1; i < d.type.size() && uniform; ++i)
            uniform = d.type[i] == d.type.front();
        if (uniform) {
            setMaterialType(d.base, d.type.front());
            d.type.clear();
        }
    }
    fold(d.finish, d.base.finish);
    if (everyFace && !d.textureIndex.empty()) {
        bool uniform = true;
        for (std::size_t i = 1; i < d.textureIndex.size() && uniform; ++i)
            uniform = d.textureIndex[i] == d.textureIndex.front();
        if (uniform) {
            d.base.texture = sparseTextureAt(d.texturePalette, d.textureIndex, 0, d.base.texture);
            std::vector<SurfaceTexture>().swap(d.texturePalette);
            std::vector<uint16_t>().swap(d.textureIndex);
        }
    }

    // An array whose every entry is the base's says nothing at all
    auto drop = [](auto &field, const auto &baseValue) {
        if (!field.empty() && saysNothing(field, baseValue))
            field.clear();
    };
    drop(d.ambient, d.base.ambientColor);
    drop(d.diffuse, d.base.diffuseColor);
    drop(d.specular, d.base.specularColor);
    drop(d.emissive, d.base.emissiveColor);
    drop(d.shininess, d.base.shininess);
    drop(d.image, d.base.image);
    drop(d.imagePath, d.base.imagePath);
    drop(d.uuid, d.base.uuid);
    drop(d.materialx, d.base.materialx);
    drop(d.type, static_cast<int8_t>(d.base.getType()));
    drop(d.finish, d.base.finish);
    collapseTexture(d.texturePalette, d.textureIndex, d.overrides.size(), d.base.texture);

    // An override that no longer differs from the base in ANY field is not
    // an override -- and a list of one entry cannot have one at all, since
    // that entry IS what the object looks like (12.6).
    const std::size_t n = d.overrides.size();
    std::vector<bool> keep(n, d.count > 1);
    for (std::size_t pos = 0; pos < n; ++pos) {
        if (!keep[pos])
            continue;
        keep[pos] = !(sparseAt(d.ambient, int(pos), d.base.ambientColor) == d.base.ambientColor)
            || !(sparseAt(d.diffuse, int(pos), d.base.diffuseColor) == d.base.diffuseColor)
            || !(sparseAt(d.specular, int(pos), d.base.specularColor) == d.base.specularColor)
            || !(sparseAt(d.emissive, int(pos), d.base.emissiveColor) == d.base.emissiveColor)
            || sparseAt(d.shininess, int(pos), d.base.shininess) != d.base.shininess
            || sparseAt(d.image, int(pos), d.base.image) != d.base.image
            || sparseAt(d.imagePath, int(pos), d.base.imagePath) != d.base.imagePath
            || sparseAt(d.uuid, int(pos), d.base.uuid) != d.base.uuid
            || sparseAt(d.materialx, int(pos), d.base.materialx) != d.base.materialx
            || sparseAt(d.type, int(pos), static_cast<int8_t>(d.base.getType()))
                    != static_cast<int8_t>(d.base.getType())
            || !(sparseAt(d.finish, int(pos), d.base.finish) == d.base.finish)
            || !(sparseTextureAt(d.texturePalette, d.textureIndex, int(pos), d.base.texture)
                    == d.base.texture);
    }
    std::size_t kept = 0;
    for (std::size_t pos = 0; pos < n; ++pos)
        kept += keep[pos] ? 1 : 0;
    if (kept != n) {
        compactSparse(d.overrides, keep);
        compactSparse(d.ambient, keep);
        compactSparse(d.diffuse, keep);
        compactSparse(d.specular, keep);
        compactSparse(d.emissive, keep);
        compactSparse(d.shininess, keep);
        compactSparse(d.image, keep);
        compactSparse(d.imagePath, keep);
        compactSparse(d.uuid, keep);
        compactSparse(d.materialx, keep);
        compactSparse(d.type, keep);
        compactSparse(d.finish, keep);
        compactSparse(d.textureIndex, keep);
        // The palette may now hold slots nothing points at
        collapseTexture(d.texturePalette, d.textureIndex, d.overrides.size(), d.base.texture);
    }

    // The diffuse alpha IS the entry's transparency, so the base's second
    // slot is never allowed to drift from it
    d.base.transparency = d.base.diffuseColor.transparency();
    d.base.pbr = d.pbr;
    d.normalized = true;
}

void AppearanceList::ensureNormalized() const
{
    // Normalising is deferred so that a loop setting one entry at a time
    // does not rescan the whole list on every step. It changes what is
    // stored but not what the list MEANS, which is why it may happen under
    // a const call -- and why it writes through nd(), sharing the work with
    // every other holder rather than detaching on a read.
    if (!_data.isNull() && !rd().normalized) {
        normalize();
    }
}

// ==================== the base ====================

const MaterialAppearance &AppearanceList::getBase() const
{
    ensureNormalized();
    return rd().base;
}

bool AppearanceList::hasDerivedBase() const
{
    return rd().baseDerived;
}

const std::vector<uint32_t> &AppearanceList::getOverrides() const
{
    ensureNormalized();
    return rd().overrides;
}

bool AppearanceList::hasOverrides() const
{
    ensureNormalized();
    return !rd().overrides.empty();
}

bool AppearanceList::isOverride(int idx) const
{
    ensureNormalized();
    return overridePos(idx) >= 0;
}

int AppearanceList::overridePos(int idx) const
{
    if (idx < 0 || rd().overrides.empty())
        return -1;
    const auto value = static_cast<uint32_t>(idx);
    const auto it = std::lower_bound(rd().overrides.begin(), rd().overrides.end(), value);
    if (it == rd().overrides.end() || *it != value)
        return -1;
    return static_cast<int>(it - rd().overrides.begin());
}

int AppearanceList::makeOverride(int idx)
{
    const int found = overridePos(idx);
    if (found >= 0)
        return found;
    Data &d = wd();
    const auto value = static_cast<uint32_t>(idx);
    const auto it = std::lower_bound(d.overrides.begin(), d.overrides.end(), value);
    const int pos = static_cast<int>(it - d.overrides.begin());
    d.overrides.insert(it, value);
    // Every array that states anything gains the base's value there, which
    // is what the new override reads until something writes to it. An
    // import painting faces in order appends, which is where a sorted
    // vector costs nothing.
    insertSparse(d.ambient, pos, d.base.ambientColor);
    insertSparse(d.diffuse, pos, d.base.diffuseColor);
    insertSparse(d.specular, pos, d.base.specularColor);
    insertSparse(d.emissive, pos, d.base.emissiveColor);
    insertSparse(d.shininess, pos, d.base.shininess);
    insertSparse(d.image, pos, d.base.image);
    insertSparse(d.imagePath, pos, d.base.imagePath);
    insertSparse(d.uuid, pos, d.base.uuid);
    insertSparse(d.materialx, pos, d.base.materialx);
    insertSparse(d.type, pos, static_cast<int8_t>(d.base.getType()));
    insertSparse(d.finish, pos, d.base.finish);
    insertTexture(d.texturePalette, d.textureIndex, pos, d.base.texture);
    return pos;
}

void AppearanceList::clearOverride(int idx)
{
    const int pos = overridePos(idx);
    if (pos < 0)
        return;
    touchFields();
    Data &d = wd();
    d.overrides.erase(d.overrides.begin() + pos);
    eraseSparse(d.ambient, pos);
    eraseSparse(d.diffuse, pos);
    eraseSparse(d.specular, pos);
    eraseSparse(d.emissive, pos);
    eraseSparse(d.shininess, pos);
    eraseSparse(d.image, pos);
    eraseSparse(d.imagePath, pos);
    eraseSparse(d.uuid, pos);
    eraseSparse(d.materialx, pos);
    eraseSparse(d.type, pos);
    eraseSparse(d.finish, pos);
    eraseSparse(d.textureIndex, pos);
}

void AppearanceList::clearOverrides()
{
    if (rd().overrides.empty())
        return;
    touchFields();
    Data &d = wd();
    std::vector<uint32_t>().swap(d.overrides);
    std::vector<Color>().swap(d.ambient);
    std::vector<Color>().swap(d.diffuse);
    std::vector<Color>().swap(d.specular);
    std::vector<Color>().swap(d.emissive);
    std::vector<float>().swap(d.shininess);
    std::vector<std::string>().swap(d.image);
    std::vector<std::string>().swap(d.imagePath);
    std::vector<std::string>().swap(d.uuid);
    std::vector<std::string>().swap(d.materialx);
    std::vector<int8_t>().swap(d.type);
    std::vector<SurfaceFinish>().swap(d.finish);
    std::vector<SurfaceTexture>().swap(d.texturePalette);
    std::vector<uint16_t>().swap(d.textureIndex);
    pruneTextureBlobs();
}

void AppearanceList::setFollowMaterial(bool enable)
{
    if (rd().follow == enable)
        return;
    wd().follow = enable;
}

void AppearanceList::followMaterial(const MaterialAppearance &card)
{
    setBase(card);
    // After the write, because setBase is a whole-object write and every
    // one of those ends the follow -- this is the one that does not.
    // Guarded, because wd() DETACHES: an unguarded write would leave new
    // storage behind every time, the property would call that a change, and
    // the view provider re-applying the card on that notification would
    // never stop.
    if (!rd().follow)
        wd().follow = true;
}

void AppearanceList::endFollow()
{
    if (rd().follow)
        wd().follow = false;
}

void AppearanceList::setBase(const MaterialAppearance &value)
{
    const MaterialAppearance mat = inMode(value);
    ensureNormalized();
    if (rd().count == 0) {
        // A base with nothing to wear it is not a list; the same rule the
        // per field whole-object writes follow
        setSize(1, mat);
        return;
    }
    const Data &d = rd();
    if (d.base.ambientColor == mat.ambientColor && d.base.diffuseColor == storedDiffuse(mat)
        && d.base.specularColor == mat.specularColor && d.base.emissiveColor == mat.emissiveColor
        && d.base.shininess == mat.shininess && d.base.image == mat.image
        && d.base.imagePath == mat.imagePath && d.base.uuid == mat.uuid
        && d.base.materialx == mat.materialx
        && d.base.getType() == mat.getType() && d.base.finish == storedFinish(mat.finish)
        && d.base.texture == storedTexture(mat.texture)) {
        return;
    }
    touchFields();
    endFollow();
    Data &w = wd();
    setMaterialType(w.base, static_cast<int8_t>(mat.getType()));
    w.base.ambientColor = mat.ambientColor;
    w.base.diffuseColor = storedDiffuse(mat);
    w.base.specularColor = mat.specularColor;
    w.base.emissiveColor = mat.emissiveColor;
    w.base.shininess = mat.shininess;
    w.base.image = mat.image;
    w.base.imagePath = mat.imagePath;
    w.base.uuid = mat.uuid;
    w.base.materialx = mat.materialx;
    w.base.finish = storedFinish(mat.finish);
    w.base.texture = storedTexture(mat.texture);
}

/// The base's type, without MaterialAppearance::setType() taking the preset's colours
/// with it -- the trap getMaterial() documents, from the other side
void AppearanceList::setMaterialType(MaterialAppearance &mat, int8_t type)
{
    if (static_cast<int8_t>(mat.getType()) == type)
        return;
    const MaterialAppearance saved = mat;
    mat.setType(static_cast<MaterialAppearance::MaterialType>(type));
    mat.ambientColor = saved.ambientColor;
    mat.diffuseColor = saved.diffuseColor;
    mat.specularColor = saved.specularColor;
    mat.emissiveColor = saved.emissiveColor;
    mat.shininess = saved.shininess;
    mat.transparency = saved.transparency;
    mat.image = saved.image;
    mat.imagePath = saved.imagePath;
    mat.uuid = saved.uuid;
    mat.materialx = saved.materialx;
    mat.finish = saved.finish;
    mat.texture = saved.texture;
    mat.pbr = saved.pbr;
}

void AppearanceList::adoptDense()
{
    if (_data.isNull())
        return;
    Data &d = nd();
    // The dense form has no overrides in it by construction
    std::vector<uint32_t>().swap(d.overrides);
    if (d.count == 0) {
        d.normalized = false;
        d.baseDerived = true;
        normalize();
        return;
    }

    const MaterialAppearance &def = defaultMaterial();
    collapseField(d.ambient, def.ambientColor);
    collapseField(d.diffuse, storedDiffuse(def));
    collapseField(d.specular, specularDefault());
    collapseField(d.emissive, def.emissiveColor);
    collapseField(d.shininess, shininessDefault());
    collapseField(d.image, def.image);
    collapseField(d.imagePath, def.imagePath);
    collapseField(d.uuid, def.uuid);
    collapseField(d.materialx, def.materialx);
    collapseField(d.type, static_cast<int8_t>(def.getType()));
    collapseField(d.finish, def.finish);
    collapseDenseTexture(d.texturePalette, d.textureIndex, d.count, def.texture);

    // A field the whole list agrees about IS the base's; one that varies
    // leaves the base at the default and every entry an override, until a
    // base is derived (@ref materiallist_base). The base starts from the
    // mode's own defaults, which is what an absent field always read as.
    d.base = def;
    d.base.pbr = d.pbr;
    d.base.specularColor = specularDefault();
    d.base.shininess = shininessDefault();
    bool varies = false;
    auto take = [&varies](auto &field, auto &baseValue) {
        if (field.size() == 1) {
            baseValue = field.front();
            field.clear();
        }
        else if (!field.empty()) {
            varies = true;
        }
    };
    take(d.ambient, d.base.ambientColor);
    take(d.diffuse, d.base.diffuseColor);
    take(d.specular, d.base.specularColor);
    take(d.emissive, d.base.emissiveColor);
    take(d.shininess, d.base.shininess);
    take(d.image, d.base.image);
    take(d.imagePath, d.base.imagePath);
    take(d.uuid, d.base.uuid);
    take(d.materialx, d.base.materialx);
    take(d.finish, d.base.finish);
    if (d.type.size() == 1) {
        setMaterialType(d.base, d.type.front());
        d.type.clear();
    }
    else if (!d.type.empty()) {
        varies = true;
    }
    if (d.textureIndex.empty()) {
        d.base.texture = d.texturePalette.empty() ? def.texture : d.texturePalette.front();
        std::vector<SurfaceTexture>().swap(d.texturePalette);
    }
    else {
        varies = true;
    }
    d.base.transparency = d.base.diffuseColor.transparency();

    if (varies) {
        // Every face holds its own until the heuristic says which of them
        // the object is. This costs exactly what the dense form cost.
        d.overrides.resize(static_cast<std::size_t>(d.count));
        for (int i = 0; i < d.count; ++i)
            d.overrides[static_cast<std::size_t>(i)] = static_cast<uint32_t>(i);
        d.baseDerived = false;
    }
    else {
        d.baseDerived = true;
    }
    d.normalized = false;
    normalize();
}

void AppearanceList::ensureBase() const
{
    if (!rd().baseDerived)
        deriveBase();
}

/** Everything one overriding POSITION states, as a key that orders
 *
 * What makes two painted faces the same material, for the vote in
 * deriveBase. The texture is its palette SLOT, which is exact because the
 * normal form numbers the palette in first-use order and holds no
 * duplicate.
 */
AppearanceList::OverrideKey AppearanceList::overrideKey(int pos) const
{
    const Data &d = rd();
    const SurfaceFinish finish = sparseAt(d.finish, pos, d.base.finish);
    return OverrideKey {
        sparseAt(d.ambient, pos, d.base.ambientColor).getPackedValue(),
        sparseAt(d.diffuse, pos, d.base.diffuseColor).getPackedValue(),
        sparseAt(d.specular, pos, d.base.specularColor).getPackedValue(),
        sparseAt(d.emissive, pos, d.base.emissiveColor).getPackedValue(),
        sparseAt(d.shininess, pos, d.base.shininess),
        sparseAt(d.type, pos, static_cast<int8_t>(d.base.getType())),
        sparseAt(d.image, pos, d.base.image),
        sparseAt(d.imagePath, pos, d.base.imagePath),
        sparseAt(d.uuid, pos, d.base.uuid),
        sparseAt(d.materialx, pos, d.base.materialx),
        finish.pattern, finish.pitch, finish.depth, finish.angle,
        d.textureIndex.empty() ? 0 : d.textureIndex[static_cast<std::size_t>(pos)]
    };
}

/** Put a different base under the same entries
 *
 * Every face keeps what it resolves to now: the ones that wore the old base
 * become overrides, the ones that match the new base stop being them. Runs
 * through the dense form of each field, which is the only way to ask the
 * question once per face rather than once per face per field.
 */
void AppearanceList::rebase(const MaterialAppearance &newBase) const
{
    Data &d = nd();
    const std::size_t n = static_cast<std::size_t>(d.count);
    std::vector<Color> ambient = expandSparse(d.count, d.overrides, d.ambient, d.base.ambientColor);
    std::vector<Color> diffuse = expandSparse(d.count, d.overrides, d.diffuse, d.base.diffuseColor);
    std::vector<Color> specular =
            expandSparse(d.count, d.overrides, d.specular, d.base.specularColor);
    std::vector<Color> emissive =
            expandSparse(d.count, d.overrides, d.emissive, d.base.emissiveColor);
    std::vector<float> shininess =
            expandSparse(d.count, d.overrides, d.shininess, d.base.shininess);
    std::vector<std::string> image = expandSparse(d.count, d.overrides, d.image, d.base.image);
    std::vector<std::string> imagePath =
            expandSparse(d.count, d.overrides, d.imagePath, d.base.imagePath);
    std::vector<std::string> uuid = expandSparse(d.count, d.overrides, d.uuid, d.base.uuid);
    std::vector<std::string> materialx =
            expandSparse(d.count, d.overrides, d.materialx, d.base.materialx);
    std::vector<int8_t> type = expandSparse(d.count, d.overrides, d.type,
                                            static_cast<int8_t>(d.base.getType()));
    std::vector<SurfaceFinish> finish =
            expandSparse(d.count, d.overrides, d.finish, d.base.finish);
    std::vector<SurfaceTexture> texture(n, d.base.texture);
    if (!d.textureIndex.empty()) {
        for (std::size_t pos = 0; pos < d.overrides.size(); ++pos) {
            if (d.overrides[pos] < n) {
                texture[d.overrides[pos]] = sparseTextureAt(d.texturePalette, d.textureIndex,
                                                            static_cast<int>(pos), d.base.texture);
            }
        }
    }

    d.base = newBase;
    d.base.transparency = d.base.diffuseColor.transparency();
    d.base.pbr = d.pbr;
    const int8_t baseType = static_cast<int8_t>(d.base.getType());
    std::vector<uint32_t>().swap(d.overrides);
    std::vector<Color>().swap(d.ambient);
    std::vector<Color>().swap(d.diffuse);
    std::vector<Color>().swap(d.specular);
    std::vector<Color>().swap(d.emissive);
    std::vector<float>().swap(d.shininess);
    std::vector<std::string>().swap(d.image);
    std::vector<std::string>().swap(d.imagePath);
    std::vector<std::string>().swap(d.uuid);
    std::vector<std::string>().swap(d.materialx);
    std::vector<int8_t>().swap(d.type);
    std::vector<SurfaceFinish>().swap(d.finish);
    std::vector<SurfaceTexture>().swap(d.texturePalette);
    std::vector<uint16_t>().swap(d.textureIndex);
    for (std::size_t i = 0; i < n; ++i) {
        if (ambient[i] == d.base.ambientColor && diffuse[i] == d.base.diffuseColor
            && specular[i] == d.base.specularColor && emissive[i] == d.base.emissiveColor
            && shininess[i] == d.base.shininess && image[i] == d.base.image
            && imagePath[i] == d.base.imagePath && uuid[i] == d.base.uuid
            && materialx[i] == d.base.materialx
            && type[i] == baseType && finish[i] == d.base.finish
            && texture[i] == d.base.texture) {
            continue;
        }
        d.overrides.push_back(static_cast<uint32_t>(i));
    }
    const std::size_t k = d.overrides.size();
    if (k) {
        std::vector<SurfaceTexture> records;
        records.reserve(k);
        for (std::size_t pos = 0; pos < k; ++pos) {
            const std::size_t i = d.overrides[pos];
            d.ambient.push_back(ambient[i]);
            d.diffuse.push_back(diffuse[i]);
            d.specular.push_back(specular[i]);
            d.emissive.push_back(emissive[i]);
            d.shininess.push_back(shininess[i]);
            d.image.push_back(std::move(image[i]));
            d.imagePath.push_back(std::move(imagePath[i]));
            d.uuid.push_back(std::move(uuid[i]));
            d.materialx.push_back(std::move(materialx[i]));
            d.type.push_back(type[i]);
            d.finish.push_back(finish[i]);
            records.push_back(texture[i]);
        }
        assignPalette(d.texturePalette, d.textureIndex, records);
    }
    d.normalized = false;
    normalize();
}

bool AppearanceList::namesDiffuse(const Color &color) const
{
    ensureNormalized();
    const Data &d = rd();
    if (d.count == 0)
        return false;
    // The base counts only while some face still wears it
    if (d.overrides.size() < static_cast<std::size_t>(d.count) && d.base.diffuseColor == color)
        return true;
    for (const auto &value : d.diffuse) {
        if (value == color)
            return true;
    }
    return false;
}

void AppearanceList::deriveBase(const Color *hint, const std::vector<double> *weights) const
{
    if (_data.isNull() || rd().baseDerived)
        return;
    ensureNormalized();
    nd().baseDerived = true;
    const Data &d = rd();
    if (d.count <= 1 || d.overrides.empty()) {
        return;   // the base is already the only thing the list says
    }

    auto weightOf = [weights](std::size_t face) {
        if (!weights || face >= weights->size())
            return 1.0;
        const double value = (*weights)[face];
        return value > 0.0 ? value : 0.0;
    };
    // The faces that are not overrides already wear the base, and they vote
    // for it as one block
    double baseScore = 0.0;
    for (int i = 0; i < d.count; ++i)
        baseScore += weightOf(static_cast<std::size_t>(i));
    for (uint32_t face : d.overrides)
        baseScore -= weightOf(face);

    // 12.4, in order. The mirror first: on a document this fork wrote it
    // holds the last uniform value, which is what the object looked like
    // before its faces were painted. On an import it holds the
    // constructor's grey, which occurs in no imported list, so it declines.
    int winner = -1;
    if (hint) {
        if (baseScore > 0.0 && d.base.diffuseColor == *hint)
            return;   // the base already is what the mirror names
        for (std::size_t pos = 0; pos < d.overrides.size() && winner < 0; ++pos) {
            if (sparseAt(d.diffuse, static_cast<int>(pos), d.base.diffuseColor) == *hint)
                winner = static_cast<int>(pos);
        }
    }
    if (winner < 0) {
        // Area, not count: a green board with five hundred gold pads is
        // decided the wrong way by count, and by both the same way when the
        // faces are all of a size. Overrides that say the same thing sum,
        // which is what makes this "the material covering the largest area"
        // and not "the largest face" -- grouped through an ordered key
        // rather than pairwise, because an import can override thousands of
        // faces and a pairwise scan over those is quadratic.
        std::map<OverrideKey, std::size_t> groups;
        std::vector<double> score(d.overrides.size(), 0.0);
        for (std::size_t pos = 0; pos < d.overrides.size(); ++pos) {
            const auto found = groups.emplace(overrideKey(static_cast<int>(pos)), pos);
            score[found.first->second] += weightOf(d.overrides[pos]);
        }
        double best = baseScore;
        for (std::size_t pos = 0; pos < score.size(); ++pos) {
            if (score[pos] > best) {
                best = score[pos];
                winner = static_cast<int>(pos);
            }
        }
    }
    if (winner < 0)
        return;   // the faces already outside the overrides win

    MaterialAppearance chosen = d.base;
    setMaterialType(chosen, sparseAt(d.type, winner, static_cast<int8_t>(d.base.getType())));
    chosen.ambientColor = sparseAt(d.ambient, winner, d.base.ambientColor);
    chosen.diffuseColor = sparseAt(d.diffuse, winner, d.base.diffuseColor);
    chosen.specularColor = sparseAt(d.specular, winner, d.base.specularColor);
    chosen.emissiveColor = sparseAt(d.emissive, winner, d.base.emissiveColor);
    chosen.shininess = sparseAt(d.shininess, winner, d.base.shininess);
    chosen.image = sparseAt(d.image, winner, d.base.image);
    chosen.imagePath = sparseAt(d.imagePath, winner, d.base.imagePath);
    chosen.uuid = sparseAt(d.uuid, winner, d.base.uuid);
    chosen.materialx = sparseAt(d.materialx, winner, d.base.materialx);
    chosen.finish = sparseAt(d.finish, winner, d.base.finish);
    chosen.texture = sparseTextureAt(d.texturePalette, d.textureIndex, winner, d.base.texture);
    rebase(chosen);
}

bool AppearanceList::variesOnlyInDiffuse() const
{
    ensureNormalized();
    // Every non-diffuse override array empty: nothing but the colour is
    // stated per face. Per-entry transparency is the diffuse alpha, so it
    // is variance a colour list CAN express and is deliberately not tested
    // here.
    return rd().ambient.empty() && rd().specular.empty() && rd().emissive.empty()
        && rd().shininess.empty() && rd().type.empty()
        && rd().image.empty() && rd().imagePath.empty() && rd().uuid.empty()
        && rd().materialx.empty()
        && rd().finish.empty() && rd().textureIndex.empty();
}

//**************************************************************************
// PBR mode

const Color &AppearanceList::specularDefault() const
{
    // White F0 tint, metallic 0 in the alpha: the natural unset PBR
    // surface. The Phong default's alpha of one would read as full metal.
    static const Color pbrDef(1.0f, 1.0f, 1.0f, 0.0f);
    return rd().pbr ? pbrDef : defaultMaterial().specularColor;
}

float AppearanceList::shininessDefault() const
{
    return rd().pbr ? 0.5f : defaultMaterial().shininess;
}

void AppearanceList::requirePBR() const
{
    if (!rd().pbr)
        throw Base::RuntimeError("material list is not in PBR mode");
}

MaterialAppearance AppearanceList::inMode(const MaterialAppearance &mat) const
{
    if (mat.pbr == rd().pbr)
        return mat;
    MaterialAppearance converted = mat;
    converted.setPBR(rd().pbr);
    return converted;
}

void AppearanceList::setPBR(bool enable)
{
    if (rd().pbr == enable)
        return;
    // Two of the base's fields mean something different in each mode when
    // nobody has stated them: the Phong specular's alpha of one would read
    // as full metal, and the shininess baseline is not the roughness one.
    // A base still holding the old mode's default is a field nobody stated,
    // so it moves to the new mode's -- which is exactly what an empty field
    // used to do when the baseline it read was chosen by the mode.
    const Color oldSpecular = specularDefault();
    const float oldShininess = shininessDefault();
    touchFields();
    wd().pbr = enable;
    if (wd().base.specularColor == oldSpecular)
        wd().base.specularColor = specularDefault();
    if (wd().base.shininess == oldShininess)
        wd().base.shininess = shininessDefault();
    wd().base.pbr = enable;
}

void AppearanceList::convertPBR(bool enable)
{
    if (wd().pbr == enable)
        return;
    // Convert while the old mode still governs the readings, then flip.
    // Per entry, so per-face variation converts entry by entry; normalize
    // collapses whatever stays uniform afterwards.
    std::vector<MaterialAppearance> values;
    values.reserve(wd().count);
    for (int i = 0; i < wd().count; ++i) {
        // Tagged with the mode they are in, so the conversion is the
        // value's own; setValues below then reads the new mode off them
        MaterialAppearance mat = getMaterial(i);
        mat.setPBR(enable);
        values.push_back(mat);
    }
    setPBR(enable);
    setValues(std::move(values));
}

float AppearanceList::getMetallic(int idx) const
{
    if (!rd().pbr)
        return 0.0f;  // the Phong model has no metals
    return getSpecularColor(idx).a;
}

float AppearanceList::getRoughness(int idx) const
{
    const float value = getShininess(idx);
    return rd().pbr ? value : MaterialAppearance::shininessToRoughness(value);
}

void AppearanceList::setMetallicValues(const std::vector<float> &values)
{
    // The alphas of the specular field, exactly as setTransparencies works
    // the diffuse alphas: empty is back-to-default, 1 uniform, N per entry,
    // and every entry's tint rgb stays what it was.
    requirePBR();
    const int newCount = static_cast<int>(values.size());
    std::vector<Color> colors = getSpecularColors();
    if (newCount == 0 || newCount == 1) {
        const float alpha = newCount ? values[0] : specularDefault().a;
        if (colors.empty())
            colors.push_back(specularDefault());
        for (auto &color : colors)
            color.a = alpha;
    }
    else {
        colors.resize(newCount, getSpecularColor(0));
        for (int i = 0; i < newCount; ++i)
            colors[i].a = values[i];
    }
    setField(&MaterialAppearance::specularColor, &Data::specular, colors);
}

void AppearanceList::setRoughnessValues(const std::vector<float> &values)
{
    requirePBR();
    setField(&MaterialAppearance::shininess, &Data::shininess, values);
}

void AppearanceList::setMetallic(int idx, float value)
{
    requirePBR();
    Color color = getSpecularColor(idx < 0 || idx >= rd().count ? 0 : idx);
    color.a = value;
    setFieldValue(&MaterialAppearance::specularColor, &Data::specular, idx, color);
}

void AppearanceList::setRoughness(int idx, float value)
{
    requirePBR();
    setFieldValue(&MaterialAppearance::shininess, &Data::shininess, idx, value);
}

void AppearanceList::setMetallic(float value)
{
    // The base's tint alpha, as every whole-object write moves the base:
    // an overriding face holds its own metallic exactly as it holds its own
    // colour (docs/ShapeAppearanceDesign.md 12.2).
    requirePBR();
    Color color = getBase().specularColor;
    color.a = value;
    setBaseField(&MaterialAppearance::specularColor, color, specularDefault());
}

void AppearanceList::setRoughness(float value)
{
    requirePBR();
    setBaseField(&MaterialAppearance::shininess, value, shininessDefault());
}

MaterialAppearance AppearanceList::getPhongMaterial(int idx) const
{
    MaterialAppearance mat = getMaterial(idx);
    if (!rd().pbr)
        return mat;
    // Why the diffuse stays the base colour: see MaterialAppearance::pbrToPhong
    return MaterialAppearance::pbrToPhong(mat);
}

MaterialAppearance AppearanceList::getPhongBase() const
{
    const MaterialAppearance &mat = getBase();
    return rd().pbr ? MaterialAppearance::pbrToPhong(mat) : mat;
}

// ==================== restore ====================
void AppearanceList::restoreValues(std::vector<MaterialAppearance> &&values, bool legacy)
{
    touchFields();
    Data &d = wd();
    d.count = static_cast<int>(values.size());
    std::vector<float> transparency;
    std::vector<uint32_t>().swap(d.overrides);
    std::vector<Color>().swap(d.ambient);
    std::vector<Color>().swap(d.diffuse);
    std::vector<Color>().swap(d.specular);
    std::vector<Color>().swap(d.emissive);
    std::vector<float>().swap(d.shininess);
    std::vector<std::string>().swap(d.image);
    std::vector<std::string>().swap(d.imagePath);
    std::vector<std::string>().swap(d.uuid);
    std::vector<std::string>().swap(d.materialx);
    std::vector<int8_t>().swap(d.type);
    // No finish or texture here: none of the encodings that come through
    // this function can carry either, so every entry reads as unfinished
    // and untextured -- which the empty fields already say, at no cost.
    std::vector<SurfaceFinish>().swap(d.finish);
    std::vector<SurfaceTexture>().swap(d.texturePalette);
    std::vector<uint16_t>().swap(d.textureIndex);
    if (d.count) {
        d.ambient.reserve(d.count);
        d.diffuse.reserve(d.count);
        d.specular.reserve(d.count);
        d.emissive.reserve(d.count);
        d.shininess.reserve(d.count);
        transparency.reserve(d.count);
        d.image.reserve(d.count);
        d.imagePath.reserve(d.count);
        d.uuid.reserve(d.count);
        d.materialx.reserve(d.count);
        d.type.reserve(d.count);
        for (auto &mat : values) {
            // The diffuse alpha exactly as the file states it; which of the
            // two slots is the entry's transparency is decided below, once
            // the legacy conversion has made them comparable.
            d.ambient.push_back(mat.ambientColor);
            d.diffuse.push_back(mat.diffuseColor);
            d.specular.push_back(mat.specularColor);
            d.emissive.push_back(mat.emissiveColor);
            d.shininess.push_back(mat.shininess);
            transparency.push_back(mat.transparency);
            d.image.push_back(std::move(mat.image));
            d.imagePath.push_back(std::move(mat.imagePath));
            d.uuid.push_back(std::move(mat.uuid));
            d.materialx.push_back(std::move(mat.materialx));
            d.type.push_back(static_cast<int8_t>(mat.getType()));
        }
        if (legacy) {
            // Every colour's alpha meant transparency: invert, as upstream's
            // convertAlphaInMaterial does. For the diffuse this makes the
            // alpha an opacity, so that the merge below can compare it with
            // the field.
            for (auto *field : {&d.ambient, &d.diffuse, &d.specular, &d.emissive}) {
                for (auto &color : *field)
                    convertAlpha(color);
            }
        }
        applyRestoredTransparency(transparency, legacy);
        // The file states one entry at a time and says nothing about which
        // of them the object is; adoptDense holds that open until a base is
        // derived (@ref materiallist_base).
        adoptDense();
    }
    else {
        normalize();
    }
}

void AppearanceList::applyRestoredTransparency(const std::vector<float> &transparency,
                                                     bool legacy)
{
    if (transparency.empty() || wd().count == 0)
        return;
    // Called between a dense landing and adoptDense, so the diffuse field
    // here is dense too: 0, 1 or count long, as the file wrote it.
    const std::size_t n = transparency.size();
    Data &d = wd();
    if (static_cast<int>(d.diffuse.size()) != d.count) {
        const Color current = d.diffuse.empty() ? storedDiffuse(defaultMaterial())
                                                : d.diffuse.front();
        d.diffuse.assign(d.count, current);
    }
    for (int i = 0; i < d.count; ++i) {
        const float field = transparency[n == 1 ? 0 : i];
        float &alpha = d.diffuse[i].a;
        // See restoreValues: for a legacy file both slots were transparency
        // and the larger of the two is the one that was actually written (the
        // alpha arrives here already inverted, so it is min() of opacities);
        // for a 1.1 file the field alone is the truth.
        if (legacy)
            alpha = std::min(alpha, 1.0F - field);
        else
            alpha = 1.0F - field;
    }
}

// ==================== access ====================
void AppearanceList::setSize(int newSize)
{
    // Grow with what an unset entry reads as, which is the BASE: adding a
    // face to an object makes it look like the object.
    if (newSize == rd().count)
        return;
    if (newSize > rd().count && rd().count > 0) {
        touchFields();
        wd().count = newSize;
        return;
    }
    MaterialAppearance def = defaultMaterial();
    def.specularColor = specularDefault();
    def.shininess = shininessDefault();
    def.pbr = rd().pbr;
    setSize(newSize, def);
}

void AppearanceList::setSize(int newSize, const MaterialAppearance &fill)
{
    if (newSize == rd().count)
        return;
    if (newSize < 0)
        throw Base::ValueError("negative list size");

    touchFields();
    if (newSize < rd().count) {
        // The faces that are gone take their overrides with them
        Data &d = wd();
        std::size_t kept = 0;
        while (kept < d.overrides.size()
               && d.overrides[kept] < static_cast<uint32_t>(newSize)) {
            ++kept;
        }
        if (kept != d.overrides.size()) {
            std::vector<bool> keep(d.overrides.size(), false);
            for (std::size_t pos = 0; pos < kept; ++pos)
                keep[pos] = true;
            compactSparse(d.overrides, keep);
            compactSparse(d.ambient, keep);
            compactSparse(d.diffuse, keep);
            compactSparse(d.specular, keep);
            compactSparse(d.emissive, keep);
            compactSparse(d.shininess, keep);
            compactSparse(d.image, keep);
            compactSparse(d.imagePath, keep);
            compactSparse(d.uuid, keep);
            compactSparse(d.materialx, keep);
            compactSparse(d.type, keep);
            compactSparse(d.finish, keep);
            compactSparse(d.textureIndex, keep);
        }
        d.count = newSize;
        return;
    }

    // Growth fills entries of this list, so the filler reads as this list
    // does; only a whole-list assignment restates the mode
    const MaterialAppearance mat = inMode(fill);
    const int oldCount = rd().count;
    if (oldCount == 0) {
        // Nothing to be the base yet, so the filler IS it
        Data &d = wd();
        d.count = newSize;
        d.base = defaultMaterial();
        d.base.pbr = d.pbr;
        d.baseDerived = true;
        setMaterialType(d.base, static_cast<int8_t>(mat.getType()));
        d.base.ambientColor = mat.ambientColor;
        d.base.diffuseColor = storedDiffuse(mat);
        d.base.specularColor = mat.specularColor;
        d.base.emissiveColor = mat.emissiveColor;
        d.base.shininess = mat.shininess;
        d.base.image = mat.image;
        d.base.imagePath = mat.imagePath;
        d.base.uuid = mat.uuid;
        d.base.materialx = mat.materialx;
        d.base.finish = storedFinish(mat.finish);
        d.base.texture = storedTexture(mat.texture);
        return;
    }
    wd().count = newSize;
    // A filler that says what the base says is what the new faces read
    // anyway, which is what keeps a growing import of identically coloured
    // faces linear
    for (int i = oldCount; i < newSize; ++i)
        applyEntry(i, mat);
}

/// Every field of one entry, which is the per-face write set1Value and a
/// growth with a filler both are
void AppearanceList::applyEntry(int idx, const MaterialAppearance &mat)
{
    setFieldValue(&MaterialAppearance::ambientColor, &Data::ambient, idx, mat.ambientColor);
    setFieldValue(&MaterialAppearance::diffuseColor, &Data::diffuse, idx, storedDiffuse(mat));
    setFieldValue(&MaterialAppearance::specularColor, &Data::specular, idx, mat.specularColor);
    setFieldValue(&MaterialAppearance::emissiveColor, &Data::emissive, idx, mat.emissiveColor);
    setFieldValue(&MaterialAppearance::shininess, &Data::shininess, idx, mat.shininess);
    setFieldValue(&MaterialAppearance::image, &Data::image, idx, mat.image);
    setFieldValue(&MaterialAppearance::imagePath, &Data::imagePath, idx, mat.imagePath);
    setFieldValue(&MaterialAppearance::uuid, &Data::uuid, idx, mat.uuid);
    setFieldValue(&MaterialAppearance::materialx, &Data::materialx, idx, mat.materialx);
    setTypeValue(idx, static_cast<int8_t>(mat.getType()));
    setFieldValue(&MaterialAppearance::finish, &Data::finish, idx, storedFinish(mat.finish));
    setTexture(idx, mat.texture);
}

/// The type field, which has no MaterialAppearance member to name it by: the base
/// keeps it inside the material, where setType() would rewrite the colours
void AppearanceList::setTypeValue(int idx, int8_t value)
{
    if (idx < 0 || idx >= rd().count)
        throw Base::RuntimeError("index out of bound");
    const int8_t base = static_cast<int8_t>(rd().base.getType());
    if (sparseAt(rd().type, overridePos(idx), base) == value)
        return;
    touchFields();
    const int pos = makeOverride(idx);
    setSparseAt(wd().type, pos, wd().overrides.size(), value, base);
}

MaterialAppearance AppearanceList::getMaterial(int idx) const
{
    MaterialAppearance mat;
    if (idx < 0 || idx >= rd().count)
        return mat;
    ensureNormalized();
    const Data &d = rd();
    const int pos = overridePos(idx);
    // The type goes on FIRST. MaterialAppearance::setType() rewrites every colour and
    // both floats with that type's preset, so a setType() after the fields
    // are laid in throws all of them away and the list hands back the
    // preset instead of what it stores -- silently, because the per field
    // getters below are unaffected and keep telling the truth.
    mat.setType(static_cast<MaterialAppearance::MaterialType>(
                sparseAt(d.type, pos, static_cast<int8_t>(d.base.getType()))));
    mat.ambientColor = sparseAt(d.ambient, pos, d.base.ambientColor);
    mat.diffuseColor = sparseAt(d.diffuse, pos, d.base.diffuseColor);
    mat.specularColor = sparseAt(d.specular, pos, d.base.specularColor);
    mat.emissiveColor = sparseAt(d.emissive, pos, d.base.emissiveColor);
    mat.shininess = sparseAt(d.shininess, pos, d.base.shininess);
    // One quantity, two slots: the material handed out is always consistent,
    // whatever inconsistent pair was once handed in.
    mat.transparency = mat.diffuseColor.transparency();
    mat.image = sparseAt(d.image, pos, d.base.image);
    mat.imagePath = sparseAt(d.imagePath, pos, d.base.imagePath);
    mat.uuid = sparseAt(d.uuid, pos, d.base.uuid);
    mat.materialx = sparseAt(d.materialx, pos, d.base.materialx);
    mat.finish = sparseAt(d.finish, pos, d.base.finish);
    mat.texture = sparseTextureAt(d.texturePalette, d.textureIndex, pos, d.base.texture);
    // Stamp the list's mode on the value, so whoever holds it still knows
    // which reading its slots are in
    mat.pbr = d.pbr;
    return mat;
}

void AppearanceList::setValue(const MaterialAppearance &mat)
{
    setValues(std::vector<MaterialAppearance>(1, mat));
}

void AppearanceList::setValues(const std::vector<MaterialAppearance> &values)
{
    // The list holds ONE mode, and an assignment states it through the
    // material it starts with; the rest are converted to that reading
    // rather than landing their slots under the wrong one. An empty
    // assignment states nothing, so the mode it finds stands.
    if (!values.empty())
        setPBR(values.front().pbr);
    touchFields();
    Data &d = wd();
    d.count = static_cast<int>(values.size());
    std::vector<uint32_t>().swap(d.overrides);
    std::vector<Color>().swap(d.ambient);
    std::vector<Color>().swap(d.diffuse);
    std::vector<Color>().swap(d.specular);
    std::vector<Color>().swap(d.emissive);
    std::vector<float>().swap(d.shininess);
    std::vector<std::string>().swap(d.image);
    std::vector<std::string>().swap(d.imagePath);
    std::vector<std::string>().swap(d.uuid);
    std::vector<std::string>().swap(d.materialx);
    std::vector<int8_t>().swap(d.type);
    std::vector<SurfaceFinish>().swap(d.finish);
    std::vector<SurfaceTexture> textures;
    std::vector<SurfaceTexture>().swap(d.texturePalette);
    std::vector<uint16_t>().swap(d.textureIndex);
    if (d.count) {
        d.ambient.reserve(d.count);
        d.diffuse.reserve(d.count);
        d.specular.reserve(d.count);
        d.emissive.reserve(d.count);
        d.shininess.reserve(d.count);
        d.image.reserve(d.count);
        d.imagePath.reserve(d.count);
        d.uuid.reserve(d.count);
        d.materialx.reserve(d.count);
        d.type.reserve(d.count);
        d.finish.reserve(d.count);
        textures.reserve(d.count);
        for (const auto &value : values) {
            const MaterialAppearance mat = inMode(value);
            d.ambient.push_back(mat.ambientColor);
            d.diffuse.push_back(storedDiffuse(mat));
            d.specular.push_back(mat.specularColor);
            d.emissive.push_back(mat.emissiveColor);
            d.shininess.push_back(mat.shininess);
            d.image.push_back(mat.image);
            d.imagePath.push_back(mat.imagePath);
            d.uuid.push_back(mat.uuid);
            d.materialx.push_back(mat.materialx);
            d.type.push_back(static_cast<int8_t>(mat.getType()));
            d.finish.push_back(storedFinish(mat.finish));
            textures.push_back(storedTexture(mat.texture));
        }
        // Dense in, palette out; adoptDense below collapses both
        assignPalette(d.texturePalette, d.textureIndex, textures);
        // One value per entry and nothing saying which of them the object
        // is -- the same standing a restore lands in
        adoptDense();
        pruneTextureBlobs();
    }
    else {
        normalize();
    }
}

void AppearanceList::set1Value(int idx, const MaterialAppearance &value)
{
    if (idx < -1 || idx > rd().count)
        throw Base::RuntimeError("index out of bound");

    // One entry cannot restate the whole list's mode, so it is converted
    // to it instead
    const MaterialAppearance mat = inMode(value);
    if (idx == -1 || idx == rd().count) {
        idx = rd().count;
        setSize(rd().count + 1, mat);
    }
    else {
        if (getMaterial(idx) == mat)
            return;
        touchFields();
        applyEntry(idx, mat);
    }
}

//**************************************************************************
// Per field access

Color AppearanceList::getAmbientColor(int idx) const
{
    ensureNormalized();
    return sparseAt(rd().ambient, overridePos(idx), rd().base.ambientColor);
}

Color AppearanceList::getDiffuseColor(int idx) const
{
    ensureNormalized();
    return sparseAt(rd().diffuse, overridePos(idx), rd().base.diffuseColor);
}

Color AppearanceList::getSpecularColor(int idx) const
{
    ensureNormalized();
    return sparseAt(rd().specular, overridePos(idx), rd().base.specularColor);
}

Color AppearanceList::getEmissiveColor(int idx) const
{
    ensureNormalized();
    return sparseAt(rd().emissive, overridePos(idx), rd().base.emissiveColor);
}

float AppearanceList::getShininess(int idx) const
{
    ensureNormalized();
    return sparseAt(rd().shininess, overridePos(idx), rd().base.shininess);
}

float AppearanceList::getTransparency(int idx) const
{
    // The complement of the diffuse alpha; there is no second store.
    return getDiffuseColor(idx).transparency();
}

const std::string &AppearanceList::getImage(int idx) const
{
    ensureNormalized();
    return sparseAt(rd().image, overridePos(idx), rd().base.image);
}

const std::string &AppearanceList::getImagePath(int idx) const
{
    ensureNormalized();
    return sparseAt(rd().imagePath, overridePos(idx), rd().base.imagePath);
}

const std::string &AppearanceList::getUuid(int idx) const
{
    ensureNormalized();
    return sparseAt(rd().uuid, overridePos(idx), rd().base.uuid);
}

const std::string &AppearanceList::getMaterialX(int idx) const
{
    ensureNormalized();
    return sparseAt(rd().materialx, overridePos(idx), rd().base.materialx);
}

SurfaceFinish AppearanceList::getFinish(int idx) const
{
    ensureNormalized();
    return sparseAt(rd().finish, overridePos(idx), rd().base.finish);
}

SurfaceTexture AppearanceList::getTexture(int idx) const
{
    ensureNormalized();
    return sparseTextureAt(rd().texturePalette, rd().textureIndex, overridePos(idx),
                           rd().base.texture);
}

MaterialAppearance::MaterialType AppearanceList::getType(int idx) const
{
    ensureNormalized();
    return static_cast<MaterialAppearance::MaterialType>(
            sparseAt(rd().type, overridePos(idx), static_cast<int8_t>(rd().base.getType())));
}

/** Take a whole field, one value per entry
 *
 * Collapsed first, so a vector that says the same thing for every entry is
 * the whole-object write it says it is: it moves the base and the override
 * array goes. A vector that varies makes an override of every entry that
 * differs from the base. A vector that is neither empty, uniform nor as
 * long as the list is a statement about how long the list should be, the
 * way assigning a colour list of a different length is; an empty one
 * returns the field to the DEFAULT, which is what it has always meant.
 */
template<class T>
void AppearanceList::setField(T MaterialAppearance::*base, std::vector<T> Data::*member,
                            const std::vector<T> &values)
{
    std::vector<T> incoming = values;
    // The default a caller means by an empty vector is the material's own,
    // not the base's: this is the write that puts a field back
    const MaterialAppearance &def = defaultMaterial();
    collapseField(incoming, def.*base);
    const int newCount = static_cast<int>(values.size());
    if (newCount != rd().count && (newCount > 1 || rd().count == 0)) {
        // Growth extends the LAST entry's material, not the default: the
        // caller is stating one field for N entries and saying nothing about
        // the others, so the others must not change meaning. An empty list
        // grows with what its entries read as, which follows the mode.
        if (rd().count)
            setSize(newCount, getMaterial(rd().count - 1));
        else
            setSize(newCount);
    }
    if (incoming.size() <= 1) {
        const T value = incoming.empty() ? def.*base : incoming.front();
        // Uniform means uniform: the overriding faces are stating this
        // field too, and a vector as long as the list has just restated it
        // for every one of them
        if (!(rd().base.*base == value) || !((rd().*member).empty())) {
            touchFields();
            endFollow();
            wd().base.*base = value;
            std::vector<T>().swap(wd().*member);
        }
        return;
    }
    touchFields();
    for (int i = 0; i < rd().count && i < static_cast<int>(incoming.size()); ++i)
        setFieldValue(base, member, i, incoming[static_cast<std::size_t>(i)]);
}

void AppearanceList::setAmbientColors(const std::vector<Color> &colors)
{
    setField(&MaterialAppearance::ambientColor, &Data::ambient, colors);
}

void AppearanceList::setDiffuseColors(const std::vector<Color> &colors)
{
    setField(&MaterialAppearance::diffuseColor, &Data::diffuse, colors);
}

void AppearanceList::setSpecularColors(const std::vector<Color> &colors)
{
    setField(&MaterialAppearance::specularColor, &Data::specular, colors);
}

void AppearanceList::setEmissiveColors(const std::vector<Color> &colors)
{
    setField(&MaterialAppearance::emissiveColor, &Data::emissive, colors);
}

void AppearanceList::setShininessValues(const std::vector<float> &values)
{
    setField(&MaterialAppearance::shininess, &Data::shininess, values);
}

void AppearanceList::setTransparencies(const std::vector<float> &values)
{
    // The alphas of the diffuse field, at the sizes the old float field
    // accepted: empty is back-to-default, 1 is uniform, N per entry -- and a
    // different N resizes the list, as assigning any field does. The rgb of
    // every entry stays what it was; only the alphas move.
    const Color def = storedDiffuse(defaultMaterial());
    const int newCount = static_cast<int>(values.size());
    std::vector<Color> colors = getDiffuseColors();
    if (newCount == 0 || newCount == 1) {
        const float alpha = newCount ? 1.0F - values[0] : def.a;
        if (colors.empty())
            colors.push_back(def);
        for (auto &color : colors)
            color.a = alpha;
    }
    else {
        // Per entry: the rgb comes along, from wherever the field has it.
        colors.resize(newCount, getDiffuseColor(0));
        for (int i = 0; i < newCount; ++i)
            colors[i].setTransparency(values[i]);
    }
    setField(&MaterialAppearance::diffuseColor, &Data::diffuse, colors);
}

void AppearanceList::setImages(const std::vector<std::string> &values)
{
    setField(&MaterialAppearance::image, &Data::image, values);
}

void AppearanceList::setImagePaths(const std::vector<std::string> &values)
{
    setField(&MaterialAppearance::imagePath, &Data::imagePath, values);
}

void AppearanceList::setUuids(const std::vector<std::string> &values)
{
    setField(&MaterialAppearance::uuid, &Data::uuid, values);
}

void AppearanceList::setMaterialXs(const std::vector<std::string> &values)
{
    setField(&MaterialAppearance::materialx, &Data::materialx, values);
}

void AppearanceList::setFinishes(const std::vector<SurfaceFinish> &values)
{
    std::vector<SurfaceFinish> clamped;
    clamped.reserve(values.size());
    for (const auto &value : values)
        clamped.push_back(storedFinish(value));
    setField(&MaterialAppearance::finish, &Data::finish, clamped);
}

void AppearanceList::setTextures(const std::vector<SurfaceTexture> &values)
{
    std::vector<SurfaceTexture> clamped;
    clamped.reserve(values.size());
    for (const auto &value : values)
        clamped.push_back(storedTexture(value));
    collapseField(clamped, defaultMaterial().texture);

    const int newCount = static_cast<int>(values.size());
    if (newCount != rd().count && (newCount > 1 || rd().count == 0)) {
        // The same growth rule setField documents: extend the LAST entry's
        // material, because the caller is stating one field and saying
        // nothing about the others
        if (rd().count)
            setSize(newCount, getMaterial(rd().count - 1));
        else
            setSize(newCount);
    }
    if (clamped.size() <= 1) {
        // Uniform means uniform, as it does in setField: the base states it
        // and the override arrays that contradicted it go
        setTexture(clamped.empty() ? defaultMaterial().texture : clamped.front());
        if (!rd().texturePalette.empty() || !rd().textureIndex.empty()) {
            touchFields();
            std::vector<SurfaceTexture>().swap(wd().texturePalette);
            std::vector<uint16_t>().swap(wd().textureIndex);
        }
        pruneTextureBlobs();
        return;
    }
    touchFields();
    for (int i = 0; i < rd().count && i < static_cast<int>(clamped.size()); ++i)
        setTexture(i, clamped[static_cast<std::size_t>(i)]);
    pruneTextureBlobs();
}

/** Write one entry of one field, growing the list if it names a new entry
 *
 * The per-face write: the face becomes an override of this field alone, or
 * -- when the value it is given is the base's -- normalisation drops it
 * back on the next read.
 */
template<class T>
void AppearanceList::setFieldValue(T MaterialAppearance::*base, std::vector<T> Data::*member, int idx,
                                 const T &value)
{
    if (idx < 0 || idx > rd().count)
        throw Base::RuntimeError("index out of bound");
    if (idx == rd().count) {
        setSize(rd().count + 1);
    }
    // By value: the base lives in the storage wd() may detach, and every
    // write below is against what it said when the write was decided
    const T baseValue = rd().base.*base;
    if (idx < rd().count && sparseAt(rd().*member, overridePos(idx), baseValue) == value) {
        // Unchanged, and nothing has been written: the storage is still
        // whatever it was, which is what tells the property there is no
        // change to record
        return;
    }
    touchFields();
    const int pos = makeOverride(idx);
    setSparseAt(wd().*member, pos, wd().overrides.size(), value, baseValue);
}

void AppearanceList::setAmbientColor(int idx, const Color &col)
{
    setFieldValue(&MaterialAppearance::ambientColor, &Data::ambient, idx, col);
}

void AppearanceList::setDiffuseColor(int idx, const Color &col)
{
    setFieldValue(&MaterialAppearance::diffuseColor, &Data::diffuse, idx, col);
}

void AppearanceList::setSpecularColor(int idx, const Color &col)
{
    setFieldValue(&MaterialAppearance::specularColor, &Data::specular, idx, col);
}

void AppearanceList::setEmissiveColor(int idx, const Color &col)
{
    setFieldValue(&MaterialAppearance::emissiveColor, &Data::emissive, idx, col);
}

void AppearanceList::setShininess(int idx, float value)
{
    setFieldValue(&MaterialAppearance::shininess, &Data::shininess, idx, value);
}

void AppearanceList::setTransparency(int idx, float value)
{
    // One entry's alpha: the same write as setDiffuseColor of that entry
    // with only the alpha changed, and it shares that setter's growth and
    // early-out behaviour.
    Color color = getDiffuseColor(idx < 0 || idx >= rd().count ? 0 : idx);
    color.setTransparency(value);
    setFieldValue(&MaterialAppearance::diffuseColor, &Data::diffuse, idx, color);
}

void AppearanceList::setImage(int idx, const std::string &value)
{
    setFieldValue(&MaterialAppearance::image, &Data::image, idx, value);
}

void AppearanceList::setImagePath(int idx, const std::string &value)
{
    setFieldValue(&MaterialAppearance::imagePath, &Data::imagePath, idx, value);
}

void AppearanceList::setUuid(int idx, const std::string &value)
{
    setFieldValue(&MaterialAppearance::uuid, &Data::uuid, idx, value);
}

void AppearanceList::setMaterialX(int idx, const std::string &value)
{
    setFieldValue(&MaterialAppearance::materialx, &Data::materialx, idx, value);
}

void AppearanceList::setFinish(int idx, const SurfaceFinish &value)
{
    setFieldValue(&MaterialAppearance::finish, &Data::finish, idx, storedFinish(value));
}

void AppearanceList::setTexture(int idx, const SurfaceTexture &value)
{
    // setFieldValue's body, over the pair: the palette is not a field the
    // member pointer form can address
    if (idx < 0 || idx > rd().count)
        throw Base::RuntimeError("index out of bound");
    const SurfaceTexture stored = storedTexture(value);
    if (idx == rd().count) {
        setSize(rd().count + 1);
    }
    const SurfaceTexture baseValue = rd().base.texture;
    if (idx < rd().count
        && sparseTextureAt(rd().texturePalette, rd().textureIndex, overridePos(idx), baseValue)
                == stored) {
        return;
    }
    touchFields();
    const int pos = makeOverride(idx);
    Data &d = wd();
    statedTexture(d.texturePalette, d.textureIndex, d.overrides.size(), baseValue);
    d.textureIndex[static_cast<std::size_t>(pos)] = paletteSlot(d.texturePalette, stored);
}

/** The whole-object write of one field: the base, and nothing else
 *
 * An overriding face keeps what it holds -- that is what makes a painted
 * face survive an appearance card being assigned (12.2). A list with no
 * entries has nowhere to put a base, so it grows one first, which is what
 * the old uniform writes did too.
 */
template<class T>
void AppearanceList::setBaseField(T MaterialAppearance::*base, const T &value, const T &def)
{
    if (rd().count == 0) {
        if (value == def)
            return;   // already the default everywhere, including here
        setSize(1);
    }
    if (rd().base.*base == value)
        return;
    touchFields();
    endFollow();
    wd().base.*base = value;
}

void AppearanceList::setAmbientColor(const Color &col)
{
    setBaseField(&MaterialAppearance::ambientColor, col, defaultMaterial().ambientColor);
}

void AppearanceList::setDiffuseColor(const Color &col)
{
    setBaseField(&MaterialAppearance::diffuseColor, col, storedDiffuse(defaultMaterial()));
}

void AppearanceList::setSpecularColor(const Color &col)
{
    setBaseField(&MaterialAppearance::specularColor, col, specularDefault());
}

void AppearanceList::setEmissiveColor(const Color &col)
{
    setBaseField(&MaterialAppearance::emissiveColor, col, defaultMaterial().emissiveColor);
}

void AppearanceList::setShininess(float value)
{
    setBaseField(&MaterialAppearance::shininess, value, shininessDefault());
}

void AppearanceList::setTransparency(float value)
{
    // The base's alpha, leaving its rgb alone -- and leaving the overriding
    // faces alone, which is what every whole-object write does
    Color color = getBase().diffuseColor;
    color.setTransparency(value);
    setBaseField(&MaterialAppearance::diffuseColor, color, storedDiffuse(defaultMaterial()));
}

void AppearanceList::setDiffuseRGB(const Color &col)
{
    setFieldRGB(&MaterialAppearance::diffuseColor, col, storedDiffuse(defaultMaterial()));
}

void AppearanceList::setSpecularRGB(const Color &col)
{
    setFieldRGB(&MaterialAppearance::specularColor, col, specularDefault());
}

void AppearanceList::setFieldRGB(Color MaterialAppearance::*base, const Color &col, const Color &def)
{
    // setTransparency's mirror image: the base's rgb, leaving its alpha
    // alone -- the diffuse alpha is the opacity and the PBR specular alpha
    // is the metallic factor, so a whole-colour write would restate them.
    Color color = getBase().*base;
    color.r = col.r;
    color.g = col.g;
    color.b = col.b;
    setBaseField(base, color, def);
}

//**************************************************************************
// Base class implementer

void AppearanceList::setImage(const std::string &value)
{
    setBaseField(&MaterialAppearance::image, value, defaultMaterial().image);
}

void AppearanceList::setImagePath(const std::string &value)
{
    setBaseField(&MaterialAppearance::imagePath, value, defaultMaterial().imagePath);
}

void AppearanceList::setUuid(const std::string &value)
{
    setBaseField(&MaterialAppearance::uuid, value, defaultMaterial().uuid);
}

void AppearanceList::setMaterialX(const std::string &value)
{
    setBaseField(&MaterialAppearance::materialx, value, defaultMaterial().materialx);
}

void AppearanceList::setFinish(const SurfaceFinish &value)
{
    setBaseField(&MaterialAppearance::finish, storedFinish(value), defaultMaterial().finish);
}

void AppearanceList::setTexture(const SurfaceTexture &value)
{
    setBaseField(&MaterialAppearance::texture, storedTexture(value), defaultMaterial().texture);
}

// ==================== memsize ====================
/** What this list holds, which is not what a MaterialAppearance weighs
 *
 * The base is a whole material in the struct and only its stated fields in
 * the value: a uniform grey list weighs one material in memory and nothing
 * here, exactly as an all-empty set of fields weighed nothing before there
 * was a base. That matters beyond reporting -- the inline-versus-archive
 * rule of PropertyLists::Save reads this number as the cost of writing the
 * property, and counting the whole struct would send every appearance in
 * the document to an archive entry of its own.
 */
unsigned int AppearanceList::getMemSize() const
{
    ensureNormalized();
    const Data &d = rd();
    if (d.count == 0)
        return 0;
    const MaterialAppearance &def = defaultMaterial();
    std::size_t size = 0;
    if (!(d.base.ambientColor == def.ambientColor))
        size += sizeof(Color);
    if (!(d.base.diffuseColor == storedDiffuse(def)))
        size += sizeof(Color);
    if (!(d.base.specularColor == specularDefault()))
        size += sizeof(Color);
    if (!(d.base.emissiveColor == def.emissiveColor))
        size += sizeof(Color);
    if (d.base.shininess != shininessDefault())
        size += sizeof(float);
    if (d.base.getType() != def.getType())
        size += sizeof(int8_t);
    size += d.base.image.size() + d.base.imagePath.size() + d.base.uuid.size();
    size += d.base.materialx.size();
    if (!(d.base.finish == def.finish))
        size += sizeof(SurfaceFinish);
    if (!(d.base.texture == def.texture)) {
        size += sizeof(SurfaceTexture);
        for (const auto &hash : d.base.texture.maps)
            size += hash.size();
    }
    size += d.overrides.size() * sizeof(uint32_t)
        + (d.ambient.size() + d.diffuse.size() + d.specular.size() + d.emissive.size())
              * sizeof(Color)
        + d.shininess.size() * sizeof(float)
        + d.type.size() * sizeof(int8_t)
        + d.finish.size() * sizeof(SurfaceFinish)
        + texturesMemSize(d.texturePalette)
        + d.textureIndex.size() * sizeof(uint16_t)
        + stringsMemSize(d.image) + stringsMemSize(d.imagePath) + stringsMemSize(d.uuid)
        + stringsMemSize(d.materialx);
    return static_cast<unsigned int>(size);
}


// ==================== issame ====================
bool AppearanceList::isSame(const AppearanceList &other) const
{
    if (&other == this || _data.isSameData(other._data)) {
        return true;
    }
    if (other.rd().count != rd().count || other.rd().pbr != rd().pbr
        || other.rd().follow != rd().follow) {
        return false;
    }
    ensureNormalized();
    other.ensureNormalized();
    // The base is part of the value: it is what a whole-object write moves
    // and what a face returns to, so two lists resolving alike over
    // different bases are not the same list (12.4).
    const Data &a = rd();
    const Data &b = other.rd();
    return a.base.ambientColor == b.base.ambientColor
        && a.base.diffuseColor == b.base.diffuseColor
        && a.base.specularColor == b.base.specularColor
        && a.base.emissiveColor == b.base.emissiveColor
        && a.base.shininess == b.base.shininess
        && a.base.image == b.base.image
        && a.base.imagePath == b.base.imagePath
        && a.base.uuid == b.base.uuid
        && a.base.materialx == b.base.materialx
        && a.base.getType() == b.base.getType()
        && a.base.finish == b.base.finish
        && a.base.texture == b.base.texture
        && a.overrides == b.overrides
        && a.ambient == b.ambient
        && a.diffuse == b.diffuse
        && a.specular == b.specular
        && a.emissive == b.emissive
        && a.shininess == b.shininess
        && a.image == b.image
        && a.imagePath == b.imagePath
        && a.uuid == b.uuid
        && a.materialx == b.materialx
        && a.type == b.type
        && a.finish == b.finish
        // Normalised on both sides, and the normal form is canonical
        // (first-use order), so the pair compares as the values do
        && a.texturePalette == b.texturePalette
        && a.textureIndex == b.textureIndex;
}
