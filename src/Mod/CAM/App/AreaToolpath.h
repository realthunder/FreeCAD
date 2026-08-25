/***************************************************************************
 *   Copyright (c) 2017 Zheng, Lei <realthunder.dev@gmail.com>             *
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

#ifndef PATH_AreaToolpath_H
#define PATH_AreaToolpath_H

#include <list>
#include <memory>

#include <Base/BoundBox.h>

#include <TopoDS_Shape.hxx>
#include <gp_Pnt.hxx>

#include <Mod/Area/App/Area.h>
#include <Mod/CAM/PathGlobal.h>

namespace Path
{

class Toolpath;

/** Convert a list of wires to gcode
 *
 * \arg \c path: output toolpath
 * \arg \c shapes: input list of shapes
 * \arg \c pstart: optional start point
 * \arg \c pend: optional output containing the ending point
 *
 * See #AREA_PARAMS_PATH for other arguments.
 *
 * This was Area::toPath. It lives here rather than in the area engine because
 * G-code is the one thing in that engine that a non-CAM caller -- the BIM IFC
 * importer, for one -- has no use for, and it was the only reason the engine
 * needed the Path module at all.
 */
/** Build the area a toolpath has already cleared
 *
 * \arg \c path: the toolpath to walk
 * \arg \c diameter: tool diameter
 * \arg \c zmax: ignore anything above this height
 * \arg \c bbox: only consider moves inside this box
 *
 * This was Area::getClearedArea. Like areaToPath above it lives on this side
 * because it walks a Toolpath, which the area engine deliberately knows
 * nothing about.
 */
PathExport std::shared_ptr<AreaLib::Area> clearedAreaFromPath(const Toolpath& path,
    double diameter, double zmax, const Base::BoundBox3d& bbox);

PathExport void areaToPath(Toolpath& path, const std::list<TopoDS_Shape>& shapes,
    const gp_Pnt* pstart = nullptr, gp_Pnt* pend = nullptr,
    PARAM_ARGS_DEF(PARAM_FARG, AREA_PARAMS_PATH));

}  // namespace Path

#endif  // PATH_AreaToolpath_H
