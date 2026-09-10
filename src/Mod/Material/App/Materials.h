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

#pragma once

#include <memory>

#include <QSet>
#include <QString>
#include <QStringList>
#include <QTextStream>

#include <App/Application.h>
#include <Base/Color.h>
#include <App/MaterialAppearance.h>
#include <App/MaterialXDocument.h>
#include <Base/BaseClass.h>

#include <Mod/Material/MaterialGlobal.h>

#include "MaterialValue.h"
#include "Model.h"

namespace Materials
{

class MaterialLibrary;

class MaterialsExport MaterialProperty: public ModelProperty
{
    TYPESYSTEM_HEADER_WITH_OVERRIDE();

public:
    MaterialProperty();
    MaterialProperty(const MaterialProperty& other);
    explicit MaterialProperty(const ModelProperty& other, QString modelUUID);
    explicit MaterialProperty(const std::shared_ptr<MaterialProperty>& other);
    ~MaterialProperty() override = default;

    MaterialValue::ValueType getType() const
    {
        return _valuePtr->getType();
    }

    const QString getModelUUID() const
    {
        return _modelUUID;
    }

    QVariant getValue();
    QVariant getValue() const;
    QList<QVariant> getList()
    {
        return _valuePtr->getList();
    }
    QList<QVariant> getList() const
    {
        return _valuePtr->getList();
    }
    bool isNull() const
    {
        return _valuePtr->isNull();
    }
    bool isEmpty() const
    {
        return _valuePtr->isEmpty();
    }
    std::shared_ptr<MaterialValue> getMaterialValue();
    std::shared_ptr<MaterialValue> getMaterialValue() const;
    QString getString() const;
    QString getYAMLString() const;
    QString getCanonicalYAMLString() const;
    QString getDictionaryString() const;  // Non-localized string
    bool getBoolean() const
    {
        return getValue().toBool();
    }
    int getInt() const
    {
        return getValue().toInt();
    }
    double getFloat() const
    {
        return getValue().toFloat();
    }
    const Base::Quantity& getQuantity() const;
    QString getURL() const
    {
        return getValue().toString();
    }
    Base::Color getColor() const;

    MaterialProperty& getColumn(int column);
    const MaterialProperty& getColumn(int column) const;
    MaterialValue::ValueType getColumnType(int column) const;
    QString getColumnUnits(int column) const;
    QVariant getColumnNull(int column) const;
    const std::vector<MaterialProperty>& getColumns() const
    {
        return _columns;
    }

    void setModelUUID(const QString& uuid);
    void setPropertyType(const QString& type) override;
    void setValue(const QVariant& value);
    void setValue(const QString& value);
    void setValue(const std::shared_ptr<MaterialValue>& value);
    void setString(const QString& value);
    void setString(const std::string& value);
    void setBoolean(bool value);
    void setBoolean(int value);
    void setBoolean(const QString& value);
    void setInt(int value);
    void setInt(const QString& value);
    void setFloat(double value);
    void setFloat(const QString& value);
    void setQuantity(const Base::Quantity& value);
    void setQuantity(double value, const QString& units);
    void setQuantity(const QString& value);
    void setList(const QList<QVariant>& value);
    void setURL(const QString& value);
    void setColor(const Base::Color& value);

    MaterialProperty& operator=(const MaterialProperty& other);
    friend QTextStream& operator<<(QTextStream& output, const MaterialProperty& property);

    bool operator==(const MaterialProperty& other) const;
    bool operator!=(const MaterialProperty& other) const
    {
        return !operator==(other);
    }

    void validate(const MaterialProperty& other) const;

    // Define precision for displaying floating point values
    static int const PRECISION;

protected:
    void setType(const QString& type);
    // void setType(MaterialValue::ValueType type) { _valueType = type; }
    void copyValuePtr(const std::shared_ptr<MaterialValue>& value);

    void addColumn(MaterialProperty& column)
    {
        _columns.push_back(column);
    }

private:
    QString _modelUUID;
    std::shared_ptr<MaterialValue> _valuePtr;
    std::vector<MaterialProperty> _columns;
};

class MaterialsExport Material: public Base::BaseClass
{
    TYPESYSTEM_HEADER_WITH_OVERRIDE();

public:
    enum ModelEdit
    {
        ModelEdit_None,   // No change
        ModelEdit_Alter,  // Existing values are changed
        ModelEdit_Extend  // New values added
    };

    Material();
    Material(const std::shared_ptr<MaterialLibrary>& library,
             const QString& directory,
             const QString& uuid,
             const QString& name);
    Material(const Material& other);
    ~Material() override = default;

    std::shared_ptr<MaterialLibrary> getLibrary() const
    {
        return _library;
    }
    QString getDirectory() const;
    QString getFilename() const;
    QString getFilePath() const;
    QString getUUID() const
    {
        return _uuid;
    }
    QString getName() const
    {
        return _name;
    }
    QString getAuthorAndLicense() const;
    QString getAuthor() const
    {
        return _author;
    }
    QString getLicense() const
    {
        return _license;
    }
    QString getParentUUID() const
    {
        return _parentUuid;
    }
    QString getDescription() const
    {
        return _description;
    }
    QString getURL() const
    {
        return _url;
    }
    QString getReference() const
    {
        return _reference;
    }
    ModelEdit getEditState() const
    {
        return _editState;
    }
    const QSet<QString>& getTags() const
    {
        return _tags;
    }
    const QSet<QString>* getPhysicalModels() const
    {
        return &_physicalUuids;
    }
    const QSet<QString>* getAppearanceModels() const
    {
        return &_appearanceUuids;
    }

    App::MaterialAppearance getMaterialAppearance() const;
    /** @name The shader graph set this card carries
     *
     * Stated by the Shader Graph Rendering appearance model
     * (MaterialXRendering.yml, docs/MaterialStorage.md sec 17): the name of
     * the entry that is the MaterialX graph, what the graph calls each
     * file, and -- in the LIBRARY form -- where each file is under the library's materialx/
     * directory. The card's identity is computed over the files' CONTENT
     * hashes, which the library loader computes off the files and a card
     * restored from a document reads back from its canonical form; the
     * paths are never part of it (17.6).
     */
    //@{
    bool hasMaterialX() const;
    QString getMaterialXShaderGraph() const;
    /// Which surface of the graph the card wears, empty for its first
    /// (docs/MaterialStorage.md sec 17.13)
    QString getMaterialXSurface() const;
    QStringList getMaterialXNames() const;
    QStringList getMaterialXFiles() const;
    /// One per name, in name order; empty where a file could not be hashed
    const std::vector<std::string>& getMaterialXHashes() const
    {
        return _materialXHashes;
    }
    void setMaterialXHashes(const std::vector<std::string>& hashes)
    {
        _materialXHashes = hashes;
    }
    /// One per name: the absolute path each hash was computed from, empty
    /// for a card whose files live in a document's store rather than on disk
    const std::vector<std::string>& getMaterialXPaths() const
    {
        return _materialXPaths;
    }
    /// One per hash, in hash order: where the bytes of each file can be
    /// read from on this machine. How a card that came out of a document's
    /// blob store says where its files are before it is written to a library
    void setMaterialXPaths(const std::vector<std::string>& paths)
    {
        _materialXPaths = paths;
    }
    /** Put the files where the library form keeps them.
     *
     * A card being saved may hold its files anywhere -- where the author
     * picked them, another library, a document's blob store -- while the
     * library form lists them relative to `materialx/` at the library's root
     * (17.1). Copies each file to `<libraryRoot>/materialx/<cardDir>/` and
     * rewrites the file list to match; the names, which are the graph's own
     * and the key the renderers join on, do not move, and neither do the
     * hashes. False when a file could not be read or copied; the list keeps
     * that file's old entry so it still lines up with the names.
     */
    bool placeMaterialXFiles(const QString& libraryRoot, const QString& cardDir);
    /** Resolve the library-form paths against \a libraryRoot/materialx and
     * hash each file. A file that is not there leaves its hash empty and
     * says so once.
     */
    void resolveMaterialXFiles(const QString& libraryRoot);
    /** The manifest naming every file by content (App::MaterialXDocument),
     * whose hash is what an appearance carries. Unset while any file is
     * still unhashed: half a manifest would be a different identity.
     */
    App::MaterialXDocument getMaterialXManifest() const;
    //@}
    /* Render_* view properties this card states, empty for almost every
     * card. Separate from getMaterialAppearance() because App::MaterialAppearance
     * deliberately does not carry the media features; see the type's
     * comment in App/MaterialAppearance.h.
     */
    App::MaterialRenderProperties getRenderProperties() const;

    void setLibrary(const std::shared_ptr<MaterialLibrary>& library)
    {
        _library = library;
    }
    void setDirectory(const QString& directory);
    void setFilename(const QString& filename);
    void setUUID(const QString& uuid)
    {
        _uuid = uuid;
    }
    void setName(const QString& name);
    void setAuthor(const QString& author);
    void setLicense(const QString& license);
    void setParentUUID(const QString& uuid);
    void setDescription(const QString& description);
    void setURL(const QString& url);
    void setReference(const QString& reference);

    void setEditState(ModelEdit newState);
    void setEditStateAlter()
    {
        setEditState(ModelEdit_Alter);
    }
    void setEditStateExtend()
    {
        setEditState(ModelEdit_Extend);
    }
    void setPropertyEditState(const QString& name);
    void setPhysicalEditState(const QString& name);
    void setAppearanceEditState(const QString& name);
    void resetEditState()
    {
        _editState = ModelEdit_None;
    }
    void addTag(const QString& tag);
    void removeTag(const QString& tag);
    bool hasTag(const QString& tag)
    {
        return _tags.contains(tag);
    }
    void addPhysical(const QString& uuid);
    void removePhysical(const QString& uuid);
    void addAppearance(const QString& uuid);
    void removeAppearance(const QString& uuid);
    void clearModels();
    void clearInherited();
    void newUuid();

    void setPhysicalValue(const QString& name, const QString& value);
    void setPhysicalValue(const QString& name, int value);
    void setPhysicalValue(const QString& name, double value);
    void setPhysicalValue(const QString& name, const Base::Quantity& value);
    void setPhysicalValue(const QString& name, const std::shared_ptr<MaterialValue>& value);
    void setPhysicalValue(const QString& name, const std::shared_ptr<QList<QVariant>>& value);
    void setPhysicalValue(const QString& name, const QVariant& value);

    void setAppearanceValue(const QString& name, const QString& value);
    void setAppearanceValue(const QString& name, const std::shared_ptr<MaterialValue>& value);
    void setAppearanceValue(const QString& name, const std::shared_ptr<QList<QVariant>>& value);
    void setAppearanceValue(const QString& name, const QVariant& value);

    void setValue(const QString& name, const QString& value);
    void setValue(const QString& name, const QVariant& value);
    void setValue(const QString& name, const std::shared_ptr<MaterialValue>& value);

    /*
     * Legacy values are thosed contained in old format files that don't fit in the new
     * property format. It should not be used as a catch all for defining a property with
     * no model.
     *
     * These values are transient and will not be saved.
     */
    void setLegacyValue(const QString& name, const QString& value);

    std::shared_ptr<MaterialProperty> getPhysicalProperty(const QString& name);
    std::shared_ptr<MaterialProperty> getPhysicalProperty(const QString& name) const;
    std::shared_ptr<MaterialProperty> getAppearanceProperty(const QString& name);
    std::shared_ptr<MaterialProperty> getAppearanceProperty(const QString& name) const;
    std::shared_ptr<MaterialProperty> getProperty(const QString& name);
    std::shared_ptr<MaterialProperty> getProperty(const QString& name) const;
    QVariant getPhysicalValue(const QString& name) const;
    Base::Quantity getPhysicalQuantity(const QString& name) const;
    QString getPhysicalValueString(const QString& name) const;
    QVariant getAppearanceValue(const QString& name) const;
    Base::Quantity getAppearanceQuantity(const QString& name) const;
    QString getAppearanceValueString(const QString& name) const;
    bool hasPhysicalProperty(const QString& name) const;
    bool hasAppearanceProperty(const QString& name) const;
    bool hasNonLegacyProperty(const QString& name) const;
    bool hasLegacyProperty(const QString& name) const;
    bool hasLegacyProperties() const;
    bool hasPhysicalProperties() const;
    bool hasAppearanceProperties() const;

    // Test if the model is defined, and if values are provided for all properties
    bool hasModel(const QString& uuid) const;
    bool hasPhysicalModel(const QString& uuid) const;
    bool hasAppearanceModel(const QString& uuid) const;
    bool isInherited(const QString& uuid) const;
    bool isModelComplete(const QString& uuid) const
    {
        return isPhysicalModelComplete(uuid) || isAppearanceModelComplete(uuid);
    }
    bool isPhysicalModelComplete(const QString& uuid) const;
    bool isAppearanceModelComplete(const QString& uuid) const;

    std::map<QString, std::shared_ptr<MaterialProperty>>& getPhysicalProperties()
    {
        return _physical;
    }
    const std::map<QString, std::shared_ptr<MaterialProperty>>& getPhysicalProperties() const
    {
        return _physical;
    }
    std::map<QString, std::shared_ptr<MaterialProperty>>& getAppearanceProperties()
    {
        return _appearance;
    }
    const std::map<QString, std::shared_ptr<MaterialProperty>>& getAppearanceProperties() const
    {
        return _appearance;
    }
    std::map<QString, QString>& getLegacyProperties()
    {
        return _legacy;
    }

    QString getModelByName(const QString& name) const;

    bool getDereferenced() const
    {
        return _dereferenced;
    }
    void markDereferenced()
    {
        _dereferenced = true;
    }
    void clearDereferenced()
    {
        _dereferenced = false;
    }
    bool isOldFormat() const
    {
        return _oldFormat;
    }
    void setOldFormat(bool isOld)
    {
        _oldFormat = isOld;
    }

    /*
     * Normalize models by removing any inherited models
     */
    static QStringList normalizeModels(const QStringList& models);

    /*
     * Set or change the base material for the current material, updating the properties as
     * required.
     */
    void updateInheritance(const QString& parent);
    /*
     * Return a list of models that are defined in the parent material but not in this one
     */
    QStringList inheritedMissingModels(const Material& parent) const;
    /*
     * Return a list of models that are defined in this model but not the parent
     */
    QStringList inheritedAddedModels(const Material& parent) const;
    /*
     * Return a list of properties that have different values from the parent material
     */
    void inheritedPropertyDiff(const QString& parent);

    void save(QTextStream& stream, bool overwrite, bool saveAsCopy, bool saveInherited);

    /*
     * Write the card in its canonical form: the exact bytes that are hashed
     * and stored as a document blob (docs/MaterialStorage.md sec 4). It is
     * ordinary .FCMat YAML, readable by MaterialLoader, but nothing outside
     * the card's own models and values reaches it -- no provenance, no
     * library lookup, no preference -- so identical content gives identical
     * bytes on every installation.
     */
    void saveCanonical(QTextStream& stream) const;
    void saveCanonicalMaterialX(QTextStream& stream) const;
    QString getCanonicalForm() const;
    /*
     * SHA-1 of the canonical form, hex, matching what App::FileBlobManager
     * computes for a file holding those bytes.
     */
    std::string getContentHash() const;

    /*
     * Assignment operator
     */
    Material& operator=(const Material& other);

    /*
     * Set the appearance properties
     */
    Material& operator=(const App::MaterialAppearance& other);

    bool operator==(const Material& other) const
    {
        if (&other == this) {
            return true;
        }
        return getTypeId() == other.getTypeId() && _uuid == other._uuid;
    }

    void validate(Material& other) const;

protected:
    void addModel(const QString& uuid);
    static void removeUUID(QSet<QString>& uuidList, const QString& uuid);

    static QVariant
    getValue(const std::map<QString, std::shared_ptr<MaterialProperty>>& propertyList,
             const QString& name);
    static QString
    getValueString(const std::map<QString, std::shared_ptr<MaterialProperty>>& propertyList,
                   const QString& name);

    bool modelChanged(const Material& parent,
                      const Model& model) const;
    bool modelAppearanceChanged(const Material& parent,
                                const Model& model) const;
    void saveGeneral(QTextStream& stream) const;
    void saveInherits(QTextStream& stream) const;
    void saveModels(QTextStream& stream, bool saveInherited) const;
    void saveAppearanceModels(QTextStream& stream, bool saveInherited) const;
    void saveCanonicalGeneral(QTextStream& stream) const;
    void saveCanonicalInherits(QTextStream& stream) const;
    void saveCanonicalModels(QTextStream& stream,
                             const QSet<QString>& modelUuids,
                             const std::map<QString, std::shared_ptr<MaterialProperty>>& properties,
                             const QString& header) const;

private:
    std::shared_ptr<MaterialLibrary> _library;
    QString _directory;
    QString _filename;
    QString _uuid;
    QString _name;
    QString _author;
    QString _license;
    QString _parentUuid;
    QString _description;
    QString _url;
    QString _reference;
    QSet<QString> _tags;
    QSet<QString> _physicalUuids;
    QSet<QString> _appearanceUuids;
    QSet<QString> _allUuids;  // Includes inherited models
    std::map<QString, std::shared_ptr<MaterialProperty>> _physical;
    std::map<QString, std::shared_ptr<MaterialProperty>> _appearance;
    std::map<QString, QString> _legacy;
    std::vector<std::string> _materialXHashes;
    std::vector<std::string> _materialXPaths;
    bool _dereferenced;
    bool _oldFormat;
    ModelEdit _editState;
};

inline QTextStream& operator<<(QTextStream& output, const MaterialProperty& property)
{
    output << MaterialValue::escapeString(property.getName()) << ":" << property.getYAMLString();
    return output;
}

using MaterialTreeNode = FolderTreeNode<Material>;

}  // namespace Materials

Q_DECLARE_METATYPE(Materials::Material*)
Q_DECLARE_METATYPE(std::shared_ptr<Materials::Material>)