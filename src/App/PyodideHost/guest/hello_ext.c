/* The smallest possible extension module, used once to prove the
 * toolchain: built with the emscripten pyodide was built with, wrapped
 * by make_wheel.py, loaded into the probe through the scoped reader.
 * If this imports and answers, the fcx_image module will link too.
 */
#include <Python.h>

static PyObject *add(PyObject *self, PyObject *args)
{
    long a, b;
    if (!PyArg_ParseTuple(args, "ll", &a, &b))
        return NULL;
    return PyLong_FromLong(a + b);
}

static PyMethodDef methods[] = {
    {"add", add, METH_VARARGS, "add two integers"},
    {NULL, NULL, 0, NULL},
};

static struct PyModuleDef moduledef = {
    PyModuleDef_HEAD_INIT, "hello_ext", "toolchain probe", -1, methods,
    NULL, NULL, NULL, NULL,
};

PyMODINIT_FUNC PyInit_hello_ext(void)
{
    return PyModule_Create(&moduledef);
}
