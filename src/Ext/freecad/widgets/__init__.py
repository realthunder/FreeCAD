# SPDX-License-Identifier: LGPL-2.1-or-later
# ***************************************************************************
# *   Copyright (c) 2026 FreeCAD Project Association                        *
# *                                                                         *
# *   This file is part of FreeCAD.                                         *
# *                                                                         *
# *   FreeCAD is free software: you can redistribute it and/or modify it    *
# *   under the terms of the GNU Lesser General Public License as           *
# *   published by the Free Software Foundation, either version 2.1 of the  *
# *   License, or (at your option) any later version.                       *
# *                                                                         *
# *   FreeCAD is distributed in the hope that it will be useful, but        *
# *   WITHOUT ANY WARRANTY; without even the implied warranty of            *
# *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU     *
# *   Lesser General Public License for more details.                       *
# *                                                                         *
# *   You should have received a copy of the GNU Lesser General Public      *
# *   License along with FreeCAD. If not, see                               *
# *   <https://www.gnu.org/licenses/>.                                      *
# *                                                                         *
# ***************************************************************************

"""The host side of the sandbox's forms (docs/Sandbox.md 7.3, U3).

A script in the sandbox guest builds its form with ipywidgets; each
widget is a MODEL that opens a comm to "the front end" and syncs its
state as key-value diffs.  Here the front end is FreeCAD: the guest's
comm traffic arrives through the `gui.comm` bridge op (src/Gui/
SandboxGui.cpp) at this manager, which keeps one `Model` per comm, and
what the user does in a rendered view goes back the same way, as an
`update` or a `custom` message on the guest's comm manager (a guest
proxy the guest registered through `gui.comm.manager`).

This module knows the protocol, not the toolkit: `freecad.widgets.qt`
renders the models with Qt (the first manager, 7.4); a manager over
bgfx renders the same models later.  Only model names a renderer knows
are drawn; an unknown one is a labeled placeholder; a widget package's
own JavaScript is never loaded.

    manager().comm_open(id, target, data, metadata, buffers)  guest -> host
    manager().comm_msg(id, data, buffers)
    manager().comm_close(id, data)
    manager().send_update(model, {key: value})               host -> guest
    manager().send_custom(model, content, buffers)
    manager().show(id, title, where) / hide(id)              the views
"""

import FreeCAD

MODEL_REF = "IPY_MODEL_"
WIDGET_TARGET = "jupyter.widget"


def _put_buffers(state, buffer_paths, buffers):
    """Restore binary buffers into `state` at `buffer_paths` (the widget
    protocol splits them out of the JSON part)."""
    for path, buf in zip(buffer_paths or [], buffers or []):
        obj = state
        for key in path[:-1]:
            obj = obj[key]
        obj[path[-1]] = buf


class Model:
    """One widget model: its comm id, synced state and live views."""

    __slots__ = ("id", "state", "views", "closed", "shown")

    def __init__(self, comm_id, state):
        self.id = comm_id
        self.state = state
        self.views = []
        self.closed = False
        self.shown = None

    @property
    def name(self):
        return self.state.get("_model_name", "")

    @property
    def ref(self):
        return MODEL_REF + self.id

    def get(self, key, default=None):
        return self.state.get(key, default)

    def __repr__(self):
        return "<Model %s %s>" % (self.name, self.id[:8])


class Manager:
    """The widget models the guest has opened, and the way back to it."""

    def __init__(self):
        self.models = {}
        self.dispatcher = None
        self.stats = {"open": 0, "msg": 0, "close": 0, "sent": 0, "custom": 0}
        self.log = None  # a list to record traffic into (the gate)

    # -- the guest's comm manager (a stand-in) ------------------------

    def set_dispatcher(self, standin):
        """The guest registered its comm manager.  A second registration
        is a new guest (a reset): the old guest's models are gone."""
        if self.dispatcher is not None and standin is not self.dispatcher:
            self.reset()
        self.dispatcher = standin

    def reset(self):
        for model in list(self.models.values()):
            self._close_model(model)
        self.models.clear()
        self.dispatcher = None

    # -- guest -> host ---------------------------------------------------

    def comm_open(self, comm_id, target_name, data, metadata=None, buffers=None):
        self.stats["open"] += 1
        self._record("open", comm_id, target_name, data)
        if target_name != WIDGET_TARGET:
            return False
        if not isinstance(data, dict):
            raise TypeError("comm_open: data is not a dict")
        state = dict(data.get("state") or {})
        _put_buffers(state, data.get("buffer_paths"), buffers)
        old = self.models.get(comm_id)
        if old is not None:
            self._close_model(old)
        self.models[comm_id] = Model(comm_id, state)
        return True

    def comm_msg(self, comm_id, data, buffers=None):
        self.stats["msg"] += 1
        self._record("msg", comm_id, data)
        model = self.models.get(comm_id)
        if model is None or not isinstance(data, dict):
            return False
        method = data.get("method")
        if method == "update":
            state = dict(data.get("state") or {})
            _put_buffers(state, data.get("buffer_paths"), buffers)
            changed = set()
            for key, value in state.items():
                if key not in model.state or model.state[key] != value:
                    changed.add(key)
                model.state[key] = value
            for view in list(model.views):
                view.update(changed)
        elif method == "echo_update":
            # the guest confirming a state the host sent: already applied
            pass
        elif method == "custom":
            for view in list(model.views):
                view.custom(data.get("content"), buffers or [])
        else:
            FreeCAD.Console.PrintWarning("freecad.widgets: unknown method %r\n" % (method,))
            return False
        return True

    def comm_close(self, comm_id, data=None):
        self.stats["close"] += 1
        self._record("close", comm_id, data)
        model = self.models.pop(comm_id, None)
        if model is None:
            return False
        self._close_model(model)
        return True

    # -- host -> guest ---------------------------------------------------

    def send_update(self, model, state):
        """The user changed `state` in a view: the guest's model follows."""
        for key, value in state.items():
            model.state[key] = value
        self.stats["sent"] += 1
        self._record("sent", model.id, state)
        self._dispatch("host_msg", model.id,
                       {"method": "update", "state": state, "buffer_paths": []}, [])

    def send_custom(self, model, content, buffers=None):
        self.stats["custom"] += 1
        self._record("custom", model.id, content)
        self._dispatch("host_msg", model.id, {"method": "custom", "content": content},
                       list(buffers or []))

    def _dispatch(self, hook, *args):
        if self.dispatcher is None:
            raise RuntimeError("no sandbox guest has registered a comm manager")
        try:
            getattr(self.dispatcher, hook)(*args)
        except ReferenceError:
            # the guest was reset under us: its models are gone
            self.reset()
            raise

    # -- models ------------------------------------------------------------

    def model(self, comm_id):
        return self.models.get(comm_id)

    def resolve(self, ref):
        """The model an `IPY_MODEL_<id>` reference names, else None."""
        if isinstance(ref, str) and ref.startswith(MODEL_REF):
            return self.models.get(ref[len(MODEL_REF):])
        return None

    def find(self, model_name=None, description=None):
        """The models of that name and/or description, in open order."""
        out = []
        for model in self.models.values():
            if model_name is not None and model.name != model_name:
                continue
            if description is not None and model.get("description") != description:
                continue
            out.append(model)
        return out

    # -- views -------------------------------------------------------------

    def show(self, comm_id, title=None, where="panel"):
        """Render the model tree rooted at `comm_id` -- as a task panel
        (`where="panel"`, the default) or a top-level window -- and
        return the toolkit's root object."""
        model = self.models.get(comm_id)
        if model is None:
            raise KeyError("no widget model %s" % comm_id)
        from . import qt

        return qt.show(self, model, title, where)

    def hide(self, comm_id):
        model = self.models.get(comm_id)
        if model is None or model.shown is None:
            return False
        from . import qt

        qt.hide(self, model)
        return True

    def _close_model(self, model):
        model.closed = True
        if model.shown is not None:
            from . import qt

            qt.hide(self, model)
        for view in list(model.views):
            view.close()

    def _record(self, kind, *args):
        if self.log is not None:
            self.log.append((kind,) + args)


_manager = None


def manager():
    """The process-wide manager (one guest, one front end)."""
    global _manager
    if _manager is None:
        _manager = Manager()
    return _manager
