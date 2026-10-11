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

#include <cstring>

#include <App/DocumentObject.h>
#include <App/Property.h>

#include "AppearanceNames.h"
#include "ViewProviderDocumentObject.h"

namespace Gui
{

App::Property *appearanceProperty(const ViewProvider *view, const char *name)
{
    if (!view || !name) {
        return nullptr;
    }
    App::Property *mine = view->getPropertyByName(name);
    // A view provider that keeps a value of its own under the name -- one
    // that shows something other than its object's own shape -- is asked
    if (mine && !mine->testStatus(App::Property::Legacy)) {
        return mine;
    }
    auto vp = Base::freecad_dynamic_cast<const ViewProviderDocumentObject>(view);
    App::DocumentObject *obj = vp ? vp->getObject() : nullptr;
    if (obj && obj->isAttachedToDocument()) {
        // The object's own, and not what a link answers for what it shows
        App::Property *prop = obj->getPropertyByName(name);
        const char *group = prop && prop->getContainer() == obj ? prop->getGroup() : nullptr;
        if (group && std::strcmp(group, "Appearances") == 0) {
            return prop;
        }
    }
    return mine;
}

}  // namespace Gui
