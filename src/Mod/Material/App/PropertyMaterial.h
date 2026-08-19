// SPDX-License-Identifier: LGPL-2.1-or-later

/***************************************************************************
 *   Copyright (c) 2023-2024 David Carter <dcarter@david.carter.ca>        *
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

#pragma once

#include <App/FileBlobManager.h>
#include <App/Property.h>
#include <Base/Reader.h>

#include "Materials.h"

namespace App
{
class Material;
}

namespace Materials
{

/** The material card assigned to an object.
 *
 * The card is stored, not referenced: the property holds content-addressed
 * bytes in the document's blob store plus the provenance -- uuid and display
 * name -- that says where those bytes came from
 * (docs/MaterialStorage.md). A document therefore carries the card it uses,
 * and opening it on an installation that has never seen that card still
 * reports the material by name with its values intact, where referencing it
 * by uuid silently reverted to Default.
 *
 * The parsed card is shared and const. Every object assigned the same
 * material holds one instance of it, a copy of the property is a refcount
 * increment, and an edit allocates rather than reaching into a card someone
 * else is reading.
 */
class MaterialsExport PropertyMaterial: public App::Property, public App::BlobReferrerProperty
{
    TYPESYSTEM_HEADER_WITH_OVERRIDE();

public:
    /**
     * A constructor.
     * A more elaborate description of the constructor.
     */
    PropertyMaterial();

    /**
     * A destructor.
     * A more elaborate description of the destructor.
     */
    ~PropertyMaterial() override;

    /** Sets the property
     */
    void setValue(const Material& mat);

    /** Sets the appearance properties
     */
    void setValue(const App::Material& mat);

    /** This method returns a string representation of the property
     */
    const Material& getValue() const;

    PyObject* getPyObject() override;
    void setPyObject(PyObject*) override;

    /// The card's uuid. Provenance: what library card this came from.
    QString getUUID() const
    {
        return _uuid;
    }
    /// The card's display name, which travels with the property, not the
    /// content -- the same card renamed is the same content.
    QString getName() const
    {
        return _name;
    }
    /** Content hash of the card, or an empty string when there is no card.
     *
     * This is what a save writes, whether or not the content travels with
     * the document -- a stock card is identified the same way and simply
     * does not need carrying.
     */
    std::string getContentHash() const;

    /** True when the card could not be resolved on restore.
     *
     * The value is then a placeholder holding the uuid and name the document
     * recorded, and nothing else. It exists so the assignment stays visible
     * and reportable rather than quietly becoming Default.
     */
    bool isUnresolved() const
    {
        return _unresolved;
    }

    void Save(Base::Writer& writer) const override;
    void Restore(Base::XMLReader& reader) override;

    void collectBlobs(App::FileBlobManager& manager,
                      const App::DocumentObject* object) const override;
    void assignRestoredBlob(const App::FileBlobHandle& blob) override;

    const char* getEditorName() const override;

    Property* Copy() const override;
    void Paste(const Property& from) override;

    unsigned int getMemSize() const override
    {
        // The card is shared, so what this property owns is the two handles
        // and its provenance. Reporting the card's size here would count one
        // instance once per referrer.
        return sizeof(*this);
    }

    bool isSame(const Property& other) const override
    {
        if (&other == this) {
            return true;
        }
        if (getTypeId() != other.getTypeId()) {
            return false;
        }
        auto& theirs = *static_cast<decltype(this)>(&other);
        // The shared instance answers most of these outright.
        if (_card && _card == theirs._card) {
            return true;
        }
        return getValue() == theirs.getValue();
    }

private:
    App::FileBlobManager& blobManager() const;
    /// How the stored card is named, and under what extension.
    App::BlobReferrer referrer(const App::DocumentObject* object) const;
    /** The stored form of the current card, minting it if this is the first
     * time anything asked. Lazy because a card is assigned far more often
     * than it is saved, and because the store to write it to is the owning
     * document's -- which a property being filled in during a recompute may
     * not have yet.
     */
    const App::FileBlobHandle& ensureBlob() const;
    /// The card's content hash, computed once and remembered.
    const std::string& contentHash() const;
    /// Whether this document has to carry the card, i.e. it is not installed.
    bool storesContent() const;
    /// Take a card as the value, without touching the document. Restore only.
    void assign(const std::shared_ptr<const Material>& card, bool unresolved);
    /** The placeholder of docs/MaterialStorage.md sec 7 case 3.
     *
     * \a hash is what the document recorded, kept so that saving it again
     * says the same thing rather than storing the placeholder.
     */
    void assignUnresolved(const std::string& hash);

    std::shared_ptr<const Material> _card;
    mutable App::FileBlobHandle _blob;
    mutable std::string _hash;
    QString _uuid;
    QString _name;
    App::FileBlobManager* _pendingManager {nullptr};
    bool _unresolved {false};
};

}  // namespace Materials