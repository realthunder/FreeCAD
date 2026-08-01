// SPDX-License-Identifier: LGPL-2.1-or-later

/***************************************************************************
 *   Copyright (c) 2023 Werner Mayer <wmayer[at]users.sourceforge.net>     *
 *                                                                         *
 *   This file is part of FreeCAD.                                         *
 *                                                                         *
 *   FreeCAD is free software: you can redistribute it and/or modify it    *
 *   under the terms of the GNU Lesser General Public License as           *
 *   published by the Free Software Foundation, either version 2.1 of the  *
 *   License, or (at your option) any later version.                       *
 *                                                                         *
 *   FreeCAD is distributed in the hope that it will be useful, but        *
 *   WITHOUT ANY WARRANTY; without even the implied warranty of            *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU      *
 *   Lesser General Public License for more details.                       *
 *                                                                         *
 *   You should have received a copy of the GNU Lesser General Public      *
 *   License along with FreeCAD. If not, see                               *
 *   <https://www.gnu.org/licenses/>.                                      *
 *                                                                         *
 **************************************************************************/

#ifndef IMPORT_READER_STEP_H
#define IMPORT_READER_STEP_H

#include <memory>

#include <Mod/Import/ImportGlobal.h>
#include <Base/FileInfo.h>
#include <TDocStd_Document.hxx>

namespace Import
{

class ImportExport ReaderStep
{
public:
    explicit ReaderStep(const Base::FileInfo& file);
    ~ReaderStep();

    void read(Handle(TDocStd_Document) hDoc);

    /** @name Streamed (batched) reading
     * openStream() parses the file and returns the number of transferable
     * roots, keeping the OCCT reader alive; transferRootRange() then moves a
     * contiguous batch of roots into the XCAF document (shape healing runs
     * deferred over the batch, in parallel when enabled). One progress
     * indicator spans the whole file and Escape aborts between batches.
     */
    //@{
    int openStream();
    void transferRootRange(Handle(TDocStd_Document) hDoc, int first, int last);
    void closeStream();
    //@}

private:
    Base::FileInfo file;
    struct Stream;
    std::unique_ptr<Stream> stream;
};

}  // namespace Import

#endif  // IMPORT_READER_STEP_H
