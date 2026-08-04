// SPDX-License-Identifier: LGPL-2.1-or-later
/****************************************************************************
 *                                                                          *
 *   Copyright (c) 2024 The FreeCAD Project Association AISBL               *
 *                                                                          *
 *   This file is part of FreeCAD.                                          *
 *                                                                          *
 *   FreeCAD is free software: you can redistribute it and/or modify it     *
 *   under the terms of the GNU Lesser General Public License as            *
 *   published by the Free Software Foundation, either version 2.1 of the   *
 *   License, or (at your option) any later version.                        *
 *                                                                          *
 *   FreeCAD is distributed in the hope that it will be useful, but         *
 *   WITHOUT ANY WARRANTY; without even the implied warranty of             *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU       *
 *   Lesser General Public License for more details.                        *
 *                                                                          *
 *   You should have received a copy of the GNU Lesser General Public       *
 *   License along with FreeCAD. If not, see                                *
 *   <https://www.gnu.org/licenses/>.                                       *
 *                                                                          *
 ***************************************************************************/

#include "PreCompiled.h"
#ifndef _PreComp_
#include <QByteArray>
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QStringList>
#endif

#include "DisplayedFilesModel.h"
#include <App/Application.h>
#include <App/ProjectFile.h>

using namespace Start;


namespace
{

std::string humanReadableSize(uint64_t bytes)
{
    static const std::vector<std::string> siPrefix {
        "b",
        "kb",
        "Mb",
        "Gb",
        "Tb",
        "Pb",
        "Eb"  // I think it's safe to stop here (for the time being)...
    };
    size_t base = 0;
    double inUnits = bytes;
    constexpr double siFactor {1000.0};
    while (inUnits > siFactor && base < siPrefix.size() - 1) {
        ++base;
        inUnits /= siFactor;
    }
    if (base == 0) {
        // Don't include a decimal point for bytes
        return fmt::format("{:.0f} {}", inUnits, siPrefix[base]);
    }
    // For all others, include one digit after the decimal place
    return fmt::format("{:.1f} {}", inUnits, siPrefix[base]);
}

FileStats fileInfoFromFreeCADFile(const std::string& path)
{
    App::ProjectFile proj(path);
    proj.loadDocument();
    auto metadata = proj.getMetadata();
    FileStats result;
    result.insert(std::make_pair(DisplayedFilesModelRoles::author, metadata.createdBy));
    result.insert(
        std::make_pair(DisplayedFilesModelRoles::modifiedTime, metadata.lastModifiedDate));
    result.insert(std::make_pair(DisplayedFilesModelRoles::creationTime, metadata.creationDate));
    result.insert(std::make_pair(DisplayedFilesModelRoles::company, metadata.company));
    result.insert(std::make_pair(DisplayedFilesModelRoles::license, metadata.license));
    result.insert(std::make_pair(DisplayedFilesModelRoles::description, metadata.comment));
    return result;
}

/// Load the thumbnail image data (if any) that is stored in an FCStd file.
/// \returns The image bytes, or an empty QByteArray (if no thumbnail was stored)
QByteArray loadFCStdThumbnail(const std::string& pathToFCStdFile)
{
    App::ProjectFile proj(pathToFCStdFile);
    if (proj.loadDocument()) {
        try {
            std::string thumbnailFile = proj.extractInputFile("thumbnails/Thumbnail.png");
            if (!thumbnailFile.empty()) {
                auto inputFile = QFile(QString::fromStdString(thumbnailFile));
                inputFile.open(QIODevice::OpenModeFlag::ReadOnly);
                return inputFile.readAll();
            }
        }
        catch (...) {
        }
    }
    return {};
}

/// A project saved as a directory rather than zipped into an .FCStd: the same entries,
/// unpacked, recognised the way App::Application and Document::restore recognise them.
bool isProjectDirectory(const std::string& path)
{
    return Base::FileInfo(path).isDir() && Base::FileInfo(path + "/Document.xml").exists();
}

/// A directory has no size of its own, so report what it holds. Recursively: a project
/// saved this way keeps its property data in blobs/, so counting only the top level
/// would report the XML and call a 200MB model 10kb.
uint64_t directorySize(const std::string& path)
{
    uint64_t total = 0;
    for (const auto& item : Base::FileInfo(path).getDirectoryContent()) {
        if (item.isDir()) {
            total += directorySize(item.filePath());
        }
        else {
            total += item.size();
        }
    }
    return total;
}

FileStats getFileInfo(const std::string& path)
{
    FileStats result;
    Base::FileInfo file(path);
    bool isDirectoryProject = isProjectDirectory(path);
    if (file.hasExtension("FCStd") || isDirectoryProject) {
        result = fileInfoFromFreeCADFile(path);
    }
    else {
        file.lastModified();
    }
    result.insert(std::make_pair(DisplayedFilesModelRoles::path, path));
    result.insert(std::make_pair(DisplayedFilesModelRoles::size,
                                 humanReadableSize(isDirectoryProject ? directorySize(path)
                                                                      : file.size())));
    result.insert(std::make_pair(DisplayedFilesModelRoles::baseName, file.fileName()));
    return result;
}

/// The file card is small, so the tooltip is where the rest of what we already know about
/// a project goes: when it was made and last touched, how big it is, who wrote it, under
/// what licence, and - the one the old start page was asked for most - where it lives.
QString buildToolTip(const FileStats& stats)
{
    auto field = [&stats](DisplayedFilesModelRoles role) {
        auto it = stats.find(role);
        return it == stats.end() ? QString() : QString::fromStdString(it->second);
    };

    QStringList lines;
    auto append = [&lines](const QString& label, const QString& value) {
        if (!value.isEmpty()) {
            lines.append(QStringLiteral("%1: %2").arg(label, value));
        }
    };

    append(QCoreApplication::translate("DisplayedFilesModel", "Created"),
           field(DisplayedFilesModelRoles::creationTime));
    append(QCoreApplication::translate("DisplayedFilesModel", "Last modified"),
           field(DisplayedFilesModelRoles::modifiedTime));
    append(QCoreApplication::translate("DisplayedFilesModel", "Size"),
           field(DisplayedFilesModelRoles::size));
    append(QCoreApplication::translate("DisplayedFilesModel", "Author"),
           field(DisplayedFilesModelRoles::author));
    append(QCoreApplication::translate("DisplayedFilesModel", "Company"),
           field(DisplayedFilesModelRoles::company));
    append(QCoreApplication::translate("DisplayedFilesModel", "License"),
           field(DisplayedFilesModelRoles::license));
    append(QCoreApplication::translate("DisplayedFilesModel", "Path"),
           field(DisplayedFilesModelRoles::path));

    auto description = field(DisplayedFilesModelRoles::description);
    if (!description.isEmpty()) {
        lines.append(QString());
        lines.append(description);
    }

    return lines.join(QLatin1Char('\n'));
}
}  // namespace

DisplayedFilesModel::DisplayedFilesModel(QObject* parent)
    : QAbstractListModel(parent)
{}


int DisplayedFilesModel::rowCount(const QModelIndex& parent) const
{
    Q_UNUSED(parent);
    return static_cast<int>(_fileInfoCache.size());
}

QVariant DisplayedFilesModel::data(const QModelIndex& index, int roleAsInt) const
{
    int row = index.row();
    if (row < 0 || row >= static_cast<int>(_fileInfoCache.size())) {
        return {};
    }
    auto mapEntry = _fileInfoCache.at(row);
    auto role = static_cast<DisplayedFilesModelRoles>(roleAsInt);
    switch (role) {
        case DisplayedFilesModelRoles::author:  // NOLINT(bugprone-branch-clone)
            [[fallthrough]];
        case DisplayedFilesModelRoles::baseName:
            [[fallthrough]];
        case DisplayedFilesModelRoles::company:
            [[fallthrough]];
        case DisplayedFilesModelRoles::creationTime:
            [[fallthrough]];
        case DisplayedFilesModelRoles::description:
            [[fallthrough]];
        case DisplayedFilesModelRoles::license:
            [[fallthrough]];
        case DisplayedFilesModelRoles::modifiedTime:
            [[fallthrough]];
        case DisplayedFilesModelRoles::path:
            [[fallthrough]];
        case DisplayedFilesModelRoles::size:
            if (mapEntry.find(role) != mapEntry.end()) {
                return QString::fromStdString(mapEntry.at(role));
            }
            else {
                return {};
            }
        case DisplayedFilesModelRoles::image: {
            auto path = QString::fromStdString(mapEntry.at(DisplayedFilesModelRoles::path));
            if (_imageCache.contains(path)) {
                return _imageCache[path];
            }
            break;
        }
        default:
            break;
    }
    switch (roleAsInt) {
        case Qt::ItemDataRole::ToolTipRole:
            return buildToolTip(mapEntry);
    }
    return {};
}

bool freecadCanOpen(const QString& extension)
{
    auto importTypes = App::GetApplication().getImportTypes();
    return std::find(importTypes.begin(), importTypes.end(), extension.toStdString())
        != importTypes.end();
}

void DisplayedFilesModel::addFile(const QString& filePath)
{
    QFileInfo qfi(filePath);
    if (!qfi.isReadable()) {
        return;
    }
    // A project saved as a directory carries no extension to judge it by, so ask what
    // it holds instead. Everything else still has to be a type FreeCAD can import.
    bool isDirectoryProject = isProjectDirectory(filePath.toStdString());
    if (!isDirectoryProject && !freecadCanOpen(qfi.suffix())) {
        return;
    }
    _fileInfoCache.emplace_back(getFileInfo(filePath.toStdString()));
    if (isDirectoryProject || qfi.completeSuffix() == QLatin1String("FCStd")) {
        auto thumbnail = loadFCStdThumbnail(filePath.toStdString());
        if (!thumbnail.isEmpty()) {
            _imageCache.insert(filePath, thumbnail);
        }
    }
}

void DisplayedFilesModel::clear()
{
    _fileInfoCache.clear();
}

QHash<int, QByteArray> DisplayedFilesModel::roleNames() const
{
    static QHash<int, QByteArray> nameMap {
        std::make_pair(int(DisplayedFilesModelRoles::author), "author"),
        std::make_pair(int(DisplayedFilesModelRoles::baseName), "baseName"),
        std::make_pair(int(DisplayedFilesModelRoles::company), "company"),
        std::make_pair(int(DisplayedFilesModelRoles::creationTime), "creationTime"),
        std::make_pair(int(DisplayedFilesModelRoles::description), "description"),
        std::make_pair(int(DisplayedFilesModelRoles::image), "image"),
        std::make_pair(int(DisplayedFilesModelRoles::license), "license"),
        std::make_pair(int(DisplayedFilesModelRoles::modifiedTime), "modifiedTime"),
        std::make_pair(int(DisplayedFilesModelRoles::path), "path"),
        std::make_pair(int(DisplayedFilesModelRoles::size), "size"),
    };
    return nameMap;
}
