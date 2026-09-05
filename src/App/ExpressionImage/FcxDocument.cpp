/* Implementation of the S1 DocumentAdapter (FcxDocument.h): the
 * bindings-pack-backed document world the ExpressionCore TUs resolve
 * against inside the sandbox image. */

#include "FcxDocument.h"

#include <cstring>

#include <nlohmann/json.hpp>

#include <Base/Exception.h>
#include <Base/QuantityPy.h>

#include <App/Expression.h>
#include <App/ExpressionParser.h>

#include "FcxWire.h"
#include "ImageMarshal.h"

using namespace App;

TYPESYSTEM_SOURCE(App::Property, Base::BaseClass)
TYPESYSTEM_SOURCE(App::PropertyString, App::Property)
TYPESYSTEM_SOURCE(App::PropertyBool, App::Property)
TYPESYSTEM_SOURCE(App::PropertyQuantity, App::Property)
TYPESYSTEM_SOURCE(App::PropertyFloat, App::Property)
TYPESYSTEM_SOURCE(App::PropertyInteger, App::Property)
TYPESYSTEM_SOURCE(App::PropertyContainer, Base::BaseClass)
TYPESYSTEM_SOURCE(App::DocumentObject, App::PropertyContainer)

std::string Property::getFullName() const
{
    auto obj = freecad_dynamic_cast<DocumentObject>(container_);
    if (obj)
        return obj->getFullName() + "." + name_;
    return name_;
}

PyObject *Property::getPyObject()
{
    Py::Object v = value_;
    v.increment_reference_count();
    return v.ptr();
}

void Property::setPyObject(PyObject *obj)
{
    value_ = Py::Object(obj);
}

App::any Property::getPathValue(const ObjectIdentifier &path) const
{
    // The host's typed properties interpret the sub-path (units on
    // quantity constraints etc.); pack values already carry their final
    // shape, so the leaf value answers any path that reaches it.
    (void)path;
    return pyObjectToAny(value_, false);
}

PyObject *PropertyString::getPyObject()
{
    return PyUnicode_FromString(str_.c_str());
}

void PropertyString::setPyObject(PyObject *obj)
{
    if (obj && PyUnicode_Check(obj)) {
        str_ = PyUnicode_AsUTF8(obj);
        return;
    }
    throw Base::TypeError("PropertyString expects a string");
}

// ---------------------------------------------------------------------

std::string DocumentObject::getFullName() const
{
    std::string name;
    if (document_) {
        name = document_->getName();
        name += "#";
    }
    name += name_;
    return name;
}

/// Wrap a pack value in the typed property the aggregate readers
/// (FunctionExpression sum/min/... over ranges) dynamic-cast for.
static std::unique_ptr<Property> wrapPackValue(const Py::Object &value)
{
    std::unique_ptr<Property> prop;
    PyObject *ptr = value.ptr();
    if (PyObject_TypeCheck(ptr, &Base::QuantityPy::Type)) {
        auto qp = std::make_unique<PropertyQuantity>();
        qp->quantity_ = *static_cast<Base::QuantityPy *>(ptr)->getQuantityPtr();
        prop = std::move(qp);
    }
    else if (PyFloat_Check(ptr)) {
        auto fp = std::make_unique<PropertyFloat>();
        fp->float_ = PyFloat_AsDouble(ptr);
        prop = std::move(fp);
    }
    else if (PyLong_Check(ptr) && !PyBool_Check(ptr)) {
        auto ip = std::make_unique<PropertyInteger>();
        ip->int_ = PyLong_AsLong(ptr);
        prop = std::move(ip);
    }
    else if (PyUnicode_Check(ptr)) {
        auto sp = std::make_unique<PropertyString>();
        sp->str_ = PyUnicode_AsUTF8(ptr);
        prop = std::move(sp);
    }
    else {
        prop = std::make_unique<Property>();
    }
    prop->value_ = value;
    return prop;
}

Property *DocumentObject::getPropertyByName(const char *name) const
{
    if (!name || !name[0])
        return nullptr;

    auto it = props_.find(name);
    if (it != props_.end())
        return it->second.get();

    auto tx = Fcx::EvalTransaction::current();
    if (!tx)
        return nullptr;

    // Reentry guard: building the canonical key below runs
    // ObjectIdentifier::toString -> resolve -> getPropertyByName again.
    static bool building = false;
    if (building)
        return nullptr;
    building = true;
    std::string key;
    try {
        ObjectIdentifier path(this, name);
        key = path.toString();
    }
    catch (...) {
        building = false;
        return nullptr;
    }
    building = false;

    Py::Object value;
    if (!tx->lookup(key, value))
        return nullptr;

    auto prop = wrapPackValue(value);
    prop->name_ = name;
    prop->container_ = const_cast<DocumentObject *>(this);
    auto res = props_.emplace(name, std::move(prop));
    return res.first->second.get();
}

PyObject *DocumentObject::getPyObject()
{
    auto tx = Fcx::EvalTransaction::current();
    if (!tx || tx->owner() != this || !tx->ownerHandle())
        throw Base::RuntimeError(
            "sandbox image: object has no host handle in this evaluation");
    nlohmann::json h;
    h[FcxWire::TagKey] = FcxWire::TagHandle;
    h["id"] = tx->ownerHandle();
    h["ty"] = "obj";
    if (!tx->ownerFacade().empty())
        h["fc"] = tx->ownerFacade();
    // the durable key (FcxWire OpResolve), as a host-minted handle
    // carries it: the transaction's document and owner names
    if (document_)
        h["k"] = nlohmann::json::array({document_->name_, name_});
    PyObject *proxy = FcxImage::decodeValue(h);
    if (!proxy)
        throw Base::PyException();
    return proxy;
}

DocumentObject *DocumentObject::getSubObject(const char *, PyObject **,
                                             Base::Matrix4D *, bool, int) const
{
    // Sub-object walks need live geometry trees; the host pre-resolves
    // identifiers that cross them (docs/ExpressionSandbox.md 7.3).
    return nullptr;
}

DocumentObject *DocumentObject::getLinkedObject(bool, Base::Matrix4D *, bool,
                                                int) const
{
    return const_cast<DocumentObject *>(this);
}

// ---------------------------------------------------------------------

DocumentObject *Document::getObject(const char *name) const
{
    auto tx = Fcx::EvalTransaction::current();
    if (tx && tx->owner()->getDocument() == this && name
        && tx->owner()->name_ == name)
        return tx->owner();
    return nullptr;
}

std::vector<DocumentObject *> Document::getObjects() const
{
    std::vector<DocumentObject *> objs;
    auto tx = Fcx::EvalTransaction::current();
    if (tx && tx->owner()->getDocument() == this)
        objs.push_back(tx->owner());
    return objs;
}

Document *Application::getDocument(const char *name) const
{
    auto tx = Fcx::EvalTransaction::current();
    if (tx && name && tx->owner()->getDocument()
        && strcmp(tx->owner()->getDocument()->getName(), name) == 0)
        return tx->owner()->getDocument();
    return nullptr;
}

std::vector<Document *> Application::getDocuments() const
{
    std::vector<Document *> docs;
    auto tx = Fcx::EvalTransaction::current();
    if (tx && tx->owner()->getDocument())
        docs.push_back(tx->owner()->getDocument());
    return docs;
}

Application &App::GetApplication()
{
    static Application app;
    return app;
}

// The persistence-side import check lives in the host's ops TU
// (ExpressionDocumentOps.cpp); nothing is being imported in-image.
void ObjectIdentifier::String::checkImport(const App::DocumentObject *,
                                           const App::DocumentObject *,
                                           String *)
{
}

// ---------------------------------------------------------------------

namespace Fcx
{

static EvalTransaction *_current;

EvalTransaction::EvalTransaction(const std::string &docName,
                                 const std::string &objName,
                                 uint64_t ownerHandle,
                                 const std::string &ownerFacade)
    : ownerHandle_(ownerHandle)
    , ownerFacade_(ownerFacade)
{
    doc_.name_ = docName.empty() ? "sandbox" : docName;
    doc_.Label.str_ = doc_.name_;
    owner_.name_ = objName.empty() ? "owner" : objName;
    owner_.document_ = &doc_;
    owner_.Label.str_ = owner_.name_;
    _current = this;
}

EvalTransaction::~EvalTransaction()
{
    if (_current == this)
        _current = nullptr;
}

EvalTransaction *EvalTransaction::current()
{
    return _current;
}

void EvalTransaction::addBinding(const std::string &key, PyObject *value)
{
    bindings_[key] = Py::Object(value);
}

void EvalTransaction::addBindingError(const std::string &key,
                                      const std::string &excType,
                                      const std::string &message)
{
    bindErrors_[key] = std::make_pair(excType, message);
}

namespace
{
/// Re-raise the host's failure as the same KIND of exception, not just
/// the same text: dispatchEvalExpr converts whatever escapes back into
/// the wire's {exc,msg}, so the type survives the round trip too.
[[noreturn]] void raiseBindingError(const std::string &excType,
                                    const std::string &message)
{
    if (excType == "TypeError")
        throw Base::TypeError(message);
    if (excType == "ValueError")
        throw Base::ValueError(message);
    if (excType == "NameError")
        throw Base::NameError(message);
    if (excType == "AttributeError")
        throw Base::AttributeError(message);
    if (excType == "IndexError")
        throw Base::IndexError(message);
    throw Base::RuntimeError(message);
}
}  // namespace

bool EvalTransaction::lookup(const std::string &key, Py::Object &out) const
{
    auto err = bindErrors_.find(key);
    if (err != bindErrors_.end())
        raiseBindingError(err->second.first, err->second.second);
    auto it = bindings_.find(key);
    if (it == bindings_.end())
        return false;
    out = it->second;
    return true;
}

std::string resolveAlias(const std::string &alias)
{
    auto tx = EvalTransaction::current();
    if (!tx || !tx->ownerHandle())
        throw Base::ExpressionError(
            "sandbox image: no owner handle to resolve range alias against");
    nlohmann::json req;
    req["op"] = FcxWire::OpResolveAlias;
    req["h"] = tx->ownerHandle();
    req["a"] = alias;
    nlohmann::json reply;
    if (!FcxImage::hostOp(req, reply)) {
        if (PyErr_Occurred())
            PyErr_Clear();
        throw Base::ExpressionError("sandbox image: host bridge unavailable");
    }
    if (!reply.value("ok", false)) {
        std::string msg = reply.value("exc", std::string("Exception")) + ": "
            + reply.value("msg", std::string());
        throw Base::ExpressionError(msg.c_str());
    }
    auto val = reply.find("val");
    if (val == reply.end() || !val->is_string())
        throw Base::ExpressionError("sandbox image: bad resolve_alias reply");
    return val->get<std::string>();
}

void initCoreTypes()
{
    static bool done;
    if (done)
        return;
    done = true;

    Base::Type::init();
    Base::BaseClass::init();
    Base::Exception::init();
    Base::AbortException::init();

    App::Property::init();
    App::PropertyString::init();
    App::PropertyBool::init();
    App::PropertyQuantity::init();
    App::PropertyFloat::init();
    App::PropertyInteger::init();
    App::PropertyContainer::init();
    App::DocumentObject::init();

    App::Expression::init();
    App::UnitExpression::init();
    App::NumberExpression::init();
    App::ConstantExpression::init();
    App::OperatorExpression::init();
    App::VariableExpression::init();
    App::FunctionExpression::init();
    App::CallableExpression::init();
    App::ConditionalExpression::init();
    App::StringExpression::init();
    App::RangeExpression::init();
    App::PyObjectExpression::init();
    App::ListExpression::init();
    App::ComprehensionExpression::init();
    App::TupleExpression::init();
    App::DictExpression::init();
    App::AssignmentExpression::init();
    App::BaseStatement::init();
    App::PseudoStatement::init();
    App::JumpStatement::init();
    App::IfStatement::init();
    App::WhileStatement::init();
    App::ForStatement::init();
    App::SimpleStatement::init();
    App::Statement::init();
    App::LambdaExpression::init();
    App::FunctionStatement::init();
    App::DelStatement::init();
    App::ScopeStatement::init();
    App::TryStatement::init();
    App::ImportStatement::init();
    App::IDictExpression::init();
}

}  // namespace Fcx
