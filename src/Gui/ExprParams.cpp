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
import ExprParams
ExprParams.define()
]]]*/

#ifdef FC_OS_MACOSX
    // Both MacOSX and Windows will pass through mouse event for the
    // transparent part of a top level dialog, and we need the mouse event to
    // handle resizing of expression editor. Windows will capture the mouse
    // event as long as there is any non transparency, hence setting alpha to 1
    // works. But OSX seems to require a higher transparency.
#   define FC_EXPR_PARAM_EDIT_BG_ALPHA 16
#else
#   define FC_EXPR_PARAM_EDIT_BG_ALPHA 1
#endif

// Auto generated code (Tools/params_utils.py:162)
#include <unordered_map>
#include <App/Application.h>
#include <App/DynamicProperty.h>
#include "ExprParams.h"
using namespace Gui;

// Auto generated code (Tools/params_utils.py:171)
namespace {
class ExprParamsP: public ParameterGrp::ObserverType {
public:
    ParameterGrp::handle handle;
    std::unordered_map<const char *,void(*)(ExprParamsP*),App::CStringHasher,App::CStringHasher> funcs;

    bool CompleterCaseSensitive;
    bool CompleterMatchExact;
    bool CompleterUnfiltered;
    bool NoSystemBackground;
    std::string EditorTrigger;
    bool AutoHideEditorIcon;
    bool AllowReturn;
    bool EvalFuncOnEdit;
    long EditDialogWidth;
    long EditDialogHeight;
    long EditDialogTextHeight;
    long EditDialogBGAlpha;

    // Auto generated code (Tools/params_utils.py:199)
    ExprParamsP() {
        handle = App::GetApplication().GetParameterGroupByPath("User parameter:BaseApp/Preferences/Expression");
        handle->Attach(this);

        CompleterCaseSensitive = handle->GetBool("CompleterCaseSensitive", false);
        funcs["CompleterCaseSensitive"] = &ExprParamsP::updateCompleterCaseSensitive;
        CompleterMatchExact = handle->GetBool("CompleterMatchExact", false);
        funcs["CompleterMatchExact"] = &ExprParamsP::updateCompleterMatchExact;
        CompleterUnfiltered = handle->GetBool("CompleterUnfiltered", false);
        funcs["CompleterUnfiltered"] = &ExprParamsP::updateCompleterUnfiltered;
        NoSystemBackground = handle->GetBool("NoSystemBackground", false);
        funcs["NoSystemBackground"] = &ExprParamsP::updateNoSystemBackground;
        EditorTrigger = handle->GetASCII("EditorTrigger", "=");
        funcs["EditorTrigger"] = &ExprParamsP::updateEditorTrigger;
        AutoHideEditorIcon = handle->GetBool("AutoHideEditorIcon", true);
        funcs["AutoHideEditorIcon"] = &ExprParamsP::updateAutoHideEditorIcon;
        AllowReturn = handle->GetBool("AllowReturn", false);
        funcs["AllowReturn"] = &ExprParamsP::updateAllowReturn;
        EvalFuncOnEdit = handle->GetBool("EvalFuncOnEdit", false);
        funcs["EvalFuncOnEdit"] = &ExprParamsP::updateEvalFuncOnEdit;
        EditDialogWidth = handle->GetInt("EditDialogWidth", 0);
        funcs["EditDialogWidth"] = &ExprParamsP::updateEditDialogWidth;
        EditDialogHeight = handle->GetInt("EditDialogHeight", 0);
        funcs["EditDialogHeight"] = &ExprParamsP::updateEditDialogHeight;
        EditDialogTextHeight = handle->GetInt("EditDialogTextHeight", 0);
        funcs["EditDialogTextHeight"] = &ExprParamsP::updateEditDialogTextHeight;
        EditDialogBGAlpha = handle->GetInt("EditDialogBGAlpha", FC_EXPR_PARAM_EDIT_BG_ALPHA);
        funcs["EditDialogBGAlpha"] = &ExprParamsP::updateEditDialogBGAlpha;
    }

    // Auto generated code (Tools/params_utils.py:213)
    ~ExprParamsP() {
    }

    // Auto generated code (Tools/params_utils.py:218)
    void OnChange(Base::Subject<const char*> &, const char* sReason) {
        if(!sReason)
            return;
        auto it = funcs.find(sReason);
        if(it == funcs.end())
            return;
        it->second(this);
        
    }


    // Auto generated code (Tools/params_utils.py:234)
    static void updateCompleterCaseSensitive(ExprParamsP *self) {
        self->CompleterCaseSensitive = self->handle->GetBool("CompleterCaseSensitive", false);
    }
    // Auto generated code (Tools/params_utils.py:234)
    static void updateCompleterMatchExact(ExprParamsP *self) {
        self->CompleterMatchExact = self->handle->GetBool("CompleterMatchExact", false);
    }
    // Auto generated code (Tools/params_utils.py:234)
    static void updateCompleterUnfiltered(ExprParamsP *self) {
        self->CompleterUnfiltered = self->handle->GetBool("CompleterUnfiltered", false);
    }
    // Auto generated code (Tools/params_utils.py:234)
    static void updateNoSystemBackground(ExprParamsP *self) {
        self->NoSystemBackground = self->handle->GetBool("NoSystemBackground", false);
    }
    // Auto generated code (Tools/params_utils.py:234)
    static void updateEditorTrigger(ExprParamsP *self) {
        self->EditorTrigger = self->handle->GetASCII("EditorTrigger", "=");
    }
    // Auto generated code (Tools/params_utils.py:234)
    static void updateAutoHideEditorIcon(ExprParamsP *self) {
        self->AutoHideEditorIcon = self->handle->GetBool("AutoHideEditorIcon", true);
    }
    // Auto generated code (Tools/params_utils.py:234)
    static void updateAllowReturn(ExprParamsP *self) {
        self->AllowReturn = self->handle->GetBool("AllowReturn", false);
    }
    // Auto generated code (Tools/params_utils.py:234)
    static void updateEvalFuncOnEdit(ExprParamsP *self) {
        self->EvalFuncOnEdit = self->handle->GetBool("EvalFuncOnEdit", false);
    }
    // Auto generated code (Tools/params_utils.py:234)
    static void updateEditDialogWidth(ExprParamsP *self) {
        self->EditDialogWidth = self->handle->GetInt("EditDialogWidth", 0);
    }
    // Auto generated code (Tools/params_utils.py:234)
    static void updateEditDialogHeight(ExprParamsP *self) {
        self->EditDialogHeight = self->handle->GetInt("EditDialogHeight", 0);
    }
    // Auto generated code (Tools/params_utils.py:234)
    static void updateEditDialogTextHeight(ExprParamsP *self) {
        self->EditDialogTextHeight = self->handle->GetInt("EditDialogTextHeight", 0);
    }
    // Auto generated code (Tools/params_utils.py:234)
    static void updateEditDialogBGAlpha(ExprParamsP *self) {
        self->EditDialogBGAlpha = self->handle->GetInt("EditDialogBGAlpha", FC_EXPR_PARAM_EDIT_BG_ALPHA);
    }
};

// Auto generated code (Tools/params_utils.py:252)
ExprParamsP *instance() {
    static ExprParamsP *inst = new ExprParamsP;
    return inst;
}

} // Anonymous namespace

// Auto generated code (Tools/params_utils.py:261)
ParameterGrp::handle ExprParams::getHandle() {
    return instance()->handle;
}

// Auto generated code (Tools/params_utils.py:284)
const char *ExprParams::docCompleterCaseSensitive() {
    return QT_TRANSLATE_NOOP("ExprParams",
"Expression completer with case sensitive");
}

// Auto generated code (Tools/params_utils.py:290)
const bool & ExprParams::getCompleterCaseSensitive() {
    return instance()->CompleterCaseSensitive;
}

// Auto generated code (Tools/params_utils.py:296)
const bool & ExprParams::defaultCompleterCaseSensitive() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:303)
void ExprParams::setCompleterCaseSensitive(const bool &v) {
    instance()->handle->SetBool("CompleterCaseSensitive",v);
    instance()->CompleterCaseSensitive = v;
}

// Auto generated code (Tools/params_utils.py:310)
void ExprParams::removeCompleterCaseSensitive() {
    instance()->handle->RemoveBool("CompleterCaseSensitive");
}

// Auto generated code (Tools/params_utils.py:284)
const char *ExprParams::docCompleterMatchExact() {
    return QT_TRANSLATE_NOOP("ExprParams",
"Expression completer match exact");
}

// Auto generated code (Tools/params_utils.py:290)
const bool & ExprParams::getCompleterMatchExact() {
    return instance()->CompleterMatchExact;
}

// Auto generated code (Tools/params_utils.py:296)
const bool & ExprParams::defaultCompleterMatchExact() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:303)
void ExprParams::setCompleterMatchExact(const bool &v) {
    instance()->handle->SetBool("CompleterMatchExact",v);
    instance()->CompleterMatchExact = v;
}

// Auto generated code (Tools/params_utils.py:310)
void ExprParams::removeCompleterMatchExact() {
    instance()->handle->RemoveBool("CompleterMatchExact");
}

// Auto generated code (Tools/params_utils.py:284)
const char *ExprParams::docCompleterUnfiltered() {
    return QT_TRANSLATE_NOOP("ExprParams",
"Expression completer unfiltered completion mode");
}

// Auto generated code (Tools/params_utils.py:290)
const bool & ExprParams::getCompleterUnfiltered() {
    return instance()->CompleterUnfiltered;
}

// Auto generated code (Tools/params_utils.py:296)
const bool & ExprParams::defaultCompleterUnfiltered() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:303)
void ExprParams::setCompleterUnfiltered(const bool &v) {
    instance()->handle->SetBool("CompleterUnfiltered",v);
    instance()->CompleterUnfiltered = v;
}

// Auto generated code (Tools/params_utils.py:310)
void ExprParams::removeCompleterUnfiltered() {
    instance()->handle->RemoveBool("CompleterUnfiltered");
}

// Auto generated code (Tools/params_utils.py:284)
const char *ExprParams::docNoSystemBackground() {
    return QT_TRANSLATE_NOOP("ExprParams",
"Enable in place expression editing");
}

// Auto generated code (Tools/params_utils.py:290)
const bool & ExprParams::getNoSystemBackground() {
    return instance()->NoSystemBackground;
}

// Auto generated code (Tools/params_utils.py:296)
const bool & ExprParams::defaultNoSystemBackground() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:303)
void ExprParams::setNoSystemBackground(const bool &v) {
    instance()->handle->SetBool("NoSystemBackground",v);
    instance()->NoSystemBackground = v;
}

// Auto generated code (Tools/params_utils.py:310)
void ExprParams::removeNoSystemBackground() {
    instance()->handle->RemoveBool("NoSystemBackground");
}

// Auto generated code (Tools/params_utils.py:284)
const char *ExprParams::docEditorTrigger() {
    return QT_TRANSLATE_NOOP("ExprParams",
"Expression editor trigger character");
}

// Auto generated code (Tools/params_utils.py:290)
const std::string & ExprParams::getEditorTrigger() {
    return instance()->EditorTrigger;
}

// Auto generated code (Tools/params_utils.py:296)
const std::string & ExprParams::defaultEditorTrigger() {
    const static std::string def = "=";
    return def;
}

// Auto generated code (Tools/params_utils.py:303)
void ExprParams::setEditorTrigger(const std::string &v) {
    instance()->handle->SetASCII("EditorTrigger",v);
    instance()->EditorTrigger = v;
}

// Auto generated code (Tools/params_utils.py:310)
void ExprParams::removeEditorTrigger() {
    instance()->handle->RemoveASCII("EditorTrigger");
}

// Auto generated code (Tools/params_utils.py:284)
const char *ExprParams::docAutoHideEditorIcon() {
    return QT_TRANSLATE_NOOP("ExprParams",
"Only show editor icon on mouse over");
}

// Auto generated code (Tools/params_utils.py:290)
const bool & ExprParams::getAutoHideEditorIcon() {
    return instance()->AutoHideEditorIcon;
}

// Auto generated code (Tools/params_utils.py:296)
const bool & ExprParams::defaultAutoHideEditorIcon() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:303)
void ExprParams::setAutoHideEditorIcon(const bool &v) {
    instance()->handle->SetBool("AutoHideEditorIcon",v);
    instance()->AutoHideEditorIcon = v;
}

// Auto generated code (Tools/params_utils.py:310)
void ExprParams::removeAutoHideEditorIcon() {
    instance()->handle->RemoveBool("AutoHideEditorIcon");
}

// Auto generated code (Tools/params_utils.py:284)
const char *ExprParams::docAllowReturn() {
    return QT_TRANSLATE_NOOP("ExprParams",
"Allow return key in expression edit box");
}

// Auto generated code (Tools/params_utils.py:290)
const bool & ExprParams::getAllowReturn() {
    return instance()->AllowReturn;
}

// Auto generated code (Tools/params_utils.py:296)
const bool & ExprParams::defaultAllowReturn() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:303)
void ExprParams::setAllowReturn(const bool &v) {
    instance()->handle->SetBool("AllowReturn",v);
    instance()->AllowReturn = v;
}

// Auto generated code (Tools/params_utils.py:310)
void ExprParams::removeAllowReturn() {
    instance()->handle->RemoveBool("AllowReturn");
}

// Auto generated code (Tools/params_utils.py:284)
const char *ExprParams::docEvalFuncOnEdit() {
    return QT_TRANSLATE_NOOP("ExprParams",
"Auto evaluate function call when editing expression");
}

// Auto generated code (Tools/params_utils.py:290)
const bool & ExprParams::getEvalFuncOnEdit() {
    return instance()->EvalFuncOnEdit;
}

// Auto generated code (Tools/params_utils.py:296)
const bool & ExprParams::defaultEvalFuncOnEdit() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:303)
void ExprParams::setEvalFuncOnEdit(const bool &v) {
    instance()->handle->SetBool("EvalFuncOnEdit",v);
    instance()->EvalFuncOnEdit = v;
}

// Auto generated code (Tools/params_utils.py:310)
void ExprParams::removeEvalFuncOnEdit() {
    instance()->handle->RemoveBool("EvalFuncOnEdit");
}

// Auto generated code (Tools/params_utils.py:284)
const char *ExprParams::docEditDialogWidth() {
    return "";
}

// Auto generated code (Tools/params_utils.py:290)
const long & ExprParams::getEditDialogWidth() {
    return instance()->EditDialogWidth;
}

// Auto generated code (Tools/params_utils.py:296)
const long & ExprParams::defaultEditDialogWidth() {
    const static long def = 0;
    return def;
}

// Auto generated code (Tools/params_utils.py:303)
void ExprParams::setEditDialogWidth(const long &v) {
    instance()->handle->SetInt("EditDialogWidth",v);
    instance()->EditDialogWidth = v;
}

// Auto generated code (Tools/params_utils.py:310)
void ExprParams::removeEditDialogWidth() {
    instance()->handle->RemoveInt("EditDialogWidth");
}

// Auto generated code (Tools/params_utils.py:284)
const char *ExprParams::docEditDialogHeight() {
    return "";
}

// Auto generated code (Tools/params_utils.py:290)
const long & ExprParams::getEditDialogHeight() {
    return instance()->EditDialogHeight;
}

// Auto generated code (Tools/params_utils.py:296)
const long & ExprParams::defaultEditDialogHeight() {
    const static long def = 0;
    return def;
}

// Auto generated code (Tools/params_utils.py:303)
void ExprParams::setEditDialogHeight(const long &v) {
    instance()->handle->SetInt("EditDialogHeight",v);
    instance()->EditDialogHeight = v;
}

// Auto generated code (Tools/params_utils.py:310)
void ExprParams::removeEditDialogHeight() {
    instance()->handle->RemoveInt("EditDialogHeight");
}

// Auto generated code (Tools/params_utils.py:284)
const char *ExprParams::docEditDialogTextHeight() {
    return "";
}

// Auto generated code (Tools/params_utils.py:290)
const long & ExprParams::getEditDialogTextHeight() {
    return instance()->EditDialogTextHeight;
}

// Auto generated code (Tools/params_utils.py:296)
const long & ExprParams::defaultEditDialogTextHeight() {
    const static long def = 0;
    return def;
}

// Auto generated code (Tools/params_utils.py:303)
void ExprParams::setEditDialogTextHeight(const long &v) {
    instance()->handle->SetInt("EditDialogTextHeight",v);
    instance()->EditDialogTextHeight = v;
}

// Auto generated code (Tools/params_utils.py:310)
void ExprParams::removeEditDialogTextHeight() {
    instance()->handle->RemoveInt("EditDialogTextHeight");
}

// Auto generated code (Tools/params_utils.py:284)
const char *ExprParams::docEditDialogBGAlpha() {
    return QT_TRANSLATE_NOOP("ExprParams",
"Expression editor background opacity value when using in place editing");
}

// Auto generated code (Tools/params_utils.py:290)
const long & ExprParams::getEditDialogBGAlpha() {
    return instance()->EditDialogBGAlpha;
}

// Auto generated code (Tools/params_utils.py:296)
const long & ExprParams::defaultEditDialogBGAlpha() {
    const static long def = FC_EXPR_PARAM_EDIT_BG_ALPHA;
    return def;
}

// Auto generated code (Tools/params_utils.py:303)
void ExprParams::setEditDialogBGAlpha(const long &v) {
    instance()->handle->SetInt("EditDialogBGAlpha",v);
    instance()->EditDialogBGAlpha = v;
}

// Auto generated code (Tools/params_utils.py:310)
void ExprParams::removeEditDialogBGAlpha() {
    instance()->handle->RemoveInt("EditDialogBGAlpha");
}
//[[[end]]]
