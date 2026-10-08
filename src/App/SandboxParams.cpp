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
import SandboxParams
SandboxParams.define()
]]]*/

// Auto generated code (Tools/params_utils.py:198)
#include <unordered_map>
#include <App/Application.h>
#include <App/DynamicProperty.h>
#include <App/ParamRegistry.h>
#include "SandboxParams.h"
using namespace App;

// Auto generated code (Tools/params_utils.py:210)
namespace {
class SandboxParamsP: public ParameterGrp::ObserverType {
public:
    ParameterGrp::handle handle;
    std::unordered_map<const char *,void(*)(SandboxParamsP*),App::CStringHasher,App::CStringHasher> funcs;
    std::vector<ParameterGrp::handle> subHandles;
    bool Evaluate;
    std::string Runtime;
    long BudgetMs;
    long GraceMs;
    long MemoryMB;
    long EngineHeapMB;
    std::string ImagePath;
    std::string StdlibPath;
    std::string PyodideDir;
    std::string PyodideWheel;
    std::string PyodideUserDir;
    std::string PyodidePackages;
    bool PyodideUnpinned;
    bool Enforce;

    // Auto generated code (Tools/params_utils.py:254)
    SandboxParamsP() {
        handle = App::GetApplication().GetParameterGroupByPath("User parameter:BaseApp/Preferences/Expression");
        handle->Attach(this);

        subHandles.resize(2);
        subHandles[0] = handle->GetGroup("Sandbox");
        subHandles[0]->Attach(this);
        subHandles[1] = handle->GetGroup("Security");
        subHandles[1]->Attach(this);
        Evaluate = this->subHandles[0]->GetBool("Evaluate", true);
        funcs["Evaluate"] = &SandboxParamsP::updateEvaluate;
        Runtime = this->subHandles[0]->GetASCII("Runtime", "");
        funcs["Runtime"] = &SandboxParamsP::updateRuntime;
        BudgetMs = this->subHandles[0]->GetInt("BudgetMs", 5000);
        funcs["BudgetMs"] = &SandboxParamsP::updateBudgetMs;
        GraceMs = this->subHandles[0]->GetInt("GraceMs", 1000);
        funcs["GraceMs"] = &SandboxParamsP::updateGraceMs;
        MemoryMB = this->subHandles[0]->GetInt("MemoryMB", 1024);
        funcs["MemoryMB"] = &SandboxParamsP::updateMemoryMB;
        EngineHeapMB = this->subHandles[0]->GetInt("EngineHeapMB", 512);
        funcs["EngineHeapMB"] = &SandboxParamsP::updateEngineHeapMB;
        ImagePath = this->subHandles[0]->GetASCII("ImagePath", "");
        funcs["ImagePath"] = &SandboxParamsP::updateImagePath;
        StdlibPath = this->subHandles[0]->GetASCII("StdlibPath", "");
        funcs["StdlibPath"] = &SandboxParamsP::updateStdlibPath;
        PyodideDir = this->subHandles[0]->GetASCII("PyodideDir", "");
        funcs["PyodideDir"] = &SandboxParamsP::updatePyodideDir;
        PyodideWheel = this->subHandles[0]->GetASCII("PyodideWheel", "");
        funcs["PyodideWheel"] = &SandboxParamsP::updatePyodideWheel;
        PyodideUserDir = this->subHandles[0]->GetASCII("PyodideUserDir", "");
        funcs["PyodideUserDir"] = &SandboxParamsP::updatePyodideUserDir;
        PyodidePackages = this->subHandles[0]->GetASCII("PyodidePackages", "");
        funcs["PyodidePackages"] = &SandboxParamsP::updatePyodidePackages;
        PyodideUnpinned = this->subHandles[0]->GetBool("PyodideUnpinned", false);
        funcs["PyodideUnpinned"] = &SandboxParamsP::updatePyodideUnpinned;
        Enforce = this->subHandles[1]->GetBool("Enforce", true);
        funcs["Enforce"] = &SandboxParamsP::updateEnforce;
    }

    // Auto generated code (Tools/params_utils.py:284)
    ~SandboxParamsP() override = default;

    // Auto generated code (Tools/params_utils.py:297)
    void OnChange(Base::Subject<const char*> &, const char* sReason) override {
        if(!sReason)
            return;
        auto it = funcs.find(sReason);
        if(it == funcs.end())
            return;
        it->second(this);
    }


    // Auto generated code (Tools/params_utils.py:314)
    static void updateEvaluate(SandboxParamsP *self) {
        self->Evaluate = self->subHandles[0]->GetBool("Evaluate", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateRuntime(SandboxParamsP *self) {
        self->Runtime = self->subHandles[0]->GetASCII("Runtime", "");
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateBudgetMs(SandboxParamsP *self) {
        self->BudgetMs = self->subHandles[0]->GetInt("BudgetMs", 5000);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateGraceMs(SandboxParamsP *self) {
        self->GraceMs = self->subHandles[0]->GetInt("GraceMs", 1000);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateMemoryMB(SandboxParamsP *self) {
        self->MemoryMB = self->subHandles[0]->GetInt("MemoryMB", 1024);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateEngineHeapMB(SandboxParamsP *self) {
        self->EngineHeapMB = self->subHandles[0]->GetInt("EngineHeapMB", 512);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateImagePath(SandboxParamsP *self) {
        self->ImagePath = self->subHandles[0]->GetASCII("ImagePath", "");
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateStdlibPath(SandboxParamsP *self) {
        self->StdlibPath = self->subHandles[0]->GetASCII("StdlibPath", "");
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updatePyodideDir(SandboxParamsP *self) {
        self->PyodideDir = self->subHandles[0]->GetASCII("PyodideDir", "");
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updatePyodideWheel(SandboxParamsP *self) {
        self->PyodideWheel = self->subHandles[0]->GetASCII("PyodideWheel", "");
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updatePyodideUserDir(SandboxParamsP *self) {
        self->PyodideUserDir = self->subHandles[0]->GetASCII("PyodideUserDir", "");
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updatePyodidePackages(SandboxParamsP *self) {
        self->PyodidePackages = self->subHandles[0]->GetASCII("PyodidePackages", "");
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updatePyodideUnpinned(SandboxParamsP *self) {
        self->PyodideUnpinned = self->subHandles[0]->GetBool("PyodideUnpinned", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateEnforce(SandboxParamsP *self) {
        self->Enforce = self->subHandles[1]->GetBool("Enforce", true);
    }
};

// Auto generated code (Tools/params_utils.py:336)
SandboxParamsP *instance() {
    static SandboxParamsP *inst = new SandboxParamsP;
    return inst;
}

} // Anonymous namespace

// Auto generated code (Tools/params_utils.py:352)
static const App::ParamRegistry::Registrar _SandboxParamsRegistrar({
    App::ParamInfo("App", "SandboxParams", "User parameter:BaseApp/Preferences/Expression/Sandbox", "Evaluate", "Evaluate", App::ParamInfo::Bool, true)
        .setTitle("Evaluate expressions in the sandbox")
        .setDoc("Expressions that call Python, and the Python objects of a document,\n"
"are evaluated in the sandbox instead of the program's own\n"
"interpreter, when the sandbox is there. The sandbox indicator of\n"
"the status bar switches it. Takes effect at once."),
    App::ParamInfo("App", "SandboxParams", "User parameter:BaseApp/Preferences/Expression/Sandbox", "Runtime", "Runtime", App::ParamInfo::String, "")
        .setTitle("Sandbox runtime")
        .setDoc("Which runtime carries the sandbox: \"pyodide\" or \"wasi\". Empty, the\n"
"environment variable FCX_RUNTIME decides, and then the build:\n"
"pyodide where it has it. Read when the sandbox starts."),
    App::ParamInfo("App", "SandboxParams", "User parameter:BaseApp/Preferences/Expression/Sandbox", "BudgetMs", "BudgetMs", App::ParamInfo::Int, 5000)
        .setTitle("Sandbox time budget (ms)")
        .setDoc("Milliseconds one evaluation in the sandbox may take before it is\n"
"interrupted. Takes effect at the next evaluation."),
    App::ParamInfo("App", "SandboxParams", "User parameter:BaseApp/Preferences/Expression/Sandbox", "GraceMs", "GraceMs", App::ParamInfo::Int, 1000)
        .setTitle("Sandbox grace time (ms)")
        .setDoc("Milliseconds an interrupted evaluation is given to stop by itself\n"
"after its time budget, before the sandbox is stopped. Takes effect\n"
"at the next evaluation."),
    App::ParamInfo("App", "SandboxParams", "User parameter:BaseApp/Preferences/Expression/Sandbox", "MemoryMB", "MemoryMB", App::ParamInfo::Int, 1024)
        .setTitle("Sandbox memory budget (MB)")
        .setDoc("Megabytes of memory the sandbox may use; an evaluation that takes\n"
"it past that stops the sandbox. Takes effect the next time the\n"
"sandbox's memory grows."),
    App::ParamInfo("App", "SandboxParams", "User parameter:BaseApp/Preferences/Expression/Sandbox", "EngineHeapMB", "EngineHeapMB", App::ParamInfo::Int, 512)
        .setTitle("Sandbox engine heap (MB)")
        .setDoc("Megabytes of heap the engine that runs the sandbox may use for\n"
"itself; 0 leaves it to the engine. Takes effect when the sandbox\n"
"next starts."),
    App::ParamInfo("App", "SandboxParams", "User parameter:BaseApp/Preferences/Expression/Sandbox", "ImagePath", "ImagePath", App::ParamInfo::String, "")
        .setTitle("Sandbox image (wasi)")
        .setDoc("Path of the sandbox image the wasi runtime loads. Empty, the\n"
"environment variable FCX_IMAGE is used, and then the image that\n"
"comes with the program. Read when the sandbox starts."),
    App::ParamInfo("App", "SandboxParams", "User parameter:BaseApp/Preferences/Expression/Sandbox", "StdlibPath", "StdlibPath", App::ParamInfo::String, "")
        .setTitle("Sandbox standard library (wasi)")
        .setDoc("Folder of the Python standard library of the wasi image. Empty,\n"
"the environment variable FCX_STDLIB is used, and then the one that\n"
"comes with the program. Read when the sandbox starts."),
    App::ParamInfo("App", "SandboxParams", "User parameter:BaseApp/Preferences/Expression/Sandbox", "PyodideDir", "PyodideDir", App::ParamInfo::String, "")
        .setTitle("Pyodide runtime folder")
        .setDoc("Folder of the Pyodide runtime the sandbox boots. Empty, the\n"
"environment variable FCX_PYODIDE is used, and then what is\n"
"installed for the user or comes with the program. Read when the\n"
"sandbox starts."),
    App::ParamInfo("App", "SandboxParams", "User parameter:BaseApp/Preferences/Expression/Sandbox", "PyodideWheel", "PyodideWheel", App::ParamInfo::String, "")
        .setTitle("Sandbox wheel (Pyodide)")
        .setDoc("Path of the wheel that holds the sandbox's own Python package.\n"
"Empty, the environment variable FCX_PYODIDE_WHEEL is used, and then\n"
"the one that comes with the runtime. Read when the sandbox starts."),
    App::ParamInfo("App", "SandboxParams", "User parameter:BaseApp/Preferences/Expression/Sandbox", "PyodideUserDir", "PyodideUserDir", App::ParamInfo::String, "")
        .setTitle("Pyodide user folder")
        .setDoc("Folder the sandbox keeps what is installed for the user in. Empty,\n"
"the environment variable FCX_PYODIDE_USER is used, and then the\n"
"folder Pyodide of the user's application data."),
    App::ParamInfo("App", "SandboxParams", "User parameter:BaseApp/Preferences/Expression/Sandbox", "PyodidePackages", "PyodidePackages", App::ParamInfo::String, "")
        .setTitle("Pyodide packages folder")
        .setDoc("Folder of the packages installed into the sandbox. Empty, the\n"
"environment variable FCX_PYODIDE_PACKAGES is used, and then the\n"
"folder packages of the Pyodide user folder."),
    App::ParamInfo("App", "SandboxParams", "User parameter:BaseApp/Preferences/Expression/Sandbox", "PyodideUnpinned", "PyodideUnpinned", App::ParamInfo::Bool, false)
        .setTitle("Accept an unpinned Pyodide release")
        .setDoc("For development only: boots a Pyodide release that is not in the\n"
"table of releases the program was pinned to, or whose files differ\n"
"from it, and says so at every boot. The environment variable\n"
"FCX_PYODIDE_UNPINNED does the same. Read when the sandbox starts."),
    App::ParamInfo("App", "SandboxParams", "User parameter:BaseApp/Preferences/Expression/Security", "Enforce", "Enforce", App::ParamInfo::Bool, true)
        .setTitle("Enforce document permissions")
        .setDoc("The permission rules of a document are enforced: what its\n"
"expressions and Python objects may reach is checked against what\n"
"the user granted. Off, nothing is checked. On no page. Takes\n"
"effect at once."),
});

// Auto generated code (Tools/params_utils.py:368)
ParameterGrp::handle SandboxParams::getHandle() {
    return instance()->handle;
}

// Auto generated code (Tools/params_utils.py:397)
const char *SandboxParams::docEvaluate() {
    return QT_TRANSLATE_NOOP("SandboxParams",
"Expressions that call Python, and the Python objects of a document,\n"
"are evaluated in the sandbox instead of the program's own\n"
"interpreter, when the sandbox is there. The sandbox indicator of\n"
"the status bar switches it. Takes effect at once.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & SandboxParams::getEvaluate() {
    return instance()->Evaluate;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & SandboxParams::defaultEvaluate() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SandboxParams::setEvaluate(const bool &v) {
    instance()->subHandles[0]->SetBool("Evaluate",v);
    instance()->Evaluate = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SandboxParams::removeEvaluate() {
    instance()->subHandles[0]->RemoveBool("Evaluate");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SandboxParams::docRuntime() {
    return QT_TRANSLATE_NOOP("SandboxParams",
"Which runtime carries the sandbox: \"pyodide\" or \"wasi\". Empty, the\n"
"environment variable FCX_RUNTIME decides, and then the build:\n"
"pyodide where it has it. Read when the sandbox starts.");
}

// Auto generated code (Tools/params_utils.py:405)
const std::string & SandboxParams::getRuntime() {
    return instance()->Runtime;
}

// Auto generated code (Tools/params_utils.py:413)
const std::string & SandboxParams::defaultRuntime() {
    const static std::string def = "";
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SandboxParams::setRuntime(const std::string &v) {
    instance()->subHandles[0]->SetASCII("Runtime",v);
    instance()->Runtime = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SandboxParams::removeRuntime() {
    instance()->subHandles[0]->RemoveASCII("Runtime");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SandboxParams::docBudgetMs() {
    return QT_TRANSLATE_NOOP("SandboxParams",
"Milliseconds one evaluation in the sandbox may take before it is\n"
"interrupted. Takes effect at the next evaluation.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & SandboxParams::getBudgetMs() {
    return instance()->BudgetMs;
}

// Auto generated code (Tools/params_utils.py:413)
const long & SandboxParams::defaultBudgetMs() {
    const static long def = 5000;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SandboxParams::setBudgetMs(const long &v) {
    instance()->subHandles[0]->SetInt("BudgetMs",v);
    instance()->BudgetMs = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SandboxParams::removeBudgetMs() {
    instance()->subHandles[0]->RemoveInt("BudgetMs");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SandboxParams::docGraceMs() {
    return QT_TRANSLATE_NOOP("SandboxParams",
"Milliseconds an interrupted evaluation is given to stop by itself\n"
"after its time budget, before the sandbox is stopped. Takes effect\n"
"at the next evaluation.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & SandboxParams::getGraceMs() {
    return instance()->GraceMs;
}

// Auto generated code (Tools/params_utils.py:413)
const long & SandboxParams::defaultGraceMs() {
    const static long def = 1000;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SandboxParams::setGraceMs(const long &v) {
    instance()->subHandles[0]->SetInt("GraceMs",v);
    instance()->GraceMs = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SandboxParams::removeGraceMs() {
    instance()->subHandles[0]->RemoveInt("GraceMs");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SandboxParams::docMemoryMB() {
    return QT_TRANSLATE_NOOP("SandboxParams",
"Megabytes of memory the sandbox may use; an evaluation that takes\n"
"it past that stops the sandbox. Takes effect the next time the\n"
"sandbox's memory grows.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & SandboxParams::getMemoryMB() {
    return instance()->MemoryMB;
}

// Auto generated code (Tools/params_utils.py:413)
const long & SandboxParams::defaultMemoryMB() {
    const static long def = 1024;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SandboxParams::setMemoryMB(const long &v) {
    instance()->subHandles[0]->SetInt("MemoryMB",v);
    instance()->MemoryMB = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SandboxParams::removeMemoryMB() {
    instance()->subHandles[0]->RemoveInt("MemoryMB");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SandboxParams::docEngineHeapMB() {
    return QT_TRANSLATE_NOOP("SandboxParams",
"Megabytes of heap the engine that runs the sandbox may use for\n"
"itself; 0 leaves it to the engine. Takes effect when the sandbox\n"
"next starts.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & SandboxParams::getEngineHeapMB() {
    return instance()->EngineHeapMB;
}

// Auto generated code (Tools/params_utils.py:413)
const long & SandboxParams::defaultEngineHeapMB() {
    const static long def = 512;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SandboxParams::setEngineHeapMB(const long &v) {
    instance()->subHandles[0]->SetInt("EngineHeapMB",v);
    instance()->EngineHeapMB = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SandboxParams::removeEngineHeapMB() {
    instance()->subHandles[0]->RemoveInt("EngineHeapMB");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SandboxParams::docImagePath() {
    return QT_TRANSLATE_NOOP("SandboxParams",
"Path of the sandbox image the wasi runtime loads. Empty, the\n"
"environment variable FCX_IMAGE is used, and then the image that\n"
"comes with the program. Read when the sandbox starts.");
}

// Auto generated code (Tools/params_utils.py:405)
const std::string & SandboxParams::getImagePath() {
    return instance()->ImagePath;
}

// Auto generated code (Tools/params_utils.py:413)
const std::string & SandboxParams::defaultImagePath() {
    const static std::string def = "";
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SandboxParams::setImagePath(const std::string &v) {
    instance()->subHandles[0]->SetASCII("ImagePath",v);
    instance()->ImagePath = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SandboxParams::removeImagePath() {
    instance()->subHandles[0]->RemoveASCII("ImagePath");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SandboxParams::docStdlibPath() {
    return QT_TRANSLATE_NOOP("SandboxParams",
"Folder of the Python standard library of the wasi image. Empty,\n"
"the environment variable FCX_STDLIB is used, and then the one that\n"
"comes with the program. Read when the sandbox starts.");
}

// Auto generated code (Tools/params_utils.py:405)
const std::string & SandboxParams::getStdlibPath() {
    return instance()->StdlibPath;
}

// Auto generated code (Tools/params_utils.py:413)
const std::string & SandboxParams::defaultStdlibPath() {
    const static std::string def = "";
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SandboxParams::setStdlibPath(const std::string &v) {
    instance()->subHandles[0]->SetASCII("StdlibPath",v);
    instance()->StdlibPath = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SandboxParams::removeStdlibPath() {
    instance()->subHandles[0]->RemoveASCII("StdlibPath");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SandboxParams::docPyodideDir() {
    return QT_TRANSLATE_NOOP("SandboxParams",
"Folder of the Pyodide runtime the sandbox boots. Empty, the\n"
"environment variable FCX_PYODIDE is used, and then what is\n"
"installed for the user or comes with the program. Read when the\n"
"sandbox starts.");
}

// Auto generated code (Tools/params_utils.py:405)
const std::string & SandboxParams::getPyodideDir() {
    return instance()->PyodideDir;
}

// Auto generated code (Tools/params_utils.py:413)
const std::string & SandboxParams::defaultPyodideDir() {
    const static std::string def = "";
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SandboxParams::setPyodideDir(const std::string &v) {
    instance()->subHandles[0]->SetASCII("PyodideDir",v);
    instance()->PyodideDir = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SandboxParams::removePyodideDir() {
    instance()->subHandles[0]->RemoveASCII("PyodideDir");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SandboxParams::docPyodideWheel() {
    return QT_TRANSLATE_NOOP("SandboxParams",
"Path of the wheel that holds the sandbox's own Python package.\n"
"Empty, the environment variable FCX_PYODIDE_WHEEL is used, and then\n"
"the one that comes with the runtime. Read when the sandbox starts.");
}

// Auto generated code (Tools/params_utils.py:405)
const std::string & SandboxParams::getPyodideWheel() {
    return instance()->PyodideWheel;
}

// Auto generated code (Tools/params_utils.py:413)
const std::string & SandboxParams::defaultPyodideWheel() {
    const static std::string def = "";
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SandboxParams::setPyodideWheel(const std::string &v) {
    instance()->subHandles[0]->SetASCII("PyodideWheel",v);
    instance()->PyodideWheel = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SandboxParams::removePyodideWheel() {
    instance()->subHandles[0]->RemoveASCII("PyodideWheel");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SandboxParams::docPyodideUserDir() {
    return QT_TRANSLATE_NOOP("SandboxParams",
"Folder the sandbox keeps what is installed for the user in. Empty,\n"
"the environment variable FCX_PYODIDE_USER is used, and then the\n"
"folder Pyodide of the user's application data.");
}

// Auto generated code (Tools/params_utils.py:405)
const std::string & SandboxParams::getPyodideUserDir() {
    return instance()->PyodideUserDir;
}

// Auto generated code (Tools/params_utils.py:413)
const std::string & SandboxParams::defaultPyodideUserDir() {
    const static std::string def = "";
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SandboxParams::setPyodideUserDir(const std::string &v) {
    instance()->subHandles[0]->SetASCII("PyodideUserDir",v);
    instance()->PyodideUserDir = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SandboxParams::removePyodideUserDir() {
    instance()->subHandles[0]->RemoveASCII("PyodideUserDir");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SandboxParams::docPyodidePackages() {
    return QT_TRANSLATE_NOOP("SandboxParams",
"Folder of the packages installed into the sandbox. Empty, the\n"
"environment variable FCX_PYODIDE_PACKAGES is used, and then the\n"
"folder packages of the Pyodide user folder.");
}

// Auto generated code (Tools/params_utils.py:405)
const std::string & SandboxParams::getPyodidePackages() {
    return instance()->PyodidePackages;
}

// Auto generated code (Tools/params_utils.py:413)
const std::string & SandboxParams::defaultPyodidePackages() {
    const static std::string def = "";
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SandboxParams::setPyodidePackages(const std::string &v) {
    instance()->subHandles[0]->SetASCII("PyodidePackages",v);
    instance()->PyodidePackages = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SandboxParams::removePyodidePackages() {
    instance()->subHandles[0]->RemoveASCII("PyodidePackages");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SandboxParams::docPyodideUnpinned() {
    return QT_TRANSLATE_NOOP("SandboxParams",
"For development only: boots a Pyodide release that is not in the\n"
"table of releases the program was pinned to, or whose files differ\n"
"from it, and says so at every boot. The environment variable\n"
"FCX_PYODIDE_UNPINNED does the same. Read when the sandbox starts.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & SandboxParams::getPyodideUnpinned() {
    return instance()->PyodideUnpinned;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & SandboxParams::defaultPyodideUnpinned() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SandboxParams::setPyodideUnpinned(const bool &v) {
    instance()->subHandles[0]->SetBool("PyodideUnpinned",v);
    instance()->PyodideUnpinned = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SandboxParams::removePyodideUnpinned() {
    instance()->subHandles[0]->RemoveBool("PyodideUnpinned");
}

// Auto generated code (Tools/params_utils.py:397)
const char *SandboxParams::docEnforce() {
    return QT_TRANSLATE_NOOP("SandboxParams",
"The permission rules of a document are enforced: what its\n"
"expressions and Python objects may reach is checked against what\n"
"the user granted. Off, nothing is checked. On no page. Takes\n"
"effect at once.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & SandboxParams::getEnforce() {
    return instance()->Enforce;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & SandboxParams::defaultEnforce() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void SandboxParams::setEnforce(const bool &v) {
    instance()->subHandles[1]->SetBool("Enforce",v);
    instance()->Enforce = v;
}

// Auto generated code (Tools/params_utils.py:431)
void SandboxParams::removeEnforce() {
    instance()->subHandles[1]->RemoveBool("Enforce");
}
//[[[end]]]
