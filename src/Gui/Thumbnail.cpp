/***************************************************************************
 *   Copyright (c) 2008 Werner Mayer <wmayer[at]users.sourceforge.net>     *
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
# include <QApplication>
# include <QBuffer>
# include <QByteArray>
# include <QDateTime>
# include <QImage>
# include <QThread>
#endif

#include <App/Application.h>
#include <App/DocumentParams.h>
#include <Base/Console.h>
#include <Base/Reader.h>
#include <Base/Writer.h>

#include "Thumbnail.h"
#include "BitmapFactory.h"
#include "View3DInventorViewer.h"


using namespace Gui;

Thumbnail::Thumbnail(int s) : size(s)
{
}

Thumbnail::~Thumbnail() = default;

void Thumbnail::setViewer(View3DInventorViewer* v)
{
    this->viewer = v;
}

void Thumbnail::setSize(int s)
{
    this->size = s;
}

void Thumbnail::setFileName(const char* fn)
{
    this->uri = QUrl::fromLocalFile(QString::fromUtf8(fn));
}

void Thumbnail::setImageFile(const char *fn)
{
    if (!fn || !fn[0]) {
        image = QImage();
        return;
    }
    QImage img(QString::fromUtf8(fn));
    if (img.isNull()) {
        Base::Console().Error("Failed to load thumbnail image %s", fn);
        return;
    }
    image = img;
}

void Thumbnail::setUpdateOnSave(bool enable)
{
    this->updateOnSave = enable;
}

unsigned int Thumbnail::getMemSize () const
{
    return 0;
}

void Thumbnail::Save (Base::Writer &writer) const
{
    if (!image.isNull() || updateOnSave)
        writer.addFile("thumbnails/Thumbnail.png", this);
}

void Thumbnail::Restore(Base::XMLReader &reader)
{
    reader.addFile("thumbnails/Thumbnail.png",this);
}

void Thumbnail::SaveDocFile (Base::Writer &writer) const
{
    QImage img;

    if (!updateOnSave || !viewer) {
        img = image;
    } else if (updateOnSave) {
        if (this->viewer->thread() != QThread::currentThread()) {
            qWarning("Cannot create a thumbnail from non-GUI thread");
            return;
        }

        int imageSize = this->size;
        if (imageSize <= 0)
            imageSize = Base::clamp<int>(App::DocumentParams::getThumbnailSize(), 64, 1024);
        QColor bgcolor;
        if (App::DocumentParams::getThumbnailNoBackground())
            bgcolor = QColor(0,0,0,0);
        this->viewer->imageFromFramebuffer(imageSize, imageSize,
                App::DocumentParams::getThumbnailSampleSize(), bgcolor, img);

        QPixmap appIcon = Gui::BitmapFactory().pixmap(App::Application::Config()["AppIcon"].c_str());
        if (img.isNull()) {
            img = appIcon.toImage();
        } else if (App::DocumentParams::getAddThumbnailLogo()) {
            // Create a small "Fc" Application icon in the bottom right of the thumbnail
            // only scale app icon if an offscreen image could be created
            appIcon = appIcon.scaled(imageSize / 4, imageSize /4, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            QPixmap px(QPixmap::fromImage(img));
            if (!px.isNull() && !appIcon.isNull())
                img = BitmapFactory().merge(px, appIcon, BitmapFactoryInst::BottomRight).toImage();
        }
        image = img;
    }

    if (!img.isNull()) {
        // according to specification add some meta-information to the image
        qint64 mt = QDateTime::currentDateTime().toTime_t();
        QString mtime = QStringLiteral("%1").arg(mt);
        img.setText(QStringLiteral("Software"), qApp->applicationName());
        img.setText(QStringLiteral("Thumb::Mimetype"), QStringLiteral("application/x-extension-fcstd"));
        img.setText(QStringLiteral("Thumb::MTime"), mtime);
        img.setText(QStringLiteral("Thumb::URI"), this->uri.toString());

        QByteArray ba;
        QBuffer buffer(&ba);
        buffer.open(QIODevice::WriteOnly);
        img.save(&buffer, "PNG");
        writer.Stream().write(ba.constData(), ba.length());
    }
}

void Thumbnail::RestoreDocFile(Base::Reader &reader)
{
    QByteArray data;
    char buf[4096];
    while(!reader.eof()) {
        int count = reader.read(buf, sizeof(buf)).gcount();
        data.append(buf, count);
    }
    image.loadFromData(data);
}

const QImage &Thumbnail::getImage() const
{
    return image;
}
