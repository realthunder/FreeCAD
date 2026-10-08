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

/*[[[cog
import EditorParams
EditorParams.define()
]]]*/

// Auto generated code (Tools/params_utils.py:198)
#include <unordered_map>
#include <App/Application.h>
#include <App/DynamicProperty.h>
#include <App/ParamRegistry.h>
#include "EditorParams.h"
using namespace Gui;

// Auto generated code (Tools/params_utils.py:210)
namespace {
class EditorParamsP: public ParameterGrp::ObserverType {
public:
    ParameterGrp::handle handle;
    std::unordered_map<const char *,void(*)(EditorParamsP*),App::CStringHasher,App::CStringHasher> funcs;
    // Auto generated code (Tools/params_utils.py:228)
    fastsignals::signal<void (const char*)> signalParamChanged;
    void signalAll()
    {
        signalParamChanged("Font");
        signalParamChanged("FontSize");
        signalParamChanged("TabSize");
        signalParamChanged("IndentSize");
        signalParamChanged("Spaces");
        signalParamChanged("EnableLineNumber");
        signalParamChanged("EnableBlockCursor");
        signalParamChanged("CheckSystemExit");
        signalParamChanged("Text");
        signalParamChanged("Keyword");
        signalParamChanged("Comment");
        signalParamChanged("Block comment");
        signalParamChanged("Number");
        signalParamChanged("String");
        signalParamChanged("Class name");
        signalParamChanged("Define name");
        signalParamChanged("Operator");
        signalParamChanged("Python output");
        signalParamChanged("Python error");
        signalParamChanged("Current line highlight");
        signalParamChanged("Background");

    // Auto generated code (Tools/params_utils.py:241)
    }
    std::string Font;
    long FontSize;
    long TabSize;
    long IndentSize;
    bool Spaces;
    bool EnableLineNumber;
    bool EnableBlockCursor;
    bool CheckSystemExit;
    unsigned long Text;
    unsigned long Keyword;
    unsigned long Comment;
    unsigned long BlockComment;
    unsigned long Number;
    unsigned long String;
    unsigned long ClassName;
    unsigned long DefineName;
    unsigned long Operator;
    unsigned long PythonOutput;
    unsigned long PythonError;
    unsigned long CurrentLineHighlight;
    unsigned long Background;

    // Auto generated code (Tools/params_utils.py:254)
    EditorParamsP() {
        handle = App::GetApplication().GetParameterGroupByPath("User parameter:BaseApp/Preferences/Editor");
        handle->Attach(this);

        Font = this->handle->GetASCII("Font", "Courier");
        funcs["Font"] = &EditorParamsP::updateFont;
        FontSize = this->handle->GetInt("FontSize", 10);
        funcs["FontSize"] = &EditorParamsP::updateFontSize;
        TabSize = this->handle->GetInt("TabSize", 4);
        funcs["TabSize"] = &EditorParamsP::updateTabSize;
        IndentSize = this->handle->GetInt("IndentSize", 4);
        funcs["IndentSize"] = &EditorParamsP::updateIndentSize;
        Spaces = this->handle->GetBool("Spaces", true);
        funcs["Spaces"] = &EditorParamsP::updateSpaces;
        EnableLineNumber = this->handle->GetBool("EnableLineNumber", true);
        funcs["EnableLineNumber"] = &EditorParamsP::updateEnableLineNumber;
        EnableBlockCursor = this->handle->GetBool("EnableBlockCursor", false);
        funcs["EnableBlockCursor"] = &EditorParamsP::updateEnableBlockCursor;
        CheckSystemExit = this->handle->GetBool("CheckSystemExit", true);
        funcs["CheckSystemExit"] = &EditorParamsP::updateCheckSystemExit;
        Text = this->handle->GetUnsigned("Text", 0x00000000);
        funcs["Text"] = &EditorParamsP::updateText;
        Keyword = this->handle->GetUnsigned("Keyword", 0x0000FF00);
        funcs["Keyword"] = &EditorParamsP::updateKeyword;
        Comment = this->handle->GetUnsigned("Comment", 0x00AA0000);
        funcs["Comment"] = &EditorParamsP::updateComment;
        BlockComment = this->handle->GetUnsigned("Block comment", 0xA0A0A400);
        funcs["Block comment"] = &EditorParamsP::updateBlockComment;
        Number = this->handle->GetUnsigned("Number", 0x0000FF00);
        funcs["Number"] = &EditorParamsP::updateNumber;
        String = this->handle->GetUnsigned("String", 0xFF000000);
        funcs["String"] = &EditorParamsP::updateString;
        ClassName = this->handle->GetUnsigned("Class name", 0xFFAA0000);
        funcs["Class name"] = &EditorParamsP::updateClassName;
        DefineName = this->handle->GetUnsigned("Define name", 0xFFAA0000);
        funcs["Define name"] = &EditorParamsP::updateDefineName;
        Operator = this->handle->GetUnsigned("Operator", 0xA0A0A400);
        funcs["Operator"] = &EditorParamsP::updateOperator;
        PythonOutput = this->handle->GetUnsigned("Python output", 0xAAAA7F00);
        funcs["Python output"] = &EditorParamsP::updatePythonOutput;
        PythonError = this->handle->GetUnsigned("Python error", 0xFF000000);
        funcs["Python error"] = &EditorParamsP::updatePythonError;
        CurrentLineHighlight = this->handle->GetUnsigned("Current line highlight", 0xE0E0E000);
        funcs["Current line highlight"] = &EditorParamsP::updateCurrentLineHighlight;
        Background = this->handle->GetUnsigned("Background", 0x00000000);
        funcs["Background"] = &EditorParamsP::updateBackground;
    }

    // Auto generated code (Tools/params_utils.py:284)
    ~EditorParamsP() override = default;

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
    static void updateFont(EditorParamsP *self) {
        self->Font = self->handle->GetASCII("Font", "Courier");
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateFontSize(EditorParamsP *self) {
        self->FontSize = self->handle->GetInt("FontSize", 10);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateTabSize(EditorParamsP *self) {
        self->TabSize = self->handle->GetInt("TabSize", 4);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateIndentSize(EditorParamsP *self) {
        self->IndentSize = self->handle->GetInt("IndentSize", 4);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateSpaces(EditorParamsP *self) {
        self->Spaces = self->handle->GetBool("Spaces", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateEnableLineNumber(EditorParamsP *self) {
        self->EnableLineNumber = self->handle->GetBool("EnableLineNumber", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateEnableBlockCursor(EditorParamsP *self) {
        self->EnableBlockCursor = self->handle->GetBool("EnableBlockCursor", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateCheckSystemExit(EditorParamsP *self) {
        self->CheckSystemExit = self->handle->GetBool("CheckSystemExit", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateText(EditorParamsP *self) {
        self->Text = self->handle->GetUnsigned("Text", 0x00000000);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateKeyword(EditorParamsP *self) {
        self->Keyword = self->handle->GetUnsigned("Keyword", 0x0000FF00);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateComment(EditorParamsP *self) {
        self->Comment = self->handle->GetUnsigned("Comment", 0x00AA0000);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateBlockComment(EditorParamsP *self) {
        self->BlockComment = self->handle->GetUnsigned("Block comment", 0xA0A0A400);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateNumber(EditorParamsP *self) {
        self->Number = self->handle->GetUnsigned("Number", 0x0000FF00);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateString(EditorParamsP *self) {
        self->String = self->handle->GetUnsigned("String", 0xFF000000);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateClassName(EditorParamsP *self) {
        self->ClassName = self->handle->GetUnsigned("Class name", 0xFFAA0000);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateDefineName(EditorParamsP *self) {
        self->DefineName = self->handle->GetUnsigned("Define name", 0xFFAA0000);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateOperator(EditorParamsP *self) {
        self->Operator = self->handle->GetUnsigned("Operator", 0xA0A0A400);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updatePythonOutput(EditorParamsP *self) {
        self->PythonOutput = self->handle->GetUnsigned("Python output", 0xAAAA7F00);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updatePythonError(EditorParamsP *self) {
        self->PythonError = self->handle->GetUnsigned("Python error", 0xFF000000);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateCurrentLineHighlight(EditorParamsP *self) {
        self->CurrentLineHighlight = self->handle->GetUnsigned("Current line highlight", 0xE0E0E000);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateBackground(EditorParamsP *self) {
        self->Background = self->handle->GetUnsigned("Background", 0x00000000);
    }
};

// Auto generated code (Tools/params_utils.py:336)
EditorParamsP *instance() {
    static EditorParamsP *inst = new EditorParamsP;
    return inst;
}

} // Anonymous namespace

// Auto generated code (Tools/params_utils.py:352)
static const App::ParamRegistry::Registrar _EditorParamsRegistrar({
    App::ParamInfo("Gui", "EditorParams", "User parameter:BaseApp/Preferences/Editor", "Font", "Font", App::ParamInfo::String, "Courier")
        .setTitle("Font family")
        .setDoc("Font family of the macro and Python editors, the Python console\n"
"and the report view: Courier unless set. An empty value means the\n"
"system's fixed-pitch font. Applied at once."),
    App::ParamInfo("Gui", "EditorParams", "User parameter:BaseApp/Preferences/Editor", "FontSize", "FontSize", App::ParamInfo::Int, 10)
        .setTitle("Font size")
        .setDoc("Font size in points of the macro and Python editors, the Python\n"
"console and the report view. Applied at once."),
    App::ParamInfo("Gui", "EditorParams", "User parameter:BaseApp/Preferences/Editor", "TabSize", "TabSize", App::ParamInfo::Int, 4)
        .setTitle("Tab size")
        .setDoc("Width of a tab stop in the macro and Python editors, in\n"
"characters of the editor font. Applied at once."),
    App::ParamInfo("Gui", "EditorParams", "User parameter:BaseApp/Preferences/Editor", "IndentSize", "IndentSize", App::ParamInfo::Int, 4)
        .setTitle("Indent size")
        .setDoc("Number of spaces one step of indentation is in the macro and\n"
"Python editors, when indentation is done with spaces."),
    App::ParamInfo("Gui", "EditorParams", "User parameter:BaseApp/Preferences/Editor", "Spaces", "Spaces", App::ParamInfo::Bool, true)
        .setTitle("Insert spaces")
        .setDoc("Indent with spaces in the macro and Python editors: the Tab key\n"
"and the automatic indentation after Enter insert IndentSize\n"
"spaces. When off they insert a tab character."),
    App::ParamInfo("Gui", "EditorParams", "User parameter:BaseApp/Preferences/Editor", "EnableLineNumber", "EnableLineNumber", App::ParamInfo::Bool, true)
        .setTitle("Enable line numbers")
        .setDoc("Show line numbers in the left margin of the macro and Python\n"
"editors. Applied at once."),
    App::ParamInfo("Gui", "EditorParams", "User parameter:BaseApp/Preferences/Editor", "EnableBlockCursor", "EnableBlockCursor", App::ParamInfo::Bool, false)
        .setTitle("Enable block cursor")
        .setDoc("Draw the text cursor of the macro and Python editors as a block\n"
"one character wide instead of a line. Applied at once."),
    App::ParamInfo("Gui", "EditorParams", "User parameter:BaseApp/Preferences/Editor", "CheckSystemExit", "CheckSystemExit", App::ParamInfo::Bool, true)
        .setTitle("Ask before exiting from the console")
        .setDoc("When code run in the Python console raises SystemExit, ask before\n"
"the program is closed. When off it closes at once."),
    App::ParamInfo("Gui", "EditorParams", "User parameter:BaseApp/Preferences/Editor", "Text", "Text", App::ParamInfo::Hex, 0x00000000)
        .setTitle("Text colour")
        .setDoc("Colour of plain text in the editors and the Python console. Not\n"
"set means the window's text colour, which follows the theme.")
        .setProxy("Color")
        .setTransparency(false),
    App::ParamInfo("Gui", "EditorParams", "User parameter:BaseApp/Preferences/Editor", "Keyword", "Keyword", App::ParamInfo::Hex, 0x0000FF00)
        .setTitle("Keyword colour")
        .setDoc("Colour of Python keywords in the editors and the Python console.")
        .setProxy("Color")
        .setTransparency(false),
    App::ParamInfo("Gui", "EditorParams", "User parameter:BaseApp/Preferences/Editor", "Comment", "Comment", App::ParamInfo::Hex, 0x00AA0000)
        .setTitle("Comment colour")
        .setDoc("Colour of comments in the editors and the Python console.")
        .setProxy("Color")
        .setTransparency(false),
    App::ParamInfo("Gui", "EditorParams", "User parameter:BaseApp/Preferences/Editor", "BlockComment", "Block comment", App::ParamInfo::Hex, 0xA0A0A400)
        .setTitle("Block comment colour")
        .setDoc("Colour of triple-quoted text in the editors and the Python\n"
"console.")
        .setProxy("Color")
        .setTransparency(false),
    App::ParamInfo("Gui", "EditorParams", "User parameter:BaseApp/Preferences/Editor", "Number", "Number", App::ParamInfo::Hex, 0x0000FF00)
        .setTitle("Number colour")
        .setDoc("Colour of numbers in the editors and the Python console.")
        .setProxy("Color")
        .setTransparency(false),
    App::ParamInfo("Gui", "EditorParams", "User parameter:BaseApp/Preferences/Editor", "String", "String", App::ParamInfo::Hex, 0xFF000000)
        .setTitle("String colour")
        .setDoc("Colour of quoted text in the editors and the Python console.")
        .setProxy("Color")
        .setTransparency(false),
    App::ParamInfo("Gui", "EditorParams", "User parameter:BaseApp/Preferences/Editor", "ClassName", "Class name", App::ParamInfo::Hex, 0xFFAA0000)
        .setTitle("Class name colour")
        .setDoc("Colour of the name after 'class' in the editors and the Python\n"
"console.")
        .setProxy("Color")
        .setTransparency(false),
    App::ParamInfo("Gui", "EditorParams", "User parameter:BaseApp/Preferences/Editor", "DefineName", "Define name", App::ParamInfo::Hex, 0xFFAA0000)
        .setTitle("Define name colour")
        .setDoc("Colour of the name after 'def' in the editors and the Python\n"
"console.")
        .setProxy("Color")
        .setTransparency(false),
    App::ParamInfo("Gui", "EditorParams", "User parameter:BaseApp/Preferences/Editor", "Operator", "Operator", App::ParamInfo::Hex, 0xA0A0A400)
        .setTitle("Operator colour")
        .setDoc("Colour of operators and brackets in the editors and the Python\n"
"console.")
        .setProxy("Color")
        .setTransparency(false),
    App::ParamInfo("Gui", "EditorParams", "User parameter:BaseApp/Preferences/Editor", "PythonOutput", "Python output", App::ParamInfo::Hex, 0xAAAA7F00)
        .setTitle("Python output colour")
        .setDoc("Colour of what Python prints in the Python console.")
        .setProxy("Color")
        .setTransparency(false),
    App::ParamInfo("Gui", "EditorParams", "User parameter:BaseApp/Preferences/Editor", "PythonError", "Python error", App::ParamInfo::Hex, 0xFF000000)
        .setTitle("Python error colour")
        .setDoc("Colour of Python's error messages in the Python console.")
        .setProxy("Color")
        .setTransparency(false),
    App::ParamInfo("Gui", "EditorParams", "User parameter:BaseApp/Preferences/Editor", "CurrentLineHighlight", "Current line highlight", App::ParamInfo::Hex, 0xE0E0E000)
        .setTitle("Current line highlight")
        .setDoc("Background colour of the line the cursor is on in the macro and\n"
"Python editors.")
        .setProxy("Color")
        .setTransparency(false),
    App::ParamInfo("Gui", "EditorParams", "User parameter:BaseApp/Preferences/Editor", "Background", "Background", App::ParamInfo::Hex, 0x00000000)
        .setTitle("Python console background")
        .setDoc("Background colour of the Python console. 0, or not set, leaves\n"
"the background to the theme.")
        .setProxy("Color")
        .setTransparency(false),
});

// Auto generated code (Tools/params_utils.py:368)
ParameterGrp::handle EditorParams::getHandle() {
    return instance()->handle;
}

// Auto generated code (Tools/params_utils.py:378)
fastsignals::signal<void (const char*)> &
EditorParams::signalParamChanged() {
    return instance()->signalParamChanged;
}

// Auto generated code (Tools/params_utils.py:387)
void EditorParams::signalAll() {
    instance()->signalAll();
}

// Auto generated code (Tools/params_utils.py:397)
const char *EditorParams::docFont() {
    return QT_TRANSLATE_NOOP("EditorParams",
"Font family of the macro and Python editors, the Python console\n"
"and the report view: Courier unless set. An empty value means the\n"
"system's fixed-pitch font. Applied at once.");
}

// Auto generated code (Tools/params_utils.py:405)
const std::string & EditorParams::getFont() {
    return instance()->Font;
}

// Auto generated code (Tools/params_utils.py:413)
const std::string & EditorParams::defaultFont() {
    const static std::string def = "Courier";
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void EditorParams::setFont(const std::string &v) {
    instance()->handle->SetASCII("Font",v);
    instance()->Font = v;
}

// Auto generated code (Tools/params_utils.py:431)
void EditorParams::removeFont() {
    instance()->handle->RemoveASCII("Font");
}

// Auto generated code (Tools/params_utils.py:397)
const char *EditorParams::docFontSize() {
    return QT_TRANSLATE_NOOP("EditorParams",
"Font size in points of the macro and Python editors, the Python\n"
"console and the report view. Applied at once.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & EditorParams::getFontSize() {
    return instance()->FontSize;
}

// Auto generated code (Tools/params_utils.py:413)
const long & EditorParams::defaultFontSize() {
    const static long def = 10;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void EditorParams::setFontSize(const long &v) {
    instance()->handle->SetInt("FontSize",v);
    instance()->FontSize = v;
}

// Auto generated code (Tools/params_utils.py:431)
void EditorParams::removeFontSize() {
    instance()->handle->RemoveInt("FontSize");
}

// Auto generated code (Tools/params_utils.py:397)
const char *EditorParams::docTabSize() {
    return QT_TRANSLATE_NOOP("EditorParams",
"Width of a tab stop in the macro and Python editors, in\n"
"characters of the editor font. Applied at once.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & EditorParams::getTabSize() {
    return instance()->TabSize;
}

// Auto generated code (Tools/params_utils.py:413)
const long & EditorParams::defaultTabSize() {
    const static long def = 4;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void EditorParams::setTabSize(const long &v) {
    instance()->handle->SetInt("TabSize",v);
    instance()->TabSize = v;
}

// Auto generated code (Tools/params_utils.py:431)
void EditorParams::removeTabSize() {
    instance()->handle->RemoveInt("TabSize");
}

// Auto generated code (Tools/params_utils.py:397)
const char *EditorParams::docIndentSize() {
    return QT_TRANSLATE_NOOP("EditorParams",
"Number of spaces one step of indentation is in the macro and\n"
"Python editors, when indentation is done with spaces.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & EditorParams::getIndentSize() {
    return instance()->IndentSize;
}

// Auto generated code (Tools/params_utils.py:413)
const long & EditorParams::defaultIndentSize() {
    const static long def = 4;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void EditorParams::setIndentSize(const long &v) {
    instance()->handle->SetInt("IndentSize",v);
    instance()->IndentSize = v;
}

// Auto generated code (Tools/params_utils.py:431)
void EditorParams::removeIndentSize() {
    instance()->handle->RemoveInt("IndentSize");
}

// Auto generated code (Tools/params_utils.py:397)
const char *EditorParams::docSpaces() {
    return QT_TRANSLATE_NOOP("EditorParams",
"Indent with spaces in the macro and Python editors: the Tab key\n"
"and the automatic indentation after Enter insert IndentSize\n"
"spaces. When off they insert a tab character.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & EditorParams::getSpaces() {
    return instance()->Spaces;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & EditorParams::defaultSpaces() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void EditorParams::setSpaces(const bool &v) {
    instance()->handle->SetBool("Spaces",v);
    instance()->Spaces = v;
}

// Auto generated code (Tools/params_utils.py:431)
void EditorParams::removeSpaces() {
    instance()->handle->RemoveBool("Spaces");
}

// Auto generated code (Tools/params_utils.py:397)
const char *EditorParams::docEnableLineNumber() {
    return QT_TRANSLATE_NOOP("EditorParams",
"Show line numbers in the left margin of the macro and Python\n"
"editors. Applied at once.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & EditorParams::getEnableLineNumber() {
    return instance()->EnableLineNumber;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & EditorParams::defaultEnableLineNumber() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void EditorParams::setEnableLineNumber(const bool &v) {
    instance()->handle->SetBool("EnableLineNumber",v);
    instance()->EnableLineNumber = v;
}

// Auto generated code (Tools/params_utils.py:431)
void EditorParams::removeEnableLineNumber() {
    instance()->handle->RemoveBool("EnableLineNumber");
}

// Auto generated code (Tools/params_utils.py:397)
const char *EditorParams::docEnableBlockCursor() {
    return QT_TRANSLATE_NOOP("EditorParams",
"Draw the text cursor of the macro and Python editors as a block\n"
"one character wide instead of a line. Applied at once.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & EditorParams::getEnableBlockCursor() {
    return instance()->EnableBlockCursor;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & EditorParams::defaultEnableBlockCursor() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void EditorParams::setEnableBlockCursor(const bool &v) {
    instance()->handle->SetBool("EnableBlockCursor",v);
    instance()->EnableBlockCursor = v;
}

// Auto generated code (Tools/params_utils.py:431)
void EditorParams::removeEnableBlockCursor() {
    instance()->handle->RemoveBool("EnableBlockCursor");
}

// Auto generated code (Tools/params_utils.py:397)
const char *EditorParams::docCheckSystemExit() {
    return QT_TRANSLATE_NOOP("EditorParams",
"When code run in the Python console raises SystemExit, ask before\n"
"the program is closed. When off it closes at once.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & EditorParams::getCheckSystemExit() {
    return instance()->CheckSystemExit;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & EditorParams::defaultCheckSystemExit() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void EditorParams::setCheckSystemExit(const bool &v) {
    instance()->handle->SetBool("CheckSystemExit",v);
    instance()->CheckSystemExit = v;
}

// Auto generated code (Tools/params_utils.py:431)
void EditorParams::removeCheckSystemExit() {
    instance()->handle->RemoveBool("CheckSystemExit");
}

// Auto generated code (Tools/params_utils.py:397)
const char *EditorParams::docText() {
    return QT_TRANSLATE_NOOP("EditorParams",
"Colour of plain text in the editors and the Python console. Not\n"
"set means the window's text colour, which follows the theme.");
}

// Auto generated code (Tools/params_utils.py:405)
const unsigned long & EditorParams::getText() {
    return instance()->Text;
}

// Auto generated code (Tools/params_utils.py:413)
const unsigned long & EditorParams::defaultText() {
    const static unsigned long def = 0x00000000;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void EditorParams::setText(const unsigned long &v) {
    instance()->handle->SetUnsigned("Text",v);
    instance()->Text = v;
}

// Auto generated code (Tools/params_utils.py:431)
void EditorParams::removeText() {
    instance()->handle->RemoveUnsigned("Text");
}

// Auto generated code (Tools/params_utils.py:397)
const char *EditorParams::docKeyword() {
    return QT_TRANSLATE_NOOP("EditorParams",
"Colour of Python keywords in the editors and the Python console.");
}

// Auto generated code (Tools/params_utils.py:405)
const unsigned long & EditorParams::getKeyword() {
    return instance()->Keyword;
}

// Auto generated code (Tools/params_utils.py:413)
const unsigned long & EditorParams::defaultKeyword() {
    const static unsigned long def = 0x0000FF00;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void EditorParams::setKeyword(const unsigned long &v) {
    instance()->handle->SetUnsigned("Keyword",v);
    instance()->Keyword = v;
}

// Auto generated code (Tools/params_utils.py:431)
void EditorParams::removeKeyword() {
    instance()->handle->RemoveUnsigned("Keyword");
}

// Auto generated code (Tools/params_utils.py:397)
const char *EditorParams::docComment() {
    return QT_TRANSLATE_NOOP("EditorParams",
"Colour of comments in the editors and the Python console.");
}

// Auto generated code (Tools/params_utils.py:405)
const unsigned long & EditorParams::getComment() {
    return instance()->Comment;
}

// Auto generated code (Tools/params_utils.py:413)
const unsigned long & EditorParams::defaultComment() {
    const static unsigned long def = 0x00AA0000;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void EditorParams::setComment(const unsigned long &v) {
    instance()->handle->SetUnsigned("Comment",v);
    instance()->Comment = v;
}

// Auto generated code (Tools/params_utils.py:431)
void EditorParams::removeComment() {
    instance()->handle->RemoveUnsigned("Comment");
}

// Auto generated code (Tools/params_utils.py:397)
const char *EditorParams::docBlockComment() {
    return QT_TRANSLATE_NOOP("EditorParams",
"Colour of triple-quoted text in the editors and the Python\n"
"console.");
}

// Auto generated code (Tools/params_utils.py:405)
const unsigned long & EditorParams::getBlockComment() {
    return instance()->BlockComment;
}

// Auto generated code (Tools/params_utils.py:413)
const unsigned long & EditorParams::defaultBlockComment() {
    const static unsigned long def = 0xA0A0A400;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void EditorParams::setBlockComment(const unsigned long &v) {
    instance()->handle->SetUnsigned("Block comment",v);
    instance()->BlockComment = v;
}

// Auto generated code (Tools/params_utils.py:431)
void EditorParams::removeBlockComment() {
    instance()->handle->RemoveUnsigned("Block comment");
}

// Auto generated code (Tools/params_utils.py:397)
const char *EditorParams::docNumber() {
    return QT_TRANSLATE_NOOP("EditorParams",
"Colour of numbers in the editors and the Python console.");
}

// Auto generated code (Tools/params_utils.py:405)
const unsigned long & EditorParams::getNumber() {
    return instance()->Number;
}

// Auto generated code (Tools/params_utils.py:413)
const unsigned long & EditorParams::defaultNumber() {
    const static unsigned long def = 0x0000FF00;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void EditorParams::setNumber(const unsigned long &v) {
    instance()->handle->SetUnsigned("Number",v);
    instance()->Number = v;
}

// Auto generated code (Tools/params_utils.py:431)
void EditorParams::removeNumber() {
    instance()->handle->RemoveUnsigned("Number");
}

// Auto generated code (Tools/params_utils.py:397)
const char *EditorParams::docString() {
    return QT_TRANSLATE_NOOP("EditorParams",
"Colour of quoted text in the editors and the Python console.");
}

// Auto generated code (Tools/params_utils.py:405)
const unsigned long & EditorParams::getString() {
    return instance()->String;
}

// Auto generated code (Tools/params_utils.py:413)
const unsigned long & EditorParams::defaultString() {
    const static unsigned long def = 0xFF000000;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void EditorParams::setString(const unsigned long &v) {
    instance()->handle->SetUnsigned("String",v);
    instance()->String = v;
}

// Auto generated code (Tools/params_utils.py:431)
void EditorParams::removeString() {
    instance()->handle->RemoveUnsigned("String");
}

// Auto generated code (Tools/params_utils.py:397)
const char *EditorParams::docClassName() {
    return QT_TRANSLATE_NOOP("EditorParams",
"Colour of the name after 'class' in the editors and the Python\n"
"console.");
}

// Auto generated code (Tools/params_utils.py:405)
const unsigned long & EditorParams::getClassName() {
    return instance()->ClassName;
}

// Auto generated code (Tools/params_utils.py:413)
const unsigned long & EditorParams::defaultClassName() {
    const static unsigned long def = 0xFFAA0000;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void EditorParams::setClassName(const unsigned long &v) {
    instance()->handle->SetUnsigned("Class name",v);
    instance()->ClassName = v;
}

// Auto generated code (Tools/params_utils.py:431)
void EditorParams::removeClassName() {
    instance()->handle->RemoveUnsigned("Class name");
}

// Auto generated code (Tools/params_utils.py:397)
const char *EditorParams::docDefineName() {
    return QT_TRANSLATE_NOOP("EditorParams",
"Colour of the name after 'def' in the editors and the Python\n"
"console.");
}

// Auto generated code (Tools/params_utils.py:405)
const unsigned long & EditorParams::getDefineName() {
    return instance()->DefineName;
}

// Auto generated code (Tools/params_utils.py:413)
const unsigned long & EditorParams::defaultDefineName() {
    const static unsigned long def = 0xFFAA0000;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void EditorParams::setDefineName(const unsigned long &v) {
    instance()->handle->SetUnsigned("Define name",v);
    instance()->DefineName = v;
}

// Auto generated code (Tools/params_utils.py:431)
void EditorParams::removeDefineName() {
    instance()->handle->RemoveUnsigned("Define name");
}

// Auto generated code (Tools/params_utils.py:397)
const char *EditorParams::docOperator() {
    return QT_TRANSLATE_NOOP("EditorParams",
"Colour of operators and brackets in the editors and the Python\n"
"console.");
}

// Auto generated code (Tools/params_utils.py:405)
const unsigned long & EditorParams::getOperator() {
    return instance()->Operator;
}

// Auto generated code (Tools/params_utils.py:413)
const unsigned long & EditorParams::defaultOperator() {
    const static unsigned long def = 0xA0A0A400;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void EditorParams::setOperator(const unsigned long &v) {
    instance()->handle->SetUnsigned("Operator",v);
    instance()->Operator = v;
}

// Auto generated code (Tools/params_utils.py:431)
void EditorParams::removeOperator() {
    instance()->handle->RemoveUnsigned("Operator");
}

// Auto generated code (Tools/params_utils.py:397)
const char *EditorParams::docPythonOutput() {
    return QT_TRANSLATE_NOOP("EditorParams",
"Colour of what Python prints in the Python console.");
}

// Auto generated code (Tools/params_utils.py:405)
const unsigned long & EditorParams::getPythonOutput() {
    return instance()->PythonOutput;
}

// Auto generated code (Tools/params_utils.py:413)
const unsigned long & EditorParams::defaultPythonOutput() {
    const static unsigned long def = 0xAAAA7F00;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void EditorParams::setPythonOutput(const unsigned long &v) {
    instance()->handle->SetUnsigned("Python output",v);
    instance()->PythonOutput = v;
}

// Auto generated code (Tools/params_utils.py:431)
void EditorParams::removePythonOutput() {
    instance()->handle->RemoveUnsigned("Python output");
}

// Auto generated code (Tools/params_utils.py:397)
const char *EditorParams::docPythonError() {
    return QT_TRANSLATE_NOOP("EditorParams",
"Colour of Python's error messages in the Python console.");
}

// Auto generated code (Tools/params_utils.py:405)
const unsigned long & EditorParams::getPythonError() {
    return instance()->PythonError;
}

// Auto generated code (Tools/params_utils.py:413)
const unsigned long & EditorParams::defaultPythonError() {
    const static unsigned long def = 0xFF000000;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void EditorParams::setPythonError(const unsigned long &v) {
    instance()->handle->SetUnsigned("Python error",v);
    instance()->PythonError = v;
}

// Auto generated code (Tools/params_utils.py:431)
void EditorParams::removePythonError() {
    instance()->handle->RemoveUnsigned("Python error");
}

// Auto generated code (Tools/params_utils.py:397)
const char *EditorParams::docCurrentLineHighlight() {
    return QT_TRANSLATE_NOOP("EditorParams",
"Background colour of the line the cursor is on in the macro and\n"
"Python editors.");
}

// Auto generated code (Tools/params_utils.py:405)
const unsigned long & EditorParams::getCurrentLineHighlight() {
    return instance()->CurrentLineHighlight;
}

// Auto generated code (Tools/params_utils.py:413)
const unsigned long & EditorParams::defaultCurrentLineHighlight() {
    const static unsigned long def = 0xE0E0E000;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void EditorParams::setCurrentLineHighlight(const unsigned long &v) {
    instance()->handle->SetUnsigned("Current line highlight",v);
    instance()->CurrentLineHighlight = v;
}

// Auto generated code (Tools/params_utils.py:431)
void EditorParams::removeCurrentLineHighlight() {
    instance()->handle->RemoveUnsigned("Current line highlight");
}

// Auto generated code (Tools/params_utils.py:397)
const char *EditorParams::docBackground() {
    return QT_TRANSLATE_NOOP("EditorParams",
"Background colour of the Python console. 0, or not set, leaves\n"
"the background to the theme.");
}

// Auto generated code (Tools/params_utils.py:405)
const unsigned long & EditorParams::getBackground() {
    return instance()->Background;
}

// Auto generated code (Tools/params_utils.py:413)
const unsigned long & EditorParams::defaultBackground() {
    const static unsigned long def = 0x00000000;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void EditorParams::setBackground(const unsigned long &v) {
    instance()->handle->SetUnsigned("Background",v);
    instance()->Background = v;
}

// Auto generated code (Tools/params_utils.py:431)
void EditorParams::removeBackground() {
    instance()->handle->RemoveUnsigned("Background");
}
//[[[end]]]

#include <QApplication>
#include <QPalette>

std::vector<std::pair<QString, QColor>> Gui::editorColorDefaults()
{
    // a colour is stored as 0xRRGGBB00
    auto color = [](unsigned long packed) {
        return QColor(static_cast<int>((packed >> 24) & 0xff),
                      static_cast<int>((packed >> 16) & 0xff),
                      static_cast<int>((packed >> 8) & 0xff));
    };
    return {
        {QStringLiteral("Text"), qApp->palette().windowText().color()},
        {QStringLiteral("Keyword"), color(EditorParams::defaultKeyword())},
        {QStringLiteral("Comment"), color(EditorParams::defaultComment())},
        {QStringLiteral("Block comment"), color(EditorParams::defaultBlockComment())},
        {QStringLiteral("Number"), color(EditorParams::defaultNumber())},
        {QStringLiteral("String"), color(EditorParams::defaultString())},
        {QStringLiteral("Class name"), color(EditorParams::defaultClassName())},
        {QStringLiteral("Define name"), color(EditorParams::defaultDefineName())},
        {QStringLiteral("Operator"), color(EditorParams::defaultOperator())},
        {QStringLiteral("Python output"), color(EditorParams::defaultPythonOutput())},
        {QStringLiteral("Python error"), color(EditorParams::defaultPythonError())},
        {QStringLiteral("Current line highlight"), color(EditorParams::defaultCurrentLineHighlight())},
        {QStringLiteral("Background"), color(EditorParams::defaultBackground())},
    };
}

#include <QFontDatabase>

QFont Gui::editorFont(const std::string& family, int pointSize)
{
    if (family.empty()) {
        QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
        font.setPointSize(pointSize);
        return font;
    }
    return QFont(QString::fromUtf8(family.c_str()), pointSize);
}
