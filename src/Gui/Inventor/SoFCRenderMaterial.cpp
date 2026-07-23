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

#include "PreCompiled.h"

#include "SoFCRenderMaterial.h"

using namespace Gui;

SO_NODE_SOURCE(SoFCRenderMaterial)

void SoFCRenderMaterial::initClass()
{
    SO_NODE_INIT_CLASS(SoFCRenderMaterial, SoNode, "Node");
}

SoFCRenderMaterial::SoFCRenderMaterial()
{
    SO_NODE_CONSTRUCTOR(SoFCRenderMaterial);
    SO_NODE_ADD_FIELD(metallic, (-1.0f));
    SO_NODE_ADD_FIELD(roughness, (-1.0f));
    SO_NODE_ADD_FIELD(water, (false));
    SO_NODE_ADD_FIELD(waterDensity, (0.0f));
    SO_NODE_ADD_FIELD(glass, (false));
    SO_NODE_ADD_FIELD(glassIOR, (0.0f));
    SO_NODE_ADD_FIELD(glassDensity, (0.0f));
    SO_NODE_ADD_FIELD(glassRoughness, (0.0f));
    SO_NODE_ADD_FIELD(cloud, (false));
    SO_NODE_ADD_FIELD(cloudDensity, (0.0f));
    SO_NODE_ADD_FIELD(cloudDetail, (0.0f));
    SO_NODE_ADD_FIELD(cloudSpeed, (1.0f));
    SO_NODE_ADD_FIELD(fire, (false));
    SO_NODE_ADD_FIELD(fireIntensity, (0.0f));
    SO_NODE_ADD_FIELD(fireDetail, (0.0f));
    SO_NODE_ADD_FIELD(fireSpeed, (1.0f));
    SO_NODE_ADD_FIELD(fountain, (false));
    SO_NODE_ADD_FIELD(fountainDensity, (0.0f));
    SO_NODE_ADD_FIELD(fountainDetail, (0.0f));
    SO_NODE_ADD_FIELD(fountainSpeed, (1.0f));
}

SO_NODE_SOURCE(SoFCRenderTexture)

void SoFCRenderTexture::initClass()
{
    SO_NODE_INIT_CLASS(SoFCRenderTexture, SoNode, "Node");
}

SoFCRenderTexture::SoFCRenderTexture()
{
    SO_NODE_CONSTRUCTOR(SoFCRenderTexture);
    SO_NODE_ADD_FIELD(slot, (EMISSIVE));
    SO_NODE_ADD_FIELD(image, (SbVec2s(0, 0), 0, nullptr));
    SO_NODE_ADD_FIELD(wrapS, (REPEAT));
    SO_NODE_ADD_FIELD(wrapT, (REPEAT));

    SO_NODE_DEFINE_ENUM_VALUE(Slot, EMISSIVE);
    SO_NODE_DEFINE_ENUM_VALUE(Slot, OCCLUSION);
    SO_NODE_DEFINE_ENUM_VALUE(Slot, METALLIC_ROUGHNESS);
    SO_NODE_SET_SF_ENUM_TYPE(slot, Slot);

    SO_NODE_DEFINE_ENUM_VALUE(Wrap, REPEAT);
    SO_NODE_DEFINE_ENUM_VALUE(Wrap, CLAMP);
    SO_NODE_SET_SF_ENUM_TYPE(wrapS, Wrap);
    SO_NODE_SET_SF_ENUM_TYPE(wrapT, Wrap);
}
