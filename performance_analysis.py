#!/usr/bin/env python3
"""
Performance Analysis for SwissDict vs Built-in Dict
Analyzes specific bottlenecks in lookup and iteration
"""

import time
import swiss

def analyze_lookup_performance():
    print("=== Lookup Performance Analysis ===")
    
    sizes = [1000, 10000, 100000, 500000, 1000000]
    
    for size in sizes:
        print(f"\nTesting with {size:,} items:")
        
        # Create test data
        test_data = [(f"key_{i}", f"value_{i}") for i in range(size)]
        
        # Test built-in dict
        d1 = {}
        for k, v in test_data:
            d1[k] = v
        
        # Test SwissDict
        d2 = swiss.SwissDict()
        for k, v in test_data:
            d2[k] = v
        
        # Test lookup performance
        lookup_keys = [f"key_{i}" for i in range(0, size, size // 100)]  # Sample 100 keys
        
        # Built-in dict lookup
        start = time.perf_counter()
        for key in lookup_keys:
            _ = d1[key]
        builtin_time = time.perf_counter() - start
        
        # SwissDict lookup
        start = time.perf_counter()
        for key in lookup_keys:
            _ = d2[key]
        swiss_time = time.perf_counter() - start
        
        ratio = builtin_time / swiss_time if swiss_time > 0 else float('inf')
        print(f"  Lookup: Built-in {builtin_time:.6f}s, SwissDict {swiss_time:.6f}s, Ratio: {ratio:.2f}x")

def analyze_iteration_performance():
    print("\n=== Iteration Performance Analysis ===")
    
    sizes = [1000, 10000, 100000, 500000, 1000000]
    
    for size in sizes:
        print(f"\nTesting with {size:,} items:")
        
        # Create test data
        test_data = [(f"key_{i}", f"value_{i}") for i in range(size)]
        
        # Test built-in dict
        d1 = {}
        for k, v in test_data:
            d1[k] = v
        
        # Test SwissDict
        d2 = swiss.SwissDict()
        for k, v in test_data:
            d2[k] = v
        
        # Test iteration performance
        # Built-in dict iteration
        start = time.perf_counter()
        for k, v in d1.items():
            pass
        builtin_time = time.perf_counter() - start
        
        # SwissDict iteration
        start = time.perf_counter()
        for k, v in d2.items():
            pass
        swiss_time = time.perf_counter() - start
        
        ratio = builtin_time / swiss_time if swiss_time > 0 else float('inf')
        print(f"  Iteration: Built-in {builtin_time:.6f}s, SwissDict {swiss_time:.6f}s, Ratio: {ratio:.2f}x")

def analyze_memory_usage():
    print("\n=== Memory Usage Analysis ===")
    
    import sys
    
    # Test with 100,000 items
    size = 100000
    test_data = [(f"key_{i}", f"value_{i}") for i in range(size)]
    
    # Built-in dict
    d1 = {}
    for k, v in test_data:
        d1[k] = v
    
    # SwissDict
    d2 = swiss.SwissDict()
    for k, v in test_data:
        d2[k] = v
    
    print(f"Built-in dict size: {sys.getsizeof(d1):,} bytes")
    print(f"SwissDict size: {sys.getsizeof(d2):,} bytes")
    
    # Estimate memory usage
    estimated_swiss = (
        sys.getsizeof(d2) +  # Base object
        size * 8 +           # insertion_order array (Py_ssize_t)
        size * 1 +           # ctrl array (uint8_t)
        size * 8 +           # keys array (PyObject*)
        size * 8 +           # values array (PyObject*)
        size * 8             # hashes array (Py_hash_t)
    )
    print(f"Estimated SwissDict memory: {estimated_swiss:,} bytes")

if __name__ == "__main__":
    analyze_lookup_performance()
    analyze_iteration_performance()
    analyze_memory_usage()
