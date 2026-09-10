/****************************************************************************
 *   Copyright (c) 2026 Zheng Lei (realthunder) <realthunder.dev@gmail.com> *
 *                                                                          *
 *   This file is part of the FreeCAD CAx development system.               *
 *                                                                          *
 *   This library is free software; you can redistribute it and/or          *
 *   modify it under the terms of the GNU Library General Public            *
 *   License as published by the Free Software Foundation; either           *
 *   version 2 of the License, or (at your option) any later version.       *
 *                                                                          *
 *   This library  is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of         *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the          *
 *   GNU Library General Public License for more details.                   *
 *                                                                          *
 *   You should have received a copy of the GNU Library General Public      *
 *   License along with this library; see the file COPYING.LIB. If not,     *
 *   write to the Free Software Foundation, Inc., 59 Temple Place,          *
 *   Suite 330, Boston, MA  02111-1307, USA                                 *
 *                                                                          *
 ****************************************************************************/

// The stb_truetype and stb_rect_pack implementations Dear ImGui's font
// atlas calls. bgfx's imconfig.h disables ImGui's own copies
// (IMGUI_DISABLE_STB_*_IMPLEMENTATION), expecting the example
// renderer (examples/common/imgui/imgui.cpp, compiled into
// example-common) to provide them -- but that one routes STBTT_malloc
// through a global allocator that only its imguiCreate() sets, so
// rasterizing the first glyph without it dereferences null. This unit
// defines them here, from the same bgfx stb headers the example (and
// bgfx's imstb_*.h forwarders) use, with plain malloc; the linker
// then never pulls the example's object in.

#include <bx/bx.h>
#include <dear-imgui/imgui.h>

// The one symbol of the example renderer that bgfx's own widgets
// (imgui_user.inl) reference: its named-font switch. Answering it here
// is what keeps the linker from pulling the example object -- and its
// stb implementation -- into this library. There is no font set to
// pick from; the current font at the requested size is what it means.
namespace ImGui {
void PushFont(Font::Enum, float fontSizeBaseUnscaled)
{
    PushFont(nullptr, fontSizeBaseUnscaled);
}
} // namespace ImGui

BX_PRAGMA_DIAGNOSTIC_IGNORED_CLANG_GCC("-Wunused-function")
BX_PRAGMA_DIAGNOSTIC_IGNORED_CLANG_GCC("-Wtype-limits")
BX_PRAGMA_DIAGNOSTIC_IGNORED_CLANG("-Wunknown-pragmas")

#define STB_RECT_PACK_IMPLEMENTATION
#include <stb/stb_rect_pack.h>
#define STB_TRUETYPE_IMPLEMENTATION
#include <stb/stb_truetype.h>
