/* Host-facility stubs for the sandbox image.  These exist host-side in
 * TUs the image cannot compile (Console.cpp is Qt-coupled); the image
 * needs only enough of them to satisfy the link and route diagnostics
 * to stderr, which the host maps to its own log.
 */
#include <cstdio>
#include <map>
#include <sstream>

#include <Base/Console.h>
#include <Base/Interpreter.h>
#include <Base/Quantity.h>
#include <Base/UnitsApi.h>

namespace Base
{

// --- ConsoleObserver.cpp ---------------------------------------------

static int _TracePySrc;
TracePySrc::TracePySrc(bool enable)
    : enabled(enable)
{
    if (enabled)
        ++_TracePySrc;
}
TracePySrc::~TracePySrc()
{
    if (enabled)
        --_TracePySrc;
}

// --- Console.cpp ------------------------------------------------------

ConsoleSingleton* ConsoleSingleton::_pcSingleton = nullptr;

ConsoleSingleton::ConsoleSingleton()
    : _defaultLogLevel(FC_LOGLEVEL_MSG)
{}

ConsoleSingleton::~ConsoleSingleton() = default;

ConsoleSingleton& ConsoleSingleton::Instance()
{
    if (!_pcSingleton)
        _pcSingleton = new ConsoleSingleton();
    return *_pcSingleton;
}

void ConsoleSingleton::Destruct()
{
    delete _pcSingleton;
    _pcSingleton = nullptr;
}

void ConsoleSingleton::postEvent(ConsoleSingleton::FreeCAD_ConsoleMsgType type,
                                 IntendedRecipient recipient,
                                 ContentType content,
                                 const std::string& notifiername,
                                 const std::string& msg)
{
    LogStyle style = LogStyle::Log;
    switch (type) {
        case MsgType_Txt:
            style = LogStyle::Message;
            break;
        case MsgType_Wrn:
            style = LogStyle::Warning;
            break;
        case MsgType_Err:
            style = LogStyle::Error;
            break;
        default:
            break;
    }
    notifyPrivate(style, recipient, content, notifiername, msg);
}

void ConsoleSingleton::notifyPrivate(LogStyle category,
                                     IntendedRecipient /*recipient*/,
                                     ContentType /*content*/,
                                     const std::string& notifiername,
                                     const std::string& msg)
{
    const char* prefix = "log";
    switch (category) {
        case LogStyle::Warning:
            prefix = "warning";
            break;
        case LogStyle::Error:
            prefix = "error";
            break;
        case LogStyle::Message:
            prefix = "message";
            break;
        default:
            break;
    }
    if (notifiername.empty())
        fprintf(stderr, "fcx %s: %s", prefix, msg.c_str());
    else
        fprintf(stderr, "fcx %s [%s]: %s", prefix, notifiername.c_str(),
                msg.c_str());
}

int* ConsoleSingleton::GetLogLevel(const char* tag, bool create)
{
    static std::map<std::string, int> levels;
    auto it = levels.find(tag);
    if (it != levels.end())
        return &it->second;
    if (!create)
        return nullptr;
    return &levels.emplace(tag, -1).first->second;
}

void ConsoleSingleton::Refresh() {}

std::stringstream& LogLevel::prefix(std::stringstream& str, const char* src, int line)
{
    if (print_tag)
        str << "<" << tag << "> ";
    if (print_src > 0)
        str << src << "(" << line << "): ";
    return str;
}

// --- UnitsApi.cpp (Qt-coupled host-side; the image always speaks the
// internal scheme -- quantities cross the wire by value, so this only
// shapes an in-image UserString) ---------------------------------------

int UnitsApi::getDecimals()
{
    return 2;
}

std::string UnitsApi::schemaTranslate(const Base::Quantity& quant,
                                      double& factor,
                                      std::string& unitString)
{
    factor = 1.0;
    unitString = quant.getUnit().getString();
    std::stringstream str;
    str << quant.getValue();
    if (!unitString.empty())
        str << " " << unitString;
    return str.str();
}

// --- Interpreter.cpp exception slice ---------------------------------
// The core TUs convert Python errors to C++ via Base::PyException.  The
// host implementation runs through PyTools/ExceptionFactory to rebuild
// registered FreeCAD exception types; in-image errors only need to reach
// the fcx_call boundary as {exc, msg}, so this is a lean equivalent
// with the same fetch-and-clear contract.

PyException::PyException(const Py::Object& obj)
{
    _sErrMsg = obj.as_string();
    // Type objects are immortal for our purposes; keep no reference,
    // matching the host implementation's book-keeping choice.
    _exceptionType = reinterpret_cast<PyObject*>(obj.ptr()->ob_type);
    _errorType = obj.ptr()->ob_type->tp_name;
}

PyException::PyException()
    : _exceptionType(nullptr)
{
    PyObject* type = nullptr;
    PyObject* value = nullptr;
    PyObject* trace = nullptr;
    PyErr_Fetch(&type, &value, &trace);
    PyErr_NormalizeException(&type, &value, &trace);
    if (type) {
        _exceptionType = type;
        _errorType = reinterpret_cast<PyTypeObject*>(type)->tp_name;
    }
    if (value) {
        PyObject* msg = PyObject_Str(value);
        if (msg) {
            const char* text = PyUnicode_AsUTF8(msg);
            if (text)
                _sErrMsg = text;
            Py_DECREF(msg);
        }
    }
    Py_XDECREF(type);
    Py_XDECREF(value);
    Py_XDECREF(trace);
    PyErr_Clear();
}

PyException::~PyException() noexcept = default;

void PyException::ThrowException()
{
    PyException myexcp;
    myexcp.raiseException();
}

void PyException::raiseException()
{
    throw *this;
}

void PyException::ReportException() const
{
    fprintf(stderr, "%s%s: %s\n", _stackTrace.c_str(), _errorType.c_str(),
            what());
}

void PyException::setPyException() const
{
    std::stringstream str;
    str << getStackTrace() << getErrorType() << ": " << what();
    PyObject* type = _exceptionType;
    if (!type || !PyExceptionClass_Check(type))
        type = PyExc_RuntimeError;
    PyErr_SetString(type, str.str().c_str());
}

}  // namespace Base
