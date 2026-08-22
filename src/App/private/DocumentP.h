/***************************************************************************
 *   Copyright (c) 2002 Jürgen Riegel <juergen.riegel@web.de>              *
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

#ifndef APP_DOCUMENTP_H
#define APP_DOCUMENTP_H

#ifdef _MSC_VER
#pragma warning( disable : 4834 )
#endif

#include <App/Document.h>
#include <App/DocumentObject.h>
#include <App/DocumentObserver.h>
#include <App/StringHasher.h>
#include <App/FileBlobManager.h>
#include <Base/Reader.h>
#include <Base/Sequencer.h>
#include <CXX/Objects.hxx>
#include <boost/bimap.hpp>
#include <boost/graph/adjacency_list.hpp>
#include <boost/bimap.hpp>
#include <chrono>
#include <unordered_map>
#include <unordered_set>


// using VertexProperty = boost::property<boost::vertex_root_t, DocumentObject* >;
using DependencyList = boost::adjacency_list <
boost::vecS,           // class OutEdgeListS  : a Sequence or an AssociativeContainer
boost::vecS,           // class VertexListS   : a Sequence or a RandomAccessContainer
boost::directedS,      // class DirectedS     : This is a directed graph
boost::no_property,    // class VertexProperty:
boost::no_property,    // class EdgeProperty:
boost::no_property,    // class GraphProperty:
boost::listS           // class EdgeListS:
>;
using Traits = boost::graph_traits<DependencyList>;
using Vertex = Traits::vertex_descriptor;
using Edge =  Traits::edge_descriptor;
using Node =  std::vector <size_t>;
using Path =  std::vector <size_t>;

namespace App {
using HasherMap = boost::bimap<StringHasherRef, int>;
class Transaction;

using HasherMap = boost::bimap<StringHasherRef,int>;

// Pimpl class
struct DocumentP
{
    // Array to preserve the creation order of created objects
    std::vector<DocumentObject*> objectArray;
    std::unordered_set<App::DocumentObject*> touchedObjs;
    std::unordered_map<std::string, DocumentObject*> objectMap;
    std::unordered_map<long, DocumentObject*> objectIdMap;
    std::unordered_map<std::string, bool> partialLoadObjects;
    std::vector<DocumentObjectT> pendingRemove;
    std::vector<App::DocumentObject*> skippedObjs;
    long lastObjectId;
    mutable std::pair<long, long> treeRanks = std::make_pair(0,0);
    long treeRankRevision = 0;
    long revision = 0; // will increase on object add or remove
    DocumentObject* activeObject;
    Transaction *activeUndoTransaction;
    // pointer to the python class
    Py::Object DocumentPythonObject;
    int iTransactionMode;
    bool rollback;
    bool undoing; ///< document in the middle of undo or redo
    bool committing;
    bool opentransaction;
    std::bitset<32> StatusBits;
    int iUndoMode;
    unsigned int UndoMemSize;
    unsigned int UndoMaxStackSize;
    std::string programVersion;
    mutable HasherMap hashers;
    /// Lifetime of the files behind this document's PropertyFileIncluded.
    mutable std::unique_ptr<App::FileBlobManager> fileBlobs;
#ifdef USE_OLD_DAG
    DependencyList DepList;
    std::map<DocumentObject*, Vertex> VertexObjectList;
    std::map<Vertex, DocumentObject*> vertexMap;
#endif //USE_OLD_DAG
    std::multimap<const App::DocumentObject*,
        std::unique_ptr<App::DocumentObjectExecReturn> > _RecomputeLog;

    StringHasherRef Hasher;

    // restored files
    std::set<std::string> files;

    /// What was written to this document while it was in a
    /// Document::RestoreDrainGuard, and therefore not touched. Reported once
    /// by whoever opened the scope, then cleared.
    Document::RestoreDrainReport drainReport;

    /// Where the last restore() spent its time, reported as one line when it
    /// finishes. Split by stage so a load can be attributed without a
    /// profiler: the two XML passes, the archive bulk, and the fixup after.
    struct RestoreTiming {
        std::chrono::duration<double> create {0};
        /// Of 'create': what the addObject() calls took by themselves --
        /// the factory, the name bookkeeping, the notifications -- against
        /// the XML element reads that surround them in the same pass.
        std::chrono::duration<double> createAdd {0};
        std::chrono::duration<double> data {0};
        std::chrono::duration<double> files {0};
        std::size_t objectCount = 0;
        /// What the <ObjectData> pass spent inside property restores, of
        /// which 'value' is the share the properties themselves took once
        /// the element was read and the property found.
        App::PropertyContainer::RestoreStats props;

        void clear() { *this = RestoreTiming(); }
    };
    RestoreTiming restoreTiming;

    /** What a <Defaults> block says an object class holds.
     *
     * Restored into an object built for the purpose, and reduced to the
     * properties the record actually moved off what this build's constructor
     * produces. That list is usually empty -- the file was written by a build
     * that agrees with this one -- and then a document's objects cost nothing
     * to default. When it is not empty, those few properties are pasted onto
     * every object of the class before the file's own statement about that
     * object is read.
     */
    struct RestoreDefaults {
        std::unique_ptr<DocumentObject> proto;
        std::vector<std::string> names;
    };
    std::map<std::string, RestoreDefaults> restoreDefaults;

    /** Deferred archive-entry restores (docs/DocumentLoad.md §14).
     *
     * While DeferShapeLoad is on, the archive walk parks entries whose
     * consumer opted in (Property::DeferRestore) instead of serving
     * them. The reader stays behind -- it holds no open handle, only
     * the central-directory index -- and each consumer is served on
     * first real use through Document::restoreDeferredFile(), or in
     * bulk by flushDeferredFiles(). Keyed by object and property NAME,
     * not pointer: an entry whose object got deleted (or is parked in a
     * transaction) simply stops resolving, instead of dangling.
     */
    std::shared_ptr<Base::ZipFileReader> archiveReader;
    std::map<std::pair<std::string, std::string>, std::string> deferredFiles;
    /** Which object each split-XML entry of the save in progress is for.
     *
     * The entry used to be found by reading the object's name back out of the
     * file name, which made the name an identity rather than a name -- and a
     * name a file system will take is not always the name an object has, so
     * an object whose name is too long for a file name silently wrote an
     * empty entry. Written as the Objects section is, read as the entries
     * are, and stale only between the two.
     */
    std::map<std::string, App::DocumentObject*> splitXmlEntries;
    /// The serve phase's progress: alive across serve slices so the
    /// indicator shows shapes-served over the whole backlog, with the
    /// per-shape import indicators nested beneath it.
    std::unique_ptr<Base::SequencerLauncher> deferServeSeq;
    /// The save's progress, owned by Document::save() for the length of one
    /// save and borrowed by the loops it spans. Null outside a save -- an
    /// export writes objects through the same function and reports nothing.
    Base::SequencerLauncher* saveSeq {nullptr};
    /// Serve-time attribution for the slice log: entry opening vs the
    /// consumer's RestoreDocFile, against the slice wall clock.
    std::chrono::duration<double> deferOpenTime {0};
    std::chrono::duration<double> deferRestoreTime {0};

    DocumentP();

    long addObject(App::DocumentObject *pcObject) {
        int id = pcObject->getID();
        for (;;) {
            auto &entry = this->objectIdMap[id ? id : ++this->lastObjectId];
            if (entry) {
                id = 0;
                continue;
            }
            entry = pcObject;
            break;
        }
        ++revision;
        this->objectArray.push_back(pcObject);
        return id ? id : this->lastObjectId;
    }

    void addRecomputeLog(const char *why, App::DocumentObject *obj) {
        addRecomputeLog(new DocumentObjectExecReturn(why, obj));
    }

    void addRecomputeLog(const std::string &why, App::DocumentObject *obj) {
        addRecomputeLog(new DocumentObjectExecReturn(why, obj));
    }

    void addRecomputeLog(DocumentObjectExecReturn *returnCode) {
        if(!returnCode->Which) {
            delete returnCode;
            return;
        }
        _RecomputeLog.emplace(returnCode->Which, std::unique_ptr<DocumentObjectExecReturn>(returnCode));
        returnCode->Which->setStatus(ObjectStatus::Error, true);
    }

    void clearRecomputeLog(const App::DocumentObject *obj=nullptr) {
        if(!obj)
            _RecomputeLog.clear();
        else
            _RecomputeLog.erase(obj);
    }

    void clearDocument() {
        activeObject = nullptr;
        objectArray.clear();
        decltype(objectMap) map = std::move(objectMap);
        objectMap.clear();
        objectIdMap.clear();
        for (auto &v : map) {
            v.second->setStatus(ObjectStatus::Destroy, true);
            delete(v.second);
        }
    }

    const char *findRecomputeLog(const App::DocumentObject *obj) {
        auto range = _RecomputeLog.equal_range(obj);
        if(range.first == range.second)
            return nullptr;
        return (--range.second)->second->Why.c_str();
    }

    static
    void findAllPathsAt(const std::vector <Node> &all_nodes, size_t id,
                        std::vector <Path> &all_paths, Path tmp);
    std::vector<App::DocumentObject*>
    topologicalSort(const std::vector<App::DocumentObject*>& objects) const;
    std::vector<App::DocumentObject*>
    static partialTopologicalSort(const std::vector<App::DocumentObject*>& objects);
};

} // namespace App

#endif // APP_DOCUMENTP_H
