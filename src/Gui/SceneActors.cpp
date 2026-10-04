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
# include <set>
#endif

#include <App/Actor.h>
#include <App/Document.h>
#include <App/TransactionLog.h>
#include <Base/Console.h>

#include "Renderer/SceneServer.h"
#include "SceneActors.h"

FC_LOG_LEVEL_INIT("SceneActors", true, true)

using namespace Gui;

namespace
{

std::map<uint64_t, std::shared_ptr<const App::Actor>>& registry()
{
    static std::map<uint64_t, std::shared_ptr<const App::Actor>> actors;
    return actors;
}

/// A grant field that names one thing, not a pattern.
bool literal(const std::string& pattern)
{
    return !pattern.empty() && pattern.find_first_of("*?") == std::string::npos;
}

bool sameUser(const App::Actor& a, const App::Actor& b)
{
    return a.kind == b.kind && a.name == b.name;
}

/// The registry's actor of a connection the roster lists, replaced when
/// the roster now says otherwise: a rename is another user (sec 30.6 U3),
/// a change of mode the same one with another access.
std::shared_ptr<const App::Actor> known(const Render::SceneClientInfo& info,
                                        const std::vector<Render::SceneGrant>& grants)
{
    App::Actor now = SceneActors::describe(info, grants);
    auto& slot = registry()[info.id];
    if (!slot || !sameUser(*slot, now) || slot->access != now.access)
        slot = std::make_shared<const App::Actor>(std::move(now));
    return slot;
}

}  // namespace

App::Actor SceneActors::describe(const Render::SceneClientInfo& info,
                                 const std::vector<Render::SceneGrant>& grants)
{
    App::Actor actor;
    actor.login = info.id;
    actor.access = Render::clientAccessName(info.access);
    if (!info.identity.empty()) {
        actor.kind = App::Actor::Verified;
        actor.name = info.identity;
        return actor;
    }
    actor.kind = App::Actor::Declared;
    actor.name = info.client.empty() ? std::string("guest") : info.client;
    if (!info.grant || info.client.empty())
        return actor;
    for (const auto& grant : grants) {
        if (grant.id != info.grant)
            continue;
        // An invitation to one named person: the token is a secret and the
        // host has said whose it is. An easing the server minted for a
        // rename names nobody the host chose.
        if (!grant.liveOnly && !grant.token.empty() && literal(grant.client)
            && grant.client == info.client)
            actor.kind = App::Actor::Invited;
        break;
    }
    return actor;
}

std::shared_ptr<const App::Actor> SceneActors::of(uint64_t client)
{
    auto it = registry().find(client);
    if (it != registry().end())
        return it->second;
    auto& server = Render::SceneStreamServer::instance();
    std::vector<Render::SceneClientInfo> clients;
    server.clients(clients);
    for (const auto& info : clients) {
        if (info.id == client)
            return known(info, server.grants());
    }
    // Gone before what it sent was run. Not kept: the id is never reused.
    App::Actor guest;
    guest.kind = App::Actor::Declared;
    guest.name = "guest";
    guest.access = "edit";
    guest.login = client;
    return std::make_shared<const App::Actor>(std::move(guest));
}

void SceneActors::sync(const std::string& group, App::Document* doc, Logins& logins)
{
    auto& server = Render::SceneStreamServer::instance();
    std::vector<Render::SceneClientInfo> clients;
    server.clients(clients);
    const auto grants = server.grants();
    App::TransactionLog* log = doc ? doc->getTransactionLog() : nullptr;

    std::set<uint64_t> connected;
    std::set<uint64_t> here;
    for (const auto& info : clients) {
        connected.insert(info.id);
        if (info.doc != group || !info.authorized || !info.viewer)
            continue;
        here.insert(info.id);
        auto actor = known(info, grants);
        auto& login = logins[info.id];
        if (login && sameUser(*login, *actor)) {
            login = actor;
            continue;
        }
        try {
            // The same connection under another name: the session of the
            // name it had is over.
            if (login && log)
                log->logout(*login);
            login = actor;
            if (log)
                log->login(*actor);
        }
        catch (const Base::Exception& e) {
            FC_ERR("login of " << actor->name << ": " << e.what());
        }
    }
    for (auto it = logins.begin(); it != logins.end();) {
        if (here.count(it->first)) {
            ++it;
            continue;
        }
        try {
            if (it->second && log)
                log->logout(*it->second);
        }
        catch (const Base::Exception& e) {
            FC_ERR("logout of " << it->second->name << ": " << e.what());
        }
        it = logins.erase(it);
    }
    for (auto it = registry().begin(); it != registry().end();) {
        if (connected.count(it->first))
            ++it;
        else
            it = registry().erase(it);
    }
}
