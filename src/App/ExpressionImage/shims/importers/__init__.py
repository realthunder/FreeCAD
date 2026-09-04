# SPDX-License-Identifier: LGPL-2.1-or-later
"""BIM's importers package, as the sandbox guest sees it: nothing of the
file importers runs in the guest (they need Mesh, ifcopenshell, files).
Only what an Arch module imports at module level is here, see
importDAE."""
