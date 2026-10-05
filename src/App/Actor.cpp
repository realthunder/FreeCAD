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
# include <vector>
#endif

#include "Actor.h"

using namespace App;

namespace
{
std::vector<std::shared_ptr<const Actor>>& actors()
{
    thread_local std::vector<std::shared_ptr<const Actor>> stack;
    return stack;
}
}  // namespace

const char* Actor::kindName(Kind kind)
{
    switch (kind) {
        case Verified:
            return "verified";
        case Invited:
            return "invited";
        case Declared:
            return "declared";
        case Fork:
            return "fork";
        case Enrolled:
            return "enrolled";
        default:
            return "local";
    }
}

bool Actor::kindFromName(const std::string& name, Kind& kind)
{
    for (Kind k : {Local, Verified, Invited, Declared, Fork, Enrolled}) {
        if (name == kindName(k)) {
            kind = k;
            return true;
        }
    }
    return false;
}

ActorScope::ActorScope(const Actor& actor)
{
    push(std::make_shared<const Actor>(actor));
}

ActorScope::ActorScope(std::shared_ptr<const Actor> actor)
{
    push(std::move(actor));
}

ActorScope::~ActorScope()
{
    pop();
}

std::shared_ptr<const Actor> ActorScope::current()
{
    auto& stack = actors();
    return stack.empty() ? nullptr : stack.back();
}

void ActorScope::push(std::shared_ptr<const Actor> actor)
{
    actors().push_back(std::move(actor));
}

bool ActorScope::pop()
{
    auto& stack = actors();
    if (stack.empty())
        return false;
    stack.pop_back();
    return true;
}
