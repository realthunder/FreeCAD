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

}  // namespace Render

#endif  // RENDER_CLIENTACCESS_H
