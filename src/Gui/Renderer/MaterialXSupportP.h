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

#ifndef RENDER_MATERIALX_SUPPORT_P_H
#define RENDER_MATERIALX_SUPPORT_P_H

/// The MaterialX-typed half of MaterialXSupport.h: what the consumers
/// of a document share (the Cycles interpreter, the raster shader
/// generator). Only a translation unit built with MaterialX on its
/// include path may include this -- MaterialXSupport.h is the one the
/// rest of the tree sees.

#include <string>
#include <vector>

#include <MaterialXCore/Document.h>

#include "MaterialXSupport.h"

/// The library's own convention, and the one every MaterialX example
/// uses. Declared at global scope on purpose: inside
/// Render::MaterialX, a bare "MaterialX" would name our namespace.
namespace mx = ::MaterialX;

namespace Render::MaterialX {

/// The standard data library -- stdlib, pbrlib, bxdf and the rest of
/// dataLibraryPath() -- parsed once and shared. Null when the library
/// is not on disk, which is a deployment fault worth reporting once.
mx::ConstDocumentPtr dataLibrary();

/// Parse a document and resolve its node references against the data
/// library. Null on failure, with why in `error`. Never throws.
mx::DocumentPtr loadDocument(const std::string &xml, std::string &error);

/// The surface-shader nodes a document renders, in document order: the
/// shader nodes of its material nodes, plus any standalone surface
/// shader. Empty means the document describes no surface, which is the
/// one thing a syntactically valid document can still get wrong.
std::vector<mx::NodePtr> surfaceShaders(const mx::DocumentPtr &doc);

}  // namespace Render::MaterialX

#endif  // RENDER_MATERIALX_SUPPORT_P_H
