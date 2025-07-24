#!/usr/bin/env python3
"""
Comprehensive Python Dictionary Benchmark
Tests various dictionary operations and scenarios
"""

import time
import random
import statistics
from collections import defaultdict
import sys
import gc

class DictBenchmark:
    def __init__(self, iterations=1000):
        self.iterations = iterations
        self.results = {}
        
    def time_function(self, func, *args, **kwargs):
        """Time a function execution"""
        gc.collect()  # Clean up before timing
        start = time.perf_counter()
        result = func(*args, **kwargs)
        end = time.perf_counter()
        return end - start, result
    
    def benchmark_creation(self):
        """Benchmark dictionary creation with different methods"""
        print("\n=== Dictionary Creation Benchmarks ===")
        
        # Empty dict creation
        times = []
        for _ in range(self.iterations):
            t, _ = self.time_function(lambda: {})
            times.append(t)
        self.results['empty_dict_creation'] = statistics.mean(times)
        print(f"Empty dict creation: {statistics.mean(times)*1e6:.2f} μs")
        
        # Dict comprehension
        times = []
        for _ in range(self.iterations):
            t, _ = self.time_function(lambda: {i: i*2 for i in range(100)})
            times.append(t)
        self.results['dict_comprehension_100'] = statistics.mean(times)
        print(f"Dict comprehension (100 items): {statistics.mean(times)*1e6:.2f} μs")
        
        # Dict constructor with list of tuples
        items = [(i, i*2) for i in range(100)]
        times = []
        for _ in range(self.iterations):
            t, _ = self.time_function(lambda: dict(items))
            times.append(t)
        self.results['dict_constructor_100'] = statistics.mean(times)
        print(f"Dict constructor (100 items): {statistics.mean(times)*1e6:.2f} μs")
        
        # Dict with keyword arguments
        times = []
        for _ in range(self.iterations):
            t, _ = self.time_function(lambda: dict(a=1, b=2, c=3, d=4, e=5))
            times.append(t)
        self.results['dict_keywords'] = statistics.mean(times)
        print(f"Dict with keywords: {statistics.mean(times)*1e6:.2f} μs")
    
    def benchmark_lookup(self):
        """Benchmark dictionary lookup operations"""
        print("\n=== Dictionary Lookup Benchmarks ===")
        
        # String keys
        d = {f"key_{i}": i for i in range(1000)}
        keys = list(d.keys())
        random.shuffle(keys)
        
        # Successful lookups
        times = []
        for _ in range(self.iterations):
            key = random.choice(keys)
            t, _ = self.time_function(lambda: d[key])
            times.append(t)
        self.results['string_lookup_success'] = statistics.mean(times)
        print(f"String key lookup (success): {statistics.mean(times)*1e6:.2f} μs")
        
        # Failed lookups
        times = []
        for _ in range(self.iterations):
            key = f"missing_key_{random.randint(1000, 2000)}"
            try:
                t, _ = self.time_function(lambda: d[key])
            except KeyError:
                t = 0  # We're timing the exception handling
            times.append(t)
        self.results['string_lookup_fail'] = statistics.mean(times)
        print(f"String key lookup (fail): {statistics.mean(times)*1e6:.2f} μs")
        
        # Integer keys
        d = {i: i*2 for i in range(1000)}
        keys = list(d.keys())
        random.shuffle(keys)
        
        times = []
        for _ in range(self.iterations):
            key = random.choice(keys)
            t, _ = self.time_function(lambda: d[key])
            times.append(t)
        self.results['int_lookup_success'] = statistics.mean(times)
        print(f"Integer key lookup (success): {statistics.mean(times)*1e6:.2f} μs")
        
        # get() method
        times = []
        for _ in range(self.iterations):
            key = random.choice(keys)
            t, _ = self.time_function(lambda: d.get(key))
            times.append(t)
        self.results['get_method'] = statistics.mean(times)
        print(f"get() method: {statistics.mean(times)*1e6:.2f} μs")
        
        # get() with default
        times = []
        for _ in range(self.iterations):
            key = f"missing_key_{random.randint(1000, 2000)}"
            t, _ = self.time_function(lambda: d.get(key, "default"))
            times.append(t)
        self.results['get_with_default'] = statistics.mean(times)
        print(f"get() with default: {statistics.mean(times)*1e6:.2f} μs")
    
    def benchmark_insertion(self):
        """Benchmark dictionary insertion operations"""
        print("\n=== Dictionary Insertion Benchmarks ===")
        
        # Insert new keys
        times = []
        for _ in range(self.iterations):
            d = {}
            t, _ = self.time_function(lambda: [d.__setitem__(f"key_{i}", i) for i in range(100)])
            times.append(t)
        self.results['insert_new_keys'] = statistics.mean(times)
        print(f"Insert 100 new keys: {statistics.mean(times)*1e6:.2f} μs")
        
        # Update existing keys
        d = {f"key_{i}": i for i in range(100)}
        times = []
        for _ in range(self.iterations):
            d_copy = d.copy()
            t, _ = self.time_function(lambda: [d_copy.__setitem__(f"key_{i}", i*2) for i in range(100)])
            times.append(t)
        self.results['update_existing_keys'] = statistics.mean(times)
        print(f"Update 100 existing keys: {statistics.mean(times)*1e6:.2f} μs")
        
        # Mixed insertions (some new, some existing)
        times = []
        for _ in range(self.iterations):
            d = {f"key_{i}": i for i in range(50)}
            t, _ = self.time_function(lambda: [d.__setitem__(f"key_{i}", i*2) for i in range(100)])
            times.append(t)
        self.results['mixed_insertions'] = statistics.mean(times)
        print(f"Mixed insertions (50 existing + 50 new): {statistics.mean(times)*1e6:.2f} μs")
    
    def benchmark_deletion(self):
        """Benchmark dictionary deletion operations"""
        print("\n=== Dictionary Deletion Benchmarks ===")
        
        # Delete existing keys
        times = []
        for _ in range(self.iterations):
            d = {f"key_{i}": i for i in range(100)}
            keys = list(d.keys())
            random.shuffle(keys)
            t, _ = self.time_function(lambda: [d.__delitem__(key) for key in keys[:50]])
            times.append(t)
        self.results['delete_existing_keys'] = statistics.mean(times)
        print(f"Delete 50 existing keys: {statistics.mean(times)*1e6:.2f} μs")
        
        # Delete non-existent keys (with exception handling)
        times = []
        for _ in range(self.iterations):
            d = {f"key_{i}": i for i in range(100)}
            t, _ = self.time_function(lambda: [d.pop(f"missing_key_{i}", None) for i in range(50)])
            times.append(t)
        self.results['delete_missing_keys'] = statistics.mean(times)
        print(f"Delete 50 missing keys (with pop): {statistics.mean(times)*1e6:.2f} μs")
    
    def benchmark_iteration(self):
        """Benchmark dictionary iteration operations"""
        print("\n=== Dictionary Iteration Benchmarks ===")
        
        d = {f"key_{i}": i for i in range(1000)}
        
        # Iterate over keys
        times = []
        for _ in range(self.iterations):
            t, _ = self.time_function(lambda: list(d.keys()))
            times.append(t)
        self.results['iterate_keys'] = statistics.mean(times)
        print(f"Iterate over keys (1000 items): {statistics.mean(times)*1e6:.2f} μs")
        
        # Iterate over values
        times = []
        for _ in range(self.iterations):
            t, _ = self.time_function(lambda: list(d.values()))
            times.append(t)
        self.results['iterate_values'] = statistics.mean(times)
        print(f"Iterate over values (1000 items): {statistics.mean(times)*1e6:.2f} μs")
        
        # Iterate over items
        times = []
        for _ in range(self.iterations):
            t, _ = self.time_function(lambda: list(d.items()))
            times.append(t)
        self.results['iterate_items'] = statistics.mean(times)
        print(f"Iterate over items (1000 items): {statistics.mean(times)*1e6:.2f} μs")
        
        # Direct iteration
        times = []
        for _ in range(self.iterations):
            t, _ = self.time_function(lambda: [k for k in d])
            times.append(t)
        self.results['direct_iteration'] = statistics.mean(times)
        print(f"Direct iteration (1000 items): {statistics.mean(times)*1e6:.2f} μs")
    
    def benchmark_membership(self):
        """Benchmark membership testing"""
        print("\n=== Dictionary Membership Benchmarks ===")
        
        d = {f"key_{i}": i for i in range(1000)}
        keys = list(d.keys())
        random.shuffle(keys)
        
        # Test existing keys
        times = []
        for _ in range(self.iterations):
            key = random.choice(keys)
            t, _ = self.time_function(lambda: key in d)
            times.append(t)
        self.results['membership_existing'] = statistics.mean(times)
        print(f"Membership test (existing key): {statistics.mean(times)*1e6:.2f} μs")
        
        # Test non-existing keys
        times = []
        for _ in range(self.iterations):
            key = f"missing_key_{random.randint(1000, 2000)}"
            t, _ = self.time_function(lambda: key in d)
            times.append(t)
        self.results['membership_missing'] = statistics.mean(times)
        print(f"Membership test (missing key): {statistics.mean(times)*1e6:.2f} μs")
    
    def benchmark_resizing(self):
        """Benchmark dictionary resizing behavior"""
        print("\n=== Dictionary Resizing Benchmarks ===")
        
        # Measure time to add items that trigger resizing
        times = []
        for _ in range(self.iterations):
            d = {}
            t, _ = self.time_function(lambda: [d.__setitem__(i, i*2) for i in range(1000)])
            times.append(t)
        self.results['resizing_1000_items'] = statistics.mean(times)
        print(f"Add 1000 items (with resizing): {statistics.mean(times)*1e6:.2f} μs")
        
        # Pre-sized dictionary
        times = []
        for _ in range(self.iterations):
            d = dict.fromkeys(range(1000), None)  # Pre-allocate
            t, _ = self.time_function(lambda: [d.__setitem__(i, i*2) for i in range(1000)])
            times.append(t)
        self.results['presized_1000_items'] = statistics.mean(times)
        print(f"Add 1000 items (pre-sized): {statistics.mean(times)*1e6:.2f} μs")
    
    def benchmark_methods(self):
        """Benchmark dictionary methods"""
        print("\n=== Dictionary Methods Benchmarks ===")
        
        d = {f"key_{i}": i for i in range(100)}
        
        # copy()
        times = []
        for _ in range(self.iterations):
            t, _ = self.time_function(lambda: d.copy())
            times.append(t)
        self.results['copy_method'] = statistics.mean(times)
        print(f"copy() method: {statistics.mean(times)*1e6:.2f} μs")
        
        # clear()
        times = []
        for _ in range(self.iterations):
            d_copy = d.copy()
            t, _ = self.time_function(lambda: d_copy.clear())
            times.append(t)
        self.results['clear_method'] = statistics.mean(times)
        print(f"clear() method: {statistics.mean(times)*1e6:.2f} μs")
        
        # update()
        update_dict = {f"update_key_{i}": i*2 for i in range(50)}
        times = []
        for _ in range(self.iterations):
            d_copy = d.copy()
            t, _ = self.time_function(lambda: d_copy.update(update_dict))
            times.append(t)
        self.results['update_method'] = statistics.mean(times)
        print(f"update() method: {statistics.mean(times)*1e6:.2f} μs")
        
        # setdefault()
        times = []
        for _ in range(self.iterations):
            d_copy = d.copy()
            t, _ = self.time_function(lambda: [d_copy.setdefault(f"key_{i}", i*2) for i in range(100)])
            times.append(t)
        self.results['setdefault_method'] = statistics.mean(times)
        print(f"setdefault() method: {statistics.mean(times)*1e6:.2f} μs")
    
    def benchmark_comparison(self):
        """Compare with other data structures"""
        print("\n=== Data Structure Comparison ===")
        
        # List vs Dict lookup
        size = 1000
        lst = list(range(size))
        d = {i: i for i in range(size)}
        
        # List lookup (linear search)
        times = []
        for _ in range(self.iterations):
            item = random.randint(0, size-1)
            t, _ = self.time_function(lambda: item in lst)
            times.append(t)
        list_lookup = statistics.mean(times)
        print(f"List membership test: {list_lookup*1e6:.2f} μs")
        
        # Dict lookup (hash table)
        times = []
        for _ in range(self.iterations):
            item = random.randint(0, size-1)
            t, _ = self.time_function(lambda: item in d)
            times.append(t)
        dict_lookup = statistics.mean(times)
        print(f"Dict membership test: {dict_lookup*1e6:.2f} μs")
        
        speedup = list_lookup / dict_lookup
        print(f"Dict is {speedup:.1f}x faster than list for membership testing")
    
    def run_all_benchmarks(self):
        """Run all benchmarks"""
        print("Python Dictionary Performance Benchmark")
        print("=" * 50)
        print(f"Python version: {sys.version}")
        print(f"Iterations per test: {self.iterations}")
        print(f"Platform: {sys.platform}")
        
        self.benchmark_creation()
        self.benchmark_lookup()
        self.benchmark_insertion()
        self.benchmark_deletion()
        self.benchmark_iteration()
        self.benchmark_membership()
        self.benchmark_resizing()
        self.benchmark_methods()
        self.benchmark_comparison()
        
        print("\n" + "=" * 50)
        print("Benchmark Summary")
        print("=" * 50)
        
        # Find fastest and slowest operations
        sorted_results = sorted(self.results.items(), key=lambda x: x[1])
        print(f"Fastest operation: {sorted_results[0][0]} ({sorted_results[0][1]*1e6:.2f} μs)")
        print(f"Slowest operation: {sorted_results[-1][0]} ({sorted_results[-1][1]*1e6:.2f} μs)")
        
        return self.results

def main():
    """Main function to run benchmarks"""
    benchmark = DictBenchmark(iterations=1000)
    results = benchmark.run_all_benchmarks()
    
    # Save results to file
    with open('dict_benchmark_results.txt', 'w') as f:
        f.write("Python Dictionary Benchmark Results\n")
        f.write("=" * 50 + "\n")
        f.write(f"Python version: {sys.version}\n")
        f.write(f"Platform: {sys.platform}\n\n")
        
        for operation, time_taken in sorted(results.items()):
            f.write(f"{operation}: {time_taken*1e6:.2f} μs\n")
    
    print(f"\nResults saved to dict_benchmark_results.txt")

if __name__ == "__main__":
    main() 