/****************************************************************************
 *   Copyright (c) 2020 Zheng Lei (realthunder) <realthunder.dev@gmail.com> *
 *                                                                          *
 *   This file is part of the FreeCAD CAx development system.               *
 *                                                                          *
 *   This library is free software; you can redistribute it and/or          *
 *   modify it under the terms of the GNU Library General Public            *
 *   License as published by the Free Software Foundation; either           *
 *   version 2 of the License, or (at your option) any later version.       *
 *                                                                          *
 *   This library  is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of         *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the          *
 *   GNU Library General Public License for more details.                   *
 *                                                                          *
 *   You should have received a copy of the GNU Library General Public      *
 *   License along with this library; see the file COPYING.LIB. If not,     *
 *   write to the Free Software Foundation, Inc., 59 Temple Place,          *
 *   Suite 330, Boston, MA  02111-1307, USA                                 *
 *                                                                          *
 ****************************************************************************/

#include "PreCompiled.h"


/*[[[cog
import DocumentParams
DocumentParams.define()
]]]*/

// Auto generated code (Tools/params_utils.py:198)
#include <unordered_map>
#include <App/Application.h>
#include <App/DynamicProperty.h>
#include <App/ParamRegistry.h>
#include "DocumentParams.h"
using namespace App;

// Auto generated code (Tools/params_utils.py:210)
namespace {
class DocumentParamsP: public ParameterGrp::ObserverType {
public:
    ParameterGrp::handle handle;
    std::unordered_map<const char *,void(*)(DocumentParamsP*),App::CStringHasher,App::CStringHasher> funcs;
    // Auto generated code (Tools/params_utils.py:228)
    fastsignals::signal<void (const char*)> signalParamChanged;
    void signalAll()
    {
        signalParamChanged("prefAuthor");
        signalParamChanged("prefSetAuthorOnSave");
        signalParamChanged("prefCompany");
        signalParamChanged("prefLicenseType");
        signalParamChanged("prefLicenseUrl");
        signalParamChanged("CompressionLevel");
        signalParamChanged("CheckExtension");
        signalParamChanged("ForceXML");
        signalParamChanged("SplitXML");
        signalParamChanged("PreferBinary");
        signalParamChanged("InlineListSize");
        signalParamChanged("ArchiveRandomAccess");
        signalParamChanged("ArchiveBlobStore");
        signalParamChanged("DeferShapeLoad");
        signalParamChanged("SaveMaterialCards");
        signalParamChanged("DedupShapePCurves");
        signalParamChanged("DedupCongruentShapes");
        signalParamChanged("DedupCrossFileGeometry");
        signalParamChanged("AutoRemoveFile");
        signalParamChanged("AutoNameDynamicProperty");
        signalParamChanged("BackupPolicy");
        signalParamChanged("CreateBackupFiles");
        signalParamChanged("UseFCBakExtension");
        signalParamChanged("SaveBackupDateFormat");
        signalParamChanged("CountBackupFiles");
        signalParamChanged("OptimizeRecompute");
        signalParamChanged("CanAbortRecompute");
        signalParamChanged("UseHasher");
        signalParamChanged("ViewObjectTransaction");
        signalParamChanged("WarnRecomputeOnRestore");
        signalParamChanged("NoPartialLoading");
        signalParamChanged("SaveThumbnail");
        signalParamChanged("ThumbnailNoBackground");
        signalParamChanged("AddThumbnailLogo");
        signalParamChanged("ThumbnailSampleSize");
        signalParamChanged("ThumbnailSize");
        signalParamChanged("DuplicateLabels");
        signalParamChanged("TransactionOnRecompute");
        signalParamChanged("RelativeStringID");
        signalParamChanged("HashIndexedName");
        signalParamChanged("EnableMaterialEdit");
        signalParamChanged("MCPServerAutoStart");
        signalParamChanged("MCPServerPort");

    // Auto generated code (Tools/params_utils.py:241)
    }
    std::string prefAuthor;
    bool prefSetAuthorOnSave;
    std::string prefCompany;
    long prefLicenseType;
    std::string prefLicenseUrl;
    long CompressionLevel;
    bool CheckExtension;
    long ForceXML;
    bool SplitXML;
    bool PreferBinary;
    long InlineListSize;
    bool ArchiveRandomAccess;
    bool ArchiveBlobStore;
    bool DeferShapeLoad;
    bool SaveMaterialCards;
    bool DedupShapePCurves;
    bool DedupCongruentShapes;
    bool DedupCrossFileGeometry;
    bool AutoRemoveFile;
    bool AutoNameDynamicProperty;
    bool BackupPolicy;
    bool CreateBackupFiles;
    bool UseFCBakExtension;
    std::string SaveBackupDateFormat;
    long CountBackupFiles;
    bool OptimizeRecompute;
    bool CanAbortRecompute;
    bool UseHasher;
    bool ViewObjectTransaction;
    bool WarnRecomputeOnRestore;
    bool NoPartialLoading;
    bool SaveThumbnail;
    bool ThumbnailNoBackground;
    bool AddThumbnailLogo;
    long ThumbnailSampleSize;
    long ThumbnailSize;
    bool DuplicateLabels;
    bool TransactionOnRecompute;
    bool RelativeStringID;
    bool HashIndexedName;
    bool EnableMaterialEdit;
    bool MCPServerAutoStart;
    long MCPServerPort;

    // Auto generated code (Tools/params_utils.py:254)
    DocumentParamsP() {
        handle = App::GetApplication().GetParameterGroupByPath("User parameter:BaseApp/Preferences/Document");
        handle->Attach(this);

        prefAuthor = this->handle->GetASCII("prefAuthor", "");
        funcs["prefAuthor"] = &DocumentParamsP::updateprefAuthor;
        prefSetAuthorOnSave = this->handle->GetBool("prefSetAuthorOnSave", false);
        funcs["prefSetAuthorOnSave"] = &DocumentParamsP::updateprefSetAuthorOnSave;
        prefCompany = this->handle->GetASCII("prefCompany", "");
        funcs["prefCompany"] = &DocumentParamsP::updateprefCompany;
        prefLicenseType = this->handle->GetInt("prefLicenseType", 0);
        funcs["prefLicenseType"] = &DocumentParamsP::updateprefLicenseType;
        prefLicenseUrl = this->handle->GetASCII("prefLicenseUrl", "");
        funcs["prefLicenseUrl"] = &DocumentParamsP::updateprefLicenseUrl;
        CompressionLevel = this->handle->GetInt("CompressionLevel", 3);
        funcs["CompressionLevel"] = &DocumentParamsP::updateCompressionLevel;
        CheckExtension = this->handle->GetBool("CheckExtension", true);
        funcs["CheckExtension"] = &DocumentParamsP::updateCheckExtension;
        ForceXML = this->handle->GetInt("ForceXML", 3);
        funcs["ForceXML"] = &DocumentParamsP::updateForceXML;
        SplitXML = this->handle->GetBool("SplitXML", true);
        funcs["SplitXML"] = &DocumentParamsP::updateSplitXML;
        PreferBinary = this->handle->GetBool("PreferBinary", false);
        funcs["PreferBinary"] = &DocumentParamsP::updatePreferBinary;
        InlineListSize = this->handle->GetInt("InlineListSize", 64);
        funcs["InlineListSize"] = &DocumentParamsP::updateInlineListSize;
        ArchiveRandomAccess = this->handle->GetBool("ArchiveRandomAccess", true);
        funcs["ArchiveRandomAccess"] = &DocumentParamsP::updateArchiveRandomAccess;
        ArchiveBlobStore = this->handle->GetBool("ArchiveBlobStore", true);
        funcs["ArchiveBlobStore"] = &DocumentParamsP::updateArchiveBlobStore;
        DeferShapeLoad = this->handle->GetBool("DeferShapeLoad", true);
        funcs["DeferShapeLoad"] = &DocumentParamsP::updateDeferShapeLoad;
        SaveMaterialCards = this->handle->GetBool("SaveMaterialCards", true);
        funcs["SaveMaterialCards"] = &DocumentParamsP::updateSaveMaterialCards;
        DedupShapePCurves = this->handle->GetBool("DedupShapePCurves", true);
        funcs["DedupShapePCurves"] = &DocumentParamsP::updateDedupShapePCurves;
        DedupCongruentShapes = this->handle->GetBool("DedupCongruentShapes", true);
        funcs["DedupCongruentShapes"] = &DocumentParamsP::updateDedupCongruentShapes;
        DedupCrossFileGeometry = this->handle->GetBool("DedupCrossFileGeometry", false);
        funcs["DedupCrossFileGeometry"] = &DocumentParamsP::updateDedupCrossFileGeometry;
        AutoRemoveFile = this->handle->GetBool("AutoRemoveFile", true);
        funcs["AutoRemoveFile"] = &DocumentParamsP::updateAutoRemoveFile;
        AutoNameDynamicProperty = this->handle->GetBool("AutoNameDynamicProperty", false);
        funcs["AutoNameDynamicProperty"] = &DocumentParamsP::updateAutoNameDynamicProperty;
        BackupPolicy = this->handle->GetBool("BackupPolicy", true);
        funcs["BackupPolicy"] = &DocumentParamsP::updateBackupPolicy;
        CreateBackupFiles = this->handle->GetBool("CreateBackupFiles", true);
        funcs["CreateBackupFiles"] = &DocumentParamsP::updateCreateBackupFiles;
        UseFCBakExtension = this->handle->GetBool("UseFCBakExtension", false);
        funcs["UseFCBakExtension"] = &DocumentParamsP::updateUseFCBakExtension;
        SaveBackupDateFormat = this->handle->GetASCII("SaveBackupDateFormat", "%Y%m%d-%H%M%S");
        funcs["SaveBackupDateFormat"] = &DocumentParamsP::updateSaveBackupDateFormat;
        CountBackupFiles = this->handle->GetInt("CountBackupFiles", 1);
        funcs["CountBackupFiles"] = &DocumentParamsP::updateCountBackupFiles;
        OptimizeRecompute = this->handle->GetBool("OptimizeRecompute", true);
        funcs["OptimizeRecompute"] = &DocumentParamsP::updateOptimizeRecompute;
        CanAbortRecompute = this->handle->GetBool("CanAbortRecompute", true);
        funcs["CanAbortRecompute"] = &DocumentParamsP::updateCanAbortRecompute;
        UseHasher = this->handle->GetBool("UseHasher", true);
        funcs["UseHasher"] = &DocumentParamsP::updateUseHasher;
        ViewObjectTransaction = this->handle->GetBool("ViewObjectTransaction", false);
        funcs["ViewObjectTransaction"] = &DocumentParamsP::updateViewObjectTransaction;
        WarnRecomputeOnRestore = this->handle->GetBool("WarnRecomputeOnRestore", true);
        funcs["WarnRecomputeOnRestore"] = &DocumentParamsP::updateWarnRecomputeOnRestore;
        NoPartialLoading = this->handle->GetBool("NoPartialLoading", false);
        funcs["NoPartialLoading"] = &DocumentParamsP::updateNoPartialLoading;
        SaveThumbnail = this->handle->GetBool("SaveThumbnail", false);
        funcs["SaveThumbnail"] = &DocumentParamsP::updateSaveThumbnail;
        ThumbnailNoBackground = this->handle->GetBool("ThumbnailNoBackground", false);
        funcs["ThumbnailNoBackground"] = &DocumentParamsP::updateThumbnailNoBackground;
        AddThumbnailLogo = this->handle->GetBool("AddThumbnailLogo", true);
        funcs["AddThumbnailLogo"] = &DocumentParamsP::updateAddThumbnailLogo;
        ThumbnailSampleSize = this->handle->GetInt("ThumbnailSampleSize", 0);
        funcs["ThumbnailSampleSize"] = &DocumentParamsP::updateThumbnailSampleSize;
        ThumbnailSize = this->handle->GetInt("ThumbnailSize", 128);
        funcs["ThumbnailSize"] = &DocumentParamsP::updateThumbnailSize;
        DuplicateLabels = this->handle->GetBool("DuplicateLabels", false);
        funcs["DuplicateLabels"] = &DocumentParamsP::updateDuplicateLabels;
        TransactionOnRecompute = this->handle->GetBool("TransactionOnRecompute", false);
        funcs["TransactionOnRecompute"] = &DocumentParamsP::updateTransactionOnRecompute;
        RelativeStringID = this->handle->GetBool("RelativeStringID", true);
        funcs["RelativeStringID"] = &DocumentParamsP::updateRelativeStringID;
        HashIndexedName = this->handle->GetBool("HashIndexedName", true);
        funcs["HashIndexedName"] = &DocumentParamsP::updateHashIndexedName;
        EnableMaterialEdit = this->handle->GetBool("EnableMaterialEdit", true);
        funcs["EnableMaterialEdit"] = &DocumentParamsP::updateEnableMaterialEdit;
        MCPServerAutoStart = this->handle->GetBool("MCPServerAutoStart", false);
        funcs["MCPServerAutoStart"] = &DocumentParamsP::updateMCPServerAutoStart;
        MCPServerPort = this->handle->GetInt("MCPServerPort", 8765);
        funcs["MCPServerPort"] = &DocumentParamsP::updateMCPServerPort;
    }

    // Auto generated code (Tools/params_utils.py:284)
    ~DocumentParamsP() override = default;

    // Auto generated code (Tools/params_utils.py:297)
    void OnChange(Base::Subject<const char*> &, const char* sReason) override {
        if(!sReason)
            return;
        auto it = funcs.find(sReason);
        if(it == funcs.end())
            return;
        it->second(this);
        signalParamChanged(sReason);
    }


    // Auto generated code (Tools/params_utils.py:314)
    static void updateprefAuthor(DocumentParamsP *self) {
        self->prefAuthor = self->handle->GetASCII("prefAuthor", "");
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateprefSetAuthorOnSave(DocumentParamsP *self) {
        self->prefSetAuthorOnSave = self->handle->GetBool("prefSetAuthorOnSave", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateprefCompany(DocumentParamsP *self) {
        self->prefCompany = self->handle->GetASCII("prefCompany", "");
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateprefLicenseType(DocumentParamsP *self) {
        self->prefLicenseType = self->handle->GetInt("prefLicenseType", 0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateprefLicenseUrl(DocumentParamsP *self) {
        self->prefLicenseUrl = self->handle->GetASCII("prefLicenseUrl", "");
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateCompressionLevel(DocumentParamsP *self) {
        self->CompressionLevel = self->handle->GetInt("CompressionLevel", 3);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateCheckExtension(DocumentParamsP *self) {
        self->CheckExtension = self->handle->GetBool("CheckExtension", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateForceXML(DocumentParamsP *self) {
        self->ForceXML = self->handle->GetInt("ForceXML", 3);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateSplitXML(DocumentParamsP *self) {
        self->SplitXML = self->handle->GetBool("SplitXML", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updatePreferBinary(DocumentParamsP *self) {
        self->PreferBinary = self->handle->GetBool("PreferBinary", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateInlineListSize(DocumentParamsP *self) {
        self->InlineListSize = self->handle->GetInt("InlineListSize", 64);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateArchiveRandomAccess(DocumentParamsP *self) {
        self->ArchiveRandomAccess = self->handle->GetBool("ArchiveRandomAccess", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateArchiveBlobStore(DocumentParamsP *self) {
        self->ArchiveBlobStore = self->handle->GetBool("ArchiveBlobStore", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateDeferShapeLoad(DocumentParamsP *self) {
        self->DeferShapeLoad = self->handle->GetBool("DeferShapeLoad", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateSaveMaterialCards(DocumentParamsP *self) {
        self->SaveMaterialCards = self->handle->GetBool("SaveMaterialCards", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateDedupShapePCurves(DocumentParamsP *self) {
        self->DedupShapePCurves = self->handle->GetBool("DedupShapePCurves", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateDedupCongruentShapes(DocumentParamsP *self) {
        self->DedupCongruentShapes = self->handle->GetBool("DedupCongruentShapes", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateDedupCrossFileGeometry(DocumentParamsP *self) {
        self->DedupCrossFileGeometry = self->handle->GetBool("DedupCrossFileGeometry", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateAutoRemoveFile(DocumentParamsP *self) {
        self->AutoRemoveFile = self->handle->GetBool("AutoRemoveFile", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateAutoNameDynamicProperty(DocumentParamsP *self) {
        self->AutoNameDynamicProperty = self->handle->GetBool("AutoNameDynamicProperty", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateBackupPolicy(DocumentParamsP *self) {
        self->BackupPolicy = self->handle->GetBool("BackupPolicy", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateCreateBackupFiles(DocumentParamsP *self) {
        self->CreateBackupFiles = self->handle->GetBool("CreateBackupFiles", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateUseFCBakExtension(DocumentParamsP *self) {
        self->UseFCBakExtension = self->handle->GetBool("UseFCBakExtension", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateSaveBackupDateFormat(DocumentParamsP *self) {
        self->SaveBackupDateFormat = self->handle->GetASCII("SaveBackupDateFormat", "%Y%m%d-%H%M%S");
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateCountBackupFiles(DocumentParamsP *self) {
        self->CountBackupFiles = self->handle->GetInt("CountBackupFiles", 1);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateOptimizeRecompute(DocumentParamsP *self) {
        self->OptimizeRecompute = self->handle->GetBool("OptimizeRecompute", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateCanAbortRecompute(DocumentParamsP *self) {
        self->CanAbortRecompute = self->handle->GetBool("CanAbortRecompute", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateUseHasher(DocumentParamsP *self) {
        self->UseHasher = self->handle->GetBool("UseHasher", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateViewObjectTransaction(DocumentParamsP *self) {
        self->ViewObjectTransaction = self->handle->GetBool("ViewObjectTransaction", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateWarnRecomputeOnRestore(DocumentParamsP *self) {
        self->WarnRecomputeOnRestore = self->handle->GetBool("WarnRecomputeOnRestore", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateNoPartialLoading(DocumentParamsP *self) {
        self->NoPartialLoading = self->handle->GetBool("NoPartialLoading", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateSaveThumbnail(DocumentParamsP *self) {
        self->SaveThumbnail = self->handle->GetBool("SaveThumbnail", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateThumbnailNoBackground(DocumentParamsP *self) {
        self->ThumbnailNoBackground = self->handle->GetBool("ThumbnailNoBackground", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateAddThumbnailLogo(DocumentParamsP *self) {
        self->AddThumbnailLogo = self->handle->GetBool("AddThumbnailLogo", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateThumbnailSampleSize(DocumentParamsP *self) {
        self->ThumbnailSampleSize = self->handle->GetInt("ThumbnailSampleSize", 0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateThumbnailSize(DocumentParamsP *self) {
        self->ThumbnailSize = self->handle->GetInt("ThumbnailSize", 128);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateDuplicateLabels(DocumentParamsP *self) {
        self->DuplicateLabels = self->handle->GetBool("DuplicateLabels", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateTransactionOnRecompute(DocumentParamsP *self) {
        self->TransactionOnRecompute = self->handle->GetBool("TransactionOnRecompute", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateRelativeStringID(DocumentParamsP *self) {
        self->RelativeStringID = self->handle->GetBool("RelativeStringID", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateHashIndexedName(DocumentParamsP *self) {
        self->HashIndexedName = self->handle->GetBool("HashIndexedName", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateEnableMaterialEdit(DocumentParamsP *self) {
        self->EnableMaterialEdit = self->handle->GetBool("EnableMaterialEdit", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateMCPServerAutoStart(DocumentParamsP *self) {
        self->MCPServerAutoStart = self->handle->GetBool("MCPServerAutoStart", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateMCPServerPort(DocumentParamsP *self) {
        self->MCPServerPort = self->handle->GetInt("MCPServerPort", 8765);
    }
};

// Auto generated code (Tools/params_utils.py:336)
DocumentParamsP *instance() {
    static DocumentParamsP *inst = new DocumentParamsP;
    return inst;
}

} // Anonymous namespace

// Auto generated code (Tools/params_utils.py:352)
static const App::ParamRegistry::Registrar _DocumentParamsRegistrar({
    App::ParamInfo("App", "DocumentParams", "User parameter:BaseApp/Preferences/Document", "prefAuthor", "prefAuthor", App::ParamInfo::String, "")
        .setTitle("Author name")
        .setDoc("Author name given to new documents as their creator. Also written as\n"
"the last modifier on save when that option is on. Leave empty to stay\n"
"anonymous."),
    App::ParamInfo("App", "DocumentParams", "User parameter:BaseApp/Preferences/Document", "prefSetAuthorOnSave", "prefSetAuthorOnSave", App::ParamInfo::Bool, false)
        .setTitle("Set author on save")
        .setDoc("Write the author name from the preferences into a document's 'Last\n"
"modified by' field each time it is saved."),
    App::ParamInfo("App", "DocumentParams", "User parameter:BaseApp/Preferences/Document", "prefCompany", "prefCompany", App::ParamInfo::String, "")
        .setTitle("Company")
        .setDoc("Company name given to new documents."),
    App::ParamInfo("App", "DocumentParams", "User parameter:BaseApp/Preferences/Document", "prefLicenseType", "prefLicenseType", App::ParamInfo::Int, 0)
        .setTitle("Default license")
        .setDoc("License given to new documents, as a position in the license list.\n"
"0 is All rights reserved, 1 to 12 the Creative Commons licenses,\n"
"13 Public Domain, 14 FreeArt, 15 to 17 the CERN hardware licences;\n"
"18 (Other) leaves the license empty."),
    App::ParamInfo("App", "DocumentParams", "User parameter:BaseApp/Preferences/Document", "prefLicenseUrl", "prefLicenseUrl", App::ParamInfo::String, "")
        .setTitle("License URL")
        .setDoc("Address of the license text given to new documents. Empty uses the\n"
"address that belongs to the license chosen from the list."),
    App::ParamInfo("App", "DocumentParams", "User parameter:BaseApp/Preferences/Document", "CompressionLevel", "CompressionLevel", App::ParamInfo::Int, 3)
        .setTitle("Compression Level")
        .setDoc("How hard a document file is compressed when saved, from 0 (none,\n"
"fastest) to 9 (smallest, slowest). Has no effect on a document saved\n"
"as a directory."),
    App::ParamInfo("App", "DocumentParams", "User parameter:BaseApp/Preferences/Document", "CheckExtension", "CheckExtension", App::ParamInfo::Bool, true)
        .setTitle("Check Extension")
        .setDoc("Add .FCStd to the file name when a document is saved under a name\n"
"without that extension, so that a save cannot overwrite an unrelated\n"
"file by accident."),
    App::ParamInfo("App", "DocumentParams", "User parameter:BaseApp/Preferences/Document", "ForceXML", "ForceXML", App::ParamInfo::Int, 3)
        .setTitle("Force XML")
        .setDoc("How much object data new documents keep inside the XML when saved\n"
"as a directory. 0 none, 1 lists, 2 also meshes, points and text\n"
"shapes, 3 also binary shapes, 4 and up also included files."),
    App::ParamInfo("App", "DocumentParams", "User parameter:BaseApp/Preferences/Document", "SplitXML", "SplitXML", App::ParamInfo::Bool, true)
        .setTitle("Split XML")
        .setDoc("Give each object an XML file of its own in new documents saved as a\n"
"directory, instead of one file for the whole document. Has no effect\n"
"on a document saved as a single file."),
    App::ParamInfo("App", "DocumentParams", "User parameter:BaseApp/Preferences/Document", "PreferBinary", "PreferBinary", App::ParamInfo::Bool, false)
        .setTitle("Prefer Binary")
        .setDoc("Save the object data of new documents in binary instead of text\n"
"form. Files get smaller but compare poorly under version control.\n"
"Each document carries its own copy of this choice."),
    App::ParamInfo("App", "DocumentParams", "User parameter:BaseApp/Preferences/Document", "InlineListSize", "InlineListSize", App::ParamInfo::Int, 64)
        .setTitle("Inline List Size")
        .setDoc("Largest list property, in bytes, still written inside the document\n"
"XML instead of as a separate entry of the file. Small lists are\n"
"cheaper inline. 0 gives every list its own entry."),
    App::ParamInfo("App", "DocumentParams", "User parameter:BaseApp/Preferences/Document", "ArchiveRandomAccess", "ArchiveRandomAccess", App::ParamInfo::Bool, true)
        .setTitle("Archive Random Access")
        .setDoc("Restore a document archive through its zip central directory\n"
"instead of one forward-only stream. Entries are then opened\n"
"independently and served in registration order whatever their\n"
"archive order, nothing pays for inflating entries nobody reads,\n"
"and an entry can be reopened after the restore. Turn off to\n"
"fall back to the forward-only walk."),
    App::ParamInfo("App", "DocumentParams", "User parameter:BaseApp/Preferences/Document", "ArchiveBlobStore", "ArchiveBlobStore", App::ParamInfo::Bool, true)
        .setTitle("Archive Blob Store")
        .setDoc("Serve the included files of a document archive out of one copy\n"
"of the archive in the transient directory, and give each its own\n"
"file only when something asks for a path. Requires\n"
"ArchiveRandomAccess. Turn off to write every included file out\n"
"during the restore, which on a monitored filesystem costs a file\n"
"create per entry."),
    App::ParamInfo("App", "DocumentParams", "User parameter:BaseApp/Preferences/Document", "DeferShapeLoad", "DeferShapeLoad", App::ParamInfo::Bool, true)
        .setTitle("Defer Shape Load")
        .setDoc("When opening a document, read each shape on first use instead of\n"
"before the window comes up. Requires ArchiveRandomAccess. The file\n"
"must not be rewritten by another program while shapes are still to be\n"
"read. Off by default."),
    App::ParamInfo("App", "DocumentParams", "User parameter:BaseApp/Preferences/Document", "SaveMaterialCards", "SaveMaterialCards", App::ParamInfo::Bool, true)
        .setTitle("Save Material Cards")
        .setDoc("Write every material card used into the document, including the\n"
"stock ones, so the document does not depend on the installed material\n"
"library. Off writes only a reference to a stock card, which is lost\n"
"if the library changes."),
    App::ParamInfo("App", "DocumentParams", "User parameter:BaseApp/Preferences/Document", "DedupShapePCurves", "DedupShapePCurves", App::ParamInfo::Bool, true)
        .setTitle("Dedup Shape PCurves")
        .setDoc("Write each 2D curve of a shape once, and leave out those on planar\n"
"faces, which are computed again on reading. The geometry read back is\n"
"the same. Applies to shapes written as ASCII BRep."),
    App::ParamInfo("App", "DocumentParams", "User parameter:BaseApp/Preferences/Document", "DedupCongruentShapes", "DedupCongruentShapes", App::ParamInfo::Bool, true)
        .setTitle("Dedup Congruent Shapes")
        .setDoc("Store one shape file for parts that are the same shape in different\n"
"places, and record the motion between them. Parts are merged only\n"
"after the motion has been checked sub-shape by sub-shape."),
    App::ParamInfo("App", "DocumentParams", "User parameter:BaseApp/Preferences/Document", "DedupCrossFileGeometry", "DedupCrossFileGeometry", App::ParamInfo::Bool, false)
        .setTitle("Dedup Cross File Geometry")
        .setDoc("Let a shape file refer to surfaces and curves another shape file of\n"
"the same document already holds instead of writing them again.\n"
"Smaller files, but a shape file then depends on another for its\n"
"geometry. Off by default."),
    App::ParamInfo("App", "DocumentParams", "User parameter:BaseApp/Preferences/Document", "AutoRemoveFile", "AutoRemoveFile", App::ParamInfo::Bool, true)
        .setTitle("Auto Remove File")
        .setDoc("Delete the files a document no longer uses from its directory when\n"
"it is saved as a directory. Turn off to leave the files of removed\n"
"objects in place."),
    App::ParamInfo("App", "DocumentParams", "User parameter:BaseApp/Preferences/Document", "AutoNameDynamicProperty", "AutoNameDynamicProperty", App::ParamInfo::Bool, false)
        .setTitle("Auto Name Dynamic Property")
        .setDoc("Rename a property added to an object when its name is empty, not a\n"
"valid name or already taken, instead of refusing to add it. A\n"
"warning reports the name chosen."),
    App::ParamInfo("App", "DocumentParams", "User parameter:BaseApp/Preferences/Document", "BackupPolicy", "BackupPolicy", App::ParamInfo::Bool, true)
        .setTitle("Backup Policy")
        .setDoc("Save a document to a temporary file first and move it over the old\n"
"file only once the write succeeded, keeping backups as configured.\n"
"Turn off to write straight over the file, with no backup."),
    App::ParamInfo("App", "DocumentParams", "User parameter:BaseApp/Preferences/Document", "CreateBackupFiles", "CreateBackupFiles", App::ParamInfo::Bool, true)
        .setTitle("Create Backup Files")
        .setDoc("Keep the previous version of a document file as a backup each time\n"
"it is saved. When off the old file is deleted once the new one is\n"
"written."),
    App::ParamInfo("App", "DocumentParams", "User parameter:BaseApp/Preferences/Document", "UseFCBakExtension", "UseFCBakExtension", App::ParamInfo::Bool, false)
        .setTitle("Use FC Bak Extension")
        .setDoc("Name a backup after the document, with the date of the replaced\n"
"file and the extension .FCBak. When off a backup is the document\n"
"file name followed by a number, as in Part.FCStd1."),
    App::ParamInfo("App", "DocumentParams", "User parameter:BaseApp/Preferences/Document", "SaveBackupDateFormat", "SaveBackupDateFormat", App::ParamInfo::String, "%Y%m%d-%H%M%S")
        .setTitle("Save Backup Date Format")
        .setDoc("Date format used in the names of .FCBak backup files, in strftime\n"
"notation. A dot in the format is written as a dash."),
    App::ParamInfo("App", "DocumentParams", "User parameter:BaseApp/Preferences/Document", "CountBackupFiles", "CountBackupFiles", App::ParamInfo::Int, 1)
        .setTitle("Count Backup Files")
        .setDoc("How many backup files are kept for one document. The oldest are\n"
"deleted when a save would exceed the number. 0 keeps none."),
    App::ParamInfo("App", "DocumentParams", "User parameter:BaseApp/Preferences/Document", "OptimizeRecompute", "OptimizeRecompute", App::ParamInfo::Bool, true)
        .setTitle("Optimize Recompute")
        .setDoc("Recompute an object only when one of its properties really changed.\n"
"Writing a property the value it already has then leaves the object\n"
"untouched. Turn off to recompute on every write."),
    App::ParamInfo("App", "DocumentParams", "User parameter:BaseApp/Preferences/Document", "CanAbortRecompute", "CanAbortRecompute", App::ParamInfo::Bool, true)
        .setTitle("Can Abort Recompute")
        .setDoc("Show progress while a document recomputes and let Esc abort it.\n"
"Costs a little recompute time."),
    App::ParamInfo("App", "DocumentParams", "User parameter:BaseApp/Preferences/Document", "UseHasher", "UseHasher", App::ParamInfo::Bool, true)
        .setTitle("Use Hasher")
        .setDoc("Store the generated element names of new documents as short\n"
"references into a string table of the document instead of in full.\n"
"Each document carries its own copy of this choice."),
    App::ParamInfo("App", "DocumentParams", "User parameter:BaseApp/Preferences/Document", "ViewObjectTransaction", "ViewObjectTransaction", App::ParamInfo::Bool, false)
        .setTitle("View Object Transaction")
        .setDoc("Let a change to a view property alone, such as colour or visibility,\n"
"create an undo step whatever command made it. When off only the\n"
"commands that ask for it do."),
    App::ParamInfo("App", "DocumentParams", "User parameter:BaseApp/Preferences/Document", "WarnRecomputeOnRestore", "WarnRecomputeOnRestore", App::ParamInfo::Bool, true)
        .setTitle("Warn Recompute On Restore")
        .setDoc("Ask to recompute after opening a document that needs it to be\n"
"brought up to date with this version. When off the document opens\n"
"without the question and is left as it is."),
    App::ParamInfo("App", "DocumentParams", "User parameter:BaseApp/Preferences/Document", "NoPartialLoading", "NoPartialLoading", App::ParamInfo::Bool, false)
        .setTitle("No Partial Loading")
        .setDoc("Load every externally linked document in full. When off a document\n"
"opened only because another links to it loads just the linked\n"
"objects and what they depend on, and cannot be edited until\n"
"reloaded."),
    App::ParamInfo("App", "DocumentParams", "User parameter:BaseApp/Preferences/Document", "SaveThumbnail", "SaveThumbnail", App::ParamInfo::Bool, false)
        .setTitle("Save Thumbnail")
        .setDoc("Save a preview picture of the 3D view into new documents each time\n"
"they are saved. Each document carries its own copy of this choice."),
    App::ParamInfo("App", "DocumentParams", "User parameter:BaseApp/Preferences/Document", "ThumbnailNoBackground", "ThumbnailNoBackground", App::ParamInfo::Bool, false)
        .setTitle("Thumbnail No Background")
        .setDoc("Leave the view background out of the thumbnail saved with a\n"
"document, so the picture has a transparent background."),
    App::ParamInfo("App", "DocumentParams", "User parameter:BaseApp/Preferences/Document", "AddThumbnailLogo", "AddThumbnailLogo", App::ParamInfo::Bool, true)
        .setTitle("Add Thumbnail Logo")
        .setDoc("Put the application icon in the bottom right corner of the thumbnail\n"
"saved with a document."),
    App::ParamInfo("App", "DocumentParams", "User parameter:BaseApp/Preferences/Document", "ThumbnailSampleSize", "ThumbnailSampleSize", App::ParamInfo::Int, 0)
        .setTitle("Thumbnail Sample Size")
        .setDoc("Number of antialiasing samples used to render the thumbnail saved\n"
"with a document. 0 renders without antialiasing."),
    App::ParamInfo("App", "DocumentParams", "User parameter:BaseApp/Preferences/Document", "ThumbnailSize", "ThumbnailSize", App::ParamInfo::Int, 128)
        .setTitle("Thumbnail Size")
        .setDoc("Width and height, in pixels, of the thumbnail saved with a document.\n"
"Values outside 64 to 1024 are brought into that range."),
    App::ParamInfo("App", "DocumentParams", "User parameter:BaseApp/Preferences/Document", "DuplicateLabels", "DuplicateLabels", App::ParamInfo::Bool, false)
        .setTitle("Duplicate Labels")
        .setDoc("Allow several objects of one document to carry the same label. When\n"
"off a label already in use gets a number added to make it unique."),
    App::ParamInfo("App", "DocumentParams", "User parameter:BaseApp/Preferences/Document", "TransactionOnRecompute", "TransactionOnRecompute", App::ParamInfo::Bool, false)
        .setTitle("Transaction On Recompute")
        .setDoc("Record a recompute started with the Refresh command as an undo step.\n"
"When off, refreshing leaves the undo and redo history alone."),
    App::ParamInfo("App", "DocumentParams", "User parameter:BaseApp/Preferences/Document", "RelativeStringID", "RelativeStringID", App::ParamInfo::Bool, true)
        .setTitle("Relative String ID")
        .setDoc("Write the ids in a document's string table as differences from the\n"
"id before, which makes the saved file smaller. Turn off to write\n"
"every id in full."),
    App::ParamInfo("App", "DocumentParams", "User parameter:BaseApp/Preferences/Document", "HashIndexedName", "HashIndexedName", App::ParamInfo::Bool, true)
        .setTitle("Hash Indexed Name")
        .setDoc("Encode a mapped name's trailing index apart from its text, as upstream\n"
"FreeCAD does. Sets the mode of new documents only: a document keeps the\n"
"mode it was saved in, and one saved before the mode was stored gets the\n"
"one its string table was written in."),
    App::ParamInfo("App", "DocumentParams", "User parameter:BaseApp/Preferences/Document", "EnableMaterialEdit", "EnableMaterialEdit", App::ParamInfo::Bool, true)
        .setTitle("Enable Material Edit")
        .setDoc("Show appearance properties in the property view with an editor for\n"
"their colours, shininess and transparency. When off they are not\n"
"listed. Applies to objects created or loaded afterwards."),
    App::ParamInfo("App", "DocumentParams", "User parameter:BaseApp/Preferences/Document", "MCPServerAutoStart", "MCPServerAutoStart", App::ParamInfo::Bool, false)
        .setTitle("M CP Server Auto Start")
        .setDoc("Start the MCP debug console server (freecad.mcp_console) when the\n"
"application starts. Toggled by the Tools -> MCP Server menu action."),
    App::ParamInfo("App", "DocumentParams", "User parameter:BaseApp/Preferences/Document", "MCPServerPort", "MCPServerPort", App::ParamInfo::Int, 8765)
        .setTitle("M CP Server Port")
        .setDoc("Port the MCP debug console server listens on. If it is already in\n"
"use the server takes the next free port after it, so the port it\n"
"ends up on is reported in the console and in the Tools -> MCP\n"
"Server tooltip."),
});

// Auto generated code (Tools/params_utils.py:368)
ParameterGrp::handle DocumentParams::getHandle() {
    return instance()->handle;
}

// Auto generated code (Tools/params_utils.py:378)
fastsignals::signal<void (const char*)> &
DocumentParams::signalParamChanged() {
    return instance()->signalParamChanged;
}

// Auto generated code (Tools/params_utils.py:387)
void signalAll() {
    instance()->signalAll();
}

// Auto generated code (Tools/params_utils.py:397)
const char *DocumentParams::docprefAuthor() {
    return QT_TRANSLATE_NOOP("DocumentParams",
"Author name given to new documents as their creator. Also written as\n"
"the last modifier on save when that option is on. Leave empty to stay\n"
"anonymous.");
}

// Auto generated code (Tools/params_utils.py:405)
const std::string & DocumentParams::getprefAuthor() {
    return instance()->prefAuthor;
}

// Auto generated code (Tools/params_utils.py:413)
const std::string & DocumentParams::defaultprefAuthor() {
    const static std::string def = "";
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void DocumentParams::setprefAuthor(const std::string &v) {
    instance()->handle->SetASCII("prefAuthor",v);
    instance()->prefAuthor = v;
}

// Auto generated code (Tools/params_utils.py:431)
void DocumentParams::removeprefAuthor() {
    instance()->handle->RemoveASCII("prefAuthor");
}

// Auto generated code (Tools/params_utils.py:397)
const char *DocumentParams::docprefSetAuthorOnSave() {
    return QT_TRANSLATE_NOOP("DocumentParams",
"Write the author name from the preferences into a document's 'Last\n"
"modified by' field each time it is saved.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & DocumentParams::getprefSetAuthorOnSave() {
    return instance()->prefSetAuthorOnSave;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & DocumentParams::defaultprefSetAuthorOnSave() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void DocumentParams::setprefSetAuthorOnSave(const bool &v) {
    instance()->handle->SetBool("prefSetAuthorOnSave",v);
    instance()->prefSetAuthorOnSave = v;
}

// Auto generated code (Tools/params_utils.py:431)
void DocumentParams::removeprefSetAuthorOnSave() {
    instance()->handle->RemoveBool("prefSetAuthorOnSave");
}

// Auto generated code (Tools/params_utils.py:397)
const char *DocumentParams::docprefCompany() {
    return QT_TRANSLATE_NOOP("DocumentParams",
"Company name given to new documents.");
}

// Auto generated code (Tools/params_utils.py:405)
const std::string & DocumentParams::getprefCompany() {
    return instance()->prefCompany;
}

// Auto generated code (Tools/params_utils.py:413)
const std::string & DocumentParams::defaultprefCompany() {
    const static std::string def = "";
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void DocumentParams::setprefCompany(const std::string &v) {
    instance()->handle->SetASCII("prefCompany",v);
    instance()->prefCompany = v;
}

// Auto generated code (Tools/params_utils.py:431)
void DocumentParams::removeprefCompany() {
    instance()->handle->RemoveASCII("prefCompany");
}

// Auto generated code (Tools/params_utils.py:397)
const char *DocumentParams::docprefLicenseType() {
    return QT_TRANSLATE_NOOP("DocumentParams",
"License given to new documents, as a position in the license list.\n"
"0 is All rights reserved, 1 to 12 the Creative Commons licenses,\n"
"13 Public Domain, 14 FreeArt, 15 to 17 the CERN hardware licences;\n"
"18 (Other) leaves the license empty.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & DocumentParams::getprefLicenseType() {
    return instance()->prefLicenseType;
}

// Auto generated code (Tools/params_utils.py:413)
const long & DocumentParams::defaultprefLicenseType() {
    const static long def = 0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void DocumentParams::setprefLicenseType(const long &v) {
    instance()->handle->SetInt("prefLicenseType",v);
    instance()->prefLicenseType = v;
}

// Auto generated code (Tools/params_utils.py:431)
void DocumentParams::removeprefLicenseType() {
    instance()->handle->RemoveInt("prefLicenseType");
}

// Auto generated code (Tools/params_utils.py:397)
const char *DocumentParams::docprefLicenseUrl() {
    return QT_TRANSLATE_NOOP("DocumentParams",
"Address of the license text given to new documents. Empty uses the\n"
"address that belongs to the license chosen from the list.");
}

// Auto generated code (Tools/params_utils.py:405)
const std::string & DocumentParams::getprefLicenseUrl() {
    return instance()->prefLicenseUrl;
}

// Auto generated code (Tools/params_utils.py:413)
const std::string & DocumentParams::defaultprefLicenseUrl() {
    const static std::string def = "";
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void DocumentParams::setprefLicenseUrl(const std::string &v) {
    instance()->handle->SetASCII("prefLicenseUrl",v);
    instance()->prefLicenseUrl = v;
}

// Auto generated code (Tools/params_utils.py:431)
void DocumentParams::removeprefLicenseUrl() {
    instance()->handle->RemoveASCII("prefLicenseUrl");
}

// Auto generated code (Tools/params_utils.py:397)
const char *DocumentParams::docCompressionLevel() {
    return QT_TRANSLATE_NOOP("DocumentParams",
"How hard a document file is compressed when saved, from 0 (none,\n"
"fastest) to 9 (smallest, slowest). Has no effect on a document saved\n"
"as a directory.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & DocumentParams::getCompressionLevel() {
    return instance()->CompressionLevel;
}

// Auto generated code (Tools/params_utils.py:413)
const long & DocumentParams::defaultCompressionLevel() {
    const static long def = 3;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void DocumentParams::setCompressionLevel(const long &v) {
    instance()->handle->SetInt("CompressionLevel",v);
    instance()->CompressionLevel = v;
}

// Auto generated code (Tools/params_utils.py:431)
void DocumentParams::removeCompressionLevel() {
    instance()->handle->RemoveInt("CompressionLevel");
}

// Auto generated code (Tools/params_utils.py:397)
const char *DocumentParams::docCheckExtension() {
    return QT_TRANSLATE_NOOP("DocumentParams",
"Add .FCStd to the file name when a document is saved under a name\n"
"without that extension, so that a save cannot overwrite an unrelated\n"
"file by accident.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & DocumentParams::getCheckExtension() {
    return instance()->CheckExtension;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & DocumentParams::defaultCheckExtension() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void DocumentParams::setCheckExtension(const bool &v) {
    instance()->handle->SetBool("CheckExtension",v);
    instance()->CheckExtension = v;
}

// Auto generated code (Tools/params_utils.py:431)
void DocumentParams::removeCheckExtension() {
    instance()->handle->RemoveBool("CheckExtension");
}

// Auto generated code (Tools/params_utils.py:397)
const char *DocumentParams::docForceXML() {
    return QT_TRANSLATE_NOOP("DocumentParams",
"How much object data new documents keep inside the XML when saved\n"
"as a directory. 0 none, 1 lists, 2 also meshes, points and text\n"
"shapes, 3 also binary shapes, 4 and up also included files.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & DocumentParams::getForceXML() {
    return instance()->ForceXML;
}

// Auto generated code (Tools/params_utils.py:413)
const long & DocumentParams::defaultForceXML() {
    const static long def = 3;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void DocumentParams::setForceXML(const long &v) {
    instance()->handle->SetInt("ForceXML",v);
    instance()->ForceXML = v;
}

// Auto generated code (Tools/params_utils.py:431)
void DocumentParams::removeForceXML() {
    instance()->handle->RemoveInt("ForceXML");
}

// Auto generated code (Tools/params_utils.py:397)
const char *DocumentParams::docSplitXML() {
    return QT_TRANSLATE_NOOP("DocumentParams",
"Give each object an XML file of its own in new documents saved as a\n"
"directory, instead of one file for the whole document. Has no effect\n"
"on a document saved as a single file.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & DocumentParams::getSplitXML() {
    return instance()->SplitXML;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & DocumentParams::defaultSplitXML() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void DocumentParams::setSplitXML(const bool &v) {
    instance()->handle->SetBool("SplitXML",v);
    instance()->SplitXML = v;
}

// Auto generated code (Tools/params_utils.py:431)
void DocumentParams::removeSplitXML() {
    instance()->handle->RemoveBool("SplitXML");
}

// Auto generated code (Tools/params_utils.py:397)
const char *DocumentParams::docPreferBinary() {
    return QT_TRANSLATE_NOOP("DocumentParams",
"Save the object data of new documents in binary instead of text\n"
"form. Files get smaller but compare poorly under version control.\n"
"Each document carries its own copy of this choice.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & DocumentParams::getPreferBinary() {
    return instance()->PreferBinary;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & DocumentParams::defaultPreferBinary() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void DocumentParams::setPreferBinary(const bool &v) {
    instance()->handle->SetBool("PreferBinary",v);
    instance()->PreferBinary = v;
}

// Auto generated code (Tools/params_utils.py:431)
void DocumentParams::removePreferBinary() {
    instance()->handle->RemoveBool("PreferBinary");
}

// Auto generated code (Tools/params_utils.py:397)
const char *DocumentParams::docInlineListSize() {
    return QT_TRANSLATE_NOOP("DocumentParams",
"Largest list property, in bytes, still written inside the document\n"
"XML instead of as a separate entry of the file. Small lists are\n"
"cheaper inline. 0 gives every list its own entry.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & DocumentParams::getInlineListSize() {
    return instance()->InlineListSize;
}

// Auto generated code (Tools/params_utils.py:413)
const long & DocumentParams::defaultInlineListSize() {
    const static long def = 64;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void DocumentParams::setInlineListSize(const long &v) {
    instance()->handle->SetInt("InlineListSize",v);
    instance()->InlineListSize = v;
}

// Auto generated code (Tools/params_utils.py:431)
void DocumentParams::removeInlineListSize() {
    instance()->handle->RemoveInt("InlineListSize");
}

// Auto generated code (Tools/params_utils.py:397)
const char *DocumentParams::docArchiveRandomAccess() {
    return QT_TRANSLATE_NOOP("DocumentParams",
"Restore a document archive through its zip central directory\n"
"instead of one forward-only stream. Entries are then opened\n"
"independently and served in registration order whatever their\n"
"archive order, nothing pays for inflating entries nobody reads,\n"
"and an entry can be reopened after the restore. Turn off to\n"
"fall back to the forward-only walk.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & DocumentParams::getArchiveRandomAccess() {
    return instance()->ArchiveRandomAccess;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & DocumentParams::defaultArchiveRandomAccess() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void DocumentParams::setArchiveRandomAccess(const bool &v) {
    instance()->handle->SetBool("ArchiveRandomAccess",v);
    instance()->ArchiveRandomAccess = v;
}

// Auto generated code (Tools/params_utils.py:431)
void DocumentParams::removeArchiveRandomAccess() {
    instance()->handle->RemoveBool("ArchiveRandomAccess");
}

// Auto generated code (Tools/params_utils.py:397)
const char *DocumentParams::docArchiveBlobStore() {
    return QT_TRANSLATE_NOOP("DocumentParams",
"Serve the included files of a document archive out of one copy\n"
"of the archive in the transient directory, and give each its own\n"
"file only when something asks for a path. Requires\n"
"ArchiveRandomAccess. Turn off to write every included file out\n"
"during the restore, which on a monitored filesystem costs a file\n"
"create per entry.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & DocumentParams::getArchiveBlobStore() {
    return instance()->ArchiveBlobStore;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & DocumentParams::defaultArchiveBlobStore() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void DocumentParams::setArchiveBlobStore(const bool &v) {
    instance()->handle->SetBool("ArchiveBlobStore",v);
    instance()->ArchiveBlobStore = v;
}

// Auto generated code (Tools/params_utils.py:431)
void DocumentParams::removeArchiveBlobStore() {
    instance()->handle->RemoveBool("ArchiveBlobStore");
}

// Auto generated code (Tools/params_utils.py:397)
const char *DocumentParams::docDeferShapeLoad() {
    return QT_TRANSLATE_NOOP("DocumentParams",
"When opening a document, read each shape on first use instead of\n"
"before the window comes up. Requires ArchiveRandomAccess. The file\n"
"must not be rewritten by another program while shapes are still to be\n"
"read. Off by default.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & DocumentParams::getDeferShapeLoad() {
    return instance()->DeferShapeLoad;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & DocumentParams::defaultDeferShapeLoad() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void DocumentParams::setDeferShapeLoad(const bool &v) {
    instance()->handle->SetBool("DeferShapeLoad",v);
    instance()->DeferShapeLoad = v;
}

// Auto generated code (Tools/params_utils.py:431)
void DocumentParams::removeDeferShapeLoad() {
    instance()->handle->RemoveBool("DeferShapeLoad");
}

// Auto generated code (Tools/params_utils.py:397)
const char *DocumentParams::docSaveMaterialCards() {
    return QT_TRANSLATE_NOOP("DocumentParams",
"Write every material card used into the document, including the\n"
"stock ones, so the document does not depend on the installed material\n"
"library. Off writes only a reference to a stock card, which is lost\n"
"if the library changes.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & DocumentParams::getSaveMaterialCards() {
    return instance()->SaveMaterialCards;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & DocumentParams::defaultSaveMaterialCards() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void DocumentParams::setSaveMaterialCards(const bool &v) {
    instance()->handle->SetBool("SaveMaterialCards",v);
    instance()->SaveMaterialCards = v;
}

// Auto generated code (Tools/params_utils.py:431)
void DocumentParams::removeSaveMaterialCards() {
    instance()->handle->RemoveBool("SaveMaterialCards");
}

// Auto generated code (Tools/params_utils.py:397)
const char *DocumentParams::docDedupShapePCurves() {
    return QT_TRANSLATE_NOOP("DocumentParams",
"Write each 2D curve of a shape once, and leave out those on planar\n"
"faces, which are computed again on reading. The geometry read back is\n"
"the same. Applies to shapes written as ASCII BRep.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & DocumentParams::getDedupShapePCurves() {
    return instance()->DedupShapePCurves;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & DocumentParams::defaultDedupShapePCurves() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void DocumentParams::setDedupShapePCurves(const bool &v) {
    instance()->handle->SetBool("DedupShapePCurves",v);
    instance()->DedupShapePCurves = v;
}

// Auto generated code (Tools/params_utils.py:431)
void DocumentParams::removeDedupShapePCurves() {
    instance()->handle->RemoveBool("DedupShapePCurves");
}

// Auto generated code (Tools/params_utils.py:397)
const char *DocumentParams::docDedupCongruentShapes() {
    return QT_TRANSLATE_NOOP("DocumentParams",
"Store one shape file for parts that are the same shape in different\n"
"places, and record the motion between them. Parts are merged only\n"
"after the motion has been checked sub-shape by sub-shape.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & DocumentParams::getDedupCongruentShapes() {
    return instance()->DedupCongruentShapes;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & DocumentParams::defaultDedupCongruentShapes() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void DocumentParams::setDedupCongruentShapes(const bool &v) {
    instance()->handle->SetBool("DedupCongruentShapes",v);
    instance()->DedupCongruentShapes = v;
}

// Auto generated code (Tools/params_utils.py:431)
void DocumentParams::removeDedupCongruentShapes() {
    instance()->handle->RemoveBool("DedupCongruentShapes");
}

// Auto generated code (Tools/params_utils.py:397)
const char *DocumentParams::docDedupCrossFileGeometry() {
    return QT_TRANSLATE_NOOP("DocumentParams",
"Let a shape file refer to surfaces and curves another shape file of\n"
"the same document already holds instead of writing them again.\n"
"Smaller files, but a shape file then depends on another for its\n"
"geometry. Off by default.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & DocumentParams::getDedupCrossFileGeometry() {
    return instance()->DedupCrossFileGeometry;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & DocumentParams::defaultDedupCrossFileGeometry() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void DocumentParams::setDedupCrossFileGeometry(const bool &v) {
    instance()->handle->SetBool("DedupCrossFileGeometry",v);
    instance()->DedupCrossFileGeometry = v;
}

// Auto generated code (Tools/params_utils.py:431)
void DocumentParams::removeDedupCrossFileGeometry() {
    instance()->handle->RemoveBool("DedupCrossFileGeometry");
}

// Auto generated code (Tools/params_utils.py:397)
const char *DocumentParams::docAutoRemoveFile() {
    return QT_TRANSLATE_NOOP("DocumentParams",
"Delete the files a document no longer uses from its directory when\n"
"it is saved as a directory. Turn off to leave the files of removed\n"
"objects in place.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & DocumentParams::getAutoRemoveFile() {
    return instance()->AutoRemoveFile;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & DocumentParams::defaultAutoRemoveFile() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void DocumentParams::setAutoRemoveFile(const bool &v) {
    instance()->handle->SetBool("AutoRemoveFile",v);
    instance()->AutoRemoveFile = v;
}

// Auto generated code (Tools/params_utils.py:431)
void DocumentParams::removeAutoRemoveFile() {
    instance()->handle->RemoveBool("AutoRemoveFile");
}

// Auto generated code (Tools/params_utils.py:397)
const char *DocumentParams::docAutoNameDynamicProperty() {
    return QT_TRANSLATE_NOOP("DocumentParams",
"Rename a property added to an object when its name is empty, not a\n"
"valid name or already taken, instead of refusing to add it. A\n"
"warning reports the name chosen.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & DocumentParams::getAutoNameDynamicProperty() {
    return instance()->AutoNameDynamicProperty;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & DocumentParams::defaultAutoNameDynamicProperty() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void DocumentParams::setAutoNameDynamicProperty(const bool &v) {
    instance()->handle->SetBool("AutoNameDynamicProperty",v);
    instance()->AutoNameDynamicProperty = v;
}

// Auto generated code (Tools/params_utils.py:431)
void DocumentParams::removeAutoNameDynamicProperty() {
    instance()->handle->RemoveBool("AutoNameDynamicProperty");
}

// Auto generated code (Tools/params_utils.py:397)
const char *DocumentParams::docBackupPolicy() {
    return QT_TRANSLATE_NOOP("DocumentParams",
"Save a document to a temporary file first and move it over the old\n"
"file only once the write succeeded, keeping backups as configured.\n"
"Turn off to write straight over the file, with no backup.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & DocumentParams::getBackupPolicy() {
    return instance()->BackupPolicy;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & DocumentParams::defaultBackupPolicy() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void DocumentParams::setBackupPolicy(const bool &v) {
    instance()->handle->SetBool("BackupPolicy",v);
    instance()->BackupPolicy = v;
}

// Auto generated code (Tools/params_utils.py:431)
void DocumentParams::removeBackupPolicy() {
    instance()->handle->RemoveBool("BackupPolicy");
}

// Auto generated code (Tools/params_utils.py:397)
const char *DocumentParams::docCreateBackupFiles() {
    return QT_TRANSLATE_NOOP("DocumentParams",
"Keep the previous version of a document file as a backup each time\n"
"it is saved. When off the old file is deleted once the new one is\n"
"written.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & DocumentParams::getCreateBackupFiles() {
    return instance()->CreateBackupFiles;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & DocumentParams::defaultCreateBackupFiles() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void DocumentParams::setCreateBackupFiles(const bool &v) {
    instance()->handle->SetBool("CreateBackupFiles",v);
    instance()->CreateBackupFiles = v;
}

// Auto generated code (Tools/params_utils.py:431)
void DocumentParams::removeCreateBackupFiles() {
    instance()->handle->RemoveBool("CreateBackupFiles");
}

// Auto generated code (Tools/params_utils.py:397)
const char *DocumentParams::docUseFCBakExtension() {
    return QT_TRANSLATE_NOOP("DocumentParams",
"Name a backup after the document, with the date of the replaced\n"
"file and the extension .FCBak. When off a backup is the document\n"
"file name followed by a number, as in Part.FCStd1.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & DocumentParams::getUseFCBakExtension() {
    return instance()->UseFCBakExtension;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & DocumentParams::defaultUseFCBakExtension() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void DocumentParams::setUseFCBakExtension(const bool &v) {
    instance()->handle->SetBool("UseFCBakExtension",v);
    instance()->UseFCBakExtension = v;
}

// Auto generated code (Tools/params_utils.py:431)
void DocumentParams::removeUseFCBakExtension() {
    instance()->handle->RemoveBool("UseFCBakExtension");
}

// Auto generated code (Tools/params_utils.py:397)
const char *DocumentParams::docSaveBackupDateFormat() {
    return QT_TRANSLATE_NOOP("DocumentParams",
"Date format used in the names of .FCBak backup files, in strftime\n"
"notation. A dot in the format is written as a dash.");
}

// Auto generated code (Tools/params_utils.py:405)
const std::string & DocumentParams::getSaveBackupDateFormat() {
    return instance()->SaveBackupDateFormat;
}

// Auto generated code (Tools/params_utils.py:413)
const std::string & DocumentParams::defaultSaveBackupDateFormat() {
    const static std::string def = "%Y%m%d-%H%M%S";
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void DocumentParams::setSaveBackupDateFormat(const std::string &v) {
    instance()->handle->SetASCII("SaveBackupDateFormat",v);
    instance()->SaveBackupDateFormat = v;
}

// Auto generated code (Tools/params_utils.py:431)
void DocumentParams::removeSaveBackupDateFormat() {
    instance()->handle->RemoveASCII("SaveBackupDateFormat");
}

// Auto generated code (Tools/params_utils.py:397)
const char *DocumentParams::docCountBackupFiles() {
    return QT_TRANSLATE_NOOP("DocumentParams",
"How many backup files are kept for one document. The oldest are\n"
"deleted when a save would exceed the number. 0 keeps none.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & DocumentParams::getCountBackupFiles() {
    return instance()->CountBackupFiles;
}

// Auto generated code (Tools/params_utils.py:413)
const long & DocumentParams::defaultCountBackupFiles() {
    const static long def = 1;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void DocumentParams::setCountBackupFiles(const long &v) {
    instance()->handle->SetInt("CountBackupFiles",v);
    instance()->CountBackupFiles = v;
}

// Auto generated code (Tools/params_utils.py:431)
void DocumentParams::removeCountBackupFiles() {
    instance()->handle->RemoveInt("CountBackupFiles");
}

// Auto generated code (Tools/params_utils.py:397)
const char *DocumentParams::docOptimizeRecompute() {
    return QT_TRANSLATE_NOOP("DocumentParams",
"Recompute an object only when one of its properties really changed.\n"
"Writing a property the value it already has then leaves the object\n"
"untouched. Turn off to recompute on every write.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & DocumentParams::getOptimizeRecompute() {
    return instance()->OptimizeRecompute;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & DocumentParams::defaultOptimizeRecompute() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void DocumentParams::setOptimizeRecompute(const bool &v) {
    instance()->handle->SetBool("OptimizeRecompute",v);
    instance()->OptimizeRecompute = v;
}

// Auto generated code (Tools/params_utils.py:431)
void DocumentParams::removeOptimizeRecompute() {
    instance()->handle->RemoveBool("OptimizeRecompute");
}

// Auto generated code (Tools/params_utils.py:397)
const char *DocumentParams::docCanAbortRecompute() {
    return QT_TRANSLATE_NOOP("DocumentParams",
"Show progress while a document recomputes and let Esc abort it.\n"
"Costs a little recompute time.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & DocumentParams::getCanAbortRecompute() {
    return instance()->CanAbortRecompute;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & DocumentParams::defaultCanAbortRecompute() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void DocumentParams::setCanAbortRecompute(const bool &v) {
    instance()->handle->SetBool("CanAbortRecompute",v);
    instance()->CanAbortRecompute = v;
}

// Auto generated code (Tools/params_utils.py:431)
void DocumentParams::removeCanAbortRecompute() {
    instance()->handle->RemoveBool("CanAbortRecompute");
}

// Auto generated code (Tools/params_utils.py:397)
const char *DocumentParams::docUseHasher() {
    return QT_TRANSLATE_NOOP("DocumentParams",
"Store the generated element names of new documents as short\n"
"references into a string table of the document instead of in full.\n"
"Each document carries its own copy of this choice.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & DocumentParams::getUseHasher() {
    return instance()->UseHasher;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & DocumentParams::defaultUseHasher() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void DocumentParams::setUseHasher(const bool &v) {
    instance()->handle->SetBool("UseHasher",v);
    instance()->UseHasher = v;
}

// Auto generated code (Tools/params_utils.py:431)
void DocumentParams::removeUseHasher() {
    instance()->handle->RemoveBool("UseHasher");
}

// Auto generated code (Tools/params_utils.py:397)
const char *DocumentParams::docViewObjectTransaction() {
    return QT_TRANSLATE_NOOP("DocumentParams",
"Let a change to a view property alone, such as colour or visibility,\n"
"create an undo step whatever command made it. When off only the\n"
"commands that ask for it do.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & DocumentParams::getViewObjectTransaction() {
    return instance()->ViewObjectTransaction;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & DocumentParams::defaultViewObjectTransaction() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void DocumentParams::setViewObjectTransaction(const bool &v) {
    instance()->handle->SetBool("ViewObjectTransaction",v);
    instance()->ViewObjectTransaction = v;
}

// Auto generated code (Tools/params_utils.py:431)
void DocumentParams::removeViewObjectTransaction() {
    instance()->handle->RemoveBool("ViewObjectTransaction");
}

// Auto generated code (Tools/params_utils.py:397)
const char *DocumentParams::docWarnRecomputeOnRestore() {
    return QT_TRANSLATE_NOOP("DocumentParams",
"Ask to recompute after opening a document that needs it to be\n"
"brought up to date with this version. When off the document opens\n"
"without the question and is left as it is.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & DocumentParams::getWarnRecomputeOnRestore() {
    return instance()->WarnRecomputeOnRestore;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & DocumentParams::defaultWarnRecomputeOnRestore() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void DocumentParams::setWarnRecomputeOnRestore(const bool &v) {
    instance()->handle->SetBool("WarnRecomputeOnRestore",v);
    instance()->WarnRecomputeOnRestore = v;
}

// Auto generated code (Tools/params_utils.py:431)
void DocumentParams::removeWarnRecomputeOnRestore() {
    instance()->handle->RemoveBool("WarnRecomputeOnRestore");
}

// Auto generated code (Tools/params_utils.py:397)
const char *DocumentParams::docNoPartialLoading() {
    return QT_TRANSLATE_NOOP("DocumentParams",
"Load every externally linked document in full. When off a document\n"
"opened only because another links to it loads just the linked\n"
"objects and what they depend on, and cannot be edited until\n"
"reloaded.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & DocumentParams::getNoPartialLoading() {
    return instance()->NoPartialLoading;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & DocumentParams::defaultNoPartialLoading() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void DocumentParams::setNoPartialLoading(const bool &v) {
    instance()->handle->SetBool("NoPartialLoading",v);
    instance()->NoPartialLoading = v;
}

// Auto generated code (Tools/params_utils.py:431)
void DocumentParams::removeNoPartialLoading() {
    instance()->handle->RemoveBool("NoPartialLoading");
}

// Auto generated code (Tools/params_utils.py:397)
const char *DocumentParams::docSaveThumbnail() {
    return QT_TRANSLATE_NOOP("DocumentParams",
"Save a preview picture of the 3D view into new documents each time\n"
"they are saved. Each document carries its own copy of this choice.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & DocumentParams::getSaveThumbnail() {
    return instance()->SaveThumbnail;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & DocumentParams::defaultSaveThumbnail() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void DocumentParams::setSaveThumbnail(const bool &v) {
    instance()->handle->SetBool("SaveThumbnail",v);
    instance()->SaveThumbnail = v;
}

// Auto generated code (Tools/params_utils.py:431)
void DocumentParams::removeSaveThumbnail() {
    instance()->handle->RemoveBool("SaveThumbnail");
}

// Auto generated code (Tools/params_utils.py:397)
const char *DocumentParams::docThumbnailNoBackground() {
    return QT_TRANSLATE_NOOP("DocumentParams",
"Leave the view background out of the thumbnail saved with a\n"
"document, so the picture has a transparent background.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & DocumentParams::getThumbnailNoBackground() {
    return instance()->ThumbnailNoBackground;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & DocumentParams::defaultThumbnailNoBackground() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void DocumentParams::setThumbnailNoBackground(const bool &v) {
    instance()->handle->SetBool("ThumbnailNoBackground",v);
    instance()->ThumbnailNoBackground = v;
}

// Auto generated code (Tools/params_utils.py:431)
void DocumentParams::removeThumbnailNoBackground() {
    instance()->handle->RemoveBool("ThumbnailNoBackground");
}

// Auto generated code (Tools/params_utils.py:397)
const char *DocumentParams::docAddThumbnailLogo() {
    return QT_TRANSLATE_NOOP("DocumentParams",
"Put the application icon in the bottom right corner of the thumbnail\n"
"saved with a document.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & DocumentParams::getAddThumbnailLogo() {
    return instance()->AddThumbnailLogo;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & DocumentParams::defaultAddThumbnailLogo() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void DocumentParams::setAddThumbnailLogo(const bool &v) {
    instance()->handle->SetBool("AddThumbnailLogo",v);
    instance()->AddThumbnailLogo = v;
}

// Auto generated code (Tools/params_utils.py:431)
void DocumentParams::removeAddThumbnailLogo() {
    instance()->handle->RemoveBool("AddThumbnailLogo");
}

// Auto generated code (Tools/params_utils.py:397)
const char *DocumentParams::docThumbnailSampleSize() {
    return QT_TRANSLATE_NOOP("DocumentParams",
"Number of antialiasing samples used to render the thumbnail saved\n"
"with a document. 0 renders without antialiasing.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & DocumentParams::getThumbnailSampleSize() {
    return instance()->ThumbnailSampleSize;
}

// Auto generated code (Tools/params_utils.py:413)
const long & DocumentParams::defaultThumbnailSampleSize() {
    const static long def = 0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void DocumentParams::setThumbnailSampleSize(const long &v) {
    instance()->handle->SetInt("ThumbnailSampleSize",v);
    instance()->ThumbnailSampleSize = v;
}

// Auto generated code (Tools/params_utils.py:431)
void DocumentParams::removeThumbnailSampleSize() {
    instance()->handle->RemoveInt("ThumbnailSampleSize");
}

// Auto generated code (Tools/params_utils.py:397)
const char *DocumentParams::docThumbnailSize() {
    return QT_TRANSLATE_NOOP("DocumentParams",
"Width and height, in pixels, of the thumbnail saved with a document.\n"
"Values outside 64 to 1024 are brought into that range.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & DocumentParams::getThumbnailSize() {
    return instance()->ThumbnailSize;
}

// Auto generated code (Tools/params_utils.py:413)
const long & DocumentParams::defaultThumbnailSize() {
    const static long def = 128;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void DocumentParams::setThumbnailSize(const long &v) {
    instance()->handle->SetInt("ThumbnailSize",v);
    instance()->ThumbnailSize = v;
}

// Auto generated code (Tools/params_utils.py:431)
void DocumentParams::removeThumbnailSize() {
    instance()->handle->RemoveInt("ThumbnailSize");
}

// Auto generated code (Tools/params_utils.py:397)
const char *DocumentParams::docDuplicateLabels() {
    return QT_TRANSLATE_NOOP("DocumentParams",
"Allow several objects of one document to carry the same label. When\n"
"off a label already in use gets a number added to make it unique.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & DocumentParams::getDuplicateLabels() {
    return instance()->DuplicateLabels;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & DocumentParams::defaultDuplicateLabels() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void DocumentParams::setDuplicateLabels(const bool &v) {
    instance()->handle->SetBool("DuplicateLabels",v);
    instance()->DuplicateLabels = v;
}

// Auto generated code (Tools/params_utils.py:431)
void DocumentParams::removeDuplicateLabels() {
    instance()->handle->RemoveBool("DuplicateLabels");
}

// Auto generated code (Tools/params_utils.py:397)
const char *DocumentParams::docTransactionOnRecompute() {
    return QT_TRANSLATE_NOOP("DocumentParams",
"Record a recompute started with the Refresh command as an undo step.\n"
"When off, refreshing leaves the undo and redo history alone.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & DocumentParams::getTransactionOnRecompute() {
    return instance()->TransactionOnRecompute;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & DocumentParams::defaultTransactionOnRecompute() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void DocumentParams::setTransactionOnRecompute(const bool &v) {
    instance()->handle->SetBool("TransactionOnRecompute",v);
    instance()->TransactionOnRecompute = v;
}

// Auto generated code (Tools/params_utils.py:431)
void DocumentParams::removeTransactionOnRecompute() {
    instance()->handle->RemoveBool("TransactionOnRecompute");
}

// Auto generated code (Tools/params_utils.py:397)
const char *DocumentParams::docRelativeStringID() {
    return QT_TRANSLATE_NOOP("DocumentParams",
"Write the ids in a document's string table as differences from the\n"
"id before, which makes the saved file smaller. Turn off to write\n"
"every id in full.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & DocumentParams::getRelativeStringID() {
    return instance()->RelativeStringID;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & DocumentParams::defaultRelativeStringID() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void DocumentParams::setRelativeStringID(const bool &v) {
    instance()->handle->SetBool("RelativeStringID",v);
    instance()->RelativeStringID = v;
}

// Auto generated code (Tools/params_utils.py:431)
void DocumentParams::removeRelativeStringID() {
    instance()->handle->RemoveBool("RelativeStringID");
}

// Auto generated code (Tools/params_utils.py:397)
const char *DocumentParams::docHashIndexedName() {
    return QT_TRANSLATE_NOOP("DocumentParams",
"Encode a mapped name's trailing index apart from its text, as upstream\n"
"FreeCAD does. Sets the mode of new documents only: a document keeps the\n"
"mode it was saved in, and one saved before the mode was stored gets the\n"
"one its string table was written in.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & DocumentParams::getHashIndexedName() {
    return instance()->HashIndexedName;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & DocumentParams::defaultHashIndexedName() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void DocumentParams::setHashIndexedName(const bool &v) {
    instance()->handle->SetBool("HashIndexedName",v);
    instance()->HashIndexedName = v;
}

// Auto generated code (Tools/params_utils.py:431)
void DocumentParams::removeHashIndexedName() {
    instance()->handle->RemoveBool("HashIndexedName");
}

// Auto generated code (Tools/params_utils.py:397)
const char *DocumentParams::docEnableMaterialEdit() {
    return QT_TRANSLATE_NOOP("DocumentParams",
"Show appearance properties in the property view with an editor for\n"
"their colours, shininess and transparency. When off they are not\n"
"listed. Applies to objects created or loaded afterwards.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & DocumentParams::getEnableMaterialEdit() {
    return instance()->EnableMaterialEdit;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & DocumentParams::defaultEnableMaterialEdit() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void DocumentParams::setEnableMaterialEdit(const bool &v) {
    instance()->handle->SetBool("EnableMaterialEdit",v);
    instance()->EnableMaterialEdit = v;
}

// Auto generated code (Tools/params_utils.py:431)
void DocumentParams::removeEnableMaterialEdit() {
    instance()->handle->RemoveBool("EnableMaterialEdit");
}

// Auto generated code (Tools/params_utils.py:397)
const char *DocumentParams::docMCPServerAutoStart() {
    return QT_TRANSLATE_NOOP("DocumentParams",
"Start the MCP debug console server (freecad.mcp_console) when the\n"
"application starts. Toggled by the Tools -> MCP Server menu action.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & DocumentParams::getMCPServerAutoStart() {
    return instance()->MCPServerAutoStart;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & DocumentParams::defaultMCPServerAutoStart() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void DocumentParams::setMCPServerAutoStart(const bool &v) {
    instance()->handle->SetBool("MCPServerAutoStart",v);
    instance()->MCPServerAutoStart = v;
}

// Auto generated code (Tools/params_utils.py:431)
void DocumentParams::removeMCPServerAutoStart() {
    instance()->handle->RemoveBool("MCPServerAutoStart");
}

// Auto generated code (Tools/params_utils.py:397)
const char *DocumentParams::docMCPServerPort() {
    return QT_TRANSLATE_NOOP("DocumentParams",
"Port the MCP debug console server listens on. If it is already in\n"
"use the server takes the next free port after it, so the port it\n"
"ends up on is reported in the console and in the Tools -> MCP\n"
"Server tooltip.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & DocumentParams::getMCPServerPort() {
    return instance()->MCPServerPort;
}

// Auto generated code (Tools/params_utils.py:413)
const long & DocumentParams::defaultMCPServerPort() {
    const static long def = 8765;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void DocumentParams::setMCPServerPort(const long &v) {
    instance()->handle->SetInt("MCPServerPort",v);
    instance()->MCPServerPort = v;
}

// Auto generated code (Tools/params_utils.py:431)
void DocumentParams::removeMCPServerPort() {
    instance()->handle->RemoveInt("MCPServerPort");
}
//[[[end]]]
