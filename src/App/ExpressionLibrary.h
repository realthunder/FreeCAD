// SPDX-License-Identifier: LGPL-2.1-or-later

#ifndef APP_EXPRESSIONLIBRARY_H
#define APP_EXPRESSIONLIBRARY_H

#include <set>
#include <string>
#include <utility>
#include <vector>

#include "TextDocument.h"

namespace App
{

/** A module written in the expression language, carried by the document
 * (docs/Sandbox.md 7.17 (c)).
 *
 * `Text` is engine statements, run ONCE in a fresh frame the first time
 * an expression of the same document imports `Module`; what the run
 * leaves bound is the module's namespace.  A function the module defines
 * resolves the module's other names when it is called, not its caller's.
 * Natively the module is built here; routed, the guest builds its own
 * from the text the host serves (`lib.source`), keyed by the principal of
 * the document whose text it is, and drops it when the text changes
 * (`lib.drop`).  Importing a library is a dependency on its `Text`.
 */
class AppExport ExpressionLibrary: public TextDocument
{
    PROPERTY_HEADER_WITH_OVERRIDE(App::ExpressionLibrary);

public:
    PropertyString Module;
    PropertyString Surface;

    ExpressionLibrary();
    ~ExpressionLibrary() override;

    /// The name `import` binds: Module, or when empty the object's Name
    /// with its first letter lower-cased -- never the Name, which a dotted
    /// identifier resolves to the object before any module.
    std::string getModuleName() const;

    /// Moves on every change of Text or Module; unique in the process.
    uint64_t getLibraryRevision() const
    {
        return revision;
    }

    /// The native module, built on first use after each change.  Throws
    /// the build's error, and ImportError on a circular import.
    Py::Object getModule();

    /// Record that the guest was served this library's text under `key`,
    /// so a change can drop the guest's module.
    void noteServed(const std::string& key);

    /// The module names the text imports, parsed once per revision; empty
    /// when the text does not parse.
    const std::vector<std::string>& importedModules();

    /// The library of `doc` whose module name is `module`, or nullptr.
    static ExpressionLibrary* find(const Document* doc, const std::string& module);

    /// Every library of `doc`, in object order.
    static std::vector<ExpressionLibrary*> libraries(const Document* doc);

    /// The native module `module` resolves to for an expression owned by
    /// `owner`, or None when the owner's document holds no such library.
    static Py::Object importModule(const DocumentObject* owner, const std::string& module);

    DocumentObjectExecReturn* execute() override;

protected:
    void onChanged(const Property* prop) override;
    void onDocumentRestored() override;
    void unsetupObject() override;

private:
    void dropModule();
    /// Whether every library the module imported is still the build it got.
    bool importsCurrent();

    uint64_t revision;
    uint64_t builtRevision = 0;
    /// which build the module is, and the libraries it imported with theirs
    uint64_t buildSerial = 0;
    std::vector<std::pair<std::string, uint64_t>> builtImports;
    std::vector<std::string> imports;
    uint64_t importsRevision = 0;
    bool building = false;
    Py::Object module;
    /// (principal key, module name) pairs the guest holds a module under
    std::set<std::pair<std::string, std::string>> served;
};

}  // namespace App

#endif  // APP_EXPRESSIONLIBRARY_H
