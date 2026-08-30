/* The S1 DocumentAdapter for the sandbox image (expression sandbox
 * phase 1 step 4, docs/ExpressionSandboxPhase0.md sec 2 seam S1 and
 * docs/ExpressionImage.md "Carve audit").
 *
 * The ExpressionCore TUs (Expression.cpp / ObjectIdentifier.cpp /
 * Range.cpp) compile into the wasm image against THIS document world
 * instead of the real one: the classes below carry the exact names and
 * the ~15-method surface the core dereferences, so the seam typedefs in
 * ObjectIdentifier.h (ExpressionDocumentT/ObjectT/PropertyT) keep
 * pointing at App::Document/DocumentObject/Property and no core code
 * changes shape.  Behavior model (docs/ExpressionSandbox.md sec 7.3):
 * identifier values come from the per-evaluation bindings pack the host
 * pre-resolved; property reads consult the pack keyed by the
 * identifier's canonical string; the owner's Python object is the
 * HostHandle proxy for the handle the host exported with the request,
 * so any drill-down past it rides the permission-checked bridge ops.
 * Document/label scans, sub-object walks and link extensions have no
 * in-image backing and resolve to "not found" -- an identifier the pack
 * misses fails cleanly instead of reaching for live Documents.
 *
 * This header is only ever compiled with FC_EXPR_IMAGE defined (the
 * image build); the host build keeps including the real App headers.
 */
#ifndef APP_FCX_DOCUMENT_H
#define APP_FCX_DOCUMENT_H

#ifndef FC_EXPR_IMAGE
#error "FcxDocument.h is the sandbox image's document world; the host build must not include it"
#endif

#include <cstdint>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <Base/BaseClass.h>
#include <Base/Matrix.h>
#include <Base/Quantity.h>
#include <CXX/Objects.hxx>

#include <App/ObjectIdentifier.h>

#include <boost/functional/hash.hpp>

// Property.h's storage-class marker for statics the host may scope
// differently; the image is one TU set, plain statics.
#ifndef FC_STATIC
#define FC_STATIC static
#endif

namespace App
{

/// DynamicProperty.h's char* hasher, used by the pseudo-property table.
struct CStringHasher
{
    std::size_t operator()(const char *s) const
    {
        if (!s)
            return 0;
        return boost::hash_range(s, s + std::strlen(s));
    }
    bool operator()(const char *a, const char *b) const
    {
        if (!a)
            return !b;
        if (!b)
            return false;
        return std::strcmp(a, b) == 0;
    }
};

class Document;
class DocumentObject;
class PropertyContainer;

/// Only member of the host enum the core consults (via testStatus).
enum class ObjectStatus
{
    Remove = 0,
};

/** A property in the image: a name plus a Python value that was either
 * decoded from the bindings pack or synthesized locally (PropertyString
 * as a string converter).  Pack-backed properties are read-only. */
class Property: public Base::BaseClass
{
    TYPESYSTEM_HEADER_WITH_OVERRIDE();

public:
    enum Status
    {
        Immutable = 0,
        PropReadOnly = 1,
    };

    Property() = default;
    ~Property() override = default;

    const char *getName() const
    {
        return name_.c_str();
    }
    bool hasName() const
    {
        return !name_.empty();
    }
    PropertyContainer *getContainer() const
    {
        return container_;
    }
    std::string getFullName() const;
    bool testStatus(Status) const
    {
        return readOnly_;
    }
    bool isTouched() const
    {
        return false;
    }

    PyObject *getPyObject() override;
    void setPyObject(PyObject *obj) override;

    App::any getPathValue(const ObjectIdentifier &path) const;
    bool getPyPathValue(const ObjectIdentifier &, Py::Object &) const
    {
        return false;
    }
    bool setPyPathValue(const ObjectIdentifier &, const Py::Object &)
    {
        return false;
    }
    ObjectIdentifier canonicalPath(const ObjectIdentifier &p) const
    {
        return p;
    }

    // filled by the Fcx factory
    std::string name_;
    PropertyContainer *container_ = nullptr;
    Py::Object value_;
    bool readOnly_ = true;
};

class PropertyString: public Property
{
    TYPESYSTEM_HEADER_WITH_OVERRIDE();

public:
    void setValue(const char *v)
    {
        str_ = v ? v : "";
    }
    void setValue(const std::string &v)
    {
        str_ = v;
    }
    const char *getValue() const
    {
        return str_.c_str();
    }
    const std::string &getStrValue() const
    {
        return str_;
    }
    PyObject *getPyObject() override;
    void setPyObject(PyObject *obj) override;

    std::string str_;
};

class PropertyBool: public Property
{
    TYPESYSTEM_HEADER_WITH_OVERRIDE();

public:
    bool getValue() const
    {
        return bool_;
    }
    bool bool_ = false;
};

class PropertyQuantity: public Property
{
    TYPESYSTEM_HEADER_WITH_OVERRIDE();

public:
    Base::Quantity getQuantityValue() const
    {
        return quantity_;
    }
    Base::Quantity quantity_;
};

class PropertyFloat: public Property
{
    TYPESYSTEM_HEADER_WITH_OVERRIDE();

public:
    double getValue() const
    {
        return float_;
    }
    double float_ = 0.0;
};

class PropertyInteger: public Property
{
    TYPESYSTEM_HEADER_WITH_OVERRIDE();

public:
    long getValue() const
    {
        return int_;
    }
    long int_ = 0;
};

class PropertyContainer: public Base::BaseClass
{
    TYPESYSTEM_HEADER_WITH_OVERRIDE();

public:
    virtual Property *getPropertyByName(const char *) const
    {
        return nullptr;
    }
};

/// The link extension surface access() consults; never present in-image.
class LinkBaseExtension
{
public:
    DocumentObject *getTrueLinkedObject(bool, Base::Matrix4D * = nullptr, int = 0)
    {
        return nullptr;
    }
};

class DocumentObject: public PropertyContainer
{
    TYPESYSTEM_HEADER_WITH_OVERRIDE();

public:
    DocumentObject() = default;

    Document *getDocument() const
    {
        return document_;
    }
    const char *getNameInDocument() const
    {
        return name_.c_str();
    }
    bool isAttachedToDocument() const
    {
        return document_ != nullptr;
    }
    std::string getFullName() const;
    std::string getExportName(bool = false) const
    {
        return name_;
    }
    bool isExporting() const
    {
        return false;
    }
    bool isTouched() const
    {
        return false;
    }
    bool testStatus(ObjectStatus) const
    {
        return false;
    }

    /// Pack-backed: the value the host pre-resolved for
    /// ObjectIdentifier(this, name), wrapped as a typed Property.
    Property *getPropertyByName(const char *name) const override;

    /// The HostHandle proxy for the handle the host exported with the
    /// eval request; raises in Python if the host exported none.
    PyObject *getPyObject() override;

    DocumentObject *getSubObject(const char *subname,
                                 PyObject **pyObj = nullptr,
                                 Base::Matrix4D *mat = nullptr,
                                 bool transform = true,
                                 int depth = 0) const;
    DocumentObject *getLinkedObject(bool recursive = true,
                                    Base::Matrix4D *mat = nullptr,
                                    bool transform = true,
                                    int depth = 0) const;
    template<typename T>
    T *getExtensionByType(bool = true) const
    {
        return nullptr;
    }

    PropertyString Label;

    std::string name_;
    Document *document_ = nullptr;
    // cache of pack-backed properties, keyed by property name
    mutable std::map<std::string, std::unique_ptr<Property>> props_;
};

class Document
{
public:
    const char *getName() const
    {
        return name_.c_str();
    }
    DocumentObject *getObject(const char *name) const;
    std::vector<DocumentObject *> getObjects() const;

    PropertyString Label;
    std::string name_;
};

/** The application shim (seam S4): document lookup only ever finds the
 * documents of the current evaluation transaction. */
class Application
{
public:
    Document *getDocument(const char *name) const;
    std::vector<Document *> getDocuments() const;
};

Application &GetApplication();

/// Static string helpers of the host's PropertyLinkBase the identifier
/// string path calls (step 2's residual S1 coupling).  In-image the
/// subname crosses through untransformed and label references are not
/// collected (dependency tracking is host work).
class PropertyLinkBase
{
public:
    static const char *exportSubName(std::string &output,
                                     const App::DocumentObject *,
                                     const char *subname,
                                     bool = false)
    {
        output = subname ? subname : "";
        return output.c_str();
    }
    static void getLabelReferences(std::vector<std::string> &, const char *)
    {
    }
};

}  // namespace App

namespace Fcx
{

/** Per-evaluation transaction state: the owner document/object the
 * request named, the host-exported owner handle, and the bindings pack.
 * Installed by the image eval dispatcher, torn down when it returns. */
class EvalTransaction
{
public:
    EvalTransaction(const std::string &docName,
                    const std::string &objName,
                    uint64_t ownerHandle);
    ~EvalTransaction();

    App::DocumentObject *owner()
    {
        return &owner_;
    }

    /// key -> CBOR-decoded wire value (see ImageMarshal); assumes
    /// ownership questions away by caching the decoded object.
    void addBinding(const std::string &key, PyObject *value);
    bool lookup(const std::string &key, Py::Object &out) const;

    uint64_t ownerHandle() const
    {
        return ownerHandle_;
    }

    static EvalTransaction *current();

private:
    App::Document doc_;
    App::DocumentObject owner_;
    uint64_t ownerHandle_ = 0;
    std::map<std::string, Py::Object> bindings_;
};

/// One-time Base::Type registrations for the classes above plus the
/// Expression node hierarchy (the image's equivalent of the
/// Application.cpp init block, seam S10).
void initCoreTypes();

/** Resolve a spreadsheet range alias to a cell address via the
 * resolve_alias bridge op on the current transaction's owner handle
 * (RangeExpression::getRange's mid-eval reach-back -- the one
 * identifier-resolution step the bindings pack cannot pre-know).
 * Throws Base::ExpressionError when no handle/bridge is available or
 * the host refuses. */
std::string resolveAlias(const std::string &alias);

}  // namespace Fcx

#endif  // APP_FCX_DOCUMENT_H
