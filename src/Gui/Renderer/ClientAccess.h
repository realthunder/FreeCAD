// SPDX-License-Identifier: LGPL-2.1-or-later

#ifndef RENDER_CLIENTACCESS_H
#define RENDER_CLIENTACCESS_H

#include <string_view>

namespace Render
{

/** What a scene stream connection may do (docs/ShareAccess.md sec 2.2).
 *
 * Ordered, so a check is a comparison. View looks: the scene, the
 * property inspector, a Cycles viewport of its own. Edit also changes
 * the document it is joined to. Host also acts as the desktop user
 * would -- the host's preferences, any command, the desktop's own tool
 * bar actions -- and is granted only to a verified identity.
 */
enum class ClientAccess : int
{
    View = 0,
    Edit = 1,
    Host = 2,
};

/// The level's name on the wire and in the Python bindings
inline const char* clientAccessName(ClientAccess access)
{
    switch (access) {
        case ClientAccess::View:
            return "view";
        case ClientAccess::Host:
            return "host";
        default:
            return "edit";
    }
}

/// The level \a name spells; false, leaving \a out alone, for anything else
inline bool clientAccessFromName(const char* name, ClientAccess& out)
{
    const std::string_view s(name ? name : "");
    if (s == "view")
        out = ClientAccess::View;
    else if (s == "edit")
        out = ClientAccess::Edit;
    else if (s == "host")
        out = ClientAccess::Host;
    else
        return false;
    return true;
}

/** Where a connection's committed selection goes (docs/ThinClient.md
 * sec 8.11a).
 *
 * Only the host sets it, per connection and for this session alone --
 * it is not part of a grant and nothing about it is persisted. It
 * decides the routing of the RESOLVED TEXT of a selection and nothing
 * else: a route into the room is plain Gui::Selection() calls, which
 * move the desktop's tree, panels and highlight exactly as the same
 * calls typed into the Python console would.
 *
 * Ordered, so a check is a comparison: Everyone is Host and more.
 * Preselection is never routed on any of them -- a client contributes
 * what it committed, so the desktop's highlight does not follow a
 * remote pointer.
 */
enum class SelectionRoute : int
{
    /// The client's own instance. It alone is told; the room is untouched.
    None = 0,
    /// Also applied to the room, as the desktop user's own selection.
    Host = 1,
    /// Also sent to the other clients, owner-tagged, for them to paint.
    Everyone = 2,
};

/// The route's name on the wire and in the Python bindings
inline const char* selectionRouteName(SelectionRoute route)
{
    switch (route) {
        case SelectionRoute::Host:
            return "host";
        case SelectionRoute::Everyone:
            return "everyone";
        default:
            return "none";
    }
}

/// The route  name spells; false, leaving  out alone, for anything else
inline bool selectionRouteFromName(const char* name, SelectionRoute& out)
{
    const std::string_view s(name ? name : "");
    if (s == "none")
        out = SelectionRoute::None;
    else if (s == "host")
        out = SelectionRoute::Host;
    else if (s == "everyone")
        out = SelectionRoute::Everyone;
    else
        return false;
    return true;
}

/// What a connection admitted at  access starts with: a full-control
/// client acts as the desktop user would, so its selection lands where
/// the desktop user's does; everything else keeps to itself until the
/// host says otherwise.
inline SelectionRoute defaultSelectionRoute(ClientAccess access)
{
    return access == ClientAccess::Host ? SelectionRoute::Host
                                        : SelectionRoute::None;
}

}  // namespace Render

#endif  // RENDER_CLIENTACCESS_H
