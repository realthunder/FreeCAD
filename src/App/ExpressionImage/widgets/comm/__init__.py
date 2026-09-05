# SPDX-License-Identifier: LGPL-2.1-or-later
"""The Jupyter ``comm`` API for the sandbox guest, over the bridge.

ipywidgets talks to its front end through the ``comm`` package
(jupyter/comm): ``create_comm(...)`` opens a channel and
``get_comm_manager()`` routes what the front end sends back.  A kernel
replaces those two functions with its own; this module IS that
replacement for the guest (docs/Sandbox.md 7.3, Probe B), shaped like
comm 0.2.3 so ipywidgets runs unchanged:

- ``Comm.publish_msg`` is one bridge op, ``gui.comm``, carrying the
  message type, the comm id, the target, ``data``, ``metadata`` and the
  binary buffers (bytes cross the wire as they are).
- ``CommManager`` is what the host drives back: it registers itself as a
  guest proxy once (``FreeCADGui._register_comm_manager``), and the host's
  widget manager calls its ``host_msg`` / ``host_close`` / ``host_open``
  hooks, which rebuild the Jupyter-shaped message the comm's callbacks
  expect.

Nothing here draws.  The host renders whatever model names it knows
(freecad.widgets on the host); every op is the ``gui`` permission there.
"""

import uuid

__version__ = "0.2.3+fcx"
__all__ = ["__version__", "create_comm", "get_comm_manager", "BaseComm", "CommManager", "Comm"]


class BaseComm:
    """A channel to one front-end model (the API of comm.base_comm.BaseComm)."""

    def __init__(self, target_name="comm", data=None, metadata=None, buffers=None, comm_id=None,
                 primary=True, target_module=None, topic=None, _open_data=None, _close_data=None,
                 **kwargs):
        self.comm_id = comm_id if comm_id else uuid.uuid4().hex
        self.primary = primary
        self.target_name = target_name
        self.target_module = target_module
        self.topic = topic if topic else ("comm-%s" % self.comm_id).encode("ascii")
        self._open_data = _open_data if _open_data else {}
        self._close_data = _close_data if _close_data else {}
        self._msg_callback = None
        self._close_callback = None
        self._closed = True
        if self.primary:
            self.open(data=data, metadata=metadata, buffers=buffers)
        else:
            self._closed = False

    def publish_msg(self, msg_type, data=None, metadata=None, buffers=None, **keys):
        raise NotImplementedError("publish_msg Comm method is not implemented")

    def __del__(self):
        try:
            self.close(deleting=True)
        except Exception:
            pass

    def open(self, data=None, metadata=None, buffers=None):
        if data is None:
            data = self._open_data
        manager = get_comm_manager()
        manager.register_comm(self)
        try:
            self.publish_msg("comm_open", data=data, metadata=metadata, buffers=buffers,
                             target_name=self.target_name, target_module=self.target_module)
            self._closed = False
        except Exception:
            manager.unregister_comm(self)
            raise

    def close(self, data=None, metadata=None, buffers=None, deleting=False):
        if self._closed:
            return
        self._closed = True
        if data is None:
            data = self._close_data
        self.publish_msg("comm_close", data=data, metadata=metadata, buffers=buffers)
        if not deleting:
            get_comm_manager().unregister_comm(self)

    def send(self, data=None, metadata=None, buffers=None):
        self.publish_msg("comm_msg", data=data, metadata=metadata, buffers=buffers)

    def on_close(self, callback):
        self._close_callback = callback

    def on_msg(self, callback):
        self._msg_callback = callback

    def handle_close(self, msg):
        if self._close_callback:
            self._close_callback(msg)

    def handle_msg(self, msg):
        if self._msg_callback:
            self._msg_callback(msg)


def _buffer_list(buffers):
    if not buffers:
        return []
    return [b if isinstance(b, bytes) else bytes(b) for b in buffers]


class Comm(BaseComm):
    """The guest's comm: publishing is the ``gui.comm`` op."""

    def publish_msg(self, msg_type, data=None, metadata=None, buffers=None, **keys):
        import _fcx

        _fcx.op("gui.comm", 0, [msg_type, self.comm_id, keys.get("target_name", self.target_name),
                                data if data is not None else {},
                                metadata if metadata is not None else {},
                                _buffer_list(buffers)])


class CommManager:
    """The comm registry, and the hooks the host's widget manager calls."""

    def __init__(self):
        self.comms = {}
        self.targets = {}
        self._registered = False

    # -- what ipywidgets and BaseComm use

    def register_target(self, target_name, f):
        if isinstance(f, str):
            package, _, obj = f.rpartition(".")
            module = __import__(package or obj, fromlist=[obj] if package else [])
            f = getattr(module, obj) if package else module
        self.targets[target_name] = f

    def unregister_target(self, target_name, f=None):
        return self.targets.pop(target_name)

    def register_comm(self, comm):
        self._ensure_registered()
        self.comms[comm.comm_id] = comm
        return comm.comm_id

    def unregister_comm(self, comm):
        self.comms.pop(comm.comm_id)

    def get_comm(self, comm_id):
        return self.comms.get(comm_id)

    # -- the Jupyter-shaped handlers (a message dict, as a kernel gets it)

    def comm_open(self, stream, ident, msg):
        content = msg["content"]
        comm_id = content["comm_id"]
        target_name = content["target_name"]
        f = self.targets.get(target_name)
        comm = create_comm(comm_id=comm_id, primary=False, target_name=target_name)
        self.register_comm(comm)
        if f is None:
            comm.close()
            raise LookupError("no such comm target registered: %s" % target_name)
        try:
            f(comm, msg)
        except Exception:
            comm.close()
            raise

    def comm_msg(self, stream, ident, msg):
        comm = self.get_comm(msg["content"]["comm_id"])
        if comm is not None:
            comm.handle_msg(msg)

    def comm_close(self, stream, ident, msg):
        comm_id = msg["content"]["comm_id"]
        comm = self.comms.pop(comm_id, None)
        if comm is not None:
            comm._closed = True
            comm.handle_close(msg)

    # -- the host's entry points (the guest proxy hooks, docs/Sandbox.md 7.3)

    def host_msg(self, comm_id, data, buffers=None):
        self.comm_msg(None, None, {"content": {"comm_id": comm_id, "data": data},
                                   "buffers": _buffer_list(buffers)})

    def host_close(self, comm_id, data=None):
        self.comm_close(None, None, {"content": {"comm_id": comm_id, "data": data or {}},
                                     "buffers": []})

    def host_open(self, comm_id, target_name, data, buffers=None):
        self.comm_open(None, None, {"content": {"comm_id": comm_id, "target_name": target_name,
                                                "data": data},
                                    "buffers": _buffer_list(buffers)})

    def _ensure_registered(self):
        if self._registered:
            return
        import FreeCADGui

        FreeCADGui._register_comm_manager(self)
        self._registered = True


_comm_manager = None


def create_comm(*args, **kwargs):
    return Comm(*args, **kwargs)


def get_comm_manager():
    global _comm_manager
    if _comm_manager is None:
        _comm_manager = CommManager()
    return _comm_manager
