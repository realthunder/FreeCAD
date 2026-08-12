// SPDX-License-Identifier: LGPL-2.1-or-later

/***************************************************************************
 *   Copyright (c) 2024 Werner Mayer <wmayer[at]users.sourceforge.net>     *
 *                                                                         *
 *   This file is part of FreeCAD.                                         *
 *                                                                         *
 *   FreeCAD is free software: you can redistribute it and/or modify it    *
 *   under the terms of the GNU Lesser General Public License as           *
 *   published by the Free Software Foundation, either version 2.1 of the  *
 *   License, or (at your option) any later version.                       *
 *                                                                         *
 *   FreeCAD is distributed in the hope that it will be useful, but        *
 *   WITHOUT ANY WARRANTY; without even the implied warranty of            *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU      *
 *   Lesser General Public License for more details.                       *
 *                                                                         *
 *   You should have received a copy of the GNU Lesser General Public      *
 *   License along with FreeCAD. If not, see                               *
 *   <https://www.gnu.org/licenses/>.                                      *
 *                                                                         *
 **************************************************************************/

#ifndef BASE_PROGRAMVERSION_H
#define BASE_PROGRAMVERSION_H

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <string_view>
#include <FCGlobal.h>

namespace Base
{

class XMLReader;
class Reader;

/// The FreeCAD release a document was written by, as far as a reader can tell
enum class Version : std::uint8_t
{
    v0_1x,
    v0_16,
    v0_17,
    v0_18,
    v0_19,
    v0_20,
    v0_21,
    v0_22,
    v1_0,
    v1_1,
    v1_2,
    v1_x,
};

/** Classify a document's ProgramVersion attribute.
 *
 * Matched by prefix, because the attribute is a release followed by a build
 * ("1.1R41234"). Anything unrecognised that does not start with a zero counts
 * as newer than every name listed, which is the safe answer for a reader
 * asking "is this older than the release that changed X" and the WRONG one
 * for a reader asking "is it newer" -- a version absent from the table, and
 * the "pre-0.14" a document without the attribute is given, both come back
 * as v1_x. Ask the second question through getReleaseNumber below, which
 * answers "no release stated" rather than "the newest one".
 *
 * Ported from upstream so their version-gated code needs no edit here.
 */
inline Version getVersion(std::string_view str)
{
    struct VersionItem
    {
        std::string_view name;
        Version version;
    };
    static const std::initializer_list<VersionItem> items = {
        {"0.16", Version::v0_16},
        {"0.17", Version::v0_17},
        {"0.18", Version::v0_18},
        {"0.19", Version::v0_19},
        {"0.20", Version::v0_20},
        {"0.21", Version::v0_21},
        {"0.22", Version::v0_22},
        {"1.0",  Version::v1_0 },
        {"1.1",  Version::v1_1 },
        {"1.2",  Version::v1_2 },
    };
    for (const auto &item : items) {
        if (str.compare(0, item.name.size(), item.name) == 0)
            return item.version;
    }
    if (!str.empty() && str[0] == '0')
        return Version::v0_1x;
    return Version::v1_x;
}

/** The release a ProgramVersion attribute begins with, as numbers
 *
 * Zeroes when it does not begin with one, which covers both an attribute an
 * old document never had ("pre-0.14", the stand-in a reader fills in) and one
 * that was never read at all. Numbers rather than the enum above because the
 * question below has to be answered for releases that do not exist yet, and a
 * name absent from that table cannot be ordered against one that is in it.
 */
struct ReleaseNumber
{
    int major {0};
    int minor {0};
};

inline ReleaseNumber getReleaseNumber(std::string_view str)
{
    ReleaseNumber release;
    std::size_t pos = 0;
    if (pos >= str.size() || str[pos] < '0' || str[pos] > '9')
        return {};
    for (; pos < str.size() && str[pos] >= '0' && str[pos] <= '9'; ++pos)
        release.major = release.major * 10 + (str[pos] - '0');
    if (pos >= str.size() || str[pos] != '.')
        return {};
    ++pos;
    for (; pos < str.size() && str[pos] >= '0' && str[pos] <= '9'; ++pos)
        release.minor = release.minor * 10 + (str[pos] - '0');
    return release;
}

/** Whether a document's colours mean opacity by their alpha component.
 *
 * Upstream inverted the meaning at 1.1: before that release the component
 * held transparency, and a 1.1 reader converts an older file on the way in.
 * This fork kept the older convention, where a face colour's alpha IS that
 * face's transparency, so the conversion it needs is the same arithmetic in
 * the opposite direction -- applied to files newer than the change, not older.
 *
 * A version this cannot read is answered with no, not yes: leaving a colour
 * as the file states it is what every release before the change did, and is
 * the answer that cannot corrupt a document nobody converted.
 *
 * NOTE: the gate is the release number, and this fork's own is still 0.22
 * (PACKAGE_VERSION in the top level CMakeLists). Should the fork ever call
 * itself 1.1 or later, its own documents would start reading as opacity based
 * and every colour would come back inverted; a fork marker in the document
 * root would have to join this test before that can happen.
 */
inline bool alphaIsOpacity(std::string_view programVersion)
{
    const ReleaseNumber release = getReleaseNumber(programVersion);
    return release.major > 1 || (release.major == 1 && release.minor >= 1);
}

/// The same question asked of the document a reader is parsing
BaseExport bool alphaIsOpacity(const XMLReader &reader);
/** The same, for a property restoring from its own archive entry.
 *
 * The entry is read after the XML pass, from a reader that carries no
 * version of its own; the parser that registered it does. False when there
 * is no parser to ask, which leaves the values exactly as the file states.
 */
BaseExport bool alphaIsOpacity(const Reader &reader);

}  // namespace Base

#endif  // BASE_PROGRAMVERSION_H
