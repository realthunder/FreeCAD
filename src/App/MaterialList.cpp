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
#include <set>

#include <Base/Exception.h>

#include "MaterialList.h"

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

/// Resolve one entry of a field that may be 0, 1 or count long
template<class T>
inline const T &fieldAt(const std::vector<T> &values, int idx, const T &def)
{
    if (values.empty())
        return def;
    return values.size() == 1 ? values.front() : values[idx];
}

/// Collapse a field to the smallest of 0, 1 and its current length
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

/// Grow a field so that one entry can differ from the others
template<class T>
void expandField(std::vector<T> &values, int count, const T &def)
{
    if (static_cast<int>(values.size()) == count)
        return;
    // by value: assign() frees the old buffer before it copies, so handing
    // it a reference into that buffer is a use after free
    const T current = values.empty() ? def : values.front();
    values.assign(count, current);
}

/** Follow a change of entry count, without materialising a uniform field
 *
 * A field only has to be written out entry by entry when the value arriving
 * disagrees with the one already there -- which is what keeps a growing
 * import of identically coloured faces linear.
 */
template<class T>
void resizeField(std::vector<T> &values, int oldCount, int newCount,
                 const T &fill, const T &def)
{
    if (newCount < oldCount) {
        if (newCount == 0)
            std::vector<T>().swap(values);
        else if (static_cast<int>(values.size()) > newCount && values.size() > 1)
            values.resize(newCount);
        return;
    }
    if (newCount == oldCount)
        return;
    const T current = values.empty() ? def : values.front();
    if (values.size() <= 1 && current == fill)
        return;
    if (values.size() <= 1)
        values.assign(oldCount, current);
    values.resize(newCount, fill);
}

/// Write one entry of a field, expanding it only if the value is new
template<class T>
bool setFieldAt(std::vector<T> &values, int idx, int count, const T &value, const T &def)
{
    if (fieldAt(values, idx, def) == value)
        return false;
    expandField(values, count, def);
    values[idx] = value;
    return true;
}

//--------------------------------------------------------------------------
// The palette+index field
//
// The same five operations the dense helpers above provide, over a pair of
// vectors instead of one. The invariant every one of them restores: an
// empty index means the palette holds 0 or 1 entries, and a non-empty one
// is exactly count long with every value addressing the palette.
//--------------------------------------------------------------------------

/// Resolve one entry of a palette+index field
const SurfaceTexture &paletteAt(const std::vector<SurfaceTexture> &palette,
                                const std::vector<uint16_t> &index,
                                int idx, const SurfaceTexture &def)
{
    if (palette.empty())
        return def;
    if (index.empty())
        return palette.front();
    if (idx < 0 || idx >= static_cast<int>(index.size()))
        return def;
    const std::size_t slot = index[idx];
    return slot < palette.size() ? palette[slot] : def;
}

/// The slot holding \a value, appending it if the palette does not have it
uint16_t paletteSlot(std::vector<SurfaceTexture> &palette, const SurfaceTexture &value)
{
    for (std::size_t i = 0; i < palette.size(); ++i) {
        if (palette[i] == value)
            return static_cast<uint16_t>(i);
    }
    if (palette.size() >= MaterialList::MaxPaletteSize)
        throw Base::ValueError("too many distinct textures");
    palette.push_back(value);
    return static_cast<uint16_t>(palette.size() - 1);
}

/// Materialise the index so one entry can differ from the others
void expandPalette(std::vector<SurfaceTexture> &palette, std::vector<uint16_t> &index,
                   int count, const SurfaceTexture &def)
{
    if (static_cast<int>(index.size()) == count && !palette.empty())
        return;
    // by value: the current uniform value can be palette.front(), and the
    // assign below reallocates the buffer it lives in
    const SurfaceTexture current = palette.empty() ? def : palette.front();
    std::vector<SurfaceTexture>(1, current).swap(palette);
    index.assign(count, 0);
}

/// Rebuild a palette+index field into the smallest of its three forms
void collapsePalette(std::vector<SurfaceTexture> &palette, std::vector<uint16_t> &index,
                     int count, const SurfaceTexture &def)
{
    if (palette.empty() || count == 0) {
        // swap rather than clear: a palette read from a large document
        // should give the memory back, not merely stop counting it
        std::vector<SurfaceTexture>().swap(palette);
        std::vector<uint16_t>().swap(index);
        return;
    }
    if (!index.empty()) {
        // Renumber into first-use order, which drops both the slots
        // nothing points at any more and any duplicate a caller wrote
        std::vector<SurfaceTexture> used;
        std::vector<uint16_t> renumbered;
        renumbered.reserve(index.size());
        for (uint16_t slot : index) {
            renumbered.push_back(paletteSlot(
                used, slot < palette.size() ? palette[slot] : def));
        }
        used.swap(palette);
        renumbered.swap(index);
        if (palette.size() > 1)
            return;   // genuinely varies: the index earns its two bytes
        std::vector<uint16_t>().swap(index);
    }
    // Uniform, so the index is gone and one record says it all -- unless
    // that record is the default, which an empty palette already says
    if (palette.front() == def)
        std::vector<SurfaceTexture>().swap(palette);
    else if (palette.size() > 1)
        std::vector<SurfaceTexture>(1, palette.front()).swap(palette);
}

/// Follow a change of entry count, without materialising a uniform field
void resizePalette(std::vector<SurfaceTexture> &palette, std::vector<uint16_t> &index,
                   int oldCount, int newCount, const SurfaceTexture &fill,
                   const SurfaceTexture &def)
{
    if (newCount < oldCount) {
        if (newCount == 0) {
            std::vector<SurfaceTexture>().swap(palette);
            std::vector<uint16_t>().swap(index);
        }
        else if (static_cast<int>(index.size()) > newCount) {
            index.resize(newCount);   // collapse prunes the palette after
        }
        return;
    }
    if (newCount == oldCount)
        return;
    const SurfaceTexture current = palette.empty() ? def : palette.front();
    if (index.empty() && current == fill)
        return;
    if (index.empty())
        expandPalette(palette, index, oldCount, def);
    index.resize(newCount, paletteSlot(palette, fill));
}

/// Write one entry, materialising the index only if the value is new
bool setPaletteAt(std::vector<SurfaceTexture> &palette, std::vector<uint16_t> &index,
                  int idx, int count, const SurfaceTexture &value,
                  const SurfaceTexture &def)
{
    if (paletteAt(palette, index, idx, def) == value)
        return false;
    expandPalette(palette, index, count, def);
    index[idx] = paletteSlot(palette, value);
    return true;
}

/// Lay a whole run in, building the palette from what is distinct in it
void assignPalette(std::vector<SurfaceTexture> &palette, std::vector<uint16_t> &index,
                   const std::vector<SurfaceTexture> &values, const SurfaceTexture &def)
{
    std::vector<SurfaceTexture>().swap(palette);
    std::vector<uint16_t>().swap(index);
    index.reserve(values.size());
    for (const auto &value : values)
        index.push_back(paletteSlot(palette, value));
    collapsePalette(palette, index, static_cast<int>(values.size()), def);
}

} // namespace

/// The diffuse colour a whole material stores: its transparency field is the
/// truth and the alpha holds its complement (see the storage note in the
/// header).
Color MaterialList::storedDiffuse(const Material &mat)
{
    Color color = mat.diffuseColor;
    color.setTransparency(mat.transparency);
    return color;
}

/// The finish a whole material stores: clamped, so that everything reading
/// this property back gets a record it can actually draw, whatever a caller
/// assembled by hand or a file happened to state
SurfaceFinish MaterialList::storedFinish(const SurfaceFinish &finish)
{
    SurfaceFinish stored = finish;
    stored.normalize();
    return stored;
}

/// The texture a whole material stores, clamped for the same reason -- and
/// so that two records differing only in a transform nobody stated land in
/// one palette slot rather than two
SurfaceTexture MaterialList::storedTexture(const SurfaceTexture &texture)
{
    SurfaceTexture stored = texture;
    stored.normalize();
    return stored;
}

// ==================== blobs ====================
std::string MaterialList::insertTextureFile(const char *path, const char *extension)
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

std::string MaterialList::getTextureFile(const std::string &hash) const
{
    const auto it = rd().textureBlobs.find(hash);
    return it == rd().textureBlobs.end() ? std::string() : it->second->path();
}

void MaterialList::noteTextureBlobs(FileBlobManager &manager,
                                            const BlobReferrer &referrer) const
{
    ensureNormalized();
    for (const auto &value : rd().texturePalette) {
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
}

void MaterialList::assignRestoredBlob(const FileBlobHandle &blob)
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
bool MaterialList::holdsEveryNamedBlob() const
{
    for (const auto &value : rd().texturePalette) {
        for (const auto &hash : value.maps) {
            if (!hash.empty() && rd().textureBlobs.find(hash) == rd().textureBlobs.end()) {
                return false;
            }
        }
    }
    return true;
}

void MaterialList::holdTextureBlob(const FileBlobHandle &blob)
{
    if (blob) {
        bd().textureBlobs[blob->hash()] = blob;
    }
}

std::vector<std::string> MaterialList::getTextureHashes() const
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
void MaterialList::pruneTextureBlobs()
{
    if (rd().textureBlobs.empty()) {
        return;
    }
    std::set<std::string> named;
    for (const auto &value : rd().texturePalette) {
        for (const auto &hash : value.maps) {
            if (!hash.empty()) {
                named.insert(hash);
            }
        }
    }
    Data &d = bd();
    for (auto it = d.textureBlobs.begin(); it != d.textureBlobs.end();) {
        it = named.count(it->first) ? std::next(it) : d.textureBlobs.erase(it);
    }
}

// ==================== default ====================
const Material &MaterialList::defaultMaterial()
{
    static const Material def;
    return def;
}

// ==================== the fields, as stored ====================

int MaterialList::getSize() const
{
    return rd().count;
}

bool MaterialList::isPBR() const
{
    return rd().pbr;
}

const std::vector<Color> &MaterialList::getAmbientColors() const
{
    return rd().ambient;
}

const std::vector<Color> &MaterialList::getDiffuseColors() const
{
    return rd().diffuse;
}

const std::vector<Color> &MaterialList::getSpecularColors() const
{
    return rd().specular;
}

const std::vector<Color> &MaterialList::getEmissiveColors() const
{
    return rd().emissive;
}

const std::vector<float> &MaterialList::getShininessValues() const
{
    return rd().shininess;
}

const std::vector<std::string> &MaterialList::getImages() const
{
    return rd().image;
}

const std::vector<std::string> &MaterialList::getImagePaths() const
{
    return rd().imagePath;
}

const std::vector<std::string> &MaterialList::getUuids() const
{
    return rd().uuid;
}

const std::vector<int8_t> &MaterialList::getTypes() const
{
    return rd().type;
}

const std::vector<SurfaceFinish> &MaterialList::getFinishes() const
{
    ensureNormalized();
    return rd().finish;
}

const std::vector<SurfaceTexture> &MaterialList::getTexturePalette() const
{
    ensureNormalized();
    return rd().texturePalette;
}

const std::vector<uint16_t> &MaterialList::getTextureIndex() const
{
    ensureNormalized();
    return rd().textureIndex;
}

bool MaterialList::hasTextureOrCard() const
{
    return !rd().image.empty() || !rd().imagePath.empty() || !rd().uuid.empty();
}

bool MaterialList::hasFinish() const
{
    // Normalised, so a finish written and then cleared answers false rather
    // than "there is still an array there"
    ensureNormalized();
    return !rd().finish.empty();
}

bool MaterialList::hasTexture() const
{
    // A palette entry survives collapse only while some entry resolves to
    // it, so an empty palette is the whole answer
    ensureNormalized();
    return !rd().texturePalette.empty();
}

// ==================== storage ====================
void MaterialList::touchFields()
{
    wd().normalized = false;
}

void MaterialList::normalize() const
{
    // Nothing stored is already the normal form, and writing through nd()
    // here would scribble on the shared empty value it answers with
    if (_data.isNull()) {
        return;
    }
    Data &d = nd();
    const Material &def = defaultMaterial();
    if (d.count == 0) {
        std::vector<Color>().swap(d.ambient);
        std::vector<Color>().swap(d.diffuse);
        std::vector<Color>().swap(d.specular);
        std::vector<Color>().swap(d.emissive);
        std::vector<float>().swap(d.shininess);
        std::vector<std::string>().swap(d.image);
        std::vector<std::string>().swap(d.imagePath);
        std::vector<std::string>().swap(d.uuid);
        std::vector<int8_t>().swap(d.type);
        std::vector<SurfaceFinish>().swap(d.finish);
        std::vector<SurfaceTexture>().swap(d.texturePalette);
        std::vector<uint16_t>().swap(d.textureIndex);
    }
    else {
        collapseField(d.ambient, def.ambientColor);
        collapseField(d.diffuse, storedDiffuse(def));
        collapseField(d.specular, specularDefault());
        collapseField(d.emissive, def.emissiveColor);
        collapseField(d.shininess, shininessDefault());
        collapseField(d.image, def.image);
        collapseField(d.imagePath, def.imagePath);
        collapseField(d.uuid, def.uuid);
        collapseField(d.type, static_cast<int8_t>(def.getType()));
        collapseField(d.finish, def.finish);
        collapsePalette(d.texturePalette, d.textureIndex, d.count, def.texture);
    }
    d.normalized = true;
}

void MaterialList::ensureNormalized() const
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

bool MaterialList::variesOnlyInDiffuse() const
{
    ensureNormalized();
    // Normalised, so a field is 0, 1 or rd().count: anything above one is a field
    // that genuinely differs from entry to entry. Per-entry transparency is
    // the diffuse alpha, so it is variance a colour list CAN express and is
    // deliberately not tested here.
    return rd().ambient.size() <= 1 && rd().specular.size() <= 1 && rd().emissive.size() <= 1
        && rd().shininess.size() <= 1 && rd().type.size() <= 1
        && rd().image.size() <= 1 && rd().imagePath.size() <= 1 && rd().uuid.size() <= 1
        && rd().finish.size() <= 1
        // Normalised, so an index exists only while the palette genuinely
        // varies -- the uniform and all-default forms have none
        && rd().textureIndex.empty();
}

//**************************************************************************
// PBR mode

const Color &MaterialList::specularDefault() const
{
    // White F0 tint, metallic 0 in the alpha: the natural unset PBR
    // surface. The Phong default's alpha of one would read as full metal.
    static const Color pbrDef(1.0f, 1.0f, 1.0f, 0.0f);
    return rd().pbr ? pbrDef : defaultMaterial().specularColor;
}

float MaterialList::shininessDefault() const
{
    return rd().pbr ? 0.5f : defaultMaterial().shininess;
}

void MaterialList::requirePBR() const
{
    if (!rd().pbr)
        throw Base::RuntimeError("material list is not in PBR mode");
}

Material MaterialList::inMode(const Material &mat) const
{
    if (mat.pbr == rd().pbr)
        return mat;
    Material converted = mat;
    converted.setPBR(rd().pbr);
    return converted;
}

void MaterialList::setPBR(bool enable)
{
    if (rd().pbr == enable)
        return;
    // The collapse baselines follow the mode
    touchFields();
    wd().pbr = enable;
}

void MaterialList::convertPBR(bool enable)
{
    if (wd().pbr == enable)
        return;
    // Convert while the old mode still governs the readings, then flip.
    // Per entry, so per-face variation converts entry by entry; normalize
    // collapses whatever stays uniform afterwards.
    std::vector<Material> values;
    values.reserve(wd().count);
    for (int i = 0; i < wd().count; ++i) {
        // Tagged with the mode they are in, so the conversion is the
        // value's own; setValues below then reads the new mode off them
        Material mat = getMaterial(i);
        mat.setPBR(enable);
        values.push_back(mat);
    }
    setPBR(enable);
    setValues(std::move(values));
}

float MaterialList::getMetallic(int idx) const
{
    if (!rd().pbr)
        return 0.0f;  // the Phong model has no metals
    return fieldAt(rd().specular, idx, specularDefault()).a;
}

float MaterialList::getRoughness(int idx) const
{
    const float value = fieldAt(rd().shininess, idx, shininessDefault());
    return rd().pbr ? value : Material::shininessToRoughness(value);
}

void MaterialList::setMetallicValues(const std::vector<float> &values)
{
    // The alphas of the specular field, exactly as setTransparencies works
    // the diffuse alphas: empty is back-to-default, 1 uniform, N per entry,
    // and every entry's tint rgb stays what it was.
    requirePBR();
    const Color def = specularDefault();
    const int newCount = static_cast<int>(values.size());
    std::vector<Color> colors = rd().specular;
    if (newCount == 0 || newCount == 1) {
        const float alpha = newCount ? values[0] : def.a;
        if (colors.empty())
            colors.push_back(def);
        for (auto &color : colors)
            color.a = alpha;
    }
    else {
        colors.resize(newCount, fieldAt(rd().specular, 0, def));
        for (int i = 0; i < newCount; ++i)
            colors[i].a = values[i];
    }
    setField(&Data::specular, colors, def);
}

void MaterialList::setRoughnessValues(const std::vector<float> &values)
{
    requirePBR();
    setField(&Data::shininess, values, shininessDefault());
}

void MaterialList::setMetallic(int idx, float value)
{
    requirePBR();
    Color color = getSpecularColor(idx < 0 || idx >= wd().count ? 0 : idx);
    color.a = value;
    setFieldValue(&Data::specular, idx, color, specularDefault());
}

void MaterialList::setRoughness(int idx, float value)
{
    requirePBR();
    setFieldValue(&Data::shininess, idx, value, shininessDefault());
}

void MaterialList::setMetallic(float value)
{
    requirePBR();
    if (wd().specular.size() <= 1) {
        Color color = fieldAt(wd().specular, 0, specularDefault());
        color.a = value;
        setUniformField(&Data::specular, color, specularDefault());
        return;
    }
    // Per-entry tints: only the alphas move
    bool changed = false;
    for (const auto &color : wd().specular) {
        if (color.a != value) {
            changed = true;
            break;
        }
    }
    if (!changed)
        return;
    touchFields();
    for (auto &color : wd().specular)
        color.a = value;
    normalize();
}

void MaterialList::setRoughness(float value)
{
    requirePBR();
    setUniformField(&Data::shininess, value, shininessDefault());
}

Material MaterialList::getPhongMaterial(int idx) const
{
    Material mat = getMaterial(idx);
    if (!rd().pbr)
        return mat;
    // Why the diffuse stays the base colour: see Material::pbrToPhong
    return Material::pbrToPhong(mat);
}

// ==================== restore ====================
void MaterialList::restoreValues(std::vector<Material> &&values, bool legacy)
{
    touchFields();
    wd().count = static_cast<int>(values.size());
    std::vector<float> transparency;
    std::vector<Color>().swap(wd().ambient);
    std::vector<Color>().swap(wd().diffuse);
    std::vector<Color>().swap(wd().specular);
    std::vector<Color>().swap(wd().emissive);
    std::vector<float>().swap(wd().shininess);
    std::vector<std::string>().swap(wd().image);
    std::vector<std::string>().swap(wd().imagePath);
    std::vector<std::string>().swap(wd().uuid);
    std::vector<int8_t>().swap(wd().type);
    // No wd().finish or texture here: none of the encodings that come through
    // this function can carry either, so every entry reads as unfinished
    // and untextured -- which the empty fields already say, at no cost.
    std::vector<SurfaceFinish>().swap(wd().finish);
    std::vector<SurfaceTexture>().swap(wd().texturePalette);
    std::vector<uint16_t>().swap(wd().textureIndex);
    if (wd().count) {
        wd().ambient.reserve(wd().count);
        wd().diffuse.reserve(wd().count);
        wd().specular.reserve(wd().count);
        wd().emissive.reserve(wd().count);
        wd().shininess.reserve(wd().count);
        transparency.reserve(wd().count);
        wd().image.reserve(wd().count);
        wd().imagePath.reserve(wd().count);
        wd().uuid.reserve(wd().count);
        wd().type.reserve(wd().count);
        for (auto &mat : values) {
            // The diffuse alpha exactly as the file states it; which of the
            // two slots is the entry's transparency is decided below, once
            // the legacy conversion has made them comparable.
            wd().ambient.push_back(mat.ambientColor);
            wd().diffuse.push_back(mat.diffuseColor);
            wd().specular.push_back(mat.specularColor);
            wd().emissive.push_back(mat.emissiveColor);
            wd().shininess.push_back(mat.shininess);
            transparency.push_back(mat.transparency);
            wd().image.push_back(std::move(mat.image));
            wd().imagePath.push_back(std::move(mat.imagePath));
            wd().uuid.push_back(std::move(mat.uuid));
            wd().type.push_back(static_cast<int8_t>(mat.getType()));
        }
        if (legacy) {
            // Every colour's alpha meant transparency: invert, as upstream's
            // convertAlphaInMaterial does. For the diffuse this makes the
            // alpha an opacity, so that the merge below can compare it with
            // the field.
            for (auto *field : {&wd().ambient, &wd().diffuse, &wd().specular, &wd().emissive}) {
                for (auto &color : *field)
                    convertAlpha(color);
            }
        }
        applyRestoredTransparency(transparency, legacy);
        normalize();
    }
    else {
        wd().normalized = true;
    }
}

void MaterialList::applyRestoredTransparency(const std::vector<float> &transparency,
                                                     bool legacy)
{
    if (transparency.empty() || wd().count == 0)
        return;
    // Sizes are 1 or wd().count, validated by every reader that fills the vector.
    const std::size_t n = transparency.size();
    expandField(wd().diffuse, wd().count, storedDiffuse(defaultMaterial()));
    for (int i = 0; i < wd().count; ++i) {
        const float field = transparency[n == 1 ? 0 : i];
        float &alpha = wd().diffuse[i].a;
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
void MaterialList::setSize(int newSize)
{
    // Grow with what an unset entry reads as, which follows the mode --
    // and say so, because these slots are already in this list's reading:
    // converting them again would state the mode's own default twice over
    Material def = defaultMaterial();
    def.specularColor = specularDefault();
    def.shininess = shininessDefault();
    def.pbr = rd().pbr;
    setSize(newSize, def);
}

void MaterialList::setSize(int newSize, const Material &fill)
{
    if (newSize == rd().count)
        return;
    if (newSize < 0)
        throw Base::ValueError("negative list size");

    // Growth fills entries of this list, so the filler reads as this list
    // does; only a whole-list assignment restates the mode
    const Material def = inMode(fill);
    touchFields();
    const Material &zero = defaultMaterial();
    resizeField(wd().ambient, wd().count, newSize, def.ambientColor, zero.ambientColor);
    resizeField(wd().diffuse, wd().count, newSize, storedDiffuse(def), storedDiffuse(zero));
    resizeField(wd().specular, wd().count, newSize, def.specularColor, specularDefault());
    resizeField(wd().emissive, wd().count, newSize, def.emissiveColor, zero.emissiveColor);
    resizeField(wd().shininess, wd().count, newSize, def.shininess, shininessDefault());
    resizeField(wd().image, wd().count, newSize, def.image, zero.image);
    resizeField(wd().imagePath, wd().count, newSize, def.imagePath, zero.imagePath);
    resizeField(wd().uuid, wd().count, newSize, def.uuid, zero.uuid);
    resizeField(wd().type, wd().count, newSize, static_cast<int8_t>(def.getType()),
                static_cast<int8_t>(zero.getType()));
    resizeField(wd().finish, wd().count, newSize, storedFinish(def.finish), zero.finish);
    resizePalette(wd().texturePalette, wd().textureIndex, wd().count, newSize,
                  storedTexture(def.texture), zero.texture);
    wd().count = newSize;
}

Material MaterialList::getMaterial(int idx) const
{
    Material mat;
    if (idx < 0 || idx >= rd().count)
        return mat;
    // The type goes on FIRST. Material::setType() rewrites every colour and
    // both floats with that type's preset, so a setType() after the fields
    // are laid in throws all of them away and the list hands back the
    // preset instead of what it stores -- silently, because the per field
    // getters below are unaffected and keep telling the truth.
    const Material &def = defaultMaterial();
    mat.setType(static_cast<Material::MaterialType>(
                fieldAt(rd().type, idx, static_cast<int8_t>(def.getType()))));
    mat.ambientColor = fieldAt(rd().ambient, idx, def.ambientColor);
    mat.diffuseColor = fieldAt(rd().diffuse, idx, storedDiffuse(def));
    mat.specularColor = fieldAt(rd().specular, idx, specularDefault());
    mat.emissiveColor = fieldAt(rd().emissive, idx, def.emissiveColor);
    mat.shininess = fieldAt(rd().shininess, idx, shininessDefault());
    // One quantity, two slots: the material handed out is always consistent,
    // whatever inconsistent pair was once handed in.
    mat.transparency = mat.diffuseColor.transparency();
    mat.image = fieldAt(rd().image, idx, def.image);
    mat.imagePath = fieldAt(rd().imagePath, idx, def.imagePath);
    mat.uuid = fieldAt(rd().uuid, idx, def.uuid);
    mat.finish = fieldAt(rd().finish, idx, def.finish);
    mat.texture = paletteAt(rd().texturePalette, rd().textureIndex, idx, def.texture);
    // Stamp the list's mode on the value, so whoever holds it still knows
    // which reading its slots are in
    mat.pbr = rd().pbr;
    return mat;
}

void MaterialList::setValue(const Material &mat)
{
    setValues(std::vector<Material>(1, mat));
}

void MaterialList::setValues(const std::vector<Material> &values)
{
    // The list holds ONE mode, and an assignment states it through the
    // material it starts with; the rest are converted to that reading
    // rather than landing their slots under the wrong one. An empty
    // assignment states nothing, so the mode it finds stands.
    if (!values.empty())
        setPBR(values.front().pbr);
    touchFields();
    wd().count = static_cast<int>(values.size());
    std::vector<Color>().swap(wd().ambient);
    std::vector<Color>().swap(wd().diffuse);
    std::vector<Color>().swap(wd().specular);
    std::vector<Color>().swap(wd().emissive);
    std::vector<float>().swap(wd().shininess);
    std::vector<std::string>().swap(wd().image);
    std::vector<std::string>().swap(wd().imagePath);
    std::vector<std::string>().swap(wd().uuid);
    std::vector<int8_t>().swap(wd().type);
    std::vector<SurfaceFinish>().swap(wd().finish);
    std::vector<SurfaceTexture> textures;
    std::vector<SurfaceTexture>().swap(wd().texturePalette);
    std::vector<uint16_t>().swap(wd().textureIndex);
    if (wd().count) {
        wd().ambient.reserve(wd().count);
        wd().diffuse.reserve(wd().count);
        wd().specular.reserve(wd().count);
        wd().emissive.reserve(wd().count);
        wd().shininess.reserve(wd().count);
        wd().image.reserve(wd().count);
        wd().imagePath.reserve(wd().count);
        wd().uuid.reserve(wd().count);
        wd().type.reserve(wd().count);
        wd().finish.reserve(wd().count);
        textures.reserve(wd().count);
        for (const auto &value : values) {
            const Material mat = inMode(value);
            wd().ambient.push_back(mat.ambientColor);
            wd().diffuse.push_back(storedDiffuse(mat));
            wd().specular.push_back(mat.specularColor);
            wd().emissive.push_back(mat.emissiveColor);
            wd().shininess.push_back(mat.shininess);
            wd().image.push_back(mat.image);
            wd().imagePath.push_back(mat.imagePath);
            wd().uuid.push_back(mat.uuid);
            wd().type.push_back(static_cast<int8_t>(mat.getType()));
            wd().finish.push_back(storedFinish(mat.finish));
            textures.push_back(storedTexture(mat.texture));
        }
        // Dense in, palette out: assignPalette collapses too, so the
        // normalize() below finds this field already in its normal form
        assignPalette(wd().texturePalette, wd().textureIndex, textures,
                      defaultMaterial().texture);
        normalize();
        pruneTextureBlobs();
    }
    else {
        wd().normalized = true;
    }
}

void MaterialList::set1Value(int idx, const Material &value)
{
    if (idx < -1 || idx > rd().count)
        throw Base::RuntimeError("index out of bound");

    // One entry cannot restate the whole list's mode, so it is converted
    // to it instead
    const Material mat = inMode(value);
    if (idx == -1 || idx == rd().count) {
        idx = rd().count;
        setSize(rd().count + 1, mat);
    }
    else {
        if (getMaterial(idx) == mat)
            return;
        touchFields();
        const Material &def = defaultMaterial();
        setFieldAt(wd().ambient, idx, wd().count, mat.ambientColor, def.ambientColor);
        setFieldAt(wd().diffuse, idx, wd().count, storedDiffuse(mat), storedDiffuse(def));
        setFieldAt(wd().specular, idx, wd().count, mat.specularColor, specularDefault());
        setFieldAt(wd().emissive, idx, wd().count, mat.emissiveColor, def.emissiveColor);
        setFieldAt(wd().shininess, idx, wd().count, mat.shininess, shininessDefault());
        setFieldAt(wd().image, idx, wd().count, mat.image, def.image);
        setFieldAt(wd().imagePath, idx, wd().count, mat.imagePath, def.imagePath);
        setFieldAt(wd().uuid, idx, wd().count, mat.uuid, def.uuid);
        setFieldAt(wd().type, idx, wd().count, static_cast<int8_t>(mat.getType()),
                   static_cast<int8_t>(def.getType()));
        setFieldAt(wd().finish, idx, wd().count, storedFinish(mat.finish), def.finish);
        setPaletteAt(wd().texturePalette, wd().textureIndex, idx, wd().count,
                     storedTexture(mat.texture), def.texture);
    }
}

//**************************************************************************
// Per field access

Color MaterialList::getAmbientColor(int idx) const
{
    return fieldAt(rd().ambient, idx, defaultMaterial().ambientColor);
}

Color MaterialList::getDiffuseColor(int idx) const
{
    return fieldAt(rd().diffuse, idx, storedDiffuse(defaultMaterial()));
}

Color MaterialList::getSpecularColor(int idx) const
{
    return fieldAt(rd().specular, idx, specularDefault());
}

Color MaterialList::getEmissiveColor(int idx) const
{
    return fieldAt(rd().emissive, idx, defaultMaterial().emissiveColor);
}

float MaterialList::getShininess(int idx) const
{
    return fieldAt(rd().shininess, idx, shininessDefault());
}

float MaterialList::getTransparency(int idx) const
{
    // The complement of the diffuse alpha; there is no second store.
    return getDiffuseColor(idx).transparency();
}

const std::string &MaterialList::getImage(int idx) const
{
    return fieldAt(rd().image, idx, defaultMaterial().image);
}

const std::string &MaterialList::getImagePath(int idx) const
{
    return fieldAt(rd().imagePath, idx, defaultMaterial().imagePath);
}

const std::string &MaterialList::getUuid(int idx) const
{
    return fieldAt(rd().uuid, idx, defaultMaterial().uuid);
}

SurfaceFinish MaterialList::getFinish(int idx) const
{
    return fieldAt(rd().finish, idx, defaultMaterial().finish);
}

SurfaceTexture MaterialList::getTexture(int idx) const
{
    return paletteAt(rd().texturePalette, rd().textureIndex, idx, defaultMaterial().texture);
}

Material::MaterialType MaterialList::getType(int idx) const
{
    return static_cast<Material::MaterialType>(
            fieldAt(rd().type, idx, static_cast<int8_t>(defaultMaterial().getType())));
}

/** Take a whole field
 *
 * A field of one is uniform and a field as long as the list is per entry.
 * A vector that is neither is a statement about how long the list should
 * be, the way assigning a colour list of a different length is; an empty
 * one returns the field to its default without disturbing the count. The
 * values normalise on the way in.
 */
template<class T>
void MaterialList::setField(std::vector<T> Data::*member, const std::vector<T> &values,
                            const T &def)
{
    if (rd().*member == values)
        return;
    const int newCount = static_cast<int>(values.size());
    if (newCount != rd().count && (newCount > 1 || rd().count == 0)) {
        // Growth extends the LAST entry's material, not the default: the
        // caller is stating one field for N entries and saying nothing about
        // the others, so the others must not change meaning -- and for a
        // uniform field, extending its own value keeps it stored as one
        // element where a default fill would materialise N of them and make
        // the appearance "vary" in fields nobody set. An empty list grows
        // with what its entries read as, which follows the mode.
        if (rd().count)
            setSize(newCount, getMaterial(rd().count - 1));
        else
            setSize(newCount);
    }
    touchFields();
    std::vector<T> &field = wd().*member;
    field = values;
    collapseField(field, def);
}

void MaterialList::setAmbientColors(const std::vector<Color> &colors)
{
    setField(&Data::ambient, colors, defaultMaterial().ambientColor);
}

void MaterialList::setDiffuseColors(const std::vector<Color> &colors)
{
    setField(&Data::diffuse, colors, storedDiffuse(defaultMaterial()));
}

void MaterialList::setSpecularColors(const std::vector<Color> &colors)
{
    setField(&Data::specular, colors, specularDefault());
}

void MaterialList::setEmissiveColors(const std::vector<Color> &colors)
{
    setField(&Data::emissive, colors, defaultMaterial().emissiveColor);
}

void MaterialList::setShininessValues(const std::vector<float> &values)
{
    setField(&Data::shininess, values, shininessDefault());
}

void MaterialList::setTransparencies(const std::vector<float> &values)
{
    // The alphas of the diffuse field, at the sizes the old float field
    // accepted: empty is back-to-default, 1 is uniform, N per entry -- and a
    // different N resizes the list, as assigning any field does. The rgb of
    // every entry stays what it was; only the alphas move.
    const Color def = storedDiffuse(defaultMaterial());
    const int newCount = static_cast<int>(values.size());
    std::vector<Color> colors = rd().diffuse;
    if (newCount == 0 || newCount == 1) {
        const float alpha = newCount ? 1.0F - values[0] : def.a;
        if (colors.empty())
            colors.push_back(def);
        for (auto &color : colors)
            color.a = alpha;
    }
    else {
        // Per entry: the rgb comes along, from wherever the field has it.
        colors.resize(newCount, fieldAt(rd().diffuse, 0, def));
        for (int i = 0; i < newCount; ++i)
            colors[i].setTransparency(values[i]);
    }
    setField(&Data::diffuse, colors, def);
}

void MaterialList::setImages(const std::vector<std::string> &values)
{
    setField(&Data::image, values, defaultMaterial().image);
}

void MaterialList::setImagePaths(const std::vector<std::string> &values)
{
    setField(&Data::imagePath, values, defaultMaterial().imagePath);
}

void MaterialList::setUuids(const std::vector<std::string> &values)
{
    setField(&Data::uuid, values, defaultMaterial().uuid);
}

void MaterialList::setFinishes(const std::vector<SurfaceFinish> &values)
{
    std::vector<SurfaceFinish> clamped;
    clamped.reserve(values.size());
    for (const auto &value : values)
        clamped.push_back(storedFinish(value));
    setField(&Data::finish, clamped, defaultMaterial().finish);
}

void MaterialList::setTextures(const std::vector<SurfaceTexture> &values)
{
    const SurfaceTexture &def = defaultMaterial().texture;
    std::vector<SurfaceTexture> clamped;
    clamped.reserve(values.size());
    for (const auto &value : values)
        clamped.push_back(storedTexture(value));

    std::vector<SurfaceTexture> palette;
    std::vector<uint16_t> index;
    assignPalette(palette, index, clamped, def);
    // assignPalette produces the canonical form -- first-use order, and
    // the smallest of the three shapes -- so comparing the pair is
    // comparing what the field means, not how it happens to be stored
    ensureNormalized();
    if (static_cast<int>(clamped.size()) == rd().count
        && palette == rd().texturePalette && index == rd().textureIndex)
        return;

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
    touchFields();
    palette.swap(wd().texturePalette);
    index.swap(wd().textureIndex);
    pruneTextureBlobs();
}

/// Write one entry of one field, growing the list if it names a new entry
template<class T>
void MaterialList::setFieldValue(std::vector<T> Data::*member, int idx, const T &value,
                                 const T &def)
{
    if (idx < 0 || idx > rd().count)
        throw Base::RuntimeError("index out of bound");
    if (idx == rd().count) {
        setSize(rd().count + 1);
    }
    else if (fieldAt(rd().*member, idx, def) == value) {
        // Unchanged, and nothing has been written: the storage is still
        // whatever it was, which is what tells the property there is no
        // change to record
        return;
    }
    touchFields();
    setFieldAt(wd().*member, idx, rd().count, value, def);
}

void MaterialList::setAmbientColor(int idx, const Color &col)
{
    setFieldValue(&Data::ambient, idx, col, defaultMaterial().ambientColor);
}

void MaterialList::setDiffuseColor(int idx, const Color &col)
{
    setFieldValue(&Data::diffuse, idx, col, storedDiffuse(defaultMaterial()));
}

void MaterialList::setSpecularColor(int idx, const Color &col)
{
    setFieldValue(&Data::specular, idx, col, specularDefault());
}

void MaterialList::setEmissiveColor(int idx, const Color &col)
{
    setFieldValue(&Data::emissive, idx, col, defaultMaterial().emissiveColor);
}

void MaterialList::setShininess(int idx, float value)
{
    setFieldValue(&Data::shininess, idx, value, shininessDefault());
}

void MaterialList::setTransparency(int idx, float value)
{
    // One entry's alpha: the same write as setDiffuseColor of that entry
    // with only the alpha changed, and it shares that setter's growth and
    // early-out behaviour.
    Color color = getDiffuseColor(idx < 0 || idx >= wd().count ? 0 : idx);
    color.setTransparency(value);
    setFieldValue(&Data::diffuse, idx, color, storedDiffuse(defaultMaterial()));
}

void MaterialList::setImage(int idx, const std::string &value)
{
    setFieldValue(&Data::image, idx, value, defaultMaterial().image);
}

void MaterialList::setImagePath(int idx, const std::string &value)
{
    setFieldValue(&Data::imagePath, idx, value, defaultMaterial().imagePath);
}

void MaterialList::setUuid(int idx, const std::string &value)
{
    setFieldValue(&Data::uuid, idx, value, defaultMaterial().uuid);
}

void MaterialList::setFinish(int idx, const SurfaceFinish &value)
{
    setFieldValue(&Data::finish, idx, storedFinish(value), defaultMaterial().finish);
}

void MaterialList::setTexture(int idx, const SurfaceTexture &value)
{
    // setFieldValue's body, over the pair: the palette is not a field the
    // member pointer form can address
    if (idx < 0 || idx > rd().count)
        throw Base::RuntimeError("index out of bound");
    const SurfaceTexture stored = storedTexture(value);
    const SurfaceTexture &def = defaultMaterial().texture;
    if (idx == rd().count) {
        setSize(rd().count + 1);
    }
    else if (paletteAt(rd().texturePalette, rd().textureIndex, idx, def) == stored) {
        return;
    }
    touchFields();
    Data &d = wd();
    setPaletteAt(d.texturePalette, d.textureIndex, idx, d.count, stored, def);
}

/// Give every entry the same value for one field, and none of it to storage
template<class T>
void MaterialList::setUniformField(std::vector<T> Data::*member, const T &value, const T &def)
{
    if ((rd().*member).empty() && value == def)
        return;  // already the default everywhere, including on an empty list
    if (rd().count && (rd().*member).size() <= 1 && fieldAt(rd().*member, 0, def) == value)
        return;
    touchFields();
    if (rd().count == 0)
        setSize(1);
    std::vector<T> &field = wd().*member;
    if (value == def)
        std::vector<T>().swap(field);
    else
        std::vector<T>(1, value).swap(field);
}

void MaterialList::setAmbientColor(const Color &col)
{
    setUniformField(&Data::ambient, col, defaultMaterial().ambientColor);
}

void MaterialList::setDiffuseColor(const Color &col)
{
    setUniformField(&Data::diffuse, col, storedDiffuse(defaultMaterial()));
}

void MaterialList::setSpecularColor(const Color &col)
{
    setUniformField(&Data::specular, col, specularDefault());
}

void MaterialList::setEmissiveColor(const Color &col)
{
    setUniformField(&Data::emissive, col, defaultMaterial().emissiveColor);
}

void MaterialList::setShininess(float value)
{
    setUniformField(&Data::shininess, value, shininessDefault());
}

void MaterialList::setTransparency(float value)
{
    // Every entry's alpha, leaving every entry's rgb alone -- which on a per
    // face field is NOT a uniform write of one colour.
    if (wd().diffuse.size() <= 1) {
        Color color = fieldAt(wd().diffuse, 0, storedDiffuse(defaultMaterial()));
        color.setTransparency(value);
        setUniformField(&Data::diffuse, color, storedDiffuse(defaultMaterial()));
        return;
    }
    const float alpha = 1.0F - value;
    bool changed = false;
    for (const auto &color : wd().diffuse) {
        if (color.a != alpha) {
            changed = true;
            break;
        }
    }
    if (!changed)
        return;
    touchFields();
    for (auto &color : wd().diffuse)
        color.a = alpha;
    normalize();
}

void MaterialList::setDiffuseRGB(const Color &col)
{
    setFieldRGB(&Data::diffuse, col, storedDiffuse(defaultMaterial()));
}

void MaterialList::setSpecularRGB(const Color &col)
{
    setFieldRGB(&Data::specular, col, specularDefault());
}

void MaterialList::setFieldRGB(std::vector<Color> Data::*member, const Color &col,
                               const Color &def)
{
    const std::vector<Color> &current = rd().*member;
    // setTransparency's mirror image: every entry's rgb, leaving every
    // entry's alpha alone -- the diffuse alpha is the opacity and the PBR
    // specular alpha is the metallic, so on a per entry field this is NOT
    // a uniform write of one colour.
    if (current.size() <= 1) {
        Color color = fieldAt(current, 0, def);
        color.r = col.r;
        color.g = col.g;
        color.b = col.b;
        setUniformField(member, color, def);
        return;
    }
    bool changed = false;
    for (const auto &color : current) {
        if (color.r != col.r || color.g != col.g || color.b != col.b) {
            changed = true;
            break;
        }
    }
    if (!changed)
        return;
    touchFields();
    for (auto &color : wd().*member) {
        color.r = col.r;
        color.g = col.g;
        color.b = col.b;
    }
    normalize();
}

//**************************************************************************
// Base class implementer

void MaterialList::setImage(const std::string &value)
{
    setUniformField(&Data::image, value, defaultMaterial().image);
}

void MaterialList::setImagePath(const std::string &value)
{
    setUniformField(&Data::imagePath, value, defaultMaterial().imagePath);
}

void MaterialList::setUuid(const std::string &value)
{
    setUniformField(&Data::uuid, value, defaultMaterial().uuid);
}

void MaterialList::setFinish(const SurfaceFinish &value)
{
    setUniformField(&Data::finish, storedFinish(value), defaultMaterial().finish);
}

void MaterialList::setTexture(const SurfaceTexture &value)
{
    // setUniformField's body, over the pair. Uniform is the form with no
    // index at all, so this drops one wherever it found one.
    const SurfaceTexture stored = storedTexture(value);
    const SurfaceTexture &def = defaultMaterial().texture;
    if (wd().texturePalette.empty() && stored == def)
        return;  // already the default everywhere, including on an empty list
    if (wd().count && wd().textureIndex.empty() && wd().texturePalette.size() == 1
        && wd().texturePalette.front() == stored)
        return;
    touchFields();
    if (wd().count == 0)
        setSize(1);
    std::vector<uint16_t>().swap(wd().textureIndex);
    if (stored == def)
        std::vector<SurfaceTexture>().swap(wd().texturePalette);
    else
        std::vector<SurfaceTexture>(1, stored).swap(wd().texturePalette);
}

// ==================== memsize ====================
unsigned int MaterialList::getMemSize() const
{
    ensureNormalized();
    return static_cast<unsigned int>(
            (rd().ambient.size() + rd().diffuse.size() + rd().specular.size() + rd().emissive.size())
                * sizeof(Color)
            + rd().shininess.size() * sizeof(float)
            + rd().type.size() * sizeof(int8_t)
            + rd().finish.size() * sizeof(SurfaceFinish)
            + texturesMemSize(rd().texturePalette)
            + rd().textureIndex.size() * sizeof(uint16_t)
            + stringsMemSize(rd().image) + stringsMemSize(rd().imagePath)
            + stringsMemSize(rd().uuid));
}


// ==================== issame ====================
bool MaterialList::isSame(const MaterialList &other) const
{
    if (&other == this || _data.isSameData(other._data)) {
        return true;
    }
    if (other.rd().count != rd().count || other.rd().pbr != rd().pbr) {
        return false;
    }
    ensureNormalized();
    other.ensureNormalized();
    return rd().ambient == other.rd().ambient
        && rd().diffuse == other.rd().diffuse
        && rd().specular == other.rd().specular
        && rd().emissive == other.rd().emissive
        && rd().shininess == other.rd().shininess
        && rd().image == other.rd().image
        && rd().imagePath == other.rd().imagePath
        && rd().uuid == other.rd().uuid
        && rd().type == other.rd().type
        && rd().finish == other.rd().finish
        // Normalised on both sides, and the normal form is canonical
        // (first-use order), so the pair compares as the values do
        && rd().texturePalette == other.rd().texturePalette
        && rd().textureIndex == other.rd().textureIndex;
}
