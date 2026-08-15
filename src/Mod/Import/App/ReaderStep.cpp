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


#include "PreCompiled.h"
#ifndef _PreComp_
#include <algorithm>
#include <Standard_Version.hxx>
#include <NCollection_HSequence.hxx>
#include <STEPCAFControl_Reader.hxx>
#if OCC_VERSION_HEX >= 0x070500
#include <Message_ProgressRange.hxx>
#include <Message_ProgressScope.hxx>
#endif
#include <Transfer_TransientProcess.hxx>
#include <XSControl_TransferReader.hxx>
#include <XSControl_WorkSession.hxx>
#endif

#include "ReaderStep.h"
#include <Base/Exception.h>
#include <Mod/Part/App/TopoShape.h>
#include <Mod/Part/App/encodeFilename.h>
#include <Mod/Part/App/ProgressIndicator.h>

using namespace Import;

struct ReaderStep::Stream
{
    STEPCAFControl_Reader reader;
    opencascade::handle<Part::ProgressIndicator> progress;
    std::unique_ptr<Message_ProgressScope> scope;
    int roots = 0;
#if OCC_VERSION_HEX >= 0x080000
    /// The assembly tree opened by openAssemblyTree(): every occurrence that
    /// may be handed over on its own, an owner before its components.
    NCollection_Sequence<STEPCAFControl_Reader::AssemblyNode> tree;
    /// Indices (into tree) of the leaves, in transfer order.
    std::vector<int> components;
#endif

    /// (Re)start the progress scope over the given number of steps.
    void openScope(int steps)
    {
        scope = std::make_unique<Message_ProgressScope>(progress->Start(),
                                                       "Reading STEP file...",
                                                       std::max(steps, 1));
    }
};

ReaderStep::ReaderStep(const Base::FileInfo& file)  // NOLINT
    : file {file}
{}

ReaderStep::~ReaderStep() = default;

int ReaderStep::openStream()
{
#if OCC_VERSION_HEX < 0x080000
    // The fork's STEPCAFControl_Reader::TransferRootRange only exists on the
    // OCCT 8 branch; report "not streamable" so the caller falls back to the
    // one-shot read().
    return 0;
#else
    std::string utf8Name = file.filePath();
    std::string name8bit = Part::encodeFilename(utf8Name);
    stream = std::make_unique<Stream>();
    auto& reader = stream->reader;
    reader.SetColorMode(true);
    reader.SetNameMode(true);
    reader.SetLayerMode(true);
    reader.SetSHUOMode(true);
    if (reader.ReadFile(name8bit.c_str()) != IFSelect_RetDone) {
        stream.reset();
        throw Base::FileException("Cannot read STEP file", file);
    }
    stream->roots = reader.NbRootsForTransfer();
    stream->progress = new Part::ProgressIndicator(100);
    stream->openScope(stream->roots);
    return stream->roots;
#endif
}

int ReaderStep::openAssemblyTree(Handle(TDocStd_Document) hDoc, int root)  // NOLINT
{
#if OCC_VERSION_HEX < 0x080000
    (void)hDoc;
    (void)root;
    return 0;
#else
    if (!stream) {
        THROWM(Base::RuntimeError, "ReaderStep: no open stream")
    }
    auto& s = *stream;
    nodes.clear();
    s.components.clear();
    if (s.reader.RootAssemblyTree(root, hDoc, s.tree) == 0) {
        return 0;
    }
    nodes.reserve(s.tree.Size());
    for (int i = 1; i <= int(s.tree.Size()); ++i) {
        const auto& node = s.tree.Value(i);
        AssemblyNode out;
        out.parent = node.Parent;
        out.isAssembly = node.IsAssembly;
        out.name = node.Name.ToCString();
        out.placement = Base::Placement(Part::TopoShape::convert(node.Location));
        nodes.push_back(std::move(out));
        if (!node.IsAssembly) {
            s.components.push_back(i);
        }
    }
    // One step per streamed component plus one for the owning root, which
    // still has to gather them (and translate whatever was left to it).
    s.openScope(int(s.components.size()) + 1);
    return int(nodes.size());
#endif
}

const std::vector<AssemblyNode>& ReaderStep::assemblyNodes() const
{
    return nodes;
}

int ReaderStep::componentCount() const
{
#if OCC_VERSION_HEX < 0x080000
    return 0;
#else
    return stream ? int(stream->components.size()) : 0;
#endif
}

void ReaderStep::transferComponentRange(Handle(TDocStd_Document) hDoc,  // NOLINT
                                        int first,
                                        int last,
                                        std::vector<std::pair<TopoDS_Shape, int>>& results)
{
#if OCC_VERSION_HEX < 0x080000
    (void)hDoc;
    (void)first;
    (void)last;
    (void)results;
    THROWM(Base::RuntimeError, "ReaderStep: streamed transfer requires OCCT 8")
#else
    if (!stream || stream->components.empty()) {
        THROWM(Base::RuntimeError, "ReaderStep: no open component stream")
    }
    auto& s = *stream;
    Handle(NCollection_HSequence<Handle(Standard_Transient)>) batch =
        new NCollection_HSequence<Handle(Standard_Transient)>;
    std::vector<int> batchNodes;
    for (int i = first; i <= last && i <= int(s.components.size()); ++i) {
        int node = s.components[i - 1];
        batch->Append(s.tree.Value(node).Component);
        batchNodes.push_back(node);
    }
    if (batch->IsEmpty()) {
        return;
    }
    s.reader.TransferComponents(batch, hDoc, s.scope->Next(double(batch->Size())));
    if (s.progress->UserBreak()) {
        THROWM(Base::AbortException, "STEP import aborted by user")
    }
    // Report what each component became, so the caller can tell which node of
    // the tree - and so which container - a new free shape belongs to.
    results.clear();
    for (std::size_t i = 0; i < batchNodes.size(); ++i) {
        TopoDS_Shape shape = s.reader.ComponentShape(batch->Value(int(i) + 1));
        if (!shape.IsNull()) {
            results.emplace_back(shape, batchNodes[i]);
        }
    }
#endif
}

void ReaderStep::transferRootRange(Handle(TDocStd_Document) hDoc, int first, int last)  // NOLINT
{
#if OCC_VERSION_HEX < 0x080000
    (void)hDoc;
    (void)first;
    (void)last;
    THROWM(Base::RuntimeError, "ReaderStep: streamed transfer requires OCCT 8")
#else
    if (!stream) {
        THROWM(Base::RuntimeError, "ReaderStep: no open stream")
    }
    auto& s = *stream;
    s.reader.TransferRootRange(first, last, hDoc, s.scope->Next(double(last - first + 1)));
    if (s.progress->UserBreak()) {
        THROWM(Base::AbortException, "STEP import aborted by user")
    }
#endif
}

void ReaderStep::closeStream()
{
    stream.reset();
}

void ReaderStep::read(Handle(TDocStd_Document) hDoc)  // NOLINT
{
    std::string utf8Name = file.filePath();
    std::string name8bit = Part::encodeFilename(utf8Name);
    STEPCAFControl_Reader aReader;
    aReader.SetColorMode(true);
    aReader.SetNameMode(true);
    aReader.SetLayerMode(true);
    aReader.SetSHUOMode(true);
    if (aReader.ReadFile(name8bit.c_str()) != IFSelect_RetDone) {
        throw Base::FileException("Cannot read STEP file", file);
    }

#if OCC_VERSION_HEX >= 0x070500
    opencascade::handle<Part::ProgressIndicator> pi = new Part::ProgressIndicator(100);
    aReader.Transfer(hDoc, pi->Start());
    if (pi->UserBreak()) {
        THROWM(Base::AbortException, "STEP import aborted by user")
    }
#else
    Handle(Message_ProgressIndicator) pi = new Part::ProgressIndicator(100);
    aReader.Reader().WS()->MapReader()->SetProgress(pi);
    pi->NewScope(100, "Reading STEP file...");
    pi->Show();
    aReader.Transfer(hDoc);
    pi->EndScope();
#endif
}
