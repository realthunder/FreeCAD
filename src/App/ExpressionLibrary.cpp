// SPDX-License-Identifier: LGPL-2.1-or-later

#include "PreCompiled.h"

#include <atomic>
#include <cctype>
#include <cstdlib>

#include <Base/Console.h>
#include <Base/Interpreter.h>

#include "Document.h"
#include "ExpressionLibrary.h"
#include "ExpressionParser.h"
#ifdef FC_EXPR_IMAGE_HOST
#include "ExpressionImageBridge.h"
#include "ExpressionImageHost.h"
#endif

FC_LOG_LEVEL_INIT("Expression", true, true)

using namespace App;

PROPERTY_SOURCE(App::ExpressionLibrary, App::TextDocument)

namespace
{
std::atomic<uint64_t> lastRevision {0};
/// Nothing to look up while no library exists, which is nearly always:
/// every `import` of every expression asks.
std::atomic<int> liveLibraries {0};
std::atomic<uint64_t> lastBuild {0};
/// The imports each library build in progress has made, innermost last.
std::vector<std::vector<std::pair<std::string, uint64_t>>*> buildStack;
}  // namespace

ExpressionLibrary::ExpressionLibrary()
    : revision(++lastRevision)
{
    ADD_PROPERTY_TYPE(Module,
                      (""),
                      "Library",
                      Prop_None,
                      "The name 'import' binds this library under in the document's "
                      "expressions; when empty, the object's Name with its first letter "
                      "lower-cased.");
    ADD_PROPERTY_TYPE(Surface,
                      (""),
                      "Library",
                      Prop_ReadOnly,
                      "The expression surface version the text was last edited against.");
    ++liveLibraries;
}

ExpressionLibrary::~ExpressionLibrary()
{
    --liveLibraries;
    if (!module.isNone()) {
        Base::PyGILStateLocker lock;
        PyDict_Clear(PyModule_GetDict(module.ptr()));
        module = Py::Object();
    }
}

std::string ExpressionLibrary::getModuleName() const
{
    const char* name = Module.getValue();
    if (name && name[0])
        return name;
    if (!isAttachedToDocument())
        return std::string();
    // Never the Name itself: `Lib.f` parses as a property of the object Lib
    // before any frame is asked, so a module named like its object would be
    // unreachable with a dot (docs/Sandbox.md sec 12).
    std::string res(getNameInDocument());
    if (!res.empty())
        res[0] = static_cast<char>(std::tolower(static_cast<unsigned char>(res[0])));
    return res;
}

ExpressionLibrary* ExpressionLibrary::find(const Document* doc, const std::string& module)
{
    if (!doc || module.empty() || liveLibraries.load() == 0)
        return nullptr;
    for (auto obj : doc->getObjectsOfType(getClassTypeId())) {
        auto lib = static_cast<ExpressionLibrary*>(obj);
        if (lib->getModuleName() == module)
            return lib;
    }
    return nullptr;
}

std::vector<ExpressionLibrary*> ExpressionLibrary::libraries(const Document* doc)
{
    std::vector<ExpressionLibrary*> res;
    if (!doc || liveLibraries.load() == 0)
        return res;
    for (auto obj : doc->getObjectsOfType(getClassTypeId()))
        res.push_back(static_cast<ExpressionLibrary*>(obj));
    return res;
}

Py::Object ExpressionLibrary::importModule(const DocumentObject* owner, const std::string& module)
{
    if (!owner || liveLibraries.load() == 0 || module.find('.') != std::string::npos)
        return Py::Object();
    auto lib = find(owner->getDocument(), module);
    if (!lib)
        return Py::Object();
    Py::Object mod = lib->getModule();
    // a library importing a library: the importer's module holds this build
    if (!buildStack.empty())
        buildStack.back()->emplace_back(module, lib->buildSerial);
    return mod;
}

Py::Object ExpressionLibrary::getModule()
{
    if (building)
        FC_THROWM(Base::ImportError, "circular import of library '" << getModuleName() << "'");
    if (!module.isNone() && builtRevision == revision && importsCurrent())
        return module;
    Base::PyGILStateLocker lock;
    if (!module.isNone()) {
        PyDict_Clear(PyModule_GetDict(module.ptr()));
        module = Py::Object();
    }
    std::vector<std::pair<std::string, uint64_t>> pending;
    building = true;
    buildStack.push_back(&pending);
    try {
        module = ExpressionParser::buildLibraryModule(this, getModuleName(), Text.getValue());
    }
    catch (...) {
        buildStack.pop_back();
        building = false;
        throw;
    }
    buildStack.pop_back();
    building = false;
    builtRevision = revision;
    builtImports = std::move(pending);
    buildSerial = ++lastBuild;
    return module;
}

bool ExpressionLibrary::importsCurrent()
{
    // An imported library rebuilt since clears the module this one holds,
    // so this one rebuilds too -- after it, so it binds the new one.
    for (const auto& imported : builtImports) {
        auto lib = find(getDocument(), imported.first);
        if (!lib)
            return false;
        lib->getModule();
        if (lib->buildSerial != imported.second)
            return false;
    }
    return true;
}

const std::vector<std::string>& ExpressionLibrary::importedModules()
{
    if (importsRevision != revision) {
        importsRevision = revision;
        imports.clear();
        const char* text = Text.getValue();
        if (text && text[0]) {
            try {
                Base::PyGILStateLocker lock;
                imports = ExpressionParser::importedModules(ExpressionParser::parse(this, text).get());
            }
            catch (Base::Exception&) {
                // a text that does not parse imports nothing; execute() says why
            }
        }
    }
    return imports;
}

void ExpressionLibrary::noteServed(const std::string& key)
{
    served.emplace(key, getModuleName());
}

void ExpressionLibrary::dropModule()
{
    if (!module.isNone()) {
        Base::PyGILStateLocker lock;
        // the module's functions hold its dict: break the cycle
        PyDict_Clear(PyModule_GetDict(module.ptr()));
        module = Py::Object();
    }
#ifdef FC_EXPR_IMAGE_HOST
    for (const auto& s : served)
        ExpressionSandbox::ImageHost::instance().dropLibrary(s.first, s.second);
#endif
    served.clear();
}

void ExpressionLibrary::onChanged(const Property* prop)
{
    if (prop == &Text || prop == &Module) {
        revision = ++lastRevision;
        dropModule();
#ifdef FC_EXPR_IMAGE_HOST
        if (prop == &Text && !isRestoring())
            Surface.setValue(std::to_string(ExpressionSandbox::surfaceVersion()));
#endif
    }
    TextDocument::onChanged(prop);
}

void ExpressionLibrary::onDocumentRestored()
{
    TextDocument::onDocumentRestored();
#ifdef FC_EXPR_IMAGE_HOST
    const char* stamp = Surface.getValue();
    if (stamp && std::isdigit(static_cast<unsigned char>(stamp[0]))) {
        const int written = std::atoi(stamp);
        if (written < ExpressionSandbox::surfaceVersion())
            FC_WARN(getFullName() << ": library written against expression surface " << written
                                  << ", this build is " << ExpressionSandbox::surfaceVersion());
    }
#endif
}

void ExpressionLibrary::unsetupObject()
{
    dropModule();
    TextDocument::unsetupObject();
}

DocumentObjectExecReturn* ExpressionLibrary::execute()
{
    // A syntax error shows on the library, not first on whichever
    // consumer happens to import it.
    const char* text = Text.getValue();
    const char* p = text;
    while (p && *p && std::isspace(static_cast<unsigned char>(*p)))
        ++p;
    if (p && *p) {
        try {
            Base::PyGILStateLocker lock;
            ExpressionParser::parse(this, text);
        }
        catch (Base::Exception& e) {
            return new DocumentObjectExecReturn(e.what());
        }
    }
    return DocumentObject::StdReturn;
}
