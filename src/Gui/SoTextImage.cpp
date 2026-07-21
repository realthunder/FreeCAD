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

#include "PreCompiled.h"

#ifndef _PreComp_
# include <algorithm>
# include <QFont>
# include <QFontMetrics>
# include <QImage>
# include <QPainter>
# include <QString>
# include <Inventor/SoPrimitiveVertex.h>
# include <Inventor/actions/SoCallbackAction.h>
# include <Inventor/elements/SoViewportRegionElement.h>
# include <Inventor/misc/SoState.h>
# include <Inventor/nodes/SoFont.h>
# include <Inventor/nodes/SoSeparator.h>
# include <Inventor/nodes/SoText2.h>
# include <Inventor/nodes/SoTexture2.h>
#endif

#include <Gui/BitmapFactory.h>
#include <Gui/Tools.h>

#include "SoTextImage.h"
#include "Inventor/SoAutoZoomTranslation.h"

using namespace Gui;

SO_NODE_SOURCE(SoTextImage)

void SoTextImage::initClass()
{
    SO_NODE_INIT_CLASS(SoTextImage, SoShape, "Shape");
}

SoTextImage::SoTextImage()
{
    SO_NODE_CONSTRUCTOR(SoTextImage);

    SO_NODE_ADD_FIELD(string, (""));
    SO_NODE_ADD_FIELD(fontName, ("Helvetica"));
    SO_NODE_ADD_FIELD(fontSize, (10.0f));
    SO_NODE_ADD_FIELD(justification, (LEFT));
    SO_NODE_ADD_FIELD(spacing, (1.0f));
    SO_NODE_ADD_FIELD(forceTexCoords, (TRUE));

    // Mirror SoText2::Justification so callers can pass its values straight
    // through (LEFT=1, RIGHT=2, CENTER=3).
    SO_NODE_DEFINE_ENUM_VALUE(Justification, LEFT);
    SO_NODE_DEFINE_ENUM_VALUE(Justification, RIGHT);
    SO_NODE_DEFINE_ENUM_VALUE(Justification, CENTER);
    SO_NODE_SET_SF_ENUM_TYPE(justification, Justification);
}

SoTextImage::~SoTextImage() = default;

SoSeparator* SoTextImage::createSubGraph(SoTextImage** outImage)
{
    auto texture = new SoTexture2;
    // White glyph atlas tinted by the current material: text follows selection /
    // pre-selection highlight colour without baking anything.
    texture->model = SoTexture2::MODULATE;

    auto zoom = new SoAutoZoomTranslation;

    auto shape = new SoTextImage;
    shape->imageTexture = texture;
    shape->imageZoom = zoom;

    auto root = new SoSeparator;
    // The companion reads its own fields back during capture; keep it out of the
    // render/bbox caches so field edits always re-emit.
    root->renderCaching = SoSeparator::OFF;
    root->boundingBoxCaching = SoSeparator::OFF;
    root->addChild(texture);
    root->addChild(zoom);
    root->addChild(shape);

    if (outImage)
        *outImage = shape;
    return root;
}

SoSeparator* SoTextImage::createFor(SoText2* label, float fontSize, const char* fontName)
{
    SoTextImage* img = nullptr;
    SoSeparator* root = createSubGraph(&img);

    img->fontSize = fontSize;
    if (fontName)
        img->fontName = fontName;
    // Justification is effectively static per label; copy it (avoids an enum
    // cross-type field connection). SoText2 and SoTextImage share enum values.
    img->justification = label->justification.getValue();
    // Track the dynamic content live: when the label's text/spacing changes the
    // companion re-rasterizes through its notify().
    img->string.connectFrom(&label->string);
    img->spacing.connectFrom(&label->spacing);
    return root;
}

SoSeparator* SoTextImage::createFor(SoText2* label, SoFont* font)
{
    SoTextImage* img = nullptr;
    SoSeparator* root = createSubGraph(&img);

    img->justification = label->justification.getValue();
    img->string.connectFrom(&label->string);
    img->spacing.connectFrom(&label->spacing);
    // Live font: re-rasterize when the ViewProvider changes size/name.
    img->fontSize.connectFrom(&font->size);
    img->fontName.connectFrom(&font->name);
    return root;
}

void SoTextImage::updateImage()
{
    imageDirty = false;

    const int lines = this->string.getNum();
    // Any empty content -> clear (companion stays invisible).
    bool hasText = false;
    for (int i = 0; i < lines; ++i) {
        if (this->string[i].getLength() > 0) { hasText = true; break; }
    }
    if (!hasText || lines == 0) {
        this->image = SoSFImage();
        this->imgSize = SbVec2s(0, 0);
        if (this->imageTexture)
            this->imageTexture->image.setValue(SbVec2s(0, 0), 0, nullptr);
        return;
    }

    QFont font(QString::fromUtf8(this->fontName.getValue().getString()));
    int px = std::max(1, (int)std::lround(this->fontSize.getValue()));
    font.setPixelSize(px);
    QFontMetrics fm(font);

    const int pitch = std::max(1, (int)std::lround(fm.height() * this->spacing.getValue()));

    int blockW = 0;
    for (int i = 0; i < lines; ++i) {
        QString s = QString::fromUtf8(this->string[i].getString());
        blockW = std::max(blockW, Gui::QtTools::horizontalAdvance(fm, s));
    }
    const int blockH = pitch * lines;
    if (blockW <= 0 || blockH <= 0) {
        this->image = SoSFImage();
        this->imgSize = SbVec2s(0, 0);
        if (this->imageTexture)
            this->imageTexture->image.setValue(SbVec2s(0, 0), 0, nullptr);
        return;
    }

    // White glyph (rgb=255, alpha=coverage) on a transparent, *straight*-alpha
    // canvas so BitmapFactory::convert (which reads unpremultiplied pixels) keeps
    // rgb=1 for MODULATE. Premultiplied would darken antialiased edges.
    QImage qimg(blockW, blockH, QImage::Format_ARGB32);
    qimg.fill(QColor(255, 255, 255, 0));

    QPainter painter(&qimg);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::TextAntialiasing);
    painter.setPen(QColor(255, 255, 255));
    painter.setFont(font);

    const int just = this->justification.getValue();
    int flag = Qt::AlignVCenter;
    flag |= (just == RIGHT) ? Qt::AlignRight
          : (just == CENTER) ? Qt::AlignHCenter
          : Qt::AlignLeft;
    for (int i = 0; i < lines; ++i) {
        QString s = QString::fromUtf8(this->string[i].getString());
        painter.drawText(QRect(0, i * pitch, blockW, pitch), flag, s);
    }
    painter.end();

    Gui::BitmapFactory().convert(qimg, this->image);
    this->imgSize = SbVec2s((short)blockW, (short)blockH);
    if (this->imageTexture) {
        SbVec2s sz; int nc;
        const unsigned char* bytes = this->image.getValue(sz, nc);
        if (bytes && sz[0] > 0 && sz[1] > 0)
            this->imageTexture->image.setValue(sz, nc, bytes);
    }

    // Block bottom-left corner offset from the projected origin, in pixels,
    // reproducing SoText2's placement: horizontal by justification, vertical so
    // the first line's baseline sits at the origin (block grows upward, extra
    // lines below). See SoTextLabel::GLRender for the same arithmetic.
    const float w = (float)blockW;
    const float h = (float)blockH;
    this->offX = (just == RIGHT) ? -w : (just == CENTER) ? -0.5f * w : 0.f;
    this->offY = (lines > 1) ? -(float(lines - 1) / (float)lines) * h : 0.f;
}

void SoTextImage::syncAutoZoom(SoAction* action)
{
    if (!this->imageZoom)
        return;
    const SbViewportRegion& vp = SoViewportRegionElement::get(action->getState());
    float vph = (float)vp.getViewportSizePixels()[1];
    if (vph <= 0.f)
        return;
    // Same calibration as SoDatumLabel's glyph companion: the backend applies
    // scaleFactor * worldToScreenScale / (5*aspect); k=7.5/vph makes the native
    // pixel quad render at the GL text's screen size. Set only on change so it
    // does not thrash the render cache each frame.
    float sf = 7.5f / vph;
    if (this->imageZoom->scaleFactor.getValue() != sf)
        this->imageZoom->scaleFactor.setValue(sf);
}

void SoTextImage::emitQuad(SoAction* action)
{
    if (this->imageDirty)
        updateImage();
    if (this->imgSize[0] <= 0 || this->imgSize[1] <= 0)
        return;

    const float x0 = this->offX;
    const float y0 = this->offY;
    const float x1 = x0 + (float)this->imgSize[0];
    const float y1 = y0 + (float)this->imgSize[1];

    // Bottom-up glyph atlas: v=0 is the bottom row.
    struct Corner { float x, y, u, v; };
    const Corner corners[4] = {
        {x0, y0, 0.f, 0.f},
        {x1, y0, 1.f, 0.f},
        {x1, y1, 1.f, 1.f},
        {x0, y1, 0.f, 1.f},
    };

    SoPrimitiveVertex pv;
    pv.setNormal(SbVec3f(0.f, 0.f, 1.f));
    pv.setMaterialIndex(0);
    auto emit = [&](int i) {
        pv.setPoint(SbVec3f(corners[i].x, corners[i].y, 0.f));
        pv.setTextureCoords(SbVec4f(corners[i].u, corners[i].v, 0.f, 1.f));
        shapeVertex(&pv);
    };

    this->beginShape(action, TRIANGLES);
    emit(0); emit(1); emit(2);
    emit(0); emit(2); emit(3);
    this->endShape();
}

void SoTextImage::generatePrimitives(SoAction* action)
{
    // Only feed the render-cache capture (SoCallbackAction); stay invisible to
    // ray picking and every other action. The owning SoText2 handles GL draw
    // and selection on the desktop.
    if (!action->isOfType(SoCallbackAction::getClassTypeId()))
        return;
    syncAutoZoom(action);
    emitQuad(action);
}

void SoTextImage::computeBBox(SoAction* action, SbBox3f& box, SbVec3f& center)
{
    // The quad is emitted at the origin in native pixels and placed screen-
    // constant by the autozoom, so contribute only a point at the origin: it
    // neither dominates fitAll nor leaves an invalid bbox. Also a convenient
    // hook to keep the autozoom calibrated for the next capture.
    syncAutoZoom(action);
    box.setBounds(SbVec3f(0.f, 0.f, 0.f), SbVec3f(0.f, 0.f, 0.f));
    center = SbVec3f(0.f, 0.f, 0.f);
}

void SoTextImage::notify(SoNotList* list)
{
    SoField* f = list->getLastField();
    if (f == &this->string || f == &this->fontName || f == &this->fontSize
        || f == &this->justification || f == &this->spacing) {
        this->imageDirty = true;
        // Re-raster eagerly so the sibling SoTexture2 is current before the next
        // traversal reaches it (texture nodes are visited before this shape).
        updateImage();
    }
    inherited::notify(list);
}
