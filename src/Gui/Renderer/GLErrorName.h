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
 *   This library is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU     *
 *   Library General Public License for more details.                      *
 *                                                                         *
 *   You should have received a copy of the GNU Library General Public     *
 *   License along with this library; see the file COPYING.LIB. If not,    *
 *   write to the Free Software Foundation, Inc., 59 Temple Place,         *
 *   Suite 330, Boston, MA  02111-1307, USA                                *
 *                                                                         *
 ***************************************************************************/

#ifndef GUI_RENDERER_GLERRORNAME_H
#define GUI_RENDERER_GLERRORNAME_H

/** \file
 * The name of a glGetError() code, for a log line.
 *
 * No GL header: the codes are the same numbers in every GL and GLES, and two
 * of them (the matrix and attribute stack ones) are not declared by GLES
 * headers at all, though a desktop context reports them.
 *
 * It answers for EVERY value. The table this replaces had four entries and
 * was indexed with min(code - 0x0500, 4): a stack underflow, an out of
 * memory or an invalid framebuffer operation read one past its end and handed
 * the logger whatever pointer lay there, so reporting the error crashed the
 * program (a hands-on run, 2026-10-06: an access violation in
 * QDebug::operator<<(const char*) reading 0x2, from the readback composite's
 * check).
 */
namespace Render
{

inline const char* glErrorName(unsigned int error)
{
    switch (error) {
        case 0x0000:
            return "GL_NO_ERROR";
        case 0x0500:
            return "GL_INVALID_ENUM";
        case 0x0501:
            return "GL_INVALID_VALUE";
        case 0x0502:
            return "GL_INVALID_OPERATION";
        case 0x0503:
            return "GL_STACK_OVERFLOW";
        case 0x0504:
            return "GL_STACK_UNDERFLOW";
        case 0x0505:
            return "GL_OUT_OF_MEMORY";
        case 0x0506:
            return "GL_INVALID_FRAMEBUFFER_OPERATION";
        case 0x0507:
            return "GL_CONTEXT_LOST";
        default:
            return "unknown GL error";
    }
}

}  // namespace Render

#endif  // GUI_RENDERER_GLERRORNAME_H
