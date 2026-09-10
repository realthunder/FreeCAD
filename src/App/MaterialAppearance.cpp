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


#include "PreCompiled.h"

#ifndef _PreComp_
# include <algorithm>
# include <cmath>
# include <cstring>
#endif

#include "MaterialAppearance.h"

using namespace App;

namespace {

/// Indexed by SurfaceFinish::Pattern; None is the empty name
const char *finishPatternNames[] = {
    "", "knurl", "knurl-straight", "brushed", "blasted", "turned"
};

static_assert(sizeof(finishPatternNames) / sizeof(finishPatternNames[0])
                  == SurfaceFinish::PatternCount,
              "every SurfaceFinish::Pattern needs a name");

/// Indexed by SurfaceTexture::Slot. Unlike the finish patterns every slot
/// has a name: there is no "no slot" value to spell as the empty string,
/// and these names are also the stored file names.
const char *textureSlotNames[] = {
    "basecolor", "metallic-roughness", "normal", "emissive", "occlusion"
};

static_assert(sizeof(textureSlotNames) / sizeof(textureSlotNames[0])
                  == SurfaceTexture::SlotCount,
              "every SurfaceTexture::Slot needs a name");

} // namespace

//===========================================================================
// SurfaceFinish
//===========================================================================

void SurfaceFinish::normalize()
{
    if (!isSet()) {
        // Nothing else means anything without a pattern, and zeroing is
        // what lets the record compare equal to the default and elide
        *this = SurfaceFinish();
        return;
    }
    if (!(pitch >= MinPitch)) {   // catches NaN too
        pitch = MinPitch;
    }
    if (!(depth >= 0.0F)) {
        depth = 0.0F;
    }
    if (!std::isfinite(angle)) {
        angle = 0.0F;
    }
    else {
        // A lay has an axis, not a direction: 190 degrees is 10 degrees
        angle = std::fmod(angle, 180.0F);
        if (angle < 0.0F) {
            angle += 180.0F;
        }
    }
}

const char *SurfaceFinish::patternName(uint8_t pattern)
{
    // An unrecognised pattern (a document from a later build) has no name
    // here; it is still stored and written back unchanged
    return pattern < PatternCount ? finishPatternNames[pattern] : "";
}

uint8_t SurfaceFinish::patternFromName(const char *name)
{
    if (!name || !name[0]) {
        return None;
    }
    for (uint8_t i = 1; i < PatternCount; ++i) {
        if (std::strcmp(name, finishPatternNames[i]) == 0) {
            return i;
        }
    }
    return None;
}

//===========================================================================
// SurfaceTexture
//===========================================================================

bool SurfaceTexture::isSet() const
{
    for (uint8_t i = 0; i < SlotCount; ++i) {
        if (!maps[i].empty()) {
            return true;
        }
    }
    return false;
}

void SurfaceTexture::normalize()
{
    if (!isSet()) {
        // A transform with nothing to transform states nothing, and
        // zeroing is what lets the record compare equal to the default
        // and elide
        *this = SurfaceTexture();
        return;
    }
    for (float& s : scale) {
        if (!std::isfinite(s)) {   // catches NaN too
            s = 1.0F;
        }
    }
    for (float& o : offset) {
        if (!std::isfinite(o)) {
            o = 0.0F;
        }
    }
    if (!std::isfinite(rotation)) {
        rotation = 0.0F;
    }
    else {
        // A texture rotation is a direction, not an axis: the full turn,
        // unlike the finish lay's half
        rotation = std::fmod(rotation, 360.0F);
        if (rotation < 0.0F) {
            rotation += 360.0F;
        }
    }
}

const char *SurfaceTexture::slotName(uint8_t slot)
{
    // A slot this build does not know -- a document from a later one --
    // has no name here; it is skipped rather than named wrongly
    return slot < SlotCount ? textureSlotNames[slot] : "";
}

uint8_t SurfaceTexture::slotFromName(const char *name)
{
    if (name && name[0]) {
        for (uint8_t i = 0; i < SlotCount; ++i) {
            if (std::strcmp(name, textureSlotNames[i]) == 0) {
                return i;
            }
        }
    }
    return SlotCount;
}

//===========================================================================
// MaterialAppearance
//===========================================================================
MaterialAppearance::MaterialAppearance()
  : shininess{0.2000f}
  , transparency{}
{
    setType(STEEL);
    setType(USER_DEFINED);
}

MaterialAppearance::MaterialAppearance(const char* MatName)
  : shininess{0.2000f}
  , transparency{}
{
    set(MatName);
}

MaterialAppearance::MaterialAppearance(const MaterialType MatType)
  : shininess{0.2000f}
  , transparency{}
{
    setType(MatType);
}

void MaterialAppearance::set(const char* MatName)
{
    if (strcmp("Brass",MatName) == 0 ) {
        setType(BRASS);
    }
    else if (strcmp("Bronze",MatName) == 0 ) {
        setType(BRONZE);
    }
    else if (strcmp("Copper",MatName) == 0 ) {
        setType(COPPER);
    }
    else if (strcmp("Gold",MatName) == 0 ) {
//      ambientColor.set(0.3f,0.1f,0.1f);
//    diffuseColor.set(0.8f,0.7f,0.2f);
//    specularColor.set(0.4f,0.3f,0.1f);
//    shininess = .4f;
//    transparency = .0f;
////    ambientColor.set(0.3f,0.1f,0.1f);
////    diffuseColor.set(0.22f,0.15f,0.00f);
////    specularColor.set(0.71f,0.70f,0.56f);
////    shininess = .16f;
////    transparency = .0f;
////    ambientColor.set(0.24725f, 0.1995f, 0.0745f);
////    diffuseColor.set(0.75164f, 0.60648f, 0.22648f);
////    specularColor.set(0.628281f, 0.555802f, 0.366065f);
////    shininess = .16f;
////    transparency = .0f;
        setType(GOLD);
    }
    else if (strcmp("Pewter",MatName) == 0 ) {
        setType(PEWTER);
    }
    else if (strcmp("Plaster",MatName) == 0 ) {
        setType(PLASTER);
    }
    else if (strcmp("Plastic",MatName) == 0 ) {
        setType(PLASTIC);
    }
    else if (strcmp("Silver",MatName) == 0 ) {
        setType(SILVER);
    }
    else if (strcmp("Steel",MatName) == 0 ) {
        setType(STEEL);
    }
    else if (strcmp("Stone",MatName) == 0 ) {
//    ambientColor.set(0.0f,0.0f,0.0f);
//    diffuseColor.set(0.0f,0.0f,0.0f);
//    specularColor.set(0.4f,0.3f,0.1f);
//    shininess = .4f;
//    transparency = .0f;
        setType(STONE);
    }
    else if (strcmp("Shiny plastic",MatName) == 0 ) {
        setType(SHINY_PLASTIC);
    }
    else if (strcmp("Satin",MatName) == 0 ) {
        setType(SATIN);
    }
    else if (strcmp("Metalized",MatName) == 0 ) {
        setType(METALIZED);
    }
    else if (strcmp("Neon GNC",MatName) == 0 ) {
        setType(NEON_GNC);
    }
    else if (strcmp("Chrome",MatName) == 0 ) {
        setType(CHROME);
    }
    else if (strcmp("Aluminium",MatName) == 0 ) {
        setType(ALUMINIUM);
    }
    else if (strcmp("Obsidian",MatName) == 0 ) {
        setType(OBSIDIAN);
    }
    else if (strcmp("Neon PHC",MatName) == 0 ) {
        setType(NEON_PHC);
    }
    else if (strcmp("Jade",MatName) == 0 ) {
        setType(JADE);
    }
    else if (strcmp("Ruby",MatName) == 0 ) {
        setType(RUBY);
    }
    else if (strcmp("Emerald",MatName) == 0 ) {
        setType(EMERALD);
    }
    else if (strcmp("Default",MatName) == 0 ) {
        setType(DEFAULT);
    }
    else {
        setType(USER_DEFINED);
    }
}

void MaterialAppearance::setType(const MaterialType MatType)
{
    _matType = MatType;
    // A preset states the whole material and none of them states a finish
    // or a texture, so a previous one must not survive -- the same rule
    // the colours and both floats below already follow. USER_DEFINED
    // states nothing and therefore changes nothing, here as in the switch.
    if (MatType != USER_DEFINED) {
        finish = SurfaceFinish();
        texture = SurfaceTexture();
    }
    switch (MatType)
    {
    case BRASS:
        ambientColor .set(0.0910f,0.0778f,0.0423f);
        diffuseColor .set(0.2275f,0.1945f,0.1057f);
        specularColor.set(0.9100f,0.7780f,0.4230f);
        emissiveColor.set(0.0000f,0.0000f,0.0000f);
        shininess    = 0.7500f;
        transparency = 0.0000f;
        break;
    case BRONZE:
        ambientColor .set(0.0910f,0.0700f,0.0450f);
        diffuseColor .set(0.2275f,0.1750f,0.1125f);
        specularColor.set(0.9100f,0.7000f,0.4500f);
        emissiveColor.set(0.0000f,0.0000f,0.0000f);
        shininess    = 0.4500f;
        transparency = 0.0000f;
        break;
    case COPPER:
        ambientColor .set(0.0955f,0.0638f,0.0538f);
        diffuseColor .set(0.2387f,0.1595f,0.1345f);
        specularColor.set(0.9550f,0.6380f,0.5380f);
        emissiveColor.set(0.0000f,0.0000f,0.0000f);
        shininess    = 0.8500f;
        transparency = 0.0000f;
        break;
    case GOLD:
        ambientColor .set(0.1000f,0.0766f,0.0336f);
        diffuseColor .set(0.2500f,0.1915f,0.0840f);
        specularColor.set(1.0000f,0.7660f,0.3360f);
        emissiveColor.set(0.0000f,0.0000f,0.0000f);
        shininess    = 0.9000f;
        transparency = 0.0000f;
        break;
    case PEWTER:
        ambientColor .set(0.0797f,0.0789f,0.0775f);
        diffuseColor .set(0.1993f,0.1973f,0.1938f);
        specularColor.set(0.7970f,0.7890f,0.7750f);
        emissiveColor.set(0.0000f,0.0000f,0.0000f);
        shininess    = 0.3000f;
        transparency = 0.0000f;
        break;
    case PLASTER:
        ambientColor .set(0.2000f,0.2000f,0.2000f);
        diffuseColor .set(0.8000f,0.8000f,0.8000f);
        specularColor.set(0.0400f,0.0400f,0.0400f);
        emissiveColor.set(0.0000f,0.0000f,0.0000f);
        shininess    = 0.0078f;
        transparency = 0.0000f;
        break;
    case PLASTIC:
        ambientColor .set(0.1375f,0.1375f,0.1375f);
        diffuseColor .set(0.5500f,0.5500f,0.5500f);
        specularColor.set(0.0500f,0.0500f,0.0500f);
        emissiveColor.set(0.0000f,0.0000f,0.0000f);
        shininess    = 0.0078f;
        transparency = 0.0000f;
        break;
    case SILVER:
        ambientColor .set(0.0972f,0.0960f,0.0915f);
        diffuseColor .set(0.2430f,0.2400f,0.2288f);
        specularColor.set(0.9720f,0.9600f,0.9150f);
        emissiveColor.set(0.0000f,0.0000f,0.0000f);
        shininess    = 0.9500f;
        transparency = 0.0000f;
        break;
    case STEEL:
        ambientColor .set(0.0562f,0.0565f,0.0578f);
        diffuseColor .set(0.1405f,0.1412f,0.1445f);
        specularColor.set(0.5620f,0.5650f,0.5780f);
        emissiveColor.set(0.0000f,0.0000f,0.0000f);
        shininess    = 0.6000f;
        transparency = 0.0000f;
        break;
    case STONE:
        ambientColor .set(0.1900f,0.1520f,0.1178f);
        diffuseColor .set(0.7500f,0.6000f,0.4650f);
        specularColor.set(0.0400f,0.0400f,0.0400f);
        emissiveColor.set(0.0000f,0.0000f,0.0000f);
        shininess    = 0.1700f;
        transparency = 0.0000f;
        break;
    case SHINY_PLASTIC:
        ambientColor .set(0.1375f,0.1375f,0.1375f);
        diffuseColor .set(0.5500f,0.5500f,0.5500f);
        specularColor.set(0.0500f,0.0500f,0.0500f);
        emissiveColor.set(0.0000f,0.0000f,0.0000f);
        shininess    = 1.0000f;
        transparency = 0.0000f;
        break;
    case SATIN:
        ambientColor .set(0.1375f,0.1375f,0.1375f);
        diffuseColor .set(0.5500f,0.5500f,0.5500f);
        specularColor.set(0.0500f,0.0500f,0.0500f);
        emissiveColor.set(0.0000f,0.0000f,0.0000f);
        shininess    = 0.0938f;
        transparency = 0.0000f;
        break;
    case METALIZED:
        ambientColor .set(0.1800f,0.1800f,0.1800f);
        diffuseColor .set(0.0000f,0.0000f,0.0000f);
        specularColor.set(0.4500f,0.4500f,0.4500f);
        emissiveColor.set(0.0000f,0.0000f,0.0000f);
        shininess    = 0.1300f;
        transparency = 0.0000f;
        break;
    case NEON_GNC:
        ambientColor .set(0.2000f,0.2000f,0.2000f);
        diffuseColor .set(0.0000f,0.0000f,0.0000f);
        specularColor.set(0.6200f,0.6200f,0.6200f);
        emissiveColor.set(1.0000f,1.0000f,0.0000f);
        shininess    = 0.0500f;
        transparency = 0.0000f;
        break;
    case CHROME:
        ambientColor .set(0.0550f,0.0556f,0.0554f);
        diffuseColor .set(0.1375f,0.1390f,0.1385f);
        specularColor.set(0.5500f,0.5560f,0.5540f);
        emissiveColor.set(0.0000f,0.0000f,0.0000f);
        shininess    = 1.0000f;
        transparency = 0.0000f;
        break;
    case ALUMINIUM:
        ambientColor .set(0.0913f,0.0922f,0.0924f);
        diffuseColor .set(0.2283f,0.2305f,0.2310f);
        specularColor.set(0.9130f,0.9220f,0.9240f);
        emissiveColor.set(0.0000f,0.0000f,0.0000f);
        shininess    = 0.5500f;
        transparency = 0.0000f;
        break;
    case OBSIDIAN:
        ambientColor .set(0.0538f,0.0500f,0.0662f);
        diffuseColor .set(0.1828f,0.1700f,0.2253f);
        specularColor.set(0.0400f,0.0400f,0.0400f);
        emissiveColor.set(0.0000f,0.0000f,0.0000f);
        shininess    = 0.8000f;
        transparency = 0.0000f;
        break;
    case NEON_PHC:
        ambientColor .set(1.0000f,1.0000f,1.0000f);
        diffuseColor .set(1.0000f,1.0000f,1.0000f);
        specularColor.set(0.6200f,0.6200f,0.6200f);
        emissiveColor.set(0.0000f,0.9000f,0.4140f);
        shininess    = 0.0500f;
        transparency = 0.0000f;
        break;
    case JADE:
        ambientColor .set(0.1350f,0.2225f,0.1575f);
        diffuseColor .set(0.5400f,0.8900f,0.6300f);
        specularColor.set(0.0616f,0.0616f,0.0616f);
        emissiveColor.set(0.0000f,0.0000f,0.0000f);
        shininess    = 0.6000f;
        transparency = 0.0000f;
        break;
    case RUBY:
        ambientColor .set(0.1745f,0.0118f,0.0118f);
        diffuseColor .set(0.6142f,0.0414f,0.0414f);
        specularColor.set(0.0766f,0.0766f,0.0766f);
        emissiveColor.set(0.0000f,0.0000f,0.0000f);
        shininess    = 0.6000f;
        transparency = 0.0000f;
        break;
    case EMERALD:
        ambientColor .set(0.0215f,0.1745f,0.0215f);
        diffuseColor .set(0.0757f,0.6142f,0.0757f);
        specularColor.set(0.0501f,0.0501f,0.0501f);
        emissiveColor.set(0.0000f,0.0000f,0.0000f);
        shininess    = 0.6000f;
        transparency = 0.0000f;
        break;
    case USER_DEFINED:
        break;
    default:
        // The Default appearance card, to the number
        // (Mod/Material/Resources/Materials/Appearance/Default.FCMat).
        // A shape carries that card and this preset both, and they used
        // to disagree: assigning a fresh object the very material it
        // already claimed changed how it looked. The card is the one
        // authored for this engine -- 0.2209 is a dielectric's F0 of
        // 0.04 in the encoded form every colour is stored in, and
        // 0.3729 is roughness 0.40 through the shininess mapping -- so
        // it is the one the other two follow (View/DefaultShapeColor
        // and View/DefaultShapeShininess are the others).
        ambientColor .set(0.3333f,0.3333f,0.3333f);
        diffuseColor .set(0.8000f,0.8000f,0.9000f);
        specularColor.set(0.2209f,0.2209f,0.2209f);
        emissiveColor.set(0.0000f,0.0000f,0.0000f);
        shininess    = 0.3729f;
        transparency = 0.0000f;
        break;
    }
}

float MaterialAppearance::shininessToRoughness(float shininess)
{
    // The classical Blinn-Phong to microfacet match is on the GGX/Beckmann
    // WIDTH: alpha = sqrt(2 / (n + 2)) for a Phong exponent n. Roughness is
    // the square root of that width -- every consumer squares it to get
    // alpha back (the shader's `a = rough * rough`, which is the glTF
    // convention) -- so the fit has to be taken to the fourth root, not the
    // second. Handing the alpha itself over as a roughness squared it a
    // second time and made every ordinary Phong appearance shade as a
    // mirror: FreeCAD's default shininess 0.2 arrived at alpha 0.073 where
    // the match says 0.269.
    const float exponent = std::max(shininess, 0.0f) * 128.0f;
    return std::min(std::pow(2.0f / (exponent + 2.0f), 0.25f), 1.0f);
}

float MaterialAppearance::roughnessToShininess(float roughness)
{
    const float alpha = std::max(roughness * roughness, 1e-3f);
    const float exponent = 2.0f / (alpha * alpha) - 2.0f;
    return std::clamp(exponent / 128.0f, 0.0f, 1.0f);
}

MaterialAppearance MaterialAppearance::pbrToPhong(const MaterialAppearance& raw)
{
    MaterialAppearance mat = raw;
    const Color& base = raw.diffuseColor;
    const Color& tint = raw.specularColor;
    const float metallic = tint.a;
    auto mix = [metallic](float dielectric, float metal) {
        return 0.04f * dielectric * (1.0f - metallic) + metal * metallic;
    };
    mat.specularColor.set(mix(tint.r, base.r), mix(tint.g, base.g), mix(tint.b, base.b));
    mat.shininess = roughnessToShininess(raw.shininess);
    mat.pbr = false;
    return mat;
}

MaterialAppearance MaterialAppearance::phongToPbr(const MaterialAppearance& classic)
{
    MaterialAppearance mat = classic;
    // White tint, metallic 0: a Phong specular is an intensity, and every
    // Phong surface is a dielectric as far as the model can say.
    mat.specularColor.set(1.0f, 1.0f, 1.0f, 0.0f);
    mat.shininess = shininessToRoughness(classic.shininess);
    mat.pbr = true;
    return mat;
}

void MaterialAppearance::setPBR(bool enable)
{
    if (pbr == enable)
        return;
    *this = enable ? phongToPbr(*this) : pbrToPhong(*this);
}

float MaterialAppearance::getMetallic() const
{
    return pbr ? specularColor.a : 0.0f;  // the Phong model has no metals
}

float MaterialAppearance::getRoughness() const
{
    return pbr ? shininess : shininessToRoughness(shininess);
}

void MaterialAppearance::setMetallic(float value)
{
    // Stating a metallic factor decides the mode: the Phong slot this
    // lands in means something else, and a value written there would be
    // thrown away by the conversion the moment the mode was switched.
    setPBR(true);
    specularColor.a = value;
}

void MaterialAppearance::setRoughness(float value)
{
    setPBR(true);
    shininess = value;
}
