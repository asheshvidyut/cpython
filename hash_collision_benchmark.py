#!/usr/bin/env python3
"""
Hash Collision Dictionary Benchmark
Tests dictionary performance under hash collision scenarios
"""

import time
import random
import sys
from collections import defaultdict

class CollisionKey:
    """A key class that always hashes to the same value (causing collisions)"""
    def __init__(self, value):
        self.value = value
    
    def __hash__(self):
        return 42  # Always return the same hash value
    
    def __eq__(self, other):
        return isinstance(other, CollisionKey) and self.value == other.value
    
    def __repr__(self):
        return f"CollisionKey({self.value})"

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

def create_collision_dict(size):
    """Create a dictionary with keys that all hash to the same value"""
    d = {}
    for i in range(size):
        d[CollisionKey(i)] = f"value_{i}"
    return d

def create_normal_dict(size):
    """Create a dictionary with normal keys"""
    d = {}
    for i in range(size):
        d[f"key_{i}"] = f"value_{i}"
    return d

def main():
    print("Hash Collision Dictionary Benchmark")
    print("=" * 50)
    print(f"Python version: {sys.version}")
    print(f"Platform: {sys.platform}")
    print()
    
    # Test 1: Normal vs Collision dictionaries
    print("=== Normal vs Collision Dictionaries ===")
    
    sizes = [10, 50, 100, 500, 1000]
    
    for size in sizes:
        print(f"\nDictionary size: {size}")
        
        # Create dictionaries
        normal_dict = create_normal_dict(size)
        collision_dict = create_collision_dict(size)
        
        # Test lookup performance
        normal_keys = list(normal_dict.keys())
        collision_keys = list(collision_dict.keys())
        
        normal_time = benchmark_operation(f"Normal dict lookup ({size} items)", 
                                        lambda: normal_dict[random.choice(normal_keys)])
        collision_time = benchmark_operation(f"Collision dict lookup ({size} items)", 
                                          lambda: collision_dict[random.choice(collision_keys)])
        
        slowdown = collision_time / normal_time
        print(f"Collision dict is {slowdown:.1f}x slower than normal dict")
    
    # Test 2: Insertion with collisions
    print("\n=== Insertion with Collisions ===")
    
    for size in [10, 50, 100, 500]:
        print(f"\nInserting {size} items:")
        
        # Normal insertion
        normal_time = benchmark_operation("Normal dict insertion", 
                                        lambda: create_normal_dict(size))
        
        # Collision insertion
        collision_time = benchmark_operation("Collision dict insertion", 
                                          lambda: create_collision_dict(size))
        
        slowdown = collision_time / normal_time
        print(f"Collision insertion is {slowdown:.1f}x slower than normal insertion")
    
    # Test 3: Membership testing with collisions
    print("\n=== Membership Testing with Collisions ===")
    
    size = 100
    normal_dict = create_normal_dict(size)
    collision_dict = create_collision_dict(size)
    
    normal_keys = list(normal_dict.keys())
    collision_keys = list(collision_dict.keys())
    
    normal_time = benchmark_operation("Normal dict membership", 
                                    lambda: random.choice(normal_keys) in normal_dict)
    collision_time = benchmark_operation("Collision dict membership", 
                                      lambda: random.choice(collision_keys) in collision_dict)
    
    slowdown = collision_time / normal_time
    print(f"Collision membership test is {slowdown:.1f}x slower than normal")
    
    # Test 4: Deletion with collisions
    print("\n=== Deletion with Collisions ===")
    
    size = 100
    normal_dict = create_normal_dict(size)
    collision_dict = create_collision_dict(size)
    
    normal_keys = list(normal_dict.keys())
    collision_keys = list(collision_dict.keys())
    
    def delete_from_normal():
        d = normal_dict.copy()
        for key in normal_keys[:10]:
            del d[key]
        return d
    
    def delete_from_collision():
        d = collision_dict.copy()
        for key in collision_keys[:10]:
            del d[key]
        return d
    
    normal_time = benchmark_operation("Normal dict deletion", delete_from_normal)
    collision_time = benchmark_operation("Collision dict deletion", delete_from_collision)
    
    slowdown = collision_time / normal_time
    print(f"Collision deletion is {slowdown:.1f}x slower than normal deletion")
    
    # Test 5: Iteration with collisions
    print("\n=== Iteration with Collisions ===")
    
    size = 1000
    normal_dict = create_normal_dict(size)
    collision_dict = create_collision_dict(size)
    
    normal_time = benchmark_operation("Normal dict iteration", 
                                    lambda: list(normal_dict.keys()))
    collision_time = benchmark_operation("Collision dict iteration", 
                                      lambda: list(collision_dict.keys()))
    
    slowdown = collision_time / normal_time
    print(f"Collision iteration is {slowdown:.1f}x slower than normal iteration")
    
    # Test 6: Hash distribution analysis
    print("\n=== Hash Distribution Analysis ===")
    
    # Test with different hash functions
    class BadHashKey:
        def __init__(self, value):
            self.value = value
        
        def __hash__(self):
            return self.value % 10  # Only 10 different hash values
        
        def __eq__(self, other):
            return isinstance(other, BadHashKey) and self.value == other.value
    
    # Create dictionaries with poor hash distribution
    sizes = [50, 100, 500]
    
    for size in sizes:
        print(f"\nDictionary with poor hash distribution (size: {size}):")
        
        # Create dict with poor hash distribution
        poor_dict = {}
        for i in range(size):
            poor_dict[BadHashKey(i)] = f"value_{i}"
        
        keys = list(poor_dict.keys())
        
        lookup_time = benchmark_operation("Poor hash lookup", 
                                        lambda: poor_dict[random.choice(keys)])
        
        # Compare with normal dict of same size
        normal_dict = create_normal_dict(size)
        normal_keys = list(normal_dict.keys())
        normal_time = benchmark_operation("Normal hash lookup", 
                                        lambda: normal_dict[random.choice(normal_keys)])
        
        slowdown = lookup_time / normal_time
        print(f"Poor hash distribution is {slowdown:.1f}x slower than normal")
    
    print("\n" + "=" * 50)
    print("Hash Collision Benchmark completed!")
    print("\nKey Findings:")
    print("- Hash collisions significantly impact dictionary performance")
    print("- Lookup time degrades from O(1) to O(n) with many collisions")
    print("- Insertion and deletion also suffer from collisions")
    print("- Python's hash table uses linear probing for collision resolution")

if __name__ == "__main__":
    main() 