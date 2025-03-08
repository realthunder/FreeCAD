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
# define WNT // avoid conflict with GUID
#endif
#ifndef _PreComp_
# include <climits>
# if defined(__clang__)
#  pragma clang diagnostic push
#  pragma clang diagnostic ignored "-Wextra-semi"
# endif
# include <APIHeaderSection_MakeHeader.hxx>
# include <IGESCAFControl_Reader.hxx>
# include <IGESCAFControl_Writer.hxx>
# include <IGESControl_Controller.hxx>
# include <IGESData_GlobalSection.hxx>
# include <IGESData_IGESModel.hxx>
# include <IGESToBRep_Actor.hxx>
# include <Interface_Static.hxx>
# include <OSD_Exception.hxx>
# include <Standard_Version.hxx>
# include <STEPCAFControl_Reader.hxx>
# include <STEPCAFControl_Writer.hxx>
# include <TColStd_IndexedDataMapOfStringString.hxx>
# include <TDocStd_Document.hxx>
# include <Transfer_TransientProcess.hxx>
# include <XCAFApp_Application.hxx>
# include <XCAFDoc_DocumentTool.hxx>
# include <XSControl_TransferReader.hxx>
# include <XSControl_WorkSession.hxx>
# if OCC_VERSION_HEX >= 0x070500
#  include <Message_ProgressRange.hxx>
#  include <RWGltf_CafWriter.hxx>
# endif
# if defined(__clang__)
#  pragma clang diagnostic pop
# endif
#endif

#include <App/Application.h>
#include <App/Document.h>
#include <App/DocumentObjectPy.h>
#include <App/DocumentObserver.h>
#include <Base/Console.h>
#include "dxf/ImpExpDxf.h"
#include <Mod/Part/App/encodeFilename.h>
#include <Mod/Part/App/ImportIges.h>
#include <Mod/Part/App/ImportStep.h>
#include <Mod/Part/App/Interface.h>
#include <Mod/Part/App/ProgressIndicator.h>
#include <Mod/Part/App/TopoShapePy.h>
#include <Mod/Part/App/PartFeaturePy.h>
#include <Mod/Part/App/OCAF/ImportExportSettings.h>
#include <Mod/Part/App/OCCError.h>
#include <Mod/Part/App/PartPyCXX.h>

#include "ImportOCAF2.h"


namespace Import {

class ImportOCAFExt : public Import::ImportOCAF2
{
public:
    ImportOCAFExt(Handle(TDocStd_Document) hStdDoc, App::Document* doc, const std::string& name)
        : ImportOCAF2(hStdDoc, doc, name)
    {
    }

    std::map<Part::Feature*, std::vector<App::Color> > partColors;

private:
    void applyFaceColors(Part::Feature* part, const std::vector<App::Color>& colors) override {
        partColors[part] = colors;
    }
};

class Module : public Py::ExtensionModule<Module>
{
public:
    Module() : Py::ExtensionModule<Module>("Import")
    {
        add_keyword_method("open",&Module::importer,
            "open(string) -- Open the file and create a new document."
        );
        add_keyword_method("insert",&Module::importer,
            "insert(string,string) -- Insert the file into the given document."
        );
        add_keyword_method("export",&Module::exporter,
            "export(list,string) -- Export a list of objects into a single file."
        );
         add_keyword_method("readDXF",&Module::readDXF,
            "readDXF(filename,[document,ignore_errors,option_source]): Imports a DXF file into the given document. ignore_errors is True by default."
        );
        add_keyword_method("writeDXFShape",&Module::writeDXFShape,
            "writeDXFShape([shape|objects],filename [version,usePolyline,optionSource]): Exports Shape(s) to a DXF file."
        );
        add_keyword_method("writeDXFObject",&Module::writeDXFObject,
            "writeDXFObject([objects|objects],filename [,version,usePolyline,optionSource]): Exports DocumentObject(s) to a DXF file."
        );
       initialize("This module is the Import module."); // register with Python       
    }

    ~Module() override = default;

private:
    Py::Object importer(const Py::Tuple& args, const Py::Dict &kwds)
    {
        char* Name;
        char* DocName=nullptr;
        PyObject *importHidden = Py_None;
        PyObject *merge = Py_None;
        PyObject *useLinkGroup = Py_None;
        PyObject *legacy = Py_None;
        int mode = -1;
        static char* kwd_list[] = {"name", "docName","importHidden","merge","useLinkGroup","mode","legacy",nullptr};
        if(!PyArg_ParseTupleAndKeywords(args.ptr(), kwds.ptr(), "et|sOOOiO", 
                    kwd_list,"utf-8",&Name,&DocName,&importHidden,&merge,&useLinkGroup,&mode,&legacy))
            throw Py::Exception();

        std::string Utf8Name = std::string(Name);
        PyMem_Free(Name);
        std::string name8bit = Part::encodeFilename(Utf8Name);

        try {
            Base::FileInfo file(Utf8Name.c_str());

            App::Document *pcDoc = nullptr;
            if (DocName) {
                pcDoc = App::GetApplication().getDocument(DocName);
            }
            if (!pcDoc) {
                pcDoc = App::GetApplication().newDocument();
            }

            Handle(XCAFApp_Application) hApp = XCAFApp_Application::GetApplication();
            Handle(TDocStd_Document) hDoc;
            hApp->NewDocument(TCollection_ExtendedString("MDTV-CAF"), hDoc);

            if (file.hasExtension("stp") || file.hasExtension("step")) {
                try {
                    STEPCAFControl_Reader aReader;
                    aReader.SetColorMode(true);
                    aReader.SetNameMode(true);
                    aReader.SetLayerMode(true);
                    if (aReader.ReadFile((Standard_CString)(name8bit.c_str())) != IFSelect_RetDone) {
                        throw Py::Exception(PyExc_IOError, "cannot read STEP file");
                    }

                    Handle(Message_ProgressIndicator) pi = new Part::ProgressIndicator(100);
#if OCC_VERSION_HEX < 0x070500
                    aReader.Reader().WS()->MapReader()->SetProgress(pi);
                    pi->NewScope(100, "Reading STEP file...");
                    pi->Show();
                    aReader.Transfer(hDoc);
#else
                    aReader.Transfer(hDoc, pi->Start());
#endif
#if OCC_VERSION_HEX < 0x070500
                    pi->EndScope();
#endif
                }
                catch (OSD_Exception& e) {
                    Base::Console().Error("%s\n", e.GetMessageString());
                    Base::Console().Message("Try to load STEP file without colors...\n");

                    Part::ImportStepParts(pcDoc,Utf8Name.c_str());
                    pcDoc->recompute();
                }
                _PY_CATCH_OCC(return Py::None());
            }
            else if (file.hasExtension("igs") || file.hasExtension("iges")) {
                Base::Reference<ParameterGrp> hGrp = App::GetApplication().GetUserParameter()
                    .GetGroup("BaseApp")->GetGroup("Preferences")->GetGroup("Mod/Part")->GetGroup("IGES");

                try {
                    IGESControl_Controller::Init();
                    IGESCAFControl_Reader aReader;
                    // http://www.opencascade.org/org/forum/thread_20603/?forum=3
                    aReader.SetReadVisible(hGrp->GetBool("SkipBlankEntities", true)
                        ? Standard_True : Standard_False);
                    aReader.SetColorMode(true);
                    aReader.SetNameMode(true);
                    aReader.SetLayerMode(true);
                    if (aReader.ReadFile((Standard_CString)(name8bit.c_str())) != IFSelect_RetDone) {
                        throw Py::Exception(PyExc_IOError, "cannot read IGES file");
                    }

                    Handle(Message_ProgressIndicator) pi = new Part::ProgressIndicator(100);
#if OCC_VERSION_HEX < 0x070500
                    aReader.WS()->MapReader()->SetProgress(pi);
                    pi->NewScope(100, "Reading IGES file...");
                    pi->Show();
                    aReader.Transfer(hDoc);
#else
                    aReader.Transfer(hDoc, pi->Start());
#endif
#if OCC_VERSION_HEX < 0x070500
                    pi->EndScope();
#endif
                    // http://opencascade.blogspot.de/2009/03/unnoticeable-memory-leaks-part-2.html
                    Handle(IGESToBRep_Actor)::DownCast(aReader.WS()->TransferReader()->Actor())
                            ->SetModel(new IGESData_IGESModel);
                }
                catch (OSD_Exception& e) {
                    Base::Console().Error("%s\n", e.GetMessageString());
                    Base::Console().Message("Try to load IGES file without colors...\n");

                    Part::ImportIgesParts(pcDoc,Utf8Name.c_str());
                    pcDoc->recompute();
                }
                _PY_CATCH_OCC(return Py::None());
            }
            else {
                throw Py::Exception(PyExc_IOError, "no supported file format");
            }

            ImportOCAFExt ocaf(hDoc, pcDoc, file.fileNamePure());
            ocaf.setImportOptions(ImportOCAFExt::customImportOptions());
            if (merge != Py_None)
                ocaf.setMerge(Base::asBoolean(merge));
            if (importHidden != Py_None)
                ocaf.setImportHiddenObject(Base::asBoolean(importHidden));
            if (useLinkGroup != Py_None)
                ocaf.setUseLinkGroup(Base::asBoolean(useLinkGroup));
            if (legacy!=Py_None)
                ocaf.setUseLegacyImporter(Base::asBoolean(legacy));
            if (mode >= 0)
                ocaf.setMode(mode);
            ocaf.loadShapes();

            hApp->Close(hDoc);

            if (!ocaf.partColors.empty()) {
                Py::List list;
                for (auto &it : ocaf.partColors) {
                    Py::Tuple tuple(2);
                    tuple.setItem(0, Py::asObject(it.first->getPyObject()));

                    App::PropertyColorList colors;
                    colors.setValues(it.second);
                    tuple.setItem(1, Py::asObject(colors.getPyObject()));

                    list.append(tuple);
                }

                return list;
            }
        }
        _PY_CATCH_OCC(return Py::None());

        return Py::None();
    }
    Py::Object exporter(const Py::Tuple& args, const Py::Dict &kwds)
    {
        PyObject* object;
        char* Name;
        PyObject *exportHidden = Py_None;
        PyObject *legacy = Py_None;
        PyObject *keepPlacement = Py_None;
        static char* kwd_list[] = {"obj", "name", "exportHidden", "legacy", "keepPlacement",nullptr};
        if (!PyArg_ParseTupleAndKeywords(args.ptr(), kwds.ptr(), "Oet|O!O!O!",
                    kwd_list,&object,"utf-8",&Name,&PyBool_Type,&exportHidden,&PyBool_Type,&legacy,
                    &PyBool_Type,&keepPlacement))
            throw Py::Exception();

        std::string Utf8Name = std::string(Name);
        PyMem_Free(Name);
        std::string name8bit = Part::encodeFilename(Utf8Name);

        try {
            Py::Sequence list(object);
            Handle(XCAFApp_Application) hApp = XCAFApp_Application::GetApplication();
            Handle(TDocStd_Document) hDoc;
            hApp->NewDocument(TCollection_ExtendedString("MDTV-CAF"), hDoc);

            std::vector<App::DocumentObject*> objs;
            for (Py::Sequence::iterator it = list.begin(); it != list.end(); ++it) {
                PyObject* item = (*it).ptr();
                if (PyObject_TypeCheck(item, &(App::DocumentObjectPy::Type)))
                    objs.push_back(static_cast<App::DocumentObjectPy*>(item)->getDocumentObjectPtr());
            }

            if (legacy == Py_None) {
                Part::OCAF::ImportExportSettings settings;
                legacy = settings.getExportLegacy() ? Py_True : Py_False;
            }

            Import::ExportOCAF2 ocaf(hDoc);
            if (!Base::asBoolean(legacy) || !ocaf.canFallback(objs)) {
                ocaf.setExportOptions(ExportOCAF2::customExportOptions());

                if (exportHidden != Py_None)
                    ocaf.setExportHiddenObject(Base::asBoolean(exportHidden));
                if (keepPlacement != Py_None)
                    ocaf.setKeepPlacement(Base::asBoolean(keepPlacement));

                ocaf.exportObjects(objs);
            }
            else {
                bool keepExplicitPlacement = Standard_True;
                ExportOCAF ocaf(hDoc, keepExplicitPlacement);
                // That stuff is exporting a list of selected objects into FreeCAD Tree
                std::vector <TDF_Label> hierarchical_label;
                std::vector <TopLoc_Location> hierarchical_loc;
                std::vector <App::DocumentObject*> hierarchical_part;
                for(auto obj : objs)
                    ocaf.exportObject(obj,hierarchical_label, hierarchical_loc,hierarchical_part);

                // Free Shapes must have absolute placement and not explicit
                std::vector <TDF_Label> FreeLabels;
                std::vector <int> part_id;
                ocaf.getFreeLabels(hierarchical_label,FreeLabels, part_id);
                // Update is not performed automatically anymore: https://tracker.dev.opencascade.org/view.php?id=28055
                XCAFDoc_DocumentTool::ShapeTool(hDoc->Main())->UpdateAssemblies();
            }

            Base::FileInfo file(Utf8Name.c_str());
            if (file.hasExtension("stp") || file.hasExtension("step")) {
                STEPCAFControl_Writer writer;
                Part::Interface::writeStepAssembly(Part::Interface::Assembly::On);
                writer.Transfer(hDoc, STEPControl_AsIs);

                APIHeaderSection_MakeHeader makeHeader(writer.ChangeWriter().Model());
                Base::Reference<ParameterGrp> hGrp = App::GetApplication().GetUserParameter()
                    .GetGroup("BaseApp")->GetGroup("Preferences")->GetGroup("Mod/Part")->GetGroup("STEP");

                // Don't set name because STEP doesn't support UTF-8
                // https://forum.freecad.org/viewtopic.php?f=8&t=52967
                makeHeader.SetAuthorValue (1, new TCollection_HAsciiString(hGrp->GetASCII("Author", "Author").c_str()));
                makeHeader.SetOrganizationValue (1, new TCollection_HAsciiString(hGrp->GetASCII("Company").c_str()));
                makeHeader.SetOriginatingSystem(new TCollection_HAsciiString(App::Application::getExecutableName().c_str()));
                makeHeader.SetDescriptionValue(1, new TCollection_HAsciiString("FreeCAD Model"));
                IFSelect_ReturnStatus ret = writer.Write(name8bit.c_str());
                if (ret == IFSelect_RetError || ret == IFSelect_RetFail || ret == IFSelect_RetStop) {
                    PyErr_Format(PyExc_IOError, "Cannot open file '%s'", Utf8Name.c_str());
                    throw Py::Exception();
                }
            }
            else if (file.hasExtension("igs") || file.hasExtension("iges")) {
                IGESControl_Controller::Init();
                IGESCAFControl_Writer writer;
                IGESData_GlobalSection header = writer.Model()->GlobalSection();
                header.SetAuthorName(new TCollection_HAsciiString(Part::Interface::writeIgesHeaderAuthor()));
                header.SetCompanyName(new TCollection_HAsciiString(Part::Interface::writeIgesHeaderCompany()));
                header.SetSendName(new TCollection_HAsciiString(Part::Interface::writeIgesHeaderProduct()));
                writer.Model()->SetGlobalSection(header);
                writer.Transfer(hDoc);
                Standard_Boolean ret = writer.Write(name8bit.c_str());
                if (!ret) {
                    PyErr_Format(PyExc_IOError, "Cannot open file '%s'", Utf8Name.c_str());
                    throw Py::Exception();
                }
            }
            else if (file.hasExtension("glb") || file.hasExtension("gltf")) {
#if OCC_VERSION_HEX >= 0x070500
                TColStd_IndexedDataMapOfStringString aMetadata;
                RWGltf_CafWriter aWriter (name8bit.c_str(), file.hasExtension("glb"));
                aWriter.SetTransformationFormat (RWGltf_WriterTrsfFormat_Compact);
                // https://github.com/KhronosGroup/glTF/blob/master/specification/2.0/README.md#coordinate-system-and-units
                aWriter.ChangeCoordinateSystemConverter().SetInputLengthUnit (0.001);
                aWriter.ChangeCoordinateSystemConverter().SetInputCoordinateSystem (RWMesh_CoordinateSystem_Zup);
#if OCC_VERSION_HEX >= 0x070700
                aWriter.SetParallel(true);
#endif
                Standard_Boolean ret = aWriter.Perform (hDoc, aMetadata, Message_ProgressRange());
                if (!ret) {
                    PyErr_Format(PyExc_IOError, "Cannot save to file '%s'", Utf8Name.c_str());
                    throw Py::Exception();
                }
#else
                throw Py::RuntimeError("gITF support requires OCCT 7.5.0 or later");
#endif
            }

            hApp->Close(hDoc);
        }
        _PY_CATCH_OCC(return Py::None());

        return Py::None();
    }

    Py::Object readDXF(const Py::Tuple& args, const Py::Dict &kwds)
    {
        char* Name;
        const char* DocName=nullptr;
        const char* optionSource = nullptr;
        bool doRecompute = true;
        std::string defaultOptions = "User parameter:BaseApp/Preferences/Mod/Draft";
        bool IgnoreErrors=true;
        static char* kwd_list[] = {"filename","document","ignore_errors",
                                   "option_source","recompute",nullptr};
        if(!PyArg_ParseTupleAndKeywords(args.ptr(), kwds.ptr(), "et|sbsb", kwd_list,
                    "utf-8",&Name,&DocName,&IgnoreErrors,&optionSource,&doRecompute))
            throw Py::Exception();

        std::string EncodedName = std::string(Name);
        PyMem_Free(Name);

        Base::FileInfo file(EncodedName.c_str());
        if (!file.exists())
            throw Py::RuntimeError("File doesn't exist");

        if (optionSource)
            defaultOptions = optionSource;

        App::Document *pcDoc;
        if (DocName)
            pcDoc = App::GetApplication().getDocument(DocName);
        else
            pcDoc = App::GetApplication().getActiveDocument();
        if (!pcDoc) 
            pcDoc = App::GetApplication().newDocument(DocName);

        try {
            // read the DXF file
            ImpExpDxfRead dxf_file(EncodedName,pcDoc);
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
        PyObject *shapeObj;
        char* fname;
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

} // namespace Import
