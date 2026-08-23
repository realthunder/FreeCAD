// SPDX-License-Identifier: LGPL-2.1-or-later
/***************************************************************************
 *   Copyright (c) 2014 Yorik van Havre <yorik@uncreated.net>              *
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

#pragma once

#include <set>

#include <App/DocumentObject.h>
#include <App/GeoFeature.h>
#include <App/FeaturePython.h>
#include <Mod/Part/App/PartFeature.h>

#include "PropertyPath.h"


namespace Path
{

// Derived from Part::Feature, not App::GeoFeature: this fork can build a real
// shape out of a toolpath (see BuildShape below), which means the feature has
// to own a Shape property and be pickable like any other piece of geometry.
class PathExport Feature: public Part::Feature
{
    using inherited = Part::Feature;
    PROPERTY_HEADER_WITH_OVERRIDE(Path::Feature);

public:
    /// Constructor
    Feature();
    ~Feature() override;

    /// returns the type name of the ViewProvider
    const char* getViewProviderName() const override
    {
        return "PathGui::ViewProviderPath";
    }
    App::DocumentObjectExecReturn* execute() override
    {
        return App::DocumentObject::StdReturn;
    }
    short mustExecute() const override;
    PyObject* getPyObject() override;

    PropertyPath Path;
    /// Build a wire shape out of the toolpath, rather than only drawing it
    App::PropertyBool BuildShape;
    /// Command numbers to leave out of the shape, one-based
    App::PropertyIntegerList CommandFilter;


protected:
    /// get called by the container when a property has changed
    void onChanged(const App::Property* prop) override;
};

/// Build a wire compound from the moves of a toolpath, skipping any command
/// whose one-based number is in the filter. Rapids are skipped as well: only
/// cutting moves become edges.
Part::TopoShape PathExport shapeFromPath(const Toolpath& path, const std::set<int>& filter = {});

using FeaturePython = App::FeaturePythonT<Feature>;

}  // namespace Path
