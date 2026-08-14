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
    return new MaterialPy(new Material);
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
    static const std::array<const char *, 14> kwds_colors{"DiffuseColor", "AmbientColor", "SpecularColor",
                                                          "EmissiveColor", "Shininess", "Transparency",
                                                          "PBR", "Metallic", "Roughness",
                                                          "Finish", "FinishPitch", "FinishDepth",
                                                          "FinishAngle", nullptr};

    if (!Base::Wrapped_ParseTupleAndKeywords(args, kwds, "|OOOOOOOOOOOOO", kwds_colors,
        &diffuse, &ambient, &specular, &emissive, &shininess, &transparency,
        &pbr, &metallic, &roughness,
        &finish, &finishPitch, &finishDepth, &finishAngle)) {
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

    getMaterialPtr()->set(pstr);

    Py_Return;
}

Py::Tuple MaterialPy::getAmbientColor() const
{
    Py::Tuple tuple(4);
    tuple.setItem(0, Py::Float(getMaterialPtr()->ambientColor.r));
    tuple.setItem(1, Py::Float(getMaterialPtr()->ambientColor.g));
    tuple.setItem(2, Py::Float(getMaterialPtr()->ambientColor.b));
    tuple.setItem(3, Py::Float(getMaterialPtr()->ambientColor.a));
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
    getMaterialPtr()->ambientColor = c;
}

Py::Tuple MaterialPy::getDiffuseColor() const
{
    Py::Tuple tuple(4);
    tuple.setItem(0, Py::Float(getMaterialPtr()->diffuseColor.r));
    tuple.setItem(1, Py::Float(getMaterialPtr()->diffuseColor.g));
    tuple.setItem(2, Py::Float(getMaterialPtr()->diffuseColor.b));
    tuple.setItem(3, Py::Float(getMaterialPtr()->diffuseColor.a));
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
    getMaterialPtr()->diffuseColor = c;
}

Py::Tuple MaterialPy::getEmissiveColor() const
{
    Py::Tuple tuple(4);
    tuple.setItem(0, Py::Float(getMaterialPtr()->emissiveColor.r));
    tuple.setItem(1, Py::Float(getMaterialPtr()->emissiveColor.g));
    tuple.setItem(2, Py::Float(getMaterialPtr()->emissiveColor.b));
    tuple.setItem(3, Py::Float(getMaterialPtr()->emissiveColor.a));
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
    getMaterialPtr()->emissiveColor = c;
}

Py::Tuple MaterialPy::getSpecularColor() const
{
    Py::Tuple tuple(4);
    tuple.setItem(0, Py::Float(getMaterialPtr()->specularColor.r));
    tuple.setItem(1, Py::Float(getMaterialPtr()->specularColor.g));
    tuple.setItem(2, Py::Float(getMaterialPtr()->specularColor.b));
    tuple.setItem(3, Py::Float(getMaterialPtr()->specularColor.a));
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
    getMaterialPtr()->specularColor = c;
}

Py::Float MaterialPy::getShininess() const
{
    return Py::Float(getMaterialPtr()->shininess);
}

void MaterialPy::setShininess(Py::Float arg)
{
    getMaterialPtr()->shininess = arg;
}

Py::Float MaterialPy::getTransparency() const
{
    return Py::Float(getMaterialPtr()->transparency);
}

void MaterialPy::setTransparency(Py::Float arg)
{
    getMaterialPtr()->transparency = arg;
}

Py::Boolean MaterialPy::getPBR() const
{
    return Py::Boolean(getMaterialPtr()->pbr);
}

void MaterialPy::setPBR(Py::Boolean arg)
{
    // Converting, like every other way of editing this value: the surface
    // keeps looking like itself in the other model
    getMaterialPtr()->setPBR(arg);
}

Py::Float MaterialPy::getMetallic() const
{
    return Py::Float(getMaterialPtr()->getMetallic());
}

void MaterialPy::setMetallic(Py::Float arg)
{
    getMaterialPtr()->setMetallic(arg);
}

Py::Float MaterialPy::getRoughness() const
{
    return Py::Float(getMaterialPtr()->getRoughness());
}

void MaterialPy::setRoughness(Py::Float arg)
{
    getMaterialPtr()->setRoughness(arg);
}

Py::String MaterialPy::getFinish() const
{
    return Py::String(SurfaceFinish::patternName(getMaterialPtr()->finish.pattern));
}

void MaterialPy::setFinish(Py::String arg)
{
    SurfaceFinish &finish = getMaterialPtr()->finish;
    finish.pattern = SurfaceFinish::patternFromName(std::string(arg).c_str());
    // Clearing the pattern clears what sized it, so that "no finish" is one
    // state rather than a pattern of None carrying stale numbers
    finish.normalize();
}

// The three size attributes deliberately do NOT clamp: normalize() zeroes
// everything while the pattern is None, so clamping here would wipe a pitch
// written before the pattern it belongs to. The clamp happens where the
// value is stored (PropertyMaterialList) and when a pattern is set.
Py::Float MaterialPy::getFinishPitch() const
{
    return Py::Float(getMaterialPtr()->finish.pitch);
}

void MaterialPy::setFinishPitch(Py::Float arg)
{
    getMaterialPtr()->finish.pitch = static_cast<float>(arg);
}

Py::Float MaterialPy::getFinishDepth() const
{
    return Py::Float(getMaterialPtr()->finish.depth);
}

void MaterialPy::setFinishDepth(Py::Float arg)
{
    getMaterialPtr()->finish.depth = static_cast<float>(arg);
}

Py::Float MaterialPy::getFinishAngle() const
{
    return Py::Float(getMaterialPtr()->finish.angle);
}

void MaterialPy::setFinishAngle(Py::Float arg)
{
    getMaterialPtr()->finish.angle = static_cast<float>(arg);
}

PyObject *MaterialPy::getCustomAttributes(const char* /*attr*/) const
{
    return nullptr;
}

int MaterialPy::setCustomAttributes(const char* /*attr*/, PyObject* /*obj*/)
{
    return 0;
}
