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
#include <tuple>
#include <vector>

#include <Base/COWData.h>

#include "FileBlobManager.h"
#include "MaterialAppearance.h"

namespace App
{

/// The property this list can be a live view of. MaterialListPy.xml injects
/// declarations naming it into the generated binding header, which includes
/// this one and nothing else that would declare it -- a friend declaration
/// inside AppearanceList is not enough to name the type at namespace scope.
class PropertyAppearanceList;

/// A texture palette's own bytes plus the hashes its records hold. Shared
/// with PropertySurfaceTextureList, which stores the same pair.
AppExport std::size_t texturesMemSize(const std::vector<SurfaceTexture> &palette);

/** A list of materials, stored per field and shared by whoever holds it
 *
 * This is what PropertyAppearanceList holds and what Python is handed. It is
 * a VALUE: copying one costs a pointer, and the first write through either
 * copy pays for the storage. So an undo snapshot, a Python variable and the
 * property itself can all name the same arrays until one of them changes
 * something.
 *
 * @section materiallist_layout The layout: a base and the faces that
 * override it
 *
 * Not a vector<MaterialAppearance>. A material is ~140 bytes and an appearance is
 * overwhelmingly uniform or nearly so, so the list is
 * (docs/ShapeAppearanceDesign.md 12):
 *
 *  - a BASE material -- what the object looks like where no face says
 *    otherwise -- stored in full;
 *  - a sorted vector of OVERRIDING face indices;
 *  - one array per field, at length 0 or |overrides|, in overrides order.
 *
 * with the invariant that entry(i) is the base for every i outside
 * overrides, and that an EMPTY field array reads as the base's value for
 * that field. Three faces painted red keep the base's gloss, so only the
 * diffuse array has three entries and every other one is empty.
 *
 * A ten thousand face box with one colour is one material. Reading a single
 * entry composes a MaterialAppearance out of the base and whatever the face overrides;
 * there is deliberately no getValues() (see PropertyAppearanceList's note on
 * it), and the per field getters below come in two kinds -- the sparse
 * storage, and a dense resolution built on read.
 *
 * The texture field is the exception: its values are large and their
 * cardinality is low, so the overriding faces share a PALETTE of distinct
 * records plus a uint16_t index each (docs/ShapeAppearanceDesign.md 10.4).
 *
 * @section materiallist_base Where the base comes from
 *
 * A list landed dense -- an old document, upstream's encoding, an import --
 * has no base in it, so one is DERIVED once and then stored: the mirror if
 * it names a value the list holds, else the material covering the largest
 * summed face area, else the most common one (12.4). deriveBase() runs it
 * with what the caller knows; ensureBase() is the fallback the save takes
 * when nobody did. Until then the list is honest but unshared: every entry
 * is an override, which costs exactly what the dense form did.
 *
 * @section materiallist_normal Normalisation
 *
 * Dropping an override that no longer differs from the base, and emptying a
 * field array whose every entry is the base's, is deferred: a loop writing
 * entry by entry would otherwise rescan the list on every step.
 * ensureNormalized() runs it on the next read, and it is const AND does not
 * detach -- it changes what is stored, never what the list means, so
 * sharing the work with every other holder is a benefit rather than a
 * hazard. The one consequence to know: a reference handed out by
 * getDiffuseOverrides() does not survive a later normalize.
 */
class AppExport AppearanceList
{
public:
    AppearanceList() = default;

    /** More distinct textures than the index can address
     *
     * Unreachable in practice -- the point of a palette is that the
     * cardinality is low -- but a silent wrap would hand back the wrong
     * texture forever, so both the writer and the readers check it.
     */
    static constexpr std::size_t MaxPaletteSize = 0x10000;

    /// The material every entry of an empty field reads as
    static const MaterialAppearance &defaultMaterial();

    /** @name What a whole material stores
     *
     * A material is clamped on the way into storage: the diffuse alpha
     * carries the transparency, and a finish or texture is normalized so
     * that everything reading this list back gets a record it can draw --
     * and so that two textures differing only in a transform nobody stated
     * land in one palette slot rather than two.
     */
    //@{
    static Color storedDiffuse(const MaterialAppearance &mat);
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
    bool isSameData(const AppearanceList &other) const { return _data.isSameData(other._data); }
    //@}

    /** @name The base and the overriding faces
     *
     * The base is the object's look; an override is one face holding its
     * own. A whole-object write moves the base and leaves the overriding
     * faces where they are; a per-face write makes a face an override, or
     * drops it back when its value returns to the base's.
     */
    //@{
    const MaterialAppearance &getBase() const;
    /// The whole-object write: every face that is not an override follows
    void setBase(const MaterialAppearance &mat);
    /// Sorted, unique, every index below getSize()
    const std::vector<uint32_t> &getOverrides() const;
    bool hasOverrides() const;
    bool isOverride(int idx) const;
    /// Every face back to the base, which is what a whole-object write used
    /// to do to a per-face list before it was an action of its own
    void clearOverrides();
    void clearOverride(int idx);
    /** Whether a base has been chosen for this list
     *
     * False for a list that arrived dense and has not been through the
     * heuristic yet -- see @ref materiallist_base.
     */
    bool hasDerivedBase() const;
    /** Choose the base, once, for a list that arrived without one
     *
     * \a hint is the view provider's mirror, which wins if it names a
     * diffuse colour the list holds; \a weights is the per entry face area,
     * whose largest sum wins next. Both may be null, and then the most
     * common entry wins. Does nothing to a list that already has a base.
     *
     * const, and it does not detach, for the reason normalisation does not:
     * it changes what is stored and not what any entry resolves to.
     */
    void deriveBase(const Color *hint = nullptr,
                    const std::vector<double> *weights = nullptr) const;
    /// The heuristic with nothing to go on, run only if nobody else did
    void ensureBase() const;

    /** @name Following the object's material card
     *
     * One flag for the whole list (docs/MaterialStorage.md 15.3): while it
     * is set, the BASE is the card's look, re-taken whenever the card
     * changes -- and the overriding faces are re-applied over it, so an
     * imported part with three painted faces can be assigned Aluminium and
     * keep its three faces. Any whole-object write ends the follow; a
     * per-face write does not, because it does not touch the base.
     *
     * The list itself knows nothing about material cards. The view provider
     * reads the card and calls followMaterial(); everything here is the
     * flag and the rule that a base written by anyone else clears it.
     */
    //@{
    bool isFollowingMaterial() const;
    /// Say whether the base is the card's, without changing it
    void setFollowMaterial(bool enable);
    /// Take the card's look as the base AND keep following it -- the one
    /// base write that does not end the follow
    void followMaterial(const MaterialAppearance &card);
    //@}
    /** Whether any entry wears exactly this diffuse colour
     *
     * The question deriveBase's mirror rule asks, asked separately so that
     * a caller knows whether an answer it would have to WORK for -- the
     * face areas -- is going to be needed at all.
     */
    bool namesDiffuse(const Color &color) const;
    //@}

    /** @name Whole material access */
    //@{
    int getSize() const;
    void setSize(int newSize);
    void setSize(int newSize, const MaterialAppearance &def);

    void setValue(const MaterialAppearance &mat);
    void setValues(const std::vector<MaterialAppearance> &values);
    MaterialAppearance operator[](int idx) const { return getMaterial(idx); }
    MaterialAppearance getMaterial(int idx) const;
    void set1Value(int idx, const MaterialAppearance &mat);
    //@}

    /** @name Per field access, as it is stored
     *
     * The overriding faces' values, in overrides order, at length 0 or
     * |overrides| -- an empty array says every override takes the base's
     * value for that field. Normalised first, so an array that is there
     * but says nothing reads as empty.
     *
     * Resolve a single entry with the indexed getter, or the whole list
     * with the dense getters below; do NOT index one of these by a face
     * number.
     */
    //@{
    const std::vector<Color> &getAmbientOverrides() const;
    const std::vector<Color> &getDiffuseOverrides() const;
    const std::vector<Color> &getSpecularOverrides() const;
    const std::vector<Color> &getEmissiveOverrides() const;
    const std::vector<float> &getShininessOverrides() const;
    const std::vector<std::string> &getImageOverrides() const;
    const std::vector<std::string> &getImagePathOverrides() const;
    const std::vector<std::string> &getUuidOverrides() const;
    const std::vector<std::string> &getMaterialXOverrides() const;
    const std::vector<int8_t> &getTypeOverrides() const;
    const std::vector<SurfaceFinish> &getFinishOverrides() const;
    /// @see materiallist_layout: distinct values plus an index, handed out
    /// as the pair because that is what the render side wants. The index is
    /// 0 or |overrides| long and the palette holds what the OVERRIDES name;
    /// the base's texture is in the base.
    //@{
    const std::vector<SurfaceTexture> &getTexturePalette() const;
    const std::vector<uint16_t> &getTextureIndex() const;
    //@}

    /// Whether a field is stated by any overriding face at all, which is
    /// the cheap question "does this vary per face"
    //@{
    bool variesInAmbient() const { return !getAmbientOverrides().empty(); }
    bool variesInDiffuse() const { return !getDiffuseOverrides().empty(); }
    bool variesInSpecular() const { return !getSpecularOverrides().empty(); }
    bool variesInEmissive() const { return !getEmissiveOverrides().empty(); }
    bool variesInShininess() const { return !getShininessOverrides().empty(); }
    bool variesInImage() const
    { return !getImageOverrides().empty() || !getImagePathOverrides().empty(); }
    bool variesInUuid() const { return !getUuidOverrides().empty(); }
    bool variesInMaterialX() const { return !getMaterialXOverrides().empty(); }
    bool variesInType() const { return !getTypeOverrides().empty(); }
    bool variesInFinish() const { return !getFinishOverrides().empty(); }
    bool variesInTexture() const { return !getTextureIndex().empty(); }
    //@}
    //@}

    /** @name Per field access, resolved
     *
     * getSize() entries, BUILT ON READ out of the base and the overrides.
     * What every consumer of a whole field wanted anyway, and the only
     * form the compatible encodings can write -- but it is a fresh vector
     * every time, so ask for it once and never in a loop.
     */
    //@{
    std::vector<Color> getAmbientColors() const;
    std::vector<Color> getDiffuseColors() const;
    std::vector<Color> getSpecularColors() const;
    std::vector<Color> getEmissiveColors() const;
    std::vector<float> getShininessValues() const;
    std::vector<std::string> getImages() const;
    std::vector<std::string> getImagePaths() const;
    std::vector<std::string> getUuids() const;
    std::vector<std::string> getMaterialXs() const;
    std::vector<int8_t> getTypes() const;
    std::vector<SurfaceFinish> getFinishes() const;
    /// The palette and one index per ENTRY, which is the form the
    /// compatible encodings and the companion element state
    void getTextures(std::vector<SurfaceTexture> &palette,
                     std::vector<uint16_t> &index) const;
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
    const std::string &getMaterialX(int idx) const;
    SurfaceFinish getFinish(int idx) const;
    /// By value, because the storage holds distinct records rather than one
    /// per entry: there is no array element to hand a reference into
    SurfaceTexture getTexture(int idx) const;
    MaterialAppearance::MaterialType getType(int idx) const;
    //@}

    /** @name Whole field writes, one value per entry
     *
     * Collapsed on the way in, so a vector that says the same thing for
     * every entry is the whole-object write it says it is: it moves the
     * base and empties that field's override array. A vector that varies
     * makes an override of every entry that differs from the base.
     */
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
    void setMaterialXs(const std::vector<std::string> &values);
    void setFinishes(const std::vector<SurfaceFinish> &values);
    /// One record per entry going in; the palette is built from what is
    /// distinct among them
    void setTextures(const std::vector<SurfaceTexture> &values);
    //@}

    /** @name One entry of one field, which is a per-face write
     *
     * The face becomes an override of that field alone, or -- when the
     * value it is given is the base's -- stops being one.
     */
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
    void setMaterialX(int idx, const std::string &value);
    void setFinish(int idx, const SurfaceFinish &value);
    void setTexture(int idx, const SurfaceTexture &value);
    //@}

    /** @name The whole-object write of one field, leaving the others alone
     *
     * The BASE's field. Every face that is not an override follows it; a
     * face holding its own for that field keeps what it holds, which is
     * what makes a painted face survive an appearance card being assigned
     * (docs/ShapeAppearanceDesign.md 12.2).
     */
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
    void setMaterialX(const std::string &value);
    void setFinish(const SurfaceFinish &value);
    void setTexture(const SurfaceTexture &value);
    //@}

    /// Whether any entry names a texture or a material card
    bool hasTextureOrCard() const;
    /// Whether any entry names an image, inline or by path
    bool hasImage() const;
    /// Whether any entry states a surface finish; the cheap gate for a
    /// consumer that has nothing to do when none does
    bool hasFinish() const;
    /// Whether any entry names a texture map
    bool hasTexture() const;
    /// Whether any entry names a MaterialX document set
    bool hasMaterialX() const;

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
    /** @name The MaterialX document sets, as stored content
     *
     * A manifest hash (MaterialAppearance::materialx) is held in the same
     * map a map's hash is, and so is every file the manifest names. The
     * children are only known through the manifest, so a restore asks for
     * the manifest first and for its children once it has arrived.
     */
    //@{
    /// Every manifest hash the base or an override names, once each.
    std::vector<std::string> materialXHashes() const;
    /// The content hashes a HELD manifest names; empty while it is not held.
    std::vector<std::string> materialXChildren(const std::string &manifestHash) const;
    //@}
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
    MaterialAppearance getPhongMaterial(int idx) const;
    /// The base as a Phong material, which is what a consumer with one
    /// material node to fill wants: the object's look, not face 0's
    MaterialAppearance getPhongBase() const;
    //@}

    /// Whether the diffuse colour is the only field that varies per entry
    bool variesOnlyInDiffuse() const;

    /// Content only, as every list property reports it: the base's STATED
    /// fields, the override list and the override arrays
    unsigned int getMemSize() const;

    /** Whether two lists say the same thing
     *
     * Same storage is the cheap answer; otherwise both are normalised and
     * the fields compared, because two equal-but-differently-stored lists
     * must not serialise differently (the shared-default scheme elides a
     * property whose bytes match its class default).
     */
    bool isSame(const AppearanceList &other) const;

    /** @name What a restore lands
     *
     * Values arrive carrying both transparency slots exactly as the file
     * states them; \a legacy says whether the file's era meant transparency
     * by a colour's alpha.
     */
    //@{
    void restoreValues(std::vector<MaterialAppearance> &&values, bool legacy);
    void applyRestoredTransparency(const std::vector<float> &transparency, bool legacy);
    //@}

private:
    friend class PropertyAppearanceList;

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
        /** Whether the base is the object's material card's look
         *
         * TRUE by default, which is what "a fresh object that carries a
         * card follows it" means (docs/MaterialStorage.md 15.3): nobody
         * has chosen this look yet, so the card may. It is the CLASS
         * DEFAULT too, so the shared-default elision is undisturbed --
         * which it would not be if the view provider raised the flag after
         * its ADD_PROPERTY.
         *
         * Only a whole-object write ends it. A whole-LIST assignment does
         * not: an import states one look per face and says nothing about
         * which card the object wears, and it is exactly the imported part
         * with three painted faces that has to be able to take Aluminium
         * and keep them.
         */
        bool follow {true};
        /** The object's look, in full
         *
         * Stored the way the fields are: the diffuse alpha carries the
         * transparency, the finish and the texture are clamped. Its type
         * is the base's type, so it is written through setMaterialType()
         * -- MaterialAppearance::setType() rewrites every colour with the preset's.
         */
        MaterialAppearance base;
        /// The faces holding their own, sorted and unique. Every array
        /// below is 0 or this long, and in this order.
        std::vector<uint32_t> overrides;
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
        /// Manifest hashes (MaterialAppearance::materialx), a string column
        /// like uuid
        std::vector<std::string> materialx;
        std::vector<int8_t> type;
        std::vector<SurfaceFinish> finish;
        std::vector<SurfaceTexture> texturePalette;
        std::vector<uint16_t> textureIndex;
        /// The content this list holds, keyed by hash. This map IS the
        /// claim on the files.
        std::map<std::string, FileBlobHandle> textureBlobs;
        /// Whether the fields are collapsed. Not part of the value.
        bool normalized {true};
        /** Whether the base was chosen rather than merely defaulted
         *
         * False on a list that arrived dense -- every entry an override
         * over a default base, which resolves correctly and costs what the
         * dense form cost. @see materiallist_base
         */
        bool baseDerived {true};
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

    /// Drop the overrides that no longer differ and empty the arrays that
    /// no longer say anything. Idempotent, and lazy.
    void ensureNormalized() const;
    void normalize() const;
    /// Mark the fields as possibly denormal after a write
    void touchFields();

    /** @name Base and override mechanics */
    //@{
    /// Where face \a idx sits among the overrides, or -1
    int overridePos(int idx) const;
    /// Make face \a idx an override -- every array that has anything gains
    /// the base's value at that position -- and answer where it landed
    int makeOverride(int idx);
    /** Take fields written at the old 0/1/count lengths as base + overrides
     *
     * What a restore of any encoding that states one entry at a time lands,
     * and what every document written before ShapeAppearanceDesign 12 holds.
     * A field that is uniform IS the base's; a field that varies leaves
     * every entry an override until a base is derived.
     */
    void adoptDense();
    /// The base's type, written without MaterialAppearance::setType() taking the
    /// preset's colours with it
    static void setMaterialType(MaterialAppearance &mat, int8_t type);
    /** Everything one overriding position states, as a key that orders
     *
     * What makes two painted faces the same material, for the vote in
     * deriveBase -- an ordered key rather than a pairwise comparison,
     * because an import can override thousands of faces.
     */
    //@{
    using OverrideKey = std::tuple<uint32_t, uint32_t, uint32_t, uint32_t, float, int8_t,
                                   std::string, std::string, std::string, std::string,
                                   uint8_t, float, float, float, uint16_t>;
    OverrideKey overrideKey(int pos) const;
    //@}
    /// Every field of one entry, which is what a per-face write and a
    /// growth with a filler both are
    void applyEntry(int idx, const MaterialAppearance &mat);
    /// The type field, which has no MaterialAppearance member to name it by
    void setTypeValue(int idx, int8_t value);
    /// Put a different base under the same entries, restating which faces
    /// override and which follow
    void rebase(const MaterialAppearance &newBase) const;
    //@}

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
    /// What every whole-object write does to the follow flag: a base
    /// somebody else chose is not the card's any more
    void endFollow();
    /// One material as this list reads it, converting what disagrees with
    /// the list's mode
    MaterialAppearance inMode(const MaterialAppearance &mat) const;

    /** @name The field writes, taken by MEMBER POINTER rather than by
     * reference
     *
     * A reference would have to come out of wd(), which detaches -- and
     * these all have a "nothing changed" path that must leave the storage
     * exactly as it found it, because that is how PropertyAppearanceList
     * decides whether there is a change to record at all.
     *
     * Each takes both members of a field: where the base keeps it and where
     * the overrides do.
     */
    //@{
    /// A whole field, one value per entry -- collapsed first, so a uniform
    /// vector is the base write it says it is
    template<class T>
    void setField(T MaterialAppearance::*base, std::vector<T> Data::*member,
                  const std::vector<T> &values);
    /// One entry, which makes it an override or drops it back
    template<class T>
    void setFieldValue(T MaterialAppearance::*base, std::vector<T> Data::*member, int idx,
                       const T &value);
    /// The whole-object write: the base alone, the overriding faces left
    /// exactly where they are
    template<class T>
    void setBaseField(T MaterialAppearance::*base, const T &value, const T &def);
    /// The rgb-only write behind setDiffuseRGB / setSpecularRGB
    void setFieldRGB(Color MaterialAppearance::*base, const Color &col, const Color &def);
    //@}

    Base::COWValue<Data> _data;
    /// Where insertTextureFile() puts content; not part of the value
    FileBlobManager *_manager {nullptr};
};

}  // namespace App

#endif  // APP_MATERIALLIST_H
