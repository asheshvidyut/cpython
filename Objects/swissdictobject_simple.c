/*
 * CPython SwissDict Implementation - Abseil-inspired Optimized Version
 *
 * This implementation is inspired by Abseil's flat_hash_map with:
 * - SIMD-optimized group operations (16-byte groups)
 * - Better memory layout for cache locality
 * - Efficient hash fingerprinting
 * - Optimized probing strategy
 */

#include "Python.h"
#include <immintrin.h>  // For SIMD instructions

/* --- Data Structures --- */
#define SWISS_GROUP_SIZE 16
#define SWISS_EMPTY 0x80
#define SWISS_DELETED 0xFE
#define SWISS_H2_MASK 0x7F

// Group structure for better cache locality (inspired by Abseil)
typedef struct {
    uint8_t control[SWISS_GROUP_SIZE];  // Control bytes
    PyObject *keys[SWISS_GROUP_SIZE];   // Keys
    PyObject *values[SWISS_GROUP_SIZE]; // Values
    Py_hash_t hashes[SWISS_GROUP_SIZE]; // Hashes
} SwissGroup;

typedef struct {
    PyObject_HEAD
    Py_ssize_t used;
    Py_ssize_t capacity;
    uint32_t version;
    Py_ssize_t num_groups;
    SwissGroup *groups;  // Array of groups for better cache locality
} SwissDictObject;

/* --- Forward Declarations --- */
static int swissdict_resize(SwissDictObject *self, Py_ssize_t min_size);
static SwissDictEntry* find_entry(SwissDictObject *self, PyObject *key, Py_hash_t hash);
static int insert_into_table(SwissDictObject *self, PyObject *key, PyObject *value, Py_hash_t hash);

/* --- SIMD-optimized Group Operations (Abseil-inspired) --- */

static inline __m128i load_group_control(const SwissGroup *group) {
    return _mm_loadu_si128((const __m128i*)group->control);
}

static inline int find_match_in_group_simd(__m128i control, uint8_t h2) {
    // Create a vector with h2 repeated 16 times
    __m128i h2_vec = _mm_set1_epi8(h2);
    
    // Compare control word with h2
    __m128i match = _mm_cmpeq_epi8(control, h2_vec);
    
    // Get the mask of matching bytes
    return _mm_movemask_epi8(match);
}

static inline int find_empty_in_group_simd(__m128i control) {
    // Create a vector with SWISS_EMPTY repeated 16 times
    __m128i empty_vec = _mm_set1_epi8(SWISS_EMPTY);
    
    // Compare control word with empty marker
    __m128i match = _mm_cmpeq_epi8(control, empty_vec);
    
    // Get the mask of empty bytes
    return _mm_movemask_epi8(match);
}

static inline int find_available_in_group_simd(__m128i control) {
    // Create vectors for empty and deleted markers
    __m128i empty_vec = _mm_set1_epi8(SWISS_EMPTY);
    __m128i deleted_vec = _mm_set1_epi8(SWISS_DELETED);
    
    // Find empty or deleted slots
    __m128i empty_match = _mm_cmpeq_epi8(control, empty_vec);
    __m128i deleted_match = _mm_cmpeq_epi8(control, deleted_vec);
    __m128i available = _mm_or_si128(empty_match, deleted_match);
    
    return _mm_movemask_epi8(available);
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
    
    // Allocate groups array
    self->groups = PyMem_Calloc(self->num_groups, sizeof(SwissGroup));
    if (!self->groups) {
        Py_DECREF(self);
        return PyErr_NoMemory();
    }
    
    // Initialize first group with empty markers
    for (int i = 0; i < SWISS_GROUP_SIZE; i++) {
        self->groups[0].control[i] = SWISS_EMPTY;
        self->groups[0].keys[i] = NULL;
        self->groups[0].values[i] = NULL;
        self->groups[0].hashes[i] = 0;
    }
    
    return (PyObject *)self;
}

static void
swissdict_dealloc(SwissDictObject *self) {
    // Clean up all groups
    for (Py_ssize_t g = 0; g < self->num_groups; g++) {
        SwissGroup *group = &self->groups[g];
        for (int i = 0; i < SWISS_GROUP_SIZE; i++) {
            if (group->keys[i] != NULL) {
                Py_DECREF(group->keys[i]);
                Py_DECREF(group->values[i]);
            }
        }
    }
    
    PyMem_Free(self->groups);
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
    
    // Find the entry
    for (Py_ssize_t g = 0; g < self->num_groups; g++) {
        SwissGroup *group = &self->groups[g];
        Py_ssize_t h1 = hash % self->num_groups;
        uint8_t h2 = (hash >> 8) & SWISS_H2_MASK;
        
        // Load control word using SIMD
        __m128i control = load_group_control(group);
        
        // Find matches using SIMD
        int match_mask = find_match_in_group_simd(control, h2);
        
        // Process matches
        while (match_mask != 0) {
            int match_pos = __builtin_ctz(match_mask);
            
            // Fast path: pointer equality
            if (group->keys[match_pos] == key) {
                Py_INCREF(group->values[match_pos]);
                return group->values[match_pos];
            }
            
            // Slow path: hash and object comparison
            if (group->keys[match_pos] != NULL && 
                group->hashes[match_pos] == hash && 
                PyObject_RichCompareBool(group->keys[match_pos], key, Py_EQ) == 1) {
                Py_INCREF(group->values[match_pos]);
                return group->values[match_pos];
            }
            
            // Clear the lowest set bit
            match_mask &= match_mask - 1;
        }
        
        // Check for early exit (all empty)
        int empty_mask = find_empty_in_group_simd(control);
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
    
    // Check if key already exists
    for (Py_ssize_t g = 0; g < self->num_groups; g++) {
        SwissGroup *group = &self->groups[g];
        Py_ssize_t h1 = hash % self->num_groups;
        uint8_t h2 = (hash >> 8) & SWISS_H2_MASK;
        
        __m128i control = load_group_control(group);
        int match_mask = find_match_in_group_simd(control, h2);
        
        while (match_mask != 0) {
            int match_pos = __builtin_ctz(match_mask);
            
            if (group->keys[match_pos] == key || 
                (group->keys[match_pos] != NULL && 
                 group->hashes[match_pos] == hash && 
                 PyObject_RichCompareBool(group->keys[match_pos], key, Py_EQ) == 1)) {
                
                // Update existing entry
                PyObject *old_value = group->values[match_pos];
                Py_INCREF(value);
                group->values[match_pos] = value;
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

static int
insert_into_table(SwissDictObject *self, PyObject *key, PyObject *value, Py_hash_t hash) {
    Py_ssize_t h1 = hash % self->num_groups;
    uint8_t h2 = (hash >> 8) & SWISS_H2_MASK;
    
    for (Py_ssize_t g = 0; g < self->num_groups; g++) {
        SwissGroup *group = &self->groups[g];
        
        // Load control word using SIMD
        __m128i control = load_group_control(group);
        
        // Find available slots using SIMD
        int available_mask = find_available_in_group_simd(control);
        
        if (available_mask != 0) {
            int pos = __builtin_ctz(available_mask);
            
            // Set the entry
            Py_INCREF(key);
            Py_INCREF(value);
            group->keys[pos] = key;
            group->values[pos] = value;
            group->hashes[pos] = hash;
            group->control[pos] = h2;
            
            return 0;
        }
    }
    
    PyErr_SetString(PyExc_RuntimeError, "SwissDict: no space available for insertion");
    return -1;
}

static int
swissdict_resize(SwissDictObject *self, Py_ssize_t min_size) {
    SwissGroup *old_groups = self->groups;
    Py_ssize_t old_num_groups = self->num_groups;
    
    // Calculate new size
    Py_ssize_t new_capacity = SWISS_GROUP_SIZE;
    while (new_capacity < min_size) new_capacity *= 2;
    
    // Allocate new groups
    self->num_groups = new_capacity / SWISS_GROUP_SIZE;
    self->groups = PyMem_Calloc(self->num_groups, sizeof(SwissGroup));
    
    if (!self->groups) {
        self->groups = old_groups;
        self->num_groups = old_num_groups;
        return PyErr_NoMemory(), -1;
    }
    
    self->capacity = new_capacity;
    
    // Initialize new groups with empty markers
    for (Py_ssize_t g = 0; g < self->num_groups; g++) {
        for (int i = 0; i < SWISS_GROUP_SIZE; i++) {
            self->groups[g].control[i] = SWISS_EMPTY;
            self->groups[g].keys[i] = NULL;
            self->groups[g].values[i] = NULL;
            self->groups[g].hashes[i] = 0;
        }
    }
    
    // Reinsert all entries
    for (Py_ssize_t g = 0; g < old_num_groups; g++) {
        SwissGroup *old_group = &old_groups[g];
        for (int i = 0; i < SWISS_GROUP_SIZE; i++) {
            if (old_group->keys[i] != NULL) {
                if (insert_into_table(self, old_group->keys[i], old_group->values[i], old_group->hashes[i]) != 0) {
                    // Clean up and return error
                    PyMem_Free(self->groups);
                    self->groups = old_groups;
                    self->num_groups = old_num_groups;
                    self->capacity = old_num_groups * SWISS_GROUP_SIZE;
                    return -1;
                }
            }
        }
    }
    
    PyMem_Free(old_groups);
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