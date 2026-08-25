# Translating upstream topological-naming code into this fork

Status: written 2026-08-25 as phase 0 of the topo-naming harvest. All counts
in this document were measured against `upstream/main` at `b352a4fc7f` and
the fork at `a1893a1ac1`. The shared merge base is `a662fbb2ff` (2023-12-31),
so the divergence is about 20 months deep -- this is a port, not a merge.

Driver: the fork's `src/Mod/Part/App/TopoShapeEx.cpp` is 6276 lines with
essentially no test coverage of its own, while upstream carries a 3472-line
GoogleTest suite over its counterpart. Harvesting that suite, and the genuine
bug fixes around it, is worth doing -- but every hunk has to be translated
first. This document is the translation table.

## 0. Policy

**Preserve this fork's naming.** Upstream renamed the element-naming API
wholesale; the fork did not follow and will not. Porting a hunk therefore
costs a mechanical identifier substitution and nothing more -- see section 1
for why that is true, and section 5 for the cases where it is not.

## 1. The wire format is byte-identical

This is the fact that makes the whole harvest cheap. The strings that end up
in a saved document are the same on both sides. Every element-name constant
matches:

| meaning            | string | fork accessor            | upstream constant                  |
|--------------------|--------|--------------------------|------------------------------------|
| element map prefix | `;`    | `elementMapPrefix()`     | `ELEMENT_MAP_PREFIX`               |
| missing element    | `?`    | `missingPrefix()`        | `MISSING_PREFIX`                   |
| mapped child       | `;:R`  | `mappedChildPrefix()`    | `MAPPED_CHILD_ELEMENTS_PREFIX`     |
| tag                | `;:H`  | `tagPostfix()`           | `POSTFIX_TAG`                      |
| decimal tag        | `;:T`  | `decimalTagPostfix()`    | `POSTFIX_DECIMAL_TAG`              |
| external tag       | `;:X`  | `externalTagPostfix()`   | `POSTFIX_EXTERNAL_TAG`             |
| child tag          | `;:C`  | `childTagPostfix()`      | `POSTFIX_CHILD`                    |
| index              | `;:I`  | `indexPostfix()`         | `POSTFIX_INDEX`                    |
| upper              | `;:U`  | `upperPostfix()`         | `POSTFIX_UPPER`                    |
| lower              | `;:L`  | `lowerPostfix()`         | `POSTFIX_LOWER`                    |
| modified           | `;:M`  | `modPostfix()`           | `POSTFIX_MOD`                      |
| generated          | `;:G`  | `genPostfix()`           | `POSTFIX_GEN`                      |
| modified+generated | `;:MG` | `modgenPostfix()`        | `POSTFIX_MODGEN`                   |
| duplicate          | `;D`   | `duplicatePostfix()`     | `POSTFIX_DUPLICATE`                |

Both sides declare these in `src/App/ElementNamingUtils.h`. The only
structural difference is the form: the fork returns
`const std::string&` from a function, upstream exposes a
`constexpr const char*`. A ported expression stays semantically valid after
substitution, though `strcmp`-style uses may need `.c_str()` or a
`std::string` comparison.

Upstream has one constant the fork does not, `ELEMENT_MAP_INDEX "_"`. It is
on the reject list -- see section 5.

The free functions around the constants -- `hasMissingElement`,
`isMappedElement`, `newElementName`, `oldElementName`, `noElementName`,
`findElementName`, `hasMappedElementName` -- carry the same names and
signatures on both sides and need no translation at all.

## 2. The method map

`makEXxx` -> `makeElementXxx`, mechanically, over 34 names. Measured
**551 call sites in 52 files** in the fork.

| fork | upstream | fork call sites |
|------|----------|-----------------|
| `makEBSplineFace`   | `makeElementBSplineFace`   | 13 |
| `makEBoolean`       | `makeElementBoolean`       | 27 |
| `makEChamfer`       | `makeElementChamfer`       | 10 |
| `makECompound`      | `makeElementCompound`      | 73 |
| `makECopy`          | `makeElementCopy`          | 25 |
| `makECut`           | `makeElementCut`           | 15 |
| `makEDraft`         | `makeElementDraft`         | 5  |
| `makEEvolve`        | `makeElementEvolve`        | 5  |
| `makEFace`          | `makeElementFace`          | 28 |
| `makEFilledFace`    | `makeElementFilledFace`    | 8  |
| `makEFillet`        | `makeElementFillet`        | 10 |
| `makEFuse`          | `makeElementFuse`          | 14 |
| `makEGTransform`    | `makeElementGTransform`    | 8  |
| `makEGeneralFuse`   | `makeElementGeneralFuse`   | 3  |
| `makELoft`          | `makeElementLoft`          | 6  |
| `makEMirror`        | `makeElementMirror`        | 6  |
| `makEOffset`        | `makeElementOffset`        | 7  |
| `makEOffset2D`      | `makeElementOffset2D`      | 14 |
| `makEOffsetFace`    | `makeElementOffsetFace`    | 5  |
| `makEOrderedWires`  | `makeElementOrderedWires`  | 4  |
| `makEPipeShell`     | `makeElementPipeShell`     | 6  |
| `makEPrism`         | `makeElementPrism`         | 8  |
| `makEPrismUntil`    | `makeElementPrismUntil`    | 7  |
| `makERefine`        | `makeElementRefine`        | 17 |
| `makERevolve`       | `makeElementRevolve`       | 9  |
| `makERuledSurface`  | `makeElementRuledSurface`  | 5  |
| `makEShell`         | `makeElementShell`         | 8  |
| `makEShellFromWires`| `makeElementShellFromWires`| 5  |
| `makESlice`         | `makeElementSlice`         | 5  |
| `makESlices`        | `makeElementSlices`        | 5  |
| `makESolid`         | `makeElementSolid`         | 18 |
| `makEThickSolid`    | `makeElementThickSolid`    | 7  |
| `makETransform`     | `makeElementTransform`     | 19 |
| `makEWires`         | `makeElementWires`         | 44 |

### The three entries that are not 1:1

- **`makESHAPE` (27 sites) and `makEShape` (75 sites) both collapse to
  upstream's single `makeElementShape`.** Translating upstream -> fork here
  needs a look at the argument list, not a substitution. This is the one
  place in the map where a blind sed is wrong.
- **`makeElementRevolution`** is upstream-only; the fork has no counterpart.
  Do not confuse it with `makERevolve` / `makeElementRevolve`, which exists
  on both sides.
- **`makeElementXor`** is upstream-only.

### Other renamed members

- fork `searchSubShape` <-> upstream `findSubShapesWithSharedVertex`.

## 3. File-layout divergence

Same filenames, different split. When a ported hunk does not appear where
upstream put it, look here first.

| what | fork | upstream |
|------|------|----------|
| the 36 `ComplexGeoData::` element-map methods | `src/App/ElementMap.cpp` (1931 lines) | `src/App/ComplexGeoData.cpp` (748 lines) |
| `src/App/ComplexGeoData.cpp` | 182 lines (the rest lives in ElementMap.cpp) | 748 lines |
| the TopoShape element-map expansion | `src/Mod/Part/App/TopoShapeEx.cpp` (6276) | `src/Mod/Part/App/TopoShapeExpansion.cpp` (6365) |

Upstream-only files, absent here: `TopoShapeMapper.cpp` (202),
`TopoShapeMapper.h` (310), `ShapeMapHasher.h` (50). A hunk that includes or
uses these needs the dependency resolved by hand, not a substitution.

**Python bindings: upstream migrated `.xml` to `.pyi`. The fork keeps `.xml`
plus `generate_from_xml`. Do not follow that migration when porting.**

## 4. The filter for a genuine bug fix

Upstream's `TopoShapeExpansion.cpp` has 164 commits, and most of them are
"Transfer in makEXxx from LS3" or "clean and test" -- that is upstream
porting *this fork's* code. A commit labelled a fix inside that window is
very often upstream repairing its own transcription error, which is worth
nothing here because the fork never had the error.

**The test: compare upstream's pre-fix text to the fork's current text.**

- They match => the bug is live in the fork. Port it.
- They differ => upstream artifact. Skip it.

Verified applications of the filter:

| commit | verdict | reason |
|--------|---------|--------|
| `9ee2c74545` loft segments too close (#5855) | **APPLIES** | fork `makELoft` has no `getCenterOfGravity` guard at all |
| `68c6fc2088` performance | **APPLIES** | fork `searchSubShape` rebuilds the whole element map per vertex via `getSubTopoShapes` |
| `41d1aed844` OCCT bug 1330 | **APPLIES, partly done** | the fork has `ShapeAnalysis_FreeBoundsFix` (added 2026-08-23, `e00436d5ee`) but wired only 2 of ~8 call sites |
| `140c9febc4` (#22889 sort crash) | SKIP | fixes `ElementNameComparator`, which upstream invented in `ef2ef6d7aa`; the fork has never had it |
| `0a3677b9eb` | SKIP | operator-precedence slip in a sanity check upstream itself added |
| `3a6f70946a` ruled surface (#16013) | **APPLIES, done** | fork sampled closed curves at their coincident end points |
| `9f3d6543c6` -> shell type check | **APPLIES, done** | fork accepted any result *containing* a shell |
| `110f3e000d` -> evolve join type | **APPLIES, done** | fork mapped `JoinType::Arc` to `GeomAbs_Tangent` |
| `76df39e99d` getSubTopoShape | SKIP | upstream wrote `IndexError` in the transfer commit itself; our `ValueError` was never upstream's |

## 5. Reject list

These would change generated element names or stored documents. **Do not
port them, and do not port a hunk that depends on them.**

- `26b2631251` "Squash to one index character in element names", together
  with `ELEMENT_MAP_INDEX "_"`, `indexSuffix()` and `indexOfElement()`. The
  fork has none of this and must not take it -- it changes the strings in
  section 1, which is exactly the property that makes the harvest cheap.
- `9b64da827a` `ElementMapVersion` redefinition and `e9ea3d0d25` the
  1.0.X -> 1.1 migration. That is upstream's document-compatibility story
  for its own installed base, not ours.

## 6. Working order for the harvest

1. **Phase 0** -- this document. Done.
2. **Phase 2** -- revive the App topo-naming tests. `tests/src/App` has 3706
   lines that look present but are dead: they hang off the legacy
   `Tests_run` target with `ENABLE_DEVELOPER_TESTS=OFF`, were last touched
   2023-09, and reference `Data::ELEMENT_MAP_PREFIX`, which the fork defines
   nowhere. Give them their own executable, as `DeferredLoad_tests_run` and
   the two suites beside it already do.
3. **Phase 3** -- port upstream's
   `tests/src/Mod/Part/App/TopoShapeExpansion.cpp` (3472 lines) through this
   map and run it against the fork's `TopoShapeEx.cpp`. Failures, triaged
   with the section 4 filter, are the real bug list.
4. **Phase 4** -- apply the harvested fixes. The three in section 4 marked
   APPLIES are confirmed and can go in without waiting for the suite;
   `41d1aed844` is standalone and is the cheapest first.

## 7. Data::ElementMap is opaque here, and stays that way

Upstream declares `class ElementMap` in `src/App/ElementMap.h`, so anything
can construct one. This fork declares only `class ElementMap;` there and
defines the class -- about 950 lines, lines 250 to 1202 -- inside
`src/App/ElementMap.cpp`. Nothing outside that translation unit can name the
type; every user goes through `ComplexGeoData`.

**Ruling, 2026-08-25: the internals stay out of the header.** Moving ~950
lines of a hot implementation class into a widely included header, to serve
tests, was rejected. It is an encapsulation this fork means to keep.

That is an encapsulation difference, not a naming one, and the name map does
not translate it. It has two consequences for the harvest:

- Any ported hunk that names `Data::ElementMap` outside `ElementMap.cpp`
  needs rerouting through `ComplexGeoData`'s public API first.
- Upstream's `tests/src/App/ElementMap.cpp` constructs the map directly in
  every case, because upstream moved `setElementName` and the hasher *onto*
  the map. Here `setElementName` is still `ComplexGeoData`'s -- upstream's own
  header says so: "The original function was in the context of
  ComplexGeoData, which provided `Tag` access, now you must pass in
  `long masterTag` explicitly."

### How the suite was revived anyway

Rerouting turned out to cover the whole thing. Every member the upstream
suite touches has a public `ComplexGeoData` wrapper that is a thin
pass-through to the `ElementMap` method of the same job, so the coverage
lands where it is meant to:

| upstream, on `ElementMap` | here, on `ComplexGeoData` |
|---------------------------|---------------------------|
| `setElementName(idx, name, masterTag, sids, overwrite)` | `setElementName(idx, name, sids, overwrite)`, tag taken from `Tag` |
| `find(IndexedName)` | `getMappedName` |
| `find(MappedName)` | `getIndexedName` |
| `findAll` | `getElementMappedNames` |
| `size` | `getElementMapSize` |
| `getAll` | `getElementMap` |
| `erase(MappedName)` | `eraseElementName(MappedName)` |
| `erase(IndexedName)` | `eraseElementName(IndexedName)` |
| `addChildElements(tag, children)` | `setMappedChildElements(children)` |
| `getChildElements` | `getMappedChildElements` |
| `hasChildElementMap` | same name |
| `hashChildMaps(tag)` | `hashChildMaps()` |
| `encodeElementName(type, name, ss, sids, masterTag, postfix, tag)` | `encodeElementName(type, name, ss, sids, postfix, tag)` |
| `elementMap->hasher` | the `Hasher` member of `ComplexGeoData` |
| `Data::ElementMap::MappedChildElements` | `Data::MappedChildElements`, same seven fields in the same order |

The suite's `LessComplexPart` holder becomes a small concrete
`ComplexGeoData` subclass instead of a class holding an `ElementMapPtr`,
which is closer to how the map is really used.

**All 16 of upstream's cases port and pass**, including
`mimicSimpleUnion`, which asserts the exact encoded name
`Face6;:M2;FUS;:H1:8,F` -- so this fork's encoder agrees with upstream's
expectation character for character.

### eraseElementName

One case, `eraseMappedName`, needed an API that did not exist. It calls
`ElementMap::erase(const MappedName&)` to drop one of an element's several
mapped names, and `ComplexGeoData` had no public route to that: passing an
empty `MappedName` to `setElementName` erases the whole indexed name, and
`setElementMap` rebuilds the map rather than erasing from it.

`ComplexGeoData::eraseElementName` was added to close the gap, in two
overloads mirroring `ElementMap::erase`:

    bool eraseElementName(const MappedName & name);     // just that name
    bool eraseElementName(const IndexedName & element); // all of the element's

Both return whether anything was found, and **both flush the element map
first**. That matters: an unflushed `TopoShape` can be carrying its map in the
cache rather than in `_elementMap`, and `flushElementMap()` is what installs
it, so erasing before the flush would either do nothing or be undone when the
map finally arrives. The read accessors -- `getMappedName`,
`getElementMappedNames` -- already flush for the same reason.

The Python binding takes one string and splits on what it is: an element name
such as `Edge1` erases the whole element, anything else is treated as a mapped
name and erases only itself. That is the same split as the two C++ overloads.

**Trap in making that split.** `IndexedName`'s constructor that takes the list
of element types defaults to `allowOthers=true`, which means it accepts any
bare word as a type it has not seen before. So `IndexedName("SECOND", types)`
is *not* null, and a mapped name called `SECOND` parses as an indexed name. A
binding written the obvious way sends it down the indexed branch, the lookup
finds nothing, and the erase silently reports `False` while the name is still
in the map. Pass `allowOthers=false` so only a type the shape actually has
counts. `ComplexGeoData::getElementName`, the general-purpose name guesser,
uses the permissive default, which is why the binding does not route through
it. Covered by `parttests/ElementNameTest.py`.

`setElementName`'s own erase path -- passing an empty `MappedName` -- now
routes through `eraseElementName` too, so it flushes as well. It did not
before, which meant that on a shape whose map was still deferred in the cache
it silently erased nothing: `_elementMap` was null, the `if (_elementMap)`
guard skipped the erase, and the element reappeared as soon as anything
flushed. Reproducible in three lines -- take `box.Edges[0]` from a box with
mapped names, erase on it without reading anything first, and the map still
has every entry. Pinned by `testEraseOnADeferredMapStillErases`, which fails
without the fix.

The Python binding for `setElementName` now honours the `None` its docstring
has always promised. Its argument format was `"s|ssOOi"`, and `s` rejects
`None` with a `TypeError`, so only the empty string worked; it is `"s|zzOOi"`
now, for `name` and `postfix` both, matching the documented signature.

A removal there does *not* go through the encoder. Handing an empty name to
`encodeElementName` is harmless with the default arguments -- it returns early
when there is no postfix and no tag -- but given either one it appends them
and hands back a real name, so the call would quietly *create* a mapping
instead of removing one. The binding erases outright instead. Pinned by
`testEraseByNoneIgnoresPostfixAndTag`, which fails without that branch.

## 8. Phase 3 result: the harvest list

Upstream's `tests/src/Mod/Part/App/TopoShapeExpansion.cpp` (3472 lines,
87 cases) is ported to `tests/src/Mod/Part/App/TopoShapeEx.cpp` and builds as
`TopoShapeEx_tests_run`. **73 cases pass against this fork's TopoShapeEx.cpp
unmodified.** That is the first coverage the file has ever had.

The translation cost was small, as section 1 predicted: the 34-name method
map, `findSubShapesWithSharedVertex` -> `searchSubShape`,
`makeShapeWithElementMap` -> `makESHAPE`, and dropping the
`TopoShapeMapper.h` include because this fork keeps `MapperMaker`,
`MapperHistory` and `TopoShape::Mapper` in `TopoShape.h` already.

### A second class of divergence: enums that were bools

Upstream replaced several of this fork's `bool` parameters with scoped enums,
and says so in its own header comments ("This replaces a boolean parameter in
the original Toponaming branch by realthunder"). Each enumerator maps back to
a bool:

| upstream enumerator | this fork |
|---------------------|-----------|
| `SingleShapeCompoundCreationPolicy::returnShape` / `::forceCompound` | `force` = `false` / `true` |
| `HistoryTraceType::stopOnTypeChange` / `::followTypeChange` | `sameType` = `true` / `false` (note the inversion) |
| `MapElement::noMap` / `::map` | `mapElement` = `false` / `true` |
| `LinearizeFace::noFaces` / `::linearizeFaces` | `face` = `false` / `true` |
| `LinearizeEdge::noEdges` / `::linearizeEdges` | `edge` = `false` / `true` |
| `IsSolid::notSolid` / `::solid` | `isSolid` = `false` / `true` |
| `IsRuled::notRuled` / `::ruled` | `isRuled` = `false` / `true` |
| `IsClosed::notClosed` / `::closed` | `isClosed` = `false` / `true` |
| `MakeSolid::noSolid` / `::makeSolid` | `makeSolid` = `false` / `true` |
| `ChamferType::equalDistance` | the default `asAngle` = `false` |

`Data::TraceCallback` is also a member typedef of `ComplexGeoData` here, not a
namespace-level name.

Nine of those ten enums do not exist in this fork at all. `HistoryTraceType`
is the exception, and it is adopted only halfway: `TopoShape.h` line 115
declares it and `TopoShapeCache.h`'s `ShapeRelationKey` takes it, but
`TopoShape::cacheRelatedElements` and `getRelatedElementsCached` still take
`bool sameType`. So the type is in the tree while the call sites that would
use it are not. Worth tidying, in one direction or the other, but it is not
part of the harvest.

### Not portable at all

- `replaceElementShape` and `removeElementShape` have no counterpart. This
  fork's `replaceShape` / `removeShape` take and return `TopoDS_Shape` and
  keep no element map. Those two cases are dropped, not disabled -- there is
  nothing here to call. **A feature gap, not a rename.**

### The 14 findings, and the phase 4 triage of them

Each was a `DISABLED_` case in the suite. **All fourteen have now been through
the section 4 filter.** Four were genuine fork defects and are fixed; the rest
are not ours to port.

Method: for each case, the fork's implementation was compared against upstream's
current text *and* against the text upstream had in the commit that first
transferred that function in from this fork. A difference that upstream
introduced at transfer time is upstream's, and skipped. A difference upstream
introduced *later*, over text that still matches ours, is a real fix, and is
ported.

#### Fixed -- real fork defects (4)

| case | defect | upstream |
|------|--------|----------|
| `makELoftRejectsCoincidentProfiles` | no guard at all against identical loft profiles; a raw `StdFail_NotDone` escaped | `9ee2c74545` (#5855), as later refined by their PR #29982 -- the naive centre-of-gravity test is too strict and rejects concentric squares |
| `makERuledSurfaceWires` | the automatic-orientation test sampled each curve at its first and last parameter. On a **closed** curve those are the same point, so the vector between them is zero and the reversal decision was noise: area 2.02 instead of 4 | `3a6f70946a` (#16013), pre-fix text byte-identical to ours |
| `makEShellIntersecting` | checked whether the sewing result *had* a shell sub-shape. A compound of shells does, so `makEShell(silent=false)` returned a compound and raised nothing | upstream's later fix over `9f3d6543c6`, byte-identical to ours |
| `makEEvolve` | `JoinType::Arc` mapped to `GeomAbs_Tangent` and `Tangent` fell through to `Arc` -- swapped. OCCT rejects anything above `GeomAbs_Arc`, so every default-argument evolve threw a bare `Standard_NotImplemented` | transferred in with the same defect as `110f3e000d`, corrected later |

Note that `makERuledSurfaceWires`'s **element names fell into line by themselves**
once the surface was built the right way round. A naming mismatch is not
necessarily a naming bug.

#### Skipped -- upstream's difference, not ours (5)

| case | why |
|------|-----|
| `getSubTopoShapeByEnum` | we raise `Base::ValueError` for an out-of-range index, upstream `Base::IndexError`. Upstream wrote `IndexError` in `76df39e99d`, the very commit that transferred the function in, and never changed it. **Expectation adapted, case enabled.** |
| `getSubTopoShapeByStringNames` | same root cause; the same test already expects our `ValueError` for an invalid name. **Expectation adapted, case enabled.** |
| `makERevolve` | our `makERevolve` ends with `fix()`, added because `BRepPrimAPI_MakeRevol` can produce a surface with reversed parameters (realthunder/FreeCAD#559). Upstream has no such call, and that `fix()` is what stamps the extra `;:M;MAK` step. A deliberate fork delta. |
| `makEBSplineFace` | the two implementations genuinely differ. Ours does geometric edge matching and transfers names through `makESHAPE` with `aFace(Tag, Hasher, ..)`; upstream's maps by index and ends in `setElementComboName` with `aFace(0, Hasher, ..)`, which is why its expectation carries no tag postfixes at all despite tagged inputs. Upstream's own source marks that path `TODO: Is this correct?`. |
| `makEOffset2D` | the input wire is a rectangle, so it has four edges. Our map names all four plus four vertices; upstream's expectation names two edges and two vertices and carries a doubled `;OFF` step. **Ours is the more complete map.** Upstream's only later change to this function -- collecting face wires with `getSubTopoShapes` instead of `splitWires` -- is not on the path a wire input takes. |

#### Not a naming bug -- a separate defect (2)

`makESlice` and `makESlices` are byte-equivalent to upstream's, and so is
`CrossSection::slice` where the work actually happens. Reproduced outside the
test entirely:

    Part.makeBox(1,1,1).slice(Vector(1,0,0), 0.5)   -> 0 wires
    Part.makeBox(2,2,2).slice(Vector(1,0,0), 1.0)   -> 1 wire

**`Part.slice()` returns nothing for any box of size 1 or less, at any offset,
while size 2 and above works.** It is scale dependent, not offset dependent, and
a translated unit box fails too. This is a live, user-visible defect in cross
sections of small shapes -- most likely an OCCT 8.0.1 change, since upstream's
CI captured these expectations on 7.x -- and it wants its own investigation
rather than being carried as a topo-naming finding.

#### Still open -- ours, but not explained yet (3)

`setElementComboNameCompound`, `makESolid` and `makERuledSurfaceEdges` all show
this fork producing a poorer element map than upstream: a bare `Edge1` where
upstream has `Edge1;:H,E`, 52 bare entries where upstream distinguishes the two
shells of a compound with `;:H,E` and `;:C1;:H:4,E`, and an empty map where
upstream names nine elements.

**No upstream fix applies to any of them.** `makESHAPE`, `mapSubElement`,
`encodeElementName`, `setElementName`, `makECompound`, `makESolid` and
`makERuledSurface` were each diffed against upstream and are equivalent modulo
the renames in section 1 and the enum table above. So the divergence is not a
missing upstream fix; it is something in our own naming path.

The prime suspect is the **deferred element map, which is fork-only** --
`hasPendingElementMap()`, `_ParentCache`, `_Cache->cachedElementMap` and a real
`TopoShape::flushElementMap()`, where upstream's `flushElementMap()` is an empty
stub. Note that `flushElementMap()`'s `_ParentCache` branch re-maps through
`self.mapSubElement(parent)` with **no op string**, which is exactly the shape of
"names arrive without their postfixes". That is where to start.

Re-enable a case by deleting its `DISABLED_` prefix, together with the fix,
and not before.

See also [Backport772.md](./Backport772.md) for the sibling policy on the
frozen OCCT 7.7.2 branch, and [UpstreamCoreSync.md](./UpstreamCoreSync.md)
for the general "adopt the name, keep an alias" rule that this document
deliberately does *not* follow for element naming.
