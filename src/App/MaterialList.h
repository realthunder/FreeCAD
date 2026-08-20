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

#ifndef APP_MATERIALLIST_H
#define APP_MATERIALLIST_H

#include <map>
#include <string>
#include <vector>

#include <Base/COWData.h>

#include "FileBlobManager.h"
#include "Material.h"

namespace App
{

/// The property this list can be a live view of. MaterialListPy.xml injects
/// declarations naming it into the generated binding header, which includes
/// this one and nothing else that would declare it -- a friend declaration
/// inside MaterialList is not enough to name the type at namespace scope.
class PropertyMaterialList;

/// A texture palette's own bytes plus the hashes its records hold. Shared
/// with PropertySurfaceTextureList, which stores the same pair.
AppExport std::size_t texturesMemSize(const std::vector<SurfaceTexture> &palette);

/** A list of materials, stored per field and shared by whoever holds it
 *
 * This is what PropertyMaterialList holds and what Python is handed. It is
 * a VALUE: copying one costs a pointer, and the first write through either
 * copy pays for the storage. So an undo snapshot, a Python variable and the
 * property itself can all name the same arrays until one of them changes
 * something.
 *
 * @section materiallist_layout The layout, and why it is per field
 *
 * Not a vector<Material>. A material is ~140 bytes and an appearance is
 * overwhelmingly uniform or nearly so, so the fields are stored separately
 * and each one is kept at one of exactly three lengths:
 *
 *  - 0 -- every entry reads the field's default, and it costs nothing
 *  - 1 -- one value shared by every entry
 *  - N -- genuinely per entry
 *
 * A ten thousand face box with one colour is one colour. Reading a single
 * entry composes a Material out of the fields; there is deliberately no
 * getValues() (see PropertyMaterialList's note on it).
 *
 * The texture field is the exception: its values are large and their
 * cardinality is low, so it is a PALETTE of distinct records plus a
 * uint16_t index per entry (docs/ShapeAppearanceDesign.md 10.4).
 *
 * @section materiallist_normal Normalisation
 *
 * Collapsing the fields back to those three lengths is deferred: a loop
 * writing entry by entry would otherwise rescan the list on every step.
 * ensureNormalized() runs it on the next read, and it is const AND does not
 * detach -- it changes what is stored, never what the list means, so
 * sharing the work with every other holder is a benefit rather than a
 * hazard. The one consequence to know: a reference handed out by
 * getDiffuseColors() does not survive a later normalize.
 */
class AppExport MaterialList
{
public:
    MaterialList() = default;

    /** More distinct textures than the index can address
     *
     * Unreachable in practice -- the point of a palette is that the
     * cardinality is low -- but a silent wrap would hand back the wrong
     * texture forever, so both the writer and the readers check it.
     */
    static constexpr std::size_t MaxPaletteSize = 0x10000;

    /// The material every entry of an empty field reads as
    static const Material &defaultMaterial();

    /** @name What a whole material stores
     *
     * A material is clamped on the way into storage: the diffuse alpha
     * carries the transparency, and a finish or texture is normalized so
     * that everything reading this list back gets a record it can draw --
     * and so that two textures differing only in a transform nobody stated
     * land in one palette slot rather than two.
     */
    //@{
    static Color storedDiffuse(const Material &mat);
    static SurfaceFinish storedFinish(const SurfaceFinish &finish);
    static SurfaceTexture storedTexture(const SurfaceTexture &texture);
    //@}

    /** @name Sharing
     *
     * isShared() is what a test asks; nothing branches on it. A write
     * through any holder detaches, which is the whole contract.
     */
    //@{
    bool isShared() const { return _data.isShared(); }
    bool isSameData(const MaterialList &other) const { return _data.isSameData(other._data); }
    //@}

    /** @name Whole material access */
    //@{
    int getSize() const;
    void setSize(int newSize);
    void setSize(int newSize, const Material &def);

    void setValue(const Material &mat);
    void setValues(const std::vector<Material> &values);
    Material operator[](int idx) const { return getMaterial(idx); }
    Material getMaterial(int idx) const;
    void set1Value(int idx, const Material &mat);
    //@}

    /** @name Per field access
     *
     * The getters hand back the raw field, whose size is 0, 1 or getSize()
     * -- resolve a single entry with the indexed getter instead of assuming
     * the array is as long as the list. The setters normalise, so a uniform
     * vector handed to setDiffuseColors() collapses to one element.
     */
    //@{
    const std::vector<Color> &getAmbientColors() const;
    const std::vector<Color> &getDiffuseColors() const;
    const std::vector<Color> &getSpecularColors() const;
    const std::vector<Color> &getEmissiveColors() const;
    const std::vector<float> &getShininessValues() const;
    const std::vector<std::string> &getImages() const;
    const std::vector<std::string> &getImagePaths() const;
    const std::vector<std::string> &getUuids() const;
    const std::vector<int8_t> &getTypes() const;
    /// Normalised first, so the "0, 1 or getSize()" rule holds for a caller
    /// that only ever reads
    const std::vector<SurfaceFinish> &getFinishes() const;
    /// @see materiallist_layout: distinct values plus an index, handed out
    /// as the pair because that is what the render side wants
    //@{
    const std::vector<SurfaceTexture> &getTexturePalette() const;
    const std::vector<uint16_t> &getTextureIndex() const;
    //@}

    Color getAmbientColor(int idx) const;
    Color getDiffuseColor(int idx) const;
    Color getSpecularColor(int idx) const;
    Color getEmissiveColor(int idx) const;
    float getShininess(int idx) const;
    float getTransparency(int idx) const;
    const std::string &getImage(int idx) const;
    const std::string &getImagePath(int idx) const;
    const std::string &getUuid(int idx) const;
    SurfaceFinish getFinish(int idx) const;
    /// By value, because the storage holds distinct records rather than one
    /// per entry: there is no array element to hand a reference into
    SurfaceTexture getTexture(int idx) const;
    Material::MaterialType getType(int idx) const;
    //@}

    /** @name Whole field writes */
    //@{
    void setAmbientColors(const std::vector<Color> &colors);
    void setDiffuseColors(const std::vector<Color> &colors);
    void setSpecularColors(const std::vector<Color> &colors);
    void setEmissiveColors(const std::vector<Color> &colors);
    void setShininessValues(const std::vector<float> &values);
    void setTransparencies(const std::vector<float> &values);
    void setImages(const std::vector<std::string> &values);
    void setImagePaths(const std::vector<std::string> &values);
    void setUuids(const std::vector<std::string> &values);
    void setFinishes(const std::vector<SurfaceFinish> &values);
    /// One record per entry going in; the palette is built from what is
    /// distinct among them
    void setTextures(const std::vector<SurfaceTexture> &values);
    //@}

    /** @name One entry of one field, expanding that field alone if it has to */
    //@{
    void setAmbientColor(int idx, const Color &col);
    void setDiffuseColor(int idx, const Color &col);
    void setSpecularColor(int idx, const Color &col);
    void setEmissiveColor(int idx, const Color &col);
    void setShininess(int idx, float value);
    void setTransparency(int idx, float value);
    void setImage(int idx, const std::string &value);
    void setImagePath(int idx, const std::string &value);
    void setUuid(int idx, const std::string &value);
    void setFinish(int idx, const SurfaceFinish &value);
    void setTexture(int idx, const SurfaceTexture &value);
    //@}

    /** @name One field for every entry, leaving the others alone */
    //@{
    void setAmbientColor(const Color &col);
    void setDiffuseColor(const Color &col);
    void setSpecularColor(const Color &col);
    void setEmissiveColor(const Color &col);
    /// Every entry's rgb, leaving every entry's alpha alone -- the diffuse
    /// alpha is the opacity and, in PBR mode, the specular alpha is the
    /// metallic factor, so a uniform colour write would silently restate
    /// them
    //@{
    void setDiffuseRGB(const Color &col);
    void setSpecularRGB(const Color &col);
    //@}
    void setShininess(float value);
    void setTransparency(float value);
    void setImage(const std::string &value);
    void setImagePath(const std::string &value);
    void setUuid(const std::string &value);
    void setFinish(const SurfaceFinish &value);
    void setTexture(const SurfaceTexture &value);
    //@}

    /// Whether any entry names a texture or a material card
    bool hasTextureOrCard() const;
    /// Whether any entry states a surface finish; the cheap gate for a
    /// consumer that has nothing to do when none does
    bool hasFinish() const;
    /// Whether any entry names a texture map
    bool hasTexture() const;

    /** @name The texture maps as stored content
     *
     * A slot holds a content hash and App::FileBlobManager owns the bytes.
     * Arrival order is NOT queue order, so a multi-slot referrer resolves
     * the slot BY CONTENT HASH and never by the order the handles come
     * back (docs/ShapeAppearanceDesign.md 10.2).
     */
    //@{
    /// Where content this list inserts goes. Null means the process-wide
    /// store, which is what a list built in Python has until it is assigned
    /// into a property with a document behind it.
    void setBlobManager(FileBlobManager *manager) { _manager = manager; }
    FileBlobManager *getBlobManager() const { return _manager; }
    /// Take a file into the store and answer the content hash a
    /// SurfaceTexture slot holds. Empty if the file cannot be read.
    std::string insertTextureFile(const char *path, const char *extension = nullptr);
    /// Where the content behind a hash is on disk, empty if this list does
    /// not hold it (yet -- a restore serves the handles later)
    std::string getTextureFile(const std::string &hash) const;
    /// Every hash this list holds content for, in no particular order
    std::vector<std::string> getTextureHashes() const;
    /// Take content in under the hash it is named by, which is how a list
    /// moving between blob managers keeps its claim
    void holdTextureBlob(const FileBlobHandle &blob);
    /// Tell a save which content this list refers to. One referrer name per
    /// SLOT, so the files land under readable names, and once per DISTINCT
    /// hash, because shared content is one file with several referrers.
    void noteTextureBlobs(FileBlobManager &manager, const BlobReferrer &referrer) const;
    /// Take a restored blob into whichever slots name its hash. Idempotent.
    void assignRestoredBlob(const FileBlobHandle &blob);
    /// Whether content is held for every hash the palette names, which is
    /// what tells a restore it has nothing left queued
    bool holdsEveryNamedBlob() const;
    /// Drop the handles no palette slot names any more, which is what ends a
    /// blob's life once the last referrer lets go. Deliberately not called
    /// from normalize(): between insertTextureFile() and the write that
    /// names the hash there is always a handle no slot points at.
    void pruneTextureBlobs();
    //@}

    /** @name PBR mode
     *
     * One bool for the whole list: when set, the same arrays are READ AS
     * PBR quantities -- the diffuse colour is the base colour, the
     * shininess slot is the roughness, the specular colour is the F0 tint
     * with the metallic factor riding its alpha.
     */
    //@{
    bool isPBR() const;
    /// Change what the fields MEAN, converting nothing
    void setPBR(bool enable);
    /// Change the mode and convert every entry, so the list looks the same
    void convertPBR(bool enable);
    float getMetallic(int idx) const;
    float getRoughness(int idx) const;
    void setMetallicValues(const std::vector<float> &values);
    void setRoughnessValues(const std::vector<float> &values);
    void setMetallic(int idx, float value);
    void setRoughness(int idx, float value);
    void setMetallic(float value);
    void setRoughness(float value);
    /// One entry as a Phong material, whatever mode the list is in
    Material getPhongMaterial(int idx) const;
    //@}

    /// Whether the diffuse colour is the only field that varies per entry
    bool variesOnlyInDiffuse() const;

    /// Content only, as every list property reports it
    unsigned int getMemSize() const;

    /** Whether two lists say the same thing
     *
     * Same storage is the cheap answer; otherwise both are normalised and
     * the fields compared, because two equal-but-differently-stored lists
     * must not serialise differently (the shared-default scheme elides a
     * property whose bytes match its class default).
     */
    bool isSame(const MaterialList &other) const;

    /** @name What a restore lands
     *
     * Values arrive carrying both transparency slots exactly as the file
     * states them; \a legacy says whether the file's era meant transparency
     * by a colour's alpha.
     */
    //@{
    void restoreValues(std::vector<Material> &&values, bool legacy);
    void applyRestoredTransparency(const std::vector<float> &transparency, bool legacy);
    //@}

private:
    friend class PropertyMaterialList;

    /** The storage every holder of this list shares until one writes
     *
     * Field lengths are 0, 1 or count, except the texture pair; see
     * @ref materiallist_layout.
     */
    struct Data
    {
        int count {0};
        /// The PBR reading of the fields
        bool pbr {false};
        std::vector<Color> ambient;
        /** Diffuse colour AND transparency: the alpha is the entry's
         * opacity, so there is no transparency array beside it
         */
        std::vector<Color> diffuse;
        std::vector<Color> specular;
        std::vector<Color> emissive;
        std::vector<float> shininess;
        std::vector<std::string> image;
        std::vector<std::string> imagePath;
        std::vector<std::string> uuid;
        std::vector<int8_t> type;
        std::vector<SurfaceFinish> finish;
        std::vector<SurfaceTexture> texturePalette;
        std::vector<uint16_t> textureIndex;
        /// The content this list holds, keyed by hash. This map IS the
        /// claim on the files.
        std::map<std::string, FileBlobHandle> textureBlobs;
        /// Whether the fields are collapsed. Not part of the value.
        bool normalized {true};
    };

    /// Read. Never detaches, and answers with an empty list when nothing
    /// has been stored yet.
    const Data &rd() const { return _data.get(); }
    /// Write. Detaches if the storage is shared.
    Data &wd() { return _data.edit(); }
    /** The storage, writable WITHOUT detaching
     *
     * Only normalisation may use this. It changes what is stored and never
     * what the list means, so sharing the work with the other holders is a
     * benefit -- where detaching on a READ would defeat the sharing
     * entirely. Never call it on a null holder: the value it answers with
     * there is shared by every empty list in the program.
     */
    Data &nd() const { return const_cast<Data &>(_data.get()); }
    /** Storage for the blob map
     *
     * The map is a CLAIM on files rather than part of the value, so taking
     * content in must not detach -- a restore serves every holder of that
     * value at once -- but it does have to have somewhere to put it.
     */
    Data &bd() { return _data.isNull() ? _data.edit() : nd(); }

    /// Collapse every field to 0, 1 or count. Idempotent, and lazy.
    void ensureNormalized() const;
    void normalize() const;
    /// Mark the fields as possibly denormal after a write
    void touchFields();

    /** What an empty specular / shininess field reads as, per mode
     *
     * The Phong defaults are the default material's -- a near-white
     * specular whose alpha is 1, which in PBR mode would read back as a
     * fully metallic surface. These are also the collapse baselines, so
     * which values a field can elide follows the mode.
     */
    const Color &specularDefault() const;
    float shininessDefault() const;
    /// Throw unless the list is in PBR mode
    void requirePBR() const;
    /// One material as this list reads it, converting what disagrees with
    /// the list's mode
    Material inMode(const Material &mat) const;

    /** @name The field writes, taken by MEMBER POINTER rather than by
     * reference
     *
     * A reference would have to come out of wd(), which detaches -- and
     * these all have a "nothing changed" path that must leave the storage
     * exactly as it found it, because that is how PropertyMaterialList
     * decides whether there is a change to record at all.
     */
    //@{
    template<class T>
    void setField(std::vector<T> Data::*member, const std::vector<T> &values, const T &def);
    template<class T>
    void setFieldValue(std::vector<T> Data::*member, int idx, const T &value, const T &def);
    template<class T>
    void setUniformField(std::vector<T> Data::*member, const T &value, const T &def);
    /// The rgb-only write behind setDiffuseRGB / setSpecularRGB
    void setFieldRGB(std::vector<Color> Data::*member, const Color &col, const Color &def);
    //@}

    Base::COWValue<Data> _data;
    /// Where insertTextureFile() puts content; not part of the value
    FileBlobManager *_manager {nullptr};
};

}  // namespace App

#endif  // APP_MATERIALLIST_H
