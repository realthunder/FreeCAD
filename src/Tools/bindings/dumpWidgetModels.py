#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Dump the sandbox widget models as JSON (docs/Sandbox.md 7.18 (c)).

The guest's `freecad.widgets` models (src/App/ExpressionImage/widgets/
freecad/widgets/models.py and items.py) declare, per Qt class, the
comm state a model carries: the `q_` traits with their types and
defaults, the signals, and the CLASSES map from a Qt class name to the
model.  A backend that renders those models somewhere else -- the DOM
tier of docs/ThinClient.md -- needs that table without running the
guest, so this script reads the two files with `ast` and writes one
JSON document.  Nothing is imported: the host Python has neither
ipywidgets nor traitlets, and the guest's are in pyodide.

    dumpWidgetModels.py [--root DIR] [--out FILE]

  --root  the `freecad/widgets` package directory (default: the tree's)
  --out   the JSON file (default: stdout)

Shape:

    {"module": "freecad.widgets", "version": "0.1", "prefix": "q_",
     "classes": {"QActionModel": {"python": "QAction",
                                  "qtClass": "QAction",
                                  "base": "QWidgetModel",
                                  "properties": {"text": {"type": "string",
                                                          "default": ""}},
                                  "signals": {"triggered": ["bool"]}}},
     "qtClasses": {"QDialog": "QDialogModel"}}

A property's `type` is a JSON type name: Unicode -> "string", Bool ->
"bool", Int -> "int", Float -> "float", Dict -> "object", List ->
"list" (with "items" naming the element type when the trait says).
`allowNone` is set when the trait allows None.  A default that is not
a literal is recorded as null with "opaque": true rather than guessed.
Properties are the class's own declarations, plus those of bases that
are not models themselves (QAbstractItemView, QAbstractButton fold into
their model subclasses); a consumer walks `base` for the rest.  The output is deterministic (sorted keys) so a
rebuild that changes nothing leaves the file alone.
"""

import argparse
import ast
import json
import os
import sys

TRAIT_TYPES = {
    "Unicode": "string",
    "Bool": "bool",
    "Int": "int",
    "Float": "float",
    "Dict": "object",
    "List": "list",
    "Any": "any",
}

SIGNAL_TYPES = {
    "bool": "bool",
    "int": "int",
    "float": "float",
    "str": "string",
    "object": "any",
}


def _literal(node):
    """(ok, value) for a literal default; (False, None) otherwise."""
    try:
        return True, ast.literal_eval(node)
    except (ValueError, TypeError, SyntaxError):
        return False, None


def _trait(call):
    """The property record of a `Type(default, ...)` trait call, or None
    when `call` is not one."""
    if not isinstance(call, ast.Call) or not isinstance(call.func, ast.Name):
        return None
    kind = TRAIT_TYPES.get(call.func.id)
    if kind is None:
        return None
    rec = {"type": kind}
    default_node = None
    if kind == "list":
        # List(ItemType(), default_value=[...]) or List()
        if call.args:
            inner = _trait(call.args[0])
            if inner is not None:
                rec["items"] = inner["type"]
        rec["default"] = []
    elif kind == "object":
        rec["default"] = {}
    elif call.args:
        default_node = call.args[0]
    else:
        rec["default"] = {"string": "", "bool": False, "int": 0, "float": 0.0, "any": None}[kind]
    for kw in call.keywords:
        if kw.arg == "default_value":
            default_node = kw.value
        elif kw.arg == "allow_none":
            ok, value = _literal(kw.value)
            if ok and value:
                rec["allowNone"] = True
    if default_node is not None:
        ok, value = _literal(default_node)
        if ok:
            rec["default"] = value
        else:
            rec["default"] = None
            rec["opaque"] = True
    return rec


def _is_synced(call):
    """Whether the assignment's value is `Trait(...).tag(sync=True)`."""
    return (
        isinstance(call, ast.Call)
        and isinstance(call.func, ast.Attribute)
        and call.func.attr == "tag"
        and any(
            kw.arg == "sync" and _literal(kw.value) == (True, True) for kw in call.keywords
        )
    )


def _signal_args(call):
    """The argument type names of a `Signal(...)` declaration."""
    out = []
    for a in call.args:
        if isinstance(a, ast.Name):
            out.append(SIGNAL_TYPES.get(a.id, a.id))
        else:
            out.append("any")
    return out


def _base_name(node):
    if isinstance(node, ast.Name):
        return node.id
    if isinstance(node, ast.Attribute):
        return node.attr
    return None


def _scan(path, prefix):
    """Every class of `path`: name -> {bases, model, qtClass, props, signals}."""
    with open(path, "r", encoding="utf-8", newline="") as f:
        tree = ast.parse(f.read(), path)
    classes = {}
    for node in tree.body:
        if not isinstance(node, ast.ClassDef):
            continue
        rec = {
            "bases": [b for b in (_base_name(b) for b in node.bases) if b],
            "model": None,
            "qtClass": None,
            "props": {},
            "signals": {},
        }
        for stmt in node.body:
            if not isinstance(stmt, ast.Assign) or len(stmt.targets) != 1:
                continue
            target = stmt.targets[0]
            if not isinstance(target, ast.Name):
                continue
            name = target.id
            value = stmt.value
            if name == "qt_class":
                ok, v = _literal(value)
                if ok:
                    rec["qtClass"] = v
            elif name == "_model_name" and _is_synced(value):
                ok, v = _literal(value.func.value.args[0]) if value.func.value.args else (False, None)
                if ok:
                    rec["model"] = v
            elif name.startswith(prefix) and _is_synced(value):
                trait = _trait(value.func.value)
                if trait is not None:
                    rec["props"][name[len(prefix):]] = trait
            elif name == "layoutSpec" and _is_synced(value):
                trait = _trait(value.func.value)
                if trait is not None:
                    rec["props"]["layoutSpec"] = trait
            elif (
                isinstance(value, ast.Call)
                and isinstance(value.func, ast.Name)
                and value.func.id == "Signal"
            ):
                rec["signals"][name] = _signal_args(value)
        classes[node.name] = rec
    return classes, tree


def _class_map(tree):
    """The CLASSES dict: Qt class name -> Python class name."""
    for node in tree.body:
        if (
            isinstance(node, ast.Assign)
            and len(node.targets) == 1
            and isinstance(node.targets[0], ast.Name)
            and node.targets[0].id == "CLASSES"
            and isinstance(node.value, ast.Dict)
        ):
            out = {}
            for k, v in zip(node.value.keys, node.value.values):
                ok, key = _literal(k)
                name = _base_name(v)
                if ok and name:
                    out[key] = name
            return out
    return {}


def dump(root):
    prefix = "q_"
    models, mtree = _scan(os.path.join(root, "models.py"), prefix)
    items, _ = _scan(os.path.join(root, "items.py"), prefix)
    header = {}
    for node in mtree.body:
        if isinstance(node, ast.Assign) and len(node.targets) == 1:
            t = node.targets[0]
            if isinstance(t, ast.Name) and t.id in ("MODULE", "MODULE_VERSION", "PREFIX"):
                ok, v = _literal(node.value)
                if ok:
                    header[t.id] = v
    prefix = header.get("PREFIX", prefix)
    everything = dict(items)
    everything.update(models)

    def model_of(pyname, seen=()):
        """The model name a Python class answers to: its own, else the
        nearest base's."""
        rec = everything.get(pyname)
        if rec is None or pyname in seen:
            return None
        if rec["model"]:
            return rec["model"]
        for b in rec["bases"]:
            m = model_of(b, seen + (pyname,))
            if m:
                return m
        return None

    def own(pyname, key, seen=()):
        """The class's `key` declarations plus those of the bases that
        have no model of their own (QAbstractItemView, QAbstractButton:
        a model class's ancestry folds into it up to the nearest base
        that is a model itself)."""
        rec = everything.get(pyname)
        if rec is None or pyname in seen:
            return {}
        out = {}
        for b in rec["bases"]:
            base = everything.get(b)
            if base is not None and not base["model"]:
                out.update(own(b, key, seen + (pyname,)))
        out.update(rec[key])
        return out

    classes = {}
    for pyname, rec in everything.items():
        if not rec["model"]:
            continue
        base = None
        for b in rec["bases"]:
            base = model_of(b)
            if base:
                break
        classes[rec["model"]] = {
            "python": pyname,
            "qtClass": rec["qtClass"],
            "base": base,
            "properties": own(pyname, "props"),
            "signals": own(pyname, "signals"),
        }
    qt_classes = {}
    for qt, pyname in _class_map(mtree).items():
        model = model_of(pyname)
        if model:
            qt_classes[qt] = model
    return {
        "module": header.get("MODULE", "freecad.widgets"),
        "version": header.get("MODULE_VERSION", ""),
        "prefix": prefix,
        "classes": classes,
        "qtClasses": qt_classes,
    }


def main(argv=None):
    here = os.path.dirname(os.path.abspath(__file__))
    default_root = os.path.normpath(
        os.path.join(here, "..", "..", "App", "ExpressionImage", "widgets", "freecad", "widgets")
    )
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--root", default=default_root)
    ap.add_argument("--out", default=None)
    args = ap.parse_args(argv)
    text = json.dumps(dump(args.root), indent=1, sort_keys=True) + "\n"
    if args.out:
        # only rewrite a changed file: CMake's dependents stay quiet
        if os.path.exists(args.out):
            with open(args.out, "r", encoding="utf-8") as f:
                if f.read() == text:
                    return 0
        with open(args.out, "w", encoding="utf-8", newline="\n") as f:
            f.write(text)
    else:
        sys.stdout.write(text)
    return 0


if __name__ == "__main__":
    sys.exit(main())
