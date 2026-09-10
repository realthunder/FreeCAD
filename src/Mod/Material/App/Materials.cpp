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

#include <set>

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMetaType>
#include <QUuid>



#include <App/Application.h>
#include <App/FileBlobManager.h>
#include <Gui/MetaTypes.h>

#include "Materials.h"

#include "MaterialLibrary.h"
#include "MaterialManager.h"
#include "ModelManager.h"
#include "ModelUuids.h"


using namespace Materials;

/* TRANSLATOR Material::Materials */

TYPESYSTEM_SOURCE(Materials::MaterialProperty, Materials::ModelProperty)

MaterialProperty::MaterialProperty()
{
    _valuePtr = std::make_shared<MaterialValue>(MaterialValue::None);
}

MaterialProperty::MaterialProperty(const ModelProperty& other, QString modelUUID)
    : ModelProperty(other)
    , _modelUUID(modelUUID)
    , _valuePtr(nullptr)
{
    setType(getPropertyType());
    auto columns = other.getColumns();
    for (auto& it : columns) {
        MaterialProperty prop(it, modelUUID);
        addColumn(prop);
    }
}

void MaterialProperty::copyValuePtr(const std::shared_ptr<MaterialValue>& value)
{
    if (value->getType() == MaterialValue::Array2D) {
        _valuePtr =
            std::make_shared<Array2D>(*(std::static_pointer_cast<Array2D>(value)));
    }
    else if (value->getType() == MaterialValue::Array3D) {
        _valuePtr =
            std::make_shared<Array3D>(*(std::static_pointer_cast<Array3D>(value)));
    }
    else {
        _valuePtr = std::make_shared<MaterialValue>(*value);
    }
}

MaterialProperty::MaterialProperty(const MaterialProperty& other)
    : ModelProperty(other)
    , _modelUUID(other._modelUUID)
{
    copyValuePtr(other._valuePtr);

    for (auto& it : other._columns) {
        _columns.push_back(it);
    }
}

MaterialProperty::MaterialProperty(const std::shared_ptr<MaterialProperty>& other)
    : MaterialProperty(*other)
{}

void MaterialProperty::setModelUUID(const QString& uuid)
{
    _modelUUID = uuid;
}

QVariant MaterialProperty::getValue()
{
    return _valuePtr->getValue();
}

QVariant MaterialProperty::getValue() const
{
    return _valuePtr->getValue();
}

std::shared_ptr<MaterialValue> MaterialProperty::getMaterialValue()
{
    return _valuePtr;
}

std::shared_ptr<MaterialValue> MaterialProperty::getMaterialValue() const
{
    return _valuePtr;
}

QString MaterialProperty::getString() const
{
    // This method produces a localized string. For a non-localized string use
    // getDictionaryString()
    if (isNull()) {
        return {};
    }
    if (getType() == MaterialValue::Quantity) {
        auto quantity = getValue().value<Base::Quantity>();
        return QString::fromStdString(quantity.getUserString());
    }
    if (getType() == MaterialValue::Float) {
        auto value = getValue();
        if (value.isNull()) {
            return {};
        }
        return QString(QStringLiteral("%L1")).arg(value.toFloat(), 0, 'g', MaterialValue::PRECISION);
    }
    return getValue().toString();
}

QString MaterialProperty::getYAMLString() const
{
    return _valuePtr->getYAMLString();
}

QString MaterialProperty::getCanonicalYAMLString() const
{
    return _valuePtr->getCanonicalYAMLString();
}

Base::Color MaterialProperty::getColor() const
{
    auto colorString = getValue().toString();
    std::stringstream stream(colorString.toStdString());

    // Every channel starts defined. A slot with no value in it is an empty
    // string, the first extraction fails, and a stream in a failed state
    // skips every extraction after it -- so an unset colour used to come
    // out of here as three floats nobody had ever written.
    char c = 0;
    stream >> c;  // read "("
    float red = 0.0F;
    stream >> red;
    stream >> c;  // ","
    float green = 0.0F;
    stream >> green;
    stream >> c;  // ","
    float blue = 0.0F;
    stream >> blue;
    stream >> c;  // ","
    float alpha = 1.0F;
    if (c == ',') {
        stream >> alpha;
    }

    Base::Color color(red, green, blue, alpha);
    return color;
}


QString MaterialProperty::getDictionaryString() const
{
    // This method produces a non-localized string. For a localized string use
    // getString()
    if (isNull()) {
        return {};
    }
    if (getType() == MaterialValue::Quantity) {
        auto quantity = getValue().value<Base::Quantity>();
        auto string = QString(QStringLiteral("%1 %2"))
                          .arg(quantity.getValue(), 0, 'g', MaterialValue::PRECISION)
                          .arg(QString::fromStdString(quantity.getUnit().getString()));
        return string;
    }
    if (getType() == MaterialValue::Float) {
        auto value = getValue();
        if (value.isNull()) {
            return {};
        }
        return QString(QStringLiteral("%1")).arg(value.toFloat(), 0, 'g', MaterialValue::PRECISION);
    }
    return getValue().toString();
}

void MaterialProperty::setPropertyType(const QString& type)
{
    ModelProperty::setPropertyType(type);
    setType(type);
}

void MaterialProperty::setType(const QString& type)
{
    auto mappedType = MaterialValue::mapType(type);
    if (mappedType == MaterialValue::None) {
        throw UnknownValueType();
    }
    if (mappedType == MaterialValue::Array2D) {
        auto arrayPtr = std::make_shared<Array2D>();
        arrayPtr->setColumns(columns());
        _valuePtr = arrayPtr;
    }
    else if (mappedType == MaterialValue::Array3D) {
        auto arrayPtr = std::make_shared<Array3D>();
        // First column is third dimension
        arrayPtr->setColumns(columns() - 1);
        _valuePtr = arrayPtr;
    }
    else {
        _valuePtr = std::make_shared<MaterialValue>(mappedType);
    }
}

MaterialProperty& MaterialProperty::getColumn(int column)
{
    try {
        return _columns.at(column);
    }
    catch (std::out_of_range const&) {
        throw InvalidIndex();
    }
}

const MaterialProperty& MaterialProperty::getColumn(int column) const
{
    try {
        return _columns.at(column);
    }
    catch (std::out_of_range const&) {
        throw InvalidIndex();
    }
}

MaterialValue::ValueType MaterialProperty::getColumnType(int column) const
{
    try {
        return _columns.at(column).getType();
    }
    catch (std::out_of_range const&) {
        throw InvalidIndex();
    }
}

QString MaterialProperty::getColumnUnits(int column) const
{
    try {
        return _columns.at(column).getUnits();
    }
    catch (std::out_of_range const&) {
        throw InvalidIndex();
    }
}

QVariant MaterialProperty::getColumnNull(int column) const
{
    MaterialValue::ValueType valueType = getColumnType(column);

    switch (valueType) {
        case MaterialValue::Quantity: {
            Base::Quantity quant = Base::Quantity(0, getColumnUnits(column).toStdString());
            return QVariant::fromValue(quant);
        }

        case MaterialValue::Float:
        case MaterialValue::Integer:
            return 0;

        default:
            break;
    }

    return QString();
}

void MaterialProperty::setValue(const QVariant& value)
{
    if (_valuePtr->getType() == MaterialValue::Quantity && value.canConvert<Base::Quantity>()) {
        // Ensure the units are set correctly
        auto quantity = value.value<Base::Quantity>();
        if (quantity.isValid()) {
            setQuantity(quantity);
        }
        else {
            // Set a default value with default units
            setValue(QStringLiteral("0"));
        }
    }
    else {
        _valuePtr->setValue(value);
    }
}

void MaterialProperty::setValue(const QString& value)
{
    if (_valuePtr->getType() == MaterialValue::Boolean) {
        setBoolean(value);
    }
    else if (_valuePtr->getType() == MaterialValue::Integer) {
        setInt(value);
    }
    else if (_valuePtr->getType() == MaterialValue::Float) {
        setFloat(value);
    }
    else if (_valuePtr->getType() == MaterialValue::URL) {
        setURL(value);
    }
    else if (_valuePtr->getType() == MaterialValue::Array2D
             || _valuePtr->getType() == MaterialValue::Array3D) {
        // This value can't be directly assigned
    }
    else if (_valuePtr->getType() == MaterialValue::Quantity) {
        try {
            setQuantity(Base::Quantity::parse(value.toStdString()));
        }
        catch (const Base::ParserError& e) {
            Base::Console().log("MaterialProperty::setValue Error '%s' - '%s'\n",
                                e.what(),
                                value.toStdString().c_str());
            // Save as a string
            setString(value);
        }
    }
    else {
        setString(value);
    }
}

void MaterialProperty::setValue(const std::shared_ptr<MaterialValue>& value)
{
    _valuePtr = value;
}

void MaterialProperty::setString(const QString& value)
{
    _valuePtr->setValue(QVariant(value));
}

void MaterialProperty::setString(const std::string& value)
{
    _valuePtr->setValue(QVariant(QString::fromStdString(value)));
}

void MaterialProperty::setBoolean(bool value)
{
    _valuePtr->setValue(QVariant(value));
}

void MaterialProperty::setBoolean(int value)
{
    _valuePtr->setValue(QVariant(value != 0));
}

void MaterialProperty::setBoolean(const QString& value)
{
    bool boolean = false;
    std::string val = value.toStdString();
    if ((val == "true") || (val == "True")) {
        boolean = true;
    }
    else if ((val == "false") || (val == "False")) {
        boolean = false;
    }
    else {
        boolean = (std::stoi(val) != 0);
    }

    setBoolean(boolean);
}

void MaterialProperty::setInt(int value)
{
    _valuePtr->setValue(QVariant(value));
}

void MaterialProperty::setInt(const QString& value)
{
    _valuePtr->setValue(value.toInt());
}

void MaterialProperty::setFloat(double value)
{
    _valuePtr->setValue(QVariant(value));
}

void MaterialProperty::setFloat(const QString& value)
{
    _valuePtr->setValue(QVariant(value.toFloat()));
}

void MaterialProperty::setQuantity(const Base::Quantity& value)
{
    auto quantity = value;
    if (quantity.isDimensionless()) {
        // Assign the default units when none are provided.
        //
        // This needs to be parsed rather than just setting units. Otherwise we get mm->m conversion
        // errors, etc
        quantity = Base::Quantity::parse(quantity.getUserString() + getUnits().toStdString());
    }
    else {
        auto propertyUnit = Base::Quantity::parse(getUnits().toStdString()).getUnit();
        auto units = quantity.getUnit();
        if (propertyUnit != units) {
            throw Base::ValueError("Incompatible material units");
        }
    }
    quantity.setFormat(MaterialValue::getQuantityFormat());
    _valuePtr->setValue(QVariant(QVariant::fromValue(quantity)));
}

void MaterialProperty::setQuantity(double value, const QString& units)
{
    setQuantity(Base::Quantity(value, units.toStdString()));
}

void MaterialProperty::setQuantity(const QString& value)
{
    setQuantity(Base::Quantity::parse(value.toStdString()));
}

void MaterialProperty::setList(const QList<QVariant>& value)
{
    _valuePtr->setList(value);
}

void MaterialProperty::setURL(const QString& value)
{
    _valuePtr->setValue(QVariant(value));
}

void MaterialProperty::setColor(const Base::Color& value)
{
    std::stringstream ss;
    ss << "(" << value.r << ", " << value.g << ", " << value.b << ", " << value.a << ")";
    _valuePtr->setValue(QVariant(QString::fromStdString(ss.str())));
}

MaterialProperty& MaterialProperty::operator=(const MaterialProperty& other)
{
    if (this == &other) {
        return *this;
    }

    ModelProperty::operator=(other);

    _modelUUID = other._modelUUID;
    copyValuePtr(other._valuePtr);

    _columns.clear();
    for (auto& it : other._columns) {
        _columns.push_back(it);
    }

    return *this;
}

bool MaterialProperty::operator==(const MaterialProperty& other) const
{
    if (this == &other) {
        return true;
    }

    if (ModelProperty::operator==(other)) {
        return (*_valuePtr == *other._valuePtr);
    }
    return false;
}

void MaterialProperty::validate(const MaterialProperty& other) const {
    _valuePtr->validate(*other._valuePtr);

    if (_columns.size() != other._columns.size()) {
        throw InvalidProperty("Model property column counts don't match");
    }
    for (size_t i = 0; i < _columns.size(); i++) {
        _columns[i].validate(other._columns[i]);
    }
}

TYPESYSTEM_SOURCE(Materials::Material, Base::BaseClass)

Material::Material()
    : _dereferenced(false)
    , _oldFormat(false)
    , _editState(ModelEdit_None)
{
    // Create an initial UUID
    newUuid();
}

Material::Material(const std::shared_ptr<MaterialLibrary>& library,
                   const QString& directory,
                   const QString& uuid,
                   const QString& name)
    : _library(library)
    , _uuid(uuid)
    , _name(name)
    , _dereferenced(false)
    , _oldFormat(false)
    , _editState(ModelEdit_None)
{
    setDirectory(directory);
}

Material::Material(const Material& other)
    : _library(other._library)
    , _directory(other._directory)
    , _filename(other._filename)
    , _uuid(other._uuid)
    , _name(other._name)
    , _author(other._author)
    , _license(other._license)
    , _parentUuid(other._parentUuid)
    , _description(other._description)
    , _url(other._url)
    , _reference(other._reference)
    , _dereferenced(other._dereferenced)
    , _oldFormat(other._oldFormat)
    , _editState(other._editState)
{
    _materialXHashes = other._materialXHashes;
    _materialXPaths = other._materialXPaths;
    for (auto& it : other._tags) {
        _tags.insert(it);
    }
    for (auto& it : other._physicalUuids) {
        _physicalUuids.insert(it);
    }
    for (auto& it : other._appearanceUuids) {
        _appearanceUuids.insert(it);
    }
    for (auto& it : other._allUuids) {
        _allUuids.insert(it);
    }
    for (auto& it : other._physical) {
        MaterialProperty prop(it.second);
        _physical[it.first] = std::make_shared<MaterialProperty>(prop);
    }
    for (auto& it : other._appearance) {
        MaterialProperty prop(it.second);
        _appearance[it.first] = std::make_shared<MaterialProperty>(prop);
    }
    for (auto& it : other._legacy) {
        _legacy[it.first] = it.second;
    }
}

QString Material::getDirectory() const
{
    return _directory;
}

void Material::setDirectory(const QString& directory)
{
    _directory = directory;
}

QString Material::getFilename() const
{
    return _filename;
}

void Material::setFilename(const QString& filename)
{
    _filename = filename;
}

QString Material::getFilePath() const
{
    return QDir(_directory + QStringLiteral("/") + _filename).absolutePath();
}

QString Material::getAuthorAndLicense() const
{
    QString authorAndLicense;

    // Combine the author and license field for backwards compatibility
    if (!_author.isNull()) {
        authorAndLicense = _author;
        if (!_license.isNull()) {
            authorAndLicense += QStringLiteral(" ") + _license;
        }
    }
    else if (!_license.isNull()) {
        authorAndLicense = _license;
    }

    return _license;
}

void Material::addModel(const QString& uuid)
{
    for (const auto& modelUUID : std::as_const(_allUuids)) {
        if (modelUUID == uuid) {
            return;
        }
    }

    _allUuids << uuid;

    auto& manager = ModelManager::getManager();

    try {
        auto model = manager.getModel(uuid);
        auto inheritance = model->getInheritance();
        for (auto& inherits : inheritance) {
            addModel(inherits);
        }
    }
    catch (ModelNotFound const&) {
    }
}

void Material::clearModels()
{
    _physicalUuids.clear();
    _appearanceUuids.clear();
    _allUuids.clear();
    _physical.clear();
    _appearance.clear();
}

void Material::clearInherited()
{
    _allUuids.clear();

    // Rebuild the UUID lists without the inherited UUIDs
    for (auto& uuid : _physicalUuids) {
        _allUuids << uuid;
    }
    for (auto& uuid : _appearanceUuids) {
        _allUuids << uuid;
    }
}

void Material::setName(const QString& name)
{
    _name = name;
    setEditStateExtend();
}

void Material::setAuthor(const QString& author)
{
    _author = author;
    setEditStateExtend();
}

void Material::setLicense(const QString& license)
{
    _license = license;
    setEditStateExtend();
}

void Material::setParentUUID(const QString& uuid)
{
    _parentUuid = uuid;
    setEditStateExtend();
}

void Material::setDescription(const QString& description)
{
    _description = description;
    setEditStateExtend();
}

void Material::setURL(const QString& url)
{
    _url = url;
    setEditStateExtend();
}

void Material::setReference(const QString& reference)
{
    _reference = reference;
    setEditStateExtend();
}

void Material::setEditState(ModelEdit newState)
{
    if (newState == ModelEdit_Extend) {
        if (_editState != ModelEdit_Alter) {
            _editState = newState;
        }
    }
    else if (newState == ModelEdit_Alter) {
        _editState = newState;
    }
}

void Material::removeUUID(QSet<QString>& uuidList, const QString& uuid)
{
    uuidList.remove(uuid);
}

void Material::addTag(const QString& tag)
{
    auto trimmed = tag.trimmed();
    if (!trimmed.isEmpty()) {
        _tags.insert(trimmed);
    }
}

void Material::removeTag(const QString& tag)
{
    _tags.remove(tag);
}

void Material::addPhysical(const QString& uuid)
{
    if (hasPhysicalModel(uuid)) {
        return;
    }

    auto& manager = ModelManager::getManager();

    try {
        auto model = manager.getModel(uuid);

        auto& inheritance = model->getInheritance();
        for (auto& it : inheritance) {
            // Inherited models may already have the properties, so just
            // remove the uuid
            removeUUID(_physicalUuids, it);
        }

        _physicalUuids.insert(uuid);
        addModel(uuid);
        setEditStateExtend();

        for (auto& it : *model) {
            QString propertyName = it.first;
            if (!hasPhysicalProperty(propertyName)) {
                ModelProperty property = static_cast<ModelProperty>(it.second);

                try {
                    _physical[propertyName] = std::make_shared<MaterialProperty>(property, uuid);
                }
                catch (const UnknownValueType&) {
                    Base::Console().error("Property '%s' has unknown type '%s'. Ignoring\n",
                                          property.getName().toStdString().c_str(),
                                          property.getPropertyType().toStdString().c_str());
                }
            }
        }
    }
    catch (ModelNotFound const&) {
    }
}

void Material::removePhysical(const QString& uuid)
{
    if (!hasPhysicalModel(uuid)) {
        return;
    }

    // If it's an inherited model, do nothing
    if (isInherited(uuid)) {
        return;
    }

    auto& manager = ModelManager::getManager();

    try {
        auto model = manager.getModel(uuid);

        auto& inheritance = model->getInheritance();
        for (auto& it : inheritance) {
            removeUUID(_physicalUuids, it);
            removeUUID(_allUuids, it);
        }
        removeUUID(_physicalUuids, uuid);
        removeUUID(_allUuids, uuid);

        for (auto& it : *model) {
            _physical.erase(it.first);
        }

        setEditStateAlter();
    }
    catch (ModelNotFound const&) {
        Base::Console().log("Physical model not found '%s'\n", uuid.toStdString().c_str());
    }
}

void Material::addAppearance(const QString& uuid)
{
    if (hasAppearanceModel(uuid)) {
        return;
    }

    auto& manager = ModelManager::getManager();

    try {
        auto model = manager.getModel(uuid);

        auto& inheritance = model->getInheritance();
        for (auto& it : inheritance) {
            // Inherited models may already have the properties, so just
            // remove the uuid
            removeUUID(_appearanceUuids, it);
        }

        _appearanceUuids.insert(uuid);
        addModel(uuid);
        setEditStateExtend();

        for (auto& it : *model) {
            QString propertyName = it.first;
            if (!hasAppearanceProperty(propertyName)) {
                ModelProperty property = static_cast<ModelProperty>(it.second);

                _appearance[propertyName] = std::make_shared<MaterialProperty>(property, uuid);
            }
        }
    }
    catch (ModelNotFound const&) {
        Base::Console().log("Appearance model not found '%s'\n", uuid.toStdString().c_str());
    }
}

void Material::removeAppearance(const QString& uuid)
{
    if (!hasAppearanceModel(uuid)) {
        return;
    }

    // If it's an inherited model, do nothing
    if (isInherited(uuid)) {
        return;
    }

    auto& manager = ModelManager::getManager();

    try {
        auto model = manager.getModel(uuid);

        auto& inheritance = model->getInheritance();
        for (auto& it : inheritance) {
            removeUUID(_appearanceUuids, it);
            removeUUID(_allUuids, it);
        }
        removeUUID(_appearanceUuids, uuid);
        removeUUID(_allUuids, uuid);

        for (auto& it : *model) {
            _appearance.erase(it.first);
        }

        setEditStateAlter();
    }
    catch (ModelNotFound const&) {
    }
}

void Material::setPropertyEditState(const QString& name)
{
    try {
        if (hasPhysicalProperty(name)) {
            setPhysicalEditState(name);
        }
        else if (hasAppearanceProperty(name)) {
            setAppearanceEditState(name);
        }
    }
    catch (const PropertyNotFound&) {
    }
}

void Material::setPhysicalEditState(const QString& name)
{
    if (getPhysicalProperty(name)->isNull()) {
        setEditStateExtend();
    }
    else {
        setEditStateAlter();
    }
}

void Material::setAppearanceEditState(const QString& name)
{
    try {
        if (getAppearanceProperty(name)->isNull()) {
            setEditStateExtend();
        }
        else {
            setEditStateAlter();
        }
    }
    catch (const PropertyNotFound&) {
    }
}

void Material::setPhysicalValue(const QString& name, const QString& value)
{
    setPhysicalEditState(name);

    if (hasPhysicalProperty(name)) {
        _physical[name]->setValue(value);  // may not be a string type, conversion may be required
    }
}

void Material::setPhysicalValue(const QString& name, int value)
{
    setPhysicalEditState(name);

    if (hasPhysicalProperty(name)) {
        _physical[name]->setInt(value);
    }
}

void Material::setPhysicalValue(const QString& name, double value)
{
    setPhysicalEditState(name);

    if (hasPhysicalProperty(name)) {
        _physical[name]->setFloat(value);
    }
}

void Material::setPhysicalValue(const QString& name, const Base::Quantity& value)
{
    setPhysicalEditState(name);

    if (hasPhysicalProperty(name)) {
        _physical[name]->setQuantity(value);
    }
}

void Material::setPhysicalValue(const QString& name, const std::shared_ptr<MaterialValue>& value)
{
    setPhysicalEditState(name);

    if (hasPhysicalProperty(name)) {
        _physical[name]->setValue(value);
    }
}

void Material::setPhysicalValue(const QString& name, const std::shared_ptr<QList<QVariant>>& value)
{
    setPhysicalEditState(name);

    if (hasPhysicalProperty(name)) {
        _physical[name]->setList(*value);
    }
}

void Material::setPhysicalValue(const QString& name, const QVariant& value)
{
    setPhysicalEditState(name);

    if (hasPhysicalProperty(name)) {
        _physical[name]->setValue(value);
    }
}

void Material::setAppearanceValue(const QString& name, const QString& value)
{
    setAppearanceEditState(name);

    if (hasAppearanceProperty(name)) {
        _appearance[name]->setValue(value);  // may not be a string type, conversion may be required
    }
}

void Material::setAppearanceValue(const QString& name, const std::shared_ptr<MaterialValue>& value)
{
    setAppearanceEditState(name);

    if (hasAppearanceProperty(name)) {
        _appearance[name]->setValue(value);
    }
}

void Material::setAppearanceValue(const QString& name,
                                  const std::shared_ptr<QList<QVariant>>& value)
{
    setAppearanceEditState(name);

    if (hasAppearanceProperty(name)) {
        _appearance[name]->setList(*value);
    }
}

void Material::setAppearanceValue(const QString& name, const QVariant& value)
{
    setAppearanceEditState(name);

    if (hasAppearanceProperty(name)) {
        _appearance[name]->setValue(value);
    }
}

void Material::setValue(const QString& name, const QString& value)
{
    if (hasPhysicalProperty(name)) {
        setPhysicalValue(name, value);
    }
    else if (hasAppearanceProperty(name)) {
        setAppearanceValue(name, value);
    }
    else {
        throw PropertyNotFound();
    }
}

void Material::setValue(const QString& name, const QVariant& value)
{
    if (hasPhysicalProperty(name)) {
        setPhysicalValue(name, value);
    }
    else if (hasAppearanceProperty(name)) {
        setAppearanceValue(name, value);
    }
    else {
        throw PropertyNotFound();
    }
}

void Material::setValue(const QString& name, const std::shared_ptr<MaterialValue>& value)
{
    if (hasPhysicalProperty(name)) {
        setPhysicalValue(name, value);
    }
    else if (hasAppearanceProperty(name)) {
        setAppearanceValue(name, value);
    }
    else {
        throw PropertyNotFound();
    }
}

void Material::setLegacyValue(const QString& name, const QString& value)
{
    setEditStateAlter();

    _legacy[name] = value;
}

std::shared_ptr<MaterialProperty> Material::getPhysicalProperty(const QString& name)
{
    try {
        return _physical.at(name);
    }
    catch (std::out_of_range const&) {
        throw PropertyNotFound();
    }
}

std::shared_ptr<MaterialProperty> Material::getPhysicalProperty(const QString& name) const
{
    try {
        return _physical.at(name);
    }
    catch (std::out_of_range const&) {
        throw PropertyNotFound();
    }
}

std::shared_ptr<MaterialProperty> Material::getAppearanceProperty(const QString& name)
{
    try {
        return _appearance.at(name);
    }
    catch (std::out_of_range const&) {
        throw PropertyNotFound();
    }
}

std::shared_ptr<MaterialProperty> Material::getAppearanceProperty(const QString& name) const
{
    try {
        return _appearance.at(name);
    }
    catch (std::out_of_range const&) {
        throw PropertyNotFound();
    }
}

std::shared_ptr<MaterialProperty> Material::getProperty(const QString& name)
{
    if (hasPhysicalProperty(name)) {
        return getPhysicalProperty(name);
    }
    if (hasAppearanceProperty(name)) {
        return getAppearanceProperty(name);
    }
    throw PropertyNotFound();
}

std::shared_ptr<MaterialProperty> Material::getProperty(const QString& name) const
{
    if (hasPhysicalProperty(name)) {
        return getPhysicalProperty(name);
    }
    if (hasAppearanceProperty(name)) {
        return getAppearanceProperty(name);
    }
    throw PropertyNotFound();
}

QVariant
Material::getValue(const std::map<QString, std::shared_ptr<MaterialProperty>>& propertyList,
                   const QString& name)
{
    try {
        return propertyList.at(name)->getValue();
    }
    catch (std::out_of_range const&) {
        throw PropertyNotFound();
    }
}

QString
Material::getValueString(const std::map<QString, std::shared_ptr<MaterialProperty>>& propertyList,
                         const QString& name)
{
    try {
        const auto& property = propertyList.at(name);
        if (property->isNull()) {
            return {};
        }
        if (property->getType() == MaterialValue::Quantity) {
            auto value = property->getValue();
            if (value.isNull()) {
                return {};
            }
            return QString::fromStdString(value.value<Base::Quantity>().getUserString());
        }
        if (property->getType() == MaterialValue::Float) {
            auto value = property->getValue();
            if (value.isNull()) {
                return {};
            }
            return QString(QStringLiteral("%L1"))
                .arg(value.toFloat(), 0, 'g', MaterialValue::PRECISION);
        }
        return property->getValue().toString();
    }
    catch (std::out_of_range const&) {
        throw PropertyNotFound();
    }
}

QVariant Material::getPhysicalValue(const QString& name) const
{
    return getValue(_physical, name);
}

Base::Quantity Material::getPhysicalQuantity(const QString& name) const
{
    return getValue(_physical, name).value<Base::Quantity>();
}

QString Material::getPhysicalValueString(const QString& name) const
{
    return getValueString(_physical, name);
}

QVariant Material::getAppearanceValue(const QString& name) const
{
    return getValue(_appearance, name);
}

Base::Quantity Material::getAppearanceQuantity(const QString& name) const
{
    return getValue(_appearance, name).value<Base::Quantity>();
}

QString Material::getAppearanceValueString(const QString& name) const
{
    return getValueString(_appearance, name);
}

bool Material::hasPhysicalProperty(const QString& name) const
{
    return _physical.find(name) != _physical.end();
}

bool Material::hasAppearanceProperty(const QString& name) const
{
    return _appearance.find(name) != _appearance.end();
}

bool Material::hasNonLegacyProperty(const QString& name) const
{
    if (hasPhysicalProperty(name) || hasAppearanceProperty(name)) {
        return true;
    }
    return false;
}

bool Material::hasLegacyProperties() const
{
    return !_legacy.empty();
}

bool Material::hasPhysicalProperties() const
{
    return !_physicalUuids.isEmpty();
}

bool Material::hasAppearanceProperties() const
{
    return !_appearanceUuids.isEmpty();
}

bool Material::isInherited(const QString& uuid) const
{
    if (_physicalUuids.contains(uuid)) {
        return false;
    }
    if (_appearanceUuids.contains(uuid)) {
        return false;
    }

    return _allUuids.contains(uuid);
}

bool Material::hasModel(const QString& uuid) const
{
    return _allUuids.contains(uuid);
}

bool Material::hasPhysicalModel(const QString& uuid) const
{
    if (!hasModel(uuid)) {
        return false;
    }

    auto& manager = ModelManager::getManager();

    try {
        auto model = manager.getModel(uuid);
        if (model->getType() == Model::ModelType_Physical) {
            return true;
        }
    }
    catch (ModelNotFound const&) {
    }

    return false;
}

bool Material::hasAppearanceModel(const QString& uuid) const
{
    if (!hasModel(uuid)) {
        return false;
    }

    auto& manager = ModelManager::getManager();

    try {
        auto model = manager.getModel(uuid);
        if (model->getType() == Model::ModelType_Appearance) {
            return true;
        }
    }
    catch (ModelNotFound const&) {
    }

    return false;
}

bool Material::isPhysicalModelComplete(const QString& uuid) const
{
    if (!hasPhysicalModel(uuid)) {
        return false;
    }

    auto& manager = ModelManager::getManager();

    try {
        auto model = manager.getModel(uuid);
        for (auto& it : *model) {
            QString propertyName = it.first;
            auto property = getPhysicalProperty(propertyName);

            if (property->isNull()) {
                return false;
            }
        }
    }
    catch (ModelNotFound const&) {
        return false;
    }

    return true;
}

bool Material::isAppearanceModelComplete(const QString& uuid) const
{
    if (!hasAppearanceModel(uuid)) {
        return false;
    }

    auto& manager = ModelManager::getManager();

    try {
        auto model = manager.getModel(uuid);
        for (auto& it : *model) {
            QString propertyName = it.first;
            auto property = getAppearanceProperty(propertyName);

            if (property->isNull()) {
                return false;
            }
        }
    }
    catch (ModelNotFound const&) {
        return false;
    }

    return true;
}

namespace
{

// Qt seeds QHash differently in every process, so a QSet hands its contents
// back in a different order on every run. An unsorted write means the same
// card serializes to different bytes each time it is saved: noise in a
// library diff, and no content addressing at all (docs/MaterialStorage.md
// sec 4.2).
QStringList sortedStrings(const QSet<QString>& uuids)
{
    QStringList sorted(uuids.begin(), uuids.end());
    sorted.sort();
    return sorted;
}

}  // namespace

void Material::saveGeneral(QTextStream& stream) const
{
    stream << "General:\n";
    stream << "  UUID: \"" << _uuid << "\"\n";
    stream << "  Name: \"" << MaterialValue::escapeString(_name) << "\"\n";
    if (!_author.isEmpty()) {
        stream << "  Author: \"" << MaterialValue::escapeString(_author) << "\"\n";
    }
    if (!_license.isEmpty()) {
        stream << "  License: \"" << MaterialValue::escapeString(_license) << "\"\n";
    }
    if (!_description.isEmpty()) {
        stream << "  Description: \"" << MaterialValue::escapeString(_description) << "\"\n";
    }
    if (!_url.isEmpty()) {
        stream << "  SourceURL: \"" << MaterialValue::escapeString(_url) << "\"\n";
    }
    if (!_reference.isEmpty()) {
        stream << "  ReferenceSource: \"" << MaterialValue::escapeString(_reference) << "\"\n";
    }
    if (!_tags.isEmpty()) {
        stream << "  Tags:\n";
        for (const auto& tag : sortedStrings(_tags)) {
            stream << "    - \"" << tag << "\"\n";
        }
    }
}

void Material::saveInherits(QTextStream& stream) const
{
    if (!_parentUuid.isEmpty()) {
        try {
            auto material = MaterialManager::getManager().getMaterial(_parentUuid);

            stream << "Inherits:\n";
            stream << "  " << material->getName() << ":\n";
            stream << "    UUID: \"" << _parentUuid << "\"\n";
        }
        catch (const MaterialNotFound&) {
        }
    }
}

bool Material::modelChanged(const Material& parent,
                            const Model& model) const
{
    for (auto& it : model) {
        QString propertyName = it.first;
        auto property = getPhysicalProperty(propertyName);
        try {
            auto parentProperty = parent.getPhysicalProperty(propertyName);

            if (*property != *parentProperty) {
                return true;
            }
        }
        catch (const PropertyNotFound&) {
            return true;
        }
    }

    return false;
}

bool Material::modelAppearanceChanged(const Material& parent,
                                      const Model& model) const
{
    for (auto& it : model) {
        QString propertyName = it.first;
        auto property = getAppearanceProperty(propertyName);
        try {
            auto parentProperty = parent.getAppearanceProperty(propertyName);

            if (*property != *parentProperty) {
                return true;
            }
        }
        catch (const PropertyNotFound&) {
            return true;
        }
    }

    return false;
}

void Material::saveModels(QTextStream& stream, bool saveInherited) const
{
    if (_physical.empty()) {
        return;
    }

    auto& modelManager = ModelManager::getManager();
    auto& materialManager = MaterialManager::getManager();

    bool inherited = saveInherited && (_parentUuid.size() > 0);
    std::shared_ptr<Material> parent;
    if (inherited) {
        try {
            parent = materialManager.getMaterial(_parentUuid);
        }
        catch (const MaterialNotFound&) {
            inherited = false;
        }
    }

    bool headerPrinted = false;
    for (const auto& itm : sortedStrings(_physicalUuids)) {
        auto model = modelManager.getModel(itm);
        if (!inherited || modelChanged(*parent, *model)) {
            if (!headerPrinted) {
                stream << "Models:\n";
                headerPrinted = true;
            }
            stream << "  " << MaterialValue::escapeString(model->getName()) << ":\n";
            stream << "    UUID: \"" << model->getUUID() << "\"\n";
            for (const auto& it : *model) {
                QString propertyName = it.first;
                std::shared_ptr<MaterialProperty> property = getPhysicalProperty(propertyName);
                std::shared_ptr<MaterialProperty> parentProperty;
                try {
                    if (inherited) {
                        parentProperty = parent->getPhysicalProperty(propertyName);
                    }
                }
                catch (const PropertyNotFound&) {
                    Base::Console().log("Material::saveModels Property not found '%s'\n",
                                        propertyName.toStdString().c_str());
                }

                if (!inherited || !parentProperty || (*property != *parentProperty)) {
                    if (!property->isNull()) {
                        stream << "    " << *property << "\n";
                    }
                }
            }
        }
    }
}

void Material::saveAppearanceModels(QTextStream& stream, bool saveInherited) const
{
    if (_appearance.empty()) {
        return;
    }

    auto& modelManager = ModelManager::getManager();
    auto& materialManager = MaterialManager::getManager();

    bool inherited = saveInherited && (_parentUuid.size() > 0);
    std::shared_ptr<Material> parent;
    if (inherited) {
        try {
            parent = materialManager.getMaterial(_parentUuid);
        }
        catch (const MaterialNotFound&) {
            inherited = false;
        }
    }

    bool headerPrinted = false;
    for (const auto& itm : sortedStrings(_appearanceUuids)) {
        auto model = modelManager.getModel(itm);
        if (!inherited || modelAppearanceChanged(*parent, *model)) {
            if (!headerPrinted) {
                stream << "AppearanceModels:\n";
                headerPrinted = true;
            }
            stream << "  " << MaterialValue::escapeString(model->getName()) << ":\n";
            stream << "    UUID: \"" << model->getUUID() << "\"\n";
            for (const auto& it : *model) {
                QString propertyName = it.first;
                std::shared_ptr<MaterialProperty> property = getAppearanceProperty(propertyName);
                std::shared_ptr<MaterialProperty> parentProperty;
                try {
                    if (inherited) {
                        parentProperty = parent->getAppearanceProperty(propertyName);
                    }
                }
                catch (const PropertyNotFound&) {
                }

                if (!inherited || !parentProperty || (*property != *parentProperty)) {
                    if (!property->isNull()) {
                        stream << "    " << *property << "\n";
                    }
                }
            }
        }
    }
}

void Material::newUuid()
{
    _uuid = QUuid::createUuid().toString(QUuid::WithoutBraces);
}

QString Material::getModelByName(const QString& name) const
{
    auto& manager = ModelManager::getManager();

    for (auto& it : _allUuids) {
        try {
            auto model = manager.getModel(it);
            if (model->getName() == name) {
                return it;
            }
        }
        catch (ModelNotFound const&) {
        }
    }

    return {};
}

void Material::save(QTextStream& stream, bool overwrite, bool saveAsCopy, bool saveInherited)
{
    if (saveInherited && !saveAsCopy) {
        // Check to see if we're an original or if we're already in the list of
        // models
        if (MaterialManager::getManager().exists(_uuid) && !overwrite) {
            // Make a new version based on the current
            setParentUUID(_uuid);
        }
    }

    // Prevent self inheritance
    if (_parentUuid == _uuid) {
        _parentUuid = QString();
    }

    if (saveAsCopy) {
        // Save it in the same format as the parent
        if (_parentUuid.isEmpty()) {
            saveInherited = false;
        }
        else {
            saveInherited = true;
        }
    }
    else {
        if (!overwrite) {
            // Creating a new derived model when overwriting sets itself as a
            // parent, that will no longer exist because it's been overwritten
            newUuid();
        }
    }

    stream << "---\n";
    stream << "# File created by " << QString::fromStdString(App::Application::getExecutableName())
           << " " << QString::fromStdString(App::Application::Config()["ExeVersion"])
           << " Revision: " << QString::fromStdString(App::Application::Config()["BuildRevision"])
           << "\n";
    saveGeneral(stream);
    if (saveInherited) {
        saveInherits(stream);
    }
    saveModels(stream, saveInherited);
    saveAppearanceModels(stream, saveInherited);

    setOldFormat(false);
}

// The uuid the canonical form carries in place of the card's own. Identity is
// provenance and travels on the property, not in the hashed bytes, but the
// loader requires a General/UUID entry -- so the slot is filled with the nil
// uuid, which also reads as "this card's identity is not in this file".
static const char* canonicalUuid = "00000000-0000-0000-0000-000000000000";

void Material::saveCanonicalGeneral(QTextStream& stream) const
{
    // Name, uuid, author, license, library, directory and filename are
    // deliberately absent: the same card obtained from two libraries, or
    // renamed, is the same content (docs/MaterialStorage.md sec 4.3). What
    // remains is description text that has nowhere else to travel, so leaving
    // it out would lose it on the round trip through a blob.
    stream << "General:\n";
    stream << "  UUID: \"" << QString::fromLatin1(canonicalUuid) << "\"\n";
    if (!_description.isEmpty()) {
        stream << "  Description: \"" << MaterialValue::escapeString(_description) << "\"\n";
    }
    if (!_url.isEmpty()) {
        stream << "  SourceURL: \"" << MaterialValue::escapeString(_url) << "\"\n";
    }
    if (!_reference.isEmpty()) {
        stream << "  ReferenceSource: \"" << MaterialValue::escapeString(_reference) << "\"\n";
    }
    if (!_tags.isEmpty()) {
        stream << "  Tags:\n";
        for (const auto& tag : sortedStrings(_tags)) {
            stream << "    - \"" << MaterialValue::escapeString(tag) << "\"\n";
        }
    }
}

void Material::saveCanonicalInherits(QTextStream& stream) const
{
    if (_parentUuid.isEmpty()) {
        return;
    }

    // saveInherits() writes the parent's name, which means asking the manager
    // for a card this installation may not have: absent, the whole block is
    // dropped and the bytes differ. The loader reads only the UUID and
    // ignores this key, so the uuid stands in for the name here.
    stream << "Inherits:\n";
    stream << "  " << _parentUuid << ":\n";
    stream << "    UUID: \"" << _parentUuid << "\"\n";
}

void Material::saveCanonicalModels(
    QTextStream& stream,
    const QSet<QString>& modelUuids,
    const std::map<QString, std::shared_ptr<MaterialProperty>>& properties,
    const QString& header) const
{
    // Grouped by the model each property names rather than by asking the
    // model library what a model contains: a card whose model this
    // installation lacks still writes its values, and the bytes do not move
    // when the shipped models do. A model with no values still gets a block,
    // so an empty model survives the round trip.
    std::map<QString, std::vector<std::shared_ptr<MaterialProperty>>> byModel;
    for (const auto& uuid : sortedStrings(modelUuids)) {
        // The MaterialX model is written as its own block, by content and
        // not by the library paths its properties hold (saveCanonicalMaterialX)
        if (uuid == ModelUUIDs::ModelUUID_Rendering_MaterialX) {
            continue;
        }
        byModel[uuid];
    }
    for (const auto& it : properties) {
        auto property = it.second;
        if (!property || property->isNull()) {
            continue;
        }
        auto modelUuid = property->getModelUUID();
        if (modelUuid == ModelUUIDs::ModelUUID_Rendering_MaterialX) {
            continue;
        }
        if (modelUuid.isEmpty()) {
            Base::Console().log("Material::saveCanonicalModels property '%s' names no model. "
                                "Not written\n",
                                it.first.toStdString().c_str());
            continue;
        }
        byModel[modelUuid].push_back(property);
    }

    if (byModel.empty()) {
        return;
    }

    stream << header << ":\n";
    for (const auto& it : byModel) {
        // The key is the model uuid, not its name: the name is a library
        // lookup, and the loader reads the UUID below and ignores the key.
        stream << "  " << it.first << ":\n";
        stream << "    UUID: \"" << it.first << "\"\n";
        for (const auto& property : it.second) {
            stream << "    " << MaterialValue::escapeString(property->getName()) << ":"
                   << property->getCanonicalYAMLString() << "\n";
        }
    }
}

void Material::saveCanonical(QTextStream& stream) const
{
    // No "created by" header line: it carries the writing version, which is
    // exactly the kind of thing that must not change the hash.
    stream << "---\n";
    saveCanonicalGeneral(stream);
    saveCanonicalInherits(stream);
    saveCanonicalModels(stream, _physicalUuids, _physical, QStringLiteral("Models"));
    saveCanonicalModels(stream, _appearanceUuids, _appearance, QStringLiteral("AppearanceModels"));
    saveCanonicalMaterialX(stream);
}

void Material::saveCanonicalMaterialX(QTextStream& stream) const
{
    // The shader graph set by CONTENT: the names the graph uses and the hash
    // of each file, never where a library keeps them (docs/MaterialStorage.md
    // 17.6). A card stored in a document reads this back and nothing else.
    if (!hasMaterialX()) {
        return;
    }
    const QStringList names = getMaterialXNames();
    stream << "MaterialX:\n";
    stream << "  ShaderGraph: \"" << MaterialValue::escapeString(getMaterialXShaderGraph()) << "\"\n";
    // Written only when one is named, so a card that wears the graph's
    // first surface keeps the canonical form -- and the content hash --
    // it had before the key existed (sec 17.13)
    const QString surface = getMaterialXSurface();
    if (!surface.isEmpty()) {
        stream << "  Surface: \"" << MaterialValue::escapeString(surface) << "\"\n";
    }
    stream << "  Names:\n";
    for (const auto& name : names) {
        stream << "    - \"" << MaterialValue::escapeString(name) << "\"\n";
    }
    stream << "  Hashes:\n";
    for (int i = 0; i < names.size(); ++i) {
        const std::string hash =
            i < static_cast<int>(_materialXHashes.size()) ? _materialXHashes[i] : std::string();
        stream << "    - \"" << QString::fromStdString(hash) << "\"\n";
    }
}

bool Material::hasMaterialX() const
{
    return hasAppearanceModel(ModelUUIDs::ModelUUID_Rendering_MaterialX)
        && hasAppearanceProperty(QStringLiteral("MaterialXShaderGraph"))
        && !getAppearanceProperty(QStringLiteral("MaterialXShaderGraph"))->isNull()
        && !getAppearanceProperty(QStringLiteral("MaterialXShaderGraph"))->getString().isEmpty();
}

QString Material::getMaterialXShaderGraph() const
{
    if (!hasAppearanceProperty(QStringLiteral("MaterialXShaderGraph"))) {
        return {};
    }
    return getAppearanceProperty(QStringLiteral("MaterialXShaderGraph"))->getString();
}

QString Material::getMaterialXSurface() const
{
    if (!hasAppearanceProperty(QStringLiteral("MaterialXSurface"))) {
        return {};
    }
    auto property = getAppearanceProperty(QStringLiteral("MaterialXSurface"));
    return property->isNull() ? QString() : property->getString();
}

static QStringList listProperty(const Material& card, const char* name)
{
    QStringList out;
    if (!card.hasAppearanceProperty(QString::fromLatin1(name))) {
        return out;
    }
    auto property = card.getAppearanceProperty(QString::fromLatin1(name));
    if (property->isNull()) {
        return out;
    }
    for (const auto& value : property->getList()) {
        out.push_back(value.toString());
    }
    return out;
}

QStringList Material::getMaterialXNames() const
{
    return listProperty(*this, "MaterialXNames");
}

QStringList Material::getMaterialXFiles() const
{
    return listProperty(*this, "MaterialXFiles");
}

void Material::resolveMaterialXFiles(const QString& libraryRoot)
{
    const QStringList names = getMaterialXNames();
    const QStringList files = getMaterialXFiles();
    _materialXHashes.assign(static_cast<std::size_t>(names.size()), std::string());
    _materialXPaths.assign(static_cast<std::size_t>(names.size()), std::string());
    if (names.isEmpty()) {
        return;
    }
    if (files.size() != names.size()) {
        Base::Console().warning("Material '%s': MaterialX names %d files but lists %d paths\n",
                                _name.toUtf8().constData(),
                                static_cast<int>(names.size()),
                                static_cast<int>(files.size()));
    }
    const QDir root(QDir(libraryRoot).filePath(QStringLiteral("materialx")));
    for (int i = 0; i < names.size() && i < files.size(); ++i) {
        const QString path = QFileInfo(files[i]).isAbsolute() ? files[i] : root.filePath(files[i]);
        const std::string hash = App::FileBlobManager::hashFile(path.toUtf8().constData());
        if (hash.empty()) {
            Base::Console().warning("Material '%s': MaterialX file '%s' is not at '%s'\n",
                                    _name.toUtf8().constData(),
                                    names[i].toUtf8().constData(),
                                    path.toUtf8().constData());
            continue;
        }
        _materialXHashes[static_cast<std::size_t>(i)] = hash;
        _materialXPaths[static_cast<std::size_t>(i)] = path.toUtf8().constData();
    }
}

bool Material::placeMaterialXFiles(const QString& libraryRoot, const QString& cardDir)
{
    if (!hasMaterialX()) {
        return true;
    }
    const QStringList names = getMaterialXNames();
    const auto count = static_cast<std::size_t>(names.size());
    if (_materialXPaths.size() != count || _materialXHashes.size() != count) {
        // Never resolved: a card the editor built from picked files, or
        // one whose list was edited by hand
        resolveMaterialXFiles(libraryRoot);
    }
    const QStringList files = getMaterialXFiles();
    const QDir root(QDir(libraryRoot).filePath(QStringLiteral("materialx")));
    const QDir dest(root.filePath(cardDir));
    auto placed = std::make_shared<QList<QVariant>>();
    std::set<QString> taken;
    bool complete = true;
    for (int i = 0; i < names.size(); ++i) {
        const auto slot = static_cast<std::size_t>(i);
        const QString old = i < files.size() ? files[i] : QString();
        const QString src = QString::fromStdString(_materialXPaths[slot]);
        if (src.isEmpty() || !QFileInfo::exists(src)) {
            Base::Console().warning("Material '%s': shader graph file '%s' has no bytes to save\n",
                                    _name.toUtf8().constData(),
                                    names[i].toUtf8().constData());
            placed->append(old);
            complete = false;
            continue;
        }
        // The stated name's own file name; two files the graph calls the
        // same in different directories get told apart by a suffix
        QString base = QFileInfo(names[i]).fileName();
        if (base.isEmpty()) {
            base = QFileInfo(src).fileName();
        }
        QString name = base;
        for (int n = 1; taken.count(name); ++n) {
            const QFileInfo info(base);
            name = info.completeBaseName() + QStringLiteral("-") + QString::number(n);
            if (!info.suffix().isEmpty()) {
                name += QStringLiteral(".") + info.suffix();
            }
        }
        taken.insert(name);
        const QString dst = dest.filePath(name);
        if (QFileInfo(dst).canonicalFilePath() != QFileInfo(src).canonicalFilePath()) {
            if (!dest.mkpath(QStringLiteral("."))
                || (QFileInfo::exists(dst) && !QFile::remove(dst))
                || !QFile::copy(src, dst)) {
                Base::Console().error("Material '%s': cannot copy '%s' to '%s'\n",
                                      _name.toUtf8().constData(),
                                      src.toUtf8().constData(),
                                      dst.toUtf8().constData());
                placed->append(old);
                complete = false;
                continue;
            }
            // A blob store keeps its files read-only; the library's copy is
            // the author's to replace
            QFile::setPermissions(dst,
                                  QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                      | QFileDevice::ReadGroup | QFileDevice::ReadOther);
        }
        _materialXPaths[slot] = dst.toStdString();
        placed->append(cardDir + QStringLiteral("/") + name);
    }
    setAppearanceValue(QStringLiteral("MaterialXFiles"), placed);
    return complete;
}

App::MaterialXDocument Material::getMaterialXManifest() const
{
    App::MaterialXDocument manifest;
    if (!hasMaterialX()) {
        return manifest;
    }
    const QStringList names = getMaterialXNames();
    if (names.isEmpty() || static_cast<int>(_materialXHashes.size()) != names.size()) {
        return manifest;
    }
    for (int i = 0; i < names.size(); ++i) {
        const std::string& hash = _materialXHashes[static_cast<std::size_t>(i)];
        if (hash.empty()) {
            return App::MaterialXDocument();   // half a manifest is another identity
        }
        manifest.files.push_back({names[i].toStdString(), hash});
    }
    const std::string document = getMaterialXShaderGraph().toStdString();
    if (!manifest.find(document.c_str())) {
        return App::MaterialXDocument();   // the document must be one of its own files
    }
    manifest.document = document;
    manifest.surface = getMaterialXSurface().toStdString();
    return manifest;
}

QString Material::getCanonicalForm() const
{
    QString form;
    QTextStream stream(&form);
    saveCanonical(stream);
    stream.flush();
    return form;
}

std::string Material::getContentHash() const
{
    QCryptographicHash hash(QCryptographicHash::Sha1);
    hash.addData(getCanonicalForm().toUtf8());
    return hash.result().toHex().constData();
}

Material& Material::operator=(const Material& other)
{
    if (this == &other) {
        return *this;
    }

    _library = other._library;
    _directory = other._directory;
    _filename = other._filename;
    _uuid = other._uuid;
    _name = other._name;
    _author = other._author;
    _license = other._license;
    _parentUuid = other._parentUuid;
    _description = other._description;
    _url = other._url;
    _reference = other._reference;
    _dereferenced = other._dereferenced;
    _oldFormat = other._oldFormat;
    _editState = other._editState;
    _materialXHashes = other._materialXHashes;
    _materialXPaths = other._materialXPaths;

    _tags.clear();
    for (auto& it : other._tags) {
        _tags.insert(it);
    }
    _physicalUuids.clear();
    for (auto& it : other._physicalUuids) {
        _physicalUuids.insert(it);
    }
    _appearanceUuids.clear();
    for (auto& it : other._appearanceUuids) {
        _appearanceUuids.insert(it);
    }
    _allUuids.clear();
    for (auto& it : other._allUuids) {
        _allUuids.insert(it);
    }

    // Create copies of the properties rather than modify the originals
    _physical.clear();
    for (auto& it : other._physical) {
        MaterialProperty prop(it.second);
        _physical[it.first] = std::make_shared<MaterialProperty>(prop);
    }
    _appearance.clear();
    for (auto& it : other._appearance) {
        MaterialProperty prop(it.second);
        _appearance[it.first] = std::make_shared<MaterialProperty>(prop);
    }
    _legacy.clear();
    for (auto& it : other._legacy) {
        _legacy[it.first] = it.second;
    }

    return *this;
}

Material& Material::operator=(const App::MaterialAppearance& other)
{
    if (!hasAppearanceModel(ModelUUIDs::ModelUUID_Rendering_Basic)) {
        addAppearance(ModelUUIDs::ModelUUID_Rendering_Basic);
    }

    getAppearanceProperty(QStringLiteral("AmbientColor"))->setColor(other.ambientColor);
    getAppearanceProperty(QStringLiteral("DiffuseColor"))->setColor(other.diffuseColor);
    getAppearanceProperty(QStringLiteral("SpecularColor"))->setColor(other.specularColor);
    getAppearanceProperty(QStringLiteral("EmissiveColor"))->setColor(other.emissiveColor);
    getAppearanceProperty(QStringLiteral("Shininess"))->setFloat(other.shininess);
    getAppearanceProperty(QStringLiteral("Transparency"))->setFloat(other.transparency);

    if (!other.image.empty() || !other.imagePath.empty()) {
        if (!hasAppearanceModel(ModelUUIDs::ModelUUID_Rendering_Texture)) {
            addAppearance(ModelUUIDs::ModelUUID_Rendering_Texture);
        }

        getAppearanceProperty(QStringLiteral("TextureImage"))->setString(other.image);
        getAppearanceProperty(QStringLiteral("TexturePath"))->setString(other.imagePath);
    }

    return *this;
}

/*
 * Normalize models by removing any inherited models
 */
QStringList Material::normalizeModels(const QStringList& models)
{
    QStringList normalized;

    auto& manager = ModelManager::getManager();

    for (auto& uuid : models) {
        auto model = manager.getModel(uuid);

        bool found = false;
        for (auto& childUuid : models) {
            if (uuid != childUuid) {
                auto childModel = manager.getModel(childUuid);
                if (childModel->inherits(childUuid)) {
                    // We're an inherited model
                    found = true;
                    break;
                }
            }
        }
        if (!found) {
            normalized << uuid;
        }
    }

    return normalized;
}

/*
 * Set or change the base material for the current material, updating the
 * properties as required.
 */
void Material::updateInheritance([[maybe_unused]] const QString& parent)
{}

/*
 * Return a list of models that are defined in the parent material but not in
 * this one
 */
QStringList Material::inheritedMissingModels(const Material& parent) const
{
    QStringList missing;
    for (auto& uuid : parent._allUuids) {
        if (!hasModel(uuid)) {
            missing << uuid;
        }
    }

    return normalizeModels(missing);
}

/*
 * Return a list of models that are defined in this model but not the parent
 */
QStringList Material::inheritedAddedModels(const Material& parent) const
{
    QStringList added;
    for (auto& uuid : _allUuids) {
        if (!parent.hasModel(uuid)) {
            added << uuid;
        }
    }

    return normalizeModels(added);
}

/*
 * Return a list of properties that have different values from the parent
 * material
 */
void Material::inheritedPropertyDiff([[maybe_unused]] const QString& parent)
{}

/*
 * Return an App::MaterialAppearance object describing the materials appearance, or DEFAULT if
 * undefined.
 */
App::MaterialAppearance Material::getMaterialAppearance() const
{
    App::MaterialAppearance material(App::MaterialAppearance::DEFAULT);

    bool custom = false;
    // A slot the card's model HAS is not a value the card STATES. The
    // MaterialX model inherits the six colour slots and a look's card
    // fills none of them: it says which shader graph shades the surface
    // and leaves the colours to the Default card. A slot with nothing in
    // it keeps the Default number it started with; the card still counts
    // as an appearance of its own, which is what carries its identity
    // (the uuid below) onto the object wearing it.
    auto colorSlot = [this, &custom](const char* name, App::Color& slot) {
        const QString key = QString::fromLatin1(name);
        if (!hasAppearanceProperty(key)) {
            return;
        }
        custom = true;
        auto property = getAppearanceProperty(key);
        if (!property->isNull()) {
            slot = property->getColor();
        }
    };
    auto floatSlot = [this, &custom](const char* name, float& slot) {
        const QString key = QString::fromLatin1(name);
        if (!hasAppearanceProperty(key)) {
            return;
        }
        custom = true;
        auto property = getAppearanceProperty(key);
        if (!property->isNull()) {
            slot = static_cast<float>(property->getFloat());
        }
    };
    colorSlot("AmbientColor", material.ambientColor);
    colorSlot("DiffuseColor", material.diffuseColor);
    colorSlot("SpecularColor", material.specularColor);
    colorSlot("EmissiveColor", material.emissiveColor);
    floatSlot("Shininess", material.shininess);
    floatSlot("Transparency", material.transparency);
    if (hasAppearanceProperty(QStringLiteral("TextureImage"))) {
        auto property = getAppearanceProperty(QStringLiteral("TextureImage"));
        if (!property->isNull()) {
            Base::Console().log("Has 'TextureImage'\n");
            material.image = property->getString().toStdString();
        }

        custom = true;
    }
    else if (hasAppearanceProperty(QStringLiteral("TexturePath"))) {
        auto property = getAppearanceProperty(QStringLiteral("TexturePath"));
        if (!property->isNull()) {
            Base::Console().log("Has 'TexturePath'\n");
            material.imagePath = property->getString().toStdString();
        }

        custom = true;
    }

    if (hasMaterialX()) {
        // The document set rides the look as ONE hash, the manifest's
        // (docs/MaterialStorage.md 17.8). Unset while a file is missing:
        // a card that cannot be shaded as stated keeps its colour slots.
        const App::MaterialXDocument manifest = getMaterialXManifest();
        if (manifest.isSet()) {
            material.materialx = manifest.manifestHash();
            custom = true;
        }
    }
    if (custom) {
        material.setType(App::MaterialAppearance::USER_DEFINED);
        material.uuid = getUUID().toStdString();
    }

    return material;
}

/*
 * Return the Render_* view properties this card states. Almost every card
 * states none: the glass model is the only one that reaches here today.
 */
App::MaterialRenderProperties Material::getRenderProperties() const
{
    App::MaterialRenderProperties props;

    // A card carrying the glass model IS glass -- there is no separate
    // on/off field, presence is the switch, which is what lets a card be
    // read without knowing the model's schema. The three values keep the
    // Render_* family's sentinel: a value <= 0 means "engine decides", so
    // it is left unstated rather than written as a zero the engine would
    // have to tell apart from a deliberate one.
    if (hasAppearanceProperty(QStringLiteral("GlassIOR"))
            || hasAppearanceProperty(QStringLiteral("GlassDensity"))
            || hasAppearanceProperty(QStringLiteral("GlassRoughness"))) {
        props.push_back({"Render_Glass", true, 1.0});

        static const std::pair<const char*, const char*> glassFloats[] = {
            {"GlassIOR", "Render_GlassIOR"},
            {"GlassDensity", "Render_GlassDensity"},
            {"GlassRoughness", "Render_GlassRoughness"},
        };
        for (const auto& [field, property] : glassFloats) {
            if (!hasAppearanceProperty(QString::fromLatin1(field))) {
                continue;
            }
            double value = getAppearanceProperty(QString::fromLatin1(field))->getFloat();
            if (value > 0.0) {
                props.push_back({property, false, value});
            }
        }
    }

    return props;
}

void Material::validate(Material& other) const
{

    try {
        _library->validate(*other._library);
    }
    catch (const InvalidLibrary& e) {
        throw InvalidMaterial(e.what());
    }

    if (_directory != other._directory) {
        throw InvalidMaterial("Model directories don't match");
    }
    if (!other._filename.isEmpty()) {
        throw InvalidMaterial("Remote filename is not empty");
    }
    if (_uuid != other._uuid) {
        throw InvalidMaterial("Model UUIDs don't match");
    }
    if (_name != other._name) {
        throw InvalidMaterial("Model names don't match");
    }
    if (_author != other._author) {
        throw InvalidMaterial("Model authors don't match");
    }
    if (_license != other._license) {
        throw InvalidMaterial("Model licenses don't match");
    }
    if (_parentUuid != other._parentUuid) {
        throw InvalidMaterial("Model parents don't match");
    }
    if (_description != other._description) {
        throw InvalidMaterial("Model descriptions don't match");
    }
    if (_url != other._url) {
        throw InvalidMaterial("Model URLs don't match");
    }
    if (_reference != other._reference) {
        throw InvalidMaterial("Model references don't match");
    }

    if (_tags.size() != other._tags.size()) {
        Base::Console().log("Local tags count %d\n", _tags.size());
        Base::Console().log("Remote tags count %d\n", other._tags.size());
        throw InvalidMaterial("Material tags counts don't match");
    }
    if (!other._tags.contains(_tags)) {
        throw InvalidMaterial("Material tags don't match");
    }

    if (_physicalUuids.size() != other._physicalUuids.size()) {
        Base::Console().log("Local physical model count %d\n", _physicalUuids.size());
        Base::Console().log("Remote physical model count %d\n", other._physicalUuids.size());
        throw InvalidMaterial("Material physical model counts don't match");
    }
    if (!other._physicalUuids.contains(_physicalUuids)) {
        throw InvalidMaterial("Material physical models don't match");
    }

    if (_physicalUuids.size() != other._physicalUuids.size()) {
        Base::Console().log("Local appearance model count %d\n", _physicalUuids.size());
        Base::Console().log("Remote appearance model count %d\n", other._physicalUuids.size());
        throw InvalidMaterial("Material appearance model counts don't match");
    }
    if (!other._physicalUuids.contains(_physicalUuids)) {
        throw InvalidMaterial("Material appearance models don't match");
    }

    if (_allUuids.size() != other._allUuids.size()) {
        Base::Console().log("Local model count %d\n", _allUuids.size());
        Base::Console().log("Remote model count %d\n", other._allUuids.size());
        throw InvalidMaterial("Material model counts don't match");
    }
    if (!other._allUuids.contains(_allUuids)) {
        throw InvalidMaterial("Material models don't match");
    }

    // Need to compare properties
    if (_physical.size() != other._physical.size()) {
        throw InvalidMaterial("Material physical property counts don't match");
    }
    for (auto& property : _physical) {
        auto& remote = other._physical[property.first];
        property.second->validate(*remote);
    }

    if (_appearance.size() != other._appearance.size()) {
        throw InvalidMaterial("Material appearance property counts don't match");
    }
    for (auto& property : _appearance) {
        auto& remote = other._appearance[property.first];
        property.second->validate(*remote);
    }
}
