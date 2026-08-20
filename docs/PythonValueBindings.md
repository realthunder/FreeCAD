# Handing Python a value without handing it a copy

How a FreeCAD property gives Python something that is cheap to read, safe to
hold, and able to write back -- and what the binding generator does not do
for you. Written from the `App::MaterialList` work (2026-08-20), which is
the first type in the tree built this way; `Shape.Faces` is the next one it
is meant for.

## 1. The problem, in the shape it keeps taking

A property holds a good internal representation. Python asks for it. The
binding builds a whole new Python container out of a whole new set of C++
copies, hands it over, and throws it away on the next read.

- `PropertyMaterialList::getPyObject()` built a `Py::Tuple` of N freshly
  copied `App::Material` objects -- ten thousand of them to answer `len()`
  on a ten thousand face part.
- `TopoShapePyImp.cpp`'s `getElements()` (behind `Shape.Faces`) builds a
  `std::vector<TopoShape>` of N, then N more heap `TopoShape` copies inside
  `getPyObject()`, then N `TopoShape*Py` wrappers -- **per attribute read**,
  over a C++ cache (`TopoShape::Cache::Info::topoShapes`) that exists
  precisely so the work is not repeated.
- `PropertyColorList::getPyObject()` builds a list of N four-float tuples.

And none of them writes back. `vp.ShapeAppearance[0].DiffuseColor = c`
compiled, ran and did nothing for years, which bit this project twice.

## 2. The three states, and what each costs

The value is **copy-on-write**: `Base::COWValue<Data>` over a `shared_ptr`,
where every const path reads and every mutating path calls `detach()` first,
which copies exactly when somebody else holds the same storage.

| State | What it is | A read | A write |
|---|---|---|---|
| **Attached** | a live view of the property | reads the property's value in place | goes through the property's own change signalling |
| **Detached** | a plain value sharing storage | reads its own | detaches, then writes its own |
| **Owned** | a value built in Python | reads its own | writes its own |

```python
a = vp.ShapeAppearance      # attached: writing to it paints the object
b = a.copy()                # detached: writing to it paints nothing
vp.ShapeAppearance = b      # O(1) -- the property takes a share of b
c = App.MaterialList(4)     # owned
```

Assigning a list into any property detaches it. That is the rule that keeps
"a view" from silently becoming "a view of two things".

## 3. The pieces

### 3.1 The value class

Everything about the storage and its rules moves out of the property into a
copyable class (`App::MaterialList`), leaving the property with
serialization, its restore queue, the touch list and the change signalling.
Three accessors, and which one a method uses is the whole discipline:

```cpp
const Data &rd() const;   // read: never detaches
Data &wd();               // write: detaches if shared
Data &nd() const;         // write WITHOUT detaching -- normalisation only
```

`nd()` is for work that changes what is *stored* and never what the value
*means* -- collapsing a field to its shortest form, taking a restored blob
handle in. Sharing that work with the other holders is the point; detaching
on a read would defeat the sharing entirely. It must never be called on a
null holder, whose storage is a default instance shared program-wide.

### 3.2 The property signals a change only when there is one

`aboutToSetValue()` has to see the value the change is FROM, and firing it
for a write that changes nothing records an undo step for nothing. Before,
every setter interleaved "did this change anything" with the write.
Copy-on-write removes the need for a per-field predicate:

```cpp
template<class Op>
void PropertyMaterialList::change(Op &&op, int touched)
{
    const MaterialList before = _list;   // a pointer; forces the next write to detach
    op();
    if (_list.isSameData(before)) {
        return;                          // the write changed nothing
    }
    const MaterialList after = _list;
    _list = before;                      // the value the change is FROM
    atomic_change guard(*this);          // ... which aboutToSetValue() now records
    _list = after;
    guard.tryInvoke();
}
```

**This only works if no setter detaches while deciding.** A pre-check that
reads through `wd()` copies the storage and every no-op write then reports a
change. That is why the field-write templates take a **member pointer**
(`std::vector<T> Data::*`) rather than a reference into the storage: a
reference has to come out of `wd()`.

### 3.3 The Python type is a view with a registry

```cpp
class MaterialListPy {
    PropertyMaterialList *owner;   // attached while non-null
    MaterialList *ownvalue;        // the private value once detached
};
```

- The **twin pointer** is the owner's value while attached and the private
  one once detached, so every generated accessor reads the live value.
- Writes never go through the twin. They go through `edit()`, which routes
  an attached write into `owner->editList(op, index)` -- the property's own
  `change()`. So a Python write records undo, touches the document and
  redraws, exactly as setting the whole property does.
- The property keeps a `std::vector<MaterialListPy *>` of the views it
  handed out and detaches every one of them in its destructor. A view of a
  closed document keeps what it last saw and writes nowhere. Move the vector
  out before walking it: detaching unregisters.
- `Delete="false"` with `Initialization="true"` in the XML: the generated
  destructor would otherwise `delete` a twin that, while attached, is the
  property's own member. `finalization()` unregisters and deletes only what
  the view owns.
- A read-only property (`Property::ReadOnly` / `Immutable`) hands out a
  `setConst()` view, and every write path refuses.

`getPyObject()` makes a fresh view per call rather than caching one. There
is nothing to go stale, and no back-reference to keep alive.

### 3.4 Items write back too, and that needed a change in Base

`PyObjectBase` already made `obj.Placement.Base.x = 1` reach the object:
`__getattro` stamps every `PyObjectBase` an attribute hands out with its
parent and the attribute name, and a write calls `startNotify()`, which
re-assigns the child to that attribute.

**None of it reached a sequence.** The generator installs `sq_item` and
`sq_ass_item` raw into the slot tables, so a value read out of a container
had no link to it -- which is why `obj.Placement.Base[0] = 1` is a silent
no-op to this day.

The item form is one more key: `__item_of_parent__` (the index) beside the
`__instance_of_parent__` the attribute form already uses, and a
`startNotify()` branch that does `PyObject_SetItem(parent, index, this)`.
A list-like type owes its items exactly one line in `sq_item`:

```cpp
auto *item = new MaterialPy(new Material(list->list().getMaterial(idx)));
list->trackReturnedItem(item, idx);   // and now writing to it comes back
return item;
```

`trackReturnedItem` makes the same two refusals `__getattro` makes (a const
value is not a way in; a `NoTrack` value meant it) and retires whatever was
handed out for that index before -- the item half of bug #0002902.

An item is a **copy with a link**, not a view: a material is composed out of
the field arrays, so there is no `Material` in storage for a wrapper to
point at. Writing any field on it writes the whole entry back to that index,
which is exactly the existing `Placement` semantics, stale-clobber included.

## 4. What the generator does not do

- **Sequence and mapping slots get no wrappers.** No `isValid`, no
  `isConst`, no `startNotify`, no exception translation -- unlike the method
  and attribute wrappers beside them. Every slot makes those checks itself.
  `src/Base/VectorPyImp.cpp:179-289` is the model for the slice handling.
- **`<ClassDeclarations>` is XML.** A bare `&` in a declaration is a parse
  error reported by expat two build steps away from where it reads.
- **`obj[i] = v` needs `sq_ass_item`; `obj[0:2] = ...` needs
  `mp_ass_subscript`.** Declaring `<Sequence>` at all installs both tables.
- **`slots` is a Qt macro** that expands to nothing, and the error it makes
  names the `.` after your variable rather than the variable.

## 5. Applying this to the shape lists

`Shape.Faces` is the same problem one layer up, and the pieces map over:

- `TopoShape::Cache::Info` already holds the `std::vector<TopoShape>` a
  `Faces` list would be a view of, and `TopoShape` itself is already
  `shared_ptr`-shared, so the value class exists.
- A `TopoShapeListPy` with `sq_length` / `sq_item` would turn N allocations
  per attribute read into O(1) plus one per index actually touched.
- Sub-shape wrappers are `setNotTracking()` today
  (`TopoShapePyImp.cpp:2892`), which is the existing decision that they are
  snapshots. `trackReturnedItem` respects that flag, so the item link is
  opt-in per type rather than something the list forces.
- `PropertyTopoShapeList::getPyObject()` (`PropertyTopoShapeList.cpp:117`)
  is a second consumer with the same shape.

The one thing to decide per type is whether an item should be a live view or
a copy with a link. For materials it had to be a copy; for a face, where the
storage really does hold a `TopoShape`, a view is possible.
