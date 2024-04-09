/***************************************************************************
 *   Copyright (c) 2011 Jürgen Riegel <juergen.riegel@web.de>              *
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

#ifndef POINTS_POINTPROPERTIES_H
#define POINTS_POINTPROPERTIES_H

#include <vector>

#include <App/PropertyStandard.h>
#include <Base/Matrix.h>
#include <Base/Reader.h>
#include <Base/Writer.h>

#include "Points.h"


namespace Points
{

/** Greyvalue property.
 */
class PointsExport PropertyGreyValue: public App::PropertyFloat
{
    TYPESYSTEM_HEADER_WITH_OVERRIDE();

public:
    PropertyGreyValue() = default;
};

class PointsExport PropertyGreyValueList: public App::_PropertyFloatList
{
    TYPESYSTEM_HEADER_WITH_OVERRIDE();
public:
    /** @name Modify */
    //@{
    void removeIndices(const std::vector<unsigned long>&);
    //@}
};

class PointsExport PropertyNormalList: public App::_PropertyVectorList
{
    TYPESYSTEM_HEADER_WITH_OVERRIDE();

public:

    /** @name Modify */
    //@{
    void transformGeometry(const Base::Matrix4D& rclMat);
    void removeIndices(const std::vector<unsigned long>&);
    //@}
};

/** Curvature information. */
struct PointsExport CurvatureInfo
{
    float fMaxCurvature, fMinCurvature;
    Base::Vector3f cMaxCurvDir, cMinCurvDir;

    bool operator == (const CurvatureInfo &other) const {
        return fMaxCurvature == other.fMaxCurvature
            && fMinCurvature == other.fMinCurvature
            && cMaxCurvDir == other.cMaxCurvDir
            && cMinCurvDir == other.cMinCurvDir;
    }
};

/** The Curvature property class.
 */
class PointsExport PropertyCurvatureList: public App::PropertyListsT<CurvatureInfo>
{
    using inherited = PropertyListsT<CurvatureInfo>;
    TYPESYSTEM_HEADER_WITH_OVERRIDE();

public:
    enum
    {
        MeanCurvature = 0,  /**< Mean curvature */
        GaussCurvature = 1, /**< Gaussian curvature */
        MaxCurvature = 2,   /**< Maximum curvature */
        MinCurvature = 3,   /**< Minimum curvature */
        AbsCurvature = 4    /**< Absolute curvature */
    };

public:
    PropertyCurvatureList();

    std::vector<float> getCurvature( int tMode) const;

    PyObject *getPyObject() override;

    /** @name Undo/Redo */
    //@{
    /// returns a new copy of the property (mainly for Undo/Redo and transactions)
    App::Property *Copy() const override;
    /// paste the value from the property (mainly for Undo/Redo and transactions)
    void Paste(const App::Property &from) override;
    //@}

    /** @name Modify */
    //@{
    void transformGeometry(const Base::Matrix4D& rclMat);
    void removeIndices(const std::vector<unsigned long>&);
    //@}

protected:
    CurvatureInfo getPyValue(PyObject *) const override;

    void restoreXML(Base::XMLReader &) override;
    bool saveXML(Base::Writer &) const override;
    bool canSaveStream(Base::Writer &) const override { return true; }
    void restoreStream(Base::InputStream &s, unsigned count) override;
    void saveStream(Base::OutputStream &) const override;
};

}  // namespace Points


#endif  // POINTS_POINTPROPERTIES_H
