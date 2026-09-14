// SPDX-License-Identifier: LGPL-2.1-or-later

#ifndef APP_EXPRESSIONLIBRARY_H
#define APP_EXPRESSIONLIBRARY_H

#include <set>
#include <string>
#include <utility>
#include <vector>

#include "PropertyLinks.h"
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
 *
 * A LINKED library takes its text from another library through `Source`,
 * usually in another file, under a `Module` name of its own.  Live, it is
 * the source's text and the source's module, and its imports resolve in
 * the source's document; `Pinned`, it is the `Snapshot` taken at the pin,
 * which serves with the source file absent.  The library whose text is
 * used is the HOLDER: the object itself, a pinned one, or the end of the
 * live links.
 */
class AppExport ExpressionLibrary: public TextDocument
{
    PROPERTY_HEADER_WITH_OVERRIDE(App::ExpressionLibrary);

public:
    PropertyString Module;
    PropertyString Surface;
    PropertyXLink Source;
    PropertyBool Pinned;
    PropertyString Snapshot;

    ExpressionLibrary();
    ~ExpressionLibrary() override;

    /// The name `import` binds: Module, or when empty the object's Name
    /// with its first letter lower-cased -- never the Name, which a dotted
    /// identifier resolves to the object before any module.
    std::string getModuleName() const;

    /// Whether `Source` names a library and the link is live, not pinned.
    bool isLiveLink() const;

    /// The library whose text this one uses: itself when unlinked or
    /// pinned, else the end of the live links.  nullptr when a link does
    /// not resolve, with the reason in `why`.
    ExpressionLibrary* getHolder(std::string* why = nullptr);

    /// The holder's text: Snapshot when pinned, else Text; "" while a link
    /// does not resolve.
    const char* getLibraryText();

    /// The holder's revision, which moves on every change of the text a
    /// consumer gets; unique in the process.  Its own while unresolved.
    uint64_t getLibraryRevision();

    /// What of this library joins its document's principal: the text for
    /// an unlinked library, the snapshot for a pinned one, and for a live
    /// link the link itself -- the text is the source document's code.
    std::string getPrincipalCode() const;

    /// The native module, built on first use after each change -- the
    /// holder's, for a live link.  Throws the build's error, ImportError on
    /// a circular import or an unresolved link.
    Py::Object getModule();

    /// Record that the guest was served this library's text under `key`
    /// and `module`, so a change can drop the guest's module.
    void noteServed(const std::string& key, const std::string& module);

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

    /// One import name of a document as the guest is told it: the principal
    /// key of the holder's document ("" while a link is unresolved) and the
    /// holder's revision.
    struct ImportEntry
    {
        std::string module;
        std::string key;
        uint64_t rev;
    };
    /// The import table of `doc`, the first library of a duplicated name.
    static std::vector<ImportEntry> importTable(const Document* doc);

    /// Whether `home` is the document of a holder that a live link reaches
    /// from `from`'s libraries, at any depth.
    static bool reachesHome(const Document* from, const Document* home);

    DocumentObjectExecReturn* execute() override;

protected:
    void onChanged(const Property* prop) override;
    void onDocumentRestored() override;
    void unsetupObject() override;

private:
    void dropModule();
    /// Whether every library the module imported is still the build it got.
    bool importsCurrent();
    uint64_t getBuildSerial();
    /// Fill Snapshot and Surface from what Source resolves to.
    void takeSnapshot();
    void updateTextStatus();

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
