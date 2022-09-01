/****************************************************************************
 *   Copyright (c) 2021 Zheng Lei (realthunder) <realthunder.dev@gmail.com> *
 *                                                                          *
 *   This file is part of the FreeCAD CAx development system.               *
 *                                                                          *
 *   This library is free software; you can redistribute it and/or          *
 *   modify it under the terms of the GNU Library General Public            *
 *   License as published by the Free Software Foundation; either           *
 *   version 2 of the License, or (at your option) any later version.       *
 *                                                                          *
 *   This library  is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of         *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the          *
 *   GNU Library General Public License for more details.                   *
 *                                                                          *
 *   You should have received a copy of the GNU Library General Public      *
 *   License along with this library; see the file COPYING.LIB. If not,     *
 *   write to the Free Software Foundation, Inc., 59 Temple Place,          *
 *   Suite 330, Boston, MA  02111-1307, USA                                 *
 *                                                                          *
 ****************************************************************************/

#include "PreCompiled.h"

#ifndef _PreComp_
# include <sstream>
#endif

#include <boost/algorithm/string/predicate.hpp>
#include "Gui/ViewProviderSavedView.h"
// inclusion of the generated files (generated out of ViewProviderSavedViewPy.xml)
#include "ViewProviderSavedViewPy.h"
#include "ViewProviderSavedViewPy.cpp"

using namespace Gui;

// returns a string which represents the object e.g. when printed in python
std::string ViewProviderSavedViewPy::representation(void) const
{
    std::stringstream str;
    str << "<ViewProviderSavedView at " << getViewProviderSavedViewPtr() << ">";

    return str.str();
}

static bool getOptions(PyObject *tuple, ViewProviderSavedView::CaptureOptions &options)
{
    if (PyArg_ParseTuple(tuple, ""))
        options = ViewProviderSavedView::CaptureOption::Default;
    else {
        PyErr_Clear();
        std::vector<std::string> opts;
        const char *opt;
        if (PyArg_ParseTuple(tuple, "s", &opt))
            opts.push_back(opt);
        else {
            PyErr_Clear();
            PyObject *args;
            if (!PyArg_ParseTuple(tuple, "O", &args))
                return false;
            if (!PySequence_Check(args)) {
                PyErr_SetString(PyExc_TypeError, "Expects argument of a string or sequence of string");
                return false;
            }
            Py::Sequence seq(args);
            for (auto it = seq.begin(); it != seq.end(); ++it) {
                PyObject* item = (*it).ptr();
                if (PyUnicode_Check(item)) {
                    opts.push_back(PyUnicode_AsUTF8(item));
                }else{
                    PyErr_SetString(PyExc_TypeError, "non-string object in sequence");
                    return false;
                }
            }
        }
        for (auto &opt : opts) {
            if (boost::iequals(opt, "visibilities"))
                options |= ViewProviderSavedView::CaptureOption::Visibilities;
            else if (boost::iequals(opt, "showontop"))
                options |= ViewProviderSavedView::CaptureOption::ShowOnTop;
            else if (boost::iequals(opt, "camera"))
                options |= ViewProviderSavedView::CaptureOption::Camera;
            else if (boost::iequals(opt, "shadow"))
                options |= ViewProviderSavedView::CaptureOption::Shadow;
            else if (boost::iequals(opt, "clippings"))
                options |= ViewProviderSavedView::CaptureOption::Clippings;
            else if (boost::iequals(opt, "all"))
                options |= ViewProviderSavedView::CaptureOption::All;
            else {
                PyErr_SetString(PyExc_ValueError, "Unknown option");
                return false;
            }
        }
    }
    return true;
}

PyObject *ViewProviderSavedViewPy::capture(PyObject *tuple)
{
    ViewProviderSavedView::CaptureOptions options;
    if (!getOptions(tuple, options))
        return nullptr;
    PY_TRY {
        getViewProviderSavedViewPtr()->capture(options);
    } PY_CATCH
    Py_Return;
}

PyObject *ViewProviderSavedViewPy::apply(PyObject *tuple)
{
    ViewProviderSavedView::CaptureOptions options;
    if (!getOptions(tuple, options))
        return nullptr;
    PY_TRY {
        getViewProviderSavedViewPtr()->apply(options);
    } PY_CATCH
    Py_Return;
}

PyObject *ViewProviderSavedViewPy::getCustomAttributes(const char* /*attr*/) const
{
    return 0;
}

int ViewProviderSavedViewPy::setCustomAttributes(const char* /*attr*/, PyObject* /*obj*/)
{
    return 0;
}
