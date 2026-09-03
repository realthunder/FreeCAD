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

#include "ImGuiStdlib.h"

namespace {

struct Chain {
    std::string *str;
    ImGuiInputTextCallback callback;
    void *userData;
};

int resizeCallback(ImGuiInputTextCallbackData *data)
{
    auto chain = static_cast<Chain *>(data->UserData);
    if (data->EventFlag == ImGuiInputTextFlags_CallbackResize) {
        // The buffer is the string's own storage; grow it in place.
        std::string *str = chain->str;
        IM_ASSERT(data->Buf == str->c_str());
        str->resize(data->BufTextLen);
        data->Buf = str->data();
    }
    else if (chain->callback) {
        data->UserData = chain->userData;
        return chain->callback(data);
    }
    return 0;
}

} // namespace

bool ImGui::InputText(const char *label, std::string *str, ImGuiInputTextFlags flags,
                      ImGuiInputTextCallback callback, void *userData)
{
    IM_ASSERT((flags & ImGuiInputTextFlags_CallbackResize) == 0);
    flags |= ImGuiInputTextFlags_CallbackResize;
    Chain chain{str, callback, userData};
    return InputText(label, str->data(), str->capacity() + 1, flags, resizeCallback, &chain);
}
