/***************************************************************************
 *   Copyright (c) 2019 WandererFan <wandererfan@gmail.com>                *
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

#ifndef ShapeExtractor_h_
#define ShapeExtractor_h_

#include <TopoDS_Shape.hxx>

#include <App/DocumentObject.h>
#include <App/Link.h>
#include <Base/Type.h>
#include <Base/Vector3D.h>
#include <Mod/Part/App/TopoShape.h>

#include <Mod/TechDraw/TechDrawGlobal.h>


namespace TechDraw
{

class TechDrawExport ShapeExtractor
{
public:
    //! The shape getters return Part::TopoShape rather than TopoDS_Shape so that the
    //! element map of the source features reaches the view geometry -- see
    //! docs/TopoNamingEnhance.md section 3.  The shapes are assembled with the
    //! name-propagating makers (makECompound, makEBoolean) for the same reason.
    static Part::TopoShape getShapes(const std::vector<App::DocumentObject*> links, bool include2d = true);
    static std::vector<Part::TopoShape> getShapes2d(const std::vector<App::DocumentObject*> links);
    static std::vector<Part::TopoShape> getShapesFromObject(const App::DocumentObject* docObj);
    static Part::TopoShape getShapesFused(const std::vector<App::DocumentObject*> links);
    static TopoDS_Shape getShapeFromXLink(const App::Link* xLink);

    static bool is2dObject(App::DocumentObject* obj);
    static bool isEdgeType(App::DocumentObject* obj);
    static bool isPointType(App::DocumentObject* obj);
    static bool isDraftPoint(App::DocumentObject* obj);
    static bool isDatumPoint(App::DocumentObject* obj);
    static Base::Vector3d getLocation3dFromFeat(App::DocumentObject *obj);

    static Part::TopoShape stripInfiniteShapes(const Part::TopoShape& inShape);

    static Part::TopoShape getLocatedShape(const App::DocumentObject* docObj);

    static bool isSketchObject(const App::DocumentObject* obj);

protected:

private:

};

} //namespace TechDraw

#endif  // #ifndef ShapeExtractor_h_
