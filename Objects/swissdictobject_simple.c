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

// Contiguous entry structure for better cache locality
typedef struct {
    PyObject *key;
    PyObject *value;
    Py_hash_t hash;
    Py_ssize_t prev;  // Previous index in insertion order
    Py_ssize_t next;  // Next index in insertion order
} SwissDictEntry;

typedef struct {
    PyObject_HEAD
    Py_ssize_t used;
    Py_ssize_t capacity;
    uint32_t version;
    
    // Control bytes array: one byte per entry
    uint8_t *ctrl;
    
    // Contiguous entries array for better cache locality
    SwissDictEntry *entries;
    
    // Doubly linked list pointers
    Py_ssize_t head;   // Index of first item
    Py_ssize_t tail;   // Index of last item
} SwissDictObject;

/* --- Forward Declarations --- */
static int swissdict_resize(SwissDictObject *self, Py_ssize_t min_size);

/* --- Hash Functions --- */

static inline Py_ssize_t h1_hash(Py_hash_t hash, Py_ssize_t capacity) {
    // Handle negative hashes properly
    Py_ssize_t h = hash;
    if (h < 0) h = -h;
    // Use bitwise AND for power-of-2 capacity (much faster than modulo)
    return h & (capacity - 1);
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
    self->head = -1;  // No items initially
    self->tail = -1;
    
    // Allocate arrays
    self->ctrl = PyMem_Calloc(self->capacity + SWISS_GROUP_SIZE, sizeof(uint8_t));
    self->entries = PyMem_Calloc(self->capacity, sizeof(SwissDictEntry));
    
    if (!self->ctrl || !self->entries) {
        PyMem_Free(self->ctrl);
        PyMem_Free(self->entries);
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
    for (Py_ssize_t i = 0; i < self->capacity; i++) {
        if (self->entries[i].key != NULL) {
            Py_DECREF(self->entries[i].key);
            Py_DECREF(self->entries[i].value);
        }
    }
    
    // Free arrays
    PyMem_Free(self->ctrl);
    PyMem_Free(self->entries);
    
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
    uint8_t h2 = h2_hash(hash);
    
    // SwissTable group-based probing with H2 filtering
    for (Py_ssize_t group_offset = 0; group_offset < self->capacity; group_offset += SWISS_GROUP_SIZE) {
        Py_ssize_t group_start = (h1 + group_offset) & (self->capacity - 1);
        
        // Check each slot in the group
        for (Py_ssize_t i = 0; i < SWISS_GROUP_SIZE; i++) {
            Py_ssize_t idx = (group_start + i) & (self->capacity - 1);
            
            // Fast H2 filtering - check metadata first
            if (self->ctrl[idx] == h2 && self->entries[idx].key != NULL) {
                // H2 matches, now check full key
                if (self->entries[idx].key == key || 
                    (self->entries[idx].hash == hash && 
                     PyObject_RichCompareBool(self->entries[idx].key, key, Py_EQ) == 1)) {
                    Py_INCREF(self->entries[idx].value);
                    return self->entries[idx].value;
                }
            }
            
            // If we find an empty slot, the key is not in the table
            if (is_empty(self->ctrl[idx])) {
                goto not_found;
            }
        }
    }
    
not_found:
    
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
    
    // Check if we need to resize first
    if (self->used >= self->capacity * 0.875) {  // 87.5% load factor (SwissTable style)
        if (swissdict_resize(self, self->capacity * 2) != 0) {
            return -1;
        }
    }
    
    Py_ssize_t h1 = h1_hash(hash, self->capacity);
    uint8_t h2 = h2_hash(hash);
    Py_ssize_t data_index = -1;
    
    // SwissTable group-based probing with H2 filtering
    for (Py_ssize_t group_offset = 0; group_offset < self->capacity; group_offset += SWISS_GROUP_SIZE) {
        Py_ssize_t group_start = (h1 + group_offset) & (self->capacity - 1);
        
        // Check each slot in the group for existing key
        for (Py_ssize_t i = 0; i < SWISS_GROUP_SIZE; i++) {
            Py_ssize_t idx = (group_start + i) & (self->capacity - 1);
            
            // Fast H2 filtering - check metadata first
            if (self->ctrl[idx] == h2 && self->entries[idx].key != NULL) {
                // H2 matches, now check full key
                if (self->entries[idx].key == key || 
                    (self->entries[idx].hash == hash && 
                     PyObject_RichCompareBool(self->entries[idx].key, key, Py_EQ) == 1)) {
                    
                    // Update existing entry
                    PyObject *old_value = self->entries[idx].value;
                    Py_INCREF(value);
                    self->entries[idx].value = value;
                    Py_DECREF(old_value);
                    self->version++;
                    return 0;
                }
            }
            
            // If we find an empty slot, the key is not in the table
            if (is_empty(self->ctrl[idx])) {
                data_index = idx;
                goto insert_new;
            }
        }
    }
    
insert_new:
    
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
    self->entries[data_index].key = key;
    self->entries[data_index].value = value;
    self->entries[data_index].hash = hash;
    self->ctrl[data_index] = h2;  // Mark as full
    
    // Add to doubly linked list (insertion order)
    if (self->head == -1) {
        // First item
        self->head = data_index;
        self->tail = data_index;
        self->entries[data_index].prev = -1;
        self->entries[data_index].next = -1;
    } else {
        // Add to tail
        self->entries[self->tail].next = data_index;
        self->entries[data_index].prev = self->tail;
        self->entries[data_index].next = -1;
        self->tail = data_index;
    }
    
    self->used++;
    self->version++;
    return 0;
}

static int
swissdict_resize(SwissDictObject *self, Py_ssize_t min_size) {
    uint8_t *old_ctrl = self->ctrl;
    SwissDictEntry *old_entries = self->entries;
    Py_ssize_t old_capacity = self->capacity;
    Py_ssize_t old_head = self->head;
    Py_ssize_t old_tail = self->tail;
    
    // Calculate new size (ensure it's a power of 2)
    Py_ssize_t new_capacity = next_power_of_2(min_size);
    
    // Ensure we have enough space for all existing items plus some headroom
    if (new_capacity < self->used * 4) {
        new_capacity = next_power_of_2(self->used * 4);
    }
    
    // Allocate new arrays
    self->capacity = new_capacity;
    
    self->ctrl = PyMem_Calloc(self->capacity + SWISS_GROUP_SIZE, sizeof(uint8_t));
    self->entries = PyMem_Calloc(self->capacity, sizeof(SwissDictEntry));
    
    if (!self->ctrl || !self->entries) {
        PyMem_Free(self->ctrl);
        PyMem_Free(self->entries);
        self->ctrl = old_ctrl;
        self->entries = old_entries;
        self->capacity = old_capacity;
        self->head = old_head;
        self->tail = old_tail;
        return PyErr_NoMemory(), -1;
    }
    
    // Initialize control bytes with empty markers
    for (Py_ssize_t i = 0; i < self->capacity; i++) {
        self->ctrl[i] = SWISS_EMPTY;
    }
    // Set sentinel
    self->ctrl[self->capacity] = SWISS_SENTINEL;
    
    // Reset counters and linked list
    self->used = 0;
    self->head = -1;
    self->tail = -1;
    
    // Reinsert all entries
    for (Py_ssize_t old_idx = 0; old_idx < old_capacity; old_idx++) {
        if (old_entries[old_idx].key != NULL) {
            // Use the same insertion logic as normal insertion
            Py_hash_t hash = old_entries[old_idx].hash;
            Py_ssize_t h1 = h1_hash(hash, self->capacity);
            uint8_t h2 = h2_hash(hash);
            Py_ssize_t data_index = -1;
            
            // Find empty slot and insert - use group-based probing
            for (Py_ssize_t group_offset = 0; group_offset < self->capacity; group_offset += SWISS_GROUP_SIZE) {
                Py_ssize_t group_start = (h1 + group_offset) & (self->capacity - 1);
                
                // Check each slot in the group for empty slot
                for (Py_ssize_t i = 0; i < SWISS_GROUP_SIZE; i++) {
                    Py_ssize_t idx = (group_start + i) & (self->capacity - 1);
                    if (is_empty(self->ctrl[idx])) {
                        data_index = idx;
                        goto found_empty_resize;
                    }
                }
            }
            
found_empty_resize:
            
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
                    PyMem_Free(self->entries);
                    self->ctrl = old_ctrl;
                    self->entries = old_entries;
                    self->capacity = old_capacity;
                    return -1;
                }
            }
            
                  // Set the entry
                  Py_INCREF(old_entries[old_idx].key);
                  Py_INCREF(old_entries[old_idx].value);
                  self->entries[data_index].key = old_entries[old_idx].key;
                  self->entries[data_index].value = old_entries[old_idx].value;
                  self->entries[data_index].hash = hash;
                  self->ctrl[data_index] = h2;  // Mark as full
                  
                  // Add to doubly linked list (maintain insertion order)
                  if (self->head == -1) {
                      // First item
                      self->head = data_index;
                      self->tail = data_index;
                      self->entries[data_index].prev = -1;
                      self->entries[data_index].next = -1;
                  } else {
                      // Add to tail
                      self->entries[self->tail].next = data_index;
                      self->entries[data_index].prev = self->tail;
                      self->entries[data_index].next = -1;
                      self->tail = data_index;
                  }
            
            self->used++;
        }
    }
    
    // Free old arrays
    PyMem_Free(old_ctrl);
    PyMem_Free(old_entries);
    
    return 0;
}

/* --- Iteration Support --- */

static PyObject *
swissdict_items(SwissDictObject *self) {
    PyObject *result = PyList_New(self->used);
    if (!result) return NULL;
    
    Py_ssize_t result_idx = 0;
    
    // Iterate through doubly linked list in insertion order
    Py_ssize_t idx = self->head;
    while (idx != -1) {
        PyObject *key = self->entries[idx].key;
        PyObject *value = self->entries[idx].value;
        
        PyObject *item = PyTuple_Pack(2, key, value);
        if (!item) {
            Py_DECREF(result);
            return NULL;
        }
        
        PyList_SET_ITEM(result, result_idx, item);
        result_idx++;
        idx = self->entries[idx].next;
    }
    
    return result;
}

static PyObject *
swissdict_keys(SwissDictObject *self) {
    PyObject *result = PyList_New(self->used);
    if (!result) return NULL;
    
    Py_ssize_t result_idx = 0;
    
    // Iterate through doubly linked list in insertion order
    Py_ssize_t idx = self->head;
    while (idx != -1) {
        PyObject *key = self->entries[idx].key;
        
        Py_INCREF(key);
        PyList_SET_ITEM(result, result_idx, key);
        result_idx++;
        idx = self->entries[idx].next;
    }
    
    return result;
}

static PyObject *
swissdict_values(SwissDictObject *self) {
    PyObject *result = PyList_New(self->used);
    if (!result) return NULL;
    
    Py_ssize_t result_idx = 0;
    
    // Iterate through doubly linked list in insertion order
    Py_ssize_t idx = self->head;
    while (idx != -1) {
        PyObject *value = self->entries[idx].value;
        
        Py_INCREF(value);
        PyList_SET_ITEM(result, result_idx, value);
        result_idx++;
        idx = self->entries[idx].next;
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