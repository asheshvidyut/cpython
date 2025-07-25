/*
 * CPython SwissDict Implementation - Optimized Version
 *
 * This implementation is inspired by Abseil's flat_hash_map and uses
 * a pure Swiss Table design without linked list overhead for maximum performance.
 * Key optimizations:
 * - No linked list overhead
 * - Better hash function utilization
 * - Compact memory layout
 * - Pure Swiss Table without insertion order preservation
 */

#include "Python.h"

/* --- Data Structures --- */
#define SWISS_GROUP_SIZE 8
#define SWISS_EMPTY 0x80
#define SWISS_DELETED 0xFE
#define SWISS_H2_MASK 0x7F

// Compact entry structure - no linked list overhead
typedef struct {
    PyObject *key;
    PyObject *value;
    Py_hash_t hash;
} SwissDictEntry;

typedef struct {
    PyObject_HEAD
    Py_ssize_t used;
    Py_ssize_t capacity;
    uint32_t version;
    SwissDictEntry *entries;  // Compact array of entries
    Py_ssize_t num_groups;
    uint64_t *control_words;
} SwissDictObject;

/* --- Forward Declarations --- */
static int swissdict_resize(SwissDictObject *self, Py_ssize_t min_size);
static SwissDictEntry* find_entry(SwissDictObject *self, PyObject *key, Py_hash_t hash);
static int insert_into_table(SwissDictObject *self, PyObject *key, PyObject *value, Py_hash_t hash);

/* --- Core Methods --- */

static PyObject *
swissdict_new(PyTypeObject *type, PyObject *args, PyObject *kwds) {
    SwissDictObject *self = (SwissDictObject *)type->tp_alloc(type, 0);
    if (!self) return NULL;
    
    self->used = 0;
    self->capacity = SWISS_GROUP_SIZE;
    self->version = 0;
    
    // Allocate entries array
    self->entries = PyMem_Calloc(self->capacity, sizeof(SwissDictEntry));
    if (!self->entries) {
        Py_DECREF(self);
        return PyErr_NoMemory();
    }
    
    // Allocate control words
    self->num_groups = 1;
    self->control_words = PyMem_Calloc(self->num_groups, sizeof(uint64_t));
    if (!self->control_words) {
        PyMem_Free(self->entries);
        Py_DECREF(self);
        return PyErr_NoMemory();
    }
    
    // Initialize control words with empty markers
    self->control_words[0] = 0x8080808080808080ULL;
    
    return (PyObject *)self;
}

static void
swissdict_dealloc(SwissDictObject *self) {
    // Clean up all entries
    for (Py_ssize_t i = 0; i < self->capacity; i++) {
        SwissDictEntry *entry = &self->entries[i];
        if (entry->key != NULL) {
            Py_DECREF(entry->key);
            Py_DECREF(entry->value);
        }
    }
    
    PyMem_Free(self->entries);
    PyMem_Free(self->control_words);
    Py_TYPE(self)->tp_free((PyObject *)self);
}

static Py_ssize_t
swissdict_length(SwissDictObject *self) {
    return self->used;
}

static PyObject *
swissdict_subscript(SwissDictObject *self, PyObject *key) {
    Py_hash_t hash = PyObject_Hash(key);
    if (hash == -1) return NULL;
    
    SwissDictEntry *entry = find_entry(self, key, hash);
    if (entry == NULL) {
        PyErr_SetObject(PyExc_KeyError, key);
        return NULL;
    }
    
    Py_INCREF(entry->value);
    return entry->value;
}

static int
swissdict_ass_sub(SwissDictObject *self, PyObject *key, PyObject *value) {
    if (value == NULL) {
        PyErr_SetString(PyExc_NotImplementedError, "deletion is not implemented");
        return -1;
    }
    
    Py_hash_t hash = PyObject_Hash(key);
    if (hash == -1) return -1;
    
    // Check if key already exists
    SwissDictEntry *existing = find_entry(self, key, hash);
    if (existing) {
        PyObject *old_value = existing->value;
        Py_INCREF(value);
        existing->value = value;
        Py_DECREF(old_value);
        self->version++;
        return 0;
    }
    
    // Resize if needed (load factor > 0.875)
    if ((self->used + 1) * 8 > self->capacity * 7) {
        if (swissdict_resize(self, self->used * 2) != 0) return -1;
    }
    
    // Insert new entry
    if (insert_into_table(self, key, value, hash) != 0) {
        return -1;
    }
    
    self->used++;
    self->version++;
    return 0;
}

/* --- Optimized Swiss Table Logic --- */

static SwissDictEntry*
find_entry(SwissDictObject *self, PyObject *key, Py_hash_t hash) {
    size_t h1 = hash % self->num_groups;
    uint8_t h2 = (hash >> 8) & SWISS_H2_MASK;
    
    for (size_t i = 0; i < self->num_groups; ++i) {
        size_t group_idx = (h1 + i) % self->num_groups;
        uint64_t control = self->control_words[group_idx];
        
        // Check each byte in the control word
        for (int j = 0; j < SWISS_GROUP_SIZE; ++j) {
            uint8_t ctrl_byte = (control >> (j * 8)) & 0xFF;
            if (ctrl_byte == h2) {
                size_t slot = group_idx * SWISS_GROUP_SIZE + j;
                if (slot < self->capacity) {
                    SwissDictEntry *entry = &self->entries[slot];
                    if (entry->key != NULL && entry->hash == hash && 
                        PyObject_RichCompareBool(entry->key, key, Py_EQ) == 1) {
                        return entry;
                    }
                }
            }
            if (ctrl_byte == SWISS_EMPTY) {
                return NULL;
            }
        }
    }
    
    return NULL;
}

static int
insert_into_table(SwissDictObject *self, PyObject *key, PyObject *value, Py_hash_t hash) {
    size_t h1 = hash % self->num_groups;
    uint8_t h2 = (hash >> 8) & SWISS_H2_MASK;
    
    for (size_t i = 0; i < self->num_groups; ++i) {
        size_t group_idx = (h1 + i) % self->num_groups;
        uint64_t control = self->control_words[group_idx];
        
        for (int j = 0; j < SWISS_GROUP_SIZE; ++j) {
            uint8_t ctrl_byte = (control >> (j * 8)) & 0xFF;
            if (ctrl_byte == SWISS_EMPTY || ctrl_byte == SWISS_DELETED) {
                size_t slot = group_idx * SWISS_GROUP_SIZE + j;
                if (slot < self->capacity) {
                    SwissDictEntry *entry = &self->entries[slot];
                    
                    // Set the entry
                    Py_INCREF(key);
                    Py_INCREF(value);
                    entry->key = key;
                    entry->value = value;
                    entry->hash = hash;
                    
                    // Update control word
                    uint64_t mask = ~((uint64_t)0xFF << (j * 8));
                    self->control_words[group_idx] &= mask;
                    self->control_words[group_idx] |= ((uint64_t)h2 << (j * 8));
                    
                    return 0;
                }
            }
        }
    }
    
    return -1;  // No space found
}

static int
swissdict_resize(SwissDictObject *self, Py_ssize_t min_size) {
    SwissDictEntry *old_entries = self->entries;
    uint64_t *old_controls = self->control_words;
    Py_ssize_t old_capacity = self->capacity;
    
    // Calculate new size
    Py_ssize_t new_capacity = SWISS_GROUP_SIZE;
    while (new_capacity < min_size) new_capacity *= 2;
    
    // Allocate new arrays
    self->entries = PyMem_Calloc(new_capacity, sizeof(SwissDictEntry));
    self->num_groups = new_capacity / SWISS_GROUP_SIZE;
    self->control_words = PyMem_Calloc(self->num_groups, sizeof(uint64_t));
    
    if (!self->entries || !self->control_words) {
        PyMem_Free(self->entries);
        PyMem_Free(self->control_words);
        self->entries = old_entries;
        self->control_words = old_controls;
        return PyErr_NoMemory(), -1;
    }
    
    self->capacity = new_capacity;
    
    // Initialize control words
    for (size_t i = 0; i < self->num_groups; ++i) {
        self->control_words[i] = 0x8080808080808080ULL;
    }
    
    // Reinsert all entries
    for (Py_ssize_t i = 0; i < old_capacity; i++) {
        SwissDictEntry *old_entry = &old_entries[i];
        if (old_entry->key != NULL) {
            if (insert_into_table(self, old_entry->key, old_entry->value, old_entry->hash) != 0) {
                // This shouldn't happen, but handle gracefully
                Py_DECREF(old_entry->key);
                Py_DECREF(old_entry->value);
            }
        }
    }
    
    PyMem_Free(old_entries);
    PyMem_Free(old_controls);
    return 0;
}

/* --- Type Definition --- */

static PyMappingMethods swissdict_as_mapping = {
    (lenfunc)swissdict_length,
    (binaryfunc)swissdict_subscript,
    (objobjargproc)swissdict_ass_sub,
};

static PyTypeObject SwissDict_Type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "swiss.SwissDict",
    .tp_basicsize = sizeof(SwissDictObject),
    .tp_itemsize = 0,
    .tp_dealloc = (destructor)swissdict_dealloc,
    .tp_as_mapping = &swissdict_as_mapping,
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_new = swissdict_new,
}; 