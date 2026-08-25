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
| `erase(IndexedName)` | `setElementName(idx, MappedName())` -- an empty name erases |
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

**15 of upstream's 16 cases port and pass**, including
`mimicSimpleUnion`, which asserts the exact encoded name
`Face6;:M2;FUS;:H1:8,F` -- so this fork's encoder agrees with upstream's
expectation character for character.

The one case that does not port is `eraseMappedName`, which calls
`ElementMap::erase(const MappedName&)` to drop one of an element's several
mapped names. There is no public equivalent: an empty `MappedName` erases the
whole indexed name (that is `eraseIndexedName`, which is covered), and
`setElementMap` rebuilds the map rather than erasing from it. Left out, with
a comment in the file saying why.

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

### The 14 findings

Each is a `DISABLED_` case in the suite with a one-line note at its
definition. **None has been through the section 4 filter yet** -- that
triage is the first step of phase 4, and until it is done none of these
should be called a fork bug.

| case | symptom |
|------|---------|
| `makELoftRejectsCoincidentProfiles` | raw `StdFail_NotDone` escapes instead of `Base::CADKernelError`. This is upstream `9ee2c74545`, already independently confirmed as applicable in section 4 -- **the strongest candidate, and the one to fix first.** |
| `makEShellIntersecting` | a self-intersecting shell raises nothing where `Base::CADKernelError` is expected |
| `getSubTopoShapeByEnum` | an out-of-range subshape throws something other than `Base::IndexError` |
| `getSubTopoShapeByStringNames` | same, by string name |
| `makEEvolve` | an empty-description C++ exception escapes the operation |
| `makESlice` | the slice comes back empty (length 0, wrong shape type) |
| `makESlices` | the slices come back empty; the test then indexes an empty vector and aborts |
| `makERuledSurfaceEdges` | the result carries an entirely empty element map |
| `makERuledSurfaceWires` | area 2.02 where 4 is expected, and different element names |
| `makESolid` | no tag or child-map postfixes at all, and 24 edges where 12 are expected |
| `makERevolve` | one face is named `...;:M;MAK;:H2:7,F` where a plain `;:G;RVL` form is expected |
| `makEBSplineFace` | names carry full tag/hasher postfixes where plain `Edge1;BSF` forms are expected |
| `makEOffset2D` | names lack the trailing `;OFF;:H1:4,E` step, and there are 4 source edges where 2 are expected |
| `setElementComboNameCompound` | the name comes out without the `;:H,E` tag postfix |

The last five are element-map naming differences, which is exactly the shape
the section 4 filter exists for: upstream transcribed this fork's naming, so a
mismatch may be upstream's transcription rather than our defect. The first
seven -- wrong exception type, empty result, empty map -- are behavioural and
much less likely to be transcription artifacts.

Re-enable a case by deleting its `DISABLED_` prefix, together with the fix,
and not before.

See also [Backport772.md](./Backport772.md) for the sibling policy on the
frozen OCCT 7.7.2 branch, and [UpstreamCoreSync.md](./UpstreamCoreSync.md)
for the general "adopt the name, keep an alias" rule that this document
deliberately does *not* follow for element naming.
