# Demo MaterialX materials

Material documents for `scripts/demo-materialx.py`, which embeds each one
in an `App::ShaderProgram` and hangs it on a material ball
(docs/CyclesIntegration.md sec 8 item 15 phase B).

## Where they come from

Sixteen of them are the example materials of
[MaterialX](https://github.com/AcademySoftwareFoundation/MaterialX),
downloaded from `resources/Materials/Examples` at tag **v1.39.5** -- the
version this tree vendors -- and licensed **Apache-2.0** with the rest of
that project:

    open_pbr_aluminum_brushed  open_pbr_carpaint  open_pbr_default
    open_pbr_glass             open_pbr_honey     open_pbr_ketchup
    open_pbr_pearl             open_pbr_velvet
    standard_surface_chrome    standard_surface_copper
    standard_surface_gold      standard_surface_jade
    standard_surface_marble_solid                 standard_surface_plastic
    standard_surface_thin_film standard_surface_velvet

They are copies rather than references into `src/3rdParty/MaterialX`,
because that is a submodule a checkout may not have, and because a demo
asset should not move when the submodule is bumped.

`fc_declared_interface.mtlx` is ours (same LGPL as the rest of this
repository). It exists because the MaterialX examples are a thin place to
look at step 3: only `standard_surface_marble_solid` declares a graph
interface, and this one declares five inputs over a procedural pattern
that needs no image, so a ball built from it comes up with five knobs in
the property editor.

## Why these sixteen

Every one of them renders in the RASTER path as well as in the path
tracer: none uses an image node, which the raster half does not bind yet
(sec 6.10), and each states a shading model that reaches OpenPBR -- either
directly or through MaterialX's own translation graphs. The other 34
example materials in the library either name image files or state a model
(`UsdPreviewSurface`, `gltf_pbr`, `disney_principled`, the hair models)
with no translation to OpenPBR, and would draw as their stock appearance.
