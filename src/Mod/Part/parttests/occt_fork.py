# Skips for the tests of what the realthunder OCCT fork fixes or adds, when
# the OCCT loaded at run time is another one -- upstream's, as FreeCAD can
# link it (FCAD_DEV_OCCT_BIN on Windows). Such a test fails or hangs there for
# want of the fork, not for a fault of FreeCAD's.
#
# The probe is the one FreeCAD itself uses to find the fork at run time: a C
# function of the fork's toolkits, looked up by name (Part.hasOCCTFunction).
# A test of one of the fork's functions asks for that function; a test of one
# of its fixes to an algorithm, which has no function of its own, asks for
# the fork (TKBRep's SetFuncShowTopoShape), and says which fix it needs.

import unittest

import Part

# The function that marks the fork, in every version of it
FORK_MARK = ("TKBRep", "SetFuncShowTopoShape")


def hasOCCTFork():
    """Whether the OCCT loaded is the realthunder fork"""
    return Part.hasOCCTFunction(*FORK_MARK)


def needsOCCTFork(fix):
    """Skips a test of a fix of the fork's (fix: which, as the skip says)
    on any other OCCT"""
    return unittest.skipUnless(hasOCCTFork(), "needs the OCCT fork: " + fix)


def needsOCCTFunction(toolkit, name):
    """Skips a test of the fork's C function name of toolkit on an OCCT
    without it"""
    return unittest.skipUnless(
        Part.hasOCCTFunction(toolkit, name), "needs the OCCT fork's %s (%s)" % (name, toolkit)
    )
