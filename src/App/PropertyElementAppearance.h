/***************************************************************************
 *   Copyright (c) 2026 Zheng, Lei <realthunder.dev@gmail.com>              *
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

#ifndef APP_PROPERTYELEMENTAPPEARANCE_H
#define APP_PROPERTYELEMENTAPPEARANCE_H

#include <array>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "AppearanceList.h"
#include "FileBlobManager.h"
#include "PropertyLinks.h"

namespace Data
{
class ComplexGeoData;
}

namespace App
{

class ElementAppearancePy;
class PropertyAppearanceList;

/** What the elements of an object's shape look like, held by the object
 *
 * docs/ShapeAppearanceDesign.md sec 14. One store for the look of an
 * object and of its faces, edges and vertices, on the object and not on its
 * view provider, so that it is there without one.
 *
 * It is a link to the object it is on, and the sub-elements of that link
 * are the elements given a look BY NAME: kept by their mapped names, and
 * followed through a recompute as any reference to an element is. Beside
 * the names, for each kind of element:
 *
 *  - the kind's OWN look -- the object's, where no element says otherwise;
 *    what the names "Face", "Edge" and "Vertex" mean;
 *  - the looks given BY NUMBER, for an element the shape gives no name;
 *  - what is DRAWN: every element's look as it comes out of the above and
 *    of whatever else the owner lays in (a face taking the look of the face
 *    it was made from). Made and not stated, and kept so that reading a
 *    document does not have to make it again.
 *
 * and for the names, a look each and which of it is the name's own.
 *
 * @section elementappearance_storage Storage
 *
 * Each of those is an App::AppearanceList, a base and the entries that
 * override it. A kind's OWN look is a list of one. The NUMBERED list of a
 * kind is one entry for each element, and says of each what it looks like
 * by its number: an element is given a look by number where its entry
 * differs from the kind's own look. The NAMED list has an entry for each
 * name, in the order of the names. The DRAWN list of a kind is one entry
 * for each element.
 *
 * The kind's own look is not the numbered list's base, though the two are
 * the same until every element is given the same look by number: a list
 * keeps what all its entries agree on as its base (sec 12), and an encoding
 * that states one entry at a time -- every one below schema 5 -- has no base
 * in it at all. Kept apart, six faces of a box painted red by number are six
 * red faces of a box that is still the colour it was.
 *
 * A list is a shared value, so what is drawn costs a pointer while it says
 * what the numbered list does -- no names, nothing laid in: an import -- and
 * a view provider that draws from it holds the same storage and not a copy.
 * A list that says nothing is not there at all.
 *
 * @section elementappearance_own Which of a look is a name's own
 *
 * Sixteen bits a name (Own). A name given a colour states its colour and
 * is the object in everything else, as the object comes to be; a name given
 * a material states all of it. So a painted face takes the gloss the object
 * is given after, and a face given a material keeps its own, face by face.
 *
 * @section elementappearance_index Smart indexing
 *
 * An element is named "Face3", or by its mapped name, or by kind and
 * number. A read answers what is drawn. A write goes to the name where the
 * shape has a mapped name for the element and to the number where it has
 * not (hasMappedName()): on a shape without an element map a name is the
 * number, and holding it as a string buys nothing and costs a great deal
 * (sec 14.1).
 */
class AppExport PropertyElementAppearance : public PropertyLinkSubHidden,
                                            public BlobReferrerProperty
{
    TYPESYSTEM_HEADER_WITH_OVERRIDE();

public:
    enum Kind
    {
        Face,
        Edge,
        Vertex,
        KindCount
    };

    /// The fields of a look a name states; the rest are the kind's own
    enum Own : uint16_t
    {
        OwnNone = 0,
        /// Colour and transparency: what "painted" is
        OwnDiffuse = 1 << 0,
        OwnAmbient = 1 << 1,
        OwnSpecular = 1 << 2,
        OwnEmissive = 1 << 3,
        OwnShininess = 1 << 4,
        OwnFinish = 1 << 5,
        OwnTexture = 1 << 6,
        /// The inline image and its path
        OwnImage = 1 << 7,
        /// The material card it names
        OwnCard = 1 << 8,
        OwnMaterialX = 1 << 9,
        OwnType = 1 << 10,
        OwnAll = (1 << 11) - 1
    };

    PropertyElementAppearance();
    ~PropertyElementAppearance() override;

    /** @name Names of elements */
    //@{
    /// "Face", "Edge", "Vertex"
    static const char *kindName(Kind kind);
    /** Take a name apart as the shape counts it
     *
     * "Face3" is (Face, 2) and "Face" is (Face, -1), the kind itself. A
     * leading path and the marker of a missing element are stepped over.
     * False for a name that is neither, a mapped name among them.
     */
    static bool parseElement(const char *name, Kind &kind, int &index);
    /// (Face, 2) is "Face3"; (Face, -1) is "Face"
    static std::string elementName(Kind kind, int index);
    /// The names of the fields \a own states, as App.Material spells them
    static std::vector<std::string> ownNames(uint16_t own);
    /// The fields named so; @throw Base::ValueError for a name that is none
    static uint16_t ownFromNames(const std::vector<std::string> &names);
    /// The fields of \a look that differ from \a from
    static uint16_t differingFields(const MaterialAppearance &look,
                                    const MaterialAppearance &from);
    /// \a base, with the fields \a own states taken from \a look
    static MaterialAppearance layOver(const MaterialAppearance &base,
                                      const MaterialAppearance &look,
                                      uint16_t own);
    //@}

    /** One change, however many writes
     *
     * Every write below announces itself; held around several, they are
     * announced once, and the names are looked up in the shape once.
     */
    class AppExport Edit
    {
    public:
        explicit Edit(PropertyElementAppearance &prop);
        ~Edit();
        Edit(const Edit &) = delete;
        Edit &operator=(const Edit &) = delete;

    private:
        PropertyElementAppearance &prop;
    };

    /** @name A kind's own look: the object's */
    //@{
    /// Whether one was given. Until then getBase() is the default material.
    bool hasBase(Kind kind) const;
    const MaterialAppearance &getBase(Kind kind) const;
    void setBase(Kind kind, const MaterialAppearance &look);
    /** Whether the kind's own look is the object's material card's
     *
     * The flag of the own look's list (docs/MaterialStorage.md sec 15.3).
     * True until a look is given: a look nobody chose is the card's to
     * give. setBase() ends it, as any look somebody chose does.
     */
    bool isFollowingMaterial(Kind kind) const;
    /// The card's look as the kind's own, and following it from now on
    void followMaterial(Kind kind, const MaterialAppearance &card);
    /** The kind's own look as a list of one made elsewhere
     *
     * A list is a shared value: every object given the same list holds the
     * one storage. What a new object is given, by the thousand.
     */
    void setBaseList(Kind kind, const AppearanceList &own);
    //@}

    /** @name By number
     *
     * One entry for each element of the kind, each the kind's own look or
     * the look the element was given by its number. Empty while no element
     * was given one.
     */
    //@{
    const AppearanceList &getNumbered(Kind kind) const;
    /// All of them at once, which is what an import has. An entry that is
    /// the kind's own look states nothing.
    void setNumbered(Kind kind, const AppearanceList &list);
    /// The fields the element's entry states: those in which it differs
    /// from the kind's own look
    uint16_t getNumberedOwn(Kind kind, int index) const;
    //@}

    /** @name By name */
    //@{
    int getNamedCount() const { return static_cast<int>(_cSubList.size()); }
    /// One whole entry for each name, in the order of getSubValues()
    const AppearanceList &getNamedLooks() const;
    /// Which fields the name at \a pos states
    uint16_t getNamedOwn(int pos) const;
    /// The kind of element the name at \a pos is of; KindCount for none
    Kind getNamedKind(int pos) const;
    /// The look of the name at \a pos as it is drawn: what it states, laid
    /// over its kind's own look as that is now
    MaterialAppearance getNamedLook(int pos) const;
    /// Where \a element is among the names, by the name the shape counts it
    /// by or by its mapped name; -1 if it is not
    int findNamed(const char *element) const;
    /** All the names at once
     *
     * \a own is as long as \a subs, or empty for "every one states all of
     * its look". \a looks has an entry for each.
     */
    void setNamed(std::vector<std::string> &&subs, const AppearanceList &looks,
                  std::vector<uint16_t> &&own = {});
    //@}

    /** @name What is drawn
     *
     * getDrawn() is the numbered list -- or the kind's own look, a list of
     * one -- until somebody lays more in. compose() makes the list -- the
     * numbered one at \a count entries, then the looks
     * handed on, then the names, each to the elements \a named says it is
     * now -- and setDrawn() keeps it. Keeping it is no change to undo: it
     * is made of what is, and made again when that changes.
     */
    //@{
    const AppearanceList &getDrawn(Kind kind) const;
    void setDrawn(Kind kind, const AppearanceList &list);
    /// \a named: an element's number to the place of its name
    AppearanceList compose(Kind kind, int count,
                           const std::vector<std::pair<int, int>> &named,
                           const std::map<int, MaterialAppearance> *handedOn = nullptr) const;
    //@}

    /** @name Smart indexing */
    //@{
    /// Whether the shape has a mapped name for an element, which decides
    /// whether a look given to it is held by name or by number
    virtual bool hasMappedName(Kind kind, int index) const;
    /// How many elements of a kind the shape has; -1 if it cannot be asked
    virtual int countElements(Kind kind) const;
    /// The kind and number a name means -- "Face3", or a mapped name,
    /// which is asked of the shape. False for a name that is no element's.
    bool resolveElement(const char *element, Kind &kind, int &index) const;
    /// What is drawn. An element nothing is stated of is its kind's look.
    MaterialAppearance getLook(Kind kind, int index) const;
    /// @throw Base::ValueError for a name that is no element's
    MaterialAppearance getLook(const char *element) const;
    void setLook(Kind kind, int index, const MaterialAppearance &look, uint16_t own = OwnAll);
    void setLook(const char *element, const MaterialAppearance &look, uint16_t own = OwnAll);
    /// The colour alone: the rest of the element follows the object
    void setColor(Kind kind, int index, const Color &color);
    void setColor(const char *element, const Color &color);
    /// Which fields the element states; OwnNone if nothing is stated of it
    uint16_t getOwn(Kind kind, int index) const;
    /// Whether anything is stated of the element, by name or by number
    bool isStated(Kind kind, int index) const;
    /// Back to the kind's own look. False if nothing was stated of it.
    bool removeLook(Kind kind, int index);
    bool removeLook(const char *element);
    /// Everything stated, the kinds' own looks included
    void clear();
    bool isEmpty() const;
    /// What is stated of elements, named first and then numbered, each
    /// with the name the shape counts it by
    std::vector<std::pair<std::string, MaterialAppearance>> getStatedLooks() const;
    //@}

    /** @name The Python view of this property
     *
     * getPyObject() hands out an ElementAppearancePy that reads and writes
     * this property. The views register here so that the property's death
     * makes them invalid rather than dangling.
     */
    //@{
    void registerView(ElementAppearancePy *view);
    void unregisterView(ElementAppearancePy *view);
    //@}

    PyObject *getPyObject() override;
    void setPyObject(PyObject *value) override;

    void Save(Base::Writer &writer) const override;
    void Restore(Base::XMLReader &reader) override;

    Property *Copy() const override;
    void Paste(const Property &from) override;
    bool isSame(const Property &other) const override;
    /** Whether another states the same
     *
     * The names with their looks and which of those are their own, the
     * looks by number, the kinds' own looks. Not what is drawn, which is
     * made of those and of more than this property knows, and is no change
     * of its own (docs/ShapeAppearanceDesign.md sec 14.6.5).
     */
    bool isSameStated(const PropertyElementAppearance &other) const;
    /// Whether the value was read from a file: an object out of a file
    /// older than this property has none
    bool wasRestored() const { return _wasRestored; }
    unsigned int getMemSize() const override;

    const char *getEditorName() const override { return ""; }

    void updateElementReference(DocumentObject *feature, bool reverse = false,
                                bool notify = false) override;

    /** @name The content a look names (a texture map, a MaterialX set) */
    //@{
    void collectBlobs(FileBlobManager &manager, const DocumentObject *object) const override;
    void assignRestoredBlob(const FileBlobHandle &blob) override;
    bool blobContentNeedsStore() const override;
    //@}

protected:
    void aboutToSetValue() override;
    void hasSetValue() override;

private:
    /// Where a list is kept: the named looks, then for each kind its own
    /// look, its numbered list and what is drawn of it
    //@{
    static constexpr int SlotNamed = 0;
    static constexpr int SlotBase = 1;
    static constexpr int SlotNumbered = SlotBase + int(KindCount);
    static constexpr int SlotDrawn = SlotNumbered + int(KindCount);
    static constexpr int SlotCount = SlotDrawn + int(KindCount);
    //@}
    /// A list, and the name its file is written under
    struct Held;
    friend class Edit;

    const AppearanceList &listAt(int slot) const;
    /// The list in \a slot to write to, where it is: made if it is not
    /// there. Only inside an Edit, which is what announces the write.
    AppearanceList &editList(int slot);
    /// Keep \a list in \a slot, or nothing where it has no entries.
    /// Announces nothing: the caller is inside an Edit, or the list is a
    /// drawn one. \a hold: take hold of the stored content the list names,
    /// which a copy of a value that holds it already has no need to.
    void assign(int slot, const AppearanceList &list, bool hold = true);
    /// The held lists as they have to be to be written or read: on this
    /// property's container, under its name
    void syncHeld() const;
    /// The lists that came to hold nothing let go
    void pruneHeld();
    /// One look for each name and no more, after the names changed under it
    void conformNames(bool keepHeld = false);
    /// The names as they are being written inside an Edit, or as they are
    const std::vector<std::string> &subs() const;
    /// The names to change, given to the link when the Edit ends -- which
    /// is what looks them up in the shape, once for all of them
    std::vector<std::string> &editSubs();
    void flushSubs();
    void eraseNamed(int pos);
    /// \a list as the kind's own look, the numbered entries put on it
    void assignBase(Kind kind, const AppearanceList &list);
    /// The numbered list of a kind to write to, one entry an element
    AppearanceList &editNumbered(Kind kind, int index);
    /// Put another look under the numbered entries: what an element does
    /// not state follows, what it states stays
    void rebaseNumbered(Kind kind, const MaterialAppearance &from, const MaterialAppearance &to);
    /// The fields \a own states of \a look written to entry \a idx
    static void applyFields(AppearanceList &list, int idx, const MaterialAppearance &look,
                            uint16_t own);
    const Data::ComplexGeoData *geometry() const;
    DocumentObject *owner() const;

    std::array<std::unique_ptr<Held>, SlotCount> _held;
    /// One for each name; empty while every name states all of its look
    std::vector<uint16_t> _own;
    std::unique_ptr<std::vector<std::string>> _pendingSubs;
    /// An element's name, as the shape counts it and as it is mapped, to
    /// its place. Made when asked for.
    mutable std::unique_ptr<std::unordered_map<std::string, int>> _places;
    std::vector<ElementAppearancePy *> _views;
    int _editing {0};
    bool _restoring {false};
    bool _wasRestored {false};
};

}  // namespace App

#endif  // APP_PROPERTYELEMENTAPPEARANCE_H
