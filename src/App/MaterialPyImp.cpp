/***************************************************************************
 *   Copyright (c) 2010 Werner Mayer <wmayer[at]users.sourceforge.net>     *
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


#include "PreCompiled.h"

// inclusion of the generated files (generated out of MaterialPy.xml)
#include "MaterialPy.h"
#include "MaterialPy.cpp"
#include <Base/PyWrapParseTupleAndKeywords.h>

using namespace App;

PyObject *MaterialPy::PyMake(struct _typeobject *, PyObject *, PyObject *)  // Python wrapper
{
    // create a new instance of MaterialPy and the Twin object
    return new MaterialPy(new MaterialAppearance);
}

// constructor method
int MaterialPy::PyInit(PyObject* args, PyObject* kwds)
{
    PyObject* diffuse = nullptr;
    PyObject* ambient = nullptr;
    PyObject* specular = nullptr;
    PyObject* emissive = nullptr;
    PyObject* shininess = nullptr;
    PyObject* transparency = nullptr;
    PyObject* pbr = nullptr;
    PyObject* metallic = nullptr;
    PyObject* roughness = nullptr;
    PyObject* finish = nullptr;
    PyObject* finishPitch = nullptr;
    PyObject* finishDepth = nullptr;
    PyObject* finishAngle = nullptr;
    PyObject* texture = nullptr;
    PyObject* textureScale = nullptr;
    PyObject* textureOffset = nullptr;
    PyObject* textureRotation = nullptr;
    PyObject* image = nullptr;
    PyObject* imagePath = nullptr;
    PyObject* uuid = nullptr;
    PyObject* materialx = nullptr;
    static const std::array<const char *, 22> kwds_colors{"DiffuseColor", "AmbientColor", "SpecularColor",
                                                          "EmissiveColor", "Shininess", "Transparency",
                                                          "PBR", "Metallic", "Roughness",
                                                          "Finish", "FinishPitch", "FinishDepth",
                                                          "FinishAngle", "Texture", "TextureScale",
                                                          "TextureOffset", "TextureRotation",
                                                          "Image", "ImagePath",
                                                          "Uuid", "MaterialX", nullptr};

    if (!Base::Wrapped_ParseTupleAndKeywords(args, kwds, "|OOOOOOOOOOOOOOOOOOOOO", kwds_colors,
        &diffuse, &ambient, &specular, &emissive, &shininess, &transparency,
        &pbr, &metallic, &roughness,
        &finish, &finishPitch, &finishDepth, &finishAngle,
        &texture, &textureScale, &textureOffset, &textureRotation,
        &image, &imagePath, &uuid, &materialx)) {
        return -1;
    }

    try {
        // The mode first, whatever the keyword order: it converts, so the
        // slots the other keywords name have to mean what the caller
        // wrote them for. PBR=True on the fresh default converts it --
        // white tint, metallic 0, the default look's roughness -- rather
        // than inheriting the Phong specular, whose alpha of one would
        // read as full metal.
        if (pbr) {
            setPBR(Py::Boolean(pbr));
        }

        if (diffuse) {
            setDiffuseColor(Py::Tuple(diffuse));
        }

        if (ambient) {
            setAmbientColor(Py::Tuple(ambient));
        }

        if (specular) {
            setSpecularColor(Py::Tuple(specular));
        }

        if (emissive) {
            setEmissiveColor(Py::Tuple(emissive));
        }

        if (shininess) {
            setShininess(Py::Float(shininess));
        }

        if (transparency) {
            setTransparency(Py::Float(transparency));
        }

        if (metallic) {
            setMetallic(Py::Float(metallic));
        }

        if (roughness) {
            setRoughness(Py::Float(roughness));
        }

        // The pattern first, whatever the keyword order, for the reason the
        // size setters do not clamp: a pattern arriving after its numbers
        // would clamp them, and a pattern of None would zero them.
        if (finish) {
            setFinish(Py::String(finish));
        }

        if (finishPitch) {
            setFinishPitch(Py::Float(finishPitch));
        }

        if (finishDepth) {
            setFinishDepth(Py::Float(finishDepth));
        }

        if (finishAngle) {
            setFinishAngle(Py::Float(finishAngle));
        }

        // The maps first, whatever the keyword order, for the reason the
        // pattern goes before its numbers: a transform arriving after them
        // would be zeroed by the normalize an empty map dict performs.
        if (texture) {
            setTexture(Py::Dict(texture));
        }

        if (textureScale) {
            setTextureScale(Py::Tuple(textureScale));
        }

        if (textureOffset) {
            setTextureOffset(Py::Tuple(textureOffset));
        }

        if (textureRotation) {
            setTextureRotation(Py::Float(textureRotation));
        }

        // Carried, not used (see the attribute docs): a material that
        // arrived with one of these keeps it through anything that
        // restates the material through this constructor.
        if (image) {
            setImage(Py::String(image));
        }

        if (imagePath) {
            setImagePath(Py::String(imagePath));
        }

        if (uuid) {
            setUuid(Py::String(uuid));
        }

        if (materialx) {
            setMaterialX(Py::String(materialx));
        }
    }
    catch (Base::Exception& e) {
        e.setPyException();
        return -1;
    }
    catch (const Py::Exception&) {
        return -1;
    }

    return 0;
}

// returns a string which represents the object e.g. when printed in python
std::string MaterialPy::representation() const
{
    return {"<Material object>"};
}

PyObject* MaterialPy::set(PyObject * args)
{
    char *pstr;
    if (!PyArg_ParseTuple(args, "s", &pstr))
        return nullptr;

    getMaterialAppearancePtr()->set(pstr);

    Py_Return;
}

Py::Tuple MaterialPy::getAmbientColor() const
{
    Py::Tuple tuple(4);
    tuple.setItem(0, Py::Float(getMaterialAppearancePtr()->ambientColor.r));
    tuple.setItem(1, Py::Float(getMaterialAppearancePtr()->ambientColor.g));
    tuple.setItem(2, Py::Float(getMaterialAppearancePtr()->ambientColor.b));
    tuple.setItem(3, Py::Float(getMaterialAppearancePtr()->ambientColor.a));
    return tuple;
}

void MaterialPy::setAmbientColor(Py::Tuple arg)
{
    Color c;
    c.r = Py::Float(arg.getItem(0));
    c.g = Py::Float(arg.getItem(1));
    c.b = Py::Float(arg.getItem(2));
    if (arg.size() == 4)
    c.a = Py::Float(arg.getItem(3));
    getMaterialAppearancePtr()->ambientColor = c;
}

Py::Tuple MaterialPy::getDiffuseColor() const
{
    Py::Tuple tuple(4);
    tuple.setItem(0, Py::Float(getMaterialAppearancePtr()->diffuseColor.r));
    tuple.setItem(1, Py::Float(getMaterialAppearancePtr()->diffuseColor.g));
    tuple.setItem(2, Py::Float(getMaterialAppearancePtr()->diffuseColor.b));
    tuple.setItem(3, Py::Float(getMaterialAppearancePtr()->diffuseColor.a));
    return tuple;
}

void MaterialPy::setDiffuseColor(Py::Tuple arg)
{
    Color c;
    c.r = Py::Float(arg.getItem(0));
    c.g = Py::Float(arg.getItem(1));
    c.b = Py::Float(arg.getItem(2));
    if (arg.size() == 4)
    c.a = Py::Float(arg.getItem(3));
    getMaterialAppearancePtr()->diffuseColor = c;
}

Py::Tuple MaterialPy::getEmissiveColor() const
{
    Py::Tuple tuple(4);
    tuple.setItem(0, Py::Float(getMaterialAppearancePtr()->emissiveColor.r));
    tuple.setItem(1, Py::Float(getMaterialAppearancePtr()->emissiveColor.g));
    tuple.setItem(2, Py::Float(getMaterialAppearancePtr()->emissiveColor.b));
    tuple.setItem(3, Py::Float(getMaterialAppearancePtr()->emissiveColor.a));
    return tuple;
}

void MaterialPy::setEmissiveColor(Py::Tuple arg)
{
    Color c;
    c.r = Py::Float(arg.getItem(0));
    c.g = Py::Float(arg.getItem(1));
    c.b = Py::Float(arg.getItem(2));
    if (arg.size() == 4)
    c.a = Py::Float(arg.getItem(3));
    getMaterialAppearancePtr()->emissiveColor = c;
}

Py::Tuple MaterialPy::getSpecularColor() const
{
    Py::Tuple tuple(4);
    tuple.setItem(0, Py::Float(getMaterialAppearancePtr()->specularColor.r));
    tuple.setItem(1, Py::Float(getMaterialAppearancePtr()->specularColor.g));
    tuple.setItem(2, Py::Float(getMaterialAppearancePtr()->specularColor.b));
    tuple.setItem(3, Py::Float(getMaterialAppearancePtr()->specularColor.a));
    return tuple;
}

void MaterialPy::setSpecularColor(Py::Tuple arg)
{
    Color c;
    c.r = Py::Float(arg.getItem(0));
    c.g = Py::Float(arg.getItem(1));
    c.b = Py::Float(arg.getItem(2));
    if (arg.size() == 4)
    c.a = Py::Float(arg.getItem(3));
    getMaterialAppearancePtr()->specularColor = c;
}

Py::Float MaterialPy::getShininess() const
{
    return Py::Float(getMaterialAppearancePtr()->shininess);
}

void MaterialPy::setShininess(Py::Float arg)
{
    getMaterialAppearancePtr()->shininess = arg;
}

Py::Float MaterialPy::getTransparency() const
{
    return Py::Float(getMaterialAppearancePtr()->transparency);
}

void MaterialPy::setTransparency(Py::Float arg)
{
    getMaterialAppearancePtr()->transparency = arg;
}

Py::Boolean MaterialPy::getPBR() const
{
    return Py::Boolean(getMaterialAppearancePtr()->pbr);
}

void MaterialPy::setPBR(Py::Boolean arg)
{
    // Converting, like every other way of editing this value: the surface
    // keeps looking like itself in the other model
    getMaterialAppearancePtr()->setPBR(arg);
}

Py::Float MaterialPy::getMetallic() const
{
    return Py::Float(getMaterialAppearancePtr()->getMetallic());
}

void MaterialPy::setMetallic(Py::Float arg)
{
    getMaterialAppearancePtr()->setMetallic(arg);
}

Py::Float MaterialPy::getRoughness() const
{
    return Py::Float(getMaterialAppearancePtr()->getRoughness());
}

void MaterialPy::setRoughness(Py::Float arg)
{
    getMaterialAppearancePtr()->setRoughness(arg);
}

Py::String MaterialPy::getFinish() const
{
    return Py::String(SurfaceFinish::patternName(getMaterialAppearancePtr()->finish.pattern));
}

void MaterialPy::setFinish(Py::String arg)
{
    SurfaceFinish &finish = getMaterialAppearancePtr()->finish;
    finish.pattern = SurfaceFinish::patternFromName(std::string(arg).c_str());
    // Clearing the pattern clears what sized it, so that "no finish" is one
    // state rather than a pattern of None carrying stale numbers
    finish.normalize();
}

// The three size attributes deliberately do NOT clamp: normalize() zeroes
// everything while the pattern is None, so clamping here would wipe a pitch
// written before the pattern it belongs to. The clamp happens where the
// value is stored (PropertyAppearanceList) and when a pattern is set.
Py::Float MaterialPy::getFinishPitch() const
{
    return Py::Float(getMaterialAppearancePtr()->finish.pitch);
}

void MaterialPy::setFinishPitch(Py::Float arg)
{
    getMaterialAppearancePtr()->finish.pitch = static_cast<float>(arg);
}

Py::Float MaterialPy::getFinishDepth() const
{
    return Py::Float(getMaterialAppearancePtr()->finish.depth);
}

void MaterialPy::setFinishDepth(Py::Float arg)
{
    getMaterialAppearancePtr()->finish.depth = static_cast<float>(arg);
}

Py::Float MaterialPy::getFinishAngle() const
{
    return Py::Float(getMaterialAppearancePtr()->finish.angle);
}

void MaterialPy::setFinishAngle(Py::Float arg)
{
    getMaterialAppearancePtr()->finish.angle = static_cast<float>(arg);
}

Py::Dict MaterialPy::getTexture() const
{
    // A slot the material does not state is absent rather than empty, so a
    // caller can test it with `in` and a round trip through this dict says
    // exactly what the material said
    Py::Dict dict;
    const SurfaceTexture &texture = getMaterialAppearancePtr()->texture;
    for (uint8_t slot = 0; slot < SurfaceTexture::SlotCount; ++slot) {
        if (!texture.maps[slot].empty()) {
            dict.setItem(SurfaceTexture::slotName(slot), Py::String(texture.maps[slot]));
        }
    }
    return dict;
}

void MaterialPy::setTexture(Py::Dict arg)
{
    // Every slot at once: an absent key clears its slot rather than leaving
    // whatever was there, so assigning a dict states the whole texture
    SurfaceTexture texture = getMaterialAppearancePtr()->texture;
    for (auto &hash : texture.maps) {
        hash.clear();
    }
    for (const auto &item : arg) {
        const std::string name = Py::String(item.first).as_std_string("utf-8");
        const uint8_t slot = SurfaceTexture::slotFromName(name.c_str());
        if (slot >= SurfaceTexture::SlotCount) {
            throw Py::ValueError("'" + name + "' is not a texture slot");
        }
        texture.maps[slot] = Py::String(item.second).as_std_string("utf-8");
    }
    // Clearing the last slot clears what positioned it, so that "no texture"
    // is one state rather than a transform with nothing to transform
    texture.normalize();
    getMaterialAppearancePtr()->texture = texture;
}

// The transform attributes deliberately do NOT clamp, for the reason the
// finish size attributes do not: normalize() zeroes everything while no
// slot is occupied, so clamping here would wipe a scale written before the
// map it belongs to. The clamp happens where the value is stored.
Py::Tuple MaterialPy::getTextureScale() const
{
    const SurfaceTexture &texture = getMaterialAppearancePtr()->texture;
    Py::Tuple value(2);
    value.setItem(0, Py::Float(texture.scale[0]));
    value.setItem(1, Py::Float(texture.scale[1]));
    return value;
}

void MaterialPy::setTextureScale(Py::Tuple arg)
{
    if (arg.size() != 2) {
        throw Py::ValueError("a texture scale is (u, v)");
    }
    SurfaceTexture &texture = getMaterialAppearancePtr()->texture;
    texture.scale[0] = static_cast<float>(Py::Float(arg[0]));
    texture.scale[1] = static_cast<float>(Py::Float(arg[1]));
}

Py::Tuple MaterialPy::getTextureOffset() const
{
    const SurfaceTexture &texture = getMaterialAppearancePtr()->texture;
    Py::Tuple value(2);
    value.setItem(0, Py::Float(texture.offset[0]));
    value.setItem(1, Py::Float(texture.offset[1]));
    return value;
}

void MaterialPy::setTextureOffset(Py::Tuple arg)
{
    if (arg.size() != 2) {
        throw Py::ValueError("a texture offset is (u, v)");
    }
    SurfaceTexture &texture = getMaterialAppearancePtr()->texture;
    texture.offset[0] = static_cast<float>(Py::Float(arg[0]));
    texture.offset[1] = static_cast<float>(Py::Float(arg[1]));
}

Py::Float MaterialPy::getTextureRotation() const
{
    return Py::Float(getMaterialAppearancePtr()->texture.rotation);
}

void MaterialPy::setTextureRotation(Py::Float arg)
{
    getMaterialAppearancePtr()->texture.rotation = static_cast<float>(arg);
}

Py::String MaterialPy::getImage() const
{
    return Py::String(getMaterialAppearancePtr()->image);
}

void MaterialPy::setImage(Py::String arg)
{
    getMaterialAppearancePtr()->image = static_cast<std::string>(arg);
}

Py::String MaterialPy::getImagePath() const
{
    return Py::String(getMaterialAppearancePtr()->imagePath);
}

void MaterialPy::setImagePath(Py::String arg)
{
    getMaterialAppearancePtr()->imagePath = static_cast<std::string>(arg);
}

Py::String MaterialPy::getUuid() const
{
    return Py::String(getMaterialAppearancePtr()->uuid);
}

Py::String MaterialPy::getMaterialX() const
{
    return Py::String(getMaterialAppearancePtr()->materialx);
}

void MaterialPy::setMaterialX(Py::String arg)
{
    getMaterialAppearancePtr()->materialx = static_cast<std::string>(arg);
}

void MaterialPy::setUuid(Py::String arg)
{
    getMaterialAppearancePtr()->uuid = static_cast<std::string>(arg);
}

PyObject *MaterialPy::getCustomAttributes(const char* /*attr*/) const
{
    return nullptr;
}

int MaterialPy::setCustomAttributes(const char* attr, PyObject* obj)
{
    // A colour written as one packed RGBA integer.
    //
    // App::PropertyColor has always accepted that spelling
    // (PropertyColor::setPyObject), and code assigns colours that way:
    // Draft's layer view provider does
    //
    //     material.DiffuseColor = params.get_param_view("DefaultShapeColor") | 0x000000FF
    //
    // The generated setters here declare Py::Tuple, so an integer never
    // reaches them -- PyCXX fails building the tuple first, with
    // "Error creating object of type N2Py7SeqBaseINS_6ObjectEEE from
    // 3435973887", which names neither the attribute nor the real
    // problem. That one line took a whole IFC import down, since
    // creating a layer creates its view provider.
    //
    // _setattr consults this hook before the generated setters, so
    // accepting the integer here leaves the tuple path exactly as it
    // was: anything that is not an int returns 0 and falls through.
    if (!PyLong_Check(obj)) {
        return 0;
    }

    Color* target = nullptr;
    MaterialAppearance* material = getMaterialAppearancePtr();
    if (strcmp(attr, "DiffuseColor") == 0) {
        target = &material->diffuseColor;
    }
    else if (strcmp(attr, "AmbientColor") == 0) {
        target = &material->ambientColor;
    }
    else if (strcmp(attr, "SpecularColor") == 0) {
        target = &material->specularColor;
    }
    else if (strcmp(attr, "EmissiveColor") == 0) {
        target = &material->emissiveColor;
    }
    if (!target) {
        return 0;
    }

    unsigned long packed = PyLong_AsUnsignedLong(obj);
    if (PyErr_Occurred()) {
        return -1;
    }
    target->setPackedValue(static_cast<uint32_t>(packed));
    return 1;
}
