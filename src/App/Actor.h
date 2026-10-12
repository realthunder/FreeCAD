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

#ifndef APP_ACTOR_H
#define APP_ACTOR_H

#include <cstdint>
#include <memory>
#include <string>

#include <FCGlobal.h>

namespace App
{

/** Who acts (docs/TransactionLog.md sec 30.3 S.b, 30.6): one login of one
 * user, as the transaction log records the author of a row.
 *
 * The desktop user, whose process it is, is the absence of an actor: no
 * scope is set, and the row is written under the process's own session.
 * A client of a served document is an actor the Gui sets for as long as it
 * replays that client's event or runs its operation.
 *
 * `kind` says how the name is known (sec 30.6): `Verified`, the identity an
 * authenticating front door asserted; `Invited`, the holder of an
 * invitation the host issued to one named person; `Declared`, a name a
 * connection gave for itself, which nothing checked. `login` tells one
 * admitted connection of a user from the next: the log keeps a session row
 * per login and a user row per kind and name, so one person's logins are
 * one author.
 *
 * `Enrolled` is one of the browsers a grant counts (docs/TransactionLog.md
 * sec 30.32): known by half a grant gave -- a token, or a sign-in -- and
 * half its own, the id it keeps. Nobody verified who sits at it; the host's
 * record of browsers says which one it is.
 *
 * `Fork` is no connection: the desktop user of another copy of the file,
 * whose rows were imported (sec 30.13 F4). Two people both called `host`
 * are two people, so the name says which file's.
 */
struct AppExport Actor
{
    enum Kind
    {
        Local,
        Verified,
        Invited,
        Declared,
        Fork,
        Enrolled
    };
    Kind kind {Local};
    std::string name;
    /// The access the login was admitted with: `view`, `edit` or `host`.
    std::string access;
    /// One admitted connection; 0 for a user with no connection of its own.
    uint64_t login {0};
    /// The browser the connection came from, by the start of its key in
    /// the host's record (sec 30.32); empty when it said none. Whatever
    /// the kind: a login says it, so one browser can be followed from one
    /// token or sign-in to another.
    std::string device;

    /// `local`, `verified`, `invited`, `declared`, `fork`, `enrolled`: the
    /// kind as the log stores it.
    static const char* kindName(Kind kind);
    /// The kind of a stored name; false for a name that is none.
    static bool kindFromName(const std::string& name, Kind& kind);
    /// Sec 30.6 U4, U6: whether this user's writes may be an author's.
    bool mayWrite() const { return kind != Declared; }
};

/** The actor of what this thread does while the scope lives (sec 30.3
 * S.b). Read when a transaction opens -- the transaction keeps it, so a
 * row closed later, at the event loop, is still its author's -- and when a
 * record with no transaction is written. Scopes nest; the innermost wins.
 */
class AppExport ActorScope
{
public:
    explicit ActorScope(const Actor& actor);
    explicit ActorScope(std::shared_ptr<const Actor> actor);
    ~ActorScope();

    ActorScope(const ActorScope&) = delete;
    ActorScope& operator=(const ActorScope&) = delete;

    /// The actor of this thread now, null for the desktop user.
    static std::shared_ptr<const Actor> current();

    /// The same without a scope object, for a caller that cannot hold one
    /// (Python): each push() is ended by one pop(), which returns false
    /// when there was nothing to end.
    static void push(std::shared_ptr<const Actor> actor);
    static bool pop();
};

}  // namespace App

#endif  // APP_ACTOR_H
