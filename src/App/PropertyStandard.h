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


#ifndef APP_PROPERTYSTANDARD_H
#define APP_PROPERTYSTANDARD_H

#include <functional>
#include <list>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include <boost/dynamic_bitset.hpp>
#include <boost/filesystem/path.hpp>
#include <Base/Uuid.h>

#include "Property.h"
#include "AppearanceList.h"
#include "Enumeration.h"
#include "FileBlobManager.h"
#include "MaterialAppearance.h"


namespace Base {
class Writer;
}


namespace App
{

/** Integer properties
 * This is the father of all properties handling Integers.
 */
class AppExport PropertyInteger: public Property
{
    TYPESYSTEM_HEADER_WITH_OVERRIDE();

public:
    bool canShareDefault() const override { return true; }
    PropertyInteger();
    ~PropertyInteger() override;

    /** Sets the property
     */
    void setValue(long);

    /** This method returns a string representation of the property
     */
    long getValue() const;
    const char* getEditorName() const override { return "Gui::PropertyEditor::PropertyIntegerItem"; }

    PyObject *getPyObject() override;
    void setPyObject(PyObject *) override;

    void Save (Base::Writer &writer) const override;
    void Restore(Base::XMLReader &reader) override;

    Property *Copy() const override;
    void Paste(const Property &from) override;

    unsigned int getMemSize () const override{return sizeof(long);}

    void setPathValue(const App::ObjectIdentifier & path, const App::any & value) override;
    App::any getPathValue(const App::ObjectIdentifier & /*path*/) const  override { return _lValue; }

    bool isSame(const Property &other) const override;
    Property *copyBeforeChange() const override {return Copy();}

    void interpolate(const Property &from, const Property &to, float t) override;

protected:
    long _lValue;
};

/** Path properties
 * Properties handling file system paths.
 */
class AppExport PropertyPath: public Property
{
    TYPESYSTEM_HEADER_WITH_OVERRIDE();

public:

    PropertyPath();
    ~PropertyPath() override;

    /** Sets the property
     */
    void setValue(const boost::filesystem::path &);

    /** Sets the property
     */
    void setValue(const char *);

    /** This method returns a string representation of the property
     */
    const boost::filesystem::path &getValue() const;

    const char* getEditorName() const override { return "Gui::PropertyEditor::PropertyPathItem"; }

    PyObject *getPyObject() override;
    void setPyObject(PyObject *) override;

    void Save (Base::Writer &writer) const override;
    void Restore(Base::XMLReader &reader) override;

    Property *Copy() const override;
    void Paste(const Property &from) override;

    unsigned int getMemSize () const override;

    bool isSame(const Property &other) const override;
    Property *copyBeforeChange() const  override {return Copy();}

protected:
    boost::filesystem::path _cValue;
};

/// Property wrapper around an Enumeration object.
class AppExport PropertyEnumeration: public Property
{
    TYPESYSTEM_HEADER_WITH_OVERRIDE();

public:
    bool canShareDefault() const override { return true; }
    /// Standard constructor
    PropertyEnumeration();

    /// Obvious constructor
    explicit PropertyEnumeration(const Enumeration &e);

    /// destructor
    ~PropertyEnumeration() override;

    /// Enumeration methods
    /*!
     * These all function as per documentation in Enumeration
     */
    //@{
    /** setting the enumeration string list
     * The list is a NULL terminated array of pointers to a const char* string
     * \code
     * const char enums[] = {"Black","White","Other",NULL}
     * \endcode
     */
    void setEnums(const char** plEnums);

    /** setting the enumeration string as vector of strings
     * This makes the enumeration custom.
     */
    void setEnums(const std::vector<std::string> &Enums);

    /** set the enum by a string
     * is slower than setValue(long). Use long if possible
     */
    void setValue(const char* value);

    /** set directly the enum value
     * In DEBUG checks for boundaries.
     * Is faster than using setValue(const char*).
     */
    void setValue(long);

    /// Setter using Enumeration
    void setValue(const Enumeration &source);

    /// Returns current value of the enumeration as an integer
    long getValue() const;

    /// checks if the property is set to a certain string value
    bool isValue(const char* value) const;

    /// checks if a string is included in the enumeration
    bool isPartOf(const char* value) const;

    /// get the value as string
    const char * getValueAsString() const;

    /// Returns Enumeration object
    const Enumeration &getEnum() const;

    /// get all possible enum values as vector of strings
    std::vector<std::string> getEnumVector() const;

    /// set enum values as vector of strings
    void setEnumVector(const std::vector<std::string> &);

    /// Set whether to persist custom enum values while saving
    void setPersistEnums(bool enable);

    /// Whether the enum values is persisted when saved to file
    bool getPersistEnums() const {return persistEnums;}

    /// get the pointer to the enum list
    bool hasEnums() const;

    /// Returns true if the instance is in a usable state
    bool isValid() const;
    //@}

    const char* getEditorName() const override { return _editorTypeName.c_str(); }
    void setEditorName(const char* name) { _editorTypeName = name; }

    PyObject * getPyObject() override;
    void setPyObject(PyObject *) override;

    void Save(Base::Writer &writer) const override;
    void Restore(Base::XMLReader &reader) override;

    Property * Copy() const override;
    void Paste(const Property &from) override;

    void setPathValue(const App::ObjectIdentifier & path, const App::any & value) override;
    bool setPyPathValue(const App::ObjectIdentifier & path, const Py::Object &value) override;
    App::any getPathValue(const App::ObjectIdentifier &path) const override;
    bool getPyPathValue(const ObjectIdentifier &path, Py::Object &r) const override;

    bool isSame(const Property &other) const override;
    Property *copyBeforeChange() const override {return Copy();}

private:
    Enumeration _enum;
    std::string _editorTypeName;
    bool persistEnums = true;
};

/** Constraint integer properties
 * This property fulfills the need of a constraint integer. It holds basically a
 * state (integer) and a struct of boundaries. If the boundaries
 * is not set it acts basically like an IntegerProperty and does no checking.
 * The constraints struct can be created on the heap or build in.
 */
class AppExport PropertyIntegerConstraint: public PropertyInteger
{
    TYPESYSTEM_HEADER_WITH_OVERRIDE();

public:
    /// Standard constructor
    PropertyIntegerConstraint();

    /// destructor
    ~PropertyIntegerConstraint() override;

    /// Constraint methods
    //@{
    /// the boundary struct
    struct Constraints {
        long LowerBound, UpperBound, StepSize;
        Constraints()
            : LowerBound(0)
            , UpperBound(0)
            , StepSize(0)
            , candelete(false)
        {
        }
        Constraints(long l, long u, long s)
            : LowerBound(l)
            , UpperBound(u)
            , StepSize(s)
            , candelete(false)
        {
        }
        ~Constraints() = default;
        void setDeletable(bool on)
        {
            candelete = on;
        }
        bool isDeletable() const
        {
            return candelete;
        }

        bool operator==(const Constraints &other) const
        {
            return LowerBound == other.LowerBound
                && UpperBound == other.UpperBound
                && StepSize == other.StepSize;
        }
    private:
        bool candelete;
    };
    /** setting the boundaries
     * This sets the constraint struct. It can be dynamically
     * allocated or set as a static in the class the property
     * belongs to:
     * \code
     * const Constraints percent = {0,100,1}
     * \endcode
     */
    void setConstraints(const Constraints* sConstraint);
    /// get the constraint struct
    const Constraints*  getConstraints() const;
    //@}

    long getMinimum() const;
    long getMaximum() const;
    long getStepSize() const;

    const char* getEditorName() const override { return "Gui::PropertyEditor::PropertyIntegerConstraintItem"; }
    void setPyObject(PyObject *) override;

    Property *Copy(void) const override;
    void Paste(const Property &from) override;

    bool isSame(const Property &other) const override;

protected:
    const Constraints* _ConstStruct{nullptr};
};

/** Percent property
 * This property is a special integer property and holds only
 * numbers between 0 and 100.
 */

class AppExport PropertyPercent: public PropertyIntegerConstraint
{
    TYPESYSTEM_HEADER_WITH_OVERRIDE();

public:
    /// Standard constructor
    PropertyPercent();

    /// destructor
    ~PropertyPercent() override;
};

/** Integer list properties
 *
 */
class AppExport PropertyIntegerList: public PropertyListsT<long>
{
    TYPESYSTEM_HEADER_WITH_OVERRIDE();

public:
    bool canShareDefault() const override { return true; }
    /**

     * A constructor.
     * A more elaborate description of the constructor.
     */
    PropertyIntegerList();

    /**
     * A destructor.
     * A more elaborate description of the destructor.
     */
    ~PropertyIntegerList() override;

    const char* getEditorName() const override
    { return "Gui::PropertyEditor::PropertyIntegerListItem"; }

    PyObject *getPyObject(void) override;
    
    Property *Copy(void) const override;
    void Paste(const Property &from) override;

    void interpolateValue(int index, const long &from, const long &to, float t) override;

protected:
    long getPyValue(PyObject *item) const override;

    void restoreXML(Base::XMLReader &) override;
    bool saveXML(Base::Writer &) const override;
};

/** Integer list properties
 *
 */
class AppExport PropertyIntegerSet: public Property
{
    TYPESYSTEM_HEADER_WITH_OVERRIDE();

public:
    /**

     * A constructor.
     * A more elaborate description of the constructor.
     */
    PropertyIntegerSet();

    /**
     * A destructor.
     * A more elaborate description of the destructor.
     */
    ~PropertyIntegerSet() override;

    /** Sets the property
     */
    void setValue(long);
    void setValue(){;}

    void addValue (long value){_lValueSet.insert(value);}
    void setValues (const std::set<long>& values);

    const std::set<long> &getValues() const{return _lValueSet;}

    PyObject *getPyObject() override;
    void setPyObject(PyObject *) override;

    void Save (Base::Writer &writer) const override;
    void Restore(Base::XMLReader &reader) override;

    Property *Copy() const override;
    void Paste(const Property &from) override;
    unsigned int getMemSize () const override;

    bool isSame(const Property &other) const override;
    Property *copyBeforeChange() const  override {return Copy();}

private:
    std::set<long> _lValueSet;
};


/** implements a key/value list as property
 *  The key ought to be ASCII the Value should be treated as UTF8 to be saved.
 */
class AppExport PropertyMap: public Property
{
    TYPESYSTEM_HEADER_WITH_OVERRIDE();

public:

    /**
     * A constructor.
     * A more elaborate description of the constructor.
     */
    PropertyMap();

    /**
     * A destructor.
     * A more elaborate description of the destructor.
     */
    ~PropertyMap() override;

    virtual int getSize() const;

    /** Sets the property
     */
    void setValue(){}
    void setValue(const std::string& key,const std::string& value);
    void setValue(const char *key,const char *value);
    void setValues(const std::map<std::string,std::string>&);
    void setValues(std::map<std::string,std::string>&&);
    
    /// index operator
    const std::string& operator[] (const std::string& key) const ;
    
    const std::map<std::string,std::string> &getValues() const {return _lValueList;}
    const char *getValue(const char *key) const;

    void set1Value (const std::string& key, const std::string& value) {setValue(key, value);}

    //virtual const char* getEditorName(void) const { return "Gui::PropertyEditor::PropertyStringListItem"; }

    PyObject *getPyObject() override;
    void setPyObject(PyObject *) override;

    void Save (Base::Writer &writer) const override;
    void Restore(Base::XMLReader &reader) override;

    unsigned int getMemSize (void) const override;
    
    bool isSame(const Property &other) const override;
    Property *copyBeforeChange() const override {return Copy();}

    Property *Copy() const override;
    void Paste(const Property &from) override;

private:
    std::map<std::string,std::string> _lValueList;
};



/** Float properties
 * This is the father of all properties handling floats.
 * Use this type only in rare cases. Mostly you want to
 * use the more specialized types like e.g. PropertyLength.
 * These properties also fulfill the needs of the unit system.
 * See PropertyUnits.h for all properties with units.
 */
class AppExport PropertyFloat: public Property
{
    TYPESYSTEM_HEADER_WITH_OVERRIDE();

public:
    bool canShareDefault() const override { return true; }
    /** Value Constructor
     *  Construct with explicit Values
     */
    PropertyFloat();

    /**
     * A destructor.
     * A more elaborate description of the destructor.
     */
    ~PropertyFloat() override;


    void setValue(double lValue);
    double getValue() const;

    const char* getEditorName() const override { return "Gui::PropertyEditor::PropertyFloatItem"; }

    PyObject *getPyObject() override;
    void setPyObject(PyObject *) override;

    void Save (Base::Writer &writer) const override;
    void Restore(Base::XMLReader &reader) override;

    Property *Copy() const override;
    void Paste(const Property &from) override;

    unsigned int getMemSize (void) const override {return sizeof(double);}
    
    void setPathValue(const App::ObjectIdentifier &path, const App::any &value) override;
    App::any getPathValue(const App::ObjectIdentifier &path) const override;

    bool isSame(const Property &other) const override;
    Property *copyBeforeChange() const override {return Copy();}

    void interpolate(const Property &from, const Property &to, float t) override;

protected:
    double _dValue;
};

/** Constraint float properties
 * This property fulfills the need of a constraint float. It holds basically a
 * state (float) and a struct of boundaries. If the boundaries
 * is not set it acts basically like a PropertyFloat and does no checking
 * The constraints struct can be created on the heap or built-in.
 */
class AppExport PropertyFloatConstraint: public PropertyFloat
{
    TYPESYSTEM_HEADER_WITH_OVERRIDE();

public:

    /** Value Constructor
     *  Construct with explicit Values
     */
    PropertyFloatConstraint();

    /**
     * A destructor.
     * A more elaborate description of the destructor.
     */
    ~PropertyFloatConstraint() override;


    /// Constraint methods
    //@{
    /// the boundary struct
    struct Constraints {
        double LowerBound, UpperBound, StepSize;
        Constraints()
            : LowerBound(0)
            , UpperBound(0)
            , StepSize(0)
            , candelete(false)
        {
        }
        Constraints(double l, double u, double s)
            : LowerBound(l)
            , UpperBound(u)
            , StepSize(s)
            , candelete(false)
        {
        }
        ~Constraints() = default;
        void setDeletable(bool on)
        {
            candelete = on;
        }
        bool isDeletable() const
        {
            return candelete;
        }
        bool operator==(const Constraints &other) const
        {
            return LowerBound == other.LowerBound
                && UpperBound == other.UpperBound
                && StepSize == other.StepSize;
        }
    private:
        bool candelete;
    };
    /** setting the boundaries
     * This sets the constraint struct. It can be dynamically
     * allocated or set as an static in the class the property
     * belongs to:
     * \code
     * const Constraints percent = {0.0,100.0,1.0}
     * \endcode
     */
    void setConstraints(const Constraints* sConstrain);
    /// get the constraint struct
    const Constraints*  getConstraints() const;
    //@}

    double getMinimum() const;
    double getMaximum() const;
    double getStepSize() const;

    const char* getEditorName() const override
    { return "Gui::PropertyEditor::PropertyFloatConstraintItem"; }

    void setPyObject(PyObject *) override;

    Property *Copy(void) const override;
    void Paste(const Property &from) override;

    bool isSame(const Property &other) const override;

protected:
    const Constraints* _ConstStruct{nullptr};
};


/** Precision properties
 * This property fulfills the need of a floating value with many decimal points,
 * e.g. for holding values like Precision::Confusion(). The value has a default
 * constraint for non-negative, but can be overridden
 */
class AppExport PropertyPrecision: public PropertyFloatConstraint
{
    TYPESYSTEM_HEADER_WITH_OVERRIDE();
public:
    PropertyPrecision();
    ~PropertyPrecision() override;
    const char* getEditorName() const override
    { return "Gui::PropertyEditor::PropertyPrecisionItem"; }
};


/// Double precision float list
class AppExport PropertyFloatList: public PropertyListsT<double>
{
    TYPESYSTEM_HEADER_WITH_OVERRIDE();

public:
    bool canShareDefault() const override { return true; }

    /**
     * A constructor.
     * A more elaborate description of the constructor.
     */
    PropertyFloatList();

    /**
     * A destructor.
     * A more elaborate description of the destructor.
     */
    ~PropertyFloatList() override;

    const char* getEditorName() const override
    { return "Gui::PropertyEditor::PropertyFloatListItem"; }

    PyObject *getPyObject(void) override;
    
    Property *Copy(void) const override;
    void Paste(const Property &from) override;

    void interpolateValue(int index, const double &from, const double &to, float t) override;

protected:
    double getPyValue(PyObject *item) const override;

    void restoreXML(Base::XMLReader &) override;
    bool saveXML(Base::Writer &) const override;
    bool canSaveStream(Base::Writer &) const override { return true; }
    void restoreStream(Base::InputStream &s, unsigned count) override;
    void saveStream(Base::OutputStream &) const override;
};

/** Single precision float list
 *
 * This property is not exposed to FreeCAD type system. It is meant to be
 * derived by other property classes for backward compatibility.
 */
class AppExport _PropertyFloatList: public PropertyListsT<float>
{
public:
    PyObject *getPyObject(void) override;

    Property *Copy(void) const override;
    void Paste(const Property &from) override;

    void interpolateValue(int index, const float &from, const float &to, float t) override;

protected:
    float getPyValue(PyObject *item) const override;

    void restoreXML(Base::XMLReader &) override;
    bool saveXML(Base::Writer &) const override;
    bool canSaveStream(Base::Writer &) const override { return true; }
    void restoreStream(Base::InputStream &s, unsigned count) override;
    void saveStream(Base::OutputStream &) const override;

    const char *xmlName() const override { return "FloatList"; }
};

/** String properties
 * This is the father of all properties handling Strings.
 */
class AppExport PropertyString: public Property
{
    TYPESYSTEM_HEADER_WITH_OVERRIDE();

public:
    bool canShareDefault() const override { return true; }

    /**
     * A constructor.
     * A more elaborate description of the constructor.
     */
    PropertyString();

    /**
     * A destructor.
     * A more elaborate description of the destructor.
     */
    ~PropertyString() override;

    virtual void setValue(const char* sString);
    void setValue(const std::string &sString);
    const char* getValue() const;
    const std::string& getStrValue() const
    { return _cValue; }
    bool isEmpty(){return _cValue.empty();}

    const char* getEditorName() const override { return "Gui::PropertyEditor::PropertyStringItem"; }
    PyObject *getPyObject() override;
    void setPyObject(PyObject *) override;

    void Save (Base::Writer &writer) const override;
    void Restore(Base::XMLReader &reader) override;

    Property *Copy() const override;
    void Paste(const Property &from) override;
    unsigned int getMemSize () const override;

    void setPathValue(const App::ObjectIdentifier &path, const App::any &value) override;
    App::any getPathValue(const App::ObjectIdentifier &path) const override;

    bool isSame(const Property &other) const override;
    Property *copyBeforeChange() const override {return Copy();}

protected:
    std::string _cValue;
};

/** UUID properties
 * This property handles unique identifiers
 */
class AppExport PropertyUUID: public Property
{
    TYPESYSTEM_HEADER_WITH_OVERRIDE();

public:

    /**
     * A constructor.
     * A more elaborate description of the constructor.
     */
    PropertyUUID();

    /**
     * A destructor.
     * A more elaborate description of the destructor.
     */
    ~PropertyUUID() override;


    void setValue(const Base::Uuid &);
    void setValue(const char* sString);
    void setValue(const std::string &sString);
    const std::string& getValueStr() const;
    const Base::Uuid& getValue() const;

    //virtual const char* getEditorName(void) const { return "Gui::PropertyEditor::PropertyStringItem"; }
    PyObject *getPyObject() override;
    void setPyObject(PyObject *) override;

    void Save (Base::Writer &writer) const override;
    void Restore(Base::XMLReader &reader) override;

    Property *Copy() const override;
    void Paste(const Property &from) override;
    unsigned int getMemSize () const override;

    bool isSame(const Property &other) const override;
    Property *copyBeforeChange() const  override {return Copy();}

private:
    Base::Uuid _uuid;
};


/** Property handling with font names.
 */
class AppExport PropertyFont : public PropertyString
{
    TYPESYSTEM_HEADER_WITH_OVERRIDE();

public:
    PropertyFont();
    ~PropertyFont() override;
    const char* getEditorName() const override
    { return "Gui::PropertyEditor::PropertyFontItem"; }

    bool isSame(const Property &other) const override {
        if (&other == this)
            return true;
        return getTypeId() == other.getTypeId()
            && getValue() == static_cast<decltype(this)>(&other)->getValue();
    }
};

class AppExport PropertyStringList: public PropertyListsT<std::string>
{
    TYPESYSTEM_HEADER_WITH_OVERRIDE();
    using inherited = PropertyListsT<std::string>;

public:
    bool canShareDefault() const override { return true; }

    /**
     * A constructor.
     * A more elaborate description of the constructor.
     */
    PropertyStringList();

    /**
     * A destructor.
     * A more elaborate description of the destructor.
     */
    ~PropertyStringList() override;

    void setValues(const std::list<std::string>&);
    using inherited::setValues;

    const char* getEditorName() const override
    { return "Gui::PropertyEditor::PropertyStringListItem"; }

    PyObject *getPyObject() override;

    Property *Copy() const override;
    void Paste(const Property &from) override;

    unsigned int getMemSize () const override;

protected:
    std::string getPyValue(PyObject *item) const override;

    void restoreXML(Base::XMLReader &) override;
    bool saveXML(Base::Writer &) const override;
};

/** Bool properties
 * This is the father of all properties handling booleans.
 */
class AppExport PropertyBool : public Property
{
    TYPESYSTEM_HEADER_WITH_OVERRIDE();

public:
    bool canShareDefault() const override { return true; }

    /**
     * A constructor.
     * A more elaborate description of the constructor.
     */
    PropertyBool();

    /**
     * A destructor.
     * A more elaborate description of the destructor.
     */
    ~PropertyBool() override;

    void setValue(bool lValue);
    bool getValue() const;

    const char* getEditorName() const override { return "Gui::PropertyEditor::PropertyBoolItem"; }

    PyObject *getPyObject() override;
    void setPyObject(PyObject *) override;

    void Save (Base::Writer &writer) const override;
    void Restore(Base::XMLReader &reader) override;

    Property *Copy() const override;
    void Paste(const Property &from) override;

    unsigned int getMemSize () const override{return sizeof(bool);}

    void setPathValue(const App::ObjectIdentifier &path, const App::any &value) override;
    App::any getPathValue(const App::ObjectIdentifier &path) const override;

    bool isSame(const Property &other) const override;
    Property *copyBeforeChange() const override {return Copy();}

private:
    bool _lValue;
};

/** Bool list properties
 *
 */
class AppExport PropertyBoolList : public PropertyListsT<bool,boost::dynamic_bitset<> >
{
    TYPESYSTEM_HEADER_WITH_OVERRIDE();
    using inherited = PropertyListsT<bool, boost::dynamic_bitset<> >;

public:
    bool canShareDefault() const override { return true; }
    PropertyBoolList();
    ~PropertyBoolList() override;

    PyObject *getPyObject() override;
    void setPyObject(PyObject *) override;

    void Save (Base::Writer &writer) const override;
    void Restore(Base::XMLReader &reader) override;

    Property *Copy() const override;
    void Paste(const Property &from) override;
    unsigned int getMemSize () const override;

protected:
    bool getPyValue(PyObject *) const override;
};


/** Color properties
 * This is the father of all properties handling colors.
 */
class AppExport PropertyColor : public Property
{
    TYPESYSTEM_HEADER_WITH_OVERRIDE();

public:
    bool canShareDefault() const override { return true; }
    /**
     * A constructor.
     * A more elaborate description of the constructor.
     */
    PropertyColor();

    /**
     * A destructor.
     * A more elaborate description of the destructor.
     */
    ~PropertyColor() override;

    /** Sets the property
     */
    void setValue(const Color &col);
    /// Default alpha 1: a colour set without one is opaque (Base/Color.h)
    void setValue(float r, float g, float b, float a=1.0f);
    void setValue(uint32_t rgba);

    /** This method returns a string representation of the property
     */
    const Color &getValue() const;

    const char* getEditorName() const override { return "Gui::PropertyEditor::PropertyColorItem"; }

    PyObject *getPyObject() override;
    void setPyObject(PyObject *) override;

    void Save (Base::Writer &writer) const override;
    void Restore(Base::XMLReader &reader) override;

    
    bool isSame(const Property &other) const override;
    Property *copyBeforeChange() const override {return Copy();}

    Property *Copy() const override;
    void Paste(const Property &from) override;

    unsigned int getMemSize () const override{return sizeof(Color);}
    
    void interpolate(const Property &from, const Property &to, float t) override;

private:
    Color _cCol;
};

class AppExport PropertyColorList: public PropertyListsT<Color>
{
    TYPESYSTEM_HEADER_WITH_OVERRIDE();

public:
    bool canShareDefault() const override { return true; }

    /**
     * A constructor.
     * A more elaborate description of the constructor.
     */
    PropertyColorList();

    /**
     * A destructor.
     * A more elaborate description of the destructor.
     */
    ~PropertyColorList() override;

    /** The colours, which a subclass may keep somewhere else
     *
     * Virtual, where the same name on PropertyListsT is not, because a method
     * of THIS class that reads the list has no way to know whether the
     * property owns its storage. One subclass does not: the Part view
     * provider's DiffuseColor is a name over a ShapeAppearance's diffuse
     * field and holds nothing of its own. RestoreDocFile below reads and
     * writes through this pair, so while the name was merely hidden it read
     * the empty base vector and then stored that emptiness back -- silently
     * emptying the face colours of every document it converted.
     *
     * Only the reads that go through here are covered. PropertyListsT binds
     * its own getValues() statically and touches _lValueList directly in
     * operator[], set1Value and setSize, so a redirecting subclass still has
     * to override each of those; PropertyDiffuseColor does.
     *
     * Deliberately not virtual on PropertyListsT itself: eleven other list
     * properties share that template and none of them has, or is likely to
     * get, a subclass that redirects.
     */
    virtual const std::vector<Color> &getValues() const {
        return PropertyListsT<Color>::getValues();
    }

    PyObject *getPyObject(void) override;

    Property *Copy(void) const override;
    void Paste(const Property &from) override;

    void interpolateValue(int index, const Color &from, const Color &to, float t) override;

    /// Converts the alpha component of a document that means opacity by it;
    /// see Base::alphaIsOpacity.
    void RestoreDocFile(Base::Reader &reader) override;

protected:
    Color getPyValue(PyObject *) const override;

    void restoreXML(Base::XMLReader &) override;
    bool saveXML(Base::Writer &) const override;
    bool canSaveStream(Base::Writer &) const override { return true; }
    void restoreStream(Base::InputStream &s, unsigned count) override;
    void saveStream(Base::OutputStream &) const override;
};

/** One appearance: the look of a thing, not the material it is made of
 *
 * Holds an App::MaterialAppearance -- ambient, diffuse, specular and
 * emissive colour, shininess, transparency. Was App::PropertyMaterial,
 * which collided with Materials::PropertyMaterial (the material CARD)
 * and with Mesh::PropertyMaterial, three unrelated things under one
 * name. The former type name is still resolved, so documents and macros
 * that say App::PropertyMaterial keep working; see
 * Application::initTypes. The saved XML element is still
 * <PropertyMaterial> -- that is the file format and does not move.
 */
class AppExport PropertyAppearance : public Property
{
    TYPESYSTEM_HEADER_WITH_OVERRIDE();

public:
    bool canShareDefault() const override { return true; }

    /**
     * A constructor.
     * A more elaborate description of the constructor.
     */
    PropertyAppearance();

    /**
     * A destructor.
     * A more elaborate description of the destructor.
     */
    ~PropertyAppearance() override;

    /** Sets the property
     */
    void setValue(const MaterialAppearance &mat);
    void setAmbientColor(const Color& col);
    void setDiffuseColor(const Color& col);
    void setSpecularColor(const Color& col);
    void setEmissiveColor(const Color& col);
    void setShininess(float);
    void setTransparency(float);

    /** This method returns a string representation of the property
     */
    const MaterialAppearance &getValue() const;

    PyObject *getPyObject() override;
    void setPyObject(PyObject *) override;

    void Save (Base::Writer &writer) const override;
    void Restore(Base::XMLReader &reader) override;

    const char* getEditorName() const override;

    Property *Copy() const override;
    void Paste(const Property &from) override;

    bool isSame(const Property &other) const override;
    Property *copyBeforeChange() const override {return Copy();}

    unsigned int getMemSize () const override{return sizeof(_cMat);}

private:
    MaterialAppearance _cMat;
};

/** A list of materials: a base, and the faces that override it
 *
 * A material list almost never varies in every field, and where it varies
 * at all it usually varies over a few faces of many. An imported solid is a
 * body colour with some pads on it; an object with a uniform appearance is
 * one material. Storing whole materials makes every entry pay for that: an
 * App::MaterialAppearance is 80 bytes against a colour's 16, so a 10,000 face import
 * spends 800 KB saying what a base and twenty overrides would have said.
 *
 * So the storage is (docs/ShapeAppearanceDesign.md 12):
 *
 *   - the BASE, one whole material -- what the object looks like where no
 *     face says otherwise;
 *   - a sorted vector of OVERRIDING face indices;
 *   - one array per field, at length 0 or |overrides|, where an empty
 *     array says every override takes the base's value for that field.
 *
 * getSize() is the logical entry count and is held separately, so a list of
 * ten thousand identical materials is one material. Reading an entry
 * composes it, which is why getValues() is not offered and operator[]
 * returns by value; prefer the per field accessors, which read and write
 * the storage directly.
 *
 * Dropping an override that no longer differs, and emptying an array that
 * says nothing, is not merely an optimisation. The shared-default scheme
 * elides a property whose serialisation is byte-identical to its class
 * default, so two appearances that are equal but serialise differently
 * would silently fail to elide. Writing is therefore always from the
 * normalised form; reading may assume it.
 *
 * A list restored from an encoding that states one entry at a time has no
 * base in it. One is DERIVED once, from the mirror or the largest face
 * area, and stored (12.4); until then every entry is an override, which
 * resolves correctly and costs what the dense form cost.
 */
class MaterialListPy;

class AppExport PropertyAppearanceList : public PropertyLists,
                                       public BlobReferrerProperty,
                                       public AtomicPropertyChangeInterface<PropertyAppearanceList>
{
    TYPESYSTEM_HEADER_WITH_OVERRIDE();

public:
    using atomic_change = AtomicPropertyChangeInterface<PropertyAppearanceList>::AtomicPropertyChange;
    friend atomic_change;
    /// Reads the value in place; every write it makes goes through
    /// editList(), so nothing bypasses the change signalling
    friend class MaterialListPy;

    bool canShareDefault() const override { return true; }

    /** The saved XML element, frozen at what the type used to be called
     *
     * PropertyLists derives the element from the live type name, so the
     * rename to PropertyAppearanceList would have written <AppearanceList>
     * and then searched every existing document for it -- and found
     * <MaterialList>, restoring the appearance EMPTY with one console line
     * to show for it. The element is the file format, not the type name;
     * the same decision froze <PropertyMaterial> on PropertyAppearance.
     */
    const char *xmlName() const override { return "MaterialList"; }

    PropertyAppearanceList();
    ~PropertyAppearanceList() override;

    /// The material every entry of an empty field reads as
    static const MaterialAppearance &defaultMaterial();

    /** @name The Python view of this property
     *
     * getPyObject() hands out a MaterialListPy that is a LIVE VIEW: it
     * reads this property's value and writes through editList(), so
     * vp.ShapeAppearance[0].DiffuseColor = c reaches the object. The views
     * register here so that this property's death turns them into plain
     * values rather than dangling pointers.
     */
    //@{
    void registerView(MaterialListPy *view);
    void unregisterView(MaterialListPy *view);
    /// Run a write against the value, recording and signalling it if it
    /// changed.  touched is the entry a per entry write names.
    void editList(const std::function<void(AppearanceList &)> &op, int touched = -1);
    //@}

    /** @name The value this property holds
     *
     * Copying it costs a pointer, so handing it to Python, snapshotting it
     * for undo and assigning one property to another are all O(1); the
     * first write through any holder pays for the storage.
     */
    //@{
    const AppearanceList &getList() const { return _list; }
    void setList(const AppearanceList &list);
    //@}

    /** @name Whole material access
     *
     * The interface a material list has always had. Each of these composes
     * or decomposes materials across the field arrays.
     */
    //@{
    int getSize() const override { return _list.getSize(); }
    void setSize(int newSize) override;
    void setSize(int newSize, const MaterialAppearance &def);

    void setValue(const MaterialAppearance &mat);
    void setValue(const std::vector<MaterialAppearance> &values = std::vector<MaterialAppearance>()) {
        setValues(values);
    }
    void setValues(const std::vector<MaterialAppearance> &values);
    void setValues(std::vector<MaterialAppearance> &&values);

    /** There is deliberately no getValues()
     *
     * Every other list property hands back its storage; this one has no
     * whole material to hand back, and the three ways of pretending
     * otherwise are all worse than not offering it. A member cache would
     * undo the layout -- the first caller grows a 10,000 entry list from
     * 200 KB to a megabyte and keeps it there until the next write. A shared
     * scratch buffer, the FC_STATIC idiom used elsewhere in this class of
     * problem, makes `a.getValues() == b.getValues()` quietly compare one
     * list with itself. Returning by value is safe but silently expensive at
     * exactly the call sites that look cheapest.
     *
     * So the whole-list read is gone and the compiler says so. Read one
     * entry with getMaterial(), or -- better -- read the one field you
     * wanted through the per field accessors below, which touch no memory
     * that is not already there.
     */
    MaterialAppearance operator[](int idx) const { return getMaterial(idx); }
    MaterialAppearance getMaterial(int idx) const;
    void set1Value(int idx, const MaterialAppearance &mat);
    /// upstream's spelling of set1Value, so their call sites port unchanged
    void setValue(int idx, const MaterialAppearance &mat) { set1Value(idx, mat); }
    //@}

    /** @name The base entry and the overriding faces
     *
     * What the object looks like, and which faces hold their own
     * (docs/ShapeAppearanceDesign.md 12). A whole-object write moves the
     * base and leaves the overriding faces alone; a per-face write makes a
     * face an override or drops it back.
     */
    //@{
    const MaterialAppearance &getBase() const { return _list.getBase(); }
    void setBase(const MaterialAppearance &mat);
    const std::vector<uint32_t> &getOverrides() const { return _list.getOverrides(); }
    bool hasOverrides() const { return _list.hasOverrides(); }
    bool isOverride(int idx) const { return _list.isOverride(idx); }
    void clearOverrides();
    void clearOverride(int idx);
    /// Whether a base has been chosen, or the list is still every entry
    /// standing for itself over a default one
    bool hasDerivedBase() const { return _list.hasDerivedBase(); }
    /** Choose the base for a list that arrived without one, once
     *
     * \a hint is the mirror -- the view provider's ShapeColor, which wins
     * if the list holds it -- and \a weights the per face area. Both may be
     * null. See docs/ShapeAppearanceDesign.md 12.4, and note that it
     * records no change: the entries resolve to exactly what they did.
     */
    void deriveBase(const Color *hint = nullptr,
                    const std::vector<double> *weights = nullptr) const
    { _list.deriveBase(hint, weights); }
    /// Whether any entry wears exactly this diffuse colour -- the mirror
    /// rule's question, asked before the face areas are worth computing
    bool namesDiffuse(const Color &color) const { return _list.namesDiffuse(color); }
    //@}

    /** @name Following the object's material card
     *
     * One flag for the whole list (docs/MaterialStorage.md 15.3). While it
     * is set, the BASE is the card's look and is re-taken whenever the card
     * changes; the overriding faces are re-applied over it, so a following
     * object keeps its painted faces. Any whole-object write ends the
     * follow, which is what makes a look the user chose outrank the card's.
     *
     * This property knows nothing about material cards: the view provider
     * reads the card and calls followMaterial().
     */
    //@{
    bool isFollowingMaterial() const { return _list.isFollowingMaterial(); }
    void setFollowMaterial(bool enable);
    /// The card's look as the base, without ending the follow
    void followMaterial(const MaterialAppearance &card);
    //@}

    /** @name Per field access, as it is stored
     *
     * The overriding faces' values, in overrides order, at length 0 or
     * |getOverrides()| -- an empty array says every override takes the
     * base's value for that field, so `!getDiffuseOverrides().empty()` is
     * the question "does the colour vary per face". Do NOT index one of
     * these by a face number; use the indexed getters, or the resolved
     * getters below.
     */
    //@{
    const std::vector<Color> &getAmbientOverrides() const
    { return _list.getAmbientOverrides(); }
    const std::vector<Color> &getDiffuseOverrides() const
    { return _list.getDiffuseOverrides(); }
    const std::vector<Color> &getSpecularOverrides() const
    { return _list.getSpecularOverrides(); }
    const std::vector<Color> &getEmissiveOverrides() const
    { return _list.getEmissiveOverrides(); }
    const std::vector<float> &getShininessOverrides() const
    { return _list.getShininessOverrides(); }
    const std::vector<std::string> &getImageOverrides() const
    { return _list.getImageOverrides(); }
    const std::vector<std::string> &getImagePathOverrides() const
    { return _list.getImagePathOverrides(); }
    const std::vector<std::string> &getUuidOverrides() const
    { return _list.getUuidOverrides(); }
    const std::vector<std::string> &getMaterialXOverrides() const
    { return _list.getMaterialXOverrides(); }
    const std::vector<SurfaceFinish> &getFinishOverrides() const
    { return _list.getFinishOverrides(); }

    bool variesInAmbient() const { return _list.variesInAmbient(); }
    bool variesInDiffuse() const { return _list.variesInDiffuse(); }
    bool variesInSpecular() const { return _list.variesInSpecular(); }
    bool variesInEmissive() const { return _list.variesInEmissive(); }
    bool variesInShininess() const { return _list.variesInShininess(); }
    bool variesInImage() const { return _list.variesInImage(); }
    bool variesInUuid() const { return _list.variesInUuid(); }
    bool variesInMaterialX() const { return _list.variesInMaterialX(); }
    bool variesInFinish() const { return _list.variesInFinish(); }
    bool variesInTexture() const { return _list.variesInTexture(); }
    //@}

    /** @name Per field access, resolved
     *
     * getSize() entries, BUILT ON READ out of the base and the overrides:
     * a fresh vector every time, so ask once and never in a loop. There is
     * deliberately no getTransparencies(): a transparency is the complement
     * of the diffuse alpha, so read getTransparency(i) or the colours.
     */
    //@{
    std::vector<Color> getAmbientColors() const { return _list.getAmbientColors(); }
    std::vector<Color> getDiffuseColors() const { return _list.getDiffuseColors(); }
    std::vector<Color> getSpecularColors() const { return _list.getSpecularColors(); }
    std::vector<Color> getEmissiveColors() const { return _list.getEmissiveColors(); }
    std::vector<float> getShininessValues() const { return _list.getShininessValues(); }
    std::vector<std::string> getImages() const { return _list.getImages(); }
    std::vector<std::string> getImagePaths() const { return _list.getImagePaths(); }
    std::vector<std::string> getUuids() const { return _list.getUuids(); }
    std::vector<std::string> getMaterialXs() const { return _list.getMaterialXs(); }
    std::vector<SurfaceFinish> getFinishes() const { return _list.getFinishes(); }
    //@}

    /** @name The texture field's storage: distinct values plus an index
     *
     * The one field that is not a plain array. A texture record is large
     * and its cardinality is low -- glTF gives a mesh a handful of
     * materials, however many faces it has -- so one odd face must not
     * materialise a record per face. The palette holds what the OVERRIDES
     * name, in first-use order, and the index is 0 or |getOverrides()|
     * long; the base's texture is in the base. getTextures() resolves the
     * pair over the whole list, which is the form the compatible encodings
     * and the companion element state.
     */
    //@{
    const std::vector<SurfaceTexture> &getTexturePalette() const
    { return _list.getTexturePalette(); }
    const std::vector<uint16_t> &getTextureIndex() const
    { return _list.getTextureIndex(); }
    void getTextures(std::vector<SurfaceTexture> &palette,
                     std::vector<uint16_t> &index) const
    { _list.getTextures(palette, index); }
    //@}

    Color getAmbientColor(int idx) const;
    Color getDiffuseColor(int idx) const;
    Color getSpecularColor(int idx) const;
    Color getEmissiveColor(int idx) const;
    float getShininess(int idx) const;
    float getTransparency(int idx) const;
    const std::string &getImage(int idx) const;
    const std::string &getImagePath(int idx) const;
    const std::string &getUuid(int idx) const;
    const std::string &getMaterialX(int idx) const;
    SurfaceFinish getFinish(int idx) const;
    /// By value, because the storage holds distinct records rather than
    /// one per entry: there is no array element to hand a reference into
    SurfaceTexture getTexture(int idx) const;
    MaterialAppearance::MaterialType getType(int idx) const;

    /** The first entry's field, which is upstream's no-argument spelling
     *
     * Theirs indexes element 0 of a list it does not check, so an empty
     * property is undefined behaviour there. Ours goes through the indexed
     * getter, which answers with the field's default when the array is
     * short, so an empty list reads as the default material.
     */
    //@{
    Color getAmbientColor() const { return getAmbientColor(0); }
    Color getDiffuseColor() const { return getDiffuseColor(0); }
    Color getSpecularColor() const { return getSpecularColor(0); }
    Color getEmissiveColor() const { return getEmissiveColor(0); }
    float getShininess() const { return getShininess(0); }
    float getTransparency() const { return getTransparency(0); }
    //@}

    void setAmbientColors(const std::vector<Color> &colors);
    void setDiffuseColors(const std::vector<Color> &colors);
    void setSpecularColors(const std::vector<Color> &colors);
    void setEmissiveColors(const std::vector<Color> &colors);
    void setShininessValues(const std::vector<float> &values);
    void setTransparencies(const std::vector<float> &values);
    void setImages(const std::vector<std::string> &values);
    void setImagePaths(const std::vector<std::string> &values);
    void setUuids(const std::vector<std::string> &values);
    void setMaterialXs(const std::vector<std::string> &values);
    /// The records clamp on the way in (SurfaceFinish::normalize), so what
    /// is stored is always something a consumer can draw
    void setFinishes(const std::vector<SurfaceFinish> &values);
    /// One record per entry going in; the palette is built from what is
    /// distinct among them. Clamps the same way the finishes do.
    void setTextures(const std::vector<SurfaceTexture> &values);

    /// Set one field of one entry, expanding that field alone if it has to
    void setAmbientColor(int idx, const Color &col);
    void setDiffuseColor(int idx, const Color &col);
    void setSpecularColor(int idx, const Color &col);
    void setEmissiveColor(int idx, const Color &col);
    void setShininess(int idx, float value);
    void setTransparency(int idx, float value);
    void setImage(int idx, const std::string &value);
    void setImagePath(int idx, const std::string &value);
    void setUuid(int idx, const std::string &value);
    void setMaterialX(int idx, const std::string &value);
    void setFinish(int idx, const SurfaceFinish &value);
    void setTexture(int idx, const SurfaceTexture &value);

    /// Set one field for every entry, leaving the others alone
    void setAmbientColor(const Color &col);
    void setDiffuseColor(const Color &col);
    void setSpecularColor(const Color &col);
    void setEmissiveColor(const Color &col);
    /** Every entry's rgb, leaving every entry's alpha alone
     *
     * The colour edit a dialog makes: the diffuse alpha is the opacity,
     * and in PBR mode the specular alpha is the metallic factor, so a
     * uniform colour write would silently restate them.
     */
    //@{
    void setDiffuseRGB(const Color &col);
    void setSpecularRGB(const Color &col);
    //@}
    void setShininess(float value);
    void setTransparency(float value);
    void setImage(const std::string &value);
    void setImagePath(const std::string &value);
    void setUuid(const std::string &value);
    void setMaterialX(const std::string &value);
    void setFinish(const SurfaceFinish &value);
    void setTexture(const SurfaceTexture &value);
    //@}

    /** Upstream's loose-float and packed-rgba spellings of the four colour
     * setters above, in both arities
     *
     * Each hands straight to the Color form beside it, so none of these
     * carries a behaviour of its own -- including the alpha, which these
     * write like any other component. Note what that means here: the
     * diffuse alpha is the opacity and, in PBR mode, the specular alpha is
     * the metallic factor, so the defaulted a = 1 is a real value and not
     * a way of saying "leave it alone". Use setDiffuseRGB/setSpecularRGB
     * above to keep the existing alpha.
     */
    //@{
    void setAmbientColor(float r, float g, float b, float a = 1.0F)
    { setAmbientColor(Color(r, g, b, a)); }
    void setAmbientColor(uint32_t rgba) { setAmbientColor(Color(rgba)); }
    void setAmbientColor(int idx, float r, float g, float b, float a = 1.0F)
    { setAmbientColor(idx, Color(r, g, b, a)); }
    void setAmbientColor(int idx, uint32_t rgba) { setAmbientColor(idx, Color(rgba)); }

    void setDiffuseColor(float r, float g, float b, float a = 1.0F)
    { setDiffuseColor(Color(r, g, b, a)); }
    void setDiffuseColor(uint32_t rgba) { setDiffuseColor(Color(rgba)); }
    void setDiffuseColor(int idx, float r, float g, float b, float a = 1.0F)
    { setDiffuseColor(idx, Color(r, g, b, a)); }
    void setDiffuseColor(int idx, uint32_t rgba) { setDiffuseColor(idx, Color(rgba)); }

    void setSpecularColor(float r, float g, float b, float a = 1.0F)
    { setSpecularColor(Color(r, g, b, a)); }
    void setSpecularColor(uint32_t rgba) { setSpecularColor(Color(rgba)); }
    void setSpecularColor(int idx, float r, float g, float b, float a = 1.0F)
    { setSpecularColor(idx, Color(r, g, b, a)); }
    void setSpecularColor(int idx, uint32_t rgba) { setSpecularColor(idx, Color(rgba)); }

    void setEmissiveColor(float r, float g, float b, float a = 1.0F)
    { setEmissiveColor(Color(r, g, b, a)); }
    void setEmissiveColor(uint32_t rgba) { setEmissiveColor(Color(rgba)); }
    void setEmissiveColor(int idx, float r, float g, float b, float a = 1.0F)
    { setEmissiveColor(idx, Color(r, g, b, a)); }
    void setEmissiveColor(int idx, uint32_t rgba) { setEmissiveColor(idx, Color(rgba)); }
    //@}

    /// Whether any entry names a texture or a material card
    bool hasTextureOrCard() const { return _list.hasTextureOrCard(); }
    /// Whether any entry names an image, inline or by path
    bool hasImage() const { return _list.hasImage(); }
    /// Whether any entry states a surface finish; the cheap gate for a
    /// consumer that has nothing to do when none does. Normalised, so a
    /// finish written and then cleared answers false rather than "there is
    /// still an array there"
    bool hasFinish() const { return _list.hasFinish(); }
    /// Whether any entry names a texture map. Normalised, so an empty
    /// palette is the whole answer: a palette entry survives collapse only
    /// while some entry still resolves to it.
    bool hasTexture() const { return _list.hasTexture(); }

    /** @name The texture maps as stored content
     *
     * A slot holds a content hash, and App::FileBlobManager owns the bytes.
     * This is the first property to refer to SEVERAL blobs at once --
     * PropertyFileIncluded and PropertyPartShape hold a single one each --
     * and the whole of what that costs is a discipline rather than a
     * signature: arrival order is NOT queue order, because
     * addPendingReferrer serves an already-read hash immediately and queues
     * the rest, so a multi-slot referrer must RESOLVE THE SLOT BY CONTENT
     * HASH and never by the order the handles come back
     * (docs/ShapeAppearanceDesign.md 10.2).
     *
     * Two slots naming the same content share one blob and both are
     * assigned, which is correct and makes the assignment idempotent --
     * which in turn is what lets a duplicated queue entry be harmless.
     */
    //@{
    /// Take a file into the document's store and answer the content hash a
    /// SurfaceTexture slot holds. Empty if the file cannot be read.
    std::string insertTextureFile(const char *path, const char *extension = nullptr);
    /// Where the content behind a hash is on disk, empty if this property
    /// does not hold it (yet -- a restore serves the handles later)
    std::string getTextureFile(const std::string &hash) const;
    /// Tell a save which content this property refers to. One referrer name
    /// per SLOT, so the files land under readable names rather than under a
    /// number, and once per DISTINCT hash, because shared content is one
    /// file with several referrers -- which is why the appearance names its
    /// own referrers here instead of handing the collect pass one handle.
    void collectBlobs(FileBlobManager &manager, const DocumentObject *object) const override;
    /** Take hold of the stored content the value names
     *
     * A card applied to the base (followMaterial) brings a MaterialX
     * manifest hash whose bytes the card's own property has already put in
     * the store; this claims them for the list, which is what keeps them
     * alive and lets a save note them from here. Content not in the store
     * is queued with the manager as a restore would queue it.
     */
    void holdStoredBlobs() { requestTextureBlobs(); }
    /// Take a restored blob into whichever slots name its hash
    void assignRestoredBlob(const FileBlobHandle &blob) override;
    /// The texture content has no schema 4 spelling: the maps themselves ride
    /// a companion element, the bytes behind them live in the store and
    /// nowhere else. A save with no store keeps the hashes and drops the
    /// files, which is what makes this the one referrer answering true.
    /// A MaterialX manifest hash has no spelling at all without the store,
    /// so it forces the offer the same way a texture does
    bool blobContentNeedsStore() const override { return hasTexture() || _list.hasMaterialX(); }
    //@}

    /** @name PBR mode
     *
     * One bool for the whole list. When set, the same arrays are READ AS
     * PBR quantities -- reinterpreted, not converted, so the storage cost
     * and the document format do not change: the diffuse colour is the
     * base colour (its alpha still the opacity), the shininess slot is
     * the roughness at full float precision, the specular colour is the
     * F0 tint with the metallic factor riding its alpha, and the emissive
     * is unchanged. The ambient slot has no PBR meaning.
     *
     * An old build opening a PBR document finds the values in the Phong
     * slots -- a degraded look, no data loss -- and re-saving there drops
     * the mode, not the values. The compatible encodings cannot carry the
     * flag at all, so an old-schema save writes the Phong derivation
     * (getPhongMaterial()) in place of the raw slots.
     */
    //@{
    bool isPBR() const { return _list.isPBR(); }
    /// Flip the reading of the stored values; converts nothing
    void setPBR(bool enable);
    /** Flip the mode AND convert the stored values so the look survives
     *
     * The editor's toggle. Toward Phong every entry goes through
     * getPhongMaterial(); toward PBR through MaterialAppearance::phongToPbr (base
     * colour kept, roughness from the shininess fit, dielectric). A
     * Phong-PBR-Phong round trip keeps the look but forgets the specular
     * colour, which only Phong can state. One atomic change; a no-op
     * when the mode already matches.
     */
    void convertPBR(bool enable);
    /// The metallic factor: the specular alpha. An unset field reads as 0
    /// (dielectric) -- see specularDefault(). In Phong mode always 0.
    float getMetallic(int idx) const;
    /// The roughness: the shininess slot in PBR mode; in Phong mode the
    /// Blinn-Phong derivation of the stored shininess.
    float getRoughness(int idx) const;
    /// The metallic/roughness writers demand PBR mode: in Phong mode the
    /// slots they would land in mean something else, and a caller holding
    /// a metallic value has decided the mode already.
    void setMetallicValues(const std::vector<float> &values);
    void setRoughnessValues(const std::vector<float> &values);
    void setMetallic(int idx, float value);
    void setRoughness(int idx, float value);
    void setMetallic(float value);
    void setRoughness(float value);
    /** The Phong reading of one entry
     *
     * In Phong mode this is getMaterial(). In PBR mode it derives the
     * classic slots: diffuse = base * (1 - metallic), specular =
     * mix(0.04 * tint, base, metallic), shininess from the roughness;
     * emissive and the strings carry over. Used by the compatible save
     * encodings, the Coin GL display leg, and exporters to formats with
     * no PBR terms.
     */
    MaterialAppearance getPhongMaterial(int idx) const;
    /// The Phong reading of the BASE, which is what a consumer with one
    /// material node to fill wants: the object's look, not face 0's
    MaterialAppearance getPhongBase() const { return _list.getPhongBase(); }
    //@}

    /** Whether the diffuse colour is the only field that varies per entry
     *
     * True for every appearance a plain colour list could have expressed --
     * a per-face import, and anything uniform. It is the condition under which
     * the compatibility name DiffuseColor can still be written out with its
     * values, so that a reader which knows nothing about this property still
     * gets the face colours. When it is false the appearance holds something
     * a colour list cannot say, and writing a lossy copy would be worse than
     * writing none.
     */
    bool variesOnlyInDiffuse() const;

    PyObject *getPyObject() override;
    void setPyObject(PyObject *) override;

    const char* getEditorName(void) const override;
    Property *Copy(void) const override;
    void Paste(const Property &from) override;
    bool isSame(const Property &other) const override;
    Property *copyBeforeChange() const override { return Copy(); }

    /** The size of the storage, which is not the size of the list
     *
     * Content only, as every list property reports it, because the
     * inline-versus-archive rule in PropertyLists::Save reads it as the cost
     * of writing this property. See getSaveSize() for why that rule needs
     * more than this number.
     */
    unsigned int getMemSize() const override;
    unsigned int getSaveSize(Base::Writer &writer) const override;

    void Save(Base::Writer &writer) const override;
    void Restore(Base::XMLReader &reader) override;
    void SaveDocFile(Base::Writer &writer) const override;
    void RestoreDocFile(Base::Reader &reader) override;

protected:
    MaterialAppearance getPyValue(PyObject *) const;
    void setPyValues(const std::vector<PyObject*> &vals, const std::vector<int> &indices) override;

    void restoreXML(Base::XMLReader &) override;
    bool saveXML(Base::Writer &) const override;
    bool canSaveStream(Base::Writer &) const override { return true; }
    void restoreStream(Base::InputStream &s, unsigned count) override;
    void saveStream(Base::OutputStream &) const override;

    /// The per field encoding, written only at a schema that admits it.
    /// \a legacy on the readers: whether the file means transparency by a
    /// colour's alpha (Base::alphaIsOpacity said no of its version).
    void saveFieldStream(Base::OutputStream &str) const;
    void restoreFieldStream(Base::InputStream &str, unsigned count, bool legacy);
    bool saveFieldXML(Base::Writer &writer) const;
    void restoreFieldXML(Base::XMLReader &reader, unsigned count);

    /// Upstream's second pass: three strings per entry, after the colours
    void saveStringStream(Base::OutputStream &str) const;
    void restoreStringStream(Base::InputStream &str, unsigned count);

private:
    /** Land a finish restored from the element beside this property's
     *
     * Held rather than applied on the spot because the two encodings land at
     * different times: an archive entry is read long after the XML pass, and
     * its restore CLEARS the finish field (the compatible stream cannot state
     * one). So the value waits until the materials are in.
     */
    void applyPendingFinish();
    /// The same for the texture pair, which lands as a palette and an index
    /// rather than as one record per entry
    void applyPendingTexture();
    /// Land the base and the override list a schema 5 file states, with the
    /// checks the storage's invariants turn into a file format
    void installBase(const MaterialAppearance &base, int8_t type);
    /// The document's blob store, or the process-wide one for a property
    /// with no document -- the same resolution PropertyFileIncluded makes
    FileBlobManager &blobManager() const;
    /// Ask the manager for every distinct hash the palette names and does
    /// not already hold. Called once the palette has landed, from both
    /// restore paths.
    void requestTextureBlobs();
    /// Ask for the files a HELD manifest names; a restore calls it once the
    /// manifest itself has arrived, since only the manifest knows them.
    void requestMaterialXChildren(const std::string &manifestHash);

    /** Run a write against the value and signal it only if it changed
     *
     * Every mutator goes through this. It works because the value is
     * copy-on-write: taking a snapshot costs a pointer, so the write can
     * simply be made and the result compared by STORAGE IDENTITY -- if the
     * value is unchanged, AppearanceList's own setters return without
     * detaching and the pointer is still the one the snapshot holds.
     *
     * The old value then goes back for exactly as long as it takes to open
     * the atomic change, because aboutToSetValue() is what snapshots the
     * property for undo and it has to see the value the change is FROM.
     * Three pointer assignments, and no per-field "would this change
     * anything" predicate to keep in step with the setter beside it.
     */
    template<class Op>
    void change(Op &&op, int touched = -1);

    /** The value, which several holders may share
     *
     * A Python variable, an undo snapshot and this property can all name
     * the same storage until one of them writes; see App::AppearanceList.
     */
    AppearanceList _list;
    /// The Python views handed out and not yet dropped. Raw pointers: a
    /// view unregisters itself when Python drops it, and this property
    /// detaches every one of them on the way out.
    std::vector<MaterialListPy *> _views;

    /// Restored from the companion element, waiting for the materials to land
    std::vector<SurfaceFinish> _pendingFinish;
    /// The same, for the texture companion; the pair travels together
    //@{
    std::vector<SurfaceTexture> _pendingTexturePalette;
    std::vector<uint16_t> _pendingTextureIndex;
    //@}
    /// The manager holding queued requests for this property, so a death
    /// mid-restore withdraws them rather than leaving it queued for content
    /// it will never take
    FileBlobManager *_pendingBlobManager {nullptr};

    /** Which shape the doc file being read is in
     *
     * Upstream states it on the element as version="3" and it means the
     * colours are followed by a second pass of strings. Absent, or on a
     * file this fork wrote at schema 5 or later, there is no second pass.
     */
    int _fileVersion {0};
};


/** The surface finish of a material list, written as a property of its own
 *
 * A migration that never happened, written out as though it had: pretend an
 * older format kept the surface finish in a property beside the appearance,
 * and that this one folded it into the material. Then a document can state
 * both, and both readings are honest.
 *
 * ⭐ What that buys is a save that is lossless BOTH ways at once, which no
 * amount of cleverness inside PropertyAppearanceList's own encodings could
 * give: upstream reads the material element byte for byte as it always has
 * and simply walks past this one (readElement skips elements whose name it
 * did not ask for), while we read both and lose nothing.
 *
 * ⚠️ It is never a member of anything. PropertyAppearanceList::Save builds one
 * on the stack, hands it the finishes, writes it, and drops it; Restore does
 * the mirror. That is deliberate: as a container property it would join the
 * undo stack and snapshot bytes that ShapeAppearance's own Copy() already
 * carries, and it would need a pointer back to the appearance whose values
 * it really held -- a second store of one value, and a Copy() that could not
 * honestly implement itself.
 */
class AppExport PropertySurfaceFinishList: public Property
{
    TYPESYSTEM_HEADER_WITH_OVERRIDE();

public:
    PropertySurfaceFinishList();
    ~PropertySurfaceFinishList() override;

    const std::vector<SurfaceFinish> &getValues() const { return _values; }
    void setValue(const std::vector<SurfaceFinish> &values) { _values = values; }
    std::vector<SurfaceFinish> takeValues() { return std::move(_values); }

    /// Restore from a reader ALREADY positioned on the element, which is how
    /// the material list reads it: it has to look at the element to know
    /// whether it is this one at all.
    void RestoreHere(Base::XMLReader &reader);

    PyObject *getPyObject() override;
    void setPyObject(PyObject *) override;

    void Save(Base::Writer &writer) const override;
    void Restore(Base::XMLReader &reader) override;

    Property *Copy() const override;
    void Paste(const Property &from) override;
    bool isSame(const Property &other) const override;
    unsigned int getMemSize() const override;

private:
    std::vector<SurfaceFinish> _values;
};

/** The texture field beside the appearance, for the schemas that cannot
 * state one inside it
 *
 * The same carrier PropertySurfaceFinishList is, and for the same reason
 * (docs/ShapeAppearanceDesign.md 9.4.1): below schema 5 the material
 * encodings are upstream's and have nowhere to put a texture -- so without
 * this a texture would vanish from every document written in upstream's
 * format, which a document restored from one still is. Written as an
 * element of its own inside the
 * appearance property's element, which upstream's reader walks straight
 * past.
 *
 * It carries the palette and the index rather than one record per entry:
 * that is what the field IS, and flattening it here would give the
 * compatible schema a bigger file than the fork's own.
 */
class AppExport PropertySurfaceTextureList: public Property
{
    TYPESYSTEM_HEADER_WITH_OVERRIDE();

public:
    PropertySurfaceTextureList();
    ~PropertySurfaceTextureList() override;

    const std::vector<SurfaceTexture> &getPalette() const { return _palette; }
    const std::vector<uint16_t> &getIndex() const { return _index; }
    void setValue(const std::vector<SurfaceTexture> &palette,
                  const std::vector<uint16_t> &index)
    { _palette = palette; _index = index; }
    /// Both halves at once, because one without the other says nothing
    void takeValues(std::vector<SurfaceTexture> &palette, std::vector<uint16_t> &index)
    { palette = std::move(_palette); index = std::move(_index); }

    /// Restore from a reader ALREADY positioned on the element, which is how
    /// the material list reads it: it has to look at the element to know
    /// whether it is this one at all.
    void RestoreHere(Base::XMLReader &reader);

    PyObject *getPyObject() override;
    void setPyObject(PyObject *) override;

    void Save(Base::Writer &writer) const override;
    void Restore(Base::XMLReader &reader) override;

    Property *Copy() const override;
    void Paste(const Property &from) override;
    bool isSame(const Property &other) const override;
    unsigned int getMemSize() const override;

private:
    std::vector<SurfaceTexture> _palette;
    std::vector<uint16_t> _index;
};

/** Property for dynamic creation of a FreeCAD persistent object
 *
 * In Python, this property can be assigned a type string to create a dynamic FreeCAD
 * object, and then read back as the Python binding of the newly created object.
 */
class AppExport PropertyPersistentObject: public PropertyString {
    TYPESYSTEM_HEADER_WITH_OVERRIDE();
    using inherited = PropertyString;
public:
    // Inherits PropertyString's opt-in, but holds a serialized object of
    // arbitrary size and meaning -- not a value a class default block
    // may speak for.
    bool canShareDefault() const override { return false; }
    PyObject *getPyObject() override;
    void setValue(const char* type) override;

    void Save (Base::Writer &writer) const override;
    void Restore(Base::XMLReader &reader) override;

    Property *Copy() const override;
    void Paste(const Property &from) override;
    unsigned int getMemSize () const override;

    std::shared_ptr<Base::Persistence> getObject() const {
        return _pObject;
    }

    bool isSame(const Property &other) const override;
    Property *copyBeforeChange() const override;

protected:
    std::shared_ptr<Base::Persistence> _pObject;
};

} // namespace App

#endif // APP_PROPERTYSTANDARD_H
