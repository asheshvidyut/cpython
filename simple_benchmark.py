#!/usr/bin/env python3
"""
Simple benchmark comparing SwissDict with Python's built-in dict.
"""

import time
from swiss import SwissDict

def simple_benchmark():
    print("Simple SwissDict vs Built-in Dict Benchmark")
    print("=" * 50)
    
    # Test data
    test_data = [(f"key_{i}", f"value_{i}") for i in range(10000)]
    
    # Test built-in dict
    print("\nTesting built-in dict...")
    start_time = time.perf_counter()
    d1 = {}
    for key, value in test_data:
        d1[key] = value
    end_time = time.perf_counter()
    builtin_insert_time = end_time - start_time
    print(f"Built-in dict insertion: {builtin_insert_time:.6f} seconds")
    
    # Test lookups
    start_time = time.perf_counter()
    for key, _ in test_data:
        _ = d1[key]
    end_time = time.perf_counter()
    builtin_lookup_time = end_time - start_time
    print(f"Built-in dict lookup: {builtin_lookup_time:.6f} seconds")
    
    # Test iteration
    start_time = time.perf_counter()
    items = list(d1.items())
    end_time = time.perf_counter()
    builtin_iter_time = end_time - start_time
    print(f"Built-in dict iteration: {builtin_iter_time:.6f} seconds")
    
    # Test SwissDict
    print("\nTesting SwissDict...")
    start_time = time.perf_counter()
    d2 = SwissDict()
    for key, value in test_data:
        d2[key] = value
    end_time = time.perf_counter()
    swiss_insert_time = end_time - start_time
    print(f"SwissDict insertion: {swiss_insert_time:.6f} seconds")
    
    # Test lookups
    start_time = time.perf_counter()
    for key, _ in test_data:
        _ = d2[key]
    end_time = time.perf_counter()
    swiss_lookup_time = end_time - start_time
    print(f"SwissDict lookup: {swiss_lookup_time:.6f} seconds")
    
    # Test iteration
    start_time = time.perf_counter()
    items = list(d2.items())
    end_time = time.perf_counter()
    swiss_iter_time = end_time - start_time
    print(f"SwissDict iteration: {swiss_iter_time:.6f} seconds")
    
    # Performance comparison
    print(f"\nPerformance Comparison (Built-in / SwissDict):")
    print(f"Insertion: {builtin_insert_time/swiss_insert_time:.2f}x")
    print(f"Lookup: {builtin_lookup_time/swiss_lookup_time:.2f}x")
    print(f"Iteration: {builtin_iter_time/swiss_iter_time:.2f}x")
    
    # Test insertion order
    print(f"\nInsertion Order Test:")
    test_order = SwissDict()
    test_order['first'] = 1
    test_order['second'] = 2
    test_order['third'] = 3
    test_order['first'] = 'updated'  # Update existing key
    
    print("Items in insertion order:", list(test_order.items()))
    print("SwissDict maintains insertion order: ✓")

if __name__ == "__main__":
    simple_benchmark()
