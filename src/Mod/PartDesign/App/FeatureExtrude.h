/***************************************************************************
 *   Copyright (c) 2020 Werner Mayer <wmayer[at]users.sourceforge.net>     *
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


#ifndef PARTDESIGN_FEATURE_EXTRUDE_H
#define PARTDESIGN_FEATURE_EXTRUDE_H

#include <App/PropertyStandard.h>
#include <App/PropertyUnits.h>
#include <Base/Bitmask.h>
#include "FeatureSketchBased.h"

class gp_Dir;
class TopoDS_Face;
class TopoDS_Shape;

namespace PartDesign
{

class PartDesignExport FeatureExtrude : public ProfileBased
{
    PROPERTY_HEADER_WITH_OVERRIDE(PartDesign::FeatureExtrude);

public:
    FeatureExtrude();

    /// "One side", "Two sides" or "Symmetric" (upstream a346c266e7). The
    /// ProfileBased Midplane stays as an alias of Symmetric, and Type
    /// "TwoLengths" becomes Two sides with both sides Length.
    App::PropertyEnumeration SideType;
    App::PropertyEnumeration Type;
    /// The type of the second side, for SideType "Two sides"
    App::PropertyEnumeration Type2;
    App::PropertyLength      Length;
    App::PropertyLength      Length2;
    App::PropertyAngle       TaperAngle;
    App::PropertyAngle       TaperAngle2; // for upstream compatibility
    App::PropertyAngle       TaperAngleRev;
    App::PropertyBool        UseCustomVector;
    App::PropertyVector      Direction;
    App::PropertyBool        AlongSketchNormal;
    /// Where the extrusion starts: see ProfileBased::StartTypesEnums
    /// (upstream bcc3e296fa, under its names)
    App::PropertyEnumeration StartType;
    App::PropertyLength      StartOffset;
    App::PropertyLinkSub     StartReference;
    App::PropertyLength      Offset;
    App::PropertyLength      Offset2;
    /// The second side's UpToFace and UpToShape, synced the same way
    App::PropertyLinkSub     UpToFace2;
    App::PropertyLinkSubList UpToShape2;
    App::PropertyLinkSub     ReferenceAxis;
    App::PropertyAngle       TaperInnerAngle;
    App::PropertyAngle       TaperInnerAngleRev;
    App::PropertyBool        AutoTaperInnerAngle;
    App::PropertyBool        UsePipeForDraft;
    App::PropertyBool        CheckUpToFaceLimits;

    static App::PropertyQuantityConstraint::Constraints signedLengthConstraint;
    static double maxAngle;
    static App::PropertyAngle::Constraints floatAngle;
    static const char* SideTypeEnums[];

    /** @name methods override feature */
    //@{
    short mustExecute() const override;
    void setupObject() override;
    //@}

    /** Whether an up-to reference is a single face: one object with one
     * element, or a whole object of one face (a datum plane, say).
     *
     * UpToShape is the reference the panel edits; UpToFace mirrors it when it
     * is a single face, and Type is then written as UpToFace, so that a file
     * of a single up-to face reads as before -- in older builds and upstream,
     * too. Anything else is Type UpToShape, and UpToFace is cleared.
     */
    static bool isSingleUpToFace(const App::PropertyLinkSubList &shape);

    /** How far along the extrusion the profile moves to start where
     * StartType says, in global coordinates; throws if the reference
     * cannot be met. For the panel and its gizmo.
     */
    double getStartOffset() const;

protected:
    /// The start offset of \a profile, extruded along \a direction; both
    /// in the frame \a invObjLoc takes the reference into
    double startOffset(const TopoShape &profile, const gp_Dir &direction,
                       const TopLoc_Location &invObjLoc) const;
    /// The direction the start moves along: the extrusion's, reversed
    /// with it except for a symmetric one, which Reversed does not turn
    gp_Dir startDirection(const Base::Vector3d &direction) const;

    void initProperties(const char *group);

    void onChanged(const App::Property *) override;
    void onDocumentRestored() override;
    void handleChangedPropertyName(Base::XMLReader &reader, const char * TypeName, const char *Name) override;

    /// Mirror one side's UpToFace into its UpToShape
    void syncUpToShape(const App::PropertyLinkSub &face, App::PropertyLinkSubList &shape);
    /// Mirror one side's UpToShape into its UpToFace, and set its Type by it
    void syncUpToFace(const App::PropertyLinkSubList &shape, App::PropertyLinkSub &face,
                      App::PropertyEnumeration &type);

    Base::Vector3d computeDirection(const Base::Vector3d& sketchVector, bool inverse);
    bool hasTaperedAngle() const;

    /// Options for buildExtrusion()
    enum class ExtrudeOption {
        MakeFace = 1,
        MakeFuse = 2,
        LegacyPocket = 4,
        InverseDirection = 8,
    };

    using ExtrudeOptions = Base::Flags<ExtrudeOption>;

    App::DocumentObjectExecReturn *buildExtrusion(ExtrudeOptions options);

    /**
      * Generates an untapered extrusion of the input sketchshape and stores it
      * in the given \a prism: L along the direction, and L2 against it for two
      * sides, or L centred on the sketch for a symmetric one
      */
    void generatePrism(TopoShape& prism,
                       TopoShape sketchshape,
                       const gp_Dir& direction,
                       const double L,
                       const double L2,
                       const bool twoSides,
                       const bool midplane,
                       const bool reversed);

    /// The up-to references of one side
    struct UpToSide {
        std::string method;
        App::PropertyLinkSub &upToFace;
        App::PropertyLinkSubList &upToShape;
        double offset;
    };

    /**
      * The shape to extrude up to, for one side: the face, the faces or the
      * shape, moved into the feature's placement, offset for a single face.
      * Returns the number of faces (0: the base).
      */
    int getUpToShape(TopoShape &upToShape,
                     const UpToSide &side,
                     const TopoShape &base,
                     const TopoShape &sketchshape,
                     const TopLoc_Location &invObjLoc,
                     gp_Dir &dir);

    /// One side extruded along \a dir, by a length or up to a shape
    TopoShape makeSide(const UpToSide &side,
                       const TopoShape &base,
                       const TopoShape &sketchshape,
                       const TopoShape &supportface,
                       const TopLoc_Location &invObjLoc,
                       gp_Dir dir,
                       double length,
                       double taperAngle,
                       double innerTaperAngle,
                       bool makeface);

    /**
      * Disables settings that are not valid for the current method
      */
    void updateProperties();

private:
    bool syncingSides = false;
};

} //namespace PartDesign

ENABLE_BITMASK_OPERATORS(PartDesign::FeatureExtrude::ExtrudeOption)

#endif // PARTDESIGN_FEATURE_EXTRUDE_H
