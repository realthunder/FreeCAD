"""View-tier included-file blob suite (docs/FileBlobsManager.md §8).

In-FreeCAD driver run by file-blob-verify.sh (desktop leg) under xvfb;
creates its own documents. Covers the parts the headless suite
(src/Mod/Test/FileBlobs.py) cannot reach, i.e. everything that needs a
View3DInventor or a view provider:

- The embedded environment image (Render_PBREnvEmbed copying into
  Render_PBREnvImageData) survives save/restore. This is the property
  that forced base64 inlining before the manager owned the blobs: a
  View3D is serialized into a string embedded in GuiDocument.xml and
  replayed from memory after the archive is consumed.
- It saves as a hash reference, not inline base64.
- A view blob and an App-tier property holding the same image share one
  archive entry -- the collect broadcast spans both tiers.
- A dynamic PropertyFileIncluded on a view provider (the shape the
  TaskRenderSettings texture properties take) round-trips too.
- The restored document's blob store holds exactly the referenced
  content: the restore hold is cleared, but only for what nobody claimed.

Env: US_OUT (output dir, default this file's dir), US_RESULT (result
file, default <US_OUT>/blob_gui.txt).
"""
import hashlib
import os
import tempfile
import traceback
import zipfile

import FreeCAD
import FreeCADGui
from PySide.QtCore import QTimer
from PySide.QtGui import QColor, QImage

OUT = os.environ.get("US_OUT", os.path.dirname(os.path.abspath(__file__)))
RESULT = os.environ.get("US_RESULT", os.path.join(OUT, "blob_gui.txt"))
BLOB_DIR = "blobs"
BLOB_INDEX = "Content.xml"

results = []


def log(msg):
    print(msg)
    results.append(msg)


def check(name, ok, detail=""):
    log("ASSERT %s: %s%s" % (name, "PASS" if ok else "FAIL", (" " + detail) if detail else ""))
    return ok


def sha1(path):
    with open(path, "rb") as handle:
        return hashlib.sha1(handle.read()).hexdigest()


def read_bytes(path):
    with open(path, "rb") as handle:
        return handle.read()


def blob_entries(project):
    """Content entries only: the index describes them, it is not one of them."""
    names = zipfile.ZipFile(project).namelist()
    index = "%s/%s" % (BLOB_DIR, BLOB_INDEX)
    return [n for n in names if n.startswith(BLOB_DIR + "/") and n != index]


def gui_xml(project):
    return zipfile.ZipFile(project).read("GuiDocument.xml").decode("utf-8", "replace")


def stored_blobs(doc):
    blobdir = os.path.join(doc.TransientDir, BLOB_DIR)
    return sorted(os.listdir(blobdir)) if os.path.isdir(blobdir) else []


def make_image(path, color=(40, 120, 200)):
    """A real (if tiny) PNG, so the renderer can load what we embed."""
    image = QImage(8, 8, QImage.Format_RGB32)
    image.fill(QColor(*color))
    image.save(path, "PNG")
    return path


def active_view(doc):
    return FreeCADGui.getDocument(doc.Name).ActiveView


def view_provider(doc, name, tries=200):
    """The view provider of an object, once the view tier has caught up.

    View providers are restored lazily, in slices driven by the event loop
    (Gui::Document::runDeferredRestoreSlice), so ViewObject is None for a while
    after openDocument and asking for it straight away is a race. Measured: it
    is None immediately and present after a few slices, at schema 4 and at 5
    alike. Only a save flushes it synchronously, which is not what this suite
    wants to do before it has checked what was restored.
    """
    for _ in range(tries):
        obj = doc.getObject(name)
        if obj is not None and obj.ViewObject is not None:
            return obj.ViewObject
        FreeCADGui.updateGui()
    return None


def env_property(view):
    """The embedded-environment-image property of a view.

    View3DInventorViewer::initRenderProperties() creates the Render_* set only
    once a renderer backend has been selected (render cache mode 3), which
    needs a GPU context this suite does not have. The property is a plain
    dynamic one, so materialize it directly: what is under test is that a
    view-owned included file survives the save, not how it gets created.
    """
    if not hasattr(view, "Render_PBREnvImageData"):
        view.addProperty(
            "App::PropertyFileIncluded",
            "Render_PBREnvImageData",
            "Render",
            "Copy of the environment image stored in the document.",
        )
    return "Render_PBREnvImageData"


def run():
    tmp = tempfile.mkdtemp(prefix="fc_blob_gui_")
    try:
        image = make_image(os.path.join(tmp, "env.png"))
        image_hash = sha1(image)
        image_bytes = read_bytes(image)
        project = os.path.join(tmp, "viewblob.FCStd")

        # --- Embed the environment image on the view.
        doc = FreeCAD.newDocument("BlobGui")
        # Shared entries are this fork's format and a new document defaults to
        # upstream's cap of 4, which writes no blob entries at all. Without
        # this the archive assertions below test schema 4 and fail; the headless
        # suite opts in the same way (FileBlobs.py newDocument).
        doc.SaveSchemaVersion = 5
        view = active_view(doc)
        env_property(view)
        view.Render_PBREnvImageData = (image, "env.png")
        embedded = view.Render_PBREnvImageData
        check("embed-copies-image", bool(embedded) and os.path.exists(embedded), embedded)
        check("embed-content", os.path.exists(embedded) and read_bytes(embedded) == image_bytes)
        check(
            "embed-stored-under-a-uuid",
            os.path.basename(embedded) != image_hash
            and os.path.basename(embedded).endswith(".png"),
            os.path.basename(embedded),
        )

        # --- An App-tier property holding the same image shares the blob.
        obj = doc.addObject("App::DocumentObjectFileIncluded", "Texture")
        obj.File = (image, "env.png")
        check("view-and-object-share", obj.File == embedded, "%s vs %s" % (obj.File, embedded))

        # --- A view provider property (the TaskRenderSettings shape).
        box = doc.addObject("App::FeaturePython", "Shaded")
        vp = box.ViewObject
        vp.addProperty("App::PropertyFileIncluded", "Render_Texture", "Render")
        second = make_image(os.path.join(tmp, "tex.png"), (200, 60, 60))
        vp.Render_Texture = (second, "tex.png")
        check("viewprovider-property", os.path.exists(vp.Render_Texture))

        doc.recompute()
        doc.saveAs(project)

        # --- Archive shape: one entry per distinct content, hash reference.
        entries = blob_entries(project)
        check("one-entry-per-content", len(entries) == 2, str(entries))
        # The view's own property cannot name a file -- it belongs to no
        # object -- so the App referrer sharing the same content names it.
        check(
            "env-entry-named-after-its-referrer",
            "%s/Texture.File.png" % BLOB_DIR in entries,
            str(entries),
        )
        check(
            "viewprovider-entry-named-with-its-tier",
            "%s/Shaded.ViewObject.Render_Texture.png" % BLOB_DIR in entries,
            str(entries),
        )
        xml = gui_xml(project)
        check("view-saves-hash", image_hash in xml)
        check(
            "view-not-base64",
            "PNG" not in xml and "iVBOR" not in xml,
            "GuiDocument.xml still carries inline image data",
        )

        # --- Round-trip.
        FreeCAD.closeDocument(doc.Name)
        doc = FreeCAD.openDocument(project)
        view = active_view(doc)
        restored = view.Render_PBREnvImageData
        check("restored-view-blob", bool(restored) and os.path.exists(restored), str(restored))
        check(
            "restored-view-content",
            os.path.exists(restored) and read_bytes(restored) == image_bytes,
        )
        restored_obj = doc.getObject("Texture")
        check(
            "restored-object-content",
            os.path.exists(restored_obj.File) and read_bytes(restored_obj.File) == image_bytes,
        )
        restored_vp = view_provider(doc, "Shaded")
        check("restored-viewprovider-exists", restored_vp is not None)
        check("restored-viewprovider", os.path.exists(restored_vp.Render_Texture))
        check("restored-sharing", restored_obj.File == restored, "view and object must share")
        check("no-unclaimed-content", len(stored_blobs(doc)) == 2, str(stored_blobs(doc)))

        # --- Clearing the view's copy leaves the object's referrer holding
        #     the same content: one blob, two independent referrers.
        view.Render_PBREnvImageData = ""
        check("cleared-view-property", not view.Render_PBREnvImageData)
        check(
            "clear-keeps-shared",
            os.path.exists(restored_obj.File),
            "object referrer lost its content",
        )
        FreeCAD.closeDocument(doc.Name)

        log("DONE")
    except Exception:
        traceback.print_exc()
        log("EXCEPTION")
    finally:
        with open(RESULT, "w") as handle:
            handle.write("\n".join(results) + "\n")
        os._exit(0)


QTimer.singleShot(1500, run)
