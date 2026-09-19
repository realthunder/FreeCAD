#!/usr/bin/env python3
"""Analyze the expression corpus for the Phase 0 member audit.

Classifies each unique expression:
  - functions called (bare = engine builtin; dotted = module callable / method)
  - member chains (Obj.Prop.Sub...)
  - pseudo-properties (._shape etc, and leading _self/_app/_gui)
  - foreign-document references (Doc#Obj)
  - cell-only / literal-only arithmetic (the pure by-value world)
"""
import json, re, collections, os, sys

here = os.path.dirname(os.path.abspath(__file__))
recs = [json.loads(l) for l in open(os.path.join(here, "corpus.jsonl"))]

# dedupe identical expression strings (King x4 copies etc)
uniq = collections.Counter()
sample_src = {}
for r in recs:
    uniq[r["expr"]] += 1
    sample_src.setdefault(r["expr"], (r["file"], r["path"] or r.get("kind","")))

STR = re.compile(r"<<[^>]*>>")
CELLREF = re.compile(r"^[A-Z]{1,2}[0-9]{1,3}$")
NUM = re.compile(r"^[0-9.]+(e-?[0-9]+)?$", re.I)
IDENT = re.compile(r"[A-Za-z_][A-Za-z0-9_]*")
# find dotted chains and calls
CHAIN = re.compile(r"((?:[A-Za-z_][A-Za-z0-9_]*|(?<![\w)\]]))(?:\s*\.\s*[A-Za-z_][A-Za-z0-9_]*)+)\s*(\()?")
BARECALL = re.compile(r"(?<![\w.])([A-Za-z_][A-Za-z0-9_]*)\s*\(")
FOREIGN = re.compile(r"([A-Za-z_][A-Za-z0-9_]*)\s*#")
PSEUDO = set(["_self","_app","_gui","_shape","_pla","_matrix","_ref","_parent_"])

bare_funcs = collections.Counter()
dotted_calls = collections.Counter()   # full chain ending in a call
member_names = collections.Counter()   # every non-head segment of a chain (reads)
head_names = collections.Counter()
pseudo_uses = collections.Counter()
foreign_docs = collections.Counter()
categories = collections.Counter()
examples = collections.defaultdict(list)

def classify(expr):
    e = STR.sub("QS", expr)
    cats = set()
    if FOREIGN.search(e):
        cats.add("foreign-doc")
        for m in FOREIGN.finditer(e):
            foreign_docs[m.group(1)] += 1
    for m in BARECALL.finditer(e):
        bare_funcs[m.group(1)] += 1
        cats.add("func-call")
    has_chain = False
    for m in CHAIN.finditer(e):
        chain = re.sub(r"\s", "", m.group(1))
        parts = chain.split(".")
        if m.group(2):  # a call
            dotted_calls[chain] += 1
            cats.add("dotted-call")
            for p in parts[1:-1]:
                member_names[p] += 1
        else:
            has_chain = True
            for p in parts[1:]:
                member_names[p] += 1
        head_names[parts[0]] += 1
        for p in parts:
            if p in PSEUDO or p.startswith("_"):
                pseudo_uses[p] += 1
                cats.add("pseudo")
    if has_chain:
        cats.add("member-read")
    if not cats:
        # only bare idents / numbers / operators / strings
        toks = [t for t in IDENT.findall(e)]
        if all(CELLREF.match(t) or t in ("mm","cm","m","in","deg","rad","um","mil","kg","s","min") for t in toks):
            cats.add("pure-value")
        else:
            cats.add("bare-ident")   # single-segment identifier e.g. alias or property name
    return cats

for expr, n in uniq.items():
    cats = classify(expr)
    for c in cats:
        categories[c] += 1
        if len(examples[c]) < 5:
            examples[c].append(expr[:110])

print(f"total: {len(recs)}, unique: {len(uniq)}")
print("\n== categories (unique-expression counts, one expr may hit several) ==")
for c, n in categories.most_common():
    print(f"{n:6d}  {c}")
    for ex in examples[c][:3]:
        print(f"          e.g. {ex}")
print("\n== bare function calls ==")
for f, n in bare_funcs.most_common(40):
    print(f"{n:6d}  {f}()")
print("\n== dotted calls (module callables / methods) ==")
for f, n in dotted_calls.most_common(40):
    print(f"{n:6d}  {f}()")
print("\n== member names read (chain segments after head) ==")
for f, n in member_names.most_common(50):
    print(f"{n:6d}  .{f}")
print("\n== pseudo-property uses ==")
for f, n in pseudo_uses.most_common(20):
    print(f"{n:6d}  {f}")
print("\n== foreign-doc heads (Doc# style) ==")
for f, n in foreign_docs.most_common(15):
    print(f"{n:6d}  {f}#")
