/***************************************************************************
 *   Copyright (c) 2026 Zheng, Lei <realthunder.dev@gmail.com>              *
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

#ifndef GUI_PROPERTY_GROUP_NAME_H
#define GUI_PROPERTY_GROUP_NAME_H

#include <string>

namespace Gui {

/** The name a property group is shown by, and its rank.
 *
 * A group's rank is written into its name, as an ending "#<rank>":
 * "Appearances#1" is the group "Appearances" at rank 1, and a name with no
 * such ending has rank 0. A property view shows the groups by rank, then by
 * name, so a rank above 0 puts a group after the others and one below 0
 * before them. The ending is not shown. Where one shown name is declared
 * with several ranks it is one group, at the largest of them
 * (docs/HandsOnQueue.md entry 106).
 *
 * @param group: the group's name as declared; may be null
 * @param rank: if given, set to the rank, 0 where the name has none
 * @return the name without its ending; empty for a null or empty name
 */
inline std::string propertyGroupDisplayName(const char *group, int *rank = nullptr)
{
    if (rank)
        *rank = 0;
    if (!group || !group[0])
        return std::string();
    std::string name(group);
    auto pos = name.rfind('#');
    // Something before the mark, and after it a number and nothing else
    if (pos == std::string::npos || pos == 0 || pos + 1 == name.size())
        return name;
    std::size_t digits = pos + 1;
    if (name[digits] == '-')
        ++digits;
    if (digits == name.size() || name.size() - digits > 6)
        return name;
    for (std::size_t i = digits; i < name.size(); ++i) {
        if (name[i] < '0' || name[i] > '9')
            return name;
    }
    if (rank)
        *rank = std::stoi(name.substr(pos + 1));
    name.resize(pos);
    return name;
}

} // namespace Gui

#endif // GUI_PROPERTY_GROUP_NAME_H
