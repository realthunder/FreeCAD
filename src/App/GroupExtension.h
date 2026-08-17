/***************************************************************************
 *   Copyright (c) 2006 Werner Mayer <wmayer[at]users.sourceforge.net>     *
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


#ifndef APP_GROUPEXTENSION_H
#define APP_GROUPEXTENSION_H

#include <App/DocumentObject.h>
#include <App/DocumentObjectExtension.h>
#include <App/ExtensionPython.h>
#include <type_traits>
#include <unordered_map>
#include <vector>


namespace App
{
class DocumentObjectGroup;
class GroupExtensionPy;

class AppExport GroupExtension : public DocumentObjectExtension
{
    EXTENSION_PROPERTY_HEADER_WITH_OVERRIDE(App::GroupExtension);
    using inherited = DocumentObjectExtension;

public:
    /// Constructor
    GroupExtension();
    ~GroupExtension() override;

    /** @name Object handling  */
    //@{
    /** Adds an object of \a sType with \a pObjectName to the document this group belongs to and
     * append it to this group as well.
     */
    virtual DocumentObject *addObject(const char* sType, const char* pObjectName);
    /** The typed form of the call above; the type name comes from T's own
     * registration. A call without an explicit T cannot pick this overload,
     * so existing calls are unaffected.
     */
    template<typename T>
    T* addObject(const char* pObjectName);
    /* Adds the object \a obj to this group. Returns all objects that have been added.
     */
    virtual std::vector<DocumentObject*> addObject(DocumentObject* obj);
    /* Adds the objects \a objs to this group. Returns all objects that have been added.
     */
    virtual std::vector<DocumentObject*> addObjects(std::vector<DocumentObject*> obj);
    
    /* Sets the objects in this group. Everything contained already will be removed first
     */
    virtual std::vector< DocumentObject* > setObjects(std::vector< DocumentObject* > obj);    
    
    /*override this function if you want only special objects
     */
    virtual bool allowObject(DocumentObject* ) {return true;}
    
    /** Removes an object from this group. Returns all objects that have been removed.
     */
    virtual std::vector<DocumentObject*> removeObject(DocumentObject* obj);
    /** Removes objects from this group. Returns all objects that have been removed.
     */
    virtual std::vector<DocumentObject*> removeObjects(std::vector<DocumentObject*> obj);
    /** Removes all children objects from this group and the document.
     */
    virtual void removeObjectsFromDocument();
    /** Returns the object of this group with \a Name. If the group doesn't have such an object 0 is returned.
     * @note This method might return 0 even if the document this group belongs to contains an object with this name.
     */
    DocumentObject *getObject(const char* Name) const;
    /**
     * Checks whether the object \a obj is part of this group.
     * @param obj        the object to check for.
     * @param recursive  if true check also if the obj is child of some sub group (default is false).
     */
    virtual bool hasObject(const DocumentObject* obj, bool recursive=false) const;
    /**
     * Checks whether this group object is a child (or sub-child if enabled)
     * of the given group object.
     */
    bool isChildOf(const GroupExtension* group, bool recursive = true) const;
    /** Returns a list of all objects this group does have.
     */
    const std::vector<DocumentObject*> &getObjects() const;
    /** Returns a list of all objects of \a typeId this group does have.
     */
    std::vector<DocumentObject*> getObjectsOfType(const Base::Type& typeId) const;
    /** Returns the number of objects of \a typeId this group does have.
     */
    int countObjectsOfType(const Base::Type& typeId) const;
    /** Returns the object group of the document which the given object \a obj is part of.
     * In case this object is not part of a group 0 is returned. 
     * @note This only returns objects that are normal groups, not any special derived type 
     * like GeoFeatureGroups or OriginGroups. To retrieve those please use their appropriate functions
     */
    static DocumentObject* getGroupOfObject(const DocumentObject* obj);
    /** Returns any group that contains GroupExtension that owns a given object.
     * If both a non geo group and a geo group contains the given object, this
     * function will return the non geo group.
     */
    static DocumentObject* getAnyGroupOfObject(const DocumentObject* obj);
    //@}
    
    PyObject* getExtensionPyObject() override;

    void extensionOnChanged(const Property* p) override;

    bool extensionGetSubObject(DocumentObject *&ret, const char *subname,
        PyObject **pyObj, Base::Matrix4D *mat, bool transform, int depth) const override;

    bool extensionGetSubObjects(std::vector<std::string> &ret, int reason) const override;

    void enableSelectionSubObjects(bool enable) {
        _enableSubObjects = enable;
    }

    int extensionIsElementVisible(const char *element) const override;

    int extensionIsElementVisibleEx(const char *element, int reason) const override;

    int extensionSetElementVisible(const char *element, bool vis) override;

    void onExtendedDocumentRestored() override;

    App::DocumentObjectExecReturn *extensionExecute() override;

    virtual void onExtendedSetupObject() override;

    virtual std::vector<App::DocumentObject *> getFullModel () const {return Group.getValues();}

    std::vector<DocumentObject*> getAllChildren() const;
    void getAllChildren(std::vector<DocumentObject*> &, std::set<DocumentObject*> &) const;

    void checkParentGroup();

    /// Properties
    PropertyLinkList Group;
    PropertyBool _GroupTouched;
    PropertyInteger _GroupVersion;
    PropertyLinkList _ExportChildren;
    PropertyBool ClaimAllChildren;

    enum ExportModeValue {
        ExportDisabled,
        ExportByVisibility,
        ExportByChildQuery,
        ExportBoth,
    };
    PropertyEnumeration ExportMode;

    /// Helper class to temporary enable old group visibility toggling behavior
    struct AppExport ToggleNestedVisibility {
        ToggleNestedVisibility();
        ~ ToggleNestedVisibility();
    };

    /** Whether the '_GroupTouched' notification being emitted right now was
     * caused solely by a child's visibility change.
     *
     * Visibility does not affect which object claims which child, so a
     * listener that only cares about the claiming structure (the view
     * provider rebuilding '_ExportChildren') can skip the work.
     *
     * This holds for ExportByVisibility too. '_ExportChildren' is only a
     * membership list -- which children the group exposes as sub-objects.
     * Everything that cares about visibility reads it live off the child
     * instead: the shape gather behind getSubObjects(GS_DEFAULT) tests
     * Visibility as it walks (Part::Feature, _getTopoShape), which is how a
     * boolean taking this group as a tool picks up a hidden child at
     * recompute. So visibility still has to touch the group -- it just must
     * not rebuild the membership list.
     */
    static bool isVisibilityOnlyTouch();

    /** Scope guard marking a '_GroupTouched' touch as visibility-only.
     *
     * Holds a depth counter rather than a flag, so that the notification
     * cascading up through nested groups stays marked for the whole chain.
     */
    struct AppExport VisibilityOnlyTouch {
        explicit VisibilityOnlyTouch(bool active = true);
        ~VisibilityOnlyTouch();
    private:
        bool active;
    };

    /** Return the link list property for holding the children for export
     * @param reason: specify the reason for export. @sa App::DocumentObject::GSReason
     */
    virtual const PropertyLinkList& getExportGroupProperty(int reason) const {
        (void)reason;
        return Group;
    }

    virtual bool getChildDefaultExport(App::DocumentObject *obj, int reason) const;
    
    bool queryChildExport(App::DocumentObject *obj, int reason) const;
    bool toggleChildExport(App::DocumentObject *obj, bool toggleGroup = true);
    static PropertyBool *getChildExportProperty(App::DocumentObject *obj,
                                                bool force = false,
                                                bool defvalue = false);

protected:
    void initSetup();
    /** Bring the child signal connections in line with the Group property
     *
     * @param exportModeChanged: whether the sync was triggered by a change of
     * ExportMode, in which case the export query is redone for every child and
     * not just for the newly added ones.
     *
     * Only the difference is applied, because this runs on every single change
     * of Group: reconnecting all children here would make filling a group
     * quadratic in the number of children.
     */
    void syncChildConnections(bool exportModeChanged = false);

private:
    void removeObjectFromDocument(DocumentObject*);
    // This function stores the already searched objects to prevent infinite recursion in case of a cyclic group graph
    // It throws an exception of type Base::RuntimeError if a cyclic dependency is detected.
    bool recursiveHasObject(const DocumentObject* obj, const GroupExtension* group, std::vector<const GroupExtension*> history) const;

    // for tracking children visibility
    void slotChildChanged(const App::Property&);

    struct ChildConnections {
        boost::signals2::scoped_connection visibility;
        boost::signals2::scoped_connection groupTouched;
        // Marks the entry as seen by the running sync, so that children gone
        // from Group can be swept without building a second lookup structure.
        unsigned long stamp = 0;
    };
    std::unordered_map<const App::DocumentObject*, ChildConnections> _Conns;
    unsigned long _ConnStamp = 0;

    bool _togglingVisibility = false;

    bool _enableSubObjects = true;
};

template<typename T>
T* GroupExtension::addObject(const char* pObjectName)
{
    static_assert(std::is_base_of_v<DocumentObject, T>,
                  "T must be derived from App::DocumentObject");
    return static_cast<T*>(addObject(T::getClassTypeId().getName(), pObjectName));
}


template<typename ExtensionT>
class GroupExtensionPythonT : public ExtensionT {
         
public:
    
    GroupExtensionPythonT() = default;
    ~GroupExtensionPythonT() override = default;
 
    //override the documentobjectextension functions to make them available in python 
    bool allowObject(DocumentObject* obj)  override {
        Py::Object pyobj = Py::asObject(obj->getPyObject());
        EXTENSION_PROXY_ONEARG(allowObject, pyobj);
                
        if(result.isNone())
            return ExtensionT::allowObject(obj);
        
        if(result.isBoolean())
            return result.isTrue();
        
        return false;
    };
};

using GroupExtensionPython = ExtensionPythonT<GroupExtensionPythonT<GroupExtension>>;

} //namespace App


#endif // APP_GROUPEXTENSION_H
