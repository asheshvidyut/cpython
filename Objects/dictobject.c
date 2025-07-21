/*
 * Swiss Table-based Python dict implementation (allocation/deallocation only)
 * Inspired by Go and Abseil Swiss Table designs.
 */

#include "Python.h"
#include "dictobject.h"
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#define SWISS_MIN_CAPACITY 8
#define SWISS_EMPTY 0x80
#define SWISS_DELETED 0xFE
#define SWISS_MAX_LOAD 0.875

// --- Allocation ---
static PyObject *dict_new(PyTypeObject *type, PyObject *args, PyObject *kwds) {
    PyDictObject *mp = (PyDictObject *)type->tp_alloc(type, 0);
    if (!mp) return NULL;
    mp->capacity = SWISS_MIN_CAPACITY;
    mp->used = 0;
    mp->deleted = 0;
    mp->entries = (DictEntry *)calloc(mp->capacity, sizeof(DictEntry));
    mp->control_bytes = (uint8_t *)malloc(mp->capacity * sizeof(uint8_t));
    if (!mp->entries || !mp->control_bytes) {
        if (mp->entries) free(mp->entries);
        if (mp->control_bytes) free(mp->control_bytes);
        Py_TYPE(mp)->tp_free((PyObject *)mp);
        return NULL;
    }
    memset(mp->control_bytes, SWISS_EMPTY, mp->capacity);
    mp->ma_head = NULL;
    mp->ma_tail = NULL;
    return (PyObject *)mp;
}

// --- Deallocation ---
static void dict_dealloc(PyObject *self) {
    PyDictObject *mp = (PyDictObject *)self;
    if (mp->entries) free(mp->entries);
    if (mp->control_bytes) free(mp->control_bytes);
    Py_TYPE(self)->tp_free(self);
}

// --- Swiss Table helpers ---
static inline uint8_t swiss_hash_to_ctrl(Py_hash_t hash) {
    // Use lower 7 bits, never 0x80 (SWISS_EMPTY) or 0xFE (SWISS_DELETED)
    uint8_t h = (uint8_t)(hash & 0x7F);
    if (h == SWISS_EMPTY || h == SWISS_DELETED) h ^= 0x42; // Arbitrary xor to avoid reserved values
    return h;
}

static inline Py_ssize_t swiss_probe(PyDictObject *mp, Py_hash_t hash, uint8_t ctrl, PyObject *key, int *found) {
    Py_ssize_t mask = mp->capacity - 1;
    Py_ssize_t i = (Py_ssize_t)hash & mask;
    for (Py_ssize_t n_probe = 0; n_probe < mp->capacity; ++n_probe) {
        uint8_t c = mp->control_bytes[i];
        if (c == ctrl) {
            // Possible match, check key equality
            if (mp->entries[i].key && PyObject_RichCompareBool(mp->entries[i].key, key, Py_EQ) == 1) {
                *found = 1;
                return i;
            }
        } else if (c == SWISS_EMPTY) {
            *found = 0;
            return i;
        }
        i = (i + 1) & mask;
    }
    *found = 0;
    return -1; // Table full (should not happen)
}

// --- Lookup ---
static PyObject *dict_lookup(PyDictObject *mp, PyObject *key) {
    Py_hash_t hash = PyObject_Hash(key);
    if (hash == -1) return NULL;
    uint8_t ctrl = swiss_hash_to_ctrl(hash);
    int found = 0;
    Py_ssize_t idx = swiss_probe(mp, hash, ctrl, key, &found);
    if (found && idx >= 0) {
        return mp->entries[idx].value;
    }
    return NULL;
}

// --- Resize ---
static int dict_resize(PyDictObject *mp, Py_ssize_t min_capacity) {
    Py_ssize_t old_capacity = mp->capacity;
    Py_ssize_t new_capacity = old_capacity * 2;
    if (new_capacity < min_capacity) {
        new_capacity = min_capacity;
    }
    DictEntry *new_entries = (DictEntry *)calloc(new_capacity, sizeof(DictEntry));
    uint8_t *new_ctrl = (uint8_t *)malloc(new_capacity * sizeof(uint8_t));
    if (!new_entries || !new_ctrl) {
        if (new_entries) free(new_entries);
        if (new_ctrl) free(new_ctrl);
        return -1;
    }
    memset(new_ctrl, SWISS_EMPTY, new_capacity);
    // Rehash all active entries
    for (Py_ssize_t i = 0; i < old_capacity; ++i) {
        if (mp->entries[i].key && mp->control_bytes[i] != SWISS_DELETED) {
            PyObject *key = mp->entries[i].key;
            PyObject *value = mp->entries[i].value;
            Py_hash_t hash = PyObject_Hash(key);
            uint8_t ctrl = swiss_hash_to_ctrl(hash);
            Py_ssize_t mask = new_capacity - 1;
            Py_ssize_t j = (Py_ssize_t)hash & mask;
            while (new_ctrl[j] != SWISS_EMPTY) {
                j = (j + 1) & mask;
            }
            new_entries[j].key = key;
            new_entries[j].value = value;
            new_ctrl[j] = ctrl;
            // Note: prev/next for insertion order will be fixed below
        }
    }
    // Rebuild the doubly linked list for insertion order
    DictEntry *prev = NULL;
    mp->ma_head = NULL;
    mp->ma_tail = NULL;
    for (Py_ssize_t i = 0; i < new_capacity; ++i) {
        if (new_entries[i].key) {
            new_entries[i].prev = prev;
            new_entries[i].next = NULL;
            if (prev) {
                prev->next = &new_entries[i];
            } else {
                mp->ma_head = &new_entries[i];
            }
            prev = &new_entries[i];
        }
    }
    mp->ma_tail = prev;
    free(mp->entries);
    free(mp->control_bytes);
    mp->entries = new_entries;
    mp->control_bytes = new_ctrl;
    mp->capacity = new_capacity;
    mp->deleted = 0;
    return 0;
}

// --- Update insert/delete to trigger resize ---
static int dict_insert(PyDictObject *mp, PyObject *key, PyObject *value) {
    double load = (double)(mp->used + mp->deleted) / (double)mp->capacity;
    if (load > SWISS_MAX_LOAD) {
        if (dict_resize(mp, mp->capacity * 2) < 0) return -1;
    }
    Py_hash_t hash = PyObject_Hash(key);
    if (hash == -1) return -1;
    uint8_t ctrl = swiss_hash_to_ctrl(hash);
    int found = 0;
    Py_ssize_t idx = swiss_probe(mp, hash, ctrl, key, &found);
    if (idx < 0) return -1; // Table full
    if (found) {
        // Overwrite value
        Py_XDECREF(mp->entries[idx].value);
        Py_INCREF(value);
        mp->entries[idx].value = value;
        return 0;
    }
    // Insert new entry
    Py_INCREF(key);
    Py_INCREF(value);
    mp->entries[idx].key = key;
    mp->entries[idx].value = value;
    mp->control_bytes[idx] = ctrl;
    // Update doubly linked list for insertion order
    mp->entries[idx].prev = mp->ma_tail;
    mp->entries[idx].next = NULL;
    if (mp->ma_tail) {
        mp->ma_tail->next = &mp->entries[idx];
    } else {
        mp->ma_head = &mp->entries[idx];
    }
    mp->ma_tail = &mp->entries[idx];
    mp->used++;
    return 0;
}

// --- Delete (with doubly linked list update) ---
static int dict_delete(PyDictObject *mp, PyObject *key) {
    if (mp->used * 2 < mp->capacity && mp->deleted > mp->capacity / 4) {
        if (dict_resize(mp, mp->capacity) < 0) return -1;
    }
    Py_hash_t hash = PyObject_Hash(key);
    if (hash == -1) return -1;
    uint8_t ctrl = swiss_hash_to_ctrl(hash);
    int found = 0;
    Py_ssize_t idx = swiss_probe(mp, hash, ctrl, key, &found);
    if (!found || idx < 0) {
        PyErr_SetString(PyExc_KeyError, "Key not found in dict");
        return -1;
    }
    // Remove from linked list
    DictEntry *entry = &mp->entries[idx];
    if (entry->prev) {
        entry->prev->next = entry->next;
    } else {
        mp->ma_head = entry->next;
    }
    if (entry->next) {
        entry->next->prev = entry->prev;
    } else {
        mp->ma_tail = entry->prev;
    }
    // Clear entry
    Py_XDECREF(entry->key);
    Py_XDECREF(entry->value);
    entry->key = NULL;
    entry->value = NULL;
    entry->prev = NULL;
    entry->next = NULL;
    mp->control_bytes[idx] = SWISS_DELETED;
    mp->used--;
    mp->deleted++;
    return 0;
}

// --- Python C API wrappers ---
int PyDict_SetItem(PyObject *op, PyObject *key, PyObject *value) {
    if (!PyDict_Check(op)) {
        PyErr_SetString(PyExc_TypeError, "PyDict_SetItem: argument is not a dict");
        return -1;
    }
    return dict_insert((PyDictObject *)op, key, value);
}

PyObject *PyDict_GetItem(PyObject *op, PyObject *key) {
    if (!PyDict_Check(op)) {
        PyErr_SetString(PyExc_TypeError, "PyDict_GetItem: argument is not a dict");
        return NULL;
    }
    return dict_lookup((PyDictObject *)op, key);
}

int PyDict_DelItem(PyObject *op, PyObject *key) {
    if (!PyDict_Check(op)) {
        PyErr_SetString(PyExc_TypeError, "PyDict_DelItem: argument is not a dict");
        return -1;
    }
    return dict_delete((PyDictObject *)op, key);
}

// --- Mapping protocol ---
static Py_ssize_t dict_length(PyObject *self) {
    return ((PyDictObject *)self)->used;
}

static PyObject *dict_subscript(PyObject *self, PyObject *key) {
    PyObject *value = PyDict_GetItem(self, key);
    if (!value) {
        PyErr_SetObject(PyExc_KeyError, key);
        return NULL;
    }
    Py_INCREF(value);
    return value;
}

static int dict_ass_sub(PyObject *self, PyObject *key, PyObject *value) {
    if (value == NULL) {
        return PyDict_DelItem(self, key);
    } else {
        return PyDict_SetItem(self, key, value);
    }
}

static PyMappingMethods dict_as_mapping = {
    dict_length, /*mp_length*/
    dict_subscript, /*mp_subscript*/
    dict_ass_sub, /*mp_ass_subscript*/
};

// --- Iteration support (insertion order) ---
typedef struct {
    PyObject_HEAD
    PyDictObject *di_dict;
    DictEntry *di_cur;
} dict_iterobject;

static PyObject *dict_iter(PyObject *self) {
    dict_iterobject *it = PyObject_New(dict_iterobject, &PyType_Type);
    if (!it) return NULL;
    Py_INCREF(self);
    it->di_dict = (PyDictObject *)self;
    it->di_cur = it->di_dict->ma_head;
    return (PyObject *)it;
}

static PyObject *dictiter_next(PyObject *self) {
    dict_iterobject *it = (dict_iterobject *)self;
    if (!it->di_cur) return NULL;
    PyObject *key = it->di_cur->key;
    it->di_cur = it->di_cur->next;
    Py_INCREF(key);
    return key;
}

static PyTypeObject dict_iter_type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "dict_keyiterator",
    .tp_basicsize = sizeof(dict_iterobject),
    .tp_iternext = dictiter_next,
};

// --- Value iteration ---
typedef struct {
    PyObject_HEAD
    PyDictObject *di_dict;
    DictEntry *di_cur;
} dict_valueiterobject;

static PyObject *dict_valueiter_next(PyObject *self) {
    dict_valueiterobject *it = (dict_valueiterobject *)self;
    if (!it->di_cur) return NULL;
    PyObject *value = it->di_cur->value;
    it->di_cur = it->di_cur->next;
    Py_INCREF(value);
    return value;
}

static PyTypeObject dict_valueiter_type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "dict_valueiterator",
    .tp_basicsize = sizeof(dict_valueiterobject),
    .tp_iternext = dict_valueiter_next,
};

static PyObject *dict_values(PyObject *self, PyObject *Py_UNUSED(ignored)) {
    dict_valueiterobject *it = PyObject_New(dict_valueiterobject, &dict_valueiter_type);
    if (!it) return NULL;
    Py_INCREF(self);
    it->di_dict = (PyDictObject *)self;
    it->di_cur = it->di_dict->ma_head;
    return (PyObject *)it;
}

// --- Item iteration ---
typedef struct {
    PyObject_HEAD
    PyDictObject *di_dict;
    DictEntry *di_cur;
} dict_itemiterobject;

static PyObject *dict_itemiter_next(PyObject *self) {
    dict_itemiterobject *it = (dict_itemiterobject *)self;
    if (!it->di_cur) return NULL;
    PyObject *key = it->di_cur->key;
    PyObject *value = it->di_cur->value;
    it->di_cur = it->di_cur->next;
    PyObject *tuple = PyTuple_Pack(2, key, value);
    return tuple;
}

static PyTypeObject dict_itemiter_type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "dict_itemiterator",
    .tp_basicsize = sizeof(dict_itemiterobject),
    .tp_iternext = dict_itemiter_next,
};

static PyObject *dict_items(PyObject *self, PyObject *Py_UNUSED(ignored)) {
    dict_itemiterobject *it = PyObject_New(dict_itemiterobject, &dict_itemiter_type);
    if (!it) return NULL;
    Py_INCREF(self);
    it->di_dict = (PyDictObject *)self;
    it->di_cur = it->di_dict->ma_head;
    return (PyObject *)it;
}

// --- Add methods to PyDict_Type ---
static PyMethodDef dict_methods[] = {
    {"values", (PyCFunction)dict_values, METH_NOARGS, "D.values() -> an iterator over the values"},
    {"items", (PyCFunction)dict_items, METH_NOARGS, "D.items() -> an iterator over the (key, value) pairs"},
    {NULL, NULL}
};

// --- Update PyDict_Type ---
PyTypeObject PyDict_Type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "dict",
    .tp_basicsize = sizeof(PyDictObject),
    .tp_dealloc = dict_dealloc,
    .tp_as_mapping = &dict_as_mapping,
    .tp_iter = dict_iter,
    .tp_methods = dict_methods,
    .tp_new = dict_new,
    // TODO: Fill in other slots (repr, etc.)
};
