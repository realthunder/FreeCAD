/***************************************************************************
 *   Copyright (c) 2019 Abdullah Tahiri <abdullah.tahiri.yo@gmail.com>     *
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

#include <Base/Reader.h>
#include <Base/Writer.h>

#include "ExternalGeometryExtension.h"
#include "ExternalGeometryExtensionPy.h"


using namespace Sketcher;

//---------- Geometry Extension

namespace {
// One map per writer being exported through; Save runs on the main thread
// and an export writer lives for one Save, so a plain map keyed by the
// writer is enough.
std::unordered_map<const Base::Writer*, std::unordered_map<std::string, int>>& exportIndexMaps()
{
    static std::unordered_map<const Base::Writer*, std::unordered_map<std::string, int>> maps;
    return maps;
}
}

ExternalGeometryExtension::ExportRefIndex::ExportRefIndex(
    const Base::Writer& w, std::unordered_map<std::string, int> indexByRef)
    : writer(&w)
{
    exportIndexMaps()[writer] = std::move(indexByRef);
}

ExternalGeometryExtension::ExportRefIndex::~ExportRefIndex()
{
    exportIndexMaps().erase(writer);
}

int ExternalGeometryExtension::exportRefIndex(const Base::Writer& writer, const std::string& ref)
{
    auto& maps = exportIndexMaps();
    if (maps.empty() || ref.empty())
        return -1;
    auto it = maps.find(&writer);
    if (it == maps.end())
        return -1;
    auto found = it->second.find(ref);
    return found == it->second.end() ? -1 : found->second;
}

constexpr std::array<const char*, ExternalGeometryExtension::NumFlags>
    ExternalGeometryExtension::flag2str;

TYPESYSTEM_SOURCE(Sketcher::ExternalGeometryExtension, Part::GeometryMigrationPersistenceExtension)

void ExternalGeometryExtension::copyAttributes(Part::GeometryExtension* cpy) const
{
    Part::GeometryPersistenceExtension::copyAttributes(cpy);

    static_cast<ExternalGeometryExtension*>(cpy)->Ref = this->Ref;
    static_cast<ExternalGeometryExtension*>(cpy)->RefIndex = this->RefIndex;
    static_cast<ExternalGeometryExtension*>(cpy)->RefElement = this->RefElement;
    static_cast<ExternalGeometryExtension*>(cpy)->Flags = this->Flags;
}

void ExternalGeometryExtension::restoreAttributes(Base::XMLReader& reader)
{
    Part::GeometryPersistenceExtension::restoreAttributes(reader);

    Ref = reader.getAttribute("Ref", "");
    RefIndex = reader.getAttributeAsInteger("RefIndex", "-1");
    RefElement = reader.getAttribute("RefElement", "");
    Flags = FlagType(reader.getAttributeAsUnsigned("Flags", "0"));
}

void ExternalGeometryExtension::saveAttributes(Base::Writer& writer) const
{
    Part::GeometryPersistenceExtension::saveAttributes(writer);
    // For compatibility with upstream FreeCAD, always save 'Ref' and 'Flags'.
    // if (Ref.size())
        writer.Stream() << "\" Ref=\"" << Base::Persistence::encodeAttribute(Ref);
    // if (Flags.any())
        writer.Stream() << "\" Flags=\"" << Flags.to_ulong();
    int refIndex = RefIndex >= 0 ? RefIndex : exportRefIndex(writer, Ref);
    if (refIndex >= 0)
        writer.Stream() << "\" RefIndex=\"" << refIndex;
    if (!RefElement.empty())
        writer.Stream() << "\" RefElement=\"" << Base::Persistence::encodeAttribute(RefElement);
}

void ExternalGeometryExtension::preSave(Base::Writer &writer) const
{
    if (Ref.size())
        writer.Stream() << " ref=\"" << Base::Persistence::encodeAttribute(Ref)  << "\"";
    int refIndex = RefIndex >= 0 ? RefIndex : exportRefIndex(writer, Ref);
    if (refIndex >= 0)
        writer.Stream() << " refIndex=\"" << refIndex << "\"";
    if (!RefElement.empty())
        writer.Stream() << " refElement=\"" << Base::Persistence::encodeAttribute(RefElement) << "\"";
    if (Flags.any())
        writer.Stream() << " flags=\"" << Flags.to_ulong() << "\"";
}

std::unique_ptr<Part::GeometryExtension> ExternalGeometryExtension::copy() const
{
    auto cpy = std::make_unique<ExternalGeometryExtension>();

    copyAttributes(cpy.get());

#if defined(__GNUC__) && (__GNUC__ <= 4)
    return std::move(cpy);
#else
    return cpy;
#endif
}

PyObject* ExternalGeometryExtension::getPyObject()
{
    return new ExternalGeometryExtensionPy(new ExternalGeometryExtension(*this));
}

bool ExternalGeometryExtension::getFlagsFromName(std::string str,
                                                 ExternalGeometryExtension::Flag& flag)
{
    auto pos = std::find_if(ExternalGeometryExtension::flag2str.begin(),
                            ExternalGeometryExtension::flag2str.end(),
                            [str](const char* val) {
                                return strcmp(val, str.c_str()) == 0;
                            });

    if (pos != ExternalGeometryExtension::flag2str.end()) {
        int index = std::distance(ExternalGeometryExtension::flag2str.begin(), pos);

        flag = static_cast<ExternalGeometryExtension::Flag>(index);
        return true;
    }

    return false;
}
