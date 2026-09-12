# SPDX-License-Identifier: LGPL-2.1-or-later
"""The FeaturePython hook table.

This is the single statement of what hooks a document object's `Proxy` and
its view provider's `Proxy` may implement, and of the three things about a
hook that a generator can write but a function cannot: its name, where the
owner object goes in the argument tuple, and what a Python error does.

Two templates read it at build time through cog -- `FeaturePythonHookApp.cog.h`
in this directory and `FeaturePythonHookView.cog.h` in `src/Gui` -- and emit
one `App::PyHookDef` table plus the hook enum per side.  Nothing else is
generated: the call itself is `App::PyHookImp::callHook`, a function, and the
per-hook bodies are hand-written around it.  See `docs/ProxyChain.md` sec 3.

Adding a hook: add a row here, add the C++ body that calls `callHook` with the
generated enumerator, and add the override in the template header.  Both
generated headers land in the build tree; neither is committed.
"""

# Where the owner object -- the feature, or the view provider -- goes in the
# argument tuple the callable receives.
SELF_NONE = "None"      # never passed; the callable sees only its own arguments
SELF_MODERN = "Modern"  # passed first, and dropped in the old __object__ form
SELF_ALWAYS = "Always"  # passed first in both forms

# What a Python error other than NotImplementedError does.  NotImplementedError
# always means "not handled" and moves on; these are the real failures.
ERR_REPORT = "Report"              # report it; the call reports Failed
ERR_REPORT_THROW = "ReportThrow"   # report it, then throw the Base::PyException
ERR_THROW = "Throw"                # Base::PyException::ThrowException()


class Hook:
    """One hook: the Proxy attribute name, plus the four generated facts."""

    def __init__(self, name, self_arg=SELF_MODERN, guarded=True, error=ERR_REPORT,
                 notify=False, note=None):
        self.name = name
        self.self_arg = self_arg
        # guarded=False is the hook whose only check today is "is the callable
        # absent" -- it carries NO recursion guard, because a nested call is
        # the normal case (a property set inside onChanged).  Guarding those
        # would silently drop the nested call.
        self.guarded = guarded
        self.error = error
        # notify=True is a hook whose result nothing reads: the chain calls
        # EVERY element rather than stopping at the first that answers, and
        # only an explicit True from a ProxyExp element stops it before the
        # Proxy.  docs/ProxyChain.md sec 2.3.
        self.notify = notify
        self.note = note

    def enumerator(self, prefix="Hook"):
        return prefix + self.name[0].upper() + self.name[1:]

    def exp_name(self, prefix):
        """The name a ProxyExp element exposes this hook under."""
        return prefix + self.name[0].upper() + self.name[1:]


# The prefixes of docs/ProxyChain.md sec 2.2, ruled 2026-09-12.  Two are
# needed because a linked object is ONE Python namespace and three hook names
# (onChanged, editProperty, attach) exist on both sides.
APP_PREFIX = "exp"
VIEW_PREFIX = "expView"


# ---------------------------------------------------------------------------
# The App side: App::FeaturePythonImp, one row per DocumentObject virtual it
# lets the Proxy take over.  Order is the order the retired FC_PY_FEATURE_PYTHON
# X-macro had.
# ---------------------------------------------------------------------------
APP_HOOKS = [
    Hook("execute", SELF_MODERN, error=ERR_REPORT_THROW,
         note="False or NotImplementedError means not handled"),
    Hook("mustExecute", SELF_MODERN),
    Hook("skipRecompute", SELF_MODERN, error=ERR_THROW,
         note="absent means true -- the base decides alone"),
    Hook("onBeforeChange", SELF_MODERN, guarded=False, notify=True),
    Hook("onBeforeChangeLabel", SELF_ALWAYS, guarded=False,
         note="a returned string replaces the label and skips the base"),
    Hook("onChanged", SELF_MODERN, guarded=False, notify=True),
    Hook("onDocumentRestored", SELF_MODERN, notify=True),
    Hook("unsetupObject", SELF_MODERN, notify=True),
    Hook("getViewProviderName", SELF_ALWAYS),
    Hook("getSubObject", SELF_ALWAYS),
    Hook("getSubObjects", SELF_ALWAYS),
    Hook("getLinkedObject", SELF_ALWAYS),
    Hook("canLinkProperties", SELF_ALWAYS),
    Hook("allowDuplicateLabel", SELF_ALWAYS),
    Hook("redirectSubName", SELF_ALWAYS),
    Hook("canLoadPartial", SELF_ALWAYS),
    Hook("hasChildElement", SELF_ALWAYS),
    Hook("isElementVisible", SELF_ALWAYS),
    Hook("isElementVisibleEx", SELF_ALWAYS),
    Hook("setElementVisible", SELF_ALWAYS),
    Hook("getElementMapVersion", SELF_ALWAYS),
    Hook("editProperty", SELF_NONE,
         note="the odd one out: the App side passes only the property name"),
]


# ---------------------------------------------------------------------------
# The view side: Gui::ViewProviderFeaturePythonImp.  Most of these pass no
# owner at all -- the Proxy method's own `self` IS the view provider's proxy,
# and upstream never settled on passing vobj as well.  Order is the order the
# retired FC_PY_VIEW_OBJECT X-macro had.
# ---------------------------------------------------------------------------
VIEW_HOOKS = [
    Hook("getIcon", SELF_NONE),
    Hook("getExtraIcons", SELF_NONE),
    Hook("getToolTip", SELF_NONE),
    Hook("claimChildren", SELF_NONE),
    Hook("useNewSelectionModel", SELF_NONE),
    Hook("getElementPicked", SELF_NONE),
    Hook("getElement", SELF_NONE),
    Hook("getDetail", SELF_NONE),
    Hook("getDetailPath", SELF_NONE),
    Hook("getSelectionShape", SELF_NONE,
         note="declared and resolved, never called -- the C++ base answers"),
    Hook("setEdit", SELF_MODERN),
    Hook("unsetEdit", SELF_MODERN),
    Hook("setEditViewer", SELF_ALWAYS),
    Hook("unsetEditViewer", SELF_ALWAYS),
    Hook("doubleClicked", SELF_MODERN),
    Hook("iconMouseEvent", SELF_NONE),
    Hook("setupContextMenu", SELF_MODERN),
    Hook("attach", SELF_MODERN, notify=True),
    Hook("updateData", SELF_MODERN, guarded=False, notify=True,
         note="the owner here is the DOCUMENT OBJECT, not the view provider"),
    Hook("onChanged", SELF_MODERN, guarded=False, notify=True),
    Hook("startRestoring", SELF_NONE, notify=True,
         note="declared and resolved, never called"),
    Hook("finishRestoring", SELF_NONE, notify=True),
    Hook("onDelete", SELF_MODERN),
    Hook("canDelete", SELF_NONE),
    Hook("isShow", SELF_NONE),
    Hook("getDefaultDisplayMode", SELF_NONE),
    Hook("getDisplayModes", SELF_MODERN),
    Hook("setDisplayMode", SELF_NONE),
    Hook("canRemoveChildrenFromRoot", SELF_NONE),
    Hook("canDragObjects", SELF_NONE),
    Hook("canDragObject", SELF_NONE),
    Hook("dragObject", SELF_MODERN),
    Hook("canDropObjects", SELF_NONE),
    Hook("canDropObject", SELF_NONE),
    Hook("dropObject", SELF_MODERN, error=ERR_THROW),
    Hook("canDragAndDropObject", SELF_NONE),
    Hook("canDropObjectEx", SELF_NONE, error=ERR_REPORT_THROW),
    Hook("dropObjectEx", SELF_ALWAYS, error=ERR_THROW),
    Hook("canAddToSceneGraph", SELF_NONE),
    Hook("getDropPrefix", SELF_NONE),
    Hook("replaceObject", SELF_NONE),
    Hook("canReplaceObject", SELF_NONE),
    Hook("reorderObjects", SELF_NONE),
    Hook("canReorderObject", SELF_NONE),
    Hook("getLinkedViewProvider", SELF_NONE),
    Hook("editProperty", SELF_NONE),
]


def emit(cog, hooks, prefix, enum_name, table_name, side):
    """Write the enum and the PyHookDef table for one side."""
    cog.outl("// Generated from src/App/FeaturePythonHooks.py -- do not edit.")
    cog.outl("// %s hooks: %d." % (side, len(hooks)))
    cog.outl("")
    cog.outl("enum %s {" % enum_name)
    for hook in hooks:
        if hook.note:
            cog.outl("    // %s" % hook.note)
        cog.outl("    %s," % hook.enumerator())
    cog.outl("    HookCount,")
    cog.outl("};")
    cog.outl("")
    cog.outl("static const App::PyHookDef %s[] = {" % table_name)
    for hook in hooks:
        cog.outl('    {"%s", "%s", App::PyHookSelf::%s, %s, %s, App::PyHookError::%s},'
                 % (hook.name, hook.exp_name(prefix), hook.self_arg,
                    "true" if hook.guarded else "false",
                    "true" if hook.notify else "false", hook.error))
    cog.outl("};")
    cog.outl("")
    cog.outl("static_assert(sizeof(%s) / sizeof(%s[0]) == HookCount,"
             % (table_name, table_name))
    cog.outl('              "the hook table and the hook enum disagree");')
