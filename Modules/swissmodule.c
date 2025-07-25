#include "Python.h"
#include "../Objects/swissdictobject_simple.c"

static struct PyModuleDef swissmodule = {
    PyModuleDef_HEAD_INIT,
    "swiss",
    "A module containing the optimized SwissDict type.",
    -1,
    NULL, NULL, NULL, NULL, NULL
};

PyMODINIT_FUNC
PyInit_swiss(void)
{
    PyObject* m;
    if (PyType_Ready(&SwissDict_Type) < 0)
        return NULL;

    m = PyModule_Create(&swissmodule);
    if (m == NULL)
        return NULL;

    Py_INCREF(&SwissDict_Type);
    if (PyModule_AddObject(m, "SwissDict", (PyObject *)&SwissDict_Type) < 0) {
        Py_DECREF(&SwissDict_Type);
        Py_DECREF(m);
        return NULL;
    }

    return m;
} 