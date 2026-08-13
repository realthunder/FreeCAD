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

#include <string>

#include <App/Color.h>

namespace App
{

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
     * Meaningful when \a pbr is set; the getters degrade gracefully on a
     * Phong value (no metals, roughness derived from the shininess), the
     * setters throw on one, exactly as the list property's do: the slots
     * they would land in mean something else there, and a caller holding
     * a metallic value has decided the mode already.
     */
    //@{
    float getMetallic() const;
    float getRoughness() const;
    void setMetallic(float value);
    void setRoughness(float value);
    //@}

    /** @name Properties */
    //@{
    Color ambientColor;  /**< Defines the ambient color. */
    Color diffuseColor;  /**< Defines the diffuse color. */
    Color specularColor; /**< Defines the specular color. */
    Color emissiveColor; /**< Defines the emissive color. */
    float shininess;
    float transparency;
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
            emissiveColor==m.emissiveColor &&
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
