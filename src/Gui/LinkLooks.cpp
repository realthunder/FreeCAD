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

#include <App/AppearanceList.h>
#include <App/DocumentObject.h>
#include <App/PropertyStandard.h>
#include <Base/Console.h>
#include <Base/Exception.h>
#include <Base/Tools.h>

#include "LinkLooks.h"

using namespace Gui;

namespace
{

using Store = App::PropertyElementAppearance;
using Looks = App::LinkAppearance;

bool sameLook(const App::MaterialAppearance &a, const App::MaterialAppearance &b)
{
    return Store::differingFields(a, b) == Store::OwnNone;
}

std::vector<App::MaterialAppearance> looksOf(const App::PropertyAppearanceList &list)
{
    std::vector<App::MaterialAppearance> res;
    res.reserve(static_cast<std::size_t>(list.getSize()));
    for (int i = 0; i < list.getSize(); ++i) {
        res.push_back(list.getMaterial(i));
    }
    return res;
}

}  // namespace

void LinkLooks::mark(bool on)
{
    for (App::Property *prop : std::initializer_list<App::Property *> {
             overrideMaterial, shapeAppearance, overrideColorList, materialList,
             overrideMaterialList}) {
        if (prop) {
            prop->setStatus(App::Property::Legacy, on);
        }
    }
    for (App::Property *prop : others) {
        if (prop) {
            prop->setStatus(App::Property::Legacy, on);
        }
    }
}

void LinkLooks::bind(const App::DocumentObject *obj, bool restored)
{
    if (isBound()) {
        return;
    }
    names = Looks::namesOf(obj);
    if (!names.store) {
        return;
    }
    mark(true);
    if (restored && !names.store->wasRestored()) {
        try {
            adopt();
        }
        catch (Base::Exception &e) {
            e.ReportException();
        }
    }
    mirror();
}

void LinkLooks::unbind()
{
    if (!isBound()) {
        return;
    }
    names = Looks::Names();
    mark(false);
}

void LinkLooks::adopt()
{
    Store &store = *names.store;
    Store::Edit edit(store);
    if (overrideMaterial && shapeAppearance) {
        Looks::setOverride(store, overrideMaterial->getValue(), shapeAppearance->getBase());
    }
    if (overrideColorList && names.colored) {
        Looks::setColored(store, names.colored->getSubValues(), overrideColorList->getValues());
    }
    if (materialList && overrideMaterialList) {
        Looks::setArrayLooks(store, looksOf(*materialList), overrideMaterialList->getValues());
    }
}

void LinkLooks::mirror()
{
    if (!isBound() || mirroring) {
        return;
    }
    Base::StateLocker lock(mirroring);
    const Store &store = *names.store;
    const bool on = Looks::hasOverride(store);
    // The look before the flag: what draws on the flag draws the look
    if (on && shapeAppearance) {
        const App::MaterialAppearance &look = store.getBase(Store::Face);
        if (shapeAppearance->getSize() < 1 || !sameLook(shapeAppearance->getBase(), look)) {
            shapeAppearance->setValue(look);
        }
    }
    if (overrideMaterial && overrideMaterial->getValue() != on) {
        overrideMaterial->setValue(on);
    }
    if (overrideColorList) {
        std::vector<std::string> subs;
        std::vector<App::Color> colors;
        Looks::getColored(store, subs, colors);
        if (overrideColorList->getValues() != colors) {
            overrideColorList->setValues(colors);
        }
    }
    if (materialList && overrideMaterialList && !writingArray) {
        std::vector<App::MaterialAppearance> looks;
        boost::dynamic_bitset<> given;
        Looks::getArrayLooks(store, looks, given);
        if (overrideMaterialList->getValues() != given) {
            overrideMaterialList->setValue(given);
        }
        const std::vector<App::MaterialAppearance> held = looksOf(*materialList);
        bool same = held.size() == looks.size();
        for (std::size_t i = 0; same && i < looks.size(); ++i) {
            same = sameLook(held[i], looks[i]);
        }
        if (!same) {
            materialList->setValue(looks);
        }
    }
}

void LinkLooks::updateData(const App::Property *prop)
{
    if (isBound() && prop == names.store) {
        mirror();
    }
}

void LinkLooks::onChanged(const App::Property *prop, bool restoring)
{
    if (!isBound() || mirroring || restoring || !prop) {
        return;
    }
    Store &store = *names.store;
    try {
        if (prop == overrideMaterial || prop == shapeAppearance) {
            if (overrideMaterial && shapeAppearance) {
                Looks::setOverride(store, overrideMaterial->getValue(),
                                   shapeAppearance->getBase());
            }
        }
        else if (prop == overrideColorList) {
            // The names are the object's, and were written before these
            std::vector<std::string> subs;
            std::vector<App::Color> colors;
            Looks::getColored(store, subs, colors);
            Looks::setColored(store, subs, overrideColorList->getValues());
        }
        else if (prop == materialList || prop == overrideMaterialList) {
            if (materialList && overrideMaterialList) {
                Base::StateLocker lock(writingArray);
                Looks::setArrayLooks(store, looksOf(*materialList),
                                     overrideMaterialList->getValues());
            }
        }
    }
    catch (Base::Exception &e) {
        e.ReportException();
    }
}
