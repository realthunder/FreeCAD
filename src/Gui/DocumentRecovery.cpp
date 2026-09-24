/***************************************************************************
 *   Copyright (c) 2015 Werner Mayer <wmayer[at]users.sourceforge.net>     *
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

// Implement FileWriter which puts files into a directory
// write a property to file only when it has been modified
// implement xml meta file

#include "PreCompiled.h"

#ifndef _PreComp_
# include <boost/interprocess/sync/file_lock.hpp>
# include <QApplication>
# include <QCloseEvent>
# include <QDateTime>
# include <QDebug>
# include <QDir>
# include <QDomDocument>
# include <QElapsedTimer>
# include <QEventLoop>
# include <QFileInfo>
# include <QHash>
# include <QHeaderView>
# include <QList>
# include <QLocale>
# include <QMap>
# include <QMenu>
# include <QMessageBox>
# include <QProgressDialog>
# include <QStyle>
# include <QSet>
# include <QTextStream>
# include <QTimer>
# include <QTreeWidgetItem>
# include <QVector>
# include <algorithm>
# include <memory>
# include <sstream>
# include <thread>
#endif

#ifdef FC_OS_WIN32
# include <windows.h>
#else
# include <cerrno>
# include <signal.h>
#endif

#include <App/Application.h>
#include <App/Document.h>
#include <Base/Console.h>
#include <Base/Exception.h>
#include <Gui/Application.h>
#include <Gui/Command.h>
#include <Gui/DlgCheckableMessageBox.h>
#include <Gui/Document.h>
#include <Gui/MainWindow.h>

#include "DocumentRecovery.h"
#include "ui_DocumentRecovery.h"
#include "WaitCursor.h"


FC_LOG_LEVEL_INIT("Gui", true, true)

using namespace Gui;
using namespace Gui::Dialog;
namespace sp = std::placeholders;

// taken from the script doctools.py
std::string DocumentRecovery::doctools =
"import os,sys,string\n"
"import xml.sax\n"
"import xml.sax.handler\n"
"import xml.sax.xmlreader\n"
"import zipfile\n"
"\n"
"# SAX handler to parse the Document.xml\n"
"class DocumentHandler(xml.sax.handler.ContentHandler):\n"
"	def __init__(self, dirname):\n"
"		self.files = []\n"
"		self.dirname = dirname\n"
"\n"
"	def startElement(self, name, attributes):\n"
"		if name == 'XLink':\n"
"			return\n"
"		item=attributes.get(\"file\")\n"
"		if item:\n"
"			self.files.append(os.path.join(self.dirname,str(item)))\n"
"\n"
"	def characters(self, data):\n"
"		return\n"
"\n"
"	def endElement(self, name):\n"
"		return\n"
"\n"
"def extractDocument(filename, outpath):\n"
"	zfile=zipfile.ZipFile(filename)\n"
"	files=zfile.namelist()\n"
"\n"
"	for i in files:\n"
"		data=zfile.read(i)\n"
"		dirs=i.split(\"/\")\n"
"		if len(dirs) > 1:\n"
"			dirs.pop()\n"
"			curpath=outpath\n"
"			for j in dirs:\n"
"				curpath=curpath+\"/\"+j\n"
"				os.mkdir(curpath)\n"
"		output=open(outpath+\"/\"+i,\'wb\')\n"
"		output.write(data)\n"
"		output.close()\n"
"\n"
"def createDocument(filename, outpath):\n"
"	files=getFilesList(filename)\n"
"	dirname=os.path.dirname(filename)\n"
"	guixml=os.path.join(dirname,\"GuiDocument.xml\")\n"
"	if os.path.exists(guixml):\n"
"		files.extend(getFilesList(guixml))\n"
"	compress=zipfile.ZipFile(outpath,\'w\',zipfile.ZIP_DEFLATED)\n"
"	for i in files:\n"
"		dirs=os.path.split(i)\n"
"		#print i, dirs[-1]\n"
"		compress.write(i,dirs[-1],zipfile.ZIP_DEFLATED)\n"
"	compress.close()\n"
"\n"
"def getFilesList(filename):\n"
"	dirname=os.path.dirname(filename)\n"
"	handler=DocumentHandler(dirname)\n"
"	parser=xml.sax.make_parser()\n"
"	parser.setContentHandler(handler)\n"
"	parser.parse(filename)\n"
"\n"
"	files=[]\n"
"	files.append(filename)\n"
"	files.extend(iter(handler.files))\n"
"	return files\n"
;


namespace Gui { namespace Dialog {
class DocumentRecoveryPrivate
{
public:
    using XmlConfig = QMap<QString, QString>;

    enum Status {
        Unknown = 0, /*!< The file is not available */
        Created = 1, /*!< The file was created but not processed so far*/
        Overage = 2, /*!< The recovery file is older than the actual project file */
        Success = 3, /*!< The file could be recovered */
        Failure = 4, /*!< The file could not be recovered */
    };
    /// Why an entry is not checked by default: a short reason for the status
    /// column and the confirmation, and the full text for the tooltip
    struct Warning {
        QString reason;
        QString text;
    };
    struct Info {
        QString directory;
        QString projectFile;
        QString xmlFile;
        QString label;
        QString fileName;
        QString tooltip;
        QDateTime modified;
        bool noLock = false;
        QList<Warning> warnings;
        Status status = Unknown;
    };
    Ui_DocumentRecovery ui;
    bool recovered;
    QList<Info> recoveryInfo;

    Info getRecoveryInfo(const QFileInfo&) const;
    void writeRecoveryInfo(const Info&) const;
    XmlConfig readXmlFile(const QString& fn) const;
    static QList<Warning> warnings(const Info&);

    /// The recovery info an item of the tree stands for
    Info& info(const QTreeWidgetItem* item)
    {
        return recoveryInfo[item->data(0, Qt::UserRole).toInt()];
    }
};

}
}

namespace {

constexpr int ModifiedColumn = 2;

/// Sorts the time column by time rather than by its text
class RecoveryItem : public QTreeWidgetItem
{
public:
    using QTreeWidgetItem::QTreeWidgetItem;

    bool operator<(const QTreeWidgetItem& other) const override
    {
        int column = treeWidget() ? treeWidget()->sortColumn() : 0;
        if (column == ModifiedColumn) {
            return data(ModifiedColumn, Qt::UserRole).toDateTime()
                < other.data(ModifiedColumn, Qt::UserRole).toDateTime();
        }
        return QTreeWidgetItem::operator<(other);
    }
};

}

DocumentRecovery::DocumentRecovery(const QList<QFileInfo>& dirs,
                                   const QList<QFileInfo>& lockless,
                                   QWidget* parent)
  : QDialog(parent), d_ptr(new DocumentRecoveryPrivate())
{
    d_ptr->ui.setupUi(this);
    connect(d_ptr->ui.buttonCleanup, &QPushButton::clicked,
            this, &DocumentRecovery::onButtonCleanupClicked);
    d_ptr->ui.buttonBox->button(QDialogButtonBox::Ok)->setText(tr("Start Recovery"));
    QTreeWidget* tree = d_ptr->ui.treeWidget;
    // The name takes what the status and the time leave
    tree->header()->setStretchLastSection(false);
    tree->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
    tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);

    d_ptr->recovered = false;

    // Recovery data older than the saved file, and data no lock file claims,
    // are listed too, unchecked and marked, so that they can be looked at and
    // cleaned up rather than kept on disk forever
    QList<DocumentRecoveryPrivate::Info> infos;
    auto collect = [&](const QList<QFileInfo>& list, bool noLock) {
        for (const QFileInfo& dir : list) {
            DocumentRecoveryPrivate::Info info = d_ptr->getRecoveryInfo(dir);
            if (info.status == DocumentRecoveryPrivate::Created
                || info.status == DocumentRecoveryPrivate::Overage) {
                info.noLock = noLock;
                info.warnings = DocumentRecoveryPrivate::warnings(info);
                infos << info;
            }
        }
    };
    collect(dirs, false);
    collect(lockless, true);

    // The entries checked by default first, the marked ones after; newest
    // first within each
    std::stable_sort(infos.begin(), infos.end(), [](const auto& a, const auto& b) {
        if (a.warnings.isEmpty() != b.warnings.isEmpty())
            return a.warnings.isEmpty();
        return a.modified > b.modified;
    });
    d_ptr->recoveryInfo = infos;

    QLocale locale;
    QIcon warningIcon = style()->standardIcon(QStyle::SP_MessageBoxWarning);
    for (int i = 0; i < infos.size(); ++i) {
        const auto& info = infos[i];
        auto item = new RecoveryItem(tree);
        item->setData(0, Qt::UserRole, i);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setText(0, info.label);
        item->setToolTip(0, info.tooltip);
        item->setText(1, tr("Not yet recovered"));
        item->setToolTip(1, info.projectFile);
        item->setText(ModifiedColumn, locale.toString(info.modified, QLocale::ShortFormat));
        item->setData(ModifiedColumn, Qt::UserRole, info.modified);
        if (!info.warnings.isEmpty()) {
            QStringList texts;
            for (const auto& warning : info.warnings)
                texts << warning.text;
            QString tooltip = texts.join(QStringLiteral("\n\n"));
            item->setIcon(0, warningIcon);
            item->setText(1, info.warnings.front().reason);
            for (int column = 0; column < tree->columnCount(); ++column)
                item->setToolTip(column, tooltip);
        }
        item->setCheckState(0, info.warnings.isEmpty() ? Qt::Checked : Qt::Unchecked);
    }

    // Sortable by a click on a header; until then the order above stands
    tree->header()->setSortIndicator(-1, Qt::AscendingOrder);
    tree->setSortingEnabled(true);

    connect(tree, &QTreeWidget::itemChanged, this, &DocumentRecovery::updateButtons);
    updateButtons();

    this->adjustSize();
    // Room for a document name beside the status and the time
    resize(std::max(width(), fontMetrics().averageCharWidth() * 90), height());
}

QList<DocumentRecoveryPrivate::Warning> DocumentRecoveryPrivate::warnings(const Info& info)
{
    QList<Warning> list;
    if (info.status == Overage) {
        QString text = DocumentRecovery::tr("The recovery data is older than the saved file\n%1\n\n"
                                            "Recovering it brings back an older state of the "
                                            "document than the one saved.").arg(info.fileName);
        QFileInfo saved(info.fileName);
        if (!info.fileName.isEmpty() && saved.exists()) {
            QLocale locale;
            text += QStringLiteral("\n\n")
                + DocumentRecovery::tr("Recovery data: %1\nSaved file: %2")
                      .arg(locale.toString(info.modified, QLocale::ShortFormat),
                           locale.toString(saved.lastModified(), QLocale::ShortFormat));
        }
        list.append({DocumentRecovery::tr("Older than the saved file"), text});
    }
    if (info.noLock) {
        list.append({DocumentRecovery::tr("No lock file"),
                     DocumentRecovery::tr("No FreeCAD session claims this recovery data: its "
                                          "directory has no lock file. It may be left from a "
                                          "session whose lock file an earlier cleanup removed, "
                                          "or could not be created.\n\n"
                                          "Check that it holds the state you expect before "
                                          "recovering it.")});
    }
    return list;
}

QList<QTreeWidgetItem*> DocumentRecovery::checkedItems() const
{
    QList<QTreeWidgetItem*> items;
    for (int i = 0; i < d_ptr->ui.treeWidget->topLevelItemCount(); ++i) {
        QTreeWidgetItem* item = d_ptr->ui.treeWidget->topLevelItem(i);
        if ((item->flags() & Qt::ItemIsUserCheckable) && item->checkState(0) == Qt::Checked)
            items << item;
    }
    return items;
}

void DocumentRecovery::updateButtons()
{
    bool checked = !checkedItems().isEmpty();
    d_ptr->ui.buttonCleanup->setEnabled(checked);
    d_ptr->ui.buttonBox->button(QDialogButtonBox::Ok)->setEnabled(d_ptr->recovered || checked);
}

DocumentRecovery::~DocumentRecovery() = default;

bool DocumentRecovery::foundDocuments() const
{
    Q_D(const DocumentRecovery);
    return (!d->recoveryInfo.isEmpty());
}

QString DocumentRecovery::createProjectFile(const QString& documentXml)
{
    QString source = documentXml;
    QFileInfo fi(source);
    QString dest = fi.dir().absoluteFilePath(QStringLiteral("fc_recovery_file.fcstd"));

    std::stringstream str;
    str << doctools << "\n";
    str << "createDocument(\"" << (const char*)source.toUtf8()
        << "\", \"" << (const char*)dest.toUtf8() << "\")";
    Gui::Command::runCommand(Gui::Command::App, str.str().c_str());

    return dest;
}

void DocumentRecovery::closeEvent(QCloseEvent* e)
{
    // Do not disable the X button in the title bar
    // #0004281: Close Document Recovery
    e->accept();
}

void DocumentRecovery::accept()
{
    Q_D(DocumentRecovery);

    if (!d->recovered) {
        const QList<QTreeWidgetItem*> checked = checkedItems();
        if (checked.isEmpty())
            return;

        QStringList marked;
        for (QTreeWidgetItem* item : checked) {
            const auto& info = d->info(item);
            QStringList reasons;
            for (const auto& warning : info.warnings)
                reasons << warning.reason;
            if (!reasons.isEmpty())
                marked << QStringLiteral("%1: %2").arg(info.label, reasons.join(QStringLiteral(", ")));
        }
        if (!marked.isEmpty()) {
            QMessageBox msgBox(this);
            msgBox.setIcon(QMessageBox::Warning);
            msgBox.setWindowTitle(tr("Recover marked documents"));
            msgBox.setText(tr("Selected documents marked with a warning:")
                           + QStringLiteral("\n\n") + marked.join(QStringLiteral("\n")));
            msgBox.setInformativeText(tr("Recovering them may bring back a state other than the "
                                         "one you expect. Recover them anyway?"));
            msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
            msgBox.setDefaultButton(QMessageBox::No);
            if (msgBox.exec() != QMessageBox::Yes)
                return;
        }

        WaitCursor wc;
        std::vector<QTreeWidgetItem*> pending;
        std::vector<std::string> filenames, paths, labels, errs;
        for (QTreeWidgetItem* item : checked) {
            auto& info = d->info(item);
            QString errorInfo;

            try {
                QString file = info.projectFile;
                QFileInfo fi(file);
                if (fi.fileName() == QStringLiteral("Document.xml"))
                    file = createProjectFile(info.projectFile);

                paths.emplace_back(file.toUtf8().constData());
                filenames.emplace_back(info.fileName.toUtf8().constData());
                labels.emplace_back(info.label.toUtf8().constData());
                pending.push_back(item);
            }
            catch (const std::exception& e) {
                errorInfo = QString::fromUtf8(e.what());
            }
            catch (const Base::Exception& e) {
                errorInfo = QString::fromUtf8(e.what());
            }
            catch (...) {
                errorInfo = tr("Unknown problem occurred");
            }

            if (!errorInfo.isEmpty()) {
                info.status = DocumentRecoveryPrivate::Failure;
                item->setText(1, tr("Failed to recover"));
                item->setToolTip(1, errorInfo);
                item->setForeground(1, QColor(170,0,0));
                // Do not mark failure so that user can retry on next run
                // d->writeRecoveryInfo(info);
            }
        }

        auto docs = App::GetApplication().openDocuments(filenames,&paths,&labels,&errs);

        // The directories recovered from, removed after the loop in one go
        QFileInfoList recoveredDirs;
        for (size_t i = 0; i < docs.size(); ++i) {
            QTreeWidgetItem* item = pending[i];
            auto& info = d->info(item);
            if (!docs[i] || !errs[i].empty()) {
                if (docs[i])
                    App::GetApplication().closeDocument(docs[i]->getName());
                // info.status = DocumentRecoveryPrivate::Failure;
                if (item) {
                    item->setText(1, tr("Failed to recover"));
                    item->setToolTip(1, QString::fromUtf8(errs[i].c_str()));
                    item->setForeground(1, QColor(170,0,0));
                }
                // Do not mark failure so that user can retry on next run
                // d->writeRecoveryInfo(info);
            }
            else {
                auto gdoc = Application::Instance->getDocument(docs[i]);
                if (gdoc)
                    gdoc->setModified(true);

                info.status = DocumentRecoveryPrivate::Success;
                item->setText(1, tr("Successfully recovered"));
                item->setForeground(1, QColor(0,170,0));
                // Nothing is left to recover or clean up for it
                item->setFlags(item->flags() & ~Qt::ItemIsUserCheckable);
                item->setData(0, Qt::CheckStateRole, QVariant());

                // Entries still parked for a deferred load keep the recovery
                // file open, and Windows will not move an open file
                docs[i]->flushDeferredFiles();

                QDir transDir(QString::fromUtf8(docs[i]->TransientDir.getValue()));

                QFileInfo xfi(info.xmlFile);
                QFileInfo fi(info.projectFile);
                bool res = false;

                if (fi.fileName() == QStringLiteral("fc_recovery_file.fcstd")) {
                    transDir.remove(fi.fileName());
                    res = transDir.rename(fi.absoluteFilePath(),fi.fileName());
                }
                else {
                    transDir.rmdir(fi.dir().dirName());
                    res = transDir.rename(fi.absolutePath(),fi.dir().dirName());
                }

                if (res) {
                    transDir.remove(xfi.fileName());
                    res = transDir.rename(xfi.absoluteFilePath(),xfi.fileName());
                }

                if (!res) {
                    FC_WARN("Failed to move recovery file of document '"
                            << docs[i]->Label.getValue() << "'");
                }
                else {
                    recoveredDirs << QFileInfo(xfi.absolutePath());
                }

                // DO NOT write success into recovery info, in case the program
                // crash again before the user save the just recovered file.
            }
        }

        if (!recoveredDirs.isEmpty())
            DocumentRecoveryCleaner().removeWithProgress(recoveredDirs, false, this);

        d->ui.buttonBox->button(QDialogButtonBox::Ok)->setText(tr("Finish"));
        d->ui.buttonBox->button(QDialogButtonBox::Cancel)->setEnabled(false);
        d->recovered = true;
        updateButtons();
    }
    else {
        QDialog::accept();
    }
}

void DocumentRecoveryPrivate::writeRecoveryInfo(const DocumentRecoveryPrivate::Info& info) const
{
    // Write recovery meta file
    QFile file(info.xmlFile);
    if (file.open(QFile::WriteOnly)) {
        QTextStream str(&file);
#if QT_VERSION < QT_VERSION_CHECK(6,0,0)
        str.setCodec("UTF-8");
#endif
        str << "<?xml version='1.0' encoding='utf-8'?>\n"
            << "<AutoRecovery SchemaVersion=\"1\">\n";
        switch (info.status) {
        case Created:
            str << "  <Status>Created</Status>\n";
            break;
        case Overage:
            str << "  <Status>Deprecated</Status>\n";
            break;
        case Success:
            str << "  <Status>Success</Status>\n";
            break;
        case Failure:
            str << "  <Status>Failure</Status>\n";
            break;
        default:
            str << "  <Status>Unknown</Status>\n";
            break;
        }
        str << "  <Label>" << info.label << "</Label>\n";
        str << "  <FileName>" << info.fileName << "</FileName>\n";
        str << "</AutoRecovery>\n";
        file.close();
    }
}

DocumentRecoveryPrivate::Info DocumentRecoveryPrivate::getRecoveryInfo(const QFileInfo& fi) const
{
    DocumentRecoveryPrivate::Info info;
    info.status = DocumentRecoveryPrivate::Unknown;
    info.label = qApp->translate("StdCmdNew","Unnamed");

    info.directory = fi.absoluteFilePath();
    QString file;
    QDir doc_dir(fi.absoluteFilePath());
    QDir rec_dir(doc_dir.absoluteFilePath(QStringLiteral("fc_recovery_files")));

    // compressed recovery file
    if (doc_dir.exists(QStringLiteral("fc_recovery_file.fcstd"))) {
        file = doc_dir.absoluteFilePath(QStringLiteral("fc_recovery_file.fcstd"));
    }
    // separate files for recovery
    else if (rec_dir.exists(QStringLiteral("Document.xml"))) {
        file = rec_dir.absoluteFilePath(QStringLiteral("Document.xml"));
    }
    else {
        info.status = DocumentRecoveryPrivate::Unknown;
        return info;
    }

    info.status = DocumentRecoveryPrivate::Created;
    info.projectFile = file;
    info.modified = QFileInfo(file).lastModified();
    info.tooltip = fi.fileName();

    // when the Xml meta exists get some relevant information
    info.xmlFile = doc_dir.absoluteFilePath(QStringLiteral("fc_recovery_file.xml"));
    if (doc_dir.exists(QStringLiteral("fc_recovery_file.xml"))) {
        XmlConfig cfg = readXmlFile(info.xmlFile);

        if (cfg.contains(QStringLiteral("Label"))) {
            info.label = cfg[QStringLiteral("Label")];
        }

        if (cfg.contains(QStringLiteral("FileName"))) {
            info.fileName = cfg[QStringLiteral("FileName")];
        }

        if (cfg.contains(QStringLiteral("Status"))) {
            QString status = cfg[QStringLiteral("Status")];
            if (status == QStringLiteral("Deprecated"))
                info.status = DocumentRecoveryPrivate::Overage;
            else if (status == QStringLiteral("Success"))
                info.status = DocumentRecoveryPrivate::Success;
            else if (status == QStringLiteral("Failure"))
                info.status = DocumentRecoveryPrivate::Failure;
        }

        if (info.status == DocumentRecoveryPrivate::Created) {
            // compare the modification dates
            QFileInfo fileInfo(info.fileName);
            if (!info.fileName.isEmpty() && fileInfo.exists()) {
                QDateTime dateRecv = QFileInfo(file).lastModified();
                QDateTime dateProj = fileInfo.lastModified();
                if (dateRecv < dateProj) {
                    info.status = DocumentRecoveryPrivate::Overage;
                    writeRecoveryInfo(info);
                    qWarning() << "Ignore recovery file " << file.toUtf8()
                        << " because it is older than the project file" << info.fileName.toUtf8() << "\n";
                }
            }
        }
    }

    return info;
}

DocumentRecoveryPrivate::XmlConfig DocumentRecoveryPrivate::readXmlFile(const QString& fn) const
{
    DocumentRecoveryPrivate::XmlConfig cfg;
    QDomDocument domDocument;
    QFile file(fn);
    if (!file.open(QFile::ReadOnly))
        return cfg;

    QString errorStr;
    int errorLine;
    int errorColumn;

    if (!domDocument.setContent(&file, true, &errorStr, &errorLine,
                                &errorColumn)) {
        return cfg;
    }

    QDomElement root = domDocument.documentElement();
    if (root.tagName() != QStringLiteral("AutoRecovery")) {
        return cfg;
    }

    file.close();

    QVector<QString> filter;
    filter << QStringLiteral("Label");
    filter << QStringLiteral("FileName");
    filter << QStringLiteral("Status");

    QDomElement child;
    if (!root.isNull()) {
        child = root.firstChildElement();
        while (!child.isNull()) {
            QString name = child.localName();
            QString value = child.text();
            if (std::find(filter.begin(), filter.end(), name) != filter.end())
                cfg[name] = value;
            child = child.nextSiblingElement();
        }
    }

    return cfg;
}

void DocumentRecovery::contextMenuEvent(QContextMenuEvent* ev)
{
    QList<QTreeWidgetItem*> items = d_ptr->ui.treeWidget->selectedItems();
    if (!items.isEmpty()) {
        QMenu menu;
        menu.addAction(tr("Delete"), this, &DocumentRecovery::onDeleteSection);
        menu.exec(ev->globalPos());
    }
}

void DocumentRecovery::onDeleteSection()
{
    removeItems(d_ptr->ui.treeWidget->selectedItems());
}

void DocumentRecovery::onButtonCleanupClicked()
{
    removeItems(checkedItems());
}

void DocumentRecovery::removeItems(const QList<QTreeWidgetItem*>& items)
{
    if (items.isEmpty())
        return;

    QMessageBox msgBox(this);
    msgBox.setIcon(QMessageBox::Warning);
    msgBox.setWindowTitle(tr("Cleanup"));
    msgBox.setText(tr("Are you sure you want to delete the selected transient directories?"));
    msgBox.setInformativeText(tr("When deleting the selected transient directory you won't be able to recover any files afterwards."));
    msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
    msgBox.setDefaultButton(QMessageBox::No);
    int ret = msgBox.exec();
    if (ret == QMessageBox::No)
        return;

    QFileInfoList dirs;
    for (QTreeWidgetItem* item : items)
        dirs << QFileInfo(d_ptr->info(item).directory);
    QFileInfoList survivors = DocumentRecoveryCleaner().removeWithProgress(dirs, false, this);

    for (QTreeWidgetItem* item : items) {
        if (!survivors.contains(QFileInfo(d_ptr->info(item).directory)))
            delete item;
    }

    // Drop the lock files of dead instances whose directories are all gone
    // now. Collect first: the scan holds each lock while it reports it.
    QList<StaleDirGroup> groups;
    DocumentRecoveryHandler handler;
    handler.checkForPreviousCrashes(
        [&](QDir& tmp, const QList<QFileInfo>& lockDirs, const QString& lockFile) {
            groups.append(StaleDirGroup(tmp.absoluteFilePath(lockFile), lockDirs));
        });
    for (const auto& group : std::as_const(groups))
        DocumentRecoveryHandler::removeStaleLock(group.first, group.second);

    if (!survivors.isEmpty()) {
        QMessageBox::warning(this, tr("Delete"),
                             tr("Not all transient directories could be deleted (%1 left). "
                                "They are kept, and looked at again at the next start.")
                                 .arg(survivors.size()));
    }

    if (d_ptr->ui.treeWidget->topLevelItemCount() == 0) {
        DlgCheckableMessageBox::showMessage(tr("Delete"), tr("Transient directories deleted."));
        reject();
        return;
    }
    updateButtons();
}

// ----------------------------------------------------------------------------

bool DocumentRecoveryFinder::checkForPreviousCrashes()
{
    //NOLINTBEGIN
    DocumentRecoveryHandler handler;
    handler.checkForPreviousCrashes(std::bind(&DocumentRecoveryFinder::checkDocumentDirs, this, sp::_1, sp::_2, sp::_3));
    //NOLINTEND

    // Directories no lock file leads to: offer what can be recovered, marked
    // as unclaimed, and delete the rest
    QFileInfoList orphans;
    for (const QFileInfo& dir : handler.findOrphansWithoutLock()) {
        if (isRecoverable(dir))
            locklessDocFiles << dir;
        else
            orphans << dir;
    }
    if (!orphans.isEmpty())
        staleDirs.append(StaleDirGroup(QString(), orphans));

    // In the background: a dead session can leave tens of thousands of blob
    // files, and where the filesystem is monitored each delete costs ~9 ms.
    if (!staleDirs.isEmpty())
        DocumentRecoveryCleaner::removeInBackground(staleDirs);

    return showRecoveryDialogIfNeeded();
}

bool DocumentRecoveryFinder::isRecoverable(const QFileInfo& dir)
{
    // What DocumentRecoveryPrivate::getRecoveryInfo() can open
    QDir docDir(dir.absoluteFilePath());
    return docDir.exists(QStringLiteral("fc_recovery_file.fcstd"))
        || docDir.exists(QStringLiteral("fc_recovery_files/Document.xml"));
}

void DocumentRecoveryFinder::checkDocumentDirs(QDir& tmp, const QList<QFileInfo>& dirs, const QString& fn)
{
    if (dirs.isEmpty()) {
        // delete the lock file immediately if no transient directories are related
        tmp.remove(fn);
        return;
    }

    QFileInfoList stale;
    for (const QFileInfo& dir : dirs) {
        if (isRecoverable(dir))
            restoreDocFiles << dir;
        else
            stale << dir;
    }

    // The lock file goes with the directories once all of them are gone, and
    // stays while a recoverable one is left
    if (!stale.isEmpty()) {
        QString lockFile = stale.size() == dirs.size() ? tmp.absoluteFilePath(fn) : QString();
        staleDirs.append(StaleDirGroup(lockFile, stale));
    }
}

bool DocumentRecoveryFinder::showRecoveryDialogIfNeeded()
{
    bool foundRecoveryFiles = false;
    if (!restoreDocFiles.isEmpty() || !locklessDocFiles.isEmpty()) {
        Gui::Dialog::DocumentRecovery dlg(restoreDocFiles, locklessDocFiles, Gui::getMainWindow());
        if (dlg.foundDocuments()) {
            foundRecoveryFiles = true;
            dlg.exec();
        }
    }

    return foundRecoveryFiles;
}

// ----------------------------------------------------------------------------

namespace {

/// Start time of the running process \a pid in msecs since the epoch; 0 if it
/// runs but its start time is not known, -1 if no such process runs.
qint64 processStartTime(qint64 pid)
{
#ifdef FC_OS_WIN32
    HANDLE handle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(pid));
    if (!handle) {
        // Anything but "no such process" (access denied, mostly) means it runs
        return GetLastError() == ERROR_INVALID_PARAMETER ? -1 : 0;
    }
    qint64 start = 0;
    DWORD code = 0;
    FILETIME created, exited, kernel, user;
    if (GetExitCodeProcess(handle, &code) && code != STILL_ACTIVE) {
        start = -1;   // exited, and someone still holds a handle to it
    }
    else if (GetProcessTimes(handle, &created, &exited, &kernel, &user)) {
        ULARGE_INTEGER ticks;
        ticks.LowPart = created.dwLowDateTime;
        ticks.HighPart = created.dwHighDateTime;
        // 100 ns ticks since 1601-01-01 UTC
        start = static_cast<qint64>(ticks.QuadPart / 10000) - Q_INT64_C(11644473600000);
    }
    CloseHandle(handle);
    return start;
#else
    return (kill(static_cast<pid_t>(pid), 0) == 0 || errno == EPERM) ? 0 : -1;
#endif
}

/// Number of entries under \a path, the way DocumentRecoveryCleaner counts
/// what it removes. Listing costs one call per directory, not one per file.
int countEntries(const QString& path, const std::atomic<bool>& cancel)
{
    int count = 0;
    const QFileInfoList entries = QDir(path).entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot
                                                           | QDir::Hidden | QDir::System);
    for (const QFileInfo& fi : entries) {
        if (cancel)
            break;
        ++count;
        bool isLink = fi.isSymLink();
#if QT_VERSION >= QT_VERSION_CHECK(6, 4, 0)
        isLink = isLink || fi.isJunction();
#endif
        if (fi.isDir() && !isLink)
            count += countEntries(fi.absoluteFilePath(), cancel);
    }
    return count;
}

/// Removes the startup scan's stale directories on a thread of its own, and
/// stops when the application quits
class BackgroundRemoval
{
public:
    static BackgroundRemoval& instance()
    {
        static BackgroundRemoval inst;
        return inst;
    }

    void start(const QList<StaleDirGroup>& groups)
    {
        if (thread.joinable()) {
            if (!finished)
                return;
            thread.join();
        }
        if (!connected) {
            connected = true;
            QObject::connect(qApp, &QCoreApplication::aboutToQuit, qApp, [this]() { stop(); });
        }
        cancel = false;
        finished = false;
        thread = std::thread([this, groups]() { run(groups); });
    }

    void stop()
    {
        cancel = true;
        if (thread.joinable())
            thread.join();
    }

    ~BackgroundRemoval()
    {
        // Static destruction may come after the thread was killed: never join here
        if (thread.joinable()) {
            cancel = true;
            thread.detach();
        }
    }

private:
    void run(const QList<StaleDirGroup>& groups)
    {
        std::atomic<int> removed {0};
        DocumentRecoveryCleaner cleaner;
        cleaner.setProgress(&removed, &cancel);
        int dirs = 0;
        int left = 0;
        for (const auto& group : groups) {
            for (const QFileInfo& dir : group.second) {
                if (cancel) {
                    finished = true;
                    return;
                }
                ++dirs;
                if (!cleaner.removeDirectory(dir))
                    ++left;
            }
            DocumentRecoveryHandler::removeStaleLock(group.first, group.second);
        }
        int count = removed;
        QMetaObject::invokeMethod(qApp, [dirs, left, count]() {
            Base::Console().Log("Removed %d stale transient directories (%d entries), %d left\n",
                                dirs - left, count, left);
        }, Qt::QueuedConnection);
        finished = true;
    }

private:
    std::thread thread;
    std::atomic<bool> cancel {false};
    std::atomic<bool> finished {false};
    bool connected = false;
};

}

void DocumentRecoveryHandler::checkForPreviousCrashes(const std::function<void(QDir&, const QList<QFileInfo>&, const QString&)> & callableFunc) const
{
    QDir tmp = QString::fromUtf8(App::Application::getUserCachePath().c_str());
    tmp.setNameFilters(QStringList() << QStringLiteral("*.lock"));
    tmp.setFilter(QDir::Files);

    QString exeName = QString::fromStdString(App::GetApplication().getExecutableName());
    // ignore the lock file for this instance
    QString ownLock = exeName + QLatin1Char('_') + QString::number(QCoreApplication::applicationPid());
    QList<QFileInfo> locks = tmp.entryInfoList();
    for (QList<QFileInfo>::iterator it = locks.begin(); it != locks.end(); ++it) {
        QString bn = it->baseName();
        if (bn.startsWith(exeName + QLatin1Char('_')) && bn != ownLock) {
            QString fn = it->absoluteFilePath();

#if !defined(FC_OS_WIN32) || (BOOST_VERSION < 107600)
            boost::interprocess::file_lock flock(fn.toUtf8());
#else
            boost::interprocess::file_lock flock(fn.toStdWString().c_str());
#endif
            if (flock.try_lock()) {
                // OK, this file is a leftover from a previous crash
                QString crashed_pid = bn.mid(exeName.length()+1);
                // search for transient directories with this PID
                QString filter;
                QTextStream str(&filter);
                str << exeName << "_Doc_*_" << crashed_pid;
                tmp.setNameFilters(QStringList() << filter);
                tmp.setFilter(QDir::Dirs);
                QList<QFileInfo> dirs = tmp.entryInfoList();

                callableFunc(tmp, dirs, it->fileName());
            }
        }
    }
}

QFileInfoList DocumentRecoveryHandler::findOrphansWithoutLock() const
{
    QDir tmp(QString::fromUtf8(App::Application::getUserCachePath().c_str()));
    QString exeName = QString::fromStdString(App::GetApplication().getExecutableName());
    const qint64 ownPid = QCoreApplication::applicationPid();

    // A lock file, stale or live, leads the other scan to its directories
    const QStringList lockList =
        tmp.entryList(QStringList() << exeName + QStringLiteral("_*.lock"), QDir::Files);
    const QSet<QString> lockNames(lockList.begin(), lockList.end());

    QHash<qint64, qint64> startTimes;
    QFileInfoList orphans;
    const QFileInfoList dirs = tmp.entryInfoList(
        QStringList() << exeName + QStringLiteral("_Doc_*"), QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QFileInfo& dir : dirs) {
        // {ExeName}_Doc_{UUID}_{HASH}_{PID}, see App::Document::getTransientDirectoryName()
        QString name = dir.fileName();
        bool ok = false;
        qint64 pid = name.mid(name.lastIndexOf(QLatin1Char('_')) + 1).toLongLong(&ok);
        if (!ok || pid == ownPid)
            continue;
        QString lockName = exeName + QLatin1Char('_') + QString::number(pid) + QStringLiteral(".lock");
        if (lockNames.contains(lockName))
            continue;

        auto found = startTimes.constFind(pid);
        if (found == startTimes.constEnd())
            found = startTimes.insert(pid, processStartTime(pid));
        qint64 start = found.value();
        if (start == 0)
            continue;
        if (start > 0) {
            // PIDs get reused. A process started after the directory was made
            // did not make it; the second of slack only ever keeps a directory.
            QDateTime born = dir.birthTime();
            if (!born.isValid() || start <= born.toMSecsSinceEpoch() + 1000)
                continue;
        }
        orphans << dir;
    }
    return orphans;
}

void DocumentRecoveryHandler::removeStaleLock(const QString& lockFile, const QFileInfoList& dirs)
{
    if (lockFile.isEmpty())
        return;
    for (const QFileInfo& dir : dirs) {
        if (QFileInfo::exists(dir.absoluteFilePath()))
            return;
    }
    try {
#if !defined(FC_OS_WIN32) || (BOOST_VERSION < 107600)
        boost::interprocess::file_lock flock(lockFile.toUtf8());
#else
        boost::interprocess::file_lock flock(lockFile.toStdWString().c_str());
#endif
        // Held means a new process of the same PID owns it now
        if (flock.try_lock())
            QFile::remove(lockFile);
    }
    catch (const boost::interprocess::interprocess_exception&) {
        // gone already
    }
}

// ----------------------------------------------------------------------------

bool DocumentRecoveryCleaner::clearDirectory(const QFileInfo& dir)
{
    QDir qThisDir(dir.absoluteFilePath());
    if (!qThisDir.exists())
        return true;

    bool done = true;
    const QFileInfoList entries = qThisDir.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot
                                                         | QDir::Hidden | QDir::System);
    for (const QFileInfo& fi : entries) {
        if (canceled())
            return false;
        // Recurse into real directories only, never through a link
        bool isLink = fi.isSymLink();
#if QT_VERSION >= QT_VERSION_CHECK(6, 4, 0)
        isLink = isLink || fi.isJunction();
#endif
        if (fi.isDir() && !isLink) {
            if (!ignoreDirs.contains(fi) && !removeDirectory(fi))
                done = false;
        }
        else if (!ignoreFiles.contains(fi.fileName()) && !removeFile(fi)) {
            done = false;
        }
    }
    return done;
}

bool DocumentRecoveryCleaner::removeDirectory(const QFileInfo& dir)
{
    if (!clearDirectory(dir))
        return false;
    QString path = dir.absoluteFilePath();
    if (!QDir().rmdir(path))
        return !QFileInfo::exists(path);
    if (removed)
        removed->fetch_add(1, std::memory_order_relaxed);
    return true;
}

bool DocumentRecoveryCleaner::removeFile(const QFileInfo& fi)
{
    QString path = fi.absoluteFilePath();
    // Blob files are made read-only, and Windows refuses to delete a read-only
    // file. Clear the flag first, as Base::FileInfo::deleteDirectoryRecursive()
    // does.
    if (!fi.isSymLink() && !fi.isWritable())
        QFile::setPermissions(path, fi.permissions() | QFile::WriteOwner | QFile::WriteUser);
    // rmdir for a link to a directory
    if (!QFile::remove(path) && !QDir().rmdir(path))
        return false;
    if (removed)
        removed->fetch_add(1, std::memory_order_relaxed);
    return true;
}

void DocumentRecoveryCleaner::setIgnoreFiles(const QStringList& list)
{
    ignoreFiles = list;
}

void DocumentRecoveryCleaner::setIgnoreDirectories(const QFileInfoList& list)
{
    ignoreDirs = list;
}

void DocumentRecoveryCleaner::setProgress(std::atomic<int>* counter, const std::atomic<bool>* flag)
{
    removed = counter;
    cancel = flag;
}

QFileInfoList DocumentRecoveryCleaner::removeWithProgress(const QFileInfoList& dirs,
                                                          bool keepRoots,
                                                          QWidget* parent) const
{
    QFileInfoList survivors;
    if (dirs.isEmpty())
        return survivors;

    std::atomic<int> removedCount {0};
    std::atomic<int> total {0};   // 0 while counting
    std::atomic<bool> cancelFlag {false};
    std::atomic<bool> finished {false};
    DocumentRecoveryCleaner worker(*this);
    worker.setProgress(&removedCount, &cancelFlag);

    std::thread thread([&]() {
        // Count first: one directory usually holds nearly everything, so a
        // bar over directories would sit still
        int entries = 0;
        for (const QFileInfo& dir : dirs)
            entries += countEntries(dir.absoluteFilePath(), cancelFlag) + (keepRoots ? 0 : 1);
        total = std::max(entries, 1);
        for (const QFileInfo& dir : dirs) {
            if (cancelFlag || !(keepRoots ? worker.clearDirectory(dir) : worker.removeDirectory(dir)))
                survivors << dir;
        }
        finished = true;
    });
    // Nothing below may leave with the thread still running
    struct Joiner {
        std::thread& thread;
        std::atomic<bool>& cancel;
        ~Joiner()
        {
            if (thread.joinable()) {
                cancel = true;
                thread.join();
            }
        }
    } joiner {thread, cancelFlag};

    // The timer wakes the waits below; the worker posts nothing
    QTimer timer;
    timer.start(50);

    // Most jobs are done before a dialog would be worth showing. Until one
    // shows, hold back user input, so that nothing re-enters the caller.
    QElapsedTimer clock;
    clock.start();
    while (!finished && clock.elapsed() < 500)
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents | QEventLoop::WaitForMoreEvents);

    if (!finished) {
        const char* context = "Gui::Dialog::DocumentRecovery";
        QProgressDialog dlg(parent);
        dlg.setWindowModality(Qt::ApplicationModal);
        dlg.setWindowTitle(QCoreApplication::translate(context, "Cleanup"));
        dlg.setAutoReset(false);
        dlg.setAutoClose(false);
        dlg.setMinimumDuration(0);
        dlg.setRange(0, 0);
        auto update = [&]() {
            if (int count = total) {
                if (dlg.maximum() != count)
                    dlg.setRange(0, count);
                // The ignore lists make the count an upper bound, not exact
                dlg.setValue(std::min<int>(removedCount, count));
            }
            dlg.setLabelText(cancelFlag
                ? QCoreApplication::translate(context, "Canceling...")
                : QCoreApplication::translate(context, "Deleting transient files: %n removed",
                                              "", removedCount));
        };
        QObject::connect(&dlg, &QProgressDialog::canceled, &dlg, [&]() {
            cancelFlag = true;
            update();
        });
        update();
        dlg.show();

        QEventLoop loop;
        QObject::connect(&timer, &QTimer::timeout, &loop, [&]() {
            if (finished)
                loop.quit();
            else
                update();
        });
        if (!finished)
            loop.exec();
    }

    thread.join();
    return survivors;
}

void DocumentRecoveryCleaner::removeInBackground(const QList<StaleDirGroup>& groups)
{
    BackgroundRemoval::instance().start(groups);
}

#include "moc_DocumentRecovery.cpp"
