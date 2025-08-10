/*
 * CPython SwissDict Implementation - Abseil Swiss Tables Design
 *
 * This implementation follows the Abseil Swiss Tables design:
 * - 16-byte groups with metadata array
 * - H1 (57 bits) for group index, H2 (7 bits) for fingerprint
 * - SSE-optimized metadata lookup
 * - Proper control byte encoding
 */

#include "Python.h"
#include <immintrin.h>  // For SSE instructions

/* --- Swiss Tables Constants --- */
#define SWISS_GROUP_SIZE 16
#define SWISS_EMPTY 0x80
#define SWISS_DELETED 0xFE
#define SWISS_H2_MASK 0x7F

/* --- Data Structures --- */

typedef struct {
    PyObject_HEAD
    Py_ssize_t used;
    Py_ssize_t capacity;
    uint32_t version;
    Py_ssize_t num_groups;
    
    // Metadata array: one byte per entry (16 bytes per group)
    uint8_t *metadata;
    
    // Data arrays: separate arrays for better cache locality
    PyObject **keys;
    PyObject **values;
    Py_hash_t *hashes;
} SwissDictObject;

/* --- Forward Declarations --- */
static int swissdict_resize(SwissDictObject *self, Py_ssize_t min_size);
static int insert_into_table(SwissDictObject *self, PyObject *key, PyObject *value, Py_hash_t hash);

/* --- SSE-optimized Metadata Operations (Abseil-inspired) --- */

static inline __m128i load_metadata_group(const uint8_t *metadata, Py_ssize_t group_idx) {
    return _mm_loadu_si128((const __m128i*)(metadata + group_idx * SWISS_GROUP_SIZE));
}

static inline int find_matches_in_group(__m128i metadata, uint8_t h2) {
    // Create a vector with h2 repeated 16 times
    __m128i h2_vec = _mm_set1_epi8(h2);
    
    // Compare metadata with h2
    __m128i match = _mm_cmpeq_epi8(metadata, h2_vec);
    
    // Return mask of matching bytes
    return _mm_movemask_epi8(match);
}

static inline int find_empty_in_group(__m128i metadata) {
    // Create a vector with SWISS_EMPTY repeated 16 times
    __m128i empty_vec = _mm_set1_epi8(SWISS_EMPTY);
    
    // Compare metadata with empty marker
    __m128i match = _mm_cmpeq_epi8(metadata, empty_vec);
    
    // Return mask of empty bytes
    return _mm_movemask_epi8(match);
}

static inline int find_available_in_group(__m128i metadata) {
    // Create vectors for empty and deleted markers
    __m128i empty_vec = _mm_set1_epi8(SWISS_EMPTY);
    __m128i deleted_vec = _mm_set1_epi8(SWISS_DELETED);
    
    // Find empty or deleted slots
    __m128i empty_match = _mm_cmpeq_epi8(metadata, empty_vec);
    __m128i deleted_match = _mm_cmpeq_epi8(metadata, deleted_vec);
    __m128i available = _mm_or_si128(empty_match, deleted_match);
    
    return _mm_movemask_epi8(available);
}

/* --- Hash Functions (Abseil-style) --- */

static inline Py_ssize_t h1_hash(Py_hash_t hash, Py_ssize_t num_groups) {
    // Use 57 bits for H1 (group index)
    return (hash >> 7) % num_groups;
}

static inline uint8_t h2_hash(Py_hash_t hash) {
    // Use 7 bits for H2 (fingerprint)
    return (hash >> 57) & SWISS_H2_MASK;
}

/* --- Core Methods --- */

static PyObject *
swissdict_new(PyTypeObject *type, PyObject *args, PyObject *kwds) {
    SwissDictObject *self = (SwissDictObject *)type->tp_alloc(type, 0);
    if (!self) return NULL;
    
    self->used = 0;
    self->capacity = SWISS_GROUP_SIZE;
    self->version = 0;
    self->num_groups = 1;
    
    // Allocate arrays
    self->metadata = PyMem_Calloc(self->capacity, sizeof(uint8_t));
    self->keys = PyMem_Calloc(self->capacity, sizeof(PyObject*));
    self->values = PyMem_Calloc(self->capacity, sizeof(PyObject*));
    self->hashes = PyMem_Calloc(self->capacity, sizeof(Py_hash_t));
    
    if (!self->metadata || !self->keys || !self->values || !self->hashes) {
        PyMem_Free(self->metadata);
        PyMem_Free(self->keys);
        PyMem_Free(self->values);
        PyMem_Free(self->hashes);
        Py_DECREF(self);
        return PyErr_NoMemory();
    }
    
    // Initialize metadata with empty markers
    for (Py_ssize_t i = 0; i < self->capacity; i++) {
        self->metadata[i] = SWISS_EMPTY;
    }
    
    return (PyObject *)self;
}

static void
swissdict_dealloc(SwissDictObject *self) {
    // Clean up all entries
    for (Py_ssize_t i = 0; i < self->capacity; i++) {
        if (self->keys[i] != NULL) {
            Py_DECREF(self->keys[i]);
            Py_DECREF(self->values[i]);
        }
    }
    
    PyMem_Free(self->metadata);
    PyMem_Free(self->keys);
    PyMem_Free(self->values);
    PyMem_Free(self->hashes);
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
    
    Py_ssize_t h1 = h1_hash(hash, self->num_groups);
    uint8_t h2 = h2_hash(hash);
    
    // Linear probing through groups
    for (Py_ssize_t g = 0; g < self->num_groups; g++) {
        Py_ssize_t group_idx = (h1 + g) % self->num_groups;
        Py_ssize_t base_idx = group_idx * SWISS_GROUP_SIZE;
        
        // Load metadata group using SSE
        __m128i metadata = load_metadata_group(self->metadata, group_idx);
        
        // Find matches using SSE
        int match_mask = find_matches_in_group(metadata, h2);
        
        // Process matches
        while (match_mask != 0) {
            int match_pos = __builtin_ctz(match_mask);
            Py_ssize_t idx = base_idx + match_pos;
            
            // Fast path: pointer equality
            if (self->keys[idx] == key) {
                Py_INCREF(self->values[idx]);
                return self->values[idx];
            }
            
            // Slow path: hash and object comparison
            if (self->keys[idx] != NULL && 
                self->hashes[idx] == hash && 
                PyObject_RichCompareBool(self->keys[idx], key, Py_EQ) == 1) {
                Py_INCREF(self->values[idx]);
                return self->values[idx];
            }
            
            // Clear the lowest set bit
            match_mask &= match_mask - 1;
        }
        
        // Check for early exit (all empty)
        int empty_mask = find_empty_in_group(metadata);
        if (empty_mask == 0xFFFF) {
            break; // Group is completely empty
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
    
    Py_ssize_t h1 = h1_hash(hash, self->num_groups);
    uint8_t h2 = h2_hash(hash);
    
    // Check if key already exists
    for (Py_ssize_t g = 0; g < self->num_groups; g++) {
        Py_ssize_t group_idx = (h1 + g) % self->num_groups;
        Py_ssize_t base_idx = group_idx * SWISS_GROUP_SIZE;
        
        __m128i metadata = load_metadata_group(self->metadata, group_idx);
        int match_mask = find_matches_in_group(metadata, h2);
        
        while (match_mask != 0) {
            int match_pos = __builtin_ctz(match_mask);
            Py_ssize_t idx = base_idx + match_pos;
            
            if (self->keys[idx] == key || 
                (self->keys[idx] != NULL && 
                 self->hashes[idx] == hash && 
                 PyObject_RichCompareBool(self->keys[idx], key, Py_EQ) == 1)) {
                
                // Update existing entry
                PyObject *old_value = self->values[idx];
                Py_INCREF(value);
                self->values[idx] = value;
                Py_DECREF(old_value);
                self->version++;
                return 0;
            }
            
            match_mask &= match_mask - 1;
        }
    }
    
    // Resize if needed (load factor > 0.875)
    if ((self->used + 1) * 8 > self->capacity * 7) {
        if (swissdict_resize(self, self->used * 2) != 0) return -1;
        // Recalculate h1 after resize
        h1 = h1_hash(hash, self->num_groups);
    }
    
    // Insert new entry
    if (insert_into_table(self, key, value, hash) != 0) {
        return -1;
    }
    
    self->used++;
    self->version++;
    return 0;
}

/* --- Swiss Table Insertion Logic --- */

static int
insert_into_table(SwissDictObject *self, PyObject *key, PyObject *value, Py_hash_t hash) {
    Py_ssize_t h1 = h1_hash(hash, self->num_groups);
    uint8_t h2 = h2_hash(hash);
    
    // Linear probing through groups
    for (Py_ssize_t g = 0; g < self->num_groups; g++) {
        Py_ssize_t group_idx = (h1 + g) % self->num_groups;
        Py_ssize_t base_idx = group_idx * SWISS_GROUP_SIZE;
        
        // Load metadata group using SSE
        __m128i metadata = load_metadata_group(self->metadata, group_idx);
        
        // Find available slots using SSE
        int available_mask = find_available_in_group(metadata);
        
        if (available_mask != 0) {
            int pos = __builtin_ctz(available_mask);
            Py_ssize_t idx = base_idx + pos;
            
            // Set the entry
            Py_INCREF(key);
            Py_INCREF(value);
            self->keys[idx] = key;
            self->values[idx] = value;
            self->hashes[idx] = hash;
            self->metadata[idx] = h2;
            
            return 0;
        }
    }
    
    PyErr_SetString(PyExc_RuntimeError, "SwissDict: no space available for insertion");
    return -1;
}

static int
swissdict_resize(SwissDictObject *self, Py_ssize_t min_size) {
    uint8_t *old_metadata = self->metadata;
    PyObject **old_keys = self->keys;
    PyObject **old_values = self->values;
    Py_hash_t *old_hashes = self->hashes;
    Py_ssize_t old_capacity = self->capacity;
    
    // Calculate new size
    Py_ssize_t new_capacity = SWISS_GROUP_SIZE;
    while (new_capacity < min_size) new_capacity *= 2;
    
    // Allocate new arrays
    self->capacity = new_capacity;
    self->num_groups = new_capacity / SWISS_GROUP_SIZE;
    
    self->metadata = PyMem_Calloc(self->capacity, sizeof(uint8_t));
    self->keys = PyMem_Calloc(self->capacity, sizeof(PyObject*));
    self->values = PyMem_Calloc(self->capacity, sizeof(PyObject*));
    self->hashes = PyMem_Calloc(self->capacity, sizeof(Py_hash_t));
    
    if (!self->metadata || !self->keys || !self->values || !self->hashes) {
        PyMem_Free(self->metadata);
        PyMem_Free(self->keys);
        PyMem_Free(self->values);
        PyMem_Free(self->hashes);
        self->metadata = old_metadata;
        self->keys = old_keys;
        self->values = old_values;
        self->hashes = old_hashes;
        self->capacity = old_capacity;
        self->num_groups = old_capacity / SWISS_GROUP_SIZE;
        return PyErr_NoMemory(), -1;
    }
    
    // Initialize metadata with empty markers
    for (Py_ssize_t i = 0; i < self->capacity; i++) {
        self->metadata[i] = SWISS_EMPTY;
    }
    
    // Reinsert all entries
    for (Py_ssize_t i = 0; i < old_capacity; i++) {
        if (old_keys[i] != NULL) {
            if (insert_into_table(self, old_keys[i], old_values[i], old_hashes[i]) != 0) {
                // Clean up and return error
                PyMem_Free(self->metadata);
                PyMem_Free(self->keys);
                PyMem_Free(self->values);
                PyMem_Free(self->hashes);
                self->metadata = old_metadata;
                self->keys = old_keys;
                self->values = old_values;
                self->hashes = old_hashes;
                self->capacity = old_capacity;
                self->num_groups = old_capacity / SWISS_GROUP_SIZE;
                return -1;
            }
        }
    }
    
    PyMem_Free(old_metadata);
    PyMem_Free(old_keys);
    PyMem_Free(old_values);
    PyMem_Free(old_hashes);
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