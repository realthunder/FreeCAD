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
#include <string>
#include <utility>
#include <vector>

#include <Mod/Import/ImportGlobal.h>
#include <Base/FileInfo.h>
#include <Base/Placement.h>
#include <TDocStd_Document.hxx>
#include <TopoDS_Shape.hxx>

namespace Import
{

/// One occurrence of the assembly tree of a STEP root, as read from the
/// product structure before anything is transferred.
struct AssemblyNode
{
    /// One-based index of the owning node, 0 for a component of the root.
    int parent = 0;
    /// Whether this component is an assembly, i.e. holds components itself.
    bool isAssembly = false;
    /// Name of the component's product.
    std::string name;
    /// Placement of the component inside its owner.
    Base::Placement placement;
};

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

    /** @name Streamed component reading
     * A single-root assembly transfers as one unit, so nothing appears until
     * the whole file is through. openAssemblyTree() reads the assembly tree of
     * a root from the product structure - every occurrence that may be handed
     * over on its own, with its placement and product name, an owner always
     * before the components it holds - so an importer can build the containers
     * before any geometry exists. Its leaves are the transferable components:
     * transferComponentRange() moves a batch of them into the document, where
     * they show up as free shapes, and reports each result shape with the node
     * it came from. Transferring the root afterwards (transferRootRange)
     * reuses those results and gathers them under the assembly.
     */
    //@{
    int openAssemblyTree(Handle(TDocStd_Document) hDoc, int root);
    const std::vector<AssemblyNode>& assemblyNodes() const;
    /// Number of transferable components, i.e. of leaves of the tree.
    int componentCount() const;
    void transferComponentRange(Handle(TDocStd_Document) hDoc,
                                int first,
                                int last,
                                std::vector<std::pair<TopoDS_Shape, int>>& results);
    //@}

private:
    Base::FileInfo file;
    struct Stream;
    std::unique_ptr<Stream> stream;
    std::vector<AssemblyNode> nodes;
};

}  // namespace Import

#endif  // IMPORT_READER_STEP_H
