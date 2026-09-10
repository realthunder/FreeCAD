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

// The renderer half of bgfx/examples/common/imgui/imgui.cpp
// (Copyright 2014-2015 Daniel Collin, BSD-2-Clause), per instance and
// without its entry:: input. The embedded shaders and the Roboto face
// are bgfx's own, included from examples/common/imgui.

#include "ImGuiBgfx.h"

#include <bgfx/bgfx.h>
#include <bgfx/embedded_shader.h>
#include <bx/allocator.h>
#include <bx/math.h>
#include <dear-imgui/imgui.h>

#include <imgui/vs_ocornut_imgui.bin.h>
#include <imgui/fs_ocornut_imgui.bin.h>
#include <imgui/vs_imgui_image.bin.h>
#include <imgui/fs_imgui_image.bin.h>
#include <imgui/roboto_regular.ttf.h>

namespace {

const bgfx::EmbeddedShader s_embeddedShaders[] = {
    BGFX_EMBEDDED_SHADER(vs_ocornut_imgui),
    BGFX_EMBEDDED_SHADER(fs_ocornut_imgui),
    BGFX_EMBEDDED_SHADER(vs_imgui_image),
    BGFX_EMBEDDED_SHADER(fs_imgui_image),
    BGFX_EMBEDDED_SHADER_END()
};

/// What an ImTextureID carries here: the same packing as bgfx's
/// example (ImGui::TextureBgfx), so a texture handed to ImGui::Image
/// by any consumer of this renderer packs the same way.
struct TexId {
    bgfx::TextureHandle handle;
    uint8_t flags;
    uint8_t mip;
    uint32_t unused;
};
static_assert(sizeof(TexId) == sizeof(ImTextureID),
              "ImTextureID must hold a TexId");

constexpr uint8_t kFlagAlphaBlend = 0x01;

} // namespace

uint64_t Render::ImGuiBgfx::packTexture(uint16_t textureIdx)
{
    TexId id;
    id.handle = {textureIdx};
    id.flags = kFlagAlphaBlend;
    id.mip = 0;
    id.unused = 0;
    static_assert(sizeof(TexId) == sizeof(uint64_t), "TexId packs into 64 bits");
    return bx::bitCast<uint64_t>(id);
}

namespace {

bx::DefaultAllocator s_allocator;

void *imguiAlloc(size_t size, void *)
{
    return bx::alloc(&s_allocator, size);
}

void imguiFree(void *ptr, void *)
{
    bx::free(&s_allocator, ptr);
}

} // namespace

namespace Render {

struct ImGuiBgfx::Private {
    ImGuiContext *context = nullptr;
    bgfx::VertexLayout layout;
    bgfx::ProgramHandle program = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle imageProgram = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle texSampler = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle imageLodEnabled = BGFX_INVALID_HANDLE;

    void syncTextures(ImDrawData *drawData)
    {
        if (!drawData->Textures)
            return;
        for (ImTextureData *texData : *drawData->Textures) {
            switch (texData->Status) {
            case ImTextureStatus_WantCreate: {
                TexId tex{
                    bgfx::createTexture2D(uint16_t(texData->Width),
                                          uint16_t(texData->Height),
                                          false, 1,
                                          bgfx::TextureFormat::BGRA8, 0),
                    kFlagAlphaBlend, 0, 0};
                bgfx::setName(tex.handle, "ImGui Font Atlas");
                bgfx::updateTexture2D(
                        tex.handle, 0, 0, 0, 0,
                        uint16_t(texData->Width), uint16_t(texData->Height),
                        bgfx::copy(texData->GetPixels(),
                                   texData->GetSizeInBytes()));
                texData->SetTexID(bx::bitCast<ImTextureID>(tex));
                texData->SetStatus(ImTextureStatus_OK);
                break;
            }
            case ImTextureStatus_WantDestroy: {
                TexId tex = bx::bitCast<TexId>(texData->GetTexID());
                bgfx::destroy(tex.handle);
                texData->SetTexID(ImTextureID_Invalid);
                texData->SetStatus(ImTextureStatus_Destroyed);
                break;
            }
            case ImTextureStatus_WantUpdates: {
                TexId tex = bx::bitCast<TexId>(texData->GetTexID());
                for (ImTextureRect &rect : texData->Updates) {
                    const uint32_t bpp = texData->BytesPerPixel;
                    const bgfx::Memory *pix = bgfx::alloc(rect.h * rect.w * bpp);
                    bx::gather(pix->data, texData->GetPixelsAt(rect.x, rect.y),
                               texData->GetPitch(), rect.w * bpp, rect.h);
                    bgfx::updateTexture2D(tex.handle, 0, 0, rect.x, rect.y,
                                          rect.w, rect.h, pix);
                }
                texData->SetStatus(ImTextureStatus_OK);
                break;
            }
            default:
                break;
            }
        }
    }

    void draw(ImDrawData *drawData, bgfx::ViewId viewId, int fbWidth,
              int fbHeight)
    {
        syncTextures(drawData);
        if (fbWidth <= 0 || fbHeight <= 0)
            return;

        bgfx::setViewName(viewId, "ImGui");
        bgfx::setViewMode(viewId, bgfx::ViewMode::Sequential);

        const bgfx::Caps *caps = bgfx::getCaps();
        {
            float ortho[16];
            const float x = drawData->DisplayPos.x;
            const float y = drawData->DisplayPos.y;
            const float width = drawData->DisplaySize.x;
            const float height = drawData->DisplaySize.y;
            bx::mtxOrtho(ortho, x, x + width, y + height, y, 0.0f, 1000.0f,
                         0.0f, caps->homogeneousDepth);
            bgfx::setViewTransform(viewId, nullptr, ortho);
            // The rect is in framebuffer pixels; the example sizes it
            // in logical units, which is wrong on a HiDPI screen.
            bgfx::setViewRect(viewId, 0, 0, uint16_t(fbWidth),
                              uint16_t(fbHeight));
        }

        const ImVec2 clipPos = drawData->DisplayPos;
        const ImVec2 clipScale = drawData->FramebufferScale;

        for (int ii = 0, num = drawData->CmdListsCount; ii < num; ++ii) {
            const ImDrawList *drawList = drawData->CmdLists[ii];
            const uint32_t numVertices = uint32_t(drawList->VtxBuffer.size());
            const uint32_t numIndices = uint32_t(drawList->IdxBuffer.size());
            const bool index32 = sizeof(ImDrawIdx) == 4;

            if (bgfx::getAvailTransientVertexBuffer(numVertices, layout)
                        != numVertices
                || bgfx::getAvailTransientIndexBuffer(numIndices, index32)
                        != numIndices)
                break;

            bgfx::TransientVertexBuffer tvb;
            bgfx::TransientIndexBuffer tib;
            bgfx::allocTransientVertexBuffer(&tvb, numVertices, layout);
            bgfx::allocTransientIndexBuffer(&tib, numIndices, index32);
            bx::memCopy(tvb.data, drawList->VtxBuffer.begin(),
                        numVertices * sizeof(ImDrawVert));
            bx::memCopy(tib.data, drawList->IdxBuffer.begin(),
                        numIndices * sizeof(ImDrawIdx));

            bgfx::Encoder *encoder = bgfx::begin();
            for (const ImDrawCmd *cmd = drawList->CmdBuffer.begin(),
                                 *cmdEnd = drawList->CmdBuffer.end();
                 cmd != cmdEnd; ++cmd) {
                if (cmd->UserCallback) {
                    cmd->UserCallback(drawList, cmd);
                    continue;
                }
                if (!cmd->ElemCount)
                    continue;

                uint64_t state = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A
                    | BGFX_STATE_MSAA;
                bgfx::TextureHandle th = BGFX_INVALID_HANDLE;
                bgfx::ProgramHandle prog = program;
                const ImTextureID texId = cmd->GetTexID();
                if (texId != ImTextureID_Invalid) {
                    TexId tex = bx::bitCast<TexId>(texId);
                    if (tex.flags & kFlagAlphaBlend)
                        state |= BGFX_STATE_BLEND_FUNC(
                                BGFX_STATE_BLEND_SRC_ALPHA,
                                BGFX_STATE_BLEND_INV_SRC_ALPHA);
                    th = tex.handle;
                    if (tex.mip) {
                        const float lod[4] = {float(tex.mip), 1.0f, 0.0f, 0.0f};
                        bgfx::setUniform(imageLodEnabled, lod);
                        prog = imageProgram;
                    }
                }
                else {
                    state |= BGFX_STATE_BLEND_FUNC(
                            BGFX_STATE_BLEND_SRC_ALPHA,
                            BGFX_STATE_BLEND_INV_SRC_ALPHA);
                }

                ImVec4 clip;
                clip.x = (cmd->ClipRect.x - clipPos.x) * clipScale.x;
                clip.y = (cmd->ClipRect.y - clipPos.y) * clipScale.y;
                clip.z = (cmd->ClipRect.z - clipPos.x) * clipScale.x;
                clip.w = (cmd->ClipRect.w - clipPos.y) * clipScale.y;
                if (clip.x >= fbWidth || clip.y >= fbHeight || clip.z < 0.0f
                    || clip.w < 0.0f)
                    continue;
                const uint16_t xx = uint16_t(bx::max(clip.x, 0.0f));
                const uint16_t yy = uint16_t(bx::max(clip.y, 0.0f));
                encoder->setScissor(xx, yy,
                                    uint16_t(bx::min(clip.z, 65535.0f) - xx),
                                    uint16_t(bx::min(clip.w, 65535.0f) - yy));
                encoder->setState(state);
                encoder->setTexture(0, texSampler, th);
                encoder->setVertexBuffer(0, &tvb, cmd->VtxOffset, numVertices);
                encoder->setIndexBuffer(&tib, cmd->IdxOffset, cmd->ElemCount);
                encoder->submit(viewId, prog);
            }
            bgfx::end(encoder);
        }
    }
};

ImGuiBgfx::ImGuiBgfx()
    : d(new Private)
{
}

ImGuiBgfx::~ImGuiBgfx()
{
    // A context that outlived its device is released without touching
    // bgfx; the handles died with the device.
    destroy(false);
}

bool ImGuiBgfx::valid() const
{
    return d->context != nullptr;
}

ImGuiContext *ImGuiBgfx::context() const
{
    return d->context;
}

void ImGuiBgfx::makeCurrent()
{
    if (d->context)
        ImGui::SetCurrentContext(d->context);
}

bool ImGuiBgfx::create(float fontSize)
{
    if (d->context)
        return true;
    if (bgfx::getRendererType() == bgfx::RendererType::Noop)
        return false;

    IMGUI_CHECKVERSION();
    // imconfig.h (bgfx's) disables the default allocators; these are
    // process-wide and the same for every context.
    ImGui::SetAllocatorFunctions(imguiAlloc, imguiFree, nullptr);
    d->context = ImGui::CreateContext();
    ImGui::SetCurrentContext(d->context);

    ImGuiIO &io = ImGui::GetIO();
    io.DisplaySize = ImVec2(1280.0f, 720.0f);
    io.DeltaTime = 1.0f / 60.0f;
    // No imgui.ini: the surface's host keeps whatever layout it wants
    // to keep (docs/ShaderGraphEditor.md sec 4.3).
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset
        | ImGuiBackendFlags_RendererHasTextures;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.BackendPlatformName = "FreeCAD Qt";
    io.BackendRendererName = "FreeCAD bgfx";

    ImGuiStyle &style = ImGui::GetStyle();
    ImGui::StyleColorsDark(&style);
    style.FrameRounding = 4.0f;
    style.WindowBorderSize = 0.0f;

    const bgfx::RendererType::Enum type = bgfx::getRendererType();
    d->program = bgfx::createProgram(
            bgfx::createEmbeddedShader(s_embeddedShaders, type,
                                       "vs_ocornut_imgui"),
            bgfx::createEmbeddedShader(s_embeddedShaders, type,
                                       "fs_ocornut_imgui"),
            true);
    d->imageLodEnabled = bgfx::createUniform("u_imageLodEnabled",
                                             bgfx::UniformType::Vec4);
    d->imageProgram = bgfx::createProgram(
            bgfx::createEmbeddedShader(s_embeddedShaders, type,
                                       "vs_imgui_image"),
            bgfx::createEmbeddedShader(s_embeddedShaders, type,
                                       "fs_imgui_image"),
            true);
    d->layout.begin()
            .add(bgfx::Attrib::Position, 2, bgfx::AttribType::Float)
            .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
            .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
            .end();
    d->texSampler = bgfx::createUniform("s_tex", bgfx::UniformType::Sampler);

    ImFontConfig config;
    config.FontDataOwnedByAtlas = false;
    io.Fonts->AddFontFromMemoryTTF((void *)s_robotoRegularTtf,
                                   sizeof(s_robotoRegularTtf), fontSize,
                                   &config, io.Fonts->GetGlyphRangesDefault());
    return true;
}

void ImGuiBgfx::destroy(bool deviceUp)
{
    if (!d->context)
        return;
    ImGui::SetCurrentContext(d->context);
    if (deviceUp) {
        for (ImTextureData *texData : ImGui::GetPlatformIO().Textures) {
            if (texData->RefCount == 1
                && texData->GetTexID() != ImTextureID_Invalid) {
                TexId tex = bx::bitCast<TexId>(texData->GetTexID());
                bgfx::destroy(tex.handle);
                texData->SetTexID(ImTextureID_Invalid);
                texData->SetStatus(ImTextureStatus_Destroyed);
            }
        }
        bgfx::destroy(d->texSampler);
        bgfx::destroy(d->imageLodEnabled);
        bgfx::destroy(d->imageProgram);
        bgfx::destroy(d->program);
    }
    d->texSampler = BGFX_INVALID_HANDLE;
    d->imageLodEnabled = BGFX_INVALID_HANDLE;
    d->imageProgram = BGFX_INVALID_HANDLE;
    d->program = BGFX_INVALID_HANDLE;
    ImGui::DestroyContext(d->context);
    d->context = nullptr;
}

void ImGuiBgfx::newFrame(float width, float height, float pixelRatio,
                         float deltaTime)
{
    ImGui::SetCurrentContext(d->context);
    ImGuiIO &io = ImGui::GetIO();
    io.DisplaySize = ImVec2(width, height);
    io.DisplayFramebufferScale = ImVec2(pixelRatio, pixelRatio);
    io.DeltaTime = deltaTime > 0.0f ? deltaTime : 1.0f / 60.0f;
    ImGui::NewFrame();
}

void ImGuiBgfx::render(uint16_t viewId, int fbWidth, int fbHeight)
{
    ImGui::SetCurrentContext(d->context);
    ImGui::Render();
    d->draw(ImGui::GetDrawData(), bgfx::ViewId(viewId), fbWidth, fbHeight);
}

} // namespace Render
