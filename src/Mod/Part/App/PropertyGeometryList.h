/***************************************************************************
 *   Copyright (c) 2010 Jürgen Riegel <juergen.riegel@web.de>              *
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

#ifndef APP_PropertyGeometryList_H
#define APP_PropertyGeometryList_H

#include <vector>

#include <App/Property.h>

#include "Geometry.h"


namespace Base {
class Writer;
}

namespace Part
{
class Geometry;

class PartExport PropertyGeometryList: public App::PropertyLists,
                                       private App::AtomicPropertyChangeInterface<PropertyGeometryList>
{
    TYPESYSTEM_HEADER_WITH_OVERRIDE();

public:
    using atomic_change = AtomicPropertyChangeInterface<PropertyGeometryList>::AtomicPropertyChange;
    friend atomic_change;

    /**
     * A constructor.
     * A more elaborate description of the constructor.
     */
    PropertyGeometryList();

    /**
     * A destructor.
     * A more elaborate description of the destructor.
     */
    ~PropertyGeometryList() override;

    void setSize(int newSize) override;
    int getSize() const override;

    bool isSame(const App::Property &other) const override;
    App::Property *copyBeforeChange() const override;

    /** Sets the property
     */
    void setValue(const Geometry*);
    /// Clones every element; the caller keeps ownership of what it passed
    void setValues(const std::vector<const Geometry*>&);
    /// Takes ownership of every element; nothing is cloned
    void setValues(std::vector<const Geometry*>&&);
    /// Same two, for a caller holding the geometry it made as non-const
    void setValues(const std::vector<Geometry*>& v) {
        const std::vector<const Geometry*> tmp(v.begin(), v.end());
        setValues(tmp);
    }
    void setValues(std::vector<Geometry*>&& v) {
        setValues(std::vector<const Geometry*>(v.begin(), v.end()));
        v.clear();
    }

    void moveValues(PropertyGeometryList &&other);

    /** Convert all linear curve to line segments
     * @return Return the count of conversion
     */
    int linearize();

    /// index operator
    const Geometry *operator[] (const int idx) const {
        return _lValueList[idx];
    }

    /** The value: the elements are const, since a write to one that
     * escapes aboutToSetValue()/hasSetValue() is invisible to undo,
     * recompute, the view provider and the transaction log. The only
     * non-const access is through a guard, mutableValue() below.
     */
    const std::vector<const Geometry*> &getValues() const {
        return _lValueList;
    }

    /** Non-const access to one element, under a guard that has already
     * marked the property as about to change. The property owns the
     * geometry, so the cast is legitimate here and nowhere else.
     */
    Geometry *mutableValue(atomic_change &guard, int idx) {
        guard.aboutToChange();
        return const_cast<Geometry*>(_lValueList[idx]);
    }

    void set1Value(int idx, std::unique_ptr<Geometry> &&);

    PyObject *getPyObject() override;
    void setPyObject(PyObject *) override;

    void Save(Base::Writer &writer) const override;
    void Restore(Base::XMLReader &reader) override;

    App::Property *Copy() const override;
    void Paste(const App::Property &from) override;

    unsigned int getMemSize() const override;

private:
    void trySaveGeometry(const Geometry * geom, Base::Writer &writer) const;
    void tryRestoreGeometry(Geometry * geom, Base::XMLReader &reader);

private:
    std::vector<const Geometry*> _lValueList;
};

} // namespace Part


#endif // APP_PropertyGeometryList_H
