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

/** Material class
 */
class AppExport Material
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
    Material();
    /** Defines the colors and shininess for the material \a MatName. If \a MatName isn't defined then USER_DEFINED is
     * set and the user must define the colors itself.
     */
    explicit Material(const char* MatName);
    /** Does basically the same as the constructor above unless that it accepts a MaterialType as argument. */
    explicit Material(const MaterialType MatType);
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
     * The Blinn-Phong-to-GGX fit the bgfx backend has always used for
     * materials that state no roughness, and its inverse. Coin's 0..1
     * shininess maps to a GL exponent of s * 128, so the inverse saturates:
     * a roughness below ~0.124 needs an exponent above 128 and converts to
     * a shininess of 1.
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
    static Material pbrToPhong(const Material& raw);

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
    static Material phongToPbr(const Material& classic);

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
     * The value-level counterpart of PropertyMaterialList::convertPBR:
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
    /** @name Texture and material-card identity, upstream's fields
     *
     * Nothing in this fork writes them yet. They are here so that a
     * document written by upstream survives a round trip through it, and so
     * that the day a reader does produce them -- glTF carries both a
     * texture and a material identity -- there is somewhere to put them.
     * Empty on every object until then, which is why the appearance
     * property stores them as fields that cost nothing at size zero.
     */
    //@{
    std::string image;
    std::string imagePath;
    std::string uuid;
    //@}
    /** Which reading the slot values carry
     *
     * A value-level tag, not storage: PropertyMaterialList keeps the mode
     * once for the whole list and stamps it on every material it hands
     * out, so a script can see which reading the values it holds are in.
     * Assigning materials back to a list adopts their tag; the dict
     * spelling's explicit PBR key overrides it.
     */
    bool pbr = false;
    //@}

    bool operator==(const Material& m) const
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
            image==m.image && imagePath==m.imagePath;
    }
    bool operator!=(const Material& m) const
    {
        return !operator==(m);
    }

private:
    MaterialType _matType;
};

} //namespace App

#endif // APP_MATERIAL_H
