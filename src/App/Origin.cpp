/***************************************************************************
 *   Copyright (c) 2015 Stefan Tröger <stefantroeger@gmx.net>              *
 *   Copyright (c) 2015 Alexander Golubev (Fat-Zer) <fatzer2@gmail.com>    *
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

#ifndef _PreComp_
# include <string>
#endif

#include <App/Document.h>

#include "Origin.h"

#include "GroupParams.h"

using namespace App;

PROPERTY_SOURCE(App::Origin, App::LocalCoordinateSystem)

Origin::Origin()
{
    // An Origin is a LCS whose placement is fixed at the identity.
    Placement.setStatus(Property::Hidden, true);
    Placement.setStatus(Property::ReadOnly, true);

    Visibility.setValue(false);
}

Origin::~Origin() = default;

void Origin::onDocumentRestored()
{
    LocalCoordinateSystem::onDocumentRestored();

    // Placements of origins restored from older documents are read only too.
    Placement.setStatus(Property::ReadOnly, true);
}

App::DocumentObjectExecReturn* Origin::execute()
{
    return DocumentObject::execute();
}

App::Property* Origin::getPropertyByName(const char* name) const
{
    if (getDocument() && !getDocument()->testStatus(App::Document::Restoring)
            && name && strcmp(name, OriginFeatures.getName()) == 0)
    {
        initObjects();
        return &const_cast<Origin*>(this)->OriginFeatures;
    }
    return LocalCoordinateSystem::getPropertyByName(name);
}

void Origin::setupObject()
{
    // Not LocalCoordinateSystem::setupObject(), which would create the
    // features unconditionally: here the preference decides, and anything
    // that later asks for them creates them then.
    GeoFeature::setupObject();
    if (GroupParams::getCreateOrigin()) {
        initObjects();
    }
}

bool Origin::canSaveExtension(Extension *ext) const
{
    // Disabled for now, as a mistake has been made several weeks ago causing
    // restore error. If it is really necessary to enable extension saving on
    // Origin, remove this override and Restore() below.
    if (ext)
        return ext != &extension;
    return foreachExtension<Extension>(
                [this](Extension *ext) {
                    return ext != &this->extension;
                });
}

void Origin::Restore(Base::XMLReader &reader)
{
    // Skips ExtensionContainer::Restore, and so the dynamic-extension
    // element, to match what canSaveExtension() refuses to write.
    return App::PropertyContainer::Restore(reader);
}
