# SPDX-License-Identifier: LGPL-2.1-or-later
"""``IPython.core.interactiveshell`` for the guest: the class, never an instance."""


class InteractiveShell:
    """No shell runs in the guest; ``initialized()`` says so."""

    @classmethod
    def initialized(cls):
        return False

    @classmethod
    def instance(cls, *args, **kwargs):
        raise RuntimeError("no IPython shell runs in the sandbox guest")
