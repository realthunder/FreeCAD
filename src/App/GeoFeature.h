/***************************************************************************
 *   Copyright (c) 2002 Jürgen Riegel <juergen.riegel@web.de>              *
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


#ifndef APP_GEOFEATURE_H
#define APP_GEOFEATURE_H

#include <memory>
#include "DocumentObject.h"
#include "MappedElement.h"
#include "MaterialAppearance.h"
#include "PropertyGeo.h"
#include "ComplexGeoData.h"


namespace App
{

class PropertyXLinkSub;

/** Base class of all geometric document objects.
 */
class PropertyLinkBase;

class AppExport GeoFeature : public App::DocumentObject
{
    PROPERTY_HEADER_WITH_OVERRIDE(App::GeoFeature);

public:
    PropertyPlacement Placement;
    PropertyString _ElementMapVersion;

    /// Constructor
    GeoFeature();
    ~GeoFeature() override;

    /**
     * @brief transformPlacement applies transform to placement of this shape.
     * Override this function to propagate the change of placement to base
     * features, for example. By the time of writing this comment, the function
     * was only called by alignment task (Edit->Alignment)
     * @param transform (input).
     */
    virtual void transformPlacement(const Base::Placement &transform);
    /**
     * This method returns the main property of a geometric object that holds
     * the actual geometry. For a part object this is the Shape property, for
     * a mesh object the Mesh property and so on.
     * The default implementation returns null.
     */
    virtual const PropertyComplexGeoData* getPropertyOfGeometry() const;
    /**
     * @brief getPyObject returns the Python binding object
     * @return the Python binding object
     */
    PyObject* getPyObject() override;

    /// Specify the type of element name to return when calling getElementName() 
    enum ElementNameType {
        /// Normal usage
        Normal=0,
        /// For importing
        Import=1,
        /// For exporting
        Export=2,
    };
    /** Return the new and old style sub-element name
     *
     * @param name: input name
     * @param type: desired element name type to return
     *
     * This function relies on ComplexGeoData::elementMapPrefix() to decide
     * whether it is a forward query, i.e. mapped -> original, or reverse
     * query.  The reason being that, unlike ComplexGeoData who deals with the
     * actual element map data, GeoFeature here sits at a higher level.
     * GeoFeature should be dealing with whatever various PropertyLinkSub(s) is
     * assigned.
     *
     * This function is made virtual, so that inherited class can do something
     * unusual, such as Sketcher::SketcherObject, which uses this to expose its
     * private geometries without a corresponding TopoShape, and yet being
     * source code compatible.
     */
    virtual std::pair<std::string,std::string> getElementName(
            const char *name, ElementNameType type=Normal) const;

    /** Resolve both the new and old style element name
     *
     * @param obj: top parent object
     * @param subname: subname reference 
     * @param elementName: output of a pair(newElementName,oldElementName)
     * @param append: Whether to include subname prefix into the returned
     *                element name
     * @param type: the type of element name to request
     * @param filter: If none zero, then only perform lookup when the element
     *                owner object is the same as this filter
     * @param element: return the start of element name in subname
     *
     * @return Return the owner object of the element
     */
    static DocumentObject *resolveElement(App::DocumentObject *obj, 
            const char *subname, std::pair<std::string,std::string> &elementName, 
            bool append=false, ElementNameType type=Normal,
            const DocumentObject *filter=nullptr,const char **element=nullptr, GeoFeature **geo=nullptr);

    static bool hasMissingElement(const char *subname);

    /**
     * @brief Calculates the placement in the global reference coordinate system
     *
     * In FreeCAD the GeoFeature placement describes the local placement of the object in its parent
     * coordinate system. This is however not always the same as the global reference system. If the
     * object is in a GeoFeatureGroup, hence in another local coordinate system, the Placement
     * property does only give the local transformation. This function can be used to calculate the
     * placement of the object in the global reference coordinate system taking all stacked local
     * systems into account.
     *
     * This only accounts for geo feature groups. When the object is reached
     * through a link, use the getGlobalPlacement() overloads below, which take
     * the path the object was reached by.
     *
     * @return Base::Placement The transformation from the global reference coordinate system
     */
    Base::Placement globalPlacement() const;

    /// Return the value of a placement property of the given object, identity if there is none
    static Base::Placement getPlacementFromProp(DocumentObject *obj, const char *propName);

    /** Return the global placement of an object reached through a sub-object path
     *
     * @param targetObj: optional object along the path to stop at. If null, the
     *                   whole path is accumulated.
     * @param rootObj: the object the path starts from.
     * @param sub: the sub-object path, relative to \c rootObj.
     */
    static Base::Placement getGlobalPlacement(DocumentObject *targetObj,
                                              DocumentObject *rootObj,
                                              const std::string &sub);
    /// Same, taking the root object and path from a link property's first sub-value
    static Base::Placement getGlobalPlacement(DocumentObject *targetObj, PropertyXLinkSub *prop);
    /// Global placement of an object that is not reached through a link, i.e. globalPlacement()
    static Base::Placement getGlobalPlacement(const DocumentObject *obj);

    /** Search sub element using internal cached geometry
     *
     * @param element: element name
     * @param options: search options
     * @param tol: coordinate tolerance
     * @param atol: angle tolerance
     *
     * @return Returns a list of found element reference to the new goemetry.
     * The returned value will be invalidated when the geometry is changed.
     *
     * Before changing the property of geometry, GeoFeature will internally
     * make a snapshot of all referenced element geometry. After change, user
     * code may call this function to search for the new element name that
     * reference to the same geometry of the old element.
     */
    virtual const std::vector<std::string>& searchElementCache(const std::string &element,
                                                               Data::SearchOptions options = Data::SearchOption::CheckGeometry,
                                                               double tol = 1e-7,
                                                               double atol = 1e-10) const;

    /** Called when a link property stops holding element references into
     * this feature: it is being re-set, or destroyed with its owner.
     *
     * Whatever the feature retained for that referrer (searchElementCache)
     * may be let go of.  Only a notice: the property may register again a
     * moment later with new content, so the feature should re-evaluate at
     * its next safe point rather than act here.
     */
    virtual void onElementReferenceReleased(PropertyLinkBase *prop) { (void)prop; }


    /// Return the object that owns the shape that contains the give element name
    virtual DocumentObject *getElementOwner(const Data::MappedName & /*name*/) const
    {return nullptr;}

    virtual const std::vector<const char *>& getElementTypes(bool all=true) const;

    /// Return the higher level element names of the given element
    virtual std::vector<Data::IndexedName> getHigherElements(const char *name, bool silent=false) const;

    /** @brief Appearance of the feature's material, as an App::MaterialAppearance
     *
     * The material itself lives in the Materials module, which the Gui module
     * cannot reach directly. These two virtuals are the bridge: a feature that
     * carries a material card reports its appearance here, and the view
     * provider reads it from the App side without linking Materials.
     */
    virtual App::MaterialAppearance getMaterialAppearance() const;
    /// Set the feature's material appearance from an App::MaterialAppearance
    virtual void setMaterialAppearance(const App::MaterialAppearance& material);
    /** Render_* view properties the feature's material card states
     *
     * The third leg of the same bridge, for what App::MaterialAppearance cannot
     * carry: the media features are dynamic properties on the view
     * provider rather than fields of a material (see
     * App::MaterialRenderProperty). Empty unless the card states one.
     */
    virtual App::MaterialRenderProperties getMaterialRenderProperties() const;

protected:
    void onChanged(const Property* prop) override;
    void onDocumentRestored() override;
    void updateElementReference();
    std::pair<std::string,std::string> _getElementName(const char *name, const Data::MappedElement &mapped) const;

private:
    std::vector<Data::MappedElement> _elementMapCache;
    std::string _elementMapVersion;
};

} //namespace App

#endif // APP_GEOFEATURE_H
