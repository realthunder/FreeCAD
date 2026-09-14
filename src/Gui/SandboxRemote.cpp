/***************************************************************************
 *   Copyright (c) 2026 Zheng, Lei <realthunder.dev@gmail.com>             *
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
#include <exception>
#include <map>
#include <memory>
#include <string>
#include <vector>
#endif

#include "Renderer/SceneServer.h"
#include "SandboxRemote.h"

#ifdef FC_EXPR_IMAGE_HOST
#include <App/Document.h>
#include <App/ExpressionImageBridge.h>
#include <App/ExpressionSecurityRuntime.h>
#include <Base/Exception.h>
#include <Base/Interpreter.h>
#endif

using namespace Gui;

#ifdef FC_EXPR_IMAGE_HOST

namespace
{

namespace Sandbox = App::ExpressionSandbox;

struct Endpoint
{
    Sandbox::HandleTable table;
    /// The document the table's handles were minted against: a
    /// connection that switches documents starts from an empty table.
    std::string doc;
};

std::map<uint64_t, std::unique_ptr<Endpoint>>& endpoints()
{
    static std::map<uint64_t, std::unique_ptr<Endpoint>> map;
    return map;
}

/// Release every handle; the table holds a reference per entry.
void clearTable(Endpoint& e)
{
    if (!Py_IsInitialized())
        return;
    Base::PyGILStateLocker lock;
    e.table.setOwner(nullptr);
    e.table.clear();
}

std::vector<uint8_t> dispatch(App::Document* doc, const Render::SceneBridgeRequest& req)
{
    const unsigned char* data = req.payload.data();
    const std::size_t len = req.payload.size();
    if (!doc)
        return Sandbox::errorReplyBytes(data, len, "RuntimeError",
                                        "this connection is joined to no served document");

    // The client, as the door knows it at this frame: a connection the
    // roster turns view-only is read-only from its next op on.
    App::ExpressionSecurity::RemoteClient client;
    client.principal = App::ExpressionSecurity::clientPrincipalId(req.identity, req.grant,
                                                                  req.client);
    client.context = "#" + std::to_string(req.client);
    if (!req.label.empty())
        client.context += " '" + req.label + "'";
    if (!req.address.empty())
        client.context += " @" + req.address;
    client.readOnly = req.viewOnly;

    auto& slot = endpoints()[req.client];
    if (!slot)
        slot = std::make_unique<Endpoint>();
    Endpoint& e = *slot;

    Base::PyGILStateLocker lock;
    if (e.doc != doc->getName()) {
        e.table.setOwner(nullptr);
        e.table.clear();
        e.doc = doc->getName();
    }
    try {
        // The document is the owner: what the guest reaches and writes
        // (ExpressionImageBridge, reachable() and the write gate), and
        // its FreeCAD.ActiveDocument.  Registered once per statement --
        // an End clears it with the rest.
        PyObject* face = doc->getPyObject();
        if (!e.table.idOf(face))
            e.table.add(face);
        e.table.setOwner(face);
        Py_DECREF(face);
        App::ExpressionSecurity::Runtime::Scope scope(doc, client);
        std::string opName;
        std::string missing;
        return Sandbox::dispatchHostBytes(e.table, data, len, opName, missing);
    }
    catch (const Base::Exception& ex) {
        return Sandbox::errorReplyBytes(data, len, "RuntimeError", ex.what());
    }
    catch (const std::exception& ex) {
        return Sandbox::errorReplyBytes(data, len, "RuntimeError", ex.what());
    }
}

}  // namespace

void SandboxRemote::handle(App::Document* doc, Render::SceneBridgeRequest& req)
{
    if (req.kind == Render::SceneBridgeRequest::End) {
        auto it = endpoints().find(req.client);
        if (it != endpoints().end())
            clearTable(*it->second);
        return;
    }
    Render::SceneStreamServer::instance().sendBridge(req.client, req.seq, dispatch(doc, req));
}

void SandboxRemote::drop(uint64_t client)
{
    auto it = endpoints().find(client);
    if (it == endpoints().end())
        return;
    clearTable(*it->second);
    endpoints().erase(it);
}

std::size_t SandboxRemote::endpointCount()
{
    return endpoints().size();
}

#else  // FC_EXPR_IMAGE_HOST

void SandboxRemote::handle(App::Document* doc, Render::SceneBridgeRequest& req)
{
    (void)doc;
    // No sandbox host in this build: an empty answer, which the page's
    // guest raises as "host bridge unavailable".
    if (req.kind == Render::SceneBridgeRequest::Op)
        Render::SceneStreamServer::instance().sendBridge(req.client, req.seq, {});
}

void SandboxRemote::drop(uint64_t client)
{
    (void)client;
}

std::size_t SandboxRemote::endpointCount()
{
    return 0;
}

#endif  // FC_EXPR_IMAGE_HOST
