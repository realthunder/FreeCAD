# SPDX-License-Identifier: LGPL-2.1-or-later
"""Arch.py does `from importers.importDAE import triangulate` at module
level; the real module needs Mesh and numpy, neither of which the
sandbox guest has.  Nothing in an Arch object's execute() triangulates
through it."""


def triangulate(shape):
    raise NotImplementedError("DAE triangulation is not available in the sandbox guest")
