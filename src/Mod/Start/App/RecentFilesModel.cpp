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

#include "PreCompiled.h"
#ifndef _PreComp_
#include <cstring>
#endif

#include "RecentFilesModel.h"
#include <App/Application.h>
#include <App/ProjectFile.h>

using namespace Start;

/// Reloads the model whenever the MRU list is rewritten, which is what opening or saving
/// a document does. Same shape as Gui::RecentFilesAction::Private.
class RecentFilesModel::Observer: public ParameterGrp::ObserverType
{
public:
    Observer(RecentFilesModel* master, Base::Reference<ParameterGrp> handle)
        : _master(master)
        , _handle(std::move(handle))
    {
        _handle->Attach(this);
    }

    ~Observer() override
    {
        _handle->Detach(this);
    }

    Observer(const Observer&) = delete;
    Observer(Observer&&) = delete;
    Observer& operator=(const Observer&) = delete;
    Observer& operator=(Observer&&) = delete;

    void OnChange(Base::Subject<const char*>& sub, const char* reason) override
    {
        Q_UNUSED(sub)
        // "RecentFiles" is the count, rewritten last when the list changes; the MRUn
        // entries themselves arrive one at a time and would reload once each.
        if (reason && strcmp(reason, "RecentFiles") == 0) {
            _master->loadRecentFiles();
        }
    }

private:
    RecentFilesModel* _master;
    Base::Reference<ParameterGrp> _handle;
};

RecentFilesModel::RecentFilesModel(QObject* parent)
    : DisplayedFilesModel(parent)
{
    _parameterGroup = App::GetApplication().GetParameterGroupByPath(
        "User parameter:BaseApp/Preferences/RecentFiles");
    _observer = std::make_unique<Observer>(this, _parameterGroup);
}

RecentFilesModel::~RecentFilesModel() = default;

void RecentFilesModel::loadRecentFiles()
{
    beginResetModel();
    clear();
    auto numRows {_parameterGroup->GetInt("RecentFiles", 0)};
    for (int i = 0; i < numRows; ++i) {
        auto entry = fmt::format("MRU{}", i);
        auto path = _parameterGroup->GetASCII(entry.c_str(), "");
        addFile(QString::fromStdString(path));
    }
    endResetModel();
}
