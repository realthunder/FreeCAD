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
#include "DocumentParams.h"
using namespace App;

// Auto generated code (Tools/params_utils.py:209)
namespace {
class DocumentParamsP: public ParameterGrp::ObserverType {
public:
    ParameterGrp::handle handle;
    std::unordered_map<const char *,void(*)(DocumentParamsP*),App::CStringHasher,App::CStringHasher> funcs;
    // Auto generated code (Tools/params_utils.py:227)
    boost::signals2::signal<void (const char*)> signalParamChanged;
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

    // Auto generated code (Tools/params_utils.py:240)
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

    // Auto generated code (Tools/params_utils.py:253)
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
        HashIndexedName = this->handle->GetBool("HashIndexedName", false);
        funcs["HashIndexedName"] = &DocumentParamsP::updateHashIndexedName;
        EnableMaterialEdit = this->handle->GetBool("EnableMaterialEdit", true);
        funcs["EnableMaterialEdit"] = &DocumentParamsP::updateEnableMaterialEdit;
    }

    // Auto generated code (Tools/params_utils.py:283)
    ~DocumentParamsP() {
    }

    // Auto generated code (Tools/params_utils.py:290)
    void OnChange(Base::Subject<const char*> &param, const char* sReason) {
        (void)param;
        if(!sReason)
            return;
        auto it = funcs.find(sReason);
        if(it == funcs.end())
            return;
        it->second(this);
        signalParamChanged(sReason);
        
    }


    // Auto generated code (Tools/params_utils.py:310)
    static void updateprefAuthor(DocumentParamsP *self) {
        self->prefAuthor = self->handle->GetASCII("prefAuthor", "");
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateprefSetAuthorOnSave(DocumentParamsP *self) {
        self->prefSetAuthorOnSave = self->handle->GetBool("prefSetAuthorOnSave", false);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateprefCompany(DocumentParamsP *self) {
        self->prefCompany = self->handle->GetASCII("prefCompany", "");
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateprefLicenseType(DocumentParamsP *self) {
        self->prefLicenseType = self->handle->GetInt("prefLicenseType", 0);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateprefLicenseUrl(DocumentParamsP *self) {
        self->prefLicenseUrl = self->handle->GetASCII("prefLicenseUrl", "");
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateCompressionLevel(DocumentParamsP *self) {
        self->CompressionLevel = self->handle->GetInt("CompressionLevel", 3);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateCheckExtension(DocumentParamsP *self) {
        self->CheckExtension = self->handle->GetBool("CheckExtension", true);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateForceXML(DocumentParamsP *self) {
        self->ForceXML = self->handle->GetInt("ForceXML", 3);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateSplitXML(DocumentParamsP *self) {
        self->SplitXML = self->handle->GetBool("SplitXML", true);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updatePreferBinary(DocumentParamsP *self) {
        self->PreferBinary = self->handle->GetBool("PreferBinary", false);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateAutoRemoveFile(DocumentParamsP *self) {
        self->AutoRemoveFile = self->handle->GetBool("AutoRemoveFile", true);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateAutoNameDynamicProperty(DocumentParamsP *self) {
        self->AutoNameDynamicProperty = self->handle->GetBool("AutoNameDynamicProperty", false);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateBackupPolicy(DocumentParamsP *self) {
        self->BackupPolicy = self->handle->GetBool("BackupPolicy", true);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateCreateBackupFiles(DocumentParamsP *self) {
        self->CreateBackupFiles = self->handle->GetBool("CreateBackupFiles", true);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateUseFCBakExtension(DocumentParamsP *self) {
        self->UseFCBakExtension = self->handle->GetBool("UseFCBakExtension", false);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateSaveBackupDateFormat(DocumentParamsP *self) {
        self->SaveBackupDateFormat = self->handle->GetASCII("SaveBackupDateFormat", "%Y%m%d-%H%M%S");
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateCountBackupFiles(DocumentParamsP *self) {
        self->CountBackupFiles = self->handle->GetInt("CountBackupFiles", 1);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateOptimizeRecompute(DocumentParamsP *self) {
        self->OptimizeRecompute = self->handle->GetBool("OptimizeRecompute", true);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateCanAbortRecompute(DocumentParamsP *self) {
        self->CanAbortRecompute = self->handle->GetBool("CanAbortRecompute", true);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateUseHasher(DocumentParamsP *self) {
        self->UseHasher = self->handle->GetBool("UseHasher", true);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateViewObjectTransaction(DocumentParamsP *self) {
        self->ViewObjectTransaction = self->handle->GetBool("ViewObjectTransaction", false);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateWarnRecomputeOnRestore(DocumentParamsP *self) {
        self->WarnRecomputeOnRestore = self->handle->GetBool("WarnRecomputeOnRestore", true);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateNoPartialLoading(DocumentParamsP *self) {
        self->NoPartialLoading = self->handle->GetBool("NoPartialLoading", false);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateSaveThumbnail(DocumentParamsP *self) {
        self->SaveThumbnail = self->handle->GetBool("SaveThumbnail", false);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateThumbnailNoBackground(DocumentParamsP *self) {
        self->ThumbnailNoBackground = self->handle->GetBool("ThumbnailNoBackground", false);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateAddThumbnailLogo(DocumentParamsP *self) {
        self->AddThumbnailLogo = self->handle->GetBool("AddThumbnailLogo", true);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateThumbnailSampleSize(DocumentParamsP *self) {
        self->ThumbnailSampleSize = self->handle->GetInt("ThumbnailSampleSize", 0);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateThumbnailSize(DocumentParamsP *self) {
        self->ThumbnailSize = self->handle->GetInt("ThumbnailSize", 128);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateDuplicateLabels(DocumentParamsP *self) {
        self->DuplicateLabels = self->handle->GetBool("DuplicateLabels", false);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateTransactionOnRecompute(DocumentParamsP *self) {
        self->TransactionOnRecompute = self->handle->GetBool("TransactionOnRecompute", false);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateRelativeStringID(DocumentParamsP *self) {
        self->RelativeStringID = self->handle->GetBool("RelativeStringID", true);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateHashIndexedName(DocumentParamsP *self) {
        self->HashIndexedName = self->handle->GetBool("HashIndexedName", false);
    }
    // Auto generated code (Tools/params_utils.py:310)
    static void updateEnableMaterialEdit(DocumentParamsP *self) {
        self->EnableMaterialEdit = self->handle->GetBool("EnableMaterialEdit", true);
    }
};

// Auto generated code (Tools/params_utils.py:332)
DocumentParamsP *instance() {
    static DocumentParamsP *inst = new DocumentParamsP;
    return inst;
}

} // Anonymous namespace

// Auto generated code (Tools/params_utils.py:343)
ParameterGrp::handle DocumentParams::getHandle() {
    return instance()->handle;
}

// Auto generated code (Tools/params_utils.py:353)
boost::signals2::signal<void (const char*)> &
DocumentParams::signalParamChanged() {
    return instance()->signalParamChanged;
}

// Auto generated code (Tools/params_utils.py:362)
void signalAll() {
    instance()->signalAll();
}

// Auto generated code (Tools/params_utils.py:372)
const char *DocumentParams::docprefAuthor() {
    return "";
}

// Auto generated code (Tools/params_utils.py:380)
const std::string & DocumentParams::getprefAuthor() {
    return instance()->prefAuthor;
}

// Auto generated code (Tools/params_utils.py:388)
const std::string & DocumentParams::defaultprefAuthor() {
    const static std::string def = "";
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void DocumentParams::setprefAuthor(const std::string &v) {
    instance()->handle->SetASCII("prefAuthor",v);
    instance()->prefAuthor = v;
}

// Auto generated code (Tools/params_utils.py:406)
void DocumentParams::removeprefAuthor() {
    instance()->handle->RemoveASCII("prefAuthor");
}

// Auto generated code (Tools/params_utils.py:372)
const char *DocumentParams::docprefSetAuthorOnSave() {
    return "";
}

// Auto generated code (Tools/params_utils.py:380)
const bool & DocumentParams::getprefSetAuthorOnSave() {
    return instance()->prefSetAuthorOnSave;
}

// Auto generated code (Tools/params_utils.py:388)
const bool & DocumentParams::defaultprefSetAuthorOnSave() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void DocumentParams::setprefSetAuthorOnSave(const bool &v) {
    instance()->handle->SetBool("prefSetAuthorOnSave",v);
    instance()->prefSetAuthorOnSave = v;
}

// Auto generated code (Tools/params_utils.py:406)
void DocumentParams::removeprefSetAuthorOnSave() {
    instance()->handle->RemoveBool("prefSetAuthorOnSave");
}

// Auto generated code (Tools/params_utils.py:372)
const char *DocumentParams::docprefCompany() {
    return "";
}

// Auto generated code (Tools/params_utils.py:380)
const std::string & DocumentParams::getprefCompany() {
    return instance()->prefCompany;
}

// Auto generated code (Tools/params_utils.py:388)
const std::string & DocumentParams::defaultprefCompany() {
    const static std::string def = "";
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void DocumentParams::setprefCompany(const std::string &v) {
    instance()->handle->SetASCII("prefCompany",v);
    instance()->prefCompany = v;
}

// Auto generated code (Tools/params_utils.py:406)
void DocumentParams::removeprefCompany() {
    instance()->handle->RemoveASCII("prefCompany");
}

// Auto generated code (Tools/params_utils.py:372)
const char *DocumentParams::docprefLicenseType() {
    return "";
}

// Auto generated code (Tools/params_utils.py:380)
const long & DocumentParams::getprefLicenseType() {
    return instance()->prefLicenseType;
}

// Auto generated code (Tools/params_utils.py:388)
const long & DocumentParams::defaultprefLicenseType() {
    const static long def = 0;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void DocumentParams::setprefLicenseType(const long &v) {
    instance()->handle->SetInt("prefLicenseType",v);
    instance()->prefLicenseType = v;
}

// Auto generated code (Tools/params_utils.py:406)
void DocumentParams::removeprefLicenseType() {
    instance()->handle->RemoveInt("prefLicenseType");
}

// Auto generated code (Tools/params_utils.py:372)
const char *DocumentParams::docprefLicenseUrl() {
    return "";
}

// Auto generated code (Tools/params_utils.py:380)
const std::string & DocumentParams::getprefLicenseUrl() {
    return instance()->prefLicenseUrl;
}

// Auto generated code (Tools/params_utils.py:388)
const std::string & DocumentParams::defaultprefLicenseUrl() {
    const static std::string def = "";
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void DocumentParams::setprefLicenseUrl(const std::string &v) {
    instance()->handle->SetASCII("prefLicenseUrl",v);
    instance()->prefLicenseUrl = v;
}

// Auto generated code (Tools/params_utils.py:406)
void DocumentParams::removeprefLicenseUrl() {
    instance()->handle->RemoveASCII("prefLicenseUrl");
}

// Auto generated code (Tools/params_utils.py:372)
const char *DocumentParams::docCompressionLevel() {
    return "";
}

// Auto generated code (Tools/params_utils.py:380)
const long & DocumentParams::getCompressionLevel() {
    return instance()->CompressionLevel;
}

// Auto generated code (Tools/params_utils.py:388)
const long & DocumentParams::defaultCompressionLevel() {
    const static long def = 3;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void DocumentParams::setCompressionLevel(const long &v) {
    instance()->handle->SetInt("CompressionLevel",v);
    instance()->CompressionLevel = v;
}

// Auto generated code (Tools/params_utils.py:406)
void DocumentParams::removeCompressionLevel() {
    instance()->handle->RemoveInt("CompressionLevel");
}

// Auto generated code (Tools/params_utils.py:372)
const char *DocumentParams::docCheckExtension() {
    return "";
}

// Auto generated code (Tools/params_utils.py:380)
const bool & DocumentParams::getCheckExtension() {
    return instance()->CheckExtension;
}

// Auto generated code (Tools/params_utils.py:388)
const bool & DocumentParams::defaultCheckExtension() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void DocumentParams::setCheckExtension(const bool &v) {
    instance()->handle->SetBool("CheckExtension",v);
    instance()->CheckExtension = v;
}

// Auto generated code (Tools/params_utils.py:406)
void DocumentParams::removeCheckExtension() {
    instance()->handle->RemoveBool("CheckExtension");
}

// Auto generated code (Tools/params_utils.py:372)
const char *DocumentParams::docForceXML() {
    return "";
}

// Auto generated code (Tools/params_utils.py:380)
const long & DocumentParams::getForceXML() {
    return instance()->ForceXML;
}

// Auto generated code (Tools/params_utils.py:388)
const long & DocumentParams::defaultForceXML() {
    const static long def = 3;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void DocumentParams::setForceXML(const long &v) {
    instance()->handle->SetInt("ForceXML",v);
    instance()->ForceXML = v;
}

// Auto generated code (Tools/params_utils.py:406)
void DocumentParams::removeForceXML() {
    instance()->handle->RemoveInt("ForceXML");
}

// Auto generated code (Tools/params_utils.py:372)
const char *DocumentParams::docSplitXML() {
    return "";
}

// Auto generated code (Tools/params_utils.py:380)
const bool & DocumentParams::getSplitXML() {
    return instance()->SplitXML;
}

// Auto generated code (Tools/params_utils.py:388)
const bool & DocumentParams::defaultSplitXML() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void DocumentParams::setSplitXML(const bool &v) {
    instance()->handle->SetBool("SplitXML",v);
    instance()->SplitXML = v;
}

// Auto generated code (Tools/params_utils.py:406)
void DocumentParams::removeSplitXML() {
    instance()->handle->RemoveBool("SplitXML");
}

// Auto generated code (Tools/params_utils.py:372)
const char *DocumentParams::docPreferBinary() {
    return "";
}

// Auto generated code (Tools/params_utils.py:380)
const bool & DocumentParams::getPreferBinary() {
    return instance()->PreferBinary;
}

// Auto generated code (Tools/params_utils.py:388)
const bool & DocumentParams::defaultPreferBinary() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void DocumentParams::setPreferBinary(const bool &v) {
    instance()->handle->SetBool("PreferBinary",v);
    instance()->PreferBinary = v;
}

// Auto generated code (Tools/params_utils.py:406)
void DocumentParams::removePreferBinary() {
    instance()->handle->RemoveBool("PreferBinary");
}

// Auto generated code (Tools/params_utils.py:372)
const char *DocumentParams::docAutoRemoveFile() {
    return "";
}

// Auto generated code (Tools/params_utils.py:380)
const bool & DocumentParams::getAutoRemoveFile() {
    return instance()->AutoRemoveFile;
}

// Auto generated code (Tools/params_utils.py:388)
const bool & DocumentParams::defaultAutoRemoveFile() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void DocumentParams::setAutoRemoveFile(const bool &v) {
    instance()->handle->SetBool("AutoRemoveFile",v);
    instance()->AutoRemoveFile = v;
}

// Auto generated code (Tools/params_utils.py:406)
void DocumentParams::removeAutoRemoveFile() {
    instance()->handle->RemoveBool("AutoRemoveFile");
}

// Auto generated code (Tools/params_utils.py:372)
const char *DocumentParams::docAutoNameDynamicProperty() {
    return "";
}

// Auto generated code (Tools/params_utils.py:380)
const bool & DocumentParams::getAutoNameDynamicProperty() {
    return instance()->AutoNameDynamicProperty;
}

// Auto generated code (Tools/params_utils.py:388)
const bool & DocumentParams::defaultAutoNameDynamicProperty() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void DocumentParams::setAutoNameDynamicProperty(const bool &v) {
    instance()->handle->SetBool("AutoNameDynamicProperty",v);
    instance()->AutoNameDynamicProperty = v;
}

// Auto generated code (Tools/params_utils.py:406)
void DocumentParams::removeAutoNameDynamicProperty() {
    instance()->handle->RemoveBool("AutoNameDynamicProperty");
}

// Auto generated code (Tools/params_utils.py:372)
const char *DocumentParams::docBackupPolicy() {
    return "";
}

// Auto generated code (Tools/params_utils.py:380)
const bool & DocumentParams::getBackupPolicy() {
    return instance()->BackupPolicy;
}

// Auto generated code (Tools/params_utils.py:388)
const bool & DocumentParams::defaultBackupPolicy() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void DocumentParams::setBackupPolicy(const bool &v) {
    instance()->handle->SetBool("BackupPolicy",v);
    instance()->BackupPolicy = v;
}

// Auto generated code (Tools/params_utils.py:406)
void DocumentParams::removeBackupPolicy() {
    instance()->handle->RemoveBool("BackupPolicy");
}

// Auto generated code (Tools/params_utils.py:372)
const char *DocumentParams::docCreateBackupFiles() {
    return "";
}

// Auto generated code (Tools/params_utils.py:380)
const bool & DocumentParams::getCreateBackupFiles() {
    return instance()->CreateBackupFiles;
}

// Auto generated code (Tools/params_utils.py:388)
const bool & DocumentParams::defaultCreateBackupFiles() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void DocumentParams::setCreateBackupFiles(const bool &v) {
    instance()->handle->SetBool("CreateBackupFiles",v);
    instance()->CreateBackupFiles = v;
}

// Auto generated code (Tools/params_utils.py:406)
void DocumentParams::removeCreateBackupFiles() {
    instance()->handle->RemoveBool("CreateBackupFiles");
}

// Auto generated code (Tools/params_utils.py:372)
const char *DocumentParams::docUseFCBakExtension() {
    return "";
}

// Auto generated code (Tools/params_utils.py:380)
const bool & DocumentParams::getUseFCBakExtension() {
    return instance()->UseFCBakExtension;
}

// Auto generated code (Tools/params_utils.py:388)
const bool & DocumentParams::defaultUseFCBakExtension() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void DocumentParams::setUseFCBakExtension(const bool &v) {
    instance()->handle->SetBool("UseFCBakExtension",v);
    instance()->UseFCBakExtension = v;
}

// Auto generated code (Tools/params_utils.py:406)
void DocumentParams::removeUseFCBakExtension() {
    instance()->handle->RemoveBool("UseFCBakExtension");
}

// Auto generated code (Tools/params_utils.py:372)
const char *DocumentParams::docSaveBackupDateFormat() {
    return "";
}

// Auto generated code (Tools/params_utils.py:380)
const std::string & DocumentParams::getSaveBackupDateFormat() {
    return instance()->SaveBackupDateFormat;
}

// Auto generated code (Tools/params_utils.py:388)
const std::string & DocumentParams::defaultSaveBackupDateFormat() {
    const static std::string def = "%Y%m%d-%H%M%S";
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void DocumentParams::setSaveBackupDateFormat(const std::string &v) {
    instance()->handle->SetASCII("SaveBackupDateFormat",v);
    instance()->SaveBackupDateFormat = v;
}

// Auto generated code (Tools/params_utils.py:406)
void DocumentParams::removeSaveBackupDateFormat() {
    instance()->handle->RemoveASCII("SaveBackupDateFormat");
}

// Auto generated code (Tools/params_utils.py:372)
const char *DocumentParams::docCountBackupFiles() {
    return "";
}

// Auto generated code (Tools/params_utils.py:380)
const long & DocumentParams::getCountBackupFiles() {
    return instance()->CountBackupFiles;
}

// Auto generated code (Tools/params_utils.py:388)
const long & DocumentParams::defaultCountBackupFiles() {
    const static long def = 1;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void DocumentParams::setCountBackupFiles(const long &v) {
    instance()->handle->SetInt("CountBackupFiles",v);
    instance()->CountBackupFiles = v;
}

// Auto generated code (Tools/params_utils.py:406)
void DocumentParams::removeCountBackupFiles() {
    instance()->handle->RemoveInt("CountBackupFiles");
}

// Auto generated code (Tools/params_utils.py:372)
const char *DocumentParams::docOptimizeRecompute() {
    return "";
}

// Auto generated code (Tools/params_utils.py:380)
const bool & DocumentParams::getOptimizeRecompute() {
    return instance()->OptimizeRecompute;
}

// Auto generated code (Tools/params_utils.py:388)
const bool & DocumentParams::defaultOptimizeRecompute() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void DocumentParams::setOptimizeRecompute(const bool &v) {
    instance()->handle->SetBool("OptimizeRecompute",v);
    instance()->OptimizeRecompute = v;
}

// Auto generated code (Tools/params_utils.py:406)
void DocumentParams::removeOptimizeRecompute() {
    instance()->handle->RemoveBool("OptimizeRecompute");
}

// Auto generated code (Tools/params_utils.py:372)
const char *DocumentParams::docCanAbortRecompute() {
    return "";
}

// Auto generated code (Tools/params_utils.py:380)
const bool & DocumentParams::getCanAbortRecompute() {
    return instance()->CanAbortRecompute;
}

// Auto generated code (Tools/params_utils.py:388)
const bool & DocumentParams::defaultCanAbortRecompute() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void DocumentParams::setCanAbortRecompute(const bool &v) {
    instance()->handle->SetBool("CanAbortRecompute",v);
    instance()->CanAbortRecompute = v;
}

// Auto generated code (Tools/params_utils.py:406)
void DocumentParams::removeCanAbortRecompute() {
    instance()->handle->RemoveBool("CanAbortRecompute");
}

// Auto generated code (Tools/params_utils.py:372)
const char *DocumentParams::docUseHasher() {
    return "";
}

// Auto generated code (Tools/params_utils.py:380)
const bool & DocumentParams::getUseHasher() {
    return instance()->UseHasher;
}

// Auto generated code (Tools/params_utils.py:388)
const bool & DocumentParams::defaultUseHasher() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void DocumentParams::setUseHasher(const bool &v) {
    instance()->handle->SetBool("UseHasher",v);
    instance()->UseHasher = v;
}

// Auto generated code (Tools/params_utils.py:406)
void DocumentParams::removeUseHasher() {
    instance()->handle->RemoveBool("UseHasher");
}

// Auto generated code (Tools/params_utils.py:372)
const char *DocumentParams::docViewObjectTransaction() {
    return "";
}

// Auto generated code (Tools/params_utils.py:380)
const bool & DocumentParams::getViewObjectTransaction() {
    return instance()->ViewObjectTransaction;
}

// Auto generated code (Tools/params_utils.py:388)
const bool & DocumentParams::defaultViewObjectTransaction() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void DocumentParams::setViewObjectTransaction(const bool &v) {
    instance()->handle->SetBool("ViewObjectTransaction",v);
    instance()->ViewObjectTransaction = v;
}

// Auto generated code (Tools/params_utils.py:406)
void DocumentParams::removeViewObjectTransaction() {
    instance()->handle->RemoveBool("ViewObjectTransaction");
}

// Auto generated code (Tools/params_utils.py:372)
const char *DocumentParams::docWarnRecomputeOnRestore() {
    return "";
}

// Auto generated code (Tools/params_utils.py:380)
const bool & DocumentParams::getWarnRecomputeOnRestore() {
    return instance()->WarnRecomputeOnRestore;
}

// Auto generated code (Tools/params_utils.py:388)
const bool & DocumentParams::defaultWarnRecomputeOnRestore() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void DocumentParams::setWarnRecomputeOnRestore(const bool &v) {
    instance()->handle->SetBool("WarnRecomputeOnRestore",v);
    instance()->WarnRecomputeOnRestore = v;
}

// Auto generated code (Tools/params_utils.py:406)
void DocumentParams::removeWarnRecomputeOnRestore() {
    instance()->handle->RemoveBool("WarnRecomputeOnRestore");
}

// Auto generated code (Tools/params_utils.py:372)
const char *DocumentParams::docNoPartialLoading() {
    return "";
}

// Auto generated code (Tools/params_utils.py:380)
const bool & DocumentParams::getNoPartialLoading() {
    return instance()->NoPartialLoading;
}

// Auto generated code (Tools/params_utils.py:388)
const bool & DocumentParams::defaultNoPartialLoading() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void DocumentParams::setNoPartialLoading(const bool &v) {
    instance()->handle->SetBool("NoPartialLoading",v);
    instance()->NoPartialLoading = v;
}

// Auto generated code (Tools/params_utils.py:406)
void DocumentParams::removeNoPartialLoading() {
    instance()->handle->RemoveBool("NoPartialLoading");
}

// Auto generated code (Tools/params_utils.py:372)
const char *DocumentParams::docSaveThumbnail() {
    return "";
}

// Auto generated code (Tools/params_utils.py:380)
const bool & DocumentParams::getSaveThumbnail() {
    return instance()->SaveThumbnail;
}

// Auto generated code (Tools/params_utils.py:388)
const bool & DocumentParams::defaultSaveThumbnail() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void DocumentParams::setSaveThumbnail(const bool &v) {
    instance()->handle->SetBool("SaveThumbnail",v);
    instance()->SaveThumbnail = v;
}

// Auto generated code (Tools/params_utils.py:406)
void DocumentParams::removeSaveThumbnail() {
    instance()->handle->RemoveBool("SaveThumbnail");
}

// Auto generated code (Tools/params_utils.py:372)
const char *DocumentParams::docThumbnailNoBackground() {
    return "";
}

// Auto generated code (Tools/params_utils.py:380)
const bool & DocumentParams::getThumbnailNoBackground() {
    return instance()->ThumbnailNoBackground;
}

// Auto generated code (Tools/params_utils.py:388)
const bool & DocumentParams::defaultThumbnailNoBackground() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void DocumentParams::setThumbnailNoBackground(const bool &v) {
    instance()->handle->SetBool("ThumbnailNoBackground",v);
    instance()->ThumbnailNoBackground = v;
}

// Auto generated code (Tools/params_utils.py:406)
void DocumentParams::removeThumbnailNoBackground() {
    instance()->handle->RemoveBool("ThumbnailNoBackground");
}

// Auto generated code (Tools/params_utils.py:372)
const char *DocumentParams::docAddThumbnailLogo() {
    return "";
}

// Auto generated code (Tools/params_utils.py:380)
const bool & DocumentParams::getAddThumbnailLogo() {
    return instance()->AddThumbnailLogo;
}

// Auto generated code (Tools/params_utils.py:388)
const bool & DocumentParams::defaultAddThumbnailLogo() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void DocumentParams::setAddThumbnailLogo(const bool &v) {
    instance()->handle->SetBool("AddThumbnailLogo",v);
    instance()->AddThumbnailLogo = v;
}

// Auto generated code (Tools/params_utils.py:406)
void DocumentParams::removeAddThumbnailLogo() {
    instance()->handle->RemoveBool("AddThumbnailLogo");
}

// Auto generated code (Tools/params_utils.py:372)
const char *DocumentParams::docThumbnailSampleSize() {
    return "";
}

// Auto generated code (Tools/params_utils.py:380)
const long & DocumentParams::getThumbnailSampleSize() {
    return instance()->ThumbnailSampleSize;
}

// Auto generated code (Tools/params_utils.py:388)
const long & DocumentParams::defaultThumbnailSampleSize() {
    const static long def = 0;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void DocumentParams::setThumbnailSampleSize(const long &v) {
    instance()->handle->SetInt("ThumbnailSampleSize",v);
    instance()->ThumbnailSampleSize = v;
}

// Auto generated code (Tools/params_utils.py:406)
void DocumentParams::removeThumbnailSampleSize() {
    instance()->handle->RemoveInt("ThumbnailSampleSize");
}

// Auto generated code (Tools/params_utils.py:372)
const char *DocumentParams::docThumbnailSize() {
    return "";
}

// Auto generated code (Tools/params_utils.py:380)
const long & DocumentParams::getThumbnailSize() {
    return instance()->ThumbnailSize;
}

// Auto generated code (Tools/params_utils.py:388)
const long & DocumentParams::defaultThumbnailSize() {
    const static long def = 128;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void DocumentParams::setThumbnailSize(const long &v) {
    instance()->handle->SetInt("ThumbnailSize",v);
    instance()->ThumbnailSize = v;
}

// Auto generated code (Tools/params_utils.py:406)
void DocumentParams::removeThumbnailSize() {
    instance()->handle->RemoveInt("ThumbnailSize");
}

// Auto generated code (Tools/params_utils.py:372)
const char *DocumentParams::docDuplicateLabels() {
    return "";
}

// Auto generated code (Tools/params_utils.py:380)
const bool & DocumentParams::getDuplicateLabels() {
    return instance()->DuplicateLabels;
}

// Auto generated code (Tools/params_utils.py:388)
const bool & DocumentParams::defaultDuplicateLabels() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void DocumentParams::setDuplicateLabels(const bool &v) {
    instance()->handle->SetBool("DuplicateLabels",v);
    instance()->DuplicateLabels = v;
}

// Auto generated code (Tools/params_utils.py:406)
void DocumentParams::removeDuplicateLabels() {
    instance()->handle->RemoveBool("DuplicateLabels");
}

// Auto generated code (Tools/params_utils.py:372)
const char *DocumentParams::docTransactionOnRecompute() {
    return "";
}

// Auto generated code (Tools/params_utils.py:380)
const bool & DocumentParams::getTransactionOnRecompute() {
    return instance()->TransactionOnRecompute;
}

// Auto generated code (Tools/params_utils.py:388)
const bool & DocumentParams::defaultTransactionOnRecompute() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void DocumentParams::setTransactionOnRecompute(const bool &v) {
    instance()->handle->SetBool("TransactionOnRecompute",v);
    instance()->TransactionOnRecompute = v;
}

// Auto generated code (Tools/params_utils.py:406)
void DocumentParams::removeTransactionOnRecompute() {
    instance()->handle->RemoveBool("TransactionOnRecompute");
}

// Auto generated code (Tools/params_utils.py:372)
const char *DocumentParams::docRelativeStringID() {
    return "";
}

// Auto generated code (Tools/params_utils.py:380)
const bool & DocumentParams::getRelativeStringID() {
    return instance()->RelativeStringID;
}

// Auto generated code (Tools/params_utils.py:388)
const bool & DocumentParams::defaultRelativeStringID() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void DocumentParams::setRelativeStringID(const bool &v) {
    instance()->handle->SetBool("RelativeStringID",v);
    instance()->RelativeStringID = v;
}

// Auto generated code (Tools/params_utils.py:406)
void DocumentParams::removeRelativeStringID() {
    instance()->handle->RemoveBool("RelativeStringID");
}

// Auto generated code (Tools/params_utils.py:372)
const char *DocumentParams::docHashIndexedName() {
    return QT_TRANSLATE_NOOP("DocumentParams",
"Enable special encoding of indexes name in toponaming. Disabled by\n"
"default for backward compatibility");
}

// Auto generated code (Tools/params_utils.py:380)
const bool & DocumentParams::getHashIndexedName() {
    return instance()->HashIndexedName;
}

// Auto generated code (Tools/params_utils.py:388)
const bool & DocumentParams::defaultHashIndexedName() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void DocumentParams::setHashIndexedName(const bool &v) {
    instance()->handle->SetBool("HashIndexedName",v);
    instance()->HashIndexedName = v;
}

// Auto generated code (Tools/params_utils.py:406)
void DocumentParams::removeHashIndexedName() {
    instance()->handle->RemoveBool("HashIndexedName");
}

// Auto generated code (Tools/params_utils.py:372)
const char *DocumentParams::docEnableMaterialEdit() {
    return "";
}

// Auto generated code (Tools/params_utils.py:380)
const bool & DocumentParams::getEnableMaterialEdit() {
    return instance()->EnableMaterialEdit;
}

// Auto generated code (Tools/params_utils.py:388)
const bool & DocumentParams::defaultEnableMaterialEdit() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:397)
void DocumentParams::setEnableMaterialEdit(const bool &v) {
    instance()->handle->SetBool("EnableMaterialEdit",v);
    instance()->EnableMaterialEdit = v;
}

// Auto generated code (Tools/params_utils.py:406)
void DocumentParams::removeEnableMaterialEdit() {
    instance()->handle->RemoveBool("EnableMaterialEdit");
}
//[[[end]]]
