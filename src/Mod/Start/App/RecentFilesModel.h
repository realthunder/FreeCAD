// SPDX-License-Identifier: LGPL-2.1-or-later
/****************************************************************************
 *                                                                          *
 *   Copyright (c) 2024 The FreeCAD Project Association AISBL               *
 *                                                                          *
 *   This file is part of FreeCAD.                                          *
 *                                                                          *
 *   FreeCAD is free software: you can redistribute it and/or modify it     *
 *   under the terms of the GNU Lesser General Public License as            *
 *   published by the Free Software Foundation, either version 2.1 of the   *
 *   License, or (at your option) any later version.                        *
 *                                                                          *
 *   FreeCAD is distributed in the hope that it will be useful, but         *
 *   WITHOUT ANY WARRANTY; without even the implied warranty of             *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU       *
 *   Lesser General Public License for more details.                        *
 *                                                                          *
 *   You should have received a copy of the GNU Lesser General Public       *
 *   License along with FreeCAD. If not, see                                *
 *   <https://www.gnu.org/licenses/>.                                       *
 *                                                                          *
 ***************************************************************************/

#ifndef FREECAD_START_RECENTFILESMODEL_H
#define FREECAD_START_RECENTFILESMODEL_H

#include <memory>

#include <QAbstractListModel>
#include <Base/Parameter.h>

#include "DisplayedFilesModel.h"
#include "../StartGlobal.h"

namespace Start
{

/// A model for displaying a list of files including a thumbnail or icon, plus various file
/// statistics.
class StartExport RecentFilesModel: public DisplayedFilesModel
{
    Q_OBJECT
public:
    explicit RecentFilesModel(QObject* parent = nullptr);
    ~RecentFilesModel() override;

    RecentFilesModel(const RecentFilesModel&) = delete;
    RecentFilesModel(RecentFilesModel&&) = delete;
    RecentFilesModel& operator=(const RecentFilesModel&) = delete;
    RecentFilesModel& operator=(RecentFilesModel&&) = delete;

    void loadRecentFiles();

private:
    /// Opening or saving a document rewrites the MRU list while the Start view is still
    /// alive - it is created once and reused - so the list has to follow the parameter
    /// rather than be read once at construction.
    class Observer;

    Base::Reference<ParameterGrp> _parameterGroup;
    std::unique_ptr<Observer> _observer;
};

}  // namespace Start

#endif  // FREECAD_START_RECENTFILESMODEL_H
