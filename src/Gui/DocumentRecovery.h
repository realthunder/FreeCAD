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


#ifndef GUI_DIALOG_DOCUMENTRECOVERY_H
#define GUI_DIALOG_DOCUMENTRECOVERY_H

#include <atomic>
#include <functional>
#include <QDialog>
#include <QFileInfo>
#include <QFileInfoList>
#include <QList>
#include <QPair>
#include <QScopedPointer>


namespace Gui { namespace Dialog {

class DocumentRecoveryPrivate;

/*!
 @author Werner Mayer
 */
class DocumentRecovery : public QDialog
{
    Q_OBJECT

public:
    explicit DocumentRecovery(const QList<QFileInfo>&, QWidget* parent = nullptr);
    ~DocumentRecovery() override;

    void accept() override;
    bool foundDocuments() const;

protected:
    void closeEvent(QCloseEvent*) override;
    void contextMenuEvent(QContextMenuEvent*) override;
    QString createProjectFile(const QString&);

protected:
    void onButtonCleanupClicked();
    void onDeleteSection();

private:
    static std::string doctools;
    QScopedPointer<DocumentRecoveryPrivate> d_ptr;
    Q_DISABLE_COPY(DocumentRecovery)
    Q_DECLARE_PRIVATE(DocumentRecovery)
};

/// Stale transient directories, with the lock file (may be empty) to drop
/// once all of them are gone
using StaleDirGroup = QPair<QString, QFileInfoList>;

class DocumentRecoveryFinder {
public:
    bool checkForPreviousCrashes();

    /// Whether \a dir holds anything the recovery dialog can offer. A transient
    /// directory of a dead session without it holds only that session's
    /// blobs, and is deleted.
    static bool isRecoverable(const QFileInfo& dir);

private:
    void checkDocumentDirs(QDir&, const QList<QFileInfo>&, const QString&);
    bool showRecoveryDialogIfNeeded();

private:
    QList<QFileInfo> restoreDocFiles;
    QList<StaleDirGroup> staleDirs;
};

class DocumentRecoveryHandler {
public:
    void checkForPreviousCrashes(const std::function<void(QDir&, const QList<QFileInfo>&, const QString&)> & callableFunc) const;

    /// Transient directories of dead processes that left no lock file to find
    /// them by. Only the GUI makes one, so these are FreeCADCmd's, a Python
    /// host's or a test's.
    QFileInfoList findOrphansWithoutLock() const;

    /// Remove the lock file \a lockFile of a dead instance, provided none of
    /// \a dirs survives and nobody holds the lock.
    static void removeStaleLock(const QString& lockFile, const QFileInfoList& dirs);
};

class DocumentRecoveryCleaner {
public:
    /// Delete the content of \a dir except the ignored entries. Returns false
    /// if anything not ignored survives.
    bool clearDirectory(const QFileInfo& dir);
    /// clearDirectory(), then \a dir itself
    bool removeDirectory(const QFileInfo& dir);
    void setIgnoreFiles(const QStringList&);
    void setIgnoreDirectories(const QFileInfoList&);
    /// Count each deleted entry into \a removed and stop once \a cancel is
    /// set. Both must outlive the calls.
    void setProgress(std::atomic<int>* removed, const std::atomic<bool>* cancel);

    /// Remove each of \a dirs -- only its content if \a keepRoots -- on a
    /// worker thread, behind a modal progress dialog that can cancel it.
    /// Returns the directories that were not removed completely.
    QFileInfoList removeWithProgress(const QFileInfoList& dirs, bool keepRoots,
                                     QWidget* parent) const;

    /// Remove the directories of \a groups on a background thread with no
    /// dialog, then each group's lock file if its directories are all gone.
    /// Canceled when the application quits.
    static void removeInBackground(const QList<StaleDirGroup>& groups);

private:
    bool removeFile(const QFileInfo& fi);
    bool canceled() const
    {
        return cancel && cancel->load(std::memory_order_relaxed);
    }

private:
    QStringList ignoreFiles;
    QFileInfoList ignoreDirs;
    std::atomic<int>* removed = nullptr;
    const std::atomic<bool>* cancel = nullptr;
};

} //namespace Dialog

} //namespace Gui


#endif //GUI_DIALOG_DOCUMENTRECOVERY_H
