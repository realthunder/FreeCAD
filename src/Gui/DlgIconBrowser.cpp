/****************************************************************************
 *   Copyright (c) 2022 Zheng Lei (realthunder) <realthunder.dev@gmail.com> *
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
#ifndef _PreComp_
# include <QTextStream>
# include <QFile>
# include <QMessageBox>
# include <QTime>
#endif

#include <QCompleter>
#include <QFileInfo>
#include <QDir>
#include <QDesktopServices>
#include <QUrl>

#include "ui_DlgIconBrowser.h"

#include <boost/algorithm/string/predicate.hpp>
#include <Base/Tools.h>
#include <Base/Console.h>
#include <App/Application.h>
#include <App/Document.h>
#include <App/DocumentObject.h>
#include "MainWindow.h"
#include "BitmapFactory.h"
#include "PrefWidgets.h"
#include "Tree.h"
#include "FileDialog.h"
#include "PreferencePages/DlgSettingsGeneral.h"
#include "DlgIconBrowser.h"

FC_LOG_LEVEL_INIT("Gui", true, true)

using namespace Gui;
using namespace Gui::Dialog;

DlgIconBrowser::DlgIconBrowser(QWidget* parent)
  : CustomizeActionPage(parent)
  , ui(new Ui_DlgIconBrowser)
  , widgetStates(new Gui::PrefWidgetStates(this, false))
{
    ui->setupUi(this);
    widgetStates->addSplitter(ui->splitter);
    ui->treeWidget->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);

    ui->editPrefix->setPlaceholderText(QStringLiteral("MyIcons"));

    ui->spinBoxIconSize->onRestore();
    ui->editPrefix->onRestore();
    on_spinBoxIconSize_valueChanged(ui->spinBoxIconSize->value());

    on_btnRefresh_clicked();
    timer.setSingleShot(true);
    connect(&timer, SIGNAL(timeout()), this, SLOT(onTimer()));
    timerFilter.setSingleShot(true);
    connect(&timerFilter, SIGNAL(timeout()), this, SLOT(onTimerFilter()));
    timerRefresh.setSingleShot(true);
    connect(&timerRefresh, SIGNAL(timeout()), this, SLOT(on_btnRefresh_clicked()));

    ui->IconSets->setAutoSave(true);

    this->hGrp = App::GetApplication().GetParameterGroupByPath("User parameter:BaseApp/Preferences/MainWindow");
    this->hGrp->Attach(this);
}

DlgIconBrowser::~DlgIconBrowser()
{
    this->hGrp->Detach(this);
    ui->spinBoxIconSize->onSave();
    ui->editPrefix->onSave();
}

void DlgIconBrowser::changeEvent(QEvent *e)
{
    if (e->type() == QEvent::StyleChange)
        on_btnRefresh_clicked();
    CustomizeActionPage::changeEvent(e);
}

void DlgIconBrowser::on_spinBoxIconSize_valueChanged(int value)
{
    if (value == 0)
        value = treeViewIconSize();
    ui->treeWidget->setIconSize(QSize(value, value));
}

void DlgIconBrowser::OnChange(Base::Subject<const char*> &, const char *reason)
{
    if (boost::equals(reason, "IconSet"))
        timerRefresh.start(500);
}

void DlgIconBrowser::on_btnExport_clicked()
{
    std::vector<QTreeWidgetItem*> items;
    for (auto it = QTreeWidgetItemIterator(ui->treeWidget, QTreeWidgetItemIterator::Checked); *it; ++it)
        items.push_back(*it);
    if (items.empty())
        return;

    QString dirname;
    QString prefix = QStringLiteral("Gui/IconSets/") + getPrefix();
    QDir dir(QString::fromUtf8(App::Application::getUserAppDataDir().c_str()));
    dir.mkpath(prefix);
    dir.cd(prefix);
    dirname = dir.absolutePath();
    qint64 timestamp = QDateTime::currentSecsSinceEpoch();

    auto checkExisting = [timestamp](const QString &filename) {
        if (!QFile::exists(filename))
            return true;
        QFileInfo fi(filename);
        QString suffix = fi.suffix();
        if (suffix.size())
            suffix = QStringLiteral(".") + suffix;
        QString base = QStringLiteral("%1.%2").arg(fi.completeBaseName()).arg(timestamp);
        for (int retry=0; retry<10; ++retry) {
            QString newname;
            if (retry == 0)
                newname = base + suffix;
            else
                newname = QStringLiteral("%1.%2%3").arg(base).arg(retry).arg(suffix);
            QString newpath = fi.absoluteDir().filePath(newname);
            if (QFile::rename(filename, newpath)) {
                FC_MSG("Rename existing icon file " << filename.toUtf8().constData()
                        << " -> " << newname.toUtf8().constData());
                return true;
            }
        }
        FC_ERR("Failed to rename existing icon file " << filename.toUtf8().constData());
        return false;
    };
    int count = 0;
    for (auto item : items) {
        QString name = item->text(0);
        QString basename = QFileInfo(name).completeBaseName();
        QString path = item->text(2);
        bool res = true;
        if (path.size()) {
            QString dest = dirname + QLatin1Char('/') + basename;
            QString suffix = QFileInfo(path).suffix();
            if (suffix.size()) {
                dest += QStringLiteral(".");
                dest += suffix;
            }
            if (!checkExisting(dest))
                continue;
            res = QFile::copy(path, dest);
            if (res)
                QFile::setPermissions(dest, QFileDevice::WriteOwner|QFile::permissions(dest));
        } else {
            QPixmap pixmap, original;
            if (!BitmapFactory().findPixmapInCache(name.toUtf8().constData(), pixmap, &original)) {
                FC_ERR("Failed to read icon cache " << name.toUtf8().constData());
                continue;
            }
            if (!original.isNull())
                pixmap = original;
            QString dest = dirname + QLatin1Char('/') + basename + QStringLiteral(".png");
            if (!checkExisting(dest))
                continue;
            res = pixmap.save(dest, "PNG");
        }
        if (res)
            ++count;
        else
            FC_ERR("Failed to extract icon " << name.toUtf8().constData());
    }
    if (count == (int)items.size())
        QMessageBox::information(this, QObject::tr("Export"),
                QStringLiteral("%1 %2").arg(QObject::tr("Exported to"), dirname));
    else if (count)
        QMessageBox::warning(this, QObject::tr("Export"),
                QStringLiteral("%1 %2").arg(QObject::tr("Some icons failed to exported to"), dirname));
    else
        QMessageBox::critical(this, QObject::tr("Export"), QObject::tr("Export failed"));
}

void DlgIconBrowser::on_btnRefresh_clicked()
{
    {
        QSignalBlocker blocker(ui->IconSets);
        ui->IconSets->clear();
        DlgSettingsGeneral::populateStylesheets("IconSet", "iconset",
                ui->IconSets, "None", QStringList(QStringLiteral("*.txt")));
    }

    auto iconNames = BitmapFactory().pixmapNames();

    std::set<QString> checked;
    for (auto it = QTreeWidgetItemIterator(ui->treeWidget, QTreeWidgetItemIterator::Checked); *it; ++it)
        checked.insert((*it)->text(0));

    QString current;
    auto currentItem = ui->treeWidget->currentItem();
    if (currentItem)
        current = currentItem->text(0);

    QSignalBlocker blocker(ui->treeWidget);
    ui->treeWidget->clear();
    currentItem = nullptr;
    for (auto &name : iconNames) {
        auto item = new QTreeWidgetItem(ui->treeWidget);
        item->setFlags(Qt::ItemIsSelectable|Qt::ItemIsUserCheckable|Qt::ItemIsEnabled);
        if (name == current)
            currentItem = item;
        item->setText(0, name);
        QByteArray n = name.toUtf8();
        QPixmap icon, original;
        if (!BitmapFactory().findPixmapInCache(n, icon, &original))
            continue;
        item->setIcon(0, original.isNull() ? icon : original);
        if (!original.isNull()) {
            item->setIcon(1, icon);
            item->setText(1, QStringLiteral("*"));
        }
        std::ostringstream ss;
        QString context;
        QTextStream ts(&context);
        ts << name;
        auto contexts = BitmapFactory().getContext(n);
        for (auto ctx : contexts) {
            QString c = QString::fromUtf8(ctx.c_str());
            if (c.endsWith(QStringLiteral("-|"))) {
                c.resize(c.size()-2);
                item->setText(2, c);
            }
            if (c != name)
                ts << QStringLiteral("\n") << c;
        }
        item->setToolTip(0, context);
        item->setCheckState(0, checked.count(name)?Qt::Checked:Qt::Unchecked);
    }

    if (currentItem) {
        ui->treeWidget->setCurrentItem(currentItem);
        ui->treeWidget->scrollToItem(currentItem);
    }

    onTimer();
    if (ui->editFilter->text().size())
        onTimerFilter();
}

void DlgIconBrowser::on_treeWidget_itemChanged(QTreeWidgetItem *item, int column)
{
    if (column != 0)
        return;
    QSignalBlocker blocker(ui->treeWidget);
    auto state = item->checkState(0);
    if (item->isSelected()) {
        for (auto item : ui->treeWidget->selectedItems())
            item->setCheckState(0,state);
    }
    timer.start(500);
}

void DlgIconBrowser::on_editFilter_textEdited(const QString &)
{
    timerFilter.start(500);
}

void DlgIconBrowser::on_editPrefix_textEdited(const QString &)
{
    timer.start(500);
}

QString DlgIconBrowser::getPrefix()
{
    QString prefix = ui->editPrefix->text();
    if (prefix.isEmpty())
        prefix = QStringLiteral("MyIcons/");
    else if (!prefix.endsWith(QLatin1Char('/')))
        prefix += QStringLiteral("/");
    return prefix;
}

void DlgIconBrowser::onTimer()
{
    QString text;
    QTextStream ts(&text);

    QString prefix = getPrefix();

    bool first = true;
    for (auto it = QTreeWidgetItemIterator(ui->treeWidget, QTreeWidgetItemIterator::Checked); *it; ++it) {
        auto item = *it;
        QString name = item->text(0);
        QString path = item->text(2);
        if (path.size()) {
            QFileInfo info(path);
            path = info.fileName();
        } else
            path = name;
        if (first)
            first = false;
        else
            ts << "\n";
        ts << name << ", iconset:" << prefix << path;
    }
    ui->editOutput->setPlainText(text);
}

void DlgIconBrowser::onTimerFilter()
{
    QString filter = ui->editFilter->text();
    for (int i=0; i<ui->treeWidget->topLevelItemCount(); ++i) {
        auto item = ui->treeWidget->topLevelItem(i);
        item->setHidden(filter.size() && !item->toolTip(0).contains(filter, Qt::CaseInsensitive));
    }
}

void DlgIconBrowser::on_btnSave_clicked()
{
    QString text = ui->editOutput->toPlainText();
    if (text.isEmpty())
        return;
    QString prefix = QStringLiteral("Gui/IconSets/");
    QDir dir(QString::fromUtf8(App::Application::getUserAppDataDir().c_str()));
    dir.mkpath(prefix);
    dir.cd(prefix);
    QString name = dir.filePath(QDir(getPrefix()).dirName() + QStringLiteral(".txt"));
    auto filename = FileDialog::getSaveFileName(this, QObject::tr("Save icon stylesheet"),
            name, QStringLiteral("%1 (*.txt)").arg(QObject::tr("Text file")));
    if (filename.size()) {
        QFile file(filename);
        if (file.open(QIODevice::WriteOnly|QIODevice::Text)) {
            file.write(text.toUtf8());
            file.close();
            on_btnRefresh_clicked();
        } else
            QMessageBox::critical(this, QObject::tr("Failed"),
                    QObject::tr("Failed to save icon stylesheet"));
    }
}

void DlgIconBrowser::on_btnBrowse_clicked()
{
    QString text = ui->editPrefix->text();
    QString prefix = QStringLiteral("Gui/IconSets/");
    QDir dir(QString::fromUtf8(App::Application::getUserAppDataDir().c_str()));
    dir.mkpath(prefix);
    dir.cd(prefix);
    if (!text.isEmpty()) {
        dir.mkpath(text);
        dir.cd(text);
    }
    QDesktopServices::openUrl(QUrl::fromLocalFile(dir.absolutePath()));
}

#include "moc_DlgIconBrowser.cpp"
