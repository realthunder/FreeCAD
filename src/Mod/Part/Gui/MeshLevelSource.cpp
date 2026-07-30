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
 ***************************************************************************/

/// The producer half of the shape-backed level generator: registration
/// of shapes against the render feed's source tags
/// (Render::MeshSourceRegistry). The build itself is a pure function
/// of (shape, params, source chunk) — MeshLevelBuild.cpp — so the
/// registered closure is safe on any of the scene server's level
/// threads, and levels of one publish build concurrently.

#include "PreCompiled.h"

#ifndef _PreComp_
# include <BRepBndLib.hxx>
# include <Bnd_Box.hxx>
# include <Precision.hxx>
# include <Standard_Failure.hxx>
# include <TopoDS_Shape.hxx>
#endif

#include <cmath>
#include <cstdlib>
#include <memory>
#include <vector>

#include <Gui/Renderer/MeshSource.h>
#include <Gui/Renderer/SceneDump.h>
#include <Gui/Renderer/SceneLadder.h>

#include "MeshLevelSource.h"

using namespace PartGui;

namespace {

/// The registered closure owns a refcounted handle of the exact shape
/// the display tessellation meshed, plus the job parameters. Const
/// after registration: level threads only read it.
struct LevelSourceState {
    TopoDS_Shape shape;
    MeshLevelJob params;
};
using LevelSourceStatePtr = std::shared_ptr<LevelSourceState>;

} // anonymous namespace

int PartGui::coarseTessellationLevel()
{
    static const int level = [] {
        const char *env = std::getenv("FC_COARSE_TESSELLATION");
        if (!env || !*env)
            return -1;
        int lvl = std::atoi(env);
        return lvl >= 0 && lvl < 8 ? lvl : -1;
    }();
    return level;
}

void PartGui::registerMeshLevelSource(const TopoDS_Shape &shape,
                                      bool normalsFromUV, SoNode *faceTag,
                                      SoNode *lineTag, float builtError,
                                      double exactDeflection,
                                      double exactAngle)
{
    if (shape.IsNull() || (!faceTag && !lineTag))
        return;
    // A degenerate shape cannot ladder; checked here so a registered
    // source always means "levels can be built".
    try {
        Bnd_Box bounds;
        BRepBndLib::Add(shape, bounds);
        bounds.SetGap(0.0);
        if (bounds.IsVoid())
            return;
        Standard_Real x0, y0, z0, x1, y1, z1;
        bounds.Get(x0, y0, z0, x1, y1, z1);
        double dx = x1 - x0, dy = y1 - y0, dz = z1 - z0;
        if (!(std::sqrt(dx * dx + dy * dy + dz * dz) > 0))
            return;
    }
    catch (const Standard_Failure &) {
        return;
    }

    auto st = std::make_shared<LevelSourceState>();
    st->shape = shape;
    st->params.normalsFromUV = normalsFromUV;
    st->params.exactDeflection = exactDeflection;
    st->params.exactAngle = exactAngle;

    auto gen = [st](uint32_t level, const void *chunk, size_t size,
                    std::vector<uint8_t> &out) {
        MeshLevelJob job = st->params;
        job.level = level;
        return buildMeshLevel(st->shape, job, chunk, size, out);
    };
    auto &reg = Render::MeshSourceRegistry::instance();
    if (faceTag)
        reg.add(faceTag, gen, builtError);
    if (lineTag)
        reg.add(lineTag, gen, builtError);
}

void PartGui::unregisterMeshLevelSource(SoNode *faceTag, SoNode *lineTag)
{
    auto &reg = Render::MeshSourceRegistry::instance();
    if (faceTag)
        reg.remove(faceTag);
    if (lineTag)
        reg.remove(lineTag);
}
