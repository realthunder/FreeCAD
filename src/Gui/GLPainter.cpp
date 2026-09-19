/***************************************************************************
 *   Copyright (c) 2013 Werner Mayer <wmayer[at]users.sourceforge.net>     *
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

#include "PreCompiled.h"

#ifndef _PreComp_
#endif

#include <Inventor/nodes/SoCoordinate3.h>
#include <Inventor/nodes/SoDrawStyle.h>
#include <Inventor/nodes/SoIndexedFaceSet.h>
#include <Inventor/nodes/SoIndexedLineSet.h>
#include <Inventor/nodes/SoLightModel.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoSeparator.h>

#include "GLPainter.h"
#include "View3DInventorViewer.h"


using namespace Gui;

namespace {
// Guarded field writers for the overlay graphs: Coin notifies on every
// field write, and a notification means a cache rebuild plus a backend
// re-feed — only touch fields whose values actually changed.
void syncPoints(SoCoordinate3 *coord, const SbVec3f *pts, int n)
{
    bool same = coord->point.getNum() == n;
    for (int i = 0; same && i < n; ++i)
        same = coord->point[i] == pts[i];
    if (same)
        return;
    coord->point.setValues(0, n, pts);
    if (coord->point.getNum() != n)
        coord->point.setNum(n);
}

void syncIndices(SoIndexedLineSet *lines, const int32_t *idx, int n)
{
    bool same = lines->coordIndex.getNum() == n;
    for (int i = 0; same && i < n; ++i)
        same = lines->coordIndex[i] == idx[i];
    if (same)
        return;
    if (n > 0)
        lines->coordIndex.setValues(0, n, idx);
    if (lines->coordIndex.getNum() != n)
        lines->coordIndex.setNum(n);
}

void syncColor(SoMaterial *mat, const SbColor &col, float alpha)
{
    if (mat->diffuseColor.getNum() != 1 || mat->diffuseColor[0] != col)
        mat->diffuseColor = col;
    float transp = 1.0F - alpha;
    if (mat->transparency.getNum() != 1 || mat->transparency[0] != transp)
        mat->transparency = transp;
}
} // namespace

TYPESYSTEM_SOURCE_ABSTRACT(Gui::GLGraphicsItem, Base::BaseClass)

GLPainter::GLPainter()
{
    depthrange[0] = 0;
    depthrange[1] = 0;
    for (int i=0; i<16; i++)
        projectionmatrix[i] = 0.0;
}

GLPainter::~GLPainter()
{
    end();
}

bool GLPainter::begin(QPaintDevice * device)
{
    if (viewer)
        return false;

    viewer = dynamic_cast<QtGLWidget*>(device);
    if (!viewer)
        return false;

    // Make current context
    QSize view = viewer->size();
    this->width = view.width();
    this->height = view.height();

    viewer->makeCurrent();

    glMatrixMode(GL_PROJECTION);
    glPushMatrix();

    glLoadIdentity();
    glOrtho(0, this->width, 0, this->height, -1, 1);

    // Store GL state
    glPushAttrib(GL_ALL_ATTRIB_BITS);
    glGetFloatv(GL_DEPTH_RANGE, this->depthrange);
    glGetDoublev(GL_PROJECTION_MATRIX, this->projectionmatrix);

    glDepthFunc(GL_ALWAYS);
    glDepthMask(GL_TRUE);
    glDepthRange(0,0);
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_LIGHTING);
    glEnable(GL_COLOR_MATERIAL);
    glDisable(GL_BLEND);

    glLineWidth(1.0f);
    glColor4f(1.0, 1.0, 1.0, 0.0);
    glViewport(0, 0, this->width, this->height);

    return true;
}

bool GLPainter::end()
{
    if (!viewer)
        return false;

    glFlush();

    if (this->logicOp) {
        this->logicOp = false;
        glDisable(GL_COLOR_LOGIC_OP);
    }

    if (this->lineStipple) {
        this->lineStipple = false;
        glDisable(GL_LINE_STIPPLE);
    }

    // Reset original state
    glDepthRange(this->depthrange[0], this->depthrange[1]);
    glMatrixMode(GL_PROJECTION);
    glLoadMatrixd(this->projectionmatrix);

    glPopAttrib();
    glPopMatrix();

    viewer = nullptr;
    return true;
}

bool GLPainter::isActive() const
{
    return viewer != nullptr;
}

void GLPainter::setLineWidth(float w)
{
    glLineWidth(w);
}

void GLPainter::setPointSize(float s)
{
    glPointSize(s);
}

void GLPainter::setColor(float r, float g, float b, float a)
{
    glColor4f(r, g, b, a);
}

void GLPainter::setLogicOp(GLenum mode)
{
    glEnable(GL_COLOR_LOGIC_OP);
    glLogicOp(mode);
    this->logicOp = true;
}

void GLPainter::resetLogicOp()
{
    glDisable(GL_COLOR_LOGIC_OP);
    this->logicOp = false;
}

void GLPainter::setDrawBuffer(GLenum mode)
{
    glDrawBuffer(mode);
}

void GLPainter::setLineStipple(GLint factor, GLushort pattern)
{
    glEnable(GL_LINE_STIPPLE);
    glLineStipple(factor, pattern);
    this->lineStipple = true;
}

void GLPainter::resetLineStipple()
{
    glDisable(GL_LINE_STIPPLE);
    this->lineStipple = false;
}

// Draw routines
void GLPainter::drawRect(int x1, int y1, int x2, int y2)
{
    if (!viewer)
        return;

    glBegin(GL_LINE_LOOP);
        glVertex3i(x1, this->height-y1, 0);
        glVertex3i(x2, this->height-y1, 0);
        glVertex3i(x2, this->height-y2, 0);
        glVertex3i(x1, this->height-y2, 0);
    glEnd();
}

void GLPainter::drawLine(int x1, int y1, int x2, int y2)
{
    if (!viewer)
        return;

    glBegin(GL_LINES);
        glVertex3i(x1, this->height-y1, 0);
        glVertex3i(x2, this->height-y2, 0);
    glEnd();
}

void GLPainter::drawPoint(int x, int y)
{
    if (!viewer)
        return;

    glBegin(GL_POINTS);
        glVertex3i(x, this->height-y, 0);
    glEnd();
}

//-----------------------------------------------

Rubberband::Rubberband(ViewerContext* v) : viewer(v)
{
    x_old = y_old = x_new = y_new = 0;
    working = false;
    stipple = true;

    rgb_r = 1.0f;
    rgb_g = 1.0f;
    rgb_b = 1.0f;
    rgb_a = 1.0f;
}

Rubberband::Rubberband() : viewer(nullptr)
{
    x_old = y_old = x_new = y_new = 0;
    working = false;
    stipple = true;

    rgb_r = 1.0f;
    rgb_g = 1.0f;
    rgb_b = 1.0f;
    rgb_a = 1.0f;
}

Rubberband::~Rubberband() = default;

void Rubberband::setWorking(bool on)
{
    working = on;
}

void Rubberband::setViewer(ViewerContext* v)
{
    viewer = v;
}

void Rubberband::setCoords(int x1, int y1, int x2, int y2)
{
    x_old = x1;
    y_old = y1;
    x_new = x2;
    y_new = y2;
}

void Rubberband::setLineStipple(bool on)
{
    stipple = on;
}

void Rubberband::setColor(float r, float g, float b, float a)
{
    rgb_a = a;
    rgb_b = b;
    rgb_g = g;
    rgb_r = r;
}

void Rubberband::paintGL()
{
    if (!working)
        return;

    const SbViewportRegion vp = viewer->getSoRenderManager()->getViewportRegion();
    SbVec2s size = vp.getViewportSizePixels();

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, size[0], size[1], 0, 0, 100);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glDisable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glLineWidth(4.0);
    glColor4f(1.0f, 1.0f, 1.0f, 0.5f);
    glRecti(x_old, y_old, x_new, y_new);

    glLineWidth(4.0);
    glColor4f(rgb_r, rgb_g, rgb_b, rgb_a);
    if (stipple) {
        glLineStipple(3, 0xAAAA);
        glEnable(GL_LINE_STIPPLE);
    }
    glBegin(GL_LINE_LOOP);
    glVertex2i(x_old, y_old);
    glVertex2i(x_old, y_new);
    glVertex2i(x_new, y_new);
    glVertex2i(x_new, y_old);
    glEnd();

    glLineWidth(1.0);

    if (stipple)
        glDisable(GL_LINE_STIPPLE);

    glDisable(GL_BLEND);
}

// Coin overlay twin of Rubberband::paintGL() for the external render
// backend: a pixel-space translucent fill plus the (optionally stippled)
// frame. Built once, fields mutated in place per call.
SoSeparator *Rubberband::getOverlaySceneGraph()
{
    if (!working)
        return nullptr;

    if (!overlayRoot) {
        overlayRoot = new SoSeparator;
        auto lightModel = new SoLightModel; // like glDisable(GL_LIGHTING)
        lightModel->model = SoLightModel::BASE_COLOR;
        overlayRoot->addChild(lightModel);
        overlayCoords = new SoCoordinate3;
        overlayRoot->addChild(overlayCoords);

        // Translucent fill (glRecti with white 50% alpha).
        auto fillMaterial = new SoMaterial;
        fillMaterial->diffuseColor = SbColor(1.0F, 1.0F, 1.0F);
        fillMaterial->transparency = 0.5F;
        overlayRoot->addChild(fillMaterial);
        auto fill = new SoIndexedFaceSet;
        static const int32_t quad[] = {0, 1, 2, 3, -1};
        fill->coordIndex.setValues(0, 5, quad);
        overlayRoot->addChild(fill);

        // Frame (4px line loop, glLineStipple(3, 0xAAAA) when stippled).
        overlayFrameMaterial = new SoMaterial;
        overlayRoot->addChild(overlayFrameMaterial);
        overlayFrameStyle = new SoDrawStyle;
        overlayFrameStyle->lineWidth = 4.0F;
        overlayFrameStyle->linePatternScaleFactor = 3;
        overlayRoot->addChild(overlayFrameStyle);
        auto frame = new SoIndexedLineSet;
        static const int32_t loop[] = {0, 1, 2, 3, 0, -1};
        frame->coordIndex.setValues(0, 6, loop);
        overlayRoot->addChild(frame);
    }

    const SbVec3f pts[4] = {
        {float(x_old), float(y_old), 0.0F},
        {float(x_old), float(y_new), 0.0F},
        {float(x_new), float(y_new), 0.0F},
        {float(x_new), float(y_old), 0.0F},
    };
    syncPoints(overlayCoords, pts, 4);
    syncColor(overlayFrameMaterial, SbColor(rgb_r, rgb_g, rgb_b), rgb_a);
    uint16_t pattern = stipple ? 0xAAAA : 0xFFFF;
    if (overlayFrameStyle->linePattern.getValue() != pattern)
        overlayFrameStyle->linePattern = pattern;
    return overlayRoot;
}

// -----------------------------------------------------------------------------------

Polyline::Polyline(ViewerContext* v) : viewer(v)
{
    x_new = y_new = 0;
    working = false;
    closed = true;
    stippled = false;
    line = 2.0;

    rgb_r = 1.0f;
    rgb_g = 1.0f;
    rgb_b = 1.0f;
    rgb_a = 1.0f;
}

Polyline::Polyline() : viewer(nullptr)
{
    x_new = y_new = 0;
    working = false;
    closed = true;
    stippled = false;
    line = 2.0;

    rgb_r = 1.0f;
    rgb_g = 1.0f;
    rgb_b = 1.0f;
    rgb_a = 1.0f;
}

Polyline::~Polyline() = default;

void Polyline::setWorking(bool on)
{
    working = on;
}

bool Polyline::isWorking() const
{
    return working;
}

void Polyline::setViewer(ViewerContext* v)
{
    viewer = v;
}

void Polyline::setCoords(int x, int y)
{
    x_new = x;
    y_new = y;
}

void Polyline::setColor(int r, int g, int b, int a)
{
    rgb_r = r;
    rgb_g = g;
    rgb_b = b;
    rgb_a = a;
}

void Polyline::setClosed(bool c)
{
    closed = c;
}

void Polyline::setCloseStippled(bool c)
{
    stippled = c;
}

void Polyline::setLineWidth(float l)
{
    line = l;
}

void Polyline::addNode(const QPoint& p)
{
    _cNodeVector.push_back(p);
}

void Polyline::popNode()
{
    if (!_cNodeVector.empty())
        _cNodeVector.pop_back();
}

void Polyline::clear()
{
    _cNodeVector.clear();
}

void Polyline::paintGL()
{
    if (!working)
        return;

    if (_cNodeVector.empty())
        return;

    const SbViewportRegion vp = viewer->getSoRenderManager()->getViewportRegion();
    SbVec2s size = vp.getViewportSizePixels();

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, size[0], size[1], 0, 0, 100);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glDisable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glLineWidth(line);
    glColor4f(rgb_r, rgb_g, rgb_b, rgb_a);

    if (closed && !stippled) {
        glBegin(GL_LINE_LOOP);

        for (const QPoint& it : _cNodeVector) {
            glVertex2i(it.x(), it.y());
        }

        glEnd();
    }
    else {
        glBegin(GL_LINES);

        QPoint start = _cNodeVector.front();
        for (const QPoint& it : _cNodeVector) {
            glVertex2i(start.x(), start.y());
            start = it;
            glVertex2i(it.x(), it.y());
        }

        glEnd();

        if (closed && stippled) {
            glEnable(GL_LINE_STIPPLE);
            glLineStipple(2, 0x3F3F);
            glBegin(GL_LINES);
                glVertex2i(_cNodeVector.back().x(), _cNodeVector.back().y());
                glVertex2i(_cNodeVector.front().x(), _cNodeVector.front().y());
            glEnd();
            glDisable(GL_LINE_STIPPLE);
        }
    }

    glDisable(GL_BLEND);
}

// Coin overlay twin of Polyline::paintGL(): the node strip (closed into a
// loop when closed && !stippled) plus the optional stippled closing edge.
SoSeparator *Polyline::getOverlaySceneGraph()
{
    if (!working || _cNodeVector.empty())
        return nullptr;

    if (!overlayRoot) {
        overlayRoot = new SoSeparator;
        auto lightModel = new SoLightModel;
        lightModel->model = SoLightModel::BASE_COLOR;
        overlayRoot->addChild(lightModel);
        overlayMaterial = new SoMaterial;
        overlayRoot->addChild(overlayMaterial);
        overlayStyle = new SoDrawStyle;
        overlayRoot->addChild(overlayStyle);
        overlayCoords = new SoCoordinate3;
        overlayRoot->addChild(overlayCoords);
        overlayLines = new SoIndexedLineSet;
        overlayRoot->addChild(overlayLines);
        // Closing edge in paintGL()'s glLineStipple(2, 0x3F3F) style.
        overlayCloseStyle = new SoDrawStyle;
        overlayCloseStyle->linePattern = 0x3F3F;
        overlayCloseStyle->linePatternScaleFactor = 2;
        overlayRoot->addChild(overlayCloseStyle);
        overlayCloseLine = new SoIndexedLineSet;
        overlayRoot->addChild(overlayCloseLine);
    }

    int n = int(_cNodeVector.size());
    std::vector<SbVec3f> pts;
    pts.reserve(n);
    for (const QPoint &p : _cNodeVector)
        pts.emplace_back(float(p.x()), float(p.y()), 0.0F);
    syncPoints(overlayCoords, pts.data(), n);
    syncColor(overlayMaterial, SbColor(rgb_r, rgb_g, rgb_b), rgb_a);
    if (overlayStyle->lineWidth.getValue() != line)
        overlayStyle->lineWidth = line;

    std::vector<int32_t> idx;
    idx.reserve(n + 2);
    for (int i = 0; i < n; ++i)
        idx.push_back(i);
    if (closed && !stippled)
        idx.push_back(0);
    idx.push_back(-1);
    syncIndices(overlayLines, idx.data(), int(idx.size()));

    if (closed && stippled && n > 1) {
        const int32_t close[] = {n - 1, 0, -1};
        syncIndices(overlayCloseLine, close, 3);
    }
    else {
        syncIndices(overlayCloseLine, nullptr, 0);
    }
    return overlayRoot;
}
