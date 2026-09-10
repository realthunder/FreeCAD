/***************************************************************************
 *   Copyright (c) 2026 Zheng Lei (realthunder) <realthunder.dev@gmail.com>*
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

#ifndef RENDER_ENVIRONMENT_H
#define RENDER_ENVIRONMENT_H

#include "Renderer.h"

/// The environment every backend lights with, as a function of
/// direction -- backend-neutral, so the bgfx cube map and the Cycles
/// world texture are baked from the SAME radiance and a model looks
/// lit by the same room in both (docs/CyclesIntegration.md sec 6).
namespace Render {

/// sRGB-encoded to linear (IEC 61966-2-1): the inverse of the output
/// transform, and what an authored (display) colour goes through on
/// its way into the linear shading math.
RendererExport float srgbToLinear(float c);

/// Radiance of the built-in procedural environment \a preset
/// (PBRConfig::envPreset) in world direction \a d (unit length, +Z up).
/// Every preset integrates to the same mean radiance, so swapping one
/// for another changes contrast, never exposure.
RendererExport void envRadianceProcedural(int preset, const float d[3], float out[3]);

/// Radiance of a user environment picture (PBRConfig::envImage) in
/// direction \a d: equirectangular when the picture is 2:1 or wider,
/// otherwise a GL sphere map facing the front view. An 8-bit picture
/// is decoded to linear -- exactly (\a managed) or by the legacy
/// squaring; an F32 picture is HDR radiance and is taken as is.
RendererExport void sampleEnvImage(const TextureImage &img,
                                   const float d[3],
                                   float out[3],
                                   bool managed);

/// The environment a PBRConfig describes: the picture where one is
/// set, else the procedural preset.
RendererExport void envRadiance(const PBRConfig &pbr, const float d[3], float out[3], bool managed);

/// Half-angle, in radians, of the aperture the background is drawn
/// through for softening  blur (PBRConfig::envBlur, 0..1).
///
/// The backdrop is defocused, so the filter every backend applies to
/// it is a LENS: the sharp environment convolved with the disc of
/// directions an aperture subtends, uniformly over solid angle. This
/// is the one shared number behind that -- the raster backend spreads
/// its taps over this cone, the path tracer bakes its camera-ray copy
/// convolved with it -- so one slider position is one softness in
/// both, and neither backend is free to invent its own law.
///
/// Zero is sharp; one opens to 45 degrees, as wide as a defocus can
/// go before the backdrop stops reading as a place. In between it
/// doubles every eighth of the slider -- the same eight halvings the
/// raster cube map's mip chain used to be walked along, now read as
/// an angle instead of a mip level.
RendererExport float envBlurAngle(float blur);

}  // namespace Render

#endif  // RENDER_ENVIRONMENT_H
