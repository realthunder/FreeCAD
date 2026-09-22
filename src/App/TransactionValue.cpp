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
# include <limits>
# include <sstream>
#endif

#include <Base/Console.h>
#include <Base/Reader.h>
#include <Base/Writer.h>

#include "TransactionValue.h"
#include "Document.h"
#include "FileBlobManager.h"
#include "Property.h"

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
    explicit CaptureWriter(const Document& doc, CapturedValue& out)
        : _out(out)
    {
        setFileVersion(2);
        setForceXML(0);
        setSplitXML(false);
        setSchemaVersion(static_cast<int>(doc.getSaveSchemaVersion()));
        if (doc.PreferBinary.getValue()) {
            setMode("BinaryBrep");
            setPreferBinary(true);
        }
        else {
            setPreferBinary(false);
        }
        _xml.precision(std::numeric_limits<double>::digits10 + 1);
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

CapturedValue App::captureValue(const Document& doc, const Base::Persistence& what)
{
    CapturedValue v;
    CaptureWriter writer(doc, v);
    try {
        what.Save(writer);
        writer.writeFiles();
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
    return v;
}

void App::restoreValue(Property& prop, const CapturedValue& value)
{
    // The fragment is one element; Property::Restore expects to read it
    // from inside an open parent, so wrap it the way Document.xml does.
    std::istringstream xml("<?xml version='1.0' encoding='utf-8'?>\n<Value>\n"
                           + value.fragment + "</Value>\n");
    Base::XMLReader reader("Value.xml", xml);
    reader.FileVersion = 2;
    if (auto doc = prop.getContainer() ? prop.getContainer()->getOwnerDocument() : nullptr)
        reader.DocumentSchema = static_cast<int>(doc->getSaveSchemaVersion());
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
}

std::string App::hashBytes(const std::string& bytes)
{
    return FileBlobManager::hashBytes(bytes);
}
