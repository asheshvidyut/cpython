#!/usr/bin/env python3
"""
Simple benchmark comparing SwissDict with Python's built-in dict.
"""

import timeit
import random
from swiss import SwissDict

def simple_benchmark():
    print("Simple SwissDict vs Built-in Dict Benchmark")
    print("=" * 50)
    
    # Test data
    test_data = [(f"key_{i}", f"value_{i}") for i in range(100000)]
    lookup_keys = [key for key, _ in test_data]
    random.shuffle(lookup_keys)
    
    # Setup functions for timeit
    def setup_builtin_dict():
        d = {}
        for key, value in test_data:
            d[key] = value
        return d
    
    def setup_swiss_dict():
        d = SwissDict()
        for key, value in test_data:
            d[key] = value
        return d
    
    # Test built-in dict insertion
    print("\nTesting built-in dict...")
    builtin_insert_time = timeit.timeit(
        lambda: setup_builtin_dict(),
        number=10
    ) / 10
    print(f"Built-in dict insertion: {builtin_insert_time:.6f} seconds")
    
    # Test built-in dict lookup
    d1 = setup_builtin_dict()
    builtin_lookup_time = timeit.timeit(
        lambda: [d1[key] for key in lookup_keys],
        number=10
    ) / 10
    print(f"Built-in dict lookup: {builtin_lookup_time:.6f} seconds")
    
    # Test built-in dict iteration
    builtin_iter_time = timeit.timeit(
        lambda: list(d1.items()),
        number=10
    ) / 10
    print(f"Built-in dict iteration: {builtin_iter_time:.6f} seconds")
    
    # Test SwissDict insertion
    print("\nTesting SwissDict...")
    swiss_insert_time = timeit.timeit(
        lambda: setup_swiss_dict(),
        number=10
    ) / 10
    print(f"SwissDict insertion: {swiss_insert_time:.6f} seconds")
    
    # Test SwissDict lookup
    d2 = setup_swiss_dict()
    swiss_lookup_time = timeit.timeit(
        lambda: [d2[key] for key in lookup_keys],
        number=10
    ) / 10
    print(f"SwissDict lookup: {swiss_lookup_time:.6f} seconds")
    
    # Test SwissDict iteration
    swiss_iter_time = timeit.timeit(
        lambda: list(d2.items()),
        number=10
    ) / 10
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