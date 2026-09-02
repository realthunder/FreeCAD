// SPDX-License-Identifier: LGPL-2.1-or-later

/***************************************************************************
 *   Copyright (c) 2023 David Carter <dcarter@david.carter.ca>             *
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

#include <QFile>
#include <QMetaType>
#include <QUuid>



#include <App/Application.h>
#include <App/Document.h>
#include <App/DocumentObject.h>
#include <App/DocumentParams.h>
#include <App/MaterialXDocument.h>
#include <Base/Console.h>
#include <Base/FileInfo.h>
#include <Base/Writer.h>
#include <Gui/MetaTypes.h>

#include "MaterialCards.h"
#include "MaterialLibrary.h"
#include "MaterialManager.h"
#include "MaterialPy.h"
#include "PropertyMaterial.h"

using namespace Materials;

/* TRANSLATOR Material::PropertyMaterial */

TYPESYSTEM_SOURCE(Materials::PropertyMaterial, App::Property)

PropertyMaterial::PropertyMaterial() = default;

PropertyMaterial::~PropertyMaterial()
{
    if (_pendingManager) {
        // Still waiting for content that will now never be delivered: the
        // manager would otherwise hand it to a property that no longer exists.
        _pendingManager->removePendingReferrer(this);
    }
}

App::FileBlobManager& PropertyMaterial::blobManager() const
{
    if (auto container = getContainer()) {
        if (auto doc = container->getOwnerDocument()) {
            return doc->getFileBlobManager();
        }
    }
    return App::FileBlobManager::defaultManager();
}

void PropertyMaterial::setValue(const Material& mat)
{
    aboutToSetValue();
    // A copy taken once, here, and shared const from now on: whoever handed us
    // this card keeps their own, and every referrer of ours shares ours.
    _card = std::make_shared<const Material>(mat);
    _uuid = mat.getUUID();
    _name = mat.getName();
    _unresolved = false;
    // Different content, so whatever was stored is no longer what this
    // property holds. The store keeps the old bytes only while something else
    // still refers to them.
    _blob.reset();
    _hash.clear();
    // The document set has to be IN the store before anything can draw it,
    // which is now, not at save time
    _materialXBlobs.clear();
    holdMaterialXBlobs(false);
    hasSetValue();
}

void PropertyMaterial::setValue(const App::MaterialAppearance& mat)
{
    aboutToSetValue();
    // Copy on write: the card is shared and const, so setting the appearance
    // means a new one rather than an edit anyone else can see.
    auto edited = _card ? std::make_shared<Material>(*_card) : std::make_shared<Material>();
    *edited = mat;
    _card = std::move(edited);
    _blob.reset();
    _hash.clear();
    // The document set has to be IN the store before anything can draw it,
    // which is now, not at save time
    _materialXBlobs.clear();
    holdMaterialXBlobs(false);
    hasSetValue();
}

const Material& PropertyMaterial::getValue() const
{
    // A property that was never assigned anything. Not the default card:
    // "no material" and "the Default material" are different answers, and
    // conflating them is how a lost card goes unnoticed.
    static const Material empty;
    return _card ? *_card : empty;
}

PyObject* PropertyMaterial::getPyObject()
{
    return new MaterialPy(new Material(getValue()));
}

void PropertyMaterial::setPyObject(PyObject* value)
{
    if (PyObject_TypeCheck(value, &(MaterialPy::Type))) {
        setValue(*static_cast<MaterialPy*>(value)->getMaterialPtr());
    }
    else {
        std::string error = std::string("type must be 'Material' not ");
        error += value->ob_type->tp_name;
        throw Base::TypeError(error);
    }
}

const std::string& PropertyMaterial::contentHash() const
{
    // Cached: a save asks twice per property, and canonicalizing a card to
    // hash it is not free when a document holds hundreds of them. An
    // unresolved value keeps the hash the document recorded instead: the
    // placeholder is not the card, and must not be written as if it were.
    if (_hash.empty() && _card && !_unresolved) {
        _hash = _card->getContentHash();
    }
    return _hash;
}

bool PropertyMaterial::storesContent() const
{
    // An unresolved value has no content of its own to store: what it holds
    // is a note of what is missing, and re-saving must leave the document
    // saying what it said.
    if (!_card || _unresolved) {
        return false;
    }
    // A stock card can be left out -- the hash says which card it was, and an
    // installation holding the same library produces the content again
    // (docs/MaterialStorage.md sec 5). That reasoning holds only while the
    // library does not move, and it moved: retuning the default appearance
    // changed the Default card, so documents written before it name a hash no
    // installed card answers to and lose the material outright, since a hash
    // miss does not fall back to the uuid. Carrying it is the default now that
    // the content is stored once per document however many objects share it.
    if (App::DocumentParams::getSaveMaterialCards()) {
        return true;
    }
    return !MaterialCards::preset(contentHash());
}

const App::FileBlobHandle& PropertyMaterial::ensureBlob() const
{
    if (_blob || !_card) {
        return _blob;
    }

    const std::string& hash = contentHash();
    auto& manager = blobManager();
    if (auto existing = manager.find(hash)) {
        // This content is already stored -- another object assigned the same
        // card, or this document was opened from a file holding it. Nothing to
        // write.
        _blob = existing;
        return _blob;
    }

    const std::string path = manager.uniquePath(hash + ".FCMat");
    QFile file(QString::fromUtf8(path.c_str()));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        Base::Console().error("PropertyMaterial: cannot write the card to '%s'\n", path.c_str());
        return _blob;
    }
    const QByteArray canonical = _card->getCanonicalForm().toUtf8();
    const bool written = file.write(canonical) == canonical.size();
    file.close();
    if (!written) {
        Base::Console().error("PropertyMaterial: cannot write the card to '%s'\n", path.c_str());
        Base::FileInfo(path).deleteFile();
        return _blob;
    }

    // adoptFile() hashes the file itself and shares an existing blob when the
    // content is already stored, so the store stays the authority on identity
    // and this hash is only a shortcut past the write above.
    _blob = manager.adoptFile(path.c_str(), ".FCMat");
    return _blob;
}

std::string PropertyMaterial::getContentHash() const
{
    return contentHash();
}

App::BlobReferrer PropertyMaterial::referrer(const App::DocumentObject* object) const
{
    auto referrer = App::FileBlobManager::referrerOf(this, object);
    // The extension is the property's to give: nothing about the content says
    // it is a material card, and a stored card should still open in whatever
    // handles one.
    referrer.ext = ".FCMat";
    return referrer;
}

void PropertyMaterial::collectBlobs(App::FileBlobManager& manager,
                                    const App::DocumentObject* object) const
{
    holdMaterialXBlobs(false);
    noteMaterialXBlobs(manager, object);
    if (!storesContent()) {
        return;
    }
    manager.noteReferenced(ensureBlob(), referrer(object));
}

Material PropertyMaterial::cardForLibrary() const
{
    Material card(*_card);
    if (!card.hasMaterialX()) {
        return card;
    }
    holdMaterialXBlobs(false);
    const auto& hashes = card.getMaterialXHashes();
    auto paths = card.getMaterialXPaths();
    paths.resize(hashes.size());
    for (std::size_t i = 0; i < hashes.size(); ++i) {
        if (!paths[i].empty() || hashes[i].empty()) {
            continue;
        }
        for (const auto& blob : _materialXBlobs) {
            if (blob && blob->hash() == hashes[i] && !blob->path().empty()) {
                paths[i] = blob->path();
                break;
            }
        }
    }
    card.setMaterialXPaths(paths);
    return card;
}

void PropertyMaterial::holdMaterialXBlobs(bool queueMissing) const
{
    if (!_card || _unresolved || !_card->hasMaterialX()) {
        return;
    }
    auto& manager = blobManager();
    auto held = [this](const std::string& hash) {
        for (const auto& blob : _materialXBlobs) {
            if (blob && blob->hash() == hash) {
                return true;
            }
        }
        return false;
    };
    const auto& hashes = _card->getMaterialXHashes();
    const auto& paths = _card->getMaterialXPaths();
    const QStringList names = _card->getMaterialXNames();
    for (std::size_t i = 0; i < hashes.size(); ++i) {
        const std::string& hash = hashes[i];
        if (hash.empty() || held(hash)) {
            continue;
        }
        if (auto blob = manager.find(hash)) {
            _materialXBlobs.push_back(std::move(blob));
            continue;
        }
        if (i < paths.size() && !paths[i].empty()) {
            // A library card: the bytes are on this machine, under the path
            // the loader hashed them at
            const std::string ext = static_cast<int>(i) < names.size()
                ? Base::FileInfo(names[static_cast<int>(i)].toStdString()).extension()
                : std::string();
            try {
                if (auto blob = manager.insertFile(paths[i].c_str(), ext.c_str())) {
                    _materialXBlobs.push_back(std::move(blob));
                    continue;
                }
            }
            catch (const Base::Exception& e) {
                Base::Console().error("PropertyMaterial: cannot store '%s': %s\n",
                                      paths[i].c_str(), e.what());
            }
        }
        if (queueMissing) {
            // A card restored from a document, whose bytes are in the archive
            // being read: the manager hands them over once it reaches them
            auto* self = const_cast<PropertyMaterial*>(this);
            self->_pendingManager = &manager;
            manager.addPendingReferrer(hash, self);
        }
    }
    // The manifest itself, written from what is known -- it is a pure
    // function of names and hashes, so it needs none of the files present
    const App::MaterialXDocument manifest = _card->getMaterialXManifest();
    if (manifest.isSet() && !held(manifest.manifestHash())) {
        if (auto blob = manifest.store(manager)) {
            _materialXBlobs.push_back(std::move(blob));
        }
    }
}

void PropertyMaterial::noteMaterialXBlobs(App::FileBlobManager& manager,
                                          const App::DocumentObject* object) const
{
    if (_materialXBlobs.empty() || !_card) {
        return;
    }
    const App::MaterialXDocument manifest = _card->getMaterialXManifest();
    const std::string manifestHash = manifest.isSet() ? manifest.manifestHash() : std::string();
    for (const auto& blob : _materialXBlobs) {
        if (!blob) {
            continue;
        }
        App::BlobReferrer named = App::FileBlobManager::referrerOf(this, object);
        if (blob->hash() == manifestHash) {
            if (!named.name.empty()) {
                named.name += ".materialx";
            }
            named.ext = ".manifest";
        }
        else {
            // Named after what the document calls the file, so an unpacked
            // project shows brass_color.jpg and not a hash
            for (const auto& file : manifest.files) {
                if (file.hash == blob->hash()) {
                    Base::FileInfo fi(file.name);
                    const std::string stem = fi.fileNamePure();
                    if (!named.name.empty() && !stem.empty()) {
                        named.name += "." + stem;
                    }
                    const std::string ext = fi.extension();
                    named.ext = ext.empty() ? std::string() : "." + ext;
                    break;
                }
            }
        }
        manager.noteReferenced(blob, named);
    }
}

void PropertyMaterial::Save(Base::Writer& writer) const
{
    const std::string uuid = _uuid.toStdString();
    if (writer.getSchemaVersion() >= 5 && _card) {
        // The card is identified by its content. The uuid stays alongside as
        // the relink anchor and as what an upstream reader will find
        // (docs/MaterialStorage.md sec 6), and the name because it is the
        // property's, not the content's.
        //
        // The content itself is written only when this installation is the
        // only place it exists. A stock card is left out: the hash still
        // says exactly which card it was, and every reader that has the
        // library can produce it.
        std::string hash = contentHash();
        if (storesContent()) {
            const auto& blob = ensureBlob();
            if (!blob) {
                // The store would not take it. Fall through to the reference
                // form rather than writing a hash nothing can resolve.
                hash.clear();
            }
            else {
                // Noted again here for the same reason PropertyFileIncluded
                // does: a property written through a path the collect pass
                // does not walk would otherwise lose its content.
                blobManager().noteReferenced(blob, referrer(nullptr));
                holdMaterialXBlobs(false);
                noteMaterialXBlobs(blobManager(), nullptr);
                hash = blob->hash();
            }
        }
        if (!hash.empty()) {
            writer.Stream() << writer.ind() << "<PropertyMaterial hash=\""
                            << encodeAttribute(hash) << "\" uuid=\"" << encodeAttribute(uuid)
                            << "\" name=\"" << encodeAttribute(_name.toStdString()) << "\"/>"
                            << std::endl;
            return;
        }
    }

    // Schema 4 and below, and the empty value: upstream's exact form, so a
    // document written for upstream stays readable there.
    writer.Stream() << writer.ind() << "<PropertyMaterial uuid=\"" << encodeAttribute(uuid)
                    << "\"/>" << std::endl;
}

void PropertyMaterial::assign(const std::shared_ptr<const Material>& card, bool unresolved)
{
    // No aboutToSetValue()/hasSetValue(): this completes the restore of a
    // value the document already had, and touching it here would mark a
    // document modified just by being opened.
    _card = card;
    _hash.clear();
    _unresolved = unresolved;
    // The card's document set: from the store, from the library's files,
    // or from the archive still being read
    _materialXBlobs.clear();
    holdMaterialXBlobs(true);
}

void PropertyMaterial::assignUnresolved(const std::string& hash)
{
    // Everything the document recorded about the card, and nothing invented:
    // the assignment stays visible and can relink if the library turns up
    // later. Reverting to Default here is the failure this design removes.
    auto placeholder = std::make_shared<Material>();
    placeholder->setUUID(_uuid);
    placeholder->setName(_name);
    assign(std::move(placeholder), true);
    // Saving this document again writes the same reference back. Opening a
    // document on the wrong machine must not be what destroys what it says.
    _hash = hash;
}

void PropertyMaterial::Restore(Base::XMLReader& reader)
{
    reader.readElement("PropertyMaterial");
    _uuid = QString::fromUtf8(reader.getAttribute<const char*>("uuid"));
    _name = reader.hasAttribute("name") ? QString::fromUtf8(reader.getAttribute<const char*>("name"))
                                        : QString();
    _card.reset();
    _blob.reset();
    _hash.clear();
    _unresolved = false;
    _materialXBlobs.clear();
    _pendingCardHash.clear();

    const std::string hash =
        reader.hasAttribute("hash") ? reader.getAttribute<const char*>("hash") : std::string();
    if (!hash.empty()) {
        // Case 1: the card is identified by its content, which is
        // authoritative -- the library may hold something else under this
        // uuid by now, and what this document was saved with is what it
        // opens with.
        if (auto card = MaterialCards::find(hash, _uuid, _name)) {
            assign(card, false);
            return;
        }
        // Installed content, which is why the document did not carry it. Also
        // reached by a document that did carry it: one instance then serves
        // every open document rather than one per document.
        if (auto stock = MaterialCards::preset(hash)) {
            assign(MaterialCards::adopt(hash, _uuid, _name, stock), false);
            return;
        }
        // The manager hands the content over: at once if the entry has been
        // read already, otherwise once the archive is drained. Until then the
        // value is the placeholder -- content that never arrives warns and
        // never calls back, and an empty value would say nothing at all.
        assignUnresolved(hash);
        auto& manager = blobManager();
        _pendingManager = &manager;
        _pendingCardHash = hash;
        manager.addPendingReferrer(hash, this);
        return;
    }

    if (_uuid.isEmpty()) {
        return;
    }

    // Case 2: an upstream document, or one of ours written at schema 4. The
    // uuid is all there is, so resolve it against the library as upstream
    // does.
    try {
        auto card = MaterialManager::getManager().getMaterial(_uuid);
        if (card) {
            if (_name.isEmpty()) {
                _name = card->getName();
            }
            // Through the cache, so several objects referring to one library
            // card share one instance here too.
            assign(MaterialCards::adopt(card->getContentHash(), _uuid, _name, card), false);
            return;
        }
    }
    catch (const Base::Exception&) {
    }

    // Case 3: nothing resolves.
    Base::Console().warning("The material '%s' (%s) assigned to %s is not installed, and this "
                            "document does not carry it.\n",
                            _name.isEmpty() ? "?" : _name.toUtf8().constData(),
                            _uuid.toUtf8().constData(),
                            getFullName().c_str());
    assignUnresolved({});
}

bool PropertyMaterial::blobUnavailable()
{
    if (_card && !_unresolved) {
        // The card is here; what is missing is one of its MaterialX files.
        // Nothing stands in for a map, and the card keeps its colour slots.
        return false;
    }
    _pendingManager = nullptr;
    if (_uuid.isEmpty()) {
        return false;
    }
    // What the document said, kept across the assignment below: the stand-in
    // must not become what this property claims to be.
    const std::string recorded = _hash;
    try {
        auto card = MaterialManager::getManager().getMaterial(_uuid);
        if (!card) {
            return false;
        }
        if (_name.isEmpty()) {
            _name = card->getName();
        }
        // Unresolved, deliberately. The card is the library's answer to this
        // uuid, which is the right thing to look at and the wrong thing to
        // claim: contentHash() keeps answering with the recorded hash, so a
        // re-save writes the reference the file carried rather than replacing
        // it with whatever this installation happens to hold.
        assign(MaterialCards::adopt(card->getContentHash(), _uuid, _name, card), true);
        _hash = recorded;
        return true;
    }
    catch (const Base::Exception&) {
    }
    return false;
}

void PropertyMaterial::assignRestoredBlob(const App::FileBlobHandle& blob)
{
    if (blob && _card && !_unresolved && blob->hash() != _pendingCardHash) {
        // Not the card: one of its MaterialX files, asked for by
        // holdMaterialXBlobs() once the card itself had arrived
        for (const auto& held : _materialXBlobs) {
            if (held && held->hash() == blob->hash()) {
                return;
            }
        }
        _materialXBlobs.push_back(blob);
        return;
    }
    _pendingCardHash.clear();
    _blob = blob;
    if (!blob) {
        _pendingManager = nullptr;
        assignUnresolved(_hash);
        return;
    }

    auto card = MaterialCards::load(blob->hash(),
                                    QString::fromUtf8(blob->path().c_str()),
                                    _uuid,
                                    _name);
    if (!card) {
        Base::Console().error("The material stored for %s cannot be read (%s).\n",
                              getFullName().c_str(),
                              blob->hash().c_str());
        assignUnresolved(blob->hash());
        return;
    }
    assign(card, false);
}

std::shared_ptr<Material> PropertyMaterial::libraryCard() const
{
    if (_uuid.isEmpty()) {
        return {};
    }
    try {
        return MaterialManager::getManager().getMaterial(_uuid);
    }
    catch (const Base::Exception&) {
        return {};
    }
}

PropertyMaterial::LibraryStatus PropertyMaterial::libraryStatus() const
{
    if (!_card) {
        return LibraryStatus::NoCard;
    }
    if (_uuid.isEmpty()) {
        return LibraryStatus::Unanchored;
    }
    auto card = libraryCard();
    if (!card) {
        return LibraryStatus::Absent;
    }
    // By content, never by uuid alone: a card someone renamed is the same
    // card, and a card someone edited is not, whatever its uuid says. For a
    // value whose content never arrived, the hash the document recorded is
    // still the honest thing to compare -- what the library holds is not it,
    // and saying so is what offers the relink.
    return card->getContentHash() == contentHash() ? LibraryStatus::Current
                                                   : LibraryStatus::Diverged;
}

const char* PropertyMaterial::statusName(LibraryStatus status)
{
    switch (status) {
        case LibraryStatus::NoCard:
            return "NoCard";
        case LibraryStatus::Unanchored:
            return "Unanchored";
        case LibraryStatus::Absent:
            return "Absent";
        case LibraryStatus::Current:
            return "Current";
        case LibraryStatus::Diverged:
            return "Diverged";
    }
    return "NoCard";
}

bool PropertyMaterial::updateFromLibrary()
{
    if (libraryStatus() != LibraryStatus::Diverged) {
        return false;
    }
    auto card = libraryCard();
    if (!card) {
        return false;
    }

    const std::string hash = card->getContentHash();
    aboutToSetValue();
    // The library's card wholesale, name included: the name is the referrer's
    // to keep only while the two are the same card, and this is the point at
    // which the referrer says it wants the library's.
    _uuid = card->getUUID();
    _name = card->getName();
    // Through the cache, so a hundred objects updated from one library card
    // share the instance the library already holds instead of taking a
    // hundred copies of it.
    _card = MaterialCards::adopt(hash, _uuid, _name, card);
    _unresolved = false;
    _blob.reset();
    _hash.clear();
    hasSetValue();
    return true;
}

bool PropertyMaterial::saveToLibrary()
{
    if (!_card || _unresolved) {
        // Nothing of our own to write. Storing the placeholder would put a
        // note about a missing card into the library under the missing card's
        // uuid, which is worse than refusing.
        return false;
    }
    auto existing = libraryCard();
    if (!existing) {
        return false;
    }
    auto library = existing->getLibrary();
    if (!library || library->isReadOnly()) {
        // Stock cards live in a read only library, so an edited preset has no
        // place to go in place. The caller has to ask where.
        return false;
    }
    const QString filename = existing->getFilename();
    if (filename.isEmpty()) {
        return false;
    }
    const QString path = existing->getDirectory() + QStringLiteral("/") + filename;

    // The writer stamps the placement onto the card it is given, and the
    // shared card must not be reachable from that -- everyone else holding it
    // is entitled to the card they were handed.
    auto card = std::make_shared<Material>(cardForLibrary());
    try {
        MaterialManager::getManager().saveMaterial(library, card, path, true, false, false);
    }
    catch (const Base::Exception& error) {
        Base::Console().error("Cannot save the material '%s' to the library: %s\n",
                              _name.toUtf8().constData(),
                              error.what());
        return false;
    }

    // The library holds different content now, so what was indexed by content
    // is stale -- in particular the preset index, which is what decides
    // whether a document has to carry a card at all.
    MaterialCards::clearPresets();
    return true;
}

const char* PropertyMaterial::getEditorName() const
{
    if (testStatus(MaterialEdit)) {
        return "";  //"Gui::PropertyEditor::PropertyAppearanceItem";
    }
    return "";
}

App::Property* PropertyMaterial::Copy() const
{
    // A refcount increment, not a card: an undo snapshot of five hundred
    // objects sharing one material is five hundred pointers.
    PropertyMaterial* p = new PropertyMaterial();
    p->_card = _card;
    p->_blob = _blob;
    p->_hash = _hash;
    p->_uuid = _uuid;
    p->_name = _name;
    p->_unresolved = _unresolved;
    p->_materialXBlobs = _materialXBlobs;
    return p;
}

void PropertyMaterial::Paste(const App::Property& from)
{
    const auto& other = dynamic_cast<const PropertyMaterial&>(from);
    aboutToSetValue();
    _card = other._card;
    _hash = other._hash;
    _uuid = other._uuid;
    _name = other._name;
    _unresolved = other._unresolved;
    // Not the blob: the value may be arriving from another document, whose
    // store this one's handles must not point into. ensureBlob() puts it in
    // the right store when it is next needed, and holdMaterialXBlobs() the
    // document set.
    _blob.reset();
    _materialXBlobs.clear();
    hasSetValue();
}
