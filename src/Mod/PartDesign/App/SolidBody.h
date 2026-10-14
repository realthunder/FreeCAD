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

#include <string>

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
 * body's coordinates, so Body.Solid.Face1 refers to the solid whichever
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
    /// The feature the solid was last seen through, if not its tip
    App::PropertyLinkHidden _Shown;
    /// Whether the solid was last seen through this object's own shape
    App::PropertyBool _ShownShape;

    App::DocumentObjectExecReturn *execute() override;
    short mustExecute() const override;
    void onChanged(const App::Property* prop) override;
    void onDocumentRestored() override;

    const char* getViewProviderName() const override {
        return "PartDesignGui::ViewProviderSolidBody";
    }

    App::DocumentObject *getSubObject(const char *subname, PyObject **pyObj,
            Base::Matrix4D *pmat, bool transform, int depth) const override;

    /// The body this solid belongs to, or null
    Body *getBody() const;

    /// Whether \a obj is one of the solid's features
    bool hasFeature(const App::DocumentObject *obj) const;

    /** @name What the solid is seen through
     *
     * A solid is drawn by one of its features, or by this object's own
     * shape. Hidden, it remembers which, and is shown through that again.
     */
    //@{
    /// Whether a feature of the solid, or this object, is visible
    bool isSolidVisible() const;
    /// What a hidden solid is shown through: what it was last seen through,
    /// else its tip
    App::DocumentObject *getShowTarget() const;
    /// To be called once \a obj, a feature of the solid or this object, is
    /// hidden. If that hid the solid, \a obj is what it was last seen through.
    void rememberShown(const App::DocumentObject *obj);
    //@}

    /// Set Shape from Tip; what execute() does
    App::DocumentObjectExecReturn *updateShape();

    /** Keep a reference to this object out of the history of its own solid
     *
     * This object has the shape of its tip, so whatever the tip is made of
     * cannot refer to it: the sketch of a feature attached to
     * Body.Solid.Face1 makes the solid depend on itself once the tip is at
     * that feature or past it. What is meant is the solid as that feature
     * finds it, which is the feature before it. Where that is the feature
     * this object has the shape of -- the reference was picked on that
     * shape, element for element -- the reference is turned into one to that
     * feature. Elsewhere the names are those of a later shape, and each is
     * turned into the element of the feature before that it comes from, by
     * the history of the element: a face the later features left alone, or
     * made their own version of. A reference to the whole object needs no
     * name. Where an element comes from nothing the feature before has --
     * the feature itself made it, or a later one -- the reference is left as
     * it is and said to be wrong, as the recompute would only say that the
     * document has a cycle.
     *
     * Called when the tip moves, and when a link of anything in the body
     * changes.
     */
    void keepOutOfItself();

    /** What keepOutOfItself() makes of a reference to the element \a sub of
     * this object that \a user holds
     *
     * This object, where \a user is nothing the solid is made of up to its
     * tip: the reference stays. Else the feature before the one \a user is
     * part of, with \a sub turned into the element of that feature. Null
     * where the element comes from nothing that feature has, which is the
     * reference that is left and said to be wrong. For whoever takes a pick
     * for \a user, to refuse that last kind and no other.
     */
    App::DocumentObject *referenceFrom(const App::DocumentObject *user, std::string &sub) const;

private:
    /// Whether somebody gave the faces, the lines or the points a look (it is
    /// not the material card's, nor the preference's)
    bool isLookGiven() const;
    /** Give the features what is the solid's
     *
     * The material card to every one of them: a solid is of one material.
     * The own look of the faces, of the lines and of the points, each if
     * somebody gave this object one, to each feature nobody gave one
     * (PartDesign::Feature::followSolidBodyLook()).
     */
    void handOn(bool card, bool look);

    /// isLookGiven() as it last was: a look taken away again is handed on too
    bool lookGiven = false;
    /// The feature Shape was last taken from, by name
    std::string shapeOf;
    /// What keepOutOfItself() last said is wrong, not to say it again
    std::string reported;
};

} //namespace PartDesign

#endif // PARTDESIGN_SolidBody_H
