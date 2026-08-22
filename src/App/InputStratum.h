/***************************************************************************
 *   Copyright (c) 2026 Zheng, Lei <realthunder.dev@gmail.com>             *
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

#ifndef APP_INPUTSTRATUM_H
#define APP_INPUTSTRATUM_H

#include <cstddef>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <FCGlobal.h>

namespace App
{

class Document;
class DocumentObject;
class Expression;
class ObjectIdentifier;
class Property;

/** The input recompute stratum.
 *
 * See docs/InputProperties.md section 5. An input property is one marked
 * Property::Input, meaning no execute() ever writes it. Because of that its
 * consumers need no ordering edge back to its owner, which is what lets a
 * container's children read the container's own parameters. The edge is
 * dropped in PropertyExpressionEngine::hasSetValue(); this class is what
 * replaces it.
 *
 * It is built fresh at the head of every Document::recompute(). There is no
 * cached state and nothing to invalidate: the build is one map-emptiness test
 * per object, and only objects that actually carry expressions cost more than
 * that. Evaluation -- the part that is expensive, because it runs the
 * expression interpreter -- is restricted to the dirty subset.
 *
 * Two structures come out of the build:
 *
 * - the sub-DAG, one node per binding whose target property is an input
 *   property, edged by in-stratum reads and topologically sorted;
 * - the propagation record, mapping each input property to the bindings that
 *   read it. In-stratum readers become nodes to evaluate; out-of-stratum
 *   readers become objects to push into the object phase, standing in for the
 *   dependency edge that is no longer there.
 *
 * Cross document references are deliberately left alone -- see section 8 --
 * so they keep the ordinary edge and the revision based staleness check.
 */
class AppExport InputStratum
{
public:
    /// An object plus a property name on it. Names rather than pointers so a
    /// reference can be recorded before the property is known to exist.
    using PropRef = std::pair<DocumentObject*, std::string>;

    explicit InputStratum(Document* doc);
    ~InputStratum();

    InputStratum(const InputStratum&) = delete;
    InputStratum& operator=(const InputStratum&) = delete;

    /** Scan the document and build the sub-DAG and the propagation record.
     *
     * @param error: filled with a description when the sub-DAG is cyclic.
     * @param culprit: set to an object the failure can be reported against,
     * since the document's recompute log is keyed by object.
     * @return false if the sub-DAG is cyclic, in which case nothing should be
     * evaluated -- there is no order to evaluate it in.
     */
    bool build(std::string& error, DocumentObject** culprit = nullptr);

    /** Evaluate the dirty part of the stratum, in topological order.
     *
     * @param force: evaluate every node, ignoring the dirty test.
     * @param referrers: appended with the objects that read an input property
     * whose value changed and are not themselves in the stratum. The caller
     * pushes these into the object phase.
     * @param error: filled with the first evaluation failure.
     * @param culprit: set to the object whose binding failed.
     * @return false on an evaluation failure.
     */
    bool evaluate(bool force,
                  std::vector<DocumentObject*>& referrers,
                  std::string& error,
                  DocumentObject** culprit = nullptr);

    /// True when the document has nothing marked input to do.
    bool empty() const;

    /** The bindings that read the given input property.
     *
     * The property editor's warning of section 7 is this question; answering
     * it costs nothing extra because the propagation record is already here.
     */
    std::vector<PropRef> referrersOf(DocumentObject* obj, const std::string& propName) const;

    /** The bindings in \a obj's own document that read \a propName on it.
     *
     * The standalone counterpart of referrersOf(): it scans the document
     * rather than reading a propagation record, so it can answer for a
     * property whose Input status is about to change -- which is what the
     * property editor warning of section 7 needs, and what refreshReferrers()
     * walks. Hidden references are included: a hiddenref() has no ordering
     * edge to lose, but it still counts for the closure rule of section 4.
     *
     * Same document only, because that is the only place an ordering edge
     * depends on the status at all -- see section 8.
     */
    static std::vector<PropRef> findReferrers(DocumentObject* obj, const std::string& propName);

    /** Rebuild the dependencies of everything that reads \a propName on \a obj.
     *
     * The dependency set of a binding depends on the Input status of the
     * properties it reads, and that status can change after the binding was
     * set: marking a property input should drop the ordering edges its readers
     * carry, and clearing it should put them back. Nothing recomputes those
     * edges on its own, so section 7 has this called from
     * DocumentObject::onPropertyStatusChanged().
     */
    static void refreshReferrers(DocumentObject* obj, const std::string& propName);

    /** Would binding \a expr to \a path close a cycle inside the stratum, or
     * break the closure rule of section 4?
     *
     * Called from PropertyExpressionEngine::validateExpression(). The ordinary
     * cycle check there works over the object graph, and these references have
     * had their object level edge removed, so it cannot see them.
     *
     * @return an error message, or an empty string when the binding is sound.
     */
    static std::string checkBinding(DocumentObject* owner, const ObjectIdentifier& path,
                                    const Expression* expr);

    /** Collect what an expression reads, in one traversal.
     *
     * @param deps: object level dependencies, exactly as
     * Expression::getDepObjects() would produce them, mapped to whether every
     * reference to that object was inside a hiddenref().
     * @param labels: appended with the label references, as getDepObjects().
     * @param refs: per property dependencies, with the same hidden flag. An
     * empty property name means the whole object is referenced, which no input
     * property can satisfy -- input-ness is per property.
     *
     * Resolving an identifier is not cheap (it takes the GIL), so the caller
     * that wants both levels must not ask twice.
     */
    static void collectDeps(const Expression* expr,
                            std::map<DocumentObject*, bool>& deps,
                            std::vector<std::string>* labels,
                            std::map<PropRef, bool>* refs);

    /// collectDeps() for a caller that only wants the per property half.
    static void collectPropRefs(const Expression* expr,
                                std::map<PropRef, bool>& refs);

    /** Is this a reference whose ordering edge may be dropped?
     *
     * That is: does it name a property marked Property::Input, in \a reader's
     * own document. Cross document references keep their edge -- section 8 --
     * even though the closure rule accepts them.
     */
    static bool isInputRef(const DocumentObject* reader, const PropRef& ref);

private:
    class Private;
    Private* d;
};

}  // namespace App

#endif  // APP_INPUTSTRATUM_H
