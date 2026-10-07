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
import FemParams
FemParams.define()
]]]*/

// Auto generated code (Tools/params_utils.py:198)
#include <unordered_map>
#include <App/Application.h>
#include <App/DynamicProperty.h>
#include <App/ParamRegistry.h>
#include "FemParams.h"
using namespace Fem;

// Auto generated code (Tools/params_utils.py:210)
namespace {
class FemParamsP: public ParameterGrp::ObserverType {
public:
    ParameterGrp::handle handle;
    std::unordered_map<const char *,void(*)(FemParamsP*),App::CStringHasher,App::CStringHasher> funcs;
    std::vector<ParameterGrp::handle> subHandles;
    bool PostAutoRecompute;
    long DefaultSolver;
    std::string MeshExportLevel;
    long AbaqusElementChoice;
    bool AbaqusWriteGroups;
    std::string GmshLogVerbosity;
    std::string Z88Solver;

    // Auto generated code (Tools/params_utils.py:254)
    FemParamsP() {
        handle = App::GetApplication().GetParameterGroupByPath("User parameter:BaseApp/Preferences/Mod/Fem");
        handle->Attach(this);

        subHandles.resize(5);
        subHandles[0] = handle->GetGroup("General");
        subHandles[0]->Attach(this);
        subHandles[1] = handle->GetGroup("InOutVtk");
        subHandles[1]->Attach(this);
        subHandles[2] = handle->GetGroup("Abaqus");
        subHandles[2]->Attach(this);
        subHandles[3] = handle->GetGroup("Gmsh");
        subHandles[3]->Attach(this);
        subHandles[4] = handle->GetGroup("Z88");
        subHandles[4]->Attach(this);
        PostAutoRecompute = this->handle->GetBool("PostAutoRecompute", true);
        funcs["PostAutoRecompute"] = &FemParamsP::updatePostAutoRecompute;
        DefaultSolver = this->subHandles[0]->GetInt("DefaultSolver", 0);
        funcs["DefaultSolver"] = &FemParamsP::updateDefaultSolver;
        MeshExportLevel = this->subHandles[1]->GetASCII("MeshExportLevel", "Highest");
        funcs["MeshExportLevel"] = &FemParamsP::updateMeshExportLevel;
        AbaqusElementChoice = this->subHandles[2]->GetInt("AbaqusElementChoice", 2);
        funcs["AbaqusElementChoice"] = &FemParamsP::updateAbaqusElementChoice;
        AbaqusWriteGroups = this->subHandles[2]->GetBool("AbaqusWriteGroups", true);
        funcs["AbaqusWriteGroups"] = &FemParamsP::updateAbaqusWriteGroups;
        GmshLogVerbosity = this->subHandles[3]->GetASCII("LogVerbosity", "3");
        funcs["LogVerbosity"] = &FemParamsP::updateGmshLogVerbosity;
        Z88Solver = this->subHandles[4]->GetASCII("Solver", "sorcg");
        funcs["Solver"] = &FemParamsP::updateZ88Solver;
    }

    // Auto generated code (Tools/params_utils.py:284)
    ~FemParamsP() override = default;

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
    static void updatePostAutoRecompute(FemParamsP *self) {
        self->PostAutoRecompute = self->handle->GetBool("PostAutoRecompute", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateDefaultSolver(FemParamsP *self) {
        self->DefaultSolver = self->subHandles[0]->GetInt("DefaultSolver", 0);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateMeshExportLevel(FemParamsP *self) {
        self->MeshExportLevel = self->subHandles[1]->GetASCII("MeshExportLevel", "Highest");
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateAbaqusElementChoice(FemParamsP *self) {
        self->AbaqusElementChoice = self->subHandles[2]->GetInt("AbaqusElementChoice", 2);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateAbaqusWriteGroups(FemParamsP *self) {
        self->AbaqusWriteGroups = self->subHandles[2]->GetBool("AbaqusWriteGroups", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateGmshLogVerbosity(FemParamsP *self) {
        self->GmshLogVerbosity = self->subHandles[3]->GetASCII("LogVerbosity", "3");
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateZ88Solver(FemParamsP *self) {
        self->Z88Solver = self->subHandles[4]->GetASCII("Solver", "sorcg");
    }
};

// Auto generated code (Tools/params_utils.py:336)
FemParamsP *instance() {
    static FemParamsP *inst = new FemParamsP;
    return inst;
}

} // Anonymous namespace

// Auto generated code (Tools/params_utils.py:352)
static const App::ParamRegistry::Registrar _FemParamsRegistrar({
    App::ParamInfo("Fem", "FemParams", "User parameter:BaseApp/Preferences/Mod/Fem", "PostAutoRecompute", "PostAutoRecompute", App::ParamInfo::Bool, true)
        .setTitle("Apply changes automatically")
        .setDoc("Recomputes a post-processing object as soon as one of its settings\n"
"is changed in a task panel. Takes effect at once."),
    App::ParamInfo("Fem", "FemParams", "User parameter:BaseApp/Preferences/Mod/Fem/General", "DefaultSolver", "DefaultSolver", App::ParamInfo::Int, 0)
        .setTitle("Default solver")
        .setDoc("Solver added to a new analysis container: 0 none, 1 CalculiX, 2\n"
"Elmer, 3 Mystran, 4 Z88. Takes effect when the next analysis is\n"
"created."),
    App::ParamInfo("Fem", "FemParams", "User parameter:BaseApp/Preferences/Mod/Fem/InOutVtk", "MeshExportLevel", "MeshExportLevel", App::ParamInfo::String, "Highest")
        .setTitle("VTK mesh export level")
        .setDoc("Which mesh elements are written to a VTK file: All, or Highest for\n"
"only those of the highest dimension. Takes effect at the next\n"
"export."),
    App::ParamInfo("Fem", "FemParams", "User parameter:BaseApp/Preferences/Mod/Fem/Abaqus", "AbaqusElementChoice", "AbaqusElementChoice", App::ParamInfo::Int, 2)
        .setTitle("INP elements to export")
        .setDoc("Which mesh elements are written to an Abaqus INP file: 0 all, 1\n"
"only the highest, 2 only the FEM elements. Takes effect at the\n"
"next export."),
    App::ParamInfo("Fem", "FemParams", "User parameter:BaseApp/Preferences/Mod/Fem/Abaqus", "AbaqusWriteGroups", "AbaqusWriteGroups", App::ParamInfo::Bool, true)
        .setTitle("Export INP group data")
        .setDoc("Writes the mesh groups as well when a mesh is exported to an\n"
"Abaqus INP file. Takes effect at the next export."),
    App::ParamInfo("Fem", "FemParams", "User parameter:BaseApp/Preferences/Mod/Fem/Gmsh", "GmshLogVerbosity", "LogVerbosity", App::ParamInfo::String, "3")
        .setTitle("Gmsh log verbosity")
        .setDoc("How much Gmsh reports in the task panel while it meshes, as its\n"
"verbosity level: from 0, silent, to 99, debug. Takes effect at the\n"
"next meshing run."),
    App::ParamInfo("Fem", "FemParams", "User parameter:BaseApp/Preferences/Mod/Fem/Z88", "Z88Solver", "Solver", App::ParamInfo::String, "sorcg")
        .setTitle("Z88 solver method")
        .setDoc("Solver method given to a new Z88 solver object. Applies to solver\n"
"objects created afterwards."),
});

// Auto generated code (Tools/params_utils.py:368)
ParameterGrp::handle FemParams::getHandle() {
    return instance()->handle;
}

// Auto generated code (Tools/params_utils.py:397)
const char *FemParams::docPostAutoRecompute() {
    return QT_TRANSLATE_NOOP("FemParams",
"Recomputes a post-processing object as soon as one of its settings\n"
"is changed in a task panel. Takes effect at once.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & FemParams::getPostAutoRecompute() {
    return instance()->PostAutoRecompute;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & FemParams::defaultPostAutoRecompute() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void FemParams::setPostAutoRecompute(const bool &v) {
    instance()->handle->SetBool("PostAutoRecompute",v);
    instance()->PostAutoRecompute = v;
}

// Auto generated code (Tools/params_utils.py:431)
void FemParams::removePostAutoRecompute() {
    instance()->handle->RemoveBool("PostAutoRecompute");
}

// Auto generated code (Tools/params_utils.py:397)
const char *FemParams::docDefaultSolver() {
    return QT_TRANSLATE_NOOP("FemParams",
"Solver added to a new analysis container: 0 none, 1 CalculiX, 2\n"
"Elmer, 3 Mystran, 4 Z88. Takes effect when the next analysis is\n"
"created.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & FemParams::getDefaultSolver() {
    return instance()->DefaultSolver;
}

// Auto generated code (Tools/params_utils.py:413)
const long & FemParams::defaultDefaultSolver() {
    const static long def = 0;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void FemParams::setDefaultSolver(const long &v) {
    instance()->subHandles[0]->SetInt("DefaultSolver",v);
    instance()->DefaultSolver = v;
}

// Auto generated code (Tools/params_utils.py:431)
void FemParams::removeDefaultSolver() {
    instance()->subHandles[0]->RemoveInt("DefaultSolver");
}

// Auto generated code (Tools/params_utils.py:397)
const char *FemParams::docMeshExportLevel() {
    return QT_TRANSLATE_NOOP("FemParams",
"Which mesh elements are written to a VTK file: All, or Highest for\n"
"only those of the highest dimension. Takes effect at the next\n"
"export.");
}

// Auto generated code (Tools/params_utils.py:405)
const std::string & FemParams::getMeshExportLevel() {
    return instance()->MeshExportLevel;
}

// Auto generated code (Tools/params_utils.py:413)
const std::string & FemParams::defaultMeshExportLevel() {
    const static std::string def = "Highest";
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void FemParams::setMeshExportLevel(const std::string &v) {
    instance()->subHandles[1]->SetASCII("MeshExportLevel",v);
    instance()->MeshExportLevel = v;
}

// Auto generated code (Tools/params_utils.py:431)
void FemParams::removeMeshExportLevel() {
    instance()->subHandles[1]->RemoveASCII("MeshExportLevel");
}

// Auto generated code (Tools/params_utils.py:397)
const char *FemParams::docAbaqusElementChoice() {
    return QT_TRANSLATE_NOOP("FemParams",
"Which mesh elements are written to an Abaqus INP file: 0 all, 1\n"
"only the highest, 2 only the FEM elements. Takes effect at the\n"
"next export.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & FemParams::getAbaqusElementChoice() {
    return instance()->AbaqusElementChoice;
}

// Auto generated code (Tools/params_utils.py:413)
const long & FemParams::defaultAbaqusElementChoice() {
    const static long def = 2;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void FemParams::setAbaqusElementChoice(const long &v) {
    instance()->subHandles[2]->SetInt("AbaqusElementChoice",v);
    instance()->AbaqusElementChoice = v;
}

// Auto generated code (Tools/params_utils.py:431)
void FemParams::removeAbaqusElementChoice() {
    instance()->subHandles[2]->RemoveInt("AbaqusElementChoice");
}

// Auto generated code (Tools/params_utils.py:397)
const char *FemParams::docAbaqusWriteGroups() {
    return QT_TRANSLATE_NOOP("FemParams",
"Writes the mesh groups as well when a mesh is exported to an\n"
"Abaqus INP file. Takes effect at the next export.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & FemParams::getAbaqusWriteGroups() {
    return instance()->AbaqusWriteGroups;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & FemParams::defaultAbaqusWriteGroups() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void FemParams::setAbaqusWriteGroups(const bool &v) {
    instance()->subHandles[2]->SetBool("AbaqusWriteGroups",v);
    instance()->AbaqusWriteGroups = v;
}

// Auto generated code (Tools/params_utils.py:431)
void FemParams::removeAbaqusWriteGroups() {
    instance()->subHandles[2]->RemoveBool("AbaqusWriteGroups");
}

// Auto generated code (Tools/params_utils.py:397)
const char *FemParams::docGmshLogVerbosity() {
    return QT_TRANSLATE_NOOP("FemParams",
"How much Gmsh reports in the task panel while it meshes, as its\n"
"verbosity level: from 0, silent, to 99, debug. Takes effect at the\n"
"next meshing run.");
}

// Auto generated code (Tools/params_utils.py:405)
const std::string & FemParams::getGmshLogVerbosity() {
    return instance()->GmshLogVerbosity;
}

// Auto generated code (Tools/params_utils.py:413)
const std::string & FemParams::defaultGmshLogVerbosity() {
    const static std::string def = "3";
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void FemParams::setGmshLogVerbosity(const std::string &v) {
    instance()->subHandles[3]->SetASCII("LogVerbosity",v);
    instance()->GmshLogVerbosity = v;
}

// Auto generated code (Tools/params_utils.py:431)
void FemParams::removeGmshLogVerbosity() {
    instance()->subHandles[3]->RemoveASCII("LogVerbosity");
}

// Auto generated code (Tools/params_utils.py:397)
const char *FemParams::docZ88Solver() {
    return QT_TRANSLATE_NOOP("FemParams",
"Solver method given to a new Z88 solver object. Applies to solver\n"
"objects created afterwards.");
}

// Auto generated code (Tools/params_utils.py:405)
const std::string & FemParams::getZ88Solver() {
    return instance()->Z88Solver;
}

// Auto generated code (Tools/params_utils.py:413)
const std::string & FemParams::defaultZ88Solver() {
    const static std::string def = "sorcg";
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void FemParams::setZ88Solver(const std::string &v) {
    instance()->subHandles[4]->SetASCII("Solver",v);
    instance()->Z88Solver = v;
}

// Auto generated code (Tools/params_utils.py:431)
void FemParams::removeZ88Solver() {
    instance()->subHandles[4]->RemoveASCII("Solver");
}
//[[[end]]]
