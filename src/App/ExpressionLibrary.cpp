// SPDX-License-Identifier: LGPL-2.1-or-later

#include "PreCompiled.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdlib>
#include <sstream>

#include <Base/Console.h>
#include <Base/Interpreter.h>

#include "Document.h"
#include "ExpressionLibrary.h"
#include "ExpressionParser.h"
#include "ExpressionSecurityRuntime.h"
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
    ADD_PROPERTY_TYPE(Source,
                      (nullptr),
                      "Library",
                      Prop_None,
                      "Another expression library, usually in another file, whose text "
                      "this one uses instead of its own.");
    ADD_PROPERTY_TYPE(Pinned,
                      (false),
                      "Library",
                      Prop_None,
                      "Use the Snapshot taken when pinned rather than following Source; "
                      "the document then opens and recomputes without the source file.");
    ADD_PROPERTY_TYPE(Snapshot,
                      (""),
                      "Library",
                      Prop_ReadOnly,
                      "The source's text at the pin.");
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

bool ExpressionLibrary::isLiveLink() const
{
    if (Pinned.getValue())
        return false;
    const char* name = Source.getObjectName();
    return Source.getValue() || (name && name[0]);
}

ExpressionLibrary* ExpressionLibrary::getHolder(std::string* why)
{
    ExpressionLibrary* lib = this;
    std::set<const ExpressionLibrary*> seen;
    while (lib->isLiveLink()) {
        if (!seen.insert(lib).second) {
            if (why)
                *why = "its Source links back to itself";
            return nullptr;
        }
        auto target = lib->Source.getValue();
        if (!target) {
            if (why) {
                std::ostringstream ss;
                ss << "Source not found: ";
                if (lib->Source.getFilePath()[0])
                    ss << lib->Source.getFilePath() << "#";
                ss << lib->Source.getObjectName();
                *why = ss.str();
            }
            return nullptr;
        }
        auto next = freecad_dynamic_cast<ExpressionLibrary>(target);
        if (!next) {
            if (why)
                *why = "Source " + target->getFullName() + " is not an expression library";
            return nullptr;
        }
        lib = next;
    }
    return lib;
}

const char* ExpressionLibrary::getLibraryText()
{
    auto holder = getHolder();
    if (!holder)
        return "";
    const char* text = holder->Pinned.getValue() ? holder->Snapshot.getValue()
                                                 : holder->Text.getValue();
    return text ? text : "";
}

uint64_t ExpressionLibrary::getLibraryRevision()
{
    auto holder = getHolder();
    return holder ? holder->revision : revision;
}

std::string ExpressionLibrary::getPrincipalCode() const
{
    if (Pinned.getValue())
        return Snapshot.getValue();
    if (isLiveLink()) {
        // the identity of the link, as stored: retargeting it voids the
        // grants, and the text it reaches is the source document's code
        return std::string("link:") + Source.getFilePath() + "#" + Source.getObjectName();
    }
    return Text.getValue();
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

std::vector<ExpressionLibrary::ImportEntry> ExpressionLibrary::importTable(const Document* doc)
{
    std::vector<ImportEntry> res;
    for (auto lib : libraries(doc)) {
        std::string module = lib->getModuleName();
        if (std::any_of(res.begin(), res.end(), [&](const ImportEntry& e) {
                return e.module == module;
            }))
            continue;
        auto holder = lib->getHolder();
        std::string key = holder
            ? ExpressionSecurity::Runtime::instance().documentPrincipal(holder->getDocument())
            : std::string();
        res.push_back({std::move(module), std::move(key), lib->getLibraryRevision()});
    }
    return res;
}

bool ExpressionLibrary::reachesHome(const Document* from, const Document* home)
{
    if (!from || !home)
        return false;
    if (from == home)
        return true;
    std::vector<const Document*> queue {from};
    std::set<const Document*> seen {from};
    while (!queue.empty()) {
        const Document* doc = queue.back();
        queue.pop_back();
        for (auto lib : libraries(doc)) {
            auto holder = lib->getHolder();
            if (!holder)
                continue;
            const Document* holderDoc = holder->getDocument();
            if (holderDoc == home)
                return true;
            if (seen.insert(holderDoc).second)
                queue.push_back(holderDoc);
        }
    }
    return false;
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
        buildStack.back()->emplace_back(module, lib->getBuildSerial());
    return mod;
}

Py::Object ExpressionLibrary::getModule()
{
    std::string why;
    auto holder = getHolder(&why);
    if (!holder)
        FC_THROWM(Base::ImportError, "library '" << getModuleName() << "': " << why);
    // a live link is the source's module: built once for all its consumers,
    // its own imports resolving in the source's document
    if (holder != this)
        return holder->getModule();
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
        module = ExpressionParser::buildLibraryModule(this, getModuleName(), getLibraryText());
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

uint64_t ExpressionLibrary::getBuildSerial()
{
    auto holder = getHolder();
    return holder ? holder->buildSerial : 0;
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
        if (lib->getBuildSerial() != imported.second)
            return false;
    }
    return true;
}

const std::vector<std::string>& ExpressionLibrary::importedModules()
{
    static const std::vector<std::string> none;
    auto holder = getHolder();
    if (!holder)
        return none;
    if (holder != this)
        return holder->importedModules();
    if (importsRevision != revision) {
        importsRevision = revision;
        imports.clear();
        const char* text = getLibraryText();
        if (text[0]) {
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

void ExpressionLibrary::noteServed(const std::string& key, const std::string& module)
{
    served.emplace(key, module);
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

void ExpressionLibrary::takeSnapshot()
{
    auto target = freecad_dynamic_cast<ExpressionLibrary>(Source.getValue());
    auto holder = target ? target->getHolder() : nullptr;
    if (!holder || holder == this) {
        // pinned with nothing to take from: the snapshot it has, if any,
        // is what it keeps serving -- a library copied in
        if (Source.getObjectName()[0])
            FC_WARN(getFullName() << ": pinned, but Source does not resolve; "
                                     "the snapshot is left as it was");
        return;
    }
    Snapshot.setValue(holder->getLibraryText());
    Surface.setValue(holder->Surface.getValue());
}

void ExpressionLibrary::updateTextStatus()
{
    // the own text is not what a linked or pinned library runs
    Text.setStatus(Property::ReadOnly, Pinned.getValue() || isLiveLink());
}

void ExpressionLibrary::onChanged(const Property* prop)
{
    if (prop == &Text || prop == &Module || prop == &Source || prop == &Pinned
        || prop == &Snapshot) {
        revision = ++lastRevision;
        dropModule();
        auto doc = getDocument();
        const bool edit = !isRestoring() && !(doc && doc->isPerformingTransaction());
#ifdef FC_EXPR_IMAGE_HOST
        if (prop == &Text && edit && !Pinned.getValue() && !isLiveLink())
            Surface.setValue(std::to_string(ExpressionSandbox::surfaceVersion()));
#endif
        if (edit && (prop == &Pinned || (prop == &Source && Pinned.getValue()))) {
            // pinning takes the source's text now; unpinning goes back to
            // following the source, and the snapshot has nothing left to say
            if (Pinned.getValue())
                takeSnapshot();
            else
                Snapshot.setValue("");
        }
        if (prop == &Source || prop == &Pinned)
            updateTextStatus();
    }
    TextDocument::onChanged(prop);
}

void ExpressionLibrary::onDocumentRestored()
{
    TextDocument::onDocumentRestored();
    updateTextStatus();
#ifdef FC_EXPR_IMAGE_HOST
    // a live link's text is the source's, stamped and warned about there
    const char* stamp = Surface.getValue();
    if (!isLiveLink() && stamp && std::isdigit(static_cast<unsigned char>(stamp[0]))) {
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
    // consumer happens to import it; so does a link that does not resolve.
    std::string why;
    if (!getHolder(&why))
        return new DocumentObjectExecReturn(why);
    const char* text = getLibraryText();
    const char* p = text;
    while (*p && std::isspace(static_cast<unsigned char>(*p)))
        ++p;
    if (*p) {
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
