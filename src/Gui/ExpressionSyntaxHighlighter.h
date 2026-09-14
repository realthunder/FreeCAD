/***************************************************************************
 *   Copyright (c) 2026 realthunder <realthunder.dev@gmail.com>            *
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

#ifndef GUI_EXPRESSIONSYNTAXHIGHLIGHTER_H
#define GUI_EXPRESSIONSYNTAXHIGHLIGHTER_H

#include <memory>

#include "SyntaxHighlighter.h"

namespace Gui
{

/** Highlights the expression language (App/ExpressionParser.l).
 *
 * Adapted from PythonSyntaxHighlighter, with the lexer's differences:
 * a '#' only starts a comment when a blank or the end of the line follows
 * it (Doc#Obj is a reference), '#@pybegin'/'#@pyend' switch to Python
 * rules until the end marker, '<<...>>' is a string, a number carries its
 * unit ('5mm'), triple-quoted strings span lines, and the '##@@' header
 * lines of the expression copy format are shown as headers.
 *
 * The block state carries the multi-line constructs only: an open triple
 * quoted string and the Python mode flag.
 */
class GuiExport ExpressionSyntaxHighlighter: public SyntaxHighlighter
{
public:
    explicit ExpressionSyntaxHighlighter(QObject* parent);
    ~ExpressionSyntaxHighlighter() override;

    void highlightBlock(const QString& text) override;

    /// Take the colors of the Editor preference page. A TextEditor pushes
    /// them itself; a plain text widget calls this once.
    void loadEditorColors();

    /// The builtin function names colored as class names.
    static bool isBuiltinFunction(const QString& name);

private:
    class Private;
    std::unique_ptr<Private> d;
};

}  // namespace Gui

#endif  // GUI_EXPRESSIONSYNTAXHIGHLIGHTER_H
