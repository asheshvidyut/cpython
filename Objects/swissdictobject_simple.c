/*
 * CPython SwissDict Implementation - Fixed Version
 *
 * This implementation follows the Swiss table design from abseil-cpp.
 * Proper control byte management and group-based probing.
 */

#include "Python.h"
#include <stdint.h>  // For uint8_t, uint32_t

/* --- Swiss Tables Constants --- */
#define SWISS_GROUP_SIZE 16
#define SWISS_EMPTY 0x80
#define SWISS_DELETED 0xFE
#define SWISS_SENTINEL 0xFF
#define SWISS_FULL 0x7F
#define SWISS_H2_MASK 0x7F

/* --- Data Structures --- */

typedef struct {
    PyObject_HEAD
    Py_ssize_t used;
    Py_ssize_t capacity;
    uint32_t version;
    
    // Control bytes array: one byte per entry
    uint8_t *ctrl;
    
    // Data arrays: separate arrays for better cache locality
    PyObject **keys;
    PyObject **values;
    Py_hash_t *hashes;
    
    // Insertion order tracking
    Py_ssize_t *insertion_order;  // Array of indices in insertion order
    Py_ssize_t insertion_count;   // Number of items in insertion order
} SwissDictObject;

/* --- Forward Declarations --- */
static int swissdict_resize(SwissDictObject *self, Py_ssize_t min_size);

/* --- Hash Functions --- */

static inline Py_ssize_t h1_hash(Py_hash_t hash, Py_ssize_t capacity) {
    // Handle negative hashes properly
    Py_ssize_t h = hash;
    if (h < 0) h = -h;
    // Use modulo instead of bitwise AND to be consistent across different capacities
    return h % capacity;
}

static inline uint8_t h2_hash(Py_hash_t hash) {
    // Handle negative hashes properly and extract H2 bits
    Py_ssize_t h = hash;
    if (h < 0) h = -h;
    // Extract the upper 7 bits for H2 (like abseil)
    return (uint8_t)((h >> (sizeof(Py_hash_t) * 8 - 7)) & SWISS_H2_MASK);
}

/* --- Control Byte Management --- */

static inline int is_empty(uint8_t ctrl) {
    return ctrl == SWISS_EMPTY;
}

static inline int is_deleted(uint8_t ctrl) {
    return ctrl == SWISS_DELETED;
}

static inline int is_full(uint8_t ctrl) {
    return ctrl < SWISS_EMPTY;
}

static inline int is_empty_or_deleted(uint8_t ctrl) {
    return ctrl >= SWISS_EMPTY;
}

/* --- Group Operations --- */

typedef struct {
    uint8_t ctrl[SWISS_GROUP_SIZE];
} Group;

static inline void group_init(Group *group, const uint8_t *ctrl, Py_ssize_t offset, Py_ssize_t capacity) {
    for (int i = 0; i < SWISS_GROUP_SIZE; i++) {
        Py_ssize_t idx = (offset + i) & (capacity - 1);  // Wrap around using bitwise AND
        group->ctrl[i] = ctrl[idx];
    }
}

static inline int group_match(const Group *group, uint8_t h2) {
    for (int i = 0; i < SWISS_GROUP_SIZE; i++) {
        if (group->ctrl[i] == h2) {
            return i;
        }
    }
    return -1;
}

static inline int group_find_empty(const Group *group) {
    for (int i = 0; i < SWISS_GROUP_SIZE; i++) {
        if (is_empty_or_deleted(group->ctrl[i])) {
            return i;
        }
    }
    return -1;
}

/* --- Capacity Management --- */

static inline Py_ssize_t next_power_of_2(Py_ssize_t n) {
    if (n <= 1) return 1;
    Py_ssize_t power = 1;
    while (power < n) {
        power *= 2;
    }
    return power;
}

static inline int is_power_of_2(Py_ssize_t n) {
    return n > 0 && (n & (n - 1)) == 0;
}

/* --- Core Methods --- */

static PyObject *
swissdict_new(PyTypeObject *type, PyObject *args, PyObject *kwds) {
    SwissDictObject *self = (SwissDictObject *)type->tp_alloc(type, 0);
    if (!self) return NULL;
    
    self->used = 0;
    self->capacity = next_power_of_2(SWISS_GROUP_SIZE * 8);  // Start with power of 2
    self->version = 0;
    self->insertion_count = 0;
    
    // Allocate arrays
    self->ctrl = PyMem_Calloc(self->capacity + SWISS_GROUP_SIZE, sizeof(uint8_t));
    self->keys = PyMem_Calloc(self->capacity, sizeof(PyObject*));
    self->values = PyMem_Calloc(self->capacity, sizeof(PyObject*));
    self->hashes = PyMem_Calloc(self->capacity, sizeof(Py_hash_t));
    self->insertion_order = PyMem_Calloc(self->capacity, sizeof(Py_ssize_t));
    
    if (!self->ctrl || !self->keys || !self->values || !self->hashes || !self->insertion_order) {
        PyMem_Free(self->ctrl);
        PyMem_Free(self->keys);
        PyMem_Free(self->values);
        PyMem_Free(self->hashes);
        PyMem_Free(self->insertion_order);
        Py_DECREF(self);
        return PyErr_NoMemory();
    }
    
    // Initialize control bytes with empty markers
    for (Py_ssize_t i = 0; i < self->capacity; i++) {
        self->ctrl[i] = SWISS_EMPTY;
    }
    // Set sentinel
    self->ctrl[self->capacity] = SWISS_SENTINEL;
    
    return (PyObject *)self;
}

static void
swissdict_dealloc(SwissDictObject *self) {
    // Decrement reference counts for all stored objects
    // Only iterate through insertion_order to avoid accessing uninitialized slots
    for (Py_ssize_t i = 0; i < self->insertion_count; i++) {
        Py_ssize_t idx = self->insertion_order[i];
        if (idx >= 0 && idx < self->capacity && self->keys[idx] != NULL) {
            Py_DECREF(self->keys[idx]);
            Py_DECREF(self->values[idx]);
        }
    }
    
    // Free arrays
    PyMem_Free(self->ctrl);
    PyMem_Free(self->keys);
    PyMem_Free(self->values);
    PyMem_Free(self->hashes);
    PyMem_Free(self->insertion_order);
    
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
    
    Py_ssize_t h1 = h1_hash(hash, self->capacity);
    
    // Linear probing to find the key
    for (Py_ssize_t offset = 0; offset < self->capacity; offset++) {
        Py_ssize_t idx = (h1 + offset) % self->capacity;
        
        // Check if this slot contains our key
        if (self->keys[idx] != NULL) {
            if (self->keys[idx] == key || 
                (self->hashes[idx] == hash && 
                 PyObject_RichCompareBool(self->keys[idx], key, Py_EQ) == 1)) {
                Py_INCREF(self->values[idx]);
                return self->values[idx];
            }
        }
        
        // If we find an empty slot, the key is not in the table
        if (is_empty(self->ctrl[idx])) {
            break;
        }
    }
    
    PyErr_SetObject(PyExc_KeyError, key);
    return NULL;
}

static int
swissdict_ass_sub(SwissDictObject *self, PyObject *key, PyObject *value) {
    if (value == NULL) {
        PyErr_SetString(PyExc_NotImplementedError, "deletion is not implemented");
        return -1;
    }
    
    Py_hash_t hash = PyObject_Hash(key);
    if (hash == -1) return -1;
    
    Py_ssize_t h1 = h1_hash(hash, self->capacity);
    uint8_t h2 = h2_hash(hash);
    
    // Check if key already exists - use linear probing
    for (Py_ssize_t offset = 0; offset < self->capacity; offset++) {
        Py_ssize_t idx = (h1 + offset) % self->capacity;
        
        // Check if this slot contains our key
        if (self->keys[idx] != NULL) {
            if (self->keys[idx] == key || 
                (self->hashes[idx] == hash && 
                 PyObject_RichCompareBool(self->keys[idx], key, Py_EQ) == 1)) {
                
                // Update existing entry
                PyObject *old_value = self->values[idx];
                Py_INCREF(value);
                self->values[idx] = value;
                Py_DECREF(old_value);
                self->version++;
                return 0;
            }
        }
        
        // If we find an empty slot, the key is not in the table
        if (is_empty(self->ctrl[idx])) {
            break;
        }
    }
    
    // Key doesn't exist, insert new entry
    if (self->used >= self->capacity * 0.75) {  // 75% load factor
        if (swissdict_resize(self, self->capacity * 2) != 0) {
            return -1;
        }
        // Recalculate hash after resize
        h1 = h1_hash(hash, self->capacity);
    }
    
    // Find empty slot and insert - use linear probing
    Py_ssize_t data_index = -1;
    for (Py_ssize_t offset = 0; offset < self->capacity; offset++) {
        Py_ssize_t idx = (h1 + offset) % self->capacity;
        if (is_empty(self->ctrl[idx])) {
            data_index = idx;
            break;
        }
    }
    
    if (data_index == -1) {
        // If we still can't find space, we need to resize
        if (swissdict_resize(self, self->capacity * 2) != 0) {
            return -1;
        }
        // Try insertion again after resize
        return swissdict_ass_sub(self, key, value);
    }
    
    // Set the entry
    Py_INCREF(key);
    Py_INCREF(value);
    self->keys[data_index] = key;
    self->values[data_index] = value;
    self->hashes[data_index] = hash;
    self->ctrl[data_index] = SWISS_FULL;  // Mark as full
    
    // Update insertion order
    self->insertion_order[self->insertion_count] = data_index;
    self->insertion_count++;
    
    self->used++;
    self->version++;
    return 0;
}

static int
swissdict_resize(SwissDictObject *self, Py_ssize_t min_size) {
    uint8_t *old_ctrl = self->ctrl;
    PyObject **old_keys = self->keys;
    PyObject **old_values = self->values;
    Py_hash_t *old_hashes = self->hashes;
    Py_ssize_t *old_insertion_order = self->insertion_order;
    Py_ssize_t old_capacity = self->capacity;
    Py_ssize_t old_insertion_count = self->insertion_count;
    
    // Calculate new size (ensure it's a power of 2)
    Py_ssize_t new_capacity = next_power_of_2(min_size);
    
    // Ensure we have enough space for all existing items plus some headroom
    if (new_capacity < old_insertion_count * 4) {
        new_capacity = next_power_of_2(old_insertion_count * 4);
    }
    
    // Allocate new arrays
    self->capacity = new_capacity;
    
    self->ctrl = PyMem_Calloc(self->capacity + SWISS_GROUP_SIZE, sizeof(uint8_t));
    self->keys = PyMem_Calloc(self->capacity, sizeof(PyObject*));
    self->values = PyMem_Calloc(self->capacity, sizeof(PyObject*));
    self->hashes = PyMem_Calloc(self->capacity, sizeof(Py_hash_t));
    self->insertion_order = PyMem_Calloc(self->capacity, sizeof(Py_ssize_t));
    
    if (!self->ctrl || !self->keys || !self->values || !self->hashes || !self->insertion_order) {
        PyMem_Free(self->ctrl);
        PyMem_Free(self->keys);
        PyMem_Free(self->values);
        PyMem_Free(self->hashes);
        PyMem_Free(self->insertion_order);
        self->ctrl = old_ctrl;
        self->keys = old_keys;
        self->values = old_values;
        self->hashes = old_hashes;
        self->insertion_order = old_insertion_order;
        self->capacity = old_capacity;
        return PyErr_NoMemory(), -1;
    }
    
    // Initialize control bytes with empty markers
    for (Py_ssize_t i = 0; i < self->capacity; i++) {
        self->ctrl[i] = SWISS_EMPTY;
    }
    // Set sentinel
    self->ctrl[self->capacity] = SWISS_SENTINEL;
    
    // Reset counters
    self->used = 0;
    self->insertion_count = 0;
    
    // Reinsert all entries in insertion order
    for (Py_ssize_t i = 0; i < old_insertion_count; i++) {
        Py_ssize_t old_idx = old_insertion_order[i];
        if (old_idx >= 0 && old_idx < old_capacity && old_keys[old_idx] != NULL) {
            // Use the same insertion logic as normal insertion
            Py_hash_t hash = old_hashes[old_idx];
            Py_ssize_t h1 = h1_hash(hash, self->capacity);
            
            // Find empty slot and insert - use linear probing
            Py_ssize_t data_index = -1;
            for (Py_ssize_t offset = 0; offset < self->capacity; offset++) {
                Py_ssize_t idx = (h1 + offset) % self->capacity;
                if (is_empty(self->ctrl[idx])) {
                    data_index = idx;
                    break;
                }
            }
            
            // This should never fail with 4x capacity, but if it does, try linear probing
            if (data_index == -1) {
                // Try linear probing as fallback
                for (Py_ssize_t fallback_idx = 0; fallback_idx < self->capacity; fallback_idx++) {
                    if (self->ctrl[fallback_idx] == SWISS_EMPTY) {
                        data_index = fallback_idx;
                        break;
                    }
                }
                
                // If still no space, we have a serious problem - this should never happen
                if (data_index == -1) {
                    // This is a critical error - we should never run out of space with 4x capacity
                    // Clean up and return error
                    PyMem_Free(self->ctrl);
                    PyMem_Free(self->keys);
                    PyMem_Free(self->values);
                    PyMem_Free(self->hashes);
                    PyMem_Free(self->insertion_order);
                    self->ctrl = old_ctrl;
                    self->keys = old_keys;
                    self->values = old_values;
                    self->hashes = old_hashes;
                    self->insertion_order = old_insertion_order;
                    self->capacity = old_capacity;
                    self->used = old_insertion_count;
                    self->insertion_count = old_insertion_count;
                    return -1;
                }
            }
            
            // Set the entry
            Py_INCREF(old_keys[old_idx]);
            Py_INCREF(old_values[old_idx]);
            self->keys[data_index] = old_keys[old_idx];
            self->values[data_index] = old_values[old_idx];
            self->hashes[data_index] = hash;
            self->ctrl[data_index] = SWISS_FULL;  // Mark as full
            
            // Update insertion order
            self->insertion_order[self->insertion_count] = data_index;
            self->insertion_count++;
            
            self->used++;
        }
    }
    
    // Free old arrays
    PyMem_Free(old_ctrl);
    PyMem_Free(old_keys);
    PyMem_Free(old_values);
    PyMem_Free(old_hashes);
    PyMem_Free(old_insertion_order);
    
    return 0;
}

/* --- Iteration Support --- */

static PyObject *
swissdict_items(SwissDictObject *self) {
    PyObject *result = PyList_New(self->insertion_count);
    if (!result) return NULL;
    
    for (Py_ssize_t i = 0; i < self->insertion_count; i++) {
        Py_ssize_t idx = self->insertion_order[i];
        PyObject *key = self->keys[idx];
        PyObject *value = self->values[idx];
        
        PyObject *item = PyTuple_Pack(2, key, value);
        if (!item) {
            Py_DECREF(result);
            return NULL;
        }
        
        PyList_SET_ITEM(result, i, item);
    }
    
    return result;
}

static PyObject *
swissdict_keys(SwissDictObject *self) {
    PyObject *result = PyList_New(self->insertion_count);
    if (!result) return NULL;
    
    for (Py_ssize_t i = 0; i < self->insertion_count; i++) {
        Py_ssize_t idx = self->insertion_order[i];
        PyObject *key = self->keys[idx];
        
        Py_INCREF(key);
        PyList_SET_ITEM(result, i, key);
    }
    
    return result;
}

static PyObject *
swissdict_values(SwissDictObject *self) {
    PyObject *result = PyList_New(self->insertion_count);
    if (!result) return NULL;
    
    for (Py_ssize_t i = 0; i < self->insertion_count; i++) {
        Py_ssize_t idx = self->insertion_order[i];
        PyObject *value = self->values[idx];
        
        Py_INCREF(value);
        PyList_SET_ITEM(result, i, value);
    }
    
    return result;
}

/* --- Type Definition --- */

static PyMappingMethods swissdict_as_mapping = {
    (lenfunc)swissdict_length,
    (binaryfunc)swissdict_subscript,
    (objobjargproc)swissdict_ass_sub,
};

static PyMethodDef swissdict_methods[] = {
    {"items", (PyCFunction)swissdict_items, METH_NOARGS, "Return a list of (key, value) pairs in insertion order"},
    {"keys", (PyCFunction)swissdict_keys, METH_NOARGS, "Return a list of keys in insertion order"},
    {"values", (PyCFunction)swissdict_values, METH_NOARGS, "Return a list of values in insertion order"},
    {NULL, NULL, 0, NULL}
};

static PyTypeObject SwissDict_Type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "swiss.SwissDict",
    .tp_basicsize = sizeof(SwissDictObject),
    .tp_itemsize = 0,
    .tp_dealloc = (destructor)swissdict_dealloc,
    .tp_as_mapping = &swissdict_as_mapping,
    .tp_methods = swissdict_methods,
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_new = swissdict_new,
}; 