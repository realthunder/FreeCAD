/****************************************************************************
 *   Copyright (c) 2026 Zheng Lei (realthunder) <realthunder.dev@gmail.com> *
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

// What an unset DontUseNativeDialog means is decided by the build
// (FREECAD_USE_QT_FILEDIALOG), which gives this file the same definition
// it gives FileDialog.cpp.
#ifdef USE_QT_FILEDIALOG
#   define FC_QT_FILEDIALOG_DEFAULT true
#else
#   define FC_QT_FILEDIALOG_DEFAULT false
#endif

/*[[[cog
import DialogParams
DialogParams.define()
]]]*/

// Auto generated code (Tools/params_utils.py:198)
#include <unordered_map>
#include <App/Application.h>
#include <App/DynamicProperty.h>
#include <App/ParamRegistry.h>
#include "DialogParams.h"
using namespace Gui;

// Auto generated code (Tools/params_utils.py:210)
namespace {
class DialogParamsP: public ParameterGrp::ObserverType {
public:
    ParameterGrp::handle handle;
    std::unordered_map<const char *,void(*)(DialogParamsP*),App::CStringHasher,App::CStringHasher> funcs;
    // Auto generated code (Tools/params_utils.py:228)
    fastsignals::signal<void (const char*)> signalParamChanged;
    void signalAll()
    {
        signalParamChanged("DontUseNativeDialog");
        signalParamChanged("DontUseNativeColorDialog");

    // Auto generated code (Tools/params_utils.py:241)
    }
    bool DontUseNativeDialog;
    bool DontUseNativeColorDialog;

    // Auto generated code (Tools/params_utils.py:254)
    DialogParamsP() {
        handle = App::GetApplication().GetParameterGroupByPath("User parameter:BaseApp/Preferences/Dialog");
        handle->Attach(this);

        DontUseNativeDialog = this->handle->GetBool("DontUseNativeDialog", FC_QT_FILEDIALOG_DEFAULT);
        funcs["DontUseNativeDialog"] = &DialogParamsP::updateDontUseNativeDialog;
        DontUseNativeColorDialog = this->handle->GetBool("DontUseNativeColorDialog", true);
        funcs["DontUseNativeColorDialog"] = &DialogParamsP::updateDontUseNativeColorDialog;
    }

    // Auto generated code (Tools/params_utils.py:284)
    ~DialogParamsP() override = default;

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
    static void updateDontUseNativeDialog(DialogParamsP *self) {
        self->DontUseNativeDialog = self->handle->GetBool("DontUseNativeDialog", FC_QT_FILEDIALOG_DEFAULT);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateDontUseNativeColorDialog(DialogParamsP *self) {
        self->DontUseNativeColorDialog = self->handle->GetBool("DontUseNativeColorDialog", true);
    }
};

// Auto generated code (Tools/params_utils.py:336)
DialogParamsP *instance() {
    static DialogParamsP *inst = new DialogParamsP;
    return inst;
}

} // Anonymous namespace

// Auto generated code (Tools/params_utils.py:352)
static const App::ParamRegistry::Registrar _DialogParamsRegistrar({
    App::ParamInfo("Gui", "DialogParams", "User parameter:BaseApp/Preferences/Dialog", "DontUseNativeDialog", "DontUseNativeDialog", App::ParamInfo::Bool, FC_QT_FILEDIALOG_DEFAULT)
        .setTitle("Use Qt's file dialog")
        .setDoc("Open and save files with Qt's own file dialog instead of the\n"
"operating system's. Holding Shift while the dialog is called for\n"
"gives the other one. Read each time a file dialog opens."),
    App::ParamInfo("Gui", "DialogParams", "User parameter:BaseApp/Preferences/Dialog", "DontUseNativeColorDialog", "DontUseNativeColorDialog", App::ParamInfo::Bool, true)
        .setTitle("Use Qt's colour dialog")
        .setDoc("Pick colours with Qt's own colour dialog instead of the operating\n"
"system's. Holding Shift gives the other one. Read each time a\n"
"colour dialog opens."),
});

// Auto generated code (Tools/params_utils.py:368)
ParameterGrp::handle DialogParams::getHandle() {
    return instance()->handle;
}

// Auto generated code (Tools/params_utils.py:378)
fastsignals::signal<void (const char*)> &
DialogParams::signalParamChanged() {
    return instance()->signalParamChanged;
}

// Auto generated code (Tools/params_utils.py:387)
void DialogParams::signalAll() {
    instance()->signalAll();
}

// Auto generated code (Tools/params_utils.py:397)
const char *DialogParams::docDontUseNativeDialog() {
    return QT_TRANSLATE_NOOP("DialogParams",
"Open and save files with Qt's own file dialog instead of the\n"
"operating system's. Holding Shift while the dialog is called for\n"
"gives the other one. Read each time a file dialog opens.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & DialogParams::getDontUseNativeDialog() {
    return instance()->DontUseNativeDialog;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & DialogParams::defaultDontUseNativeDialog() {
    const static bool def = FC_QT_FILEDIALOG_DEFAULT;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void DialogParams::setDontUseNativeDialog(const bool &v) {
    instance()->handle->SetBool("DontUseNativeDialog",v);
    instance()->DontUseNativeDialog = v;
}

// Auto generated code (Tools/params_utils.py:431)
void DialogParams::removeDontUseNativeDialog() {
    instance()->handle->RemoveBool("DontUseNativeDialog");
}

// Auto generated code (Tools/params_utils.py:397)
const char *DialogParams::docDontUseNativeColorDialog() {
    return QT_TRANSLATE_NOOP("DialogParams",
"Pick colours with Qt's own colour dialog instead of the operating\n"
"system's. Holding Shift gives the other one. Read each time a\n"
"colour dialog opens.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & DialogParams::getDontUseNativeColorDialog() {
    return instance()->DontUseNativeColorDialog;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & DialogParams::defaultDontUseNativeColorDialog() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void DialogParams::setDontUseNativeColorDialog(const bool &v) {
    instance()->handle->SetBool("DontUseNativeColorDialog",v);
    instance()->DontUseNativeColorDialog = v;
}

// Auto generated code (Tools/params_utils.py:431)
void DialogParams::removeDontUseNativeColorDialog() {
    instance()->handle->RemoveBool("DontUseNativeColorDialog");
}
//[[[end]]]
