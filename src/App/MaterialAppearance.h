/***************************************************************************
 *   Copyright (c) 2005 Jürgen Riegel <juergen.riegel@web.de>              *
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


#ifndef APP_MATERIAL_H
#define APP_MATERIAL_H

#include <cstdint>
#include <string>
#include <vector>

#include <App/Color.h>

namespace App
{

/** A machined surface finish: what the surface was done to, not how it shades
 *
 * Knurled, brushed, blasted or turned, plus the three numbers that size
 * the pattern. It rides a material so that a per-face appearance carries
 * a per-face finish with no property, no indexing and no restore path of
 * its own -- and it is carried through every Phong/PBR conversion
 * untouched, because it states a fact about the physical surface rather
 * than a reading of the shading slots.
 *
 * Physical units throughout: the renderer has to know when a feature has
 * fallen below the pixel footprint and should shade as roughness rather
 * than as a perturbed normal, which a normalised amplitude cannot say.
 *
 * The default is all zeros, which is what lets an unset finish elide out
 * of both the storage (an empty field array) and the document.
 */
struct AppExport SurfaceFinish
{
    enum Pattern : uint8_t {
        None = 0,
        Knurl,          /**< diamond knurl */
        KnurlStraight,  /**< straight knurl */
        Brushed,        /**< linear brushed lay */
        Blasted,        /**< isotropic bead or shot blasted */
        Turned,         /**< concentric turning marks */
        PatternCount
    };

    /** The pattern, as a Pattern value
     *
     * Deliberately a plain uint8 and deliberately NOT clamped to the
     * values this build knows: a document written by a later build states
     * a pattern this one cannot draw, and storing it unchanged is what
     * makes the round trip lossless. A consumer treats anything it does
     * not recognise as unfinished.
     */
    uint8_t pattern {None};
    float pitch {0.0F};   /**< mm, feature spacing */
    float depth {0.0F};   /**< mm, peak to valley */
    float angle {0.0F};   /**< degrees, lay direction in the pattern frame */

    bool isSet() const { return pattern != None; }

    /// The smallest pitch a set pattern may state. Below this a pattern
    /// is not a finish but a divisor no consumer should have to defend
    /// itself against.
    static constexpr float MinPitch = 1.0e-4F;

    /** Clamp into the range every consumer may assume
     *
     * An unset pattern states nothing else, so the record elides; a set
     * one has a pitch of at least MinPitch, a non-negative depth and an
     * angle in [0, 180) -- a lay has no direction, only an axis.
     */
    void normalize();

    /// The pattern name the Python API uses, "" for None and for anything
    /// unrecognised; and its inverse, which answers None for an unknown
    /// name.
    static const char *patternName(uint8_t pattern);
    static uint8_t patternFromName(const char *name);

    bool operator==(const SurfaceFinish& f) const
    {
        return pattern == f.pattern && pitch == f.pitch && depth == f.depth
            && angle == f.angle;
    }
    bool operator!=(const SurfaceFinish& f) const { return !operator==(f); }
};

/** The texture maps a surface is shaded with
 *
 * The five maps glTF states per material -- base colour, metallic
 * roughness, normal, emissive and occlusion -- plus the one transform
 * that format applies to all of their coordinates. Like the finish above
 * it rides a material, so a per-face appearance carries a per-face
 * texture set with no property and no restore path of its own, and it is
 * carried through every Phong/PBR conversion untouched: which image is
 * pasted on a surface is not a reading of the shading slots.
 *
 * A slot holds the CONTENT HASH of a blob, not a path: the property that
 * stores this is the blob referrer and App::FileBlobManager owns the
 * bytes, exactly as PropertyPartShape does. Two slots naming the same
 * hash share one blob, which is why assigning a restored handle by hash
 * is idempotent.
 *
 * The default is all slots empty, which is what lets an unset texture
 * elide out of both the storage and the document.
 */
struct AppExport SurfaceTexture
{
    /** Which map a slot holds
     *
     * The order is glTF's, and it is part of the serialized form: a slot
     * is written by index, so values may be appended but never
     * renumbered.
     */
    enum Slot : uint8_t {
        BaseColor = 0,
        MetallicRoughness,
        Normal,
        Emissive,
        Occlusion,
        SlotCount
    };

    /// Content hash of the blob in each slot; empty = no map there
    std::string maps[SlotCount];
    float scale[2] {1.0F, 1.0F};   /**< coordinate scale, glTF KHR_texture_transform */
    float offset[2] {0.0F, 0.0F};  /**< coordinate offset */
    float rotation {0.0F};         /**< coordinate rotation, degrees */

    /// Whether any slot is occupied. A transform with no map states
    /// nothing, so it does not count.
    bool isSet() const;

    /** Clamp into the range every consumer may assume
     *
     * An unset record states nothing else, so it elides; a set one has a
     * finite transform with the rotation wrapped into [0, 360) -- a
     * texture rotation is a direction, unlike a finish lay, which is only
     * an axis.
     */
    void normalize();

    /// The slot name the Python API, the serialized keys and the stored
    /// file names use; and its inverse, which answers SlotCount for an
    /// unknown name.
    static const char *slotName(uint8_t slot);
    static uint8_t slotFromName(const char *name);

    bool operator==(const SurfaceTexture& t) const
    {
        for (uint8_t i = 0; i < SlotCount; ++i) {
            if (maps[i] != t.maps[i]) {
                return false;
            }
        }
        return scale[0] == t.scale[0] && scale[1] == t.scale[1]
            && offset[0] == t.offset[0] && offset[1] == t.offset[1]
            && rotation == t.rotation;
    }
    bool operator!=(const SurfaceTexture& t) const { return !operator==(t); }
};

/** MaterialAppearance class
 */
class AppExport MaterialAppearance
{
public:
    enum MaterialType {
        BRASS,
        BRONZE,
        COPPER,
        GOLD,
        PEWTER,
        PLASTER,
        PLASTIC,
        SILVER,
        STEEL,
        STONE,
        SHINY_PLASTIC,
        SATIN,
        METALIZED,
        NEON_GNC,
        CHROME,
        ALUMINIUM,
        OBSIDIAN,
        NEON_PHC,
        JADE,
        RUBY,
        EMERALD,
        DEFAULT,
        USER_DEFINED
    };

public:
    /** @name Constructors
     */
    //@{
    /** Sets the USER_DEFINED material type. The user must set the colors afterwards. */
    MaterialAppearance();
    /** Defines the colors and shininess for the material \a MatName. If \a MatName isn't defined then USER_DEFINED is
     * set and the user must define the colors itself.
     */
    explicit MaterialAppearance(const char* MatName);
    /** Does basically the same as the constructor above unless that it accepts a MaterialType as argument. */
    explicit MaterialAppearance(const MaterialType MatType);
    //@}

    /** Set a material by name
     *  There are some standard materials defined which are:
     *  \li Brass
     *  \li Bronze
     *  \li Copper
     *  \li Gold
     *  \li Pewter
     *  \li Plaster
     *  \li Plastic
     *  \li Silver
     *  \li Steel
     *  \li Stone
     *  \li Shiny plastic
     *  \li Satin
     *  \li Metalized
     *  \li Neon GNC
     *  \li Chrome
     *  \li Aluminium
     *  \li Obsidian
     *  \li Neon PHC
     *  \li Jade
     *  \li Ruby
     *  \li Emerald
     * Furthermore there two additional modes \a Default which defines a kind of grey metallic and user defined that
     * does nothing.
     * The Color and the other properties of the material are defined in the range [0-1].
     * If \a MatName is an unknown material name then the type USER_DEFINED is set and the material doesn't get changed.
     */
    void set(const char* MatName);
    /**
     * This method is provided for convenience which does basically the same as the method above unless that it accepts a MaterialType
     * as argument.
     */
    void setType(const MaterialType MatType);
    /**
     * Returns the currently set material type.
     */
    MaterialType getType() const
    { return _matType; }

    /** @name Phong and PBR readings of the shininess slot
     *
     * The Blinn-Phong-to-GGX fit the bgfx backend uses for materials that
     * state no roughness, and its inverse. Coin's 0..1 shininess maps to a
     * GL exponent of s * 128 and the match is alpha = sqrt(2 / (n + 2)) on
     * the GGX width, which a roughness squares -- so this is the fourth
     * root of that ratio. The inverse saturates: a roughness below ~0.352
     * needs an exponent above 128 and converts to a shininess of 1.
     */
    //@{
    static float shininessToRoughness(float shininess);
    static float roughnessToShininess(float roughness);
    //@}

    /** The Phong material a PBR-slot material most nearly means
     *
     * \a raw carries the material list's PBR readings in the classic
     * slots: base colour in the diffuse, roughness in the shininess, the
     * F0 tint in the specular with the metallic factor in its alpha. The
     * diffuse stays the base colour -- the bgfx PBR path reads its base
     * colour out of the diffuse slot, so zeroing a metal's diffuse here
     * would shade it black there, and a Phong metal shown as a shiny
     * colour is the better degradation anyway. The specular becomes the
     * F0 the surface most nearly means: a metal carries its colour
     * there, a dielectric 0.04 scaled by the tint. The result is tagged
     * Phong.
     */
    static MaterialAppearance pbrToPhong(const MaterialAppearance& raw);

    /** The PBR material a Phong material most nearly means
     *
     * The inverse direction of pbrToPhong for the editor's mode toggle:
     * the diffuse stays as the base colour, the shininess slot becomes
     * the roughness by the fit above, and the surface reads dielectric --
     * white F0 tint, metallic 0 -- because a Phong specular states an
     * intensity, not a metal. Emissive and the identity strings carry
     * over; the result is tagged PBR. Round-tripping through pbrToPhong
     * keeps the look but forgets the specular colour, which Phong alone
     * can state.
     */
    static MaterialAppearance phongToPbr(const MaterialAppearance& classic);

    /** @name PBR readings of one material value
     *
     * The getters degrade gracefully on a Phong value: no metals, and the
     * roughness derived from the shininess. The setters state a PBR
     * quantity, so on a Phong value they convert it first through
     * setPBR() rather than landing a number in a slot that means
     * something else -- writing a metallic factor is deciding the mode.
     */
    //@{
    float getMetallic() const;
    float getRoughness() const;
    void setMetallic(float value);
    void setRoughness(float value);
    //@}

    /** Switch the reading, converting the values so the look survives
     *
     * The value-level counterpart of PropertyAppearanceList::convertPBR:
     * pbrToPhong one way, phongToPbr the other, and nothing at all when
     * the mode already matches. This is what setting the mode means
     * everywhere a material value is edited -- assign the flag directly
     * (\a pbr) only to state what raw slots already hold.
     */
    void setPBR(bool enable);

    /** @name Properties */
    //@{
    Color ambientColor;  /**< Defines the ambient color. */
    Color diffuseColor;  /**< Defines the diffuse color. */
    Color specularColor; /**< Defines the specular color. */
    Color emissiveColor; /**< Defines the emissive color. */
    float shininess;
    float transparency;
    /** What the surface was done to
     *
     * Orthogonal to the shading model above: pbrToPhong, phongToPbr and
     * setPBR all carry it through unchanged. Default (None) on every
     * material until something states one, which is why it costs nothing
     * in the appearance property that stores it.
     */
    SurfaceFinish finish;
    /** What is pasted on the surface
     *
     * Orthogonal to the shading model in the same way the finish is, and
     * carried through the conversions for the same reason. Empty on every
     * material until something states one, which is why it costs nothing
     * in the appearance property that stores it.
     */
    SurfaceTexture texture;
    /** @name Texture and material-card identity, upstream's fields
     *
     * image holds the encoded bytes of an image file and imagePath names
     * one; the render engine paints either onto the faces the appearance
     * carries them on (ViewProviderGeometryObject::updateFaceTextures --
     * a per-face appearance can put a different image on every face).
     * uuid is carried, not used: it is here so that a document written
     * by upstream survives a round trip, and so that the day a reader
     * produces one there is somewhere to put it. All three are empty on
     * every object until something states them, which is why the
     * appearance property stores them as fields that cost nothing at
     * size zero.
     */
    //@{
    std::string image;
    std::string imagePath;
    std::string uuid;
    //@}
    /** The MaterialX document set this look is shaded by, as a manifest hash
     *
     * The content hash of an App::MaterialXDocument manifest -- the tree
     * object naming the document and its image maps by content -- or empty
     * when the look is the Phong/PBR slots alone. ONE string, held the way
     * a texture slot holds a map's hash: the property storing this is the
     * blob referrer and keeps the manifest and every file it names alive,
     * and App::FileBlobManager owns the bytes. A card carrying a MaterialX
     * document puts its manifest hash here (docs/MaterialStorage.md sec
     * 17.8), so it rides the follow rule and the per-face palette like the
     * colours do, and is carried through every Phong/PBR conversion
     * untouched.
     */
    std::string materialx;
    /** Which reading the slot values carry
     *
     * A value-level tag, not storage: PropertyAppearanceList keeps the mode
     * once for the whole list and stamps it on every material it hands
     * out, so a script can see which reading the values it holds are in.
     * Assigning materials back to a list adopts their tag; the dict
     * spelling's explicit PBR key overrides it.
     */
    bool pbr = false;
    //@}

    bool operator==(const MaterialAppearance& m) const
    {
        // Two appearances naming the same material card are the same
        // appearance whatever their colours currently say, which is how
        // upstream defines it: the card is the identity and the colours are
        // a rendering of it. Inert here until something sets a uuid.
        if (!uuid.empty() && uuid == m.uuid) {
            return true;
        }
        return _matType==m._matType && pbr==m.pbr && shininess==m.shininess &&
            transparency==m.transparency && ambientColor==m.ambientColor &&
            diffuseColor==m.diffuseColor && specularColor==m.specularColor &&
            emissiveColor==m.emissiveColor && finish==m.finish &&
            texture==m.texture &&
            image==m.image && imagePath==m.imagePath && materialx==m.materialx;
    }
    bool operator!=(const MaterialAppearance& m) const
    {
        return !operator==(m);
    }

private:
    MaterialType _matType;
};

/** One dynamic Render_* view property stated by a material card.
 *
 * The renderer's media features -- glass and its kin -- are per-object
 * dynamic properties on the VIEW provider, not fields of MaterialAppearance: they
 * turn a closed shape into a volume, and a volume has no faces to attach
 * a per-face appearance to (docs/ShapeAppearanceDesign.md sec 8). A card
 * that wants to state one therefore hands over a list of these rather
 * than anything MaterialAppearance could carry, and the Gui side creates the
 * properties. The type lives here, in App, because it has to cross from
 * the Materials module to Gui, which cannot see Materials directly.
 */
struct AppExport MaterialRenderProperty
{
    /// The view property name, e.g. "Render_GlassIOR".
    std::string name;
    /// true selects App::PropertyBool, false App::PropertyFloat.
    bool boolean {false};
    /// For a bool property, non-zero is true.
    double value {0.0};
};

using MaterialRenderProperties = std::vector<MaterialRenderProperty>;

} //namespace App

#endif // APP_MATERIAL_H
