/***************************************************************************
 *   Copyright (c) 2026 Zheng, Lei <realthunder.dev@gmail.com>              *
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

#include "PreCompiled.h"

#include <set>
#include <vector>

#include <Base/Exception.h>

#include "AppearanceUpdater.h"
#include "Document.h"
#include "DocumentObject.h"
#include "DocumentObserver.h"
#include "GeoFeature.h"

using namespace App;

namespace
{
/// How many scopes are open; -1 while the objects are being told
int _Counter;
std::set<DocumentObjectT> _Changed;
}  // namespace

AppearanceUpdater::AppearanceUpdater()
{
    if (_Counter >= 0) {
        ++_Counter;
    }
}

AppearanceUpdater::~AppearanceUpdater()
{
    if (_Counter <= 0 || --_Counter > 0 || _Changed.empty()) {
        return;
    }
    // What an object told does to its own looks is not told again: the
    // walk below reaches everything that depends on it already
    _Counter = -1;
    try {
        std::set<DocumentObject *> inset;
        for (const auto &objT : _Changed) {
            if (auto obj = objT.getObject()) {
                if (!obj->isRecomputing() && !obj->isTouched()) {
                    obj->getInListEx(inset, true);
                }
            }
        }
        std::vector<DocumentObject *> objs(inset.begin(), inset.end());
        for (auto obj : Document::getDependencyList(objs, Document::DepSort)) {
            if (obj->isRecomputing() || obj->isTouched() || !inset.count(obj)) {
                continue;
            }
            if (auto geo = Base::freecad_dynamic_cast<GeoFeature>(obj)) {
                geo->onSourceAppearanceChanged();
            }
        }
    }
    catch (Base::Exception &e) {
        e.ReportException();
    }
    catch (...) {
    }
    _Changed.clear();
    _Counter = 0;
}

void AppearanceUpdater::addObject(DocumentObject *obj)
{
    // A document read has every object's looks in it, made when it was
    // written: nothing is handed on, and the sort of a whole document's
    // dependencies for each object is what a large one cannot pay
    if (Document::isAnyRestoring()) {
        return;
    }
    if (_Counter && obj && !obj->isRecomputing() && !obj->isTouched()) {
        _Changed.emplace(obj);
    }
}
