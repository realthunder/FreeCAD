// SPDX-License-Identifier: LGPL-2.1-or-later

/***************************************************************************
 *   Copyright (c) 2026 Zheng Lei <realthunder.dev@gmail.com>              *
 *                                                                         *
 *   This file is part of FreeCAD.                                         *
 *                                                                         *
 *   FreeCAD is free software: you can redistribute it and/or modify it    *
 *   under the terms of the GNU Lesser General Public License as           *
 *   published by the Free Software Foundation, either version 2.1 of the  *
 *   License, or (at your option) any later version.                       *
 *                                                                         *
 *   FreeCAD is distributed in the hope that it will be useful, but        *
 *   WITHOUT ANY WARRANTY; without even the implied warranty of            *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU      *
 *   Lesser General Public License for more details.                       *
 *                                                                         *
 *   You should have received a copy of the GNU Lesser General Public      *
 *   License along with FreeCAD. If not, see                               *
 *   <https://www.gnu.org/licenses/>.                                      *
 *                                                                         *
 **************************************************************************/

#ifndef MATERIAL_SHADERGRAPH_H
#define MATERIAL_SHADERGRAPH_H

#include <memory>

#include <Mod/Material/MaterialGlobal.h>

namespace App
{
class DocumentObject;
class ShaderBinding;
class ShaderProgram;
}  // namespace App

namespace Materials
{
class Material;
class PropertyMaterial;

/** Materializing a card's shader graph on the object that wears it.
 *
 * The "Edit Shader Graph..." half of docs/MaterialStorage.md 17.11. A card
 * carries its MaterialX graph as content the object cannot edit; putting
 * the same bytes into an App::ShaderProgram (MATERIALX dialect) grouped by
 * an App::Shader and bound to the object by a Scope=Object
 * App::ShaderBinding makes them editable in place. The object goes on
 * wearing the card the whole time -- an explicit binding beats the card an
 * object wears, so the edited graph is what is drawn -- and reverting is
 * removing the three objects: the card's look reappears by construction.
 *
 * The same shape as the bundled render effects' activate/deactivate
 * (src/Ext/freecad/rendereffects), seeded from the card instead of an
 * effect package.
 */
namespace ShaderGraph
{
/// The binding materialized on \a owner, or null
MaterialsExport App::ShaderBinding* materialized(const App::DocumentObject* owner);
/// The program a materialized binding was made for, or null when \a
/// binding is not one
MaterialsExport App::ShaderProgram* programOf(const App::ShaderBinding* binding);
/** Materialize the graph of \a card onto \a owner.
 *
 * Returns the binding already there when there is one. Null when the
 * card carries no graph, or its bytes are not all in the document's
 * store yet. The images are stored on the program FIRST and the text set
 * SECOND, so the sync that runs on the text change finds every name
 * carried and imports nothing (17.11).
 */
MaterialsExport App::ShaderBinding* materialize(App::DocumentObject* owner, PropertyMaterial& card);
/// Whether the materialized program's text differs from the graph it was
/// made from: what a revert would discard
MaterialsExport bool edited(const App::DocumentObject* owner);
/** Remove what materialize() made for \a owner. False when nothing was.
 *
 * A binding that has since been given other targets as well keeps them:
 * only \a owner leaves it.
 */
MaterialsExport bool revert(App::DocumentObject* owner);
/** The card the edit amounts to: a copy of \a card inheriting from it,
 * whose graph is the materialized program's text as it is now and whose
 * images are the ones the program carries.
 *
 * The return leg of 17.12: Inherit, pick a graph, Edit, Save. The text is
 * written into the document's blob store so the library save has a file
 * to place. Null when nothing is materialized on \a owner.
 */
MaterialsExport std::shared_ptr<Material> cardFromEdit(const App::DocumentObject* owner,
                                                       const PropertyMaterial& card);
}  // namespace ShaderGraph

}  // namespace Materials

#endif  // MATERIAL_SHADERGRAPH_H
