/***************************************************************************
 *   Copyright (c) 2013 Werner Mayer <wmayer[at]users.sourceforge.net>     *
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
#include <boost/algorithm/string/predicate.hpp>
#include <boost/range/adaptor/indexed.hpp>
#include <climits>
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wextra-semi"
#endif
#include <gp_Pln.hxx>
#include <Interface_Static.hxx>
#include <OSD_Exception.hxx>
#include <Standard_Version.hxx>
#include <TColStd_IndexedDataMapOfStringString.hxx>
#include <TDocStd_Document.hxx>
#include <Transfer_TransientProcess.hxx>
#include <XCAFApp_Application.hxx>
#include <XCAFDoc_DocumentTool.hxx>
#include <XSControl_TransferReader.hxx>
#include <XSControl_WorkSession.hxx>
#if OCC_VERSION_HEX >= 0x070500
#include <Message_ProgressRange.hxx>
#endif
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
#endif

#include "dxf/ImpExpDxf.h"
#include <App/Application.h>
#include <App/Document.h>
#include <App/DocumentObjectPy.h>
#include <App/DocumentObserver.h>
#include <Base/Console.h>
#include <Base/PyWrapParseTupleAndKeywords.h>
#include <Mod/Part/App/ImportIges.h>
#include <Mod/Part/App/ImportStep.h>
#include <Mod/Part/App/Interface.h>
#include <Mod/Part/App/OCAF/ImportExportSettings.h>
#include <Mod/Part/App/PartFeaturePy.h>
#include <Mod/Part/App/ProgressIndicator.h>
#include <Mod/Part/App/TopoShapePy.h>
#include <Mod/Part/App/encodeFilename.h>
#include <Mod/Part/App/OCCError.h>
#include <Mod/Part/App/PartPyCXX.h>

#include "ImportOCAF2.h"
#include "ExportOCAF2.h"
#include "ReaderGltf.h"
#include "ReaderIges.h"
#include "ReaderStep.h"
#include "WriterGltf.h"
#include "WriterIges.h"
#include "WriterStep.h"

namespace Import
{

class Module : public Py::ExtensionModule<Module>
{
public:
    Module()
        : Py::ExtensionModule<Module>("Import")
    {
        add_keyword_method("open",
                           &Module::importer,
                           "open(string) -- Open the file and create a new document.");
        add_keyword_method("insert",
                           &Module::importer,
                           "insert(string,string) -- Insert the file into the given document.");
        add_keyword_method("export",
                           &Module::exporter,
                           "export(list,string) -- Export a list of objects into a single file.");
        add_keyword_method("readDXF",
                           &Module::readDXF,
                           "readDXF(filename,[document,ignore_errors,option_source]): Imports a "
                           "DXF file into the given document. ignore_errors is True by default.");
        add_keyword_method("writeDXFShape",
                           &Module::writeDXFShape,
                           "writeDXFShape([shape],filename [version,usePolyline,optionSource]): "
                           "Exports Shape(s) to a DXF file.");
        add_keyword_method(
            "writeDXFObject",
            &Module::writeDXFObject,
            "writeDXFObject([objects],filename [,version,usePolyline,optionSource]): Exports "
            "DocumentObject(s) to a DXF file.");
        initialize("This module is the Import module.");  // register with Python
    }

    ~Module() override = default;

private:
    Py::Object importer(const Py::Tuple& args, const Py::Dict& kwds)
    {
        char* Name = nullptr;
        char* DocName = nullptr;
        PyObject* importHidden = Py_None;
        PyObject* merge = Py_None;
        PyObject* useLinkGroup = Py_None;
        PyObject *legacy = Py_None;
        int mode = -1;
        static const std::array<const char*, 8>
            kwd_list {"name", "docName", "importHidden", "merge", "useLinkGroup", "mode", "legacy", nullptr};
        if (!Base::Wrapped_ParseTupleAndKeywords(args.ptr(),
                                                 kwds.ptr(),
                                                 "et|etO!O!O!iO",
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
                                                 &legacy)) {
            throw Py::Exception();
        }

        std::string Utf8Name = std::string(Name);
        PyMem_Free(Name);

        try {
            Base::FileInfo file(Utf8Name.c_str());

            App::Document* pcDoc = nullptr;
            if (DocName) {
                pcDoc = App::GetApplication().getDocument(DocName);
            }
            if (!pcDoc) {
                pcDoc = App::GetApplication().newDocument();
            }

            Handle(XCAFApp_Application) hApp = XCAFApp_Application::GetApplication();
            Handle(TDocStd_Document) hDoc;
            hApp->NewDocument(TCollection_ExtendedString("MDTV-CAF"), hDoc);

            if (file.hasExtension({"stp", "step"})) {
                try {
                    Import::ReaderStep reader(file);
                    reader.read(hDoc);
                }
                catch (OSD_Exception& e) {
                    Base::Console().Error("%s\n", e.GetMessageString());
                    Base::Console().Message("Try to load STEP file without colors...\n");

                    Part::ImportStepParts(pcDoc, Utf8Name.c_str());
                    pcDoc->recompute();
                }
                _PY_CATCH_OCC(return Py::None());
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
                _PY_CATCH_OCC(return Py::None());
            }
            else if (file.hasExtension({"glb", "gltf"})) {
                Import::ReaderGltf reader(file);
                reader.read(hDoc);
            }
            else if (file.hasExtension({"mtlx"})) {
                // Import::ReaderLook does the FreeCAD half of a look, but
                // reading the document is MaterialX's, and MaterialX is
                // carried by the renderer, which the App tier does not link
                // (docs/MaterialStorage.md sec 17.13 item 2)
                throw Py::Exception(PyExc_IOError,
                                    "reading a MaterialX look needs the GUI module: "
                                    "ImportGui.insert()");
            }
            else {
                throw Py::Exception(PyExc_IOError, "no supported file format");
            }

            ImportOCAFExt ocaf(hDoc, pcDoc, file.fileNamePure());
            ocaf.setImportOptions(ImportOCAFExt::customImportOptions());
            if (merge != Py_None) {
                ocaf.setMerge(Base::asBoolean(merge));
            }
            if (importHidden != Py_None) {
                ocaf.setImportHiddenObject(Base::asBoolean(importHidden));
            }
            if (useLinkGroup != Py_None) {
                ocaf.setUseLinkGroup(Base::asBoolean(useLinkGroup));
            }
            if (legacy!=Py_None) {
                ocaf.setUseLegacyImporter(Base::asBoolean(legacy));
            }
            if (mode >= 0) {
                ocaf.setMode(mode);
            }
            ocaf.loadShapes();

            hApp->Close(hDoc);

            if (!ocaf.partColors.empty()) {
                Py::List list;
                for (auto& it : ocaf.partColors) {
                    Py::Tuple tuple(2);
                    tuple.setItem(0, Py::asObject(it.first->getPyObject()));

                    App::PropertyColorList colors;
                    colors.setValues(it.second);
                    tuple.setItem(1, Py::asObject(colors.getPyObject()));

                    list.append(tuple);
                }

                return list;  // NOLINT
            }
        }
        _PY_CATCH_OCC(return Py::None());

        return Py::None();
    }
    Py::Object exporter(const Py::Tuple& args, const Py::Dict& kwds)
    {
        PyObject* object = nullptr;
        char* Name = nullptr;
        PyObject* pyexportHidden = Py_None;
        PyObject* pylegacy = Py_None;
        PyObject* pykeepPlacement = Py_None;
        static const std::array<const char*, 6> kwd_list {"obj",
                                                          "name",
                                                          "exportHidden",
                                                          "legacy",
                                                          "keepPlacement",
                                                          nullptr};
        if (!Base::Wrapped_ParseTupleAndKeywords(args.ptr(),
                                                 kwds.ptr(),
                                                 "Oet|O!O!O!",
                                                 kwd_list,
                                                 &object,
                                                 "utf-8",
                                                 &Name,
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

        // The legacy exporter is retired and no longer preference-driven: it
        // runs only when explicitly requested with legacy=True.
        bool legacyExport = (pylegacy != Py_None && Base::asBoolean(pylegacy));
        bool exportHidden = (pyexportHidden   == Py_None ? settings.getExportHiddenObject()
                                                         : Base::asBoolean(pyexportHidden));
        bool keepPlacement = (pykeepPlacement == Py_None ? settings.getExportKeepPlacement()
                                                         : Base::asBoolean(pykeepPlacement));
        // clang-format on

        try {
            Py::Sequence list(object);
            std::vector<App::DocumentObject*> objs;
            std::map<Part::Feature*, std::vector<App::Color>> partColor;
            for (Py::Sequence::iterator it = list.begin(); it != list.end(); ++it) {
                Py::Object item(*it);
                if (PyObject_TypeCheck(item.ptr(), &(App::DocumentObjectPy::Type))) {
                    auto pydoc = static_cast<App::DocumentObjectPy*>(item.ptr());
                    objs.push_back(pydoc->getDocumentObjectPtr());
                }
                else if (PyTuple_Check(item.ptr()) && PyTuple_Size(item.ptr()) == 2) {
                    Py::Tuple tuple(*it);
                    Py::Object item0 = tuple.getItem(0);
                    Py::Object item1 = tuple.getItem(1);
                    if (PyObject_TypeCheck(item0.ptr(), &(App::DocumentObjectPy::Type))) {
                        auto pydoc = static_cast<App::DocumentObjectPy*>(item0.ptr());
                        App::DocumentObject* obj = pydoc->getDocumentObjectPtr();
                        objs.push_back(obj);
                        if (Part::Feature* part = dynamic_cast<Part::Feature*>(obj)) {
                            App::PropertyColorList colors;
                            colors.setPyObject(item1.ptr());
                            partColor[part] = colors.getValues();
                        }
                    }
                }
            }

            Handle(XCAFApp_Application) hApp = XCAFApp_Application::GetApplication();
            Handle(TDocStd_Document) hDoc;
            hApp->NewDocument(TCollection_ExtendedString("MDTV-CAF"), hDoc);

            auto getShapeColors = [partColor](App::DocumentObject* obj, const char* subname) {
                std::map<std::string, App::Color> cols;
                auto it = partColor.find(dynamic_cast<Part::Feature*>(obj));
                if (it != partColor.end() && boost::starts_with(subname, "Face")) {
                    const auto& colors = it->second;
                    std::string face("Face");
                    for (const auto& element : colors | boost::adaptors::indexed(1)) {
                        cols[face + std::to_string(element.index())] = element.value();
                    }
                }
                return cols;
            };

            Import::ExportOCAF2 ocaf(hDoc, getShapeColors);
            if (!legacyExport || !ocaf.canFallback(objs)) {
                ocaf.setExportOptions(ExportOCAF2::customExportOptions());
                ocaf.setExportHiddenObject(exportHidden);
                ocaf.setKeepPlacement(keepPlacement);

                ocaf.exportObjects(objs);
            }
            else {
                bool keepExplicitPlacement = true;
                ExportOCAFCmd ocaf(hDoc, keepExplicitPlacement);
                ocaf.setPartColorsMap(partColor);
                ocaf.exportObjects(objs);
            }

            Base::FileInfo file(Utf8Name.c_str());
            if (file.hasExtension({"stp", "step"})) {
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
        _PY_CATCH_OCC(return Py::None());

        return Py::None();
    }

    // This readDXF method is an almost exact duplicate of the one in ImportGui::Module.
    // The only difference is the CDxfRead class derivation that is created.
    // It would seem desirable to have most of this code in just one place, passing it
    // e.g. a pointer to a function that does the 4 lines during the lifetime of the
    // CDxfRead object, but right now Import::Module and ImportGui::Module cannot see
    // each other's functions so this shared code would need some place to live where
    // both places could include a declaration.
    Py::Object readDXF(const Py::Tuple& args, const Py::Dict &kwds)
    {
        char* Name = nullptr;
        const char* DocName = nullptr;
        const char* optionSource = nullptr;
        bool doRecompute = true;
        std::string defaultOptions = "User parameter:BaseApp/Preferences/Mod/Draft";
        bool IgnoreErrors = true;
        static std::array<const char*,6> kwd_list = {"filename","document","ignore_errors",
                                   "option_source","recompute",nullptr};
        if(!Base::Wrapped_ParseTupleAndKeywords(args.ptr(), kwds.ptr(), "et|sbsb", kwd_list,
                    "utf-8",&Name,&DocName,&IgnoreErrors,&optionSource,&doRecompute)) {
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
            ImpExpDxfRead dxf_file(EncodedName, pcDoc);
            dxf_file.setOptionSource(defaultOptions);
            dxf_file.setOptions();
            dxf_file.DoRead(IgnoreErrors);

            if (doRecompute)
                pcDoc->recompute();
        }
        _PY_CATCH_OCC(return Py::None());
        return Py::None();
    }

    static std::vector<std::pair<std::string, Part::TopoShape>> getShapes(const Py::Object &pyObj, bool autoTransform)
    {
        std::vector<std::pair<std::string, Part::TopoShape>> res;
        std::vector<App::DocumentObjectT> objs;
        Base::BoundBox3d bbox;
        auto shapes = Part::getPyShapes(pyObj.ptr(), &objs);
        auto itObj = objs.begin();
        for (const auto &shape : shapes) {
            std::string name;
            if (itObj != objs.end()) {
                name = itObj->getObjectLabel();
                ++itObj;
            }
            res.emplace_back(name, shape);
            if (autoTransform && !bbox.IsValid()) {
                bbox = shape.getBoundBox();
            }
        }
        if (autoTransform && bbox.IsValid()) {
            auto center = bbox.GetCenter();
            gp_Vec vcenter(center.x, center.y, center.z);
            for (auto &item : res) {
                auto &shape = item.second;
                gp_Pln pln;
                if (!shape.findPlane(pln))
                    continue;
                if (pln.Axis().IsParallel(gp_Ax1(), Precision::Angular()*10))
                    continue;
                gp_Trsf trsf;
                pln.SetLocation(gp_Pnt());
                trsf.SetTransformation(gp_Ax3(), pln.Position());
                trsf.SetTranslationPart(vcenter);
                gp_Trsf translate;
                translate.SetTranslation(-vcenter);
                shape.transformShape(Part::TopoShape::convert(trsf.Multiplied(translate)), false);
                Part::Feature::create(shape);
            }
        }
        return res;
    }

    Py::Object writeDXFShape(const Py::Tuple& args, const Py::Dict &kwds)
    {
        PyObject* shapeObj = nullptr;
        char* fname = nullptr;
        std::string filePath;
        std::string layerName;
        const char* optionSource = nullptr;
        std::string defaultOptions = "User parameter:BaseApp/Preferences/Mod/Import";
        int   versionParm = -1;
        bool  versionOverride = false;
        bool  polyOverride = false;
        PyObject *autoTransform = Py_True;
        PyObject *usePolyline = Py_False;

        static char* kwd_list[] = {"objs", "file_name","version_param","use_polyline","option_source","auto_transform",nullptr};
        if(!PyArg_ParseTupleAndKeywords(args.ptr(), kwds.ptr(), "Oet|iOsO", kwd_list,
                                                       &shapeObj, 
                                                       "utf-8",
                                                       &fname, 
                                                       &versionParm,
                                                       &usePolyline,
                                                       &optionSource,
                                                       &autoTransform)) {
            throw Py::TypeError("expected ([Shape],path");
        }

        filePath = std::string(fname);
        layerName = "none";
        PyMem_Free(fname);

        if ((versionParm == 12) ||
            (versionParm == 14)) {
            versionOverride = true;
        }
        if (usePolyline == Py_True) {
            polyOverride = true; 
        }
        if (optionSource) {
            defaultOptions = optionSource;
        }
        try {
            ImpExpDxfWrite writer(filePath);
            writer.setOptionSource(defaultOptions);
            writer.setOptions();
            if (versionOverride) {
                writer.setVersion(versionParm);
            }
            writer.setPolyOverride(polyOverride);
            writer.setLayerName(layerName);
            writer.init();

            auto shapeInfo = getShapes(Py::Object(shapeObj), PyObject_IsTrue(autoTransform));
            for (auto &item : shapeInfo) {
                if (item.first.empty())
                    item.first = layerName;
                writer.setLayerName(item.first);
                writer.exportShape(item.second.getShape());
            }
            writer.endRun();
            return Py::None();
        } _PY_CATCH_OCC(return Py::None());
    }

    Py::Object writeDXFObject(const Py::Tuple& args, const Py::Dict &kwds)
    {
        return writeDXFShape(args, kwds);
    }
};


PyObject* initModule()
{
    return Base::Interpreter().addModule(new Module);
}

}  // namespace Import
