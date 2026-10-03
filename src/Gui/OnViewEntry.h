// SPDX-License-Identifier: LGPL-2.1-or-later
/****************************************************************************
 *                                                                          *
 *   This file is part of FreeCAD.                                          *
 *                                                                          *
 *   FreeCAD is free software: you can redistribute it and/or modify it     *
 *   under the terms of the GNU Lesser General Public License as            *
 *   published by the Free Software Foundation, either version 2.1 of the   *
 *   License, or (at your option) any later version.                        *
 *                                                                          *
 *   FreeCAD is distributed in the hope that it will be useful, but         *
 *   WITHOUT ANY WARRANTY; without even the implied warranty of             *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU       *
 *   Lesser General Public License for more details.                        *
 *                                                                          *
 *   You should have received a copy of the GNU Lesser General Public       *
 *   License along with FreeCAD. If not, see                                *
 *   <https://www.gnu.org/licenses/>.                                       *
 *                                                                          *
 ***************************************************************************/

#ifndef GUI_ONVIEWENTRY_H
#define GUI_ONVIEWENTRY_H

#include <string>

#include <Inventor/SbVec3f.h>
#include <FCGlobal.h>

class QKeyEvent;

namespace Gui
{

/** An entry an edit mode puts on a view, as a view without widgets sees it
 * (docs/ThinClient.md section 8.7).
 *
 * The desktop shows the entry's own widgets and needs none of this. A
 * mirror keeps the same widgets unshown, delivers the client's keys to them
 * and streams what they show; this is that seam. Two kinds stand on it: a
 * drawing tool's parameter (EditableDatumLabel, one number) and the editor
 * of a value the scene draws already (DatumValueEditor: a value or an
 * expression, a reference toggle, a name).
 */
class GuiExport OnViewEntry
{
public:
    /// What a client is told to draw. Display state only: what a key does
    /// is decided where the widgets are.
    struct State
    {
        /// "param" for a tool's parameter, "datum" for a value's editor
        const char* kind = "param";
        /// Where the entry belongs, in world coordinates
        SbVec3f anchor {0, 0, 0};
        /// A world point one unit from the anchor, in the direction the
        /// entry's other rows grow so that they hide nothing it edits.
        /// Unset for a parameter, which has no other rows.
        bool hasAway = false;
        SbVec3f away {0, 0, 0};
        /// The field's text exactly as a desktop user reads it
        std::string text;
        int selStart = 0;
        int selLength = 0;
        /// A parameter fixed by the user rather than by the pointer
        bool set = false;

        /** @name A value's editor */
        //@{
        /// Which field takes the keys: "value" or "name"
        const char* field = "value";
        /// The field holds an expression (its text starts with '=')
        bool expression = false;
        /// The line under the field: what the expression gives, or why the
        /// text gives nothing. Empty when there is nothing to say.
        std::string result;
        /// 0 plain, 1 a value, 2 a warning, 3 an error
        int resultLevel = 0;
        /// -1 no toggle, 0 a reference, 1 driving
        int driving = -1;
        /// A value that can be stated in either of two measures (a circle's
        /// size as its radius or as its diameter): -1 for none, else which
        /// of the two it is stated in now, and that measure's name
        int measure = -1;
        std::string measureName;
        /// The name row, when there is one
        bool nameShown = false;
        std::string name;
        int nameSelStart = 0;
        int nameSelLength = 0;
        /// The document object an expression is written in, for a client
        /// that completes names itself
        std::string objectName;
        //@}
    };

    /** A client's act on an entry that is not a key.
     *
     * "toggle" flips the reference toggle; "field" gives the keys to the
     * field named by `text` ("value" or "name"); "replace" puts `text` in
     * place of `length` characters at `start` of the value field, which is
     * how a completion the client offered is taken.
     */
    struct Action
    {
        std::string name;
        std::string text;
        int start = 0;
        int length = 0;
    };

    virtual ~OnViewEntry() = default;

    /// Whether the entry is on the screen now
    virtual bool isShownOnView() const = 0;
    virtual void describe(State& state) const = 0;
    /// Deliver a key, the desktop's focus having done it there
    virtual bool sendKeyEvent(QKeyEvent* event) = 0;
    /// Give this entry the keys
    virtual void takeKeys() = 0;
    virtual bool act(const Action& action)
    {
        (void)action;
        return false;
    }
};

}  // namespace Gui

#endif  // GUI_ONVIEWENTRY_H
