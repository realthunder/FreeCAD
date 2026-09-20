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

#ifndef APP_Origin_H
#define APP_Origin_H

#include "Datums.h"

namespace App
{

/** The local coordinate system a document, Part or Body is built on.
 *
 * An Origin is a LocalCoordinateSystem pinned to the identity placement.
 * Everything it offers -- the axes, the planes, the origin point, the
 * OriginFeatures property and the role-prefix subname lookup -- lives on
 * that base; what stays here is what this fork does differently:
 *
 *  - the datum features are created LAZILY, so an Origin that nothing ever
 *    asks about costs nothing. getPropertyByName() is the second trigger,
 *    for code that reaches OriginFeatures directly rather than by getter.
 *  - the held group extension is not saved (canSaveExtension/Restore).
 */
class AppExport Origin : public App::LocalCoordinateSystem
{
    PROPERTY_HEADER_WITH_OVERRIDE(App::Origin);
    Q_DECLARE_TR_FUNCTIONS(App::Origin)

public:
    /// Constructor
    Origin();
    ~Origin() override;

    /// returns the type name of the ViewProvider
    const char* getViewProviderName() const override {
        return "Gui::ViewProviderCoordinateSystem";
    }

    bool isOrigin() const override {
        return true;
    }

    /** Materializes the datum features when OriginFeatures is asked for.
     *
     * Fork-local, and the reason the laziness is invisible to callers:
     * code that reads the property directly rather than through a getter
     * still sees a populated Origin.
     */
    Property* getPropertyByName(const char* name) const override;

    /// Kept spelling of LocalCoordinateSystem::getDatumElement
    App::DatumElement* getOriginFeature(const char* role) const {
        return getDatumElement(role);
    }

    void onDocumentRestored() override;

protected:
    /** Does NOT check that the datum features exist.
     *
     * LocalCoordinateSystem::execute() reaches every getter to assert the
     * set is complete, and each getter materializes. That would create the
     * features on the first recompute of every document, which is exactly
     * what the laziness here is for.
     */
    App::DocumentObjectExecReturn* execute() override;

    /** Creates the features only if the preference asks for it.
     *
     * Deliberately does not chain to LocalCoordinateSystem::setupObject(),
     * which creates them unconditionally.
     */
    void setupObject() override;

    bool canSaveExtension(Extension*) const override;
    void Restore(Base::XMLReader& reader) override;
};

} //namespace App

#endif // APP_Origin_H
