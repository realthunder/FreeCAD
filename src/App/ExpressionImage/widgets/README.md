# The guest side of the forms

Packed as the `fcx_widgets` bundled wheel (target `WidgetsSandboxWheel`,
docs/Sandbox.md 7.3 and 5.6) beside the unmodified `ipywidgets` and
`traitlets` wheels that `FREECAD_BUNDLED_WHEELS` names:

- `comm/` -- the Jupyter `comm` API (`create_comm`, `get_comm_manager`,
  `BaseComm`, `CommManager`) implemented over the bridge: publishing is the
  `gui.comm` op, and the manager registers itself once as a guest proxy so the
  host's widget manager (`freecad.widgets`, host side) can deliver messages
  back through its `host_msg` / `host_close` / `host_open` hooks.
- `IPython/` -- the three names ipywidgets imports from IPython, without the
  shell: `get_ipython()` is None, `display(widget)` shows the widget on the
  host, `InteractiveShell` is a class no one instantiates.  The real package
  is 12 wheels and 1.5 s of guest import for those names.

Nothing here draws; the host renders the model names it knows and labels
the rest as placeholders.
