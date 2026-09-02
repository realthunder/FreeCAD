/***************************************************************************
 *   Copyright (c) 2011 Werner Mayer <wmayer[at]users.sourceforge.net>     *
 *                                                                         *
 *   This file is part of the FreeCAD CAx development system.              *
 *                                                                         *
 *   This library is free software; you can redistribute it and/or         *
 *   modify it under the terms of the GNU Library General Public           *
 *   License as published by the Free Software Foundation; either          *
 *   version 2 of the License, or (at your option) any later version.      *
 *                                                                         *
 *   This library  is distributed in the hope that it will be useful,      *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU Library General Public License for more details.                  *
 *                                                                         *
 *   You should have received a copy of the GNU Library General Public     *
 *   License along with this library; see the file COPYING.LIB. If not,    *
 *   write to the Free Software Foundation, Inc., 59 Temple Place,         *
 *   Suite 330, Boston, MA  02111-1307, USA                                *
 *                                                                         *
 ***************************************************************************/

#include "PreCompiled.h"
#if defined(__MINGW32__)
#define WNT  // avoid conflict with GUID
#endif
#ifndef _PreComp_
#include <climits>
#include <cstdlib>
#include <iostream>
#include <set>
#include <atomic>
#include <thread>

#include <QApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QString>
#include <QThread>

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wextra-semi"
#endif

#include <OSD_Exception.hxx>
#include <Standard_Version.hxx>
#include <TColStd_IndexedDataMapOfStringString.hxx>
#include <TDataXtd_Shape.hxx>
#include <TDocStd_Document.hxx>
#include <XCAFApp_Application.hxx>

#if defined(__clang__)
#pragma clang diagnostic pop
#endif
#endif

#include "ExportOCAFGui.h"
#include "ImportOCAFGui.h"
#include "ReaderLookGui.h"
#include "OCAFBrowser.h"

#include "dxf/ImpExpDxfGui.h"
#include <App/Document.h>
#include <App/DocumentObjectPy.h>
#include <App/DocumentObserver.h>
#include <App/PropertyFile.h>
#include <Base/Console.h>
#include <Base/Interpreter.h>
#include <Base/PyWrapParseTupleAndKeywords.h>
#include <Base/Sequencer.h>
#include <Base/TimeInfo.h>
#include <Base/Tools.h>
#include <Gui/Application.h>
#include <Gui/Command.h>
#include <Gui/Document.h>
#include <Gui/MainWindow.h>
#include <Gui/LiveViewInteraction.h>
#include <Gui/WaitCursor.h>
#include <Gui/ViewProviderGeometryObject.h>
#include <Gui/ViewProviderLink.h>
#include <Mod/Part/Gui/ViewProvider.h>
#include <Mod/Import/App/ExportOCAF2.h>
#include <Mod/Import/App/ImportOCAF2.h>
#include <Mod/Import/App/ReaderGltf.h>
#include <Mod/Import/App/ReaderIges.h>
#include <Mod/Import/App/ReaderStep.h>
#include <Mod/Import/App/WriterGltf.h>
#include <Mod/Import/App/WriterIges.h>
#include <Mod/Import/App/WriterStep.h>
#include <Mod/Part/App/ImportIges.h>
#include <Mod/Part/App/ImportStep.h>
#include <Mod/Part/App/Interface.h>
#include <Mod/Part/App/OCAF/ImportExportSettings.h>
#include <Mod/Part/App/ProgressIndicator.h>
#include <Mod/Part/App/encodeFilename.h>
#include <Mod/Part/Gui/DlgExportStep.h>
#include <Mod/Part/Gui/ViewProvider.h>


FC_LOG_LEVEL_INIT("Import", true, true)

namespace ImportGui
{

class Module: public Py::ExtensionModule<Module>
{
public:
    Module()
        : Py::ExtensionModule<Module>("ImportGui")
    {
        add_keyword_method("open",
                           &Module::insert,
                           "open(string) -- Open the file and create a new document.");
        add_keyword_method("insert",
                           &Module::insert,
                           "insert(string,string) -- Insert the file into the given document.");
        add_varargs_method("readDXF",
                           &Module::readDXF,
                           "readDXF(filename,[document,ignore_errors,option_source]): Imports a "
                           "DXF file into the given document. ignore_errors is True by default.");
        add_varargs_method("exportOptions",
                           &Module::exportOptions,
                           "exportOptions(string) -- Return the export options of a file type.");
        add_keyword_method("export",
                           &Module::exporter,
                           "export(list,string) -- Export a list of objects into a single file.");
        add_varargs_method("ocaf", &Module::ocaf, "ocaf(string) -- Browse the ocaf structure.");
        initialize("This module is the ImportGui module.");  // register with Python
    }

private:
    // Guards against a nested import starting from the pumped event loops
    // below; a nested call falls back to the fully synchronous path.
    static inline bool importBusy;

    // Run the STEP read + XCAF transfer. When called on the GUI thread, the
    // OCCT work runs on a worker thread while a local event loop keeps the
    // application pumping (progress bar, redraws, scene publishing); the
    // sequencer provides progress and Escape-cancel. The XCAF document is the
    // only state the worker touches. With an analyzer, the worker also runs
    // the progressive-import analysis after the read; returns whether that
    // analysis succeeded (false = caller must use the synchronous
    // loadShapes() path).
    static bool readStep(const Base::FileInfo& file,
                         Handle(TDocStd_Document) hDoc,  // NOLINT
                         App::Document* pcDoc,
                         Import::ImportOCAF2* analyzer)
    {
        if (importBusy || !qApp || QThread::currentThread() != qApp->thread()) {
            Import::ReaderStep reader(file);
            reader.read(hDoc);
            return false;
        }
        Base::StateLocker nestGuard(importBusy);
        // the read owns a worker thread, not the GUI thread: whatever is
        // already in the document stays navigable while it runs
        Gui::LiveViewInteraction navigable;
        App::DocumentWeakPtrT docPtr(pcDoc);
        std::exception_ptr readError;
        bool analyzed = false;
        std::atomic<bool> streaming {false};
        std::atomic<bool> workerDone {false};
        Base::TimeInfo importStart;
        std::thread worker([&] {
            try {
                Import::ReaderStep reader(file);
                int roots = 0;
                if (analyzer) {
                    Base::TimeInfo parseStart;
                    roots = reader.openStream();
                    FC_LOG("file parsed in " << Base::TimeInfo::diffTimeF(parseStart) << "s, "
                                             << roots << " roots");
                }
                // A file with a single root - the common assembly - transfers
                // as one unit and would show nothing until it is through, so
                // its components are streamed ahead of it instead, at any
                // depth of the assembly tree.
                int components = 0;
                if (analyzer && roots == 1) {
                    Base::TimeInfo treeStart;
                    const int nodes = reader.openAssemblyTree(hDoc, 1);
                    FC_LOG("assembly tree walked in " << Base::TimeInfo::diffTimeF(treeStart)
                                                      << "s, " << nodes << " nodes");
                    if (nodes > 0) {
                        components = reader.componentCount();
                    }
                }
                if (analyzer && roots > 0 && analyzer->analyzeBegin()) {
                    // Streamed: transfer geometrically growing batches and
                    // analyze each, publishing sealed ops the GUI thread
                    // applies meanwhile. Growing batches bound the repeated
                    // per-batch attribute passes to O(log n) while the first
                    // parts still appear quickly.
                    streaming = true;
                    auto analyzeBatch = [&analyzer]() {
                        try {
                            return analyzer->analyzeRoots();
                        }
                        catch (Base::Exception& e) {
                            FC_WARN("progressive import analysis failed, "
                                    "falling back: " << e.what());
                        }
                        catch (Standard_Failure& e) {
                            FC_WARN("progressive import analysis failed, falling back: "
                                    << (e.GetMessageString() ? e.GetMessageString()
                                                             : "OCCT failure"));
                        }
                        return false;
                    };
                    Part::OCAF::ImportExportSettings settings;
                    bool ok = true;
                    int first = 1;
                    int batch = settings.getStreamBatchStart();
                    const int growth = settings.getStreamBatchFactor();
                    const int units = components >= 2 ? components : roots;
                    std::vector<std::pair<TopoDS_Shape, int>> streamed;
                    if (components >= 2) {
                        analyzer->setComponentStreaming(true);
                        // The containers of the tree are reserved before any
                        // of it transfers, so a component of any depth lands
                        // in its own the moment it arrives.
                        Base::TimeInfo skeletonStart;
                        ok = analyzer->beginSkeleton(reader.assemblyNodes());
                        FC_LOG("streaming " << components << " components of the single root, "
                                            << reader.assemblyNodes().size() - components
                                            << " assemblies deep, skeleton reserved in "
                                            << Base::TimeInfo::diffTimeF(skeletonStart) << "s");
                    }
                    else {
                        FC_LOG("streaming " << roots << " roots");
                    }
                    while (ok && first <= units) {
                        int last = std::min(first + batch - 1, units);
                        Base::TimeInfo batchStart;
                        if (components >= 2) {
                            reader.transferComponentRange(hDoc, first, last, streamed);
                            analyzer->addStreamedShapes(streamed);
                        }
                        else {
                            reader.transferRootRange(hDoc, first, last);
                        }
                        FC_LOG("batch " << first << ".." << last << " transferred in "
                                        << Base::TimeInfo::diffTimeF(batchStart) << "s");
                        first = last + 1;
                        batch *= growth;
                        if (!analyzeBatch()) {
                            ok = false;
                            break;
                        }
                    }
                    if (components >= 2) {
                        // The root itself still has to run: it reuses the
                        // streamed components and adds whatever was left to
                        // it (the instances a reduced import merges).
                        if (ok) {
                            analyzer->expectRootAssembly();
                        }
                        Base::TimeInfo rootStart;
                        reader.transferRootRange(hDoc, 1, 1);
                        FC_LOG("root gathered in "
                               << Base::TimeInfo::diffTimeF(rootStart)
                               << "s");
                        if (ok) {
                            ok = analyzeBatch();
                        }
                    }
                    else if (!ok && first <= roots) {
                        // finish the transfer so the synchronous fallback
                        // sees the complete document
                        reader.transferRootRange(hDoc, first, roots);
                    }
                    if (ok) {
                        ok = analyzer->analyzeEnd();
                    }
                    analyzed = ok;
                }
                else {
                    if (roots > 0) {
                        // parsed but not streamable (or analysis ineligible):
                        // reuse the open stream for a single full transfer
                        reader.transferRootRange(hDoc, 1, roots);
                    }
                    else {
                        reader.closeStream();
                        reader.read(hDoc);
                    }
                    if (analyzer) {
                        try {
                            analyzed = analyzer->analyze();
                        }
                        catch (Base::Exception& e) {
                            FC_WARN("progressive import analysis failed, "
                                    "falling back: " << e.what());
                        }
                        catch (Standard_Failure& e) {
                            FC_WARN("progressive import analysis failed, falling back: "
                                    << (e.GetMessageString() ? e.GetMessageString()
                                                             : "OCCT failure"));
                        }
                    }
                }
            }
            catch (...) {
                readError = std::current_exception();
            }
            workerDone = true;
        });
        int undoMode = pcDoc->getUndoMode();
        bool live = false;
        bool appliedAny = false;
        {
            Base::PyGILStateRelease unlock;
            while (!workerDone) {
                QCoreApplication::processEvents(QEventLoop::AllEvents
                                                    | QEventLoop::WaitForMoreEvents,
                                                50);
                if (!streaming || docPtr.expired()) {
                    continue;
                }
                if (!live && analyzer->opsPublished() > 0) {
                    // same regime as applyProgressive(): no undo of a
                    // progressive import, doc-mutating commands gated
                    pcDoc->setUndoMode(0);
                    pcDoc->setStatus(App::Document::LiveImport, true);
                    live = true;
                }
                if (live) {
                    try {
                        QElapsedTimer timer;
                        timer.start();
                        const bool wasFirst = !appliedAny;
                        while (timer.elapsed() < 50 && !docPtr.expired()
                               && analyzer->applyNextOp()) {
                            appliedAny = true;
                        }
                        if (wasFirst && appliedAny) {
                            FC_LOG("first op applied " << Base::TimeInfo::diffTimeF(importStart)
                                                       << "s into the import");
                        }
                    }
                    catch (Base::Exception& e) {
                        FC_ERR("progressive op failed during streamed import: "
                               << e.what());
                    }
                    catch (Standard_Failure& e) {
                        FC_ERR("progressive op failed during streamed import: "
                               << (e.GetMessageString() ? e.GetMessageString()
                                                        : "OCCT failure"));
                    }
                }
            }
        }
        worker.join();
        bool expired = docPtr.expired();
        if (!expired && live && (readError || !analyzed)) {
            // cancel/error mid-stream or late fallback: leave a clean slate
            // (removals record no undo while the modes are still overridden)
            analyzer->rollbackOps();
        }
        if (!expired && live) {
            pcDoc->setStatus(App::Document::LiveImport, false);
            pcDoc->setUndoMode(undoMode);
        }
        if (expired) {
            THROWM(Base::RuntimeError, "Target document was closed during STEP import")
        }
        if (readError) {
            std::rethrow_exception(readError);
        }
        return analyzed;
    }

    // Materialize the analyzed ops on the GUI thread: ~50ms of object
    // creation per slot, then a full event-loop pass, so the model grows
    // on screen while the user orbits it (the wait cursor is lifted and
    // both input filters let 3D-view navigation through; doc-mutating
    // commands are gated through the LiveImport status). Escape keeps
    // the partial result, as does a failing op; there is no undo of a
    // progressive import.
    static App::DocumentObject* applyProgressive(Import::ImportOCAF2& ocaf,
                                                 App::Document* pcDoc)
    {
        Base::StateLocker nestGuard(importBusy);
        Base::SequencerLauncher seq("Creating objects...", ocaf.opCount());
        Gui::WaitCursorRestorer cursorRestorer;
        // ...and the sequencer's own input filter has to make the same
        // exception, or the wait cursor is the only thing that lifts
        Gui::LiveViewInteraction navigable;
        App::DocumentWeakPtrT docPtr(pcDoc);
        int undoMode = pcDoc->getUndoMode();
        pcDoc->setUndoMode(0);
        pcDoc->setStatus(App::Document::LiveImport, true);
        bool canceled = false;
        std::string error;
        try {
            QElapsedTimer timer;
            while (!docPtr.expired() && ocaf.opsApplied() < ocaf.opCount()) {
                timer.start();
                do {
                    if (!ocaf.applyNextOp()) {
                        break;
                    }
                    seq.next(true);  // throws Base::AbortException on Escape
                } while (!docPtr.expired() && timer.elapsed() < 50
                         && ocaf.opsApplied() < ocaf.opCount());
                QCoreApplication::processEvents();
            }
        }
        catch (Base::AbortException&) {
            canceled = true;
        }
        catch (Base::Exception& e) {
            error = e.what();
        }
        catch (Standard_Failure& e) {
            error = e.GetMessageString() ? e.GetMessageString() : "OCCT failure";
        }
        if (docPtr.expired()) {
            THROWM(Base::RuntimeError, "Target document was closed during STEP import")
        }
        // the terminal recompute must still run with undo disabled (an
        // App::Part's origin creation, for one, records a transaction)
        auto ret = ocaf.finishOps();
        pcDoc->setStatus(App::Document::LiveImport, false);
        pcDoc->setUndoMode(undoMode);
        if (canceled) {
            Base::Console().Warning(
                "STEP import canceled: partial result kept (%zu of %zu objects)\n",
                ocaf.opsApplied(), ocaf.opCount());
        }
        else if (!error.empty()) {
            Base::Console().Error(
                "STEP import failed after %zu of %zu objects, partial result "
                "kept: %s\n",
                ocaf.opsApplied(), ocaf.opCount(), error.c_str());
        }
        return ret;
    }

    Py::Object insert(const Py::Tuple& args, const Py::Dict& kwds)
    {
        char* Name;
        char* DocName = nullptr;
        PyObject* importHidden = Py_None;
        PyObject* merge = Py_None;
        PyObject* useLinkGroup = Py_None;
        int mode = -1;
        PyObject *legacy = Py_None;
        PyObject *progressive = Py_None;
        static const std::array<const char*, 9>
            kwd_list {"name", "docName", "importHidden", "merge", "useLinkGroup", "mode", "legacy",
                      "progressive", nullptr};
        if (!Base::Wrapped_ParseTupleAndKeywords(args.ptr(),
                                                 kwds.ptr(),
                                                 "et|etO!O!O!iOO!",
                                                 kwd_list,
                                                 "utf-8",
                                                 &Name,
                                                 "utf-8",
                                                 &DocName,
                                                 &PyBool_Type,
                                                 &importHidden,
                                                 &PyBool_Type,
                                                 &merge,
                                                 &PyBool_Type,
                                                 &useLinkGroup,
                                                 &mode,
                                                 &legacy,
                                                 &PyBool_Type,
                                                 &progressive)) {
            throw Py::Exception();
        }

        std::string Utf8Name = std::string(Name);
        PyMem_Free(Name);
        std::string name8bit = Part::encodeFilename(Utf8Name);

        try {
            Base::FileInfo file(Utf8Name.c_str());

            App::Document* pcDoc = nullptr;
            if (DocName) {
                std::string Utf8DocName = std::string(DocName);
                PyMem_Free(DocName);
                pcDoc = App::GetApplication().getDocument(Utf8DocName.c_str());
            }
            if (file.hasExtension({"mtlx"})) {
                // A material document is not geometry: it says which of its
                // materials each piece of an asset already here wears
                // (docs/MaterialStorage.md sec 17.13 item 2). So it dresses
                // the document it is imported into and makes nothing, and a
                // new empty document would have nothing to dress.
                if (!pcDoc) {
                    pcDoc = App::GetApplication().getActiveDocument();
                }
                if (!pcDoc) {
                    throw Py::Exception(PyExc_IOError,
                                        "a look is read onto the objects of a document, "
                                        "and there is no document open");
                }
                if (readLook(file, pcDoc) < 0) {
                    throw Py::Exception(PyExc_IOError, "no look to read");
                }
                return Py::None();
            }
            if (!pcDoc) {
                pcDoc = App::GetApplication().newDocument();
            }

            Handle(XCAFApp_Application) hApp = XCAFApp_Application::GetApplication();
            Handle(TDocStd_Document) hDoc;
            hApp->NewDocument(TCollection_ExtendedString("MDTV-CAF"), hDoc);
            ImportOCAFGui ocaf(hDoc, pcDoc, file.fileNamePure());
            ocaf.setImportOptions(ImportOCAFGui::customImportOptions());
            // Apply keyword overrides before the read so the worker-thread
            // analysis (and the reader's auto-naming setup) see the final
            // options.
            if (merge != Py_None) {
                ocaf.setMerge(Base::asBoolean(merge));
            }
            if (importHidden != Py_None) {
                ocaf.setImportHiddenObject(Base::asBoolean(importHidden));
            }
            if (useLinkGroup != Py_None) {
                ocaf.setUseLinkGroup(Base::asBoolean(useLinkGroup));
            }
            if (legacy != Py_None) {
                ocaf.setUseLegacyImporter(Base::asBoolean(legacy));
            }
            FC_TIME_INIT(t);
            FC_DURATION_DECL_INIT2(d1, d2);

            bool analyzed = false;
            if (file.hasExtension({"stp", "step"})) {

                if (mode < 0) {
                    mode = ocaf.getMode();
                }
                if (mode && !pcDoc->isSaved()) {
                    auto gdoc = Gui::Application::Instance->getDocument(pcDoc);
                    if (!gdoc->save()) {
                        return Py::Object();
                    }
                }
                // after the save, so a multi-document mode resolves its
                // output directory
                ocaf.setMode(mode);

                bool wantProgressive;
                if (progressive != Py_None) {
                    wantProgressive = Base::asBoolean(progressive);
                }
                else {
                    // Scene-serving backends keep the synchronous path
                    // unless the import script opts in explicitly.
                    wantProgressive =
                        Part::OCAF::ImportExportSettings().getProgressiveImport()
                        && !std::getenv("FC_BGFX_SERVE_SCENE");
                }
                if (wantProgressive
                    && (!Gui::Application::Instance->getDocument(pcDoc)
                        || pcDoc->testStatus(App::Document::Restoring))) {
                    // no GUI document, or inside an importFrom() that
                    // defers all visuals: nothing to show progressively
                    wantProgressive = false;
                }

                try {
                    analyzed = readStep(file, hDoc, pcDoc, wantProgressive ? &ocaf : nullptr);
                }
                catch (OSD_Exception& e) {
                    Base::Console().Error("%s\n", e.GetMessageString());
                    Base::Console().Message("Try to load STEP file without colors...\n");

                    Part::ImportStepParts(pcDoc, Utf8Name.c_str());
                    pcDoc->recompute();
                }
            }
            else if (file.hasExtension({"igs", "iges"})) {
                try {
                    Import::ReaderIges reader(file);
                    reader.read(hDoc);
                }
                catch (OSD_Exception& e) {
                    Base::Console().Error("%s\n", e.GetMessageString());
                    Base::Console().Message("Try to load IGES file without colors...\n");

                    Part::ImportIgesParts(pcDoc, Utf8Name.c_str());
                    pcDoc->recompute();
                }
            }
            else if (file.hasExtension({"glb", "gltf"})) {
                Import::ReaderGltf reader(file);
                reader.read(hDoc);
            }
            else {
                throw Py::Exception(PyExc_IOError, "no supported file format");
            }

            FC_DURATION_PLUS(d1, t);
            if (mode >= 0 && !file.hasExtension({"stp", "step"})) {
                ocaf.setMode(mode);
            }
            App::DocumentObject* ret = nullptr;
            if (analyzed) {
                // Progressive path: objects appear as they are created,
                // tessellated inline (coarse-first under the bgfx level
                // machinery), so no deferral pass is needed.
                ret = applyProgressive(ocaf, pcDoc);
            }
            else {
            // Defer per-object tessellation while objects are being created;
            // Application::importFrom() sets the same guard around insert(),
            // in which case it also runs the finishRestoring pass itself.
            bool outerRestoring = pcDoc->testStatus(App::Document::Restoring);
            std::set<long> existingIds;
            {
                Base::ObjectStatusLocker<App::Document::Status, App::Document>
                    guard(App::Document::Restoring, pcDoc);
                if (!outerRestoring) {
                    for (auto obj : pcDoc->getObjects()) {
                        existingIds.insert(obj->getID());
                    }
                }
                ret = ocaf.loadShapes();
            }
            if (!outerRestoring) {
                auto gdoc = Gui::Application::Instance->getDocument(pcDoc);
                // copy: afterImport() may add objects and invalidate the array
                std::vector<App::DocumentObject*> objs = pcDoc->getObjects();
                std::vector<App::DocumentObject*> fresh;
                for (auto obj : objs) {
                    if (existingIds.count(obj->getID())) {
                        continue;
                    }
                    fresh.push_back(obj);
                    pcDoc->afterImport(obj);
                    if (gdoc) {
                        auto vp = Base::freecad_dynamic_cast<Gui::ViewProviderDocumentObject>(
                            gdoc->getViewProvider(obj));
                        if (vp) {
                            vp->finishRestoring();
                        }
                    }
                }
                // An asset carrying its materials in a MaterialX document
                // beside it dresses itself as it arrives; only what this
                // import made, so a second asset in the document is left
                // alone (docs/MaterialStorage.md sec 17.13 item 2)
                readSidecarLook(file, pcDoc, fresh);
            }
            }
            hApp->Close(hDoc);
            FC_DURATION_PLUS(d2, t);
            FC_DURATION_LOG(d1, "file read");
            FC_DURATION_LOG(d2, "import");
            FC_DURATION_LOG((d1 + d2), "total");

            if (ret) {
                App::GetApplication().setActiveDocument(pcDoc);
                auto gdoc = Gui::Application::Instance->getDocument(pcDoc);
                if (gdoc) {
                    gdoc->setActiveView();
                    Gui::Application::Instance->commandManager().runCommandByName("Std_ViewFitAll");
                }
                return Py::asObject(ret->getPyObject());
            }
        }
        catch (Standard_Failure& e) {
            throw Py::Exception(Base::PyExc_FC_GeneralError, e.GetMessageString());
        }
        catch (const Base::Exception& e) {
            e.setPyException();
            throw Py::Exception();
        }

        return Py::None();
    }

    static std::map<std::string, App::Color> getShapeColors(App::DocumentObject* obj,
                                                            const char* subname)
    {
        auto vp = Gui::Application::Instance->getViewProvider(obj);
        if (vp) {
            return vp->getElementColors(subname);
        }
        return {};
    }

    static bool getShapeAppearance(App::DocumentObject* obj, std::vector<App::MaterialAppearance>& mats,
                                   bool& pbr)
    {
        // Whole materials, only when the appearance says something the
        // colour labels cannot: a field beyond diffuse varying across the
        // faces, or a uniform emissive that is lit at all (no other
        // export channel carries emissive) -- or the PBR mode at all,
        // whose metallic and roughness have no colour-label channel.
        auto vp = dynamic_cast<PartGui::ViewProviderPartExt*>(
            Gui::Application::Instance->getViewProvider(obj));
        if (!vp) {
            return false;
        }
        pbr = vp->ShapeAppearance.isPBR();
        if (!pbr) {
            const App::Color e = vp->ShapeAppearance.getEmissiveColor(0);
            bool emissive = e.r > 0.004f || e.g > 0.004f || e.b > 0.004f;
            if (vp->ShapeAppearance.variesOnlyInDiffuse() && !emissive) {
                return false;
            }
        }
        int count = vp->ShapeAppearance.getSize();
        mats.reserve(count);
        for (int i = 0; i < count; ++i) {
            // Raw slots either way; a PBR list's conversion happens at the
            // writer, which needs both readings.
            mats.push_back(vp->ShapeAppearance.getMaterial(i));
        }
        return !mats.empty();
    }

    static bool getRenderMaterial(App::DocumentObject* obj, Import::RenderMaterial& mat)
    {
        // Per-object render engine settings (Render_* dynamic properties,
        // see ViewProviderGeometryObject) become a glTF PBR material. An
        // object without any of them exports color-only, as before.
        auto vp = dynamic_cast<Gui::ViewProviderGeometryObject*>(
            Gui::Application::Instance->getViewProvider(obj));
        if (!vp) {
            return false;
        }
        auto getFloat = [vp](const char* name) -> double {
            auto prop = Base::freecad_dynamic_cast<App::PropertyFloat>(
                vp->getPropertyByName(name));
            return prop ? prop->getValue() : -1.0;
        };
        auto getFile = [vp](const char* name) -> std::string {
            auto prop = Base::freecad_dynamic_cast<App::PropertyFileIncluded>(
                vp->getPropertyByName(name));
            if (prop && prop->getValue()) {
                return prop->getValue();
            }
            return {};
        };
        // The appearance is where the importer and the appearance dialogs
        // put the PBR factors. Entry 0 -- the whole-object reading, and the
        // first face's for a per-face appearance, since one glTF material
        // per object is all this carries.
        if (auto* appearance = Base::freecad_dynamic_cast<App::PropertyAppearanceList>(
                vp->getPropertyByName("ShapeAppearance"))) {
            if (appearance->isPBR() && appearance->getSize() > 0) {
                mat.metallic = appearance->getMetallic(0);
                mat.roughness = appearance->getRoughness(0);
            }
        }
        // The Render_* knobs stay readable for an object a script set them
        // on; nothing the user does through the dialogs writes them.
        if (mat.metallic < 0.0) {
            mat.metallic = getFloat("Render_Metallic");
        }
        if (mat.roughness < 0.0) {
            mat.roughness = getFloat("Render_Roughness");
        }
        mat.baseColorTexture = getFile("Render_BaseColorTexture");
        mat.normalMapTexture = getFile("Render_NormalMap");
        mat.emissiveTexture = getFile("Render_EmissiveMap");
        mat.occlusionTexture = getFile("Render_OcclusionMap");
        mat.metallicRoughnessTexture = getFile("Render_MetallicRoughnessMap");
        mat.valid = mat.metallic >= 0.0 || mat.roughness >= 0.0
            || !mat.baseColorTexture.empty() || !mat.normalMapTexture.empty()
            || !mat.emissiveTexture.empty() || !mat.occlusionTexture.empty()
            || !mat.metallicRoughnessTexture.empty();
        if (mat.valid) {
            mat.hasBaseColor = true;
            mat.baseColor = vp->ShapeColor.getValue();
            mat.baseColor.setTransparency(float(vp->Transparency.getValue()) / 100.0f);
        }
        return mat.valid;
    }

    // This readDXF method is an almost exact duplicate of the one in Import::Module.
    // The only difference is the CDxfRead class derivation that is created.
    // It would seem desirable to have most of this code in just one place, passing it
    // e.g. a pointer to a function that does the 4 lines during the lifetime of the
    // CDxfRead object, but right now Import::Module and ImportGui::Module cannot see
    // each other's functions so this shared code would need some place to live where
    // both places could include a declaration.
    Py::Object readDXF(const Py::Tuple& args)
    {
        char* Name = nullptr;
        const char* DocName = nullptr;
        const char* optionSource = nullptr;
        std::string defaultOptions = "User parameter:BaseApp/Preferences/Mod/Draft";
        bool IgnoreErrors = true;
        if (!PyArg_ParseTuple(args.ptr(),
                              "et|sbs",
                              "utf-8",
                              &Name,
                              &DocName,
                              &IgnoreErrors,
                              &optionSource)) {
            throw Py::Exception();
        }

        std::string EncodedName = std::string(Name);
        PyMem_Free(Name);

        Base::FileInfo file(EncodedName.c_str());
        if (!file.exists()) {
            throw Py::RuntimeError("File doesn't exist");
        }

        if (optionSource) {
            defaultOptions = optionSource;
        }

        App::Document* pcDoc = nullptr;
        if (DocName) {
            pcDoc = App::GetApplication().getDocument(DocName);
        }
        else {
            pcDoc = App::GetApplication().getActiveDocument();
        }
        if (!pcDoc) {
            pcDoc = App::GetApplication().newDocument(DocName);
        }

        try {
            // read the DXF file
            ImpExpDxfReadGui dxf_file(EncodedName, pcDoc);
            dxf_file.setOptionSource(defaultOptions);
            dxf_file.setOptions();
            dxf_file.DoRead(IgnoreErrors);
            pcDoc->recompute();
        }
        catch (const Standard_Failure& e) {
            throw Py::RuntimeError(e.GetMessageString());
        }
        catch (const Base::Exception& e) {
            throw Py::RuntimeError(e.what());
        }
        return Py::None();
    }

    Py::Object exportOptions(const Py::Tuple& args)
    {
        char* Name;
        if (!PyArg_ParseTuple(args.ptr(), "et", "utf-8", &Name)) {
            throw Py::Exception();
        }

        std::string Utf8Name = std::string(Name);
        PyMem_Free(Name);
        std::string name8bit = Part::encodeFilename(Utf8Name);

        Py::Dict options;
        Base::FileInfo file(name8bit.c_str());

        if (file.hasExtension({"stp", "step"})) {
            PartGui::TaskExportStep dlg(Gui::getMainWindow());
            if (!dlg.showDialog() || dlg.exec()) {
                auto stepSettings = dlg.getSettings();
                options.setItem("exportHidden", Py::Boolean(stepSettings.exportHidden));
                options.setItem("keepPlacement", Py::Boolean(stepSettings.keepPlacement));
            }
        }

        return options;
    }

    Py::Object exporter(const Py::Tuple& args, const Py::Dict& kwds)
    {
        PyObject* object;
        char* Name;
        PyObject* pyoptions = nullptr;
        PyObject* pyexportHidden = Py_None;
        PyObject* pylegacy = Py_None;
        PyObject* pykeepPlacement = Py_None;
        static const std::array<const char*, 7>
            kwd_list {"obj", "name", "options", "exportHidden", "legacy", "keepPlacement", nullptr};
        if (!Base::Wrapped_ParseTupleAndKeywords(args.ptr(),
                                                 kwds.ptr(),
                                                 "Oet|O!O!O!O!",
                                                 kwd_list,
                                                 &object,
                                                 "utf-8",
                                                 &Name,
                                                 &PyDict_Type,
                                                 &pyoptions,
                                                 &PyBool_Type,
                                                 &pyexportHidden,
                                                 &PyBool_Type,
                                                 &pylegacy,
                                                 &PyBool_Type,
                                                 &pykeepPlacement)) {
            throw Py::Exception();
        }

        std::string Utf8Name = std::string(Name);
        PyMem_Free(Name);

        // clang-format off
        // determine export options
        Part::OCAF::ImportExportSettings settings;

        // still support old way
        // The legacy exporter is retired and no longer preference-driven: it
        // runs only when explicitly requested with legacy=True.
        bool legacyExport = (pylegacy != Py_None && Base::asBoolean(pylegacy));
        bool exportHidden = (pyexportHidden   == Py_None ? settings.getExportHiddenObject()
                                                         : Base::asBoolean(pyexportHidden));
        bool keepPlacement = (pykeepPlacement == Py_None ? settings.getExportKeepPlacement()
                                                         : Base::asBoolean(pykeepPlacement));
        // clang-format on

        // new way
        if (pyoptions) {
            Py::Dict options(pyoptions);
            if (options.hasKey("legacy")) {
                legacyExport = static_cast<bool>(Py::Boolean(options.getItem("legacy")));
            }
            if (options.hasKey("exportHidden")) {
                exportHidden = static_cast<bool>(Py::Boolean(options.getItem("exportHidden")));
            }
            if (options.hasKey("keepPlacement")) {
                keepPlacement = static_cast<bool>(Py::Boolean(options.getItem("keepPlacement")));
            }
        }

        try {
            Py::Sequence list(object);
            std::vector<App::DocumentObject*> objs;
            for (Py::Sequence::iterator it = list.begin(); it != list.end(); ++it) {
                Py::Object item(*it);
                if (PyObject_TypeCheck(item.ptr(), &(App::DocumentObjectPy::Type))) {
                    auto pydoc = static_cast<App::DocumentObjectPy*>(item.ptr());
                    objs.push_back(pydoc->getDocumentObjectPtr());
                }
            }

            Handle(XCAFApp_Application) hApp = XCAFApp_Application::GetApplication();
            Handle(TDocStd_Document) hDoc;
            hApp->NewDocument(TCollection_ExtendedString("MDTV-CAF"), hDoc);

            Import::ExportOCAF2 ocaf(hDoc, &getShapeColors);
            ocaf.setGetRenderMaterial(&getRenderMaterial);
            ocaf.setGetShapeAppearance(&getShapeAppearance);
            if (!legacyExport || !ocaf.canFallback(objs)) {
                ocaf.setExportOptions(Import::ExportOCAF2::customExportOptions());
                ocaf.setExportHiddenObject(exportHidden);
                ocaf.setKeepPlacement(keepPlacement);

                ocaf.exportObjects(objs);
            }
            else {
                bool keepExplicitPlacement = true;
                ExportOCAFGui ocaf(hDoc, keepExplicitPlacement);
                ocaf.exportObjects(objs);
            }

            Base::FileInfo file(Utf8Name.c_str());
            if (file.hasExtension({"stp", "step"})) {
                ParameterGrp::handle hGrp = App::GetApplication().GetParameterGroupByPath(
                    "User parameter:BaseApp/Preferences/Mod/Part/STEP");
                std::string scheme = hGrp->GetASCII("Scheme", Part::Interface::writeStepScheme());
                std::list<std::string> supported = Part::supportedSTEPSchemes();
                if (std::find(supported.begin(), supported.end(), scheme) != supported.end()) {
                    Part::Interface::writeStepScheme(scheme.c_str());
                }

                Import::WriterStep writer(file);
                writer.write(hDoc);
            }
            else if (file.hasExtension({"igs", "iges"})) {
                Import::WriterIges writer(file);
                writer.write(hDoc);
            }
            else if (file.hasExtension({"glb", "gltf"})) {
                Import::WriterGltf writer(file);
                writer.write(hDoc);
            }

            hApp->Close(hDoc);
        }
        catch (Standard_Failure& e) {
            throw Py::Exception(Base::PyExc_FC_GeneralError, e.GetMessageString());
        }
        catch (const Base::Exception& e) {
            e.setPyException();
            throw Py::Exception();
        }

        return Py::None();
    }
    Py::Object ocaf(const Py::Tuple& args)
    {
        const char* Name;
        if (!PyArg_ParseTuple(args.ptr(), "s", &Name)) {
            throw Py::Exception();
        }

        try {
            Base::FileInfo file(Name);

            Handle(XCAFApp_Application) hApp = XCAFApp_Application::GetApplication();
            Handle(TDocStd_Document) hDoc;
            hApp->NewDocument(TCollection_ExtendedString("MDTV-CAF"), hDoc);

            if (file.hasExtension({"stp", "step"})) {
                Import::ReaderStep reader(file);
                reader.read(hDoc);
            }
            else if (file.hasExtension({"igs", "iges"})) {
                Import::ReaderIges reader(file);
                reader.read(hDoc);
            }
            else if (file.hasExtension({"glb", "gltf"})) {
                Import::ReaderGltf reader(file);
                reader.read(hDoc);
            }
            else {
                throw Py::Exception(PyExc_IOError, "no supported file format");
            }

            OCAFBrowser::showDialog(QString::fromStdString(file.fileName()), hDoc);
            hApp->Close(hDoc);
        }
        catch (Standard_Failure& e) {
            throw Py::Exception(Base::PyExc_FC_GeneralError, e.GetMessageString());
        }
        catch (const Base::Exception& e) {
            e.setPyException();
            throw Py::Exception();
        }

        return Py::None();
    }
};

PyObject* initModule()
{
    return Base::Interpreter().addModule(new Module);
}

}  // namespace ImportGui
