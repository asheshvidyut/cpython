#!/usr/bin/env python3
"""
Simple Python Dictionary Benchmark
Quick performance tests for common dictionary operations
"""

import time
import random
import sys

def benchmark_operation(name, operation, iterations=10000):
    """Benchmark a single operation"""
    times = []
    
    for _ in range(iterations):
        start = time.perf_counter()
        result = operation()
        end = time.perf_counter()
        times.append(end - start)
    
    avg_time = sum(times) / len(times)
    print(f"{name}: {avg_time*1e6:.2f} μs per operation")
    return avg_time

def main():
    print("Simple Dictionary Benchmark")
    print("=" * 40)
    print(f"Python version: {sys.version}")
    print(f"Platform: {sys.platform}")
    print()
    
    # Test 1: Dictionary creation
    print("=== Creation Tests ===")
    benchmark_operation("Empty dict creation", lambda: {})
    benchmark_operation("Dict with 10 items", lambda: {i: i*2 for i in range(10)})
    benchmark_operation("Dict with 100 items", lambda: {i: i*2 for i in range(100)})
    benchmark_operation("Dict with 1000 items", lambda: {i: i*2 for i in range(1000)})
    
    # Test 2: Lookup performance
    print("\n=== Lookup Tests ===")
    d_small = {i: i*2 for i in range(100)}
    d_large = {i: i*2 for i in range(10000)}
    
    benchmark_operation("Small dict lookup (existing)", 
                       lambda: d_small[random.randint(0, 99)])
    benchmark_operation("Large dict lookup (existing)", 
                       lambda: d_large[random.randint(0, 9999)])
    benchmark_operation("Small dict lookup (missing)", 
                       lambda: d_small.get(random.randint(100, 200), None))
    benchmark_operation("Large dict lookup (missing)", 
                       lambda: d_large.get(random.randint(10000, 20000), None))
    
    # Test 3: String keys vs integer keys
    print("\n=== Key Type Tests ===")
    d_str = {f"key_{i}": i for i in range(1000)}
    d_int = {i: i for i in range(1000)}
    
    benchmark_operation("String key lookup", 
                       lambda: d_str[f"key_{random.randint(0, 999)}"])
    benchmark_operation("Integer key lookup", 
                       lambda: d_int[random.randint(0, 999)])
    
    # Test 4: Insertion performance
    print("\n=== Insertion Tests ===")
    benchmark_operation("Insert 100 new items", 
                       lambda: {i: i for i in range(100)})
    benchmark_operation("Insert 1000 new items", 
                       lambda: {i: i for i in range(1000)})
    
    # Test 5: Membership testing
    print("\n=== Membership Tests ===")
    d = {i: i for i in range(1000)}
    keys = list(d.keys())
    missing_keys = [f"missing_{i}" for i in range(1000)]
    
    benchmark_operation("Membership test (existing)", 
                       lambda: random.choice(keys) in d)
    benchmark_operation("Membership test (missing)", 
                       lambda: random.choice(missing_keys) in d)
    
    # Test 6: Dictionary methods
    print("\n=== Method Tests ===")
    d = {i: i for i in range(100)}
    
    benchmark_operation("copy() method", lambda: d.copy())
    benchmark_operation("keys() method", lambda: list(d.keys()))
    benchmark_operation("values() method", lambda: list(d.values()))
    benchmark_operation("items() method", lambda: list(d.items()))
    
    # Test 7: Comparison with list
    print("\n=== Data Structure Comparison ===")
    size = 1000
    lst = list(range(size))
    d = {i: i for i in range(size)}
    
    list_time = benchmark_operation("List membership (existing)", 
                                   lambda: random.randint(0, size-1) in lst)
    dict_time = benchmark_operation("Dict membership (existing)", 
                                   lambda: random.randint(0, size-1) in d)
    
    speedup = list_time / dict_time
    print(f"\nDictionary is {speedup:.1f}x faster than list for membership testing")
    
    # Test 8: Resizing behavior
    print("\n=== Resizing Tests ===")
    benchmark_operation("Add items (with resizing)", 
                       lambda: {i: i for i in range(1000)})
    benchmark_operation("Pre-sized dict", 
                       lambda: dict.fromkeys(range(1000), None))
    
    # Test 9: Iteration performance
    print("\n=== Iteration Tests ===")
    d = {i: i for i in range(1000)}
    
    benchmark_operation("Iterate over dict", 
                       lambda: [k for k in d])
    benchmark_operation("Iterate over keys", 
                       lambda: list(d.keys()))
    benchmark_operation("Iterate over values", 
                       lambda: list(d.values()))
    benchmark_operation("Iterate over items", 
                       lambda: list(d.items()))
    
    print("\n" + "=" * 40)
    print("Benchmark completed!")

if __name__ == "__main__":
    main() 