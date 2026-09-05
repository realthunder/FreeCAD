/***************************************************************************
 *   Copyright (c) 2026 FreeCAD contributors                               *
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

#ifndef PART_FOREIGNBASESHAPES_H
#define PART_FOREIGNBASESHAPES_H

#include <string>
#include <Mod/Part/PartGlobal.h>

namespace App {
class Document;
class DocumentObject;
class PropertyLinkBase;
}

namespace Part {

class TopoShape;

/** The evidence a document keeps for its element references into other
 * documents (docs/TopoNamingEnhance.md 7.13).
 *
 * A feature retains a generation of its shape only for referrers in its own
 * document: a referrer in another document cannot be seen while that
 * document is closed, which is the normal state of an assembly while its
 * part is edited.  So the referrer document keeps the evidence itself, as
 * sub-shapes rather than whole shapes: one document-wide
 * `Part::PropertyPartShape` holding a compound of every foreign sub-shape
 * its element references resolved against, and one `App::PropertyMap`
 * mapping the full reference name to the child's index in the compound.
 * Both are dynamic properties of the document, so a document with no such
 * reference carries neither.
 *
 * The store is rebuilt at every save of the document: a healthy reference
 * is refreshed from the live foreign shape, a missing one keeps the child
 * it has, everything else is dropped.  It is served through
 * Feature::searchElementCache() when a referring property passes itself
 * along with the request.
 */
class PartExport ForeignBaseShapes
{
public:
    /// The name of the compound property, `_ForeignBaseShapes`
    static const char *shapesName();
    /// The name of the manifest property, `_ForeignBaseShapeRefs`
    static const char *refsName();

    /** The name a reference is filed under.
     *
     * The persisted file path of the XLink when 'obj' is in another
     * document (relative to the referring file, as the link writes it),
     * then the object name and 'subname', which is the sub-name path as the
     * property holds it ending in the indexed element name:
     * `../parts/bracket.FCStd#Cut.Face3`, or `Link.Face3` for a reference
     * through a local object.  Empty when the reference cannot be named:
     * a cross-document reference that is not an XLink, or a linked document
     * that has never been saved.
     */
    static std::string referenceKey(const App::PropertyLinkBase *referrer,
                                    const App::DocumentObject *obj,
                                    const char *subname);

    /// The sub-shape filed under 'key' in the store of 'doc', or a null shape
    static TopoShape find(const App::Document *doc, const std::string &key);

    /// Rebuild the store of 'doc' from its element references, see above
    static void rebuild(App::Document &doc);

    /// Connected to App::Application::signalStartSaveDocument by the Part module
    static void onStartSaveDocument(const App::Document &doc, const std::string &filename);
};

} // namespace Part

#endif // PART_FOREIGNBASESHAPES_H
