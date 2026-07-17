/****************************************************************************
 *   Copyright (c) 2021 Zheng Lei (realthunder) <realthunder.dev@gmail.com> *
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

#ifndef RENDERER_BGFX_RENDERER_H
#define RENDERER_BGFX_RENDERER_H

#include "Renderer.h"

namespace Render {

class BGFXRendererLibP;

class BGFXRendererLib : public RendererLib
{
public:
    BGFXRendererLib();
    virtual const std::string &name() const override;
    virtual const std::vector<std::string> &types() const override;
    virtual std::unique_ptr<Renderer> create(
            const std::string &type, QOpenGLWidget *widget) const override;
};

class BGFXRenderer : public Renderer
{
public:
    BGFXRenderer(QOpenGLWidget *widget);
    ~BGFXRenderer();
    virtual const std::string &type() const override;
    virtual bool render(const QColor &bg,
                        const void *viewMatrix,
                        const void *projMatrix) override;
    virtual bool boundBox(float &xmin, float &ymin, float &zmin,
                          float &xmax, float &ymax, float &zmax) override;

    virtual void setScene(DrawCallList &&draws) override;
    virtual void setBackground(const Background &bg) override;
    virtual void addSelection(int id, DrawCallList &&draws) override;
    virtual void removeSelection(int id) override;
    virtual void setHighlight(DrawCallList &&draws, bool wholeOnTop) override;
    virtual void clearHighlight() override;
    virtual void setHiddenLineConfig(const HiddenLineConfig &config) override;
    virtual void setSectionConfig(const SectionConfig &config) override;
    virtual void setAOConfig(const AOConfig &config) override;
    virtual void setPBRConfig(const PBRConfig &config) override;
    virtual void setAutoZoomScale(float scale) override;
    virtual void setHatchImage(const void *data, int nc,
                               int width, int height) override;
    virtual bool needsRedraw() const override;
    virtual bool canSkipInternal() const override;

    friend class BGFXRendererLib;
    friend class BGFXRendererLibP;

private:
    class Private;
    std::unique_ptr<Private> pimpl;
};

} // namespace Renderer

#endif // RENDERER_BGFX_RENDERER_H
