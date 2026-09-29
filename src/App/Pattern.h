// SPDX-License-Identifier: LGPL-2.1-or-later
/****************************************************************************
 *   Copyright (c) 2026 Zheng Lei <realthunder.dev@gmail.com>               *
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

#ifndef APP_PATTERN_H
#define APP_PATTERN_H

#include <memory>
#include <string>
#include <vector>

#include <Base/Placement.h>
#include <Base/Vector3D.h>
#include <FCGlobal.h>

namespace App
{

class DocumentObject;
class Property;
class PropertyContainer;

/** The pattern engine shared by App::LinkArray and the pattern features of
 * other modules (Part's pattern extensions, PartDesign's patterns)
 *
 * It knows no geometry. A pattern reads its inputs from the properties of the
 * object carrying it, by name -- upstream's names (Direction, Occurrences,
 * Axis, Path, ...), whether they are static members, extension members, or
 * the dynamic properties of an App::LinkArray -- and returns one placement per
 * occurrence, the first one normally the identity.
 *
 * What a reference means -- the direction of an edge, the normal of a face, a
 * path along some wires -- is answered by resolvers. App resolves its own
 * datums (App::Line, App::Plane, App::Point and the coordinate systems holding
 * them); a module with geometry registers a Resolver for the rest, as Part does
 * for shapes.
 */
class AppExport Pattern
{
public:
    /// The kinds of pattern, in the order of TypeEnums
    enum class Type
    {
        Linear,
        Polar,
        Circular,
        Path,
        Point,
    };
    static const char* TypeEnums[];

    /// A line resolved in the frame of the referenced object's parent
    struct Axis
    {
        Base::Vector3d base;
        Base::Vector3d direction;
    };

    /// A path, walked by arc length
    class AppExport Path
    {
    public:
        virtual ~Path() = default;
        virtual double length() const = 0;
        virtual bool isClosed() const = 0;
        /// The position and tangent at \a distance from the start of the path
        virtual void evaluate(double distance, Base::Vector3d& position, Base::Vector3d& tangent) const
            = 0;
    };

    /** Resolves references in the frame of the referenced object's parent
     *
     * Each function returns false (or null) for a reference it does not know,
     * so that the next resolver is asked, and throws for one it knows but
     * cannot use -- a curved edge as a direction, say.
     */
    class AppExport Resolver
    {
    public:
        virtual ~Resolver() = default;
        virtual bool getDirection(DocumentObject* obj, const std::string& sub, Base::Vector3d& dir) const;
        virtual bool getAxis(DocumentObject* obj, const std::string& sub, Axis& axis) const;
        virtual std::unique_ptr<Path> getPath(DocumentObject* obj,
                                              const std::vector<std::string>& subs) const;
        virtual bool getPoints(DocumentObject* obj,
                               const std::vector<std::string>& subs,
                               std::vector<Base::Vector3d>& points) const;
    };
    /// Resolvers are asked after App's own datums, the last one added first
    static void addResolver(std::shared_ptr<Resolver> resolver);
    static void removeResolver(const Resolver* resolver);

    static Base::Vector3d resolveDirection(DocumentObject* obj, const std::vector<std::string>& subs);
    static Axis resolveAxis(DocumentObject* obj, const std::vector<std::string>& subs);
    static std::unique_ptr<Path> resolvePath(DocumentObject* obj, const std::vector<std::string>& subs);
    static std::vector<Base::Vector3d> resolvePoints(DocumentObject* obj,
                                                     const std::vector<std::string>& subs);

    /// How the placements of a pattern are made
    struct Context
    {
        /** The placement of the patterned object in its parent's frame
         *
         * References are resolved in that frame and brought into the object's
         * own by its inverse, a linear direction or a polar axis alike.
         */
        Base::Placement placement;
        /** A linear direction without a reference is the local X axis (Y for
         * the second direction), a polar or circular axis the local Z axis.
         * Otherwise a missing reference is an error.
         */
        bool defaultReferences = false;
        /// A path or point pattern puts its first occurrence at the identity
        bool relativeToFirst = false;
    };

    /** The placements of a pattern of \a type read from \a obj, one per
     * occurrence
     *
     * A path or point pattern without a reference has none.
     */
    static std::vector<Base::Placement> getPlacements(Type type,
                                                      const PropertyContainer& obj,
                                                      const Context& context);

    /** The unit direction of a linear pattern, the second one if \a second,
     * in the object's own frame and turned round if it is reversed -- what
     * its steps are taken along. Throws as getPlacements() does for a
     * missing reference.
     */
    static Base::Vector3d getDirection(const PropertyContainer& obj,
                                       const Context& context,
                                       bool second = false);
    /// The axis of a polar or circular pattern, as getDirection() does it
    static Axis getAxis(const PropertyContainer& obj, const Context& context);

    /// A property of a pattern, as a pattern object carries it
    struct PropertySpec
    {
        const char* name;
        const char* type;
        const char* group;
        const char* doc;
    };
    static const std::vector<PropertySpec>& getPropertySpecs(Type type);
    static const PropertySpec* getPropertySpec(Type type, const char* name);

    /** Give a property just added its default value and its constraints
     *
     * @param name: the name of the property, when it cannot tell it yet, as
     * an extension's member in the extension's constructor
     */
    static void initProperty(Type type, Property* prop, const char* name = nullptr);
    /** Give the pattern properties of \a obj the constraints and enumerations
     * a file does not store, and the status their modes imply
     */
    static void setupProperties(Type type, PropertyContainer& obj);
    /** Keep the dependent inputs of a pattern in step after \a prop changed:
     * the read-only side of Length/Offset (Angle/Offset) by Mode, Length and
     * Offset with each other, Spacings with Occurrences.
     *
     * Not called while restoring.
     */
    static void onChanged(Type type, PropertyContainer& obj, const Property* prop);
    /// Whether an input of the pattern is touched
    static bool isTouched(Type type, const PropertyContainer& obj);
    /// Whether \a prop is one of the inputs of the pattern
    static bool isPatternProperty(Type type, const Property* prop);
    /** The property \a name of \a obj itself
     *
     * A link answers getPropertyByName() with the properties of the object
     * it links to too, which are not inputs of its pattern.
     */
    static Property* getProperty(const PropertyContainer& obj, const char* name);

    /** The gap before occurrence \a index + 1 of a direction in Spacing mode:
     * the individual spacing, else the spacing pattern when it has more than
     * one value, else the offset. A list shorter than the gaps reads -1 where
     * it is short.
     */
    static double getSpacing(const std::vector<double>& spacings,
                             const std::vector<double>& pattern,
                             double offset,
                             int index);

    /** The most gaps Spacings is grown to when Occurrences changes: the gaps
     * after it read -1 anyway, and Occurrences set to two billion made a list
     * of 16 GB on the spot.
     */
    static constexpr int MaxListedSpacings = 1000;
    /// The most occurrences a circular or path pattern generates by itself
    static constexpr int MaxGeneratedOccurrences = 10000;
};

}  // namespace App

#endif  // APP_PATTERN_H
