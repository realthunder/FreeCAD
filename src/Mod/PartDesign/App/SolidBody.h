/****************************************************************************
 *   Copyright (c) 2026 Zheng Lei (realthunder) <realthunder.dev@gmail.com> *
 *                                                                          *
 *   This file is part of the FreeCAD CAx development system.               *
 *                                                                          *
 *   This library is free software; you can redistribute it and/or          *
 *   modify it under the terms of the GNU Library General Public            *
 *   License as published by the Free Software Foundation; either           *
 *   version 2 of the License, or (at your option) any later version.       *
 *                                                                          *
 *   This library  is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of         *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the          *
 *   GNU Library General Public License for more details.                   *
 *                                                                          *
 *   You should have received a copy of the GNU Library General Public      *
 *   License along with this library; see the file COPYING.LIB. If not,     *
 *   write to the Free Software Foundation, Inc., 59 Temple Place,          *
 *   Suite 330, Boston, MA  02111-1307, USA                                 *
 *                                                                          *
 ****************************************************************************/

#ifndef PARTDESIGN_SolidBody_H
#define PARTDESIGN_SolidBody_H

#include <App/PropertyLinks.h>
#include <Mod/Part/App/PartFeature.h>
#include <Mod/PartDesign/PartDesignGlobal.h>

namespace PartDesign
{
class Body;

/** One solid of a body
 *
 * A body holding more than one solid gives each of them a SolidBody: the
 * object that stays the same while the features making up the solid come and
 * go. It lists those features (Group) for the tree, names the one of them the
 * solid is rolled back to (Tip), and has the shape of that feature in the
 * body's coordinates, so Body.SolidBody.Face1 refers to the solid whichever
 * feature is its tip.
 *
 * It is a plain member of Body.Group and never a solid feature itself. The
 * body keeps it in step with the BaseFeature chains (Body::syncSolidBodies());
 * those chains stay the authoritative record of what belongs to which solid.
 *
 * Not to be confused with PartDesign::Solid, the piece a Split makes.
 */
class PartDesignExport SolidBody : public Part::Feature
{
    PROPERTY_HEADER_WITH_OVERRIDE(PartDesign::SolidBody);
    using inherited = Part::Feature;

public:
    SolidBody();

    /// The feature this solid is rolled back to: its shape is the solid's,
    /// and a new feature of the solid goes after it
    App::PropertyLink Tip;
    /// The features of the solid in history order, kept by the body
    App::PropertyLinkList Group;
    App::PropertyLinkHidden _Body;

    App::DocumentObjectExecReturn *execute() override;
    short mustExecute() const override;
    void onChanged(const App::Property* prop) override;

    const char* getViewProviderName() const override {
        return "PartDesignGui::ViewProviderSolidBody";
    }

    App::DocumentObject *getSubObject(const char *subname, PyObject **pyObj,
            Base::Matrix4D *pmat, bool transform, int depth) const override;

    /// The body this solid belongs to, or null
    Body *getBody() const;

    /// Whether \a obj is one of the solid's features
    bool hasFeature(const App::DocumentObject *obj) const;

    /// Set Shape from Tip; what execute() does
    App::DocumentObjectExecReturn *updateShape();
};

} //namespace PartDesign

#endif // PARTDESIGN_SolidBody_H
