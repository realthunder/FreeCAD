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
        signalParamChanged("BlobSegmentSize");
        signalParamChanged("DeferShapeLoad");
        signalParamChanged("SaveMaterialCards");
        signalParamChanged("DedupShapePCurves");
        signalParamChanged("StableShapeBytes");
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
        signalParamChanged("TransactionLog");
        signalParamChanged("TransactionLogIdentity");
        signalParamChanged("TransactionLogDerived");
        signalParamChanged("TransactionLogSnapshotTransactions");
        signalParamChanged("TransactionLogKeepVersions");
        signalParamChanged("AutoSaveEnabled");
        signalParamChanged("AutoSaveTimeout");
        signalParamChanged("TransactionLogDeltaHops");
        signalParamChanged("TransactionLogDeltaRatio");
        signalParamChanged("TransactionLogVerify");
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
    long BlobSegmentSize;
    bool DeferShapeLoad;
    bool SaveMaterialCards;
    bool DedupShapePCurves;
    bool StableShapeBytes;
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
    long TransactionLog;
    bool TransactionLogIdentity;
    long TransactionLogDerived;
    long TransactionLogSnapshotTransactions;
    long TransactionLogKeepVersions;
    bool AutoSaveEnabled;
    long AutoSaveTimeout;
    long TransactionLogDeltaHops;
    long TransactionLogDeltaRatio;
    bool TransactionLogVerify;
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
        BlobSegmentSize = this->handle->GetInt("BlobSegmentSize", 65536);
        funcs["BlobSegmentSize"] = &DocumentParamsP::updateBlobSegmentSize;
        DeferShapeLoad = this->handle->GetBool("DeferShapeLoad", true);
        funcs["DeferShapeLoad"] = &DocumentParamsP::updateDeferShapeLoad;
        SaveMaterialCards = this->handle->GetBool("SaveMaterialCards", true);
        funcs["SaveMaterialCards"] = &DocumentParamsP::updateSaveMaterialCards;
        DedupShapePCurves = this->handle->GetBool("DedupShapePCurves", true);
        funcs["DedupShapePCurves"] = &DocumentParamsP::updateDedupShapePCurves;
        StableShapeBytes = this->handle->GetBool("StableShapeBytes", true);
        funcs["StableShapeBytes"] = &DocumentParamsP::updateStableShapeBytes;
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
        TransactionLog = this->handle->GetInt("TransactionLog", 0);
        funcs["TransactionLog"] = &DocumentParamsP::updateTransactionLog;
        TransactionLogIdentity = this->handle->GetBool("TransactionLogIdentity", false);
        funcs["TransactionLogIdentity"] = &DocumentParamsP::updateTransactionLogIdentity;
        TransactionLogDerived = this->handle->GetInt("TransactionLogDerived", 1);
        funcs["TransactionLogDerived"] = &DocumentParamsP::updateTransactionLogDerived;
        TransactionLogSnapshotTransactions = this->handle->GetInt("TransactionLogSnapshotTransactions", 200);
        funcs["TransactionLogSnapshotTransactions"] = &DocumentParamsP::updateTransactionLogSnapshotTransactions;
        TransactionLogKeepVersions = this->handle->GetInt("TransactionLogKeepVersions", 0);
        funcs["TransactionLogKeepVersions"] = &DocumentParamsP::updateTransactionLogKeepVersions;
        AutoSaveEnabled = this->handle->GetBool("AutoSaveEnabled", true);
        funcs["AutoSaveEnabled"] = &DocumentParamsP::updateAutoSaveEnabled;
        AutoSaveTimeout = this->handle->GetInt("AutoSaveTimeout", 15);
        funcs["AutoSaveTimeout"] = &DocumentParamsP::updateAutoSaveTimeout;
        TransactionLogDeltaHops = this->handle->GetInt("TransactionLogDeltaHops", 8);
        funcs["TransactionLogDeltaHops"] = &DocumentParamsP::updateTransactionLogDeltaHops;
        TransactionLogDeltaRatio = this->handle->GetInt("TransactionLogDeltaRatio", 50);
        funcs["TransactionLogDeltaRatio"] = &DocumentParamsP::updateTransactionLogDeltaRatio;
        TransactionLogVerify = this->handle->GetBool("TransactionLogVerify", false);
        funcs["TransactionLogVerify"] = &DocumentParamsP::updateTransactionLogVerify;
        RelativeStringID = this->handle->GetBool("RelativeStringID", true);
        funcs["RelativeStringID"] = &DocumentParamsP::updateRelativeStringID;
        HashIndexedName = this->handle->GetBool("HashIndexedName", false);
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
    static void updateBlobSegmentSize(DocumentParamsP *self) {
        self->BlobSegmentSize = self->handle->GetInt("BlobSegmentSize", 65536);
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
    static void updateStableShapeBytes(DocumentParamsP *self) {
        self->StableShapeBytes = self->handle->GetBool("StableShapeBytes", true);
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
    static void updateTransactionLog(DocumentParamsP *self) {
        self->TransactionLog = self->handle->GetInt("TransactionLog", 0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateTransactionLogIdentity(DocumentParamsP *self) {
        self->TransactionLogIdentity = self->handle->GetBool("TransactionLogIdentity", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateTransactionLogDerived(DocumentParamsP *self) {
        self->TransactionLogDerived = self->handle->GetInt("TransactionLogDerived", 1);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateTransactionLogSnapshotTransactions(DocumentParamsP *self) {
        self->TransactionLogSnapshotTransactions = self->handle->GetInt("TransactionLogSnapshotTransactions", 200);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateTransactionLogKeepVersions(DocumentParamsP *self) {
        self->TransactionLogKeepVersions = self->handle->GetInt("TransactionLogKeepVersions", 0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateAutoSaveEnabled(DocumentParamsP *self) {
        self->AutoSaveEnabled = self->handle->GetBool("AutoSaveEnabled", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateAutoSaveTimeout(DocumentParamsP *self) {
        self->AutoSaveTimeout = self->handle->GetInt("AutoSaveTimeout", 15);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateTransactionLogDeltaHops(DocumentParamsP *self) {
        self->TransactionLogDeltaHops = self->handle->GetInt("TransactionLogDeltaHops", 8);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateTransactionLogDeltaRatio(DocumentParamsP *self) {
        self->TransactionLogDeltaRatio = self->handle->GetInt("TransactionLogDeltaRatio", 50);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateTransactionLogVerify(DocumentParamsP *self) {
        self->TransactionLogVerify = self->handle->GetBool("TransactionLogVerify", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateRelativeStringID(DocumentParamsP *self) {
        self->RelativeStringID = self->handle->GetBool("RelativeStringID", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateHashIndexedName(DocumentParamsP *self) {
        self->HashIndexedName = self->handle->GetBool("HashIndexedName", false);
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
        .setTitle("pref Author"),
    App::ParamInfo("App", "DocumentParams", "User parameter:BaseApp/Preferences/Document", "prefSetAuthorOnSave", "prefSetAuthorOnSave", App::ParamInfo::Bool, false)
        .setTitle("pref Set Author On Save"),
    App::ParamInfo("App", "DocumentParams", "User parameter:BaseApp/Preferences/Document", "prefCompany", "prefCompany", App::ParamInfo::String, "")
        .setTitle("pref Company"),
    App::ParamInfo("App", "DocumentParams", "User parameter:BaseApp/Preferences/Document", "prefLicenseType", "prefLicenseType", App::ParamInfo::Int, 0)
        .setTitle("pref License Type"),
    App::ParamInfo("App", "DocumentParams", "User parameter:BaseApp/Preferences/Document", "prefLicenseUrl", "prefLicenseUrl", App::ParamInfo::String, "")
        .setTitle("pref License Url"),
    App::ParamInfo("App", "DocumentParams", "User parameter:BaseApp/Preferences/Document", "CompressionLevel", "CompressionLevel", App::ParamInfo::Int, 3)
        .setTitle("Compression Level"),
    App::ParamInfo("App", "DocumentParams", "User parameter:BaseApp/Preferences/Document", "CheckExtension", "CheckExtension", App::ParamInfo::Bool, true)
        .setTitle("Check Extension"),
    App::ParamInfo("App", "DocumentParams", "User parameter:BaseApp/Preferences/Document", "ForceXML", "ForceXML", App::ParamInfo::Int, 3)
        .setTitle("Force XM L"),
    App::ParamInfo("App", "DocumentParams", "User parameter:BaseApp/Preferences/Document", "SplitXML", "SplitXML", App::ParamInfo::Bool, true)
        .setTitle("Split XM L"),
    App::ParamInfo("App", "DocumentParams", "User parameter:BaseApp/Preferences/Document", "PreferBinary", "PreferBinary", App::ParamInfo::Bool, false)
        .setTitle("Prefer Binary"),
    App::ParamInfo("App", "DocumentParams", "User parameter:BaseApp/Preferences/Document", "InlineListSize", "InlineListSize", App::ParamInfo::Int, 64)
        .setTitle("Inline List Size")
        .setDoc("Largest list property, in bytes of values, still written inline\n"
"in the XML instead of taking an archive entry of its own. An\n"
"entry costs around 190 bytes of zip headers before any content,\n"
"and one more thing for the reader to open, which a one-element\n"
"colour list has no way of paying back. Written in the same form\n"
"the reader has always used for lists that cannot be streamed, so\n"
"the file stays readable by FreeCAD versions without this option.\n"
"Set to 0 to give every list an entry, as before."),
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
        .setDoc("Keep a document's included files in a pack store: a few zip\n"
"segment files in the transient directory instead of a file per\n"
"blob (docs/FileBlobsManager.md sec 15.7-15.10). An opened archive\n"
"is split into segments, new content is compressed once and\n"
"batched into them, and a save copies the members as they are. A\n"
"blob gets a file of its own only when something asks for a path.\n"
"Turn off for a file per blob, which on a monitored filesystem\n"
"costs a file create per blob."),
    App::ParamInfo("App", "DocumentParams", "User parameter:BaseApp/Preferences/Document", "BlobSegmentSize", "BlobSegmentSize", App::ParamInfo::Int, 65536)
        .setTitle("Blob Segment Size")
        .setDoc("Cap of one pack store segment, in KB. A segment is rewritten\n"
"whole when content is added to it or dropped from it, so this\n"
"bounds the cost of every such rewrite. Content over a quarter of\n"
"it is kept as a file of its own."),
    App::ParamInfo("App", "DocumentParams", "User parameter:BaseApp/Preferences/Document", "DeferShapeLoad", "DeferShapeLoad", App::ParamInfo::Bool, true)
        .setTitle("Defer Shape Load")
        .setDoc("Park shape archive entries during restore and read each one on\n"
"first real use instead of before the document opens, so the\n"
"window is up while shapes stream in with the progressive visual\n"
"fill. Requires ArchiveRandomAccess. An entry not yet served is\n"
"read when anything asks for the shape -- visual build, script,\n"
"save -- so the value is never observably missing; the trade is\n"
"that the document must not be rewritten externally while loads\n"
"are pending. Off by default until gated on the large references."),
    App::ParamInfo("App", "DocumentParams", "User parameter:BaseApp/Preferences/Document", "SaveMaterialCards", "SaveMaterialCards", App::ParamInfo::Bool, true)
        .setTitle("Save Material Cards")
        .setDoc("Write every material card into the document, including the\n"
"stock ones.\n"
"\n"
"A stock card used to be left out: the hash says which card it\n"
"was, and any installation holding the same library can produce\n"
"the content again. That holds only while the library does not\n"
"move. It moved -- retuning the default appearance changed the\n"
"Default card, and every document written before it then named a\n"
"hash no installed card answers to, losing the material outright\n"
"rather than degrading to the uuid. A shipped library is not a\n"
"fixed point, so a document cannot be built on the assumption\n"
"that it is.\n"
"\n"
"Carrying the content costs almost nothing now that identical\n"
"cards are stored once per document: a model whose objects all\n"
"share one card writes that card once, whatever the object\n"
"count. Turn off to write only the hash of a stock card, which\n"
"is smaller by that one card and readable only by an\n"
"installation whose library still matches."),
    App::ParamInfo("App", "DocumentParams", "User parameter:BaseApp/Preferences/Document", "DedupShapePCurves", "DedupShapePCurves", App::ParamInfo::Bool, true)
        .setTitle("Dedup Shape PCurves")
        .setDoc("Store each 2D curve of a shape once, and leave out the ones\n"
"reading the file back computes again anyway.\n"
"\n"
"Two things, because they are the same bargain. A pcurve computed\n"
"twice used to be written twice, which on a real project is the\n"
"largest single duplication inside a shape file; and a pcurve on a\n"
"planar face need not be stored at all, since the kernel projects\n"
"the 3D curve onto the plane when it finds none. Neither changes\n"
"the geometry that comes back: a merged pcurve is the identical\n"
"curve, and a dropped one is checked against the projection that\n"
"will replace it before it is dropped.\n"
"\n"
"Applies to shapes written as ASCII BRep. Turn off to write what\n"
"the kernel holds, entry for entry."),
    App::ParamInfo("App", "DocumentParams", "User parameter:BaseApp/Preferences/Document", "StableShapeBytes", "StableShapeBytes", App::ParamInfo::Bool, true)
        .setTitle("Stable Shape Bytes")
        .setDoc("Write a shape's file from the shape alone, not from what was\n"
"done with it.\n"
"\n"
"An edge keeps a 2D curve for every face built on it, including\n"
"faces of other objects: extruding a sketch's face gives the\n"
"sketch's own edges a curve on each side face, and those were saved\n"
"with the sketch. Some flags record what was last done to a shape\n"
"rather than what it is. With this on, curves on surfaces that no\n"
"face of the saved shape carries are left out and those flags are\n"
"written as constants, so an unchanged shape saves to the same bytes\n"
"(docs/TransactionLog.md sec 23.12). Nothing the shape needs is\n"
"lost. Needs the realthunder OCCT fork; ignored without it."),
    App::ParamInfo("App", "DocumentParams", "User parameter:BaseApp/Preferences/Document", "DedupCongruentShapes", "DedupCongruentShapes", App::ParamInfo::Bool, true)
        .setTitle("Dedup Congruent Shapes")
        .setDoc("Store one file for parts that are the same shape in different\n"
"places, and record the motion between them instead of writing the\n"
"geometry again.\n"
"\n"
"Content addressing already shares parts whose bytes match, which\n"
"an exporter that bakes each placement into the coordinates\n"
"defeats: the same part at twenty positions is twenty distinct\n"
"contents. Two instances are only merged once the rigid motion\n"
"between them has been recovered and checked sub-shape by\n"
"sub-shape, so a mirrored instance or a near-miss is written out\n"
"in full rather than merged."),
    App::ParamInfo("App", "DocumentParams", "User parameter:BaseApp/Preferences/Document", "DedupCrossFileGeometry", "DedupCrossFileGeometry", App::ParamInfo::Bool, false)
        .setTitle("Dedup Cross File Geometry")
        .setDoc("Let a shape file name the surfaces and curves another shape file\n"
"already holds instead of writing its own copy of them.\n"
"\n"
"Each shape file carries its own table of surfaces, 3D curves and\n"
"2D curves, so a face two parts have in common is written once per\n"
"part. On a real project those tables are most of the bytes and\n"
"about half of what they hold repeats between files. An entry may\n"
"instead name a file and a position in its table, and the reader\n"
"then puts the entry it parsed there into this file.\n"
"\n"
"Off by default: it makes a shape file depend on another one for\n"
"its geometry, not only for whole sub-shapes, so a file that goes\n"
"missing costs more than it did. Applies to shapes written as\n"
"ASCII BRep inside a document; an exported file names nothing."),
    App::ParamInfo("App", "DocumentParams", "User parameter:BaseApp/Preferences/Document", "AutoRemoveFile", "AutoRemoveFile", App::ParamInfo::Bool, true)
        .setTitle("Auto Remove File"),
    App::ParamInfo("App", "DocumentParams", "User parameter:BaseApp/Preferences/Document", "AutoNameDynamicProperty", "AutoNameDynamicProperty", App::ParamInfo::Bool, false)
        .setTitle("Auto Name Dynamic Property"),
    App::ParamInfo("App", "DocumentParams", "User parameter:BaseApp/Preferences/Document", "BackupPolicy", "BackupPolicy", App::ParamInfo::Bool, true)
        .setTitle("Backup Policy"),
    App::ParamInfo("App", "DocumentParams", "User parameter:BaseApp/Preferences/Document", "CreateBackupFiles", "CreateBackupFiles", App::ParamInfo::Bool, true)
        .setTitle("Create Backup Files"),
    App::ParamInfo("App", "DocumentParams", "User parameter:BaseApp/Preferences/Document", "UseFCBakExtension", "UseFCBakExtension", App::ParamInfo::Bool, false)
        .setTitle("Use FC Bak Extension"),
    App::ParamInfo("App", "DocumentParams", "User parameter:BaseApp/Preferences/Document", "SaveBackupDateFormat", "SaveBackupDateFormat", App::ParamInfo::String, "%Y%m%d-%H%M%S")
        .setTitle("Save Backup Date Format"),
    App::ParamInfo("App", "DocumentParams", "User parameter:BaseApp/Preferences/Document", "CountBackupFiles", "CountBackupFiles", App::ParamInfo::Int, 1)
        .setTitle("Count Backup Files"),
    App::ParamInfo("App", "DocumentParams", "User parameter:BaseApp/Preferences/Document", "OptimizeRecompute", "OptimizeRecompute", App::ParamInfo::Bool, true)
        .setTitle("Optimize Recompute"),
    App::ParamInfo("App", "DocumentParams", "User parameter:BaseApp/Preferences/Document", "CanAbortRecompute", "CanAbortRecompute", App::ParamInfo::Bool, true)
        .setTitle("Can Abort Recompute"),
    App::ParamInfo("App", "DocumentParams", "User parameter:BaseApp/Preferences/Document", "UseHasher", "UseHasher", App::ParamInfo::Bool, true)
        .setTitle("Use Hasher"),
    App::ParamInfo("App", "DocumentParams", "User parameter:BaseApp/Preferences/Document", "ViewObjectTransaction", "ViewObjectTransaction", App::ParamInfo::Bool, false)
        .setTitle("View Object Transaction"),
    App::ParamInfo("App", "DocumentParams", "User parameter:BaseApp/Preferences/Document", "WarnRecomputeOnRestore", "WarnRecomputeOnRestore", App::ParamInfo::Bool, true)
        .setTitle("Warn Recompute On Restore"),
    App::ParamInfo("App", "DocumentParams", "User parameter:BaseApp/Preferences/Document", "NoPartialLoading", "NoPartialLoading", App::ParamInfo::Bool, false)
        .setTitle("No Partial Loading"),
    App::ParamInfo("App", "DocumentParams", "User parameter:BaseApp/Preferences/Document", "SaveThumbnail", "SaveThumbnail", App::ParamInfo::Bool, false)
        .setTitle("Save Thumbnail"),
    App::ParamInfo("App", "DocumentParams", "User parameter:BaseApp/Preferences/Document", "ThumbnailNoBackground", "ThumbnailNoBackground", App::ParamInfo::Bool, false)
        .setTitle("Thumbnail No Background"),
    App::ParamInfo("App", "DocumentParams", "User parameter:BaseApp/Preferences/Document", "AddThumbnailLogo", "AddThumbnailLogo", App::ParamInfo::Bool, true)
        .setTitle("Add Thumbnail Logo"),
    App::ParamInfo("App", "DocumentParams", "User parameter:BaseApp/Preferences/Document", "ThumbnailSampleSize", "ThumbnailSampleSize", App::ParamInfo::Int, 0)
        .setTitle("Thumbnail Sample Size"),
    App::ParamInfo("App", "DocumentParams", "User parameter:BaseApp/Preferences/Document", "ThumbnailSize", "ThumbnailSize", App::ParamInfo::Int, 128)
        .setTitle("Thumbnail Size"),
    App::ParamInfo("App", "DocumentParams", "User parameter:BaseApp/Preferences/Document", "DuplicateLabels", "DuplicateLabels", App::ParamInfo::Bool, false)
        .setTitle("Duplicate Labels"),
    App::ParamInfo("App", "DocumentParams", "User parameter:BaseApp/Preferences/Document", "TransactionOnRecompute", "TransactionOnRecompute", App::ParamInfo::Bool, false)
        .setTitle("Transaction On Recompute"),
    App::ParamInfo("App", "DocumentParams", "User parameter:BaseApp/Preferences/Document", "TransactionLog", "TransactionLog", App::ParamInfo::Int, 0)
        .setTitle("Transaction Log")
        .setDoc("Transaction log mode (docs/TransactionLog.md sec 13.3): 0 off,\n"
"1 session -- the log lives in the document transient directory\n"
"and dies with it. Off by default while the writer is synchronous."),
    App::ParamInfo("App", "DocumentParams", "User parameter:BaseApp/Preferences/Document", "TransactionLogIdentity", "TransactionLogIdentity", App::ParamInfo::Bool, false)
        .setTitle("Transaction Log Identity")
        .setDoc("Record the user and host name in the transaction log session\n"
"row (sec 13.3, privacy). Off by default."),
    App::ParamInfo("App", "DocumentParams", "User parameter:BaseApp/Preferences/Document", "TransactionLogDerived", "TransactionLogDerived", App::ParamInfo::Int, 1)
        .setTitle("Transaction Log Derived")
        .setDoc("What the transaction log does with derived values, i.e. values\n"
"written by their own object recompute (sec 10): 0 none (the op\n"
"notes the change, no value), 1 cache (evictable tier), 2 full."),
    App::ParamInfo("App", "DocumentParams", "User parameter:BaseApp/Preferences/Document", "TransactionLogSnapshotTransactions", "TransactionLogSnapshotTransactions", App::ParamInfo::Int, 200)
        .setTitle("Transaction Log Snapshot Transactions")
        .setDoc("The transaction log takes an unnamed version (sec 16.3) every\n"
"this many committed transactions since the last version; 0 for\n"
"none. A snapshot serialises the document like a save, without\n"
"writing an archive. With the time rule of AutoSaveTimeout, it\n"
"bounds how much a crash recovery replays (sec 25.3)."),
    App::ParamInfo("App", "DocumentParams", "User parameter:BaseApp/Preferences/Document", "TransactionLogKeepVersions", "TransactionLogKeepVersions", App::ParamInfo::Int, 0)
        .setTitle("Transaction Log Keep Versions")
        .setDoc("How many unnamed versions the transaction log keeps (sec 16.3):\n"
"when a version is added, the oldest unnamed ones over this count\n"
"are evicted -- never a named one, never the newest. 0 keeps all."),
    App::ParamInfo("App", "DocumentParams", "User parameter:BaseApp/Preferences/Document", "AutoSaveEnabled", "AutoSaveEnabled", App::ParamInfo::Bool, true)
        .setTitle("Auto Save Enabled")
        .setDoc("Autosave. Without the transaction log, the Gui writes a recovery\n"
"file every AutoSaveTimeout minutes; with it, the log takes an\n"
"unnamed version at the first commit that many minutes after the\n"
"last one (docs/TransactionLog.md sec 25.3)."),
    App::ParamInfo("App", "DocumentParams", "User parameter:BaseApp/Preferences/Document", "AutoSaveTimeout", "AutoSaveTimeout", App::ParamInfo::Int, 15)
        .setTitle("The autosave interval in minutes, see AutoSaveEnabled.")
        .setDoc("The autosave interval in minutes, see AutoSaveEnabled."),
    App::ParamInfo("App", "DocumentParams", "User parameter:BaseApp/Preferences/Document", "TransactionLogDeltaHops", "TransactionLogDeltaHops", App::ParamInfo::Int, 8)
        .setTitle("Transaction Log Delta Hops")
        .setDoc("How long a reverse-delta chain the transaction log allows (sec\n"
"23.2): an entity superseded by a newer one is re-encoded as a\n"
"patch against it unless the chain below it would then be this\n"
"many hops from a full entity. 0 stores everything full."),
    App::ParamInfo("App", "DocumentParams", "User parameter:BaseApp/Preferences/Document", "TransactionLogDeltaRatio", "TransactionLogDeltaRatio", App::ParamInfo::Int, 50)
        .setTitle("Transaction Log Delta Ratio")
        .setDoc("The largest patch the transaction log keeps, as a percent of the\n"
"full compressed size (sec 23.2); a patch over it means the codec\n"
"found nothing to share and the entity stays full."),
    App::ParamInfo("App", "DocumentParams", "User parameter:BaseApp/Preferences/Document", "TransactionLogVerify", "TransactionLogVerify", App::ParamInfo::Bool, false)
        .setTitle("Transaction Log Verify")
        .setDoc("A composed snapshot (sec 23.3) serialises the properties it\n"
"would have taken from the log anyway and compares: a mismatch\n"
"names a value changed without aboutToSetValue (sec 23.6). Always\n"
"on in a debug build."),
    App::ParamInfo("App", "DocumentParams", "User parameter:BaseApp/Preferences/Document", "RelativeStringID", "RelativeStringID", App::ParamInfo::Bool, true)
        .setTitle("Relative String ID"),
    App::ParamInfo("App", "DocumentParams", "User parameter:BaseApp/Preferences/Document", "HashIndexedName", "HashIndexedName", App::ParamInfo::Bool, false)
        .setTitle("Hash Indexed Name")
        .setDoc("Enable special encoding of indexes name in toponaming. Disabled by\n"
"default for backward compatibility"),
    App::ParamInfo("App", "DocumentParams", "User parameter:BaseApp/Preferences/Document", "EnableMaterialEdit", "EnableMaterialEdit", App::ParamInfo::Bool, true)
        .setTitle("Enable Material Edit"),
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
    return "";
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
    return "";
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
    return "";
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
    return "";
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
    return "";
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
    return "";
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
    return "";
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
    return "";
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
    return "";
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
    return "";
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
"Largest list property, in bytes of values, still written inline\n"
"in the XML instead of taking an archive entry of its own. An\n"
"entry costs around 190 bytes of zip headers before any content,\n"
"and one more thing for the reader to open, which a one-element\n"
"colour list has no way of paying back. Written in the same form\n"
"the reader has always used for lists that cannot be streamed, so\n"
"the file stays readable by FreeCAD versions without this option.\n"
"Set to 0 to give every list an entry, as before.");
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
"Keep a document's included files in a pack store: a few zip\n"
"segment files in the transient directory instead of a file per\n"
"blob (docs/FileBlobsManager.md sec 15.7-15.10). An opened archive\n"
"is split into segments, new content is compressed once and\n"
"batched into them, and a save copies the members as they are. A\n"
"blob gets a file of its own only when something asks for a path.\n"
"Turn off for a file per blob, which on a monitored filesystem\n"
"costs a file create per blob.");
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
const char *DocumentParams::docBlobSegmentSize() {
    return QT_TRANSLATE_NOOP("DocumentParams",
"Cap of one pack store segment, in KB. A segment is rewritten\n"
"whole when content is added to it or dropped from it, so this\n"
"bounds the cost of every such rewrite. Content over a quarter of\n"
"it is kept as a file of its own.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & DocumentParams::getBlobSegmentSize() {
    return instance()->BlobSegmentSize;
}

// Auto generated code (Tools/params_utils.py:413)
const long & DocumentParams::defaultBlobSegmentSize() {
    const static long def = 65536;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void DocumentParams::setBlobSegmentSize(const long &v) {
    instance()->handle->SetInt("BlobSegmentSize",v);
    instance()->BlobSegmentSize = v;
}

// Auto generated code (Tools/params_utils.py:431)
void DocumentParams::removeBlobSegmentSize() {
    instance()->handle->RemoveInt("BlobSegmentSize");
}

// Auto generated code (Tools/params_utils.py:397)
const char *DocumentParams::docDeferShapeLoad() {
    return QT_TRANSLATE_NOOP("DocumentParams",
"Park shape archive entries during restore and read each one on\n"
"first real use instead of before the document opens, so the\n"
"window is up while shapes stream in with the progressive visual\n"
"fill. Requires ArchiveRandomAccess. An entry not yet served is\n"
"read when anything asks for the shape -- visual build, script,\n"
"save -- so the value is never observably missing; the trade is\n"
"that the document must not be rewritten externally while loads\n"
"are pending. Off by default until gated on the large references.");
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
"Write every material card into the document, including the\n"
"stock ones.\n"
"\n"
"A stock card used to be left out: the hash says which card it\n"
"was, and any installation holding the same library can produce\n"
"the content again. That holds only while the library does not\n"
"move. It moved -- retuning the default appearance changed the\n"
"Default card, and every document written before it then named a\n"
"hash no installed card answers to, losing the material outright\n"
"rather than degrading to the uuid. A shipped library is not a\n"
"fixed point, so a document cannot be built on the assumption\n"
"that it is.\n"
"\n"
"Carrying the content costs almost nothing now that identical\n"
"cards are stored once per document: a model whose objects all\n"
"share one card writes that card once, whatever the object\n"
"count. Turn off to write only the hash of a stock card, which\n"
"is smaller by that one card and readable only by an\n"
"installation whose library still matches.");
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
"Store each 2D curve of a shape once, and leave out the ones\n"
"reading the file back computes again anyway.\n"
"\n"
"Two things, because they are the same bargain. A pcurve computed\n"
"twice used to be written twice, which on a real project is the\n"
"largest single duplication inside a shape file; and a pcurve on a\n"
"planar face need not be stored at all, since the kernel projects\n"
"the 3D curve onto the plane when it finds none. Neither changes\n"
"the geometry that comes back: a merged pcurve is the identical\n"
"curve, and a dropped one is checked against the projection that\n"
"will replace it before it is dropped.\n"
"\n"
"Applies to shapes written as ASCII BRep. Turn off to write what\n"
"the kernel holds, entry for entry.");
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
const char *DocumentParams::docStableShapeBytes() {
    return QT_TRANSLATE_NOOP("DocumentParams",
"Write a shape's file from the shape alone, not from what was\n"
"done with it.\n"
"\n"
"An edge keeps a 2D curve for every face built on it, including\n"
"faces of other objects: extruding a sketch's face gives the\n"
"sketch's own edges a curve on each side face, and those were saved\n"
"with the sketch. Some flags record what was last done to a shape\n"
"rather than what it is. With this on, curves on surfaces that no\n"
"face of the saved shape carries are left out and those flags are\n"
"written as constants, so an unchanged shape saves to the same bytes\n"
"(docs/TransactionLog.md sec 23.12). Nothing the shape needs is\n"
"lost. Needs the realthunder OCCT fork; ignored without it.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & DocumentParams::getStableShapeBytes() {
    return instance()->StableShapeBytes;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & DocumentParams::defaultStableShapeBytes() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void DocumentParams::setStableShapeBytes(const bool &v) {
    instance()->handle->SetBool("StableShapeBytes",v);
    instance()->StableShapeBytes = v;
}

// Auto generated code (Tools/params_utils.py:431)
void DocumentParams::removeStableShapeBytes() {
    instance()->handle->RemoveBool("StableShapeBytes");
}

// Auto generated code (Tools/params_utils.py:397)
const char *DocumentParams::docDedupCongruentShapes() {
    return QT_TRANSLATE_NOOP("DocumentParams",
"Store one file for parts that are the same shape in different\n"
"places, and record the motion between them instead of writing the\n"
"geometry again.\n"
"\n"
"Content addressing already shares parts whose bytes match, which\n"
"an exporter that bakes each placement into the coordinates\n"
"defeats: the same part at twenty positions is twenty distinct\n"
"contents. Two instances are only merged once the rigid motion\n"
"between them has been recovered and checked sub-shape by\n"
"sub-shape, so a mirrored instance or a near-miss is written out\n"
"in full rather than merged.");
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
"Let a shape file name the surfaces and curves another shape file\n"
"already holds instead of writing its own copy of them.\n"
"\n"
"Each shape file carries its own table of surfaces, 3D curves and\n"
"2D curves, so a face two parts have in common is written once per\n"
"part. On a real project those tables are most of the bytes and\n"
"about half of what they hold repeats between files. An entry may\n"
"instead name a file and a position in its table, and the reader\n"
"then puts the entry it parsed there into this file.\n"
"\n"
"Off by default: it makes a shape file depend on another one for\n"
"its geometry, not only for whole sub-shapes, so a file that goes\n"
"missing costs more than it did. Applies to shapes written as\n"
"ASCII BRep inside a document; an exported file names nothing.");
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
    return "";
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
    return "";
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
    return "";
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
    return "";
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
    return "";
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
    return "";
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
    return "";
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
    return "";
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
    return "";
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
    return "";
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
    return "";
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
    return "";
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
    return "";
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
    return "";
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
    return "";
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
    return "";
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
    return "";
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
    return "";
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
    return "";
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
    return "";
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
const char *DocumentParams::docTransactionLog() {
    return QT_TRANSLATE_NOOP("DocumentParams",
"Transaction log mode (docs/TransactionLog.md sec 13.3): 0 off,\n"
"1 session -- the log lives in the document transient directory\n"
"and dies with it. Off by default while the writer is synchronous.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & DocumentParams::getTransactionLog() {
    return instance()->TransactionLog;
}

// Auto generated code (Tools/params_utils.py:413)
const long & DocumentParams::defaultTransactionLog() {
    const static long def = 0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void DocumentParams::setTransactionLog(const long &v) {
    instance()->handle->SetInt("TransactionLog",v);
    instance()->TransactionLog = v;
}

// Auto generated code (Tools/params_utils.py:431)
void DocumentParams::removeTransactionLog() {
    instance()->handle->RemoveInt("TransactionLog");
}

// Auto generated code (Tools/params_utils.py:397)
const char *DocumentParams::docTransactionLogIdentity() {
    return QT_TRANSLATE_NOOP("DocumentParams",
"Record the user and host name in the transaction log session\n"
"row (sec 13.3, privacy). Off by default.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & DocumentParams::getTransactionLogIdentity() {
    return instance()->TransactionLogIdentity;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & DocumentParams::defaultTransactionLogIdentity() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void DocumentParams::setTransactionLogIdentity(const bool &v) {
    instance()->handle->SetBool("TransactionLogIdentity",v);
    instance()->TransactionLogIdentity = v;
}

// Auto generated code (Tools/params_utils.py:431)
void DocumentParams::removeTransactionLogIdentity() {
    instance()->handle->RemoveBool("TransactionLogIdentity");
}

// Auto generated code (Tools/params_utils.py:397)
const char *DocumentParams::docTransactionLogDerived() {
    return QT_TRANSLATE_NOOP("DocumentParams",
"What the transaction log does with derived values, i.e. values\n"
"written by their own object recompute (sec 10): 0 none (the op\n"
"notes the change, no value), 1 cache (evictable tier), 2 full.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & DocumentParams::getTransactionLogDerived() {
    return instance()->TransactionLogDerived;
}

// Auto generated code (Tools/params_utils.py:413)
const long & DocumentParams::defaultTransactionLogDerived() {
    const static long def = 1;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void DocumentParams::setTransactionLogDerived(const long &v) {
    instance()->handle->SetInt("TransactionLogDerived",v);
    instance()->TransactionLogDerived = v;
}

// Auto generated code (Tools/params_utils.py:431)
void DocumentParams::removeTransactionLogDerived() {
    instance()->handle->RemoveInt("TransactionLogDerived");
}

// Auto generated code (Tools/params_utils.py:397)
const char *DocumentParams::docTransactionLogSnapshotTransactions() {
    return QT_TRANSLATE_NOOP("DocumentParams",
"The transaction log takes an unnamed version (sec 16.3) every\n"
"this many committed transactions since the last version; 0 for\n"
"none. A snapshot serialises the document like a save, without\n"
"writing an archive. With the time rule of AutoSaveTimeout, it\n"
"bounds how much a crash recovery replays (sec 25.3).");
}

// Auto generated code (Tools/params_utils.py:405)
const long & DocumentParams::getTransactionLogSnapshotTransactions() {
    return instance()->TransactionLogSnapshotTransactions;
}

// Auto generated code (Tools/params_utils.py:413)
const long & DocumentParams::defaultTransactionLogSnapshotTransactions() {
    const static long def = 200;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void DocumentParams::setTransactionLogSnapshotTransactions(const long &v) {
    instance()->handle->SetInt("TransactionLogSnapshotTransactions",v);
    instance()->TransactionLogSnapshotTransactions = v;
}

// Auto generated code (Tools/params_utils.py:431)
void DocumentParams::removeTransactionLogSnapshotTransactions() {
    instance()->handle->RemoveInt("TransactionLogSnapshotTransactions");
}

// Auto generated code (Tools/params_utils.py:397)
const char *DocumentParams::docTransactionLogKeepVersions() {
    return QT_TRANSLATE_NOOP("DocumentParams",
"How many unnamed versions the transaction log keeps (sec 16.3):\n"
"when a version is added, the oldest unnamed ones over this count\n"
"are evicted -- never a named one, never the newest. 0 keeps all.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & DocumentParams::getTransactionLogKeepVersions() {
    return instance()->TransactionLogKeepVersions;
}

// Auto generated code (Tools/params_utils.py:413)
const long & DocumentParams::defaultTransactionLogKeepVersions() {
    const static long def = 0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void DocumentParams::setTransactionLogKeepVersions(const long &v) {
    instance()->handle->SetInt("TransactionLogKeepVersions",v);
    instance()->TransactionLogKeepVersions = v;
}

// Auto generated code (Tools/params_utils.py:431)
void DocumentParams::removeTransactionLogKeepVersions() {
    instance()->handle->RemoveInt("TransactionLogKeepVersions");
}

// Auto generated code (Tools/params_utils.py:397)
const char *DocumentParams::docAutoSaveEnabled() {
    return QT_TRANSLATE_NOOP("DocumentParams",
"Autosave. Without the transaction log, the Gui writes a recovery\n"
"file every AutoSaveTimeout minutes; with it, the log takes an\n"
"unnamed version at the first commit that many minutes after the\n"
"last one (docs/TransactionLog.md sec 25.3).");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & DocumentParams::getAutoSaveEnabled() {
    return instance()->AutoSaveEnabled;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & DocumentParams::defaultAutoSaveEnabled() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void DocumentParams::setAutoSaveEnabled(const bool &v) {
    instance()->handle->SetBool("AutoSaveEnabled",v);
    instance()->AutoSaveEnabled = v;
}

// Auto generated code (Tools/params_utils.py:431)
void DocumentParams::removeAutoSaveEnabled() {
    instance()->handle->RemoveBool("AutoSaveEnabled");
}

// Auto generated code (Tools/params_utils.py:397)
const char *DocumentParams::docAutoSaveTimeout() {
    return QT_TRANSLATE_NOOP("DocumentParams",
"The autosave interval in minutes, see AutoSaveEnabled.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & DocumentParams::getAutoSaveTimeout() {
    return instance()->AutoSaveTimeout;
}

// Auto generated code (Tools/params_utils.py:413)
const long & DocumentParams::defaultAutoSaveTimeout() {
    const static long def = 15;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void DocumentParams::setAutoSaveTimeout(const long &v) {
    instance()->handle->SetInt("AutoSaveTimeout",v);
    instance()->AutoSaveTimeout = v;
}

// Auto generated code (Tools/params_utils.py:431)
void DocumentParams::removeAutoSaveTimeout() {
    instance()->handle->RemoveInt("AutoSaveTimeout");
}

// Auto generated code (Tools/params_utils.py:397)
const char *DocumentParams::docTransactionLogDeltaHops() {
    return QT_TRANSLATE_NOOP("DocumentParams",
"How long a reverse-delta chain the transaction log allows (sec\n"
"23.2): an entity superseded by a newer one is re-encoded as a\n"
"patch against it unless the chain below it would then be this\n"
"many hops from a full entity. 0 stores everything full.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & DocumentParams::getTransactionLogDeltaHops() {
    return instance()->TransactionLogDeltaHops;
}

// Auto generated code (Tools/params_utils.py:413)
const long & DocumentParams::defaultTransactionLogDeltaHops() {
    const static long def = 8;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void DocumentParams::setTransactionLogDeltaHops(const long &v) {
    instance()->handle->SetInt("TransactionLogDeltaHops",v);
    instance()->TransactionLogDeltaHops = v;
}

// Auto generated code (Tools/params_utils.py:431)
void DocumentParams::removeTransactionLogDeltaHops() {
    instance()->handle->RemoveInt("TransactionLogDeltaHops");
}

// Auto generated code (Tools/params_utils.py:397)
const char *DocumentParams::docTransactionLogDeltaRatio() {
    return QT_TRANSLATE_NOOP("DocumentParams",
"The largest patch the transaction log keeps, as a percent of the\n"
"full compressed size (sec 23.2); a patch over it means the codec\n"
"found nothing to share and the entity stays full.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & DocumentParams::getTransactionLogDeltaRatio() {
    return instance()->TransactionLogDeltaRatio;
}

// Auto generated code (Tools/params_utils.py:413)
const long & DocumentParams::defaultTransactionLogDeltaRatio() {
    const static long def = 50;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void DocumentParams::setTransactionLogDeltaRatio(const long &v) {
    instance()->handle->SetInt("TransactionLogDeltaRatio",v);
    instance()->TransactionLogDeltaRatio = v;
}

// Auto generated code (Tools/params_utils.py:431)
void DocumentParams::removeTransactionLogDeltaRatio() {
    instance()->handle->RemoveInt("TransactionLogDeltaRatio");
}

// Auto generated code (Tools/params_utils.py:397)
const char *DocumentParams::docTransactionLogVerify() {
    return QT_TRANSLATE_NOOP("DocumentParams",
"A composed snapshot (sec 23.3) serialises the properties it\n"
"would have taken from the log anyway and compares: a mismatch\n"
"names a value changed without aboutToSetValue (sec 23.6). Always\n"
"on in a debug build.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & DocumentParams::getTransactionLogVerify() {
    return instance()->TransactionLogVerify;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & DocumentParams::defaultTransactionLogVerify() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void DocumentParams::setTransactionLogVerify(const bool &v) {
    instance()->handle->SetBool("TransactionLogVerify",v);
    instance()->TransactionLogVerify = v;
}

// Auto generated code (Tools/params_utils.py:431)
void DocumentParams::removeTransactionLogVerify() {
    instance()->handle->RemoveBool("TransactionLogVerify");
}

// Auto generated code (Tools/params_utils.py:397)
const char *DocumentParams::docRelativeStringID() {
    return "";
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
"Enable special encoding of indexes name in toponaming. Disabled by\n"
"default for backward compatibility");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & DocumentParams::getHashIndexedName() {
    return instance()->HashIndexedName;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & DocumentParams::defaultHashIndexedName() {
    const static bool def = false;
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
    return "";
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
