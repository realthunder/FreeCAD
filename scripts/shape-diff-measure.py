# shape-diff-measure.py : docs/TransactionLog.md sec 32.4 step 1 and sec 32.5 -- how the faces of two states of
# one object's shape pair up after the edits a user makes: by name, by name once the steps only one side has are
# taken out of it, by geometry, by the faces around. A measurement, not a check; it prints "SD ..." lines.
#
#   SD_FILE=model.FCStd [SD_BODY=Body] [SD_DUMP=dir] FreeCADCmd -c "exec(open('scripts/shape-diff-measure.py').read())"
#
# SD_FILE: a file with a PartDesign body of at least six features, one of them a pocket of a sketch of four or
# more elements (the measurement of sec 32.5 is of scanner.FCStd, sec 27.52). The file is copied, not written.
# SD_DUMP: a folder to write every pair's two names into, spelled out and canonical.
import FreeCAD, Part, os, shutil, tempfile, time, traceback, functools
from FreeCAD import Vector as V
print = functools.partial(print, flush=True)
BODY = os.environ.get("SD_BODY", "Body")

def faces(shape):
    """mapped name -> indexed name, of the faces that have a name; and how long the asking took"""
    t = time.time()
    rm = shape.ElementReverseMap
    out = {}
    for idx, name in rm.items():
        if idx.startswith("Face"):
            out[name] = idx
    return out, time.time() - t

def same(fa, fb):
    """the cheap test of one geometry: area, centre and box"""
    if abs(fa.Area - fb.Area) > 1e-7 * max(1.0, fa.Area): return False
    if fa.CenterOfMass.distanceToPoint(fb.CenterOfMass) > 1e-6: return False
    a, b = fa.BoundBox, fb.BoundBox
    return abs(a.DiagonalLength - b.DiagonalLength) < 1e-6 and a.Center.distanceToPoint(b.Center) < 1e-6

import re
_plain = {}
def plain(name):
    """a hashed name spelled out through the string table"""
    if name in _plain: return _plain[name]
    h = doc.Hasher
    out = name
    for _ in range(5000):
        m = re.search(r"#([0-9a-f]+)", out)
        if not m: break
        try:
            data = h.getID(int(m.group(1), 16)).Data
        except Exception:
            data = "?" + m.group(1)
        out = out[:m.start()] + data + out[m.end():]
    _plain[name] = out
    return out

HOP = re.compile(r";:H-?([0-9a-f]*)(?::[0-9a-f]+)?,[A-Z]")
def tagsOf(texts):
    out = set()
    for t in texts:
        out.update(m.group(1) for m in HOP.finditer(t))
    return out

HOPF = re.compile(r";:H(-?)([0-9a-f]*)(?::[0-9a-f]+)?,([A-Z])")
def canonSeg(seg, one):
    """an operation's text: the names it lists are made canonical too, and sorted"""
    i = seg.find("(")
    if i < 0: return seg
    depth = 0; j = -1
    for k in range(i, len(seg)):
        if seg[k] == "(": depth += 1
        elif seg[k] == ")":
            depth -= 1
            if depth == 0: j = k; break
    if j < 0: return seg
    parts = []; depth = 0; last = i + 1
    for k in range(i + 1, j):
        c = seg[k]
        if c == "(": depth += 1
        elif c == ")": depth -= 1
        elif c == "|" and depth == 0:
            parts.append(seg[last:k]); last = k + 1
    parts.append(seg[last:j])
    return seg[:i] + "(" + "|".join(sorted(canon(x, one) for x in parts)) + ")" + canonSeg(seg[j + 1:], one)

def canon(text, one):
    """a spelled-out name with the steps of the objects in 'one' taken out of it. A name is a root and then
    steps 'operation;:Htag': the element as it was in the shape of 'tag', and what was done to it next -- by
    the object of the step after. So an object's step goes out as: its tag, and the operation before it."""
    depth = 0; at = []
    for c in text:
        at.append(depth)
        if c == "(": depth += 1
        elif c == ")": depth -= 1
    hops = [m for m in HOPF.finditer(text) if at[m.start()] == 0]
    if not hops: return text
    first = text[:hops[0].start()]
    k = first.find(";")
    root, op0 = (first, "") if k < 0 else (first[:k], first[k:])
    toks = []
    for i, m in enumerate(hops):
        op = op0 if i == 0 else text[hops[i - 1].end():m.start()]
        toks.append([op, m.group(1), m.group(2), m.group(3)])
    tail = text[hops[-1].end():]
    born = toks[0][2] in one        # made by an object of the one side: its steps stay
    new = []
    for op, sign, tag, ty in toks:
        if tag in one and not sign and new and not born:
            new[-1][0] = op
        else:
            new.append([op, sign, tag, ty])
    return root + "".join(canonSeg(op, one) + ";:H" + sign + tag + "," + ty for op, sign, tag, ty in new) + canonSeg(tail, one)

def neighbours(S):
    """indexed face -> the indexed faces it shares an edge with"""
    idx = {}
    for i, f in enumerate(S.Faces):
        idx[f.hashCode()] = "Face%d" % (i + 1)
    out = {}
    for e in S.Edges:
        fs = [idx.get(f.hashCode()) for f in S.ancestorsOfType(e, Part.Face)]
        for x in fs:
            for y in fs:
                if x != y and x and y:
                    out.setdefault(x, set()).add(y)
    return out

def compare(tag, A, B, live=None):
    """A, B: two shapes of one object. Pairs their faces in stages, each with what the one before left:
    1 one name; 2 one name once the steps only one side has are taken out of it; 3 one geometry; 4 the
    same paired faces around. live: the object, where B is its shape now (for the names' history)."""
    a, ta = faces(A); b, tb = faces(B)
    pairs = {}                       # A's indexed face -> B's
    how = {}
    # 1
    t = time.time()
    for n in set(a) & set(b):
        pairs[a[n]] = b[n]; how[a[n]] = 1
    t1 = time.time() - t + ta + tb
    n1 = len(pairs)
    partner = unchanged = changed = 0
    for x, y in pairs.items():
        fa = A.getElement(x); fb = B.getElement(y)
        if fa.isPartner(fb): partner += 1
        elif same(fa, fb): unchanged += 1
        else: changed += 1
    onlyA = [n for n in a if a[n] not in pairs]; usedB = set(pairs.values()); onlyB = [n for n in b if b[n] not in usedB]
    related = None
    if live is not None and onlyA:
        related = many = 0
        for n in onlyA:
            try: r = [x for x in Part.getRelatedElements(live, n) if x[1].startswith("Face")]
            except Exception: r = []
            if r: related += 1
            if len(r) > 1: many += 1
    # 2
    t = time.time()
    pa = {n: plain(n) for n in a}; pb = {n: plain(n) for n in b}
    one = (tagsOf(pa.values()) ^ tagsOf(pb.values())) - {""}
    n2 = clash = 0; agree = differ = 0
    if one and onlyA and onlyB:
        ca = {}; cb = {}
        for n in onlyA: ca.setdefault(canon(pa[n], one), []).append(n)
        for n in onlyB: cb.setdefault(canon(pb[n], one), []).append(n)
        for k, v in ca.items():
            w = cb.get(k)
            if not w: continue
            if len(v) != 1 or len(w) != 1: clash += 1; continue
            pairs[a[v[0]]] = b[w[0]]; how[a[v[0]]] = 2; n2 += 1
            # where the face is unchanged the geometry can say whether the name paired the right one
            r = B.searchSubShape(A.getElement(a[v[0]]), needName=True)
            if r:
                if b[w[0]] in [x[0] for x in r]: agree += 1
                else: differ += 1
    t2 = time.time() - t
    ch2 = sum(1 for x in pairs if how[x] == 2 and not same(A.getElement(x), B.getElement(pairs[x])))
    # 3
    t = time.time()
    usedB = set(pairs.values()); n3 = 0
    for n in a:
        if a[n] in pairs: continue
        r = B.searchSubShape(A.getElement(a[n]), needName=True)
        r = [x[0] for x in r if x[0] not in usedB]
        if len(r) == 1:
            pairs[a[n]] = r[0]; how[a[n]] = 3; usedB.add(r[0]); n3 += 1
    t3 = time.time() - t
    if os.environ.get("SD_DUMP") and one:
        inv = {v: k for k, v in a.items()}; invb = {v: k for k, v in b.items()}
        with open(os.path.join(os.environ["SD_DUMP"], tag.replace(" / ", "_").replace(" ", "") + ".txt"), "w") as f:
            for x, y in sorted(pairs.items(), key=lambda v: int(v[0][4:])):
                fa = A.getElement(x); fb = B.getElement(y)
                f.write("# %s area %.6f %.6f centre %s %s partner %s\n" % (x, fa.Area, fb.Area, fa.CenterOfMass, fb.CenterOfMass, fa.isPartner(fb)))
                f.write("%d %s %s\nA %s\nB %s\na %s\nb %s\n" % (how[x], x, y, pa[inv[x]], pb[invb[y]], canon(pa[inv[x]], one), canon(pb[invb[y]], one)))
    # 4
    t = time.time()
    n4 = 0
    restA = [x for x in a.values() if x not in pairs]; restB = [y for y in b.values() if y not in usedB]
    if restA and restB:
        na = neighbours(A); nb = neighbours(B)
        while True:
            back = {y: x for x, y in pairs.items()}
            sa = {}; sb = {}
            for x in restA:
                sa.setdefault(frozenset(pairs[q] for q in na.get(x, ()) if q in pairs), []).append(x)
            for y in restB:
                sb.setdefault(frozenset(q for q in nb.get(y, ()) if q in back), []).append(y)
            got = [(v[0], sb[k][0]) for k, v in sa.items() if len(k) >= 2 and len(v) == 1 and len(sb.get(k, ())) == 1]
            if not got: break
            for x, y in got:
                pairs[x] = y; how[x] = 4; n4 += 1
                restA.remove(x); restB.remove(y)
    t4 = time.time() - t
    # the geometry alone, for a shape with no names
    t = time.time(); foundAll = sum(1 for f in A.Faces if B.searchSubShape(f)); tall = time.time() - t
    print("SD %-20s faces %3d / %3d | 1 one name %3d (%d one TShape, %d same geometry, %d changed) | 2 steps of one side taken out +%d "
          "(%d changed; geometry agrees %d, not %d; several %d) | 3 one geometry +%d | 4 the faces around +%d | left %d / %d%s" % (
        tag, len(A.Faces), len(B.Faces), n1, partner, unchanged, changed, n2, ch2, agree, differ, clash, n3, n4, len(restA), len(restB),
        "" if related is None else " | getRelatedElements of the %d not paired by 1: %d, to several %d" % (len(onlyA), related, many)))
    print("SD %-20s   time (Python): 1 %.1f ms, 2 %.1f ms, 3 %.1f ms, 4 %.1f ms; geometry alone, all faces: %.1f ms, pairs %d; tags of one side %s" % (
        tag, t1 * 1e3, t2 * 1e3, t3 * 1e3, t4 * 1e3, tall * 1e3, foundAll, sorted(one)))
    # the same asked of Part::ShapeDiff (sec 32.6), which is what is built from this
    if hasattr(A, "diff"):
        import collections
        t = time.time(); d = A.diff(B); tc = time.time() - t
        by = collections.Counter(e[2] for e in d["Face"]); what = collections.Counter(e[3] for e in d["Face"])
        differ = sum(1 for e in d["Face"] if e[0] and e[1] and pairs.get(e[0]) != e[1])
        print("SD %-20s   Shape.diff: 1 %d, 2 %d, 3 %d, 4 %d, left %d / %d; same %d moved %d changed %d; pairs other than above %d; %.1f ms (%s)%s" % (
            tag, by["name"] + by["shape"], by["steps"], by["geometry"], by["around"], what["removed"], what["added"],
            what["same"], what["moved"], what["changed"], differ, tc * 1e3,
            ", ".join("%s %.1f" % (k, v * 1e3) for k, v in d["cost"].items() if v >= 5e-5),
            " (the whole: same)" if d["same"] else " (the whole: moved)" if d["moved"] else ""))
    inv = {v: k for k, v in a.items()}; invb = {v: k for k, v in b.items()}
    for x in restA[:2]: print("SD %-20s     left in A %s: %s" % (tag, x, pa[inv[x]][:300]))
    for y in restB[:2]: print("SD %-20s     left in B %s: %s" % (tag, y, pb[invb[y]][:300]))

try:
    d = tempfile.mkdtemp(prefix="sd-")
    path = os.path.join(d, "model.FCStd")
    shutil.copyfile(os.path.expanduser(os.environ["SD_FILE"]), path)
    doc = FreeCAD.openDocument(path)
    doc.UndoMode = 1
    t = time.time(); doc.recompute(None, True); print("SD recompute all %.2fs" % (time.time() - t))
    body = doc.getObject(BODY)
    feats = [o for o in body.Group if o.isDerivedFrom("PartDesign::Feature")]
    tip = body.Tip
    print("SD body %s: %d features, tip %s, faces %d" % (body.Name, len(feats), tip.Name, len(tip.Shape.Faces)))
    snaps = {}
    def snap(key):
        snaps[key] = tip.Shape      # a value of its own that shares the TShapes, not a deep copy
        return snaps[key]
    base = snap("base")
    compare("base / base again", base, tip.Shape)

    def edit(key, label, fn):
        doc.openTransaction(label)
        try:
            fn()
            t = time.time(); doc.recompute(); tr = time.time() - t
        finally:
            doc.commitTransaction()
        bad = [o.Name for o in body.Group if hasattr(o, "isValid") and not o.isValid()]
        print("SD --- %s: %s; recompute %.2fs; invalid %s" % (key, label, tr, bad))
        s = snap(key)
        compare("base / " + key, base, s, tip)
        doc.undo()
        doc.recompute()
        back = tip.Shape
        fa, _ = faces(base); fb, _ = faces(back)
        print("SD     undone: faces %d, names as the base %s" % (len(back.Faces), set(fa) == set(fb)))

    # (a) a parameter, early: the first pad's length
    first = feats[0]
    def a():
        first.Length = first.Length.Value + 1.0
    edit("a", "%s.Length + 1" % first.Name, a)

    # (a2) a parameter, late
    free = [o for o in feats if o.TypeId in ("PartDesign::Pad", "PartDesign::Pocket") and o.Type == "Length"
            and not [e for e in o.ExpressionEngine if e[0] == "Length"]]
    late = free[-1]
    def a2():
        late.Length = late.Length.Value + 0.5
    edit("a2", "%s.Length + 0.5" % late.Name, a2)

    # (d) a fillet's radius
    fillets = [o for o in feats if o.TypeId == "PartDesign::Fillet" and not o.ExpressionEngine]
    if fillets:
        def dd():
            fillets[0].Radius = fillets[0].Radius.Value * 1.5
        edit("d", "%s.Radius x 1.5" % fillets[0].Name, dd)

    # (b) a feature put in before the others: a hole through, after the 4th feature
    def b():
        after = feats[3]
        s = tip.Shape
        bb = s.BoundBox
        r = bb.DiagonalLength * 0.004
        p = None
        for i in range(1, 40):
            for j in range(1, 40):
                x = bb.XMin + bb.XLength * i / 40.0; y = bb.YMin + bb.YLength * j / 40.0
                ok = True
                for dx, dy in ((0, 0), (3 * r, 0), (-3 * r, 0), (0, 3 * r), (0, -3 * r)):
                    for z in (bb.ZMin + bb.ZLength * 0.1, bb.ZMin + bb.ZLength * 0.3):
                        q = V(x + dx, y + dy, z)
                        if not (s.isInside(q, 1e-6, False) and after.Shape.isInside(q, 1e-6, False)): ok = False
                if ok: p = V(x, y, 0); break
            if p: break
        sk = doc.addObject("Sketcher::SketchObject", "SdSketch")
        body.insertObject(sk, after, True)
        xy = [o for o in body.Origin.OutList if "XY" in o.Role][0]
        sk.Support = [(xy, "")]
        sk.MapMode = "FlatFace"
        sk.addGeometry(Part.Circle(V(p.x, p.y, 0), V(0, 0, 1), r), False)
        pk = doc.addObject("PartDesign::Pocket", "SdPocket")
        body.insertObject(pk, after, True)
        pk.Profile = sk
        pk.Type = 1
        pk.Midplane = True
        body.Tip = tip
        print("SD     hole of radius %.3f at (%.2f, %.2f) after %s; its base %s, the next one's base %s, tip %s" % (
            r, p.x, p.y, after.Name, pk.BaseFeature.Name if pk.BaseFeature else None, feats[4].BaseFeature.Name, body.Tip.Name))
    edit("b", "a hole put in after %s" % feats[3].Name, b)

    # (c) a sketch redrawn: one of its lines, then all of it
    sketch = None
    for o in feats:
        prof = getattr(o, "Profile", None)
        if prof and prof[0].TypeId == "Sketcher::SketchObject" and prof[0].GeometryCount >= 4:
            sketch = prof[0]; break
    def redraw(which):
        def fn():
            geo = list(sketch.Geometry)
            cons = [sketch.getConstruction(i) for i in range(len(geo))]
            for i in sorted(which, reverse=True):
                sketch.delGeometry(i)
            for i in sorted(which):
                sketch.addGeometry(geo[i], cons[i])
        return fn
    if sketch:
        edit("c1", "%s: its first line redrawn" % sketch.Name, redraw([0]))
        edit("c2", "%s: all %d redrawn" % (sketch.Name, sketch.GeometryCount), redraw(list(range(sketch.GeometryCount))))

    # (e) a feature taken out of the middle
    gone = feats[5]
    def e():
        body.removeObject(gone)
        doc.removeObject(gone.Name)
    try:
        edit("e", "%s removed" % gone.Name, e)
    except Exception:
        traceback.print_exc()

    # (g) one more circle in a sketch a pocket is cut by
    def g():
        target = None
        for o in feats:
            prof = getattr(o, "Profile", None)
            if o.TypeId == "PartDesign::Pocket" and prof and prof[0].TypeId == "Sketcher::SketchObject":
                for geo in prof[0].Geometry:
                    if geo.TypeId == "Part::GeomCircle":
                        target = (prof[0], geo); break
            if target: break
        sk, c = target
        sk.addGeometry(Part.Circle(c.Center + V(c.Radius * 0.3, 0, 0), V(0, 0, 1), c.Radius * 0.1), False)
        print("SD     a circle added to %s" % sk.Name)
    try:
        edit("g", "a circle more in a pocket's sketch", g)
    except Exception:
        traceback.print_exc()

    # two branches: one edit against another
    for x, y in (("a", "b"), ("a", "c1"), ("a", "c2"), ("b", "c2"), ("a2", "c2"), ("d", "c1"), ("b", "d"), ("a2", "b")):
        if x in snaps and y in snaps:
            compare("%s / %s" % (x, y), snaps[x], snaps[y])
    print("SD DONE")
except Exception:
    traceback.print_exc()
os._exit(0)
