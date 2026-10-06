/***************************************************************************
 *   Copyright (c) 2026 Zheng Lei (realthunder) <realthunder.dev@gmail.com>*
 *                                                                         *
 *   This file is part of the FreeCAD CAx development system.              *
 *                                                                         *
 *   This library is free software; you can redistribute it and/or         *
 *   modify it under the terms of the GNU Library General Public           *
 *   License as published by the Free Software Foundation; either          *
 *   version 2 of the License, or (at your option) any later version.      *
 *                                                                         *
 *   This library  is distributed in the hope that it will be useful,      *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU Library General Public License for more details.                  *
 *                                                                         *
 *   You should have received a copy of the GNU Library General Public     *
 *   License along with this library; see the file COPYING.LIB. If not,    *
 *   write to the Free Software Foundation, Inc., 59 Temple Place,         *
 *   Suite 330, Boston, MA  02111-1307, USA                                *
 *                                                                         *
 ***************************************************************************/

#include "PreCompiled.h"

#ifndef _PreComp_
# include <algorithm>
# include <limits>
# include <sstream>
#endif

#include <Base/Console.h>
#include <Base/Reader.h>
#include <Base/Writer.h>

#include "TransactionValue.h"
#include "Application.h"
#include "Document.h"
#include "DocumentObject.h"
#include "ElementMap.h"
#include "ElementNamingUtils.h"
#include "ExpressionParser.h"
#include "FileBlobManager.h"
#include "Property.h"
#include "PropertyExpressionEngine.h"
#include "StringHasher.h"

FC_LOG_LEVEL_INIT("App", true, true)

using namespace App;

namespace {

/** A writer that keeps the XML and every requested file in memory,
 * configured the way a document save configures its writer so the bytes
 * are the bytes a save would produce.
 */
class CaptureWriter : public Base::Writer
{
public:
    explicit CaptureWriter(const CaptureConfig& config, CapturedValue& out)
        : _out(out)
    {
        // The archive's configuration (Document::save with an archive:
        // the writer's defaults, file version 1, XML not forced), so that
        // a saved part and a captured value are the same bytes.
        setFileVersion(1);
        setForceXML(0);
        setSplitXML(false);
        setSchemaVersion(config.schema);
        // A property holding the file its value was last written to may
        // answer with that file's hash instead of its content (decision
        // 6b); the log holds the file through BlobReferrerProperty.
        setMode("BlobRef");
        if (config.preferBinary) {
            setMode("BinaryBrep");
            setPreferBinary(true);
        }
        else {
            setPreferBinary(false);
        }
        _xml.precision(std::numeric_limits<double>::digits10 + 1);
        _xml.setf(std::ios::fixed, std::ios::floatfield);
    }

    std::ostream& Stream() override { return _current ? *_current : _xml; }

    void writeFiles() override
    {
        // While loop: an attachment may request another (a hasher table
        // behind an element map).
        size_t index = 0;
        while (index < FileList.size()) {
            FileEntry entry = FileList[index++];
            std::ostringstream out;
            out.precision(std::numeric_limits<double>::digits10 + 1);
            out.setf(std::ios::fixed, std::ios::floatfield);
            _current = &out;
            putNextEntry(entry.FileName.c_str());
            indent = 0;
            indBuf[0] = 0;
            entry.Object->SaveDocFile(*this);
            _current = nullptr;
            _out.attachments.push_back({entry.FileName, out.str()});
        }
        _out.fragment = _xml.str();
    }

private:
    CapturedValue& _out;
    std::ostringstream _xml;
    std::ostringstream* _current {nullptr};
};

} // namespace

// The log's schema, not the document's: everything the log holds is the
// current schema, whatever the document is saved as (sec 27.62).
App::CaptureConfig::CaptureConfig(const Document& doc)
    : schema(static_cast<int>(Document::getCurrentSchemaVersion()))
    , preferBinary(doc.PreferBinary.getValue())
    , blobs(&doc.getFileBlobManager())
    , hasher(doc.getHasher().get())
    , document(&doc)
{
}

namespace {
thread_local const App::Document* captureDocument = nullptr;
}

const App::Document* App::capturingDocument()
{
    return captureDocument;
}

CapturedValue App::captureValue(const Document& doc, const Base::Persistence& what)
{
    return captureValue(CaptureConfig(doc), what);
}

CapturedValue App::captureValue(const CaptureConfig& config, const Base::Persistence& what)
{
    CapturedValue v;
    CaptureWriter writer(config, v);
    // What the Save notes goes to the value, not to the save set of the
    // document's next save (sec 23.16).
    BlobRecorder recorder(config.blobs);
    // An element map lists the ids of the file's hasher it uses, whatever a
    // save on the main thread is marking meanwhile (sec 27.49), and the
    // value records them (sec 27.50 item 4).
    StringIDCollector strings(config.hasher);
    // Element maps numbered for this value alone (sec 27.67).
    Data::ElementMapIdScope mapIds;
    const Document* outer = captureDocument;
    captureDocument = config.document;
    try {
        what.Save(writer);
        writer.writeFiles();
        v.blobs = recorder.blobs();
        v.stringIds = strings.sortedIds();
        v.ok = true;
    }
    catch (Base::Exception& e) {
        FC_WARN("transaction value: serialise failed: " << e.what());
    }
    catch (std::exception& e) {
        FC_WARN("transaction value: serialise failed: " << e.what());
    }
    catch (...) {
        FC_WARN("transaction value: serialise failed");
    }
    captureDocument = outer;
    return v;
}

namespace App
{
/** A restore from a log value is a change to a live property, not the load
 * of a new one: it must be recorded in the open transaction and notify its
 * container like any setValue(). Most Restore()s call setValue() and do it
 * themselves; one that only names a blob by hash (PropertyFileIncluded,
 * served through assignRestoredBlob()) does neither, so the whole restore is
 * bracketed. Nested calls are harmless: the first aboutToSetValue() takes
 * the before copy, and a second touch changes nothing.
 */
class PropertyValueRestorer
{
public:
    explicit PropertyValueRestorer(Property& prop) : _prop(prop) { _prop.aboutToSetValue(); }
    ~PropertyValueRestorer()
    {
        try {
            _prop.hasSetValue();
        }
        catch (Base::Exception& e) {
            e.ReportException();
        }
        catch (...) {
        }
    }

private:
    Property& _prop;
};
} // namespace App

namespace {
thread_local App::RestoreNames* restoreNames = nullptr;

/// The reader a value is restored from: names go through the scope of
/// RestoreNames, when there is one.
class ValueReader: public Base::XMLReader
{
public:
    ValueReader(const char* name, std::istream& in)
        : Base::XMLReader(name, in)
    {}
    const char* getName(const char* name) const override
    {
        return restoreNames ? restoreNames->map(name) : name;
    }
    bool doNameMapping() const override
    {
        return restoreNames && !restoreNames->empty();
    }
};
}

/// What an expression parsed while the scope lives maps its names by.
struct App::RestoreNames::Importing
{
    std::istringstream xml {"<?xml version='1.0' encoding='utf-8'?>\n<Value/>\n"};
    ValueReader reader {"Names.xml", xml};
    ExpressionParser::ExpressionImporter importer {reader};
};

App::RestoreNames::RestoreNames(std::map<std::string, std::string> names)
    : _names(std::move(names))
    , _outer(restoreNames)
{
    restoreNames = this;
    if (!_names.empty() && !ExpressionParser::ExpressionImporter::reader())
        _importing = std::make_unique<Importing>();
}

App::RestoreNames::~RestoreNames()
{
    _importing.reset();
    restoreNames = _outer;
}

const App::RestoreNames* App::RestoreNames::current()
{
    return restoreNames;
}

const char* App::RestoreNames::map(const char* name) const
{
    auto it = _names.find(name);
    return it == _names.end() ? name : it->second.c_str();
}

namespace {
thread_local App::RestoreStrings* restoreStrings = nullptr;
thread_local const std::string* restoreTarget = nullptr;
thread_local const App::RestoreMinted* restoreMinted = nullptr;
}

App::RestoreStrings::Target::Target(const std::string& object)
    : _outer(restoreTarget)
{
    restoreTarget = &object;
}

App::RestoreStrings::Target::~Target()
{
    restoreTarget = _outer;
}

App::RestoreMinted::RestoreMinted(Document& doc, const Maps& maps)
    : _doc(doc)
    , _maps(maps)
    , _outer(restoreMinted)
{
    restoreMinted = this;
}

App::RestoreMinted::~RestoreMinted()
{
    restoreMinted = _outer;
}

const App::RestoreMinted* App::RestoreMinted::current()
{
    return restoreMinted;
}

bool App::RestoreMinted::byId(long id, std::string& name) const
{
    auto it = _maps.find(id);
    if (it == _maps.end() || it->second.empty())
        return false;
    const DocumentObject* obj = _doc.getObjectByID(id);
    return obj && obj->importMintedName(name, it->second);
}

bool App::RestoreMinted::byName(const std::string& object, std::string& name) const
{
    const DocumentObject* obj = _doc.getObject(object.c_str());
    return obj && byId(obj->getID(), name);
}

App::RestoreStrings::RestoreStrings(StringHasherRef from, StringHasherRef to)
    : _from(std::move(from))
    , _to(std::move(to))
    , _outer(restoreStrings)
{
    // Nothing to read through when the two are one table, or either is
    // missing: current() then answers the scope outside.
    if (_from && _to && _from != _to)
        restoreStrings = this;
}

App::RestoreStrings::~RestoreStrings()
{
    if (restoreStrings == this)
        restoreStrings = _outer;
}

App::RestoreStrings* App::RestoreStrings::current()
{
    return restoreStrings;
}

std::string App::RestoreStrings::element(const char* element)
{
    const char* mapped = element ? Data::isMappedElement(element) : nullptr;
    if (!mapped)
        return element ? std::string(element) : std::string();
    // A mapped name has no dot: the last one starts the indexed name.
    const char* dot = strrchr(mapped, '.');
    QByteArray text(mapped, dot ? static_cast<int>(dot - mapped) : static_cast<int>(strlen(mapped)));
    QByteArray res;
    QVector<StringIDRef> sids;
    if (!_to->importText(text, *_from, res, &sids, _memo))
        return dot ? std::string(dot + 1) : std::string();
    // An element of the object the path ends at, named by a number that
    // object gave (sec 31.14): one built on a string says whose it is
    // itself, and was read so as its strings were taken in.
    if (restoreMinted && restoreTarget && !restoreTarget->empty() && res.indexOf('#') < 0) {
        std::string name(res.constData(), res.size());
        if (restoreMinted->byName(*restoreTarget, name))
            res = QByteArray(name.c_str(), static_cast<int>(name.size()));
    }
    std::string out = Data::elementMapPrefix();
    out.append(res.constData(), res.size());
    if (dot)
        out += dot;
    _held[out] = std::move(sids);
    return out;
}

std::string App::RestoreStrings::sub(const std::string& sub)
{
    const char* element = Data::findElementName(sub.c_str());
    if (!element || !Data::isMappedElement(element))
        return sub;
    // The object the element is of: the last the path names, or else the
    // one the path starts from (Target).
    std::string owner;
    if (element > sub.c_str() + 1) {
        const std::size_t end = static_cast<std::size_t>(element - sub.c_str()) - 1;
        const std::size_t dot = end ? sub.rfind('.', end - 1) : std::string::npos;
        owner = sub.substr(dot == std::string::npos ? 0 : dot + 1,
                           end - (dot == std::string::npos ? 0 : dot + 1));
    }
    const std::string* outer = restoreTarget;
    if (!owner.empty())
        restoreTarget = &owner;
    const std::string translated = this->element(element);
    restoreTarget = outer;
    std::string out = sub.substr(0, element - sub.c_str()) + translated;
    auto it = _held.find(translated);
    if (it != _held.end())
        _held[out] = it->second;
    return out;
}

QVector<App::StringIDRef> App::RestoreStrings::held(const std::string& text) const
{
    auto it = _held.find(text);
    return it == _held.end() ? QVector<StringIDRef>() : it->second;
}

long App::RestoreStrings::id(long id)
{
    StringIDRef theirs = _from->getID(id);
    if (!theirs)
        return 0;
    StringIDRef here = _to->importID(theirs, &_memo);
    if (!here)
        return 0;
    _ids.push_back(here);   // held until the scope ends
    return here.value();
}

void App::restoreValue(Property& prop, const CapturedValue& value)
{
    // The value's element maps read by the ids it wrote, not the ones some
    // earlier restore left (sec 27.67).
    Data::ElementMapIdScope mapIds;
    PropertyValueRestorer bracket(prop);
    // The fragment is one element; Property::Restore expects to read it
    // from inside an open parent, so wrap it the way Document.xml does.
    std::istringstream xml("<?xml version='1.0' encoding='utf-8'?>\n<Value>\n"
                           + value.fragment + "</Value>\n");
    ValueReader reader("Value.xml", xml);
    reader.FileVersion = 1;   // what the capture writes under
    // Captured at the log's schema (CaptureConfig).
    reader.DocumentSchema = static_cast<int>(Document::getCurrentSchemaVersion());
    if (!reader.isValid())
        throw Base::RuntimeError("transaction value: fragment does not parse");
    prop.Restore(reader);
    // Whatever the property registered for, served by name from the
    // attachments, in registration order like every other reader.
    for (size_t i = 0; i < reader.getFileList().size(); ++i) {
        Base::XMLReader::FileEntry entry = reader.getFileList()[i];
        const CapturedValue::Attachment* found = nullptr;
        for (auto& a : value.attachments) {
            if (a.name == entry.FileName) {
                found = &a;
                break;
            }
        }
        if (!found) {
            FC_WARN("transaction value: attachment " << entry.FileName << " missing");
            continue;
        }
        std::istringstream bytes(found->bytes);
        Base::Reader in(bytes, entry.FileName, &reader);
        entry.Object->RestoreDocFile(in);
    }
    // What a document's restore does once every property is read (sec
    // 27.67): an expression engine only parks the expressions its Restore
    // read, and installs them here -- without it, every logged engine
    // value restored to nothing.
    if (auto batch = RestoreBatch::current())
        batch->defer(prop);
    else
        prop.afterRestore();
}

namespace {
thread_local App::RestoreBatch* restoreBatch = nullptr;
}

App::RestoreBatch::RestoreBatch()
    : _outer(restoreBatch)
{
    restoreBatch = this;
    _removed = GetApplication().signalRemoveDynamicProperty.connect(
        [this](const Property& prop) { forget(&prop); });
    _deleted = GetApplication().signalDeletedObject.connect([this](const DocumentObject& obj) {
        _props.erase(std::remove_if(_props.begin(), _props.end(),
                                    [&](Property* p) { return p->getContainer() == &obj; }),
                     _props.end());
    });
}

void App::RestoreBatch::defer(Property& prop)
{
    if (std::find(_props.begin(), _props.end(), &prop) == _props.end())
        _props.push_back(&prop);
}

void App::RestoreBatch::forget(const Property* prop)
{
    _props.erase(std::remove(_props.begin(), _props.end(), prop), _props.end());
}

App::RestoreBatch::~RestoreBatch()
{
    finish();
}

App::RestoreBatch* App::RestoreBatch::current()
{
    return restoreBatch;
}

void App::RestoreBatch::finish()
{
    if (_finished)
        return;
    _finished = true;
    // Unlinked first: an afterRestore() that restores a value runs it now.
    // Taken out one by one, so an afterRestore() that removes a property
    // yet to come is heard (forget()).
    restoreBatch = _outer;
    // The engines of the batch are installed one after another, and an
    // expression is refused where it closes a cycle. What one still holds
    // of the state being left can close one with what another brings: the
    // box followed the cylinder there, the cylinder follows the box here.
    // So each lets go first of what its value does not keep as it is.
    for (Property* prop : std::vector<Property*>(_props)) {
        if (std::find(_props.begin(), _props.end(), prop) == _props.end())
            continue;
        if (auto engine = freecad_dynamic_cast<PropertyExpressionEngine>(prop)) {
            try {
                engine->releaseBeforeRestore();
            }
            catch (Base::Exception& e) {
                FC_ERR("transaction value: before restore of " << prop->getFullName() << ": "
                       << e.what());
            }
        }
    }
    while (!_props.empty()) {
        Property* prop = _props.front();
        _props.erase(_props.begin());
        try {
            prop->afterRestore();
        }
        catch (Base::Exception& e) {
            FC_ERR("transaction value: after restore of " << prop->getFullName() << ": "
                   << e.what());
        }
        catch (std::exception& e) {
            FC_ERR("transaction value: after restore of " << prop->getFullName() << ": "
                   << e.what());
        }
    }
    _removed.disconnect();
    _deleted.disconnect();
}

namespace {
thread_local App::CaptureNames* captureNames = nullptr;
}

App::CaptureNames::CaptureNames(std::unordered_map<const DocumentObject*, std::string> names)
    : _names(std::move(names))
    , _outer(captureNames)
{
    captureNames = this;
}

App::CaptureNames::~CaptureNames()
{
    captureNames = _outer;
}

const std::string* App::CaptureNames::find(const DocumentObject* obj)
{
    for (auto scope = captureNames; scope; scope = scope->_outer) {
        auto it = scope->_names.find(obj);
        if (it != scope->_names.end())
            return &it->second;
    }
    return nullptr;
}

std::string App::hashBytes(const std::string& bytes)
{
    return FileBlobManager::hashBytes(bytes);
}
