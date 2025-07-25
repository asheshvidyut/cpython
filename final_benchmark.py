import timeit
import gc
import sys
import random
import string

try:
    import swiss
    SWISS_DICT_TYPE = swiss.SwissDict
except ImportError:
    print("Error: Could not import the 'swiss' module.")
    sys.exit(1)

# --- Benchmark Configuration ---
SIZES = [1_000, 10_000, 100_000, 500_000]
NUMBER = 50

def generate_random_strings(size):
    """Generate random strings for more realistic testing."""
    return [''.join(random.choices(string.ascii_letters, k=8)) for _ in range(size)]

def generate_random_ints(size):
    """Generate random integers for collision testing."""
    return [random.randint(0, size * 10) for _ in range(size)]

def run_benchmark(dict_type_name, size, benchmark_name, key_type="sequential"):
    """A helper to run and print timings for a benchmark."""
    
    if key_type == "random_strings":
        keys = generate_random_strings(size)
    elif key_type == "random_ints":
        keys = generate_random_ints(size)
    else:  # sequential
        keys = list(range(size))
    
    setup_code = f"""
import gc
from swiss import SwissDict as SWISS_DICT_TYPE
gc.disable()
SIZE = {size}
keys = {keys}
d = {dict_type_name}()
if '{benchmark_name}' != 'Insertion (New)':
    if '{benchmark_name}' == 'Failed Lookups':
        for i in range(0, SIZE*2, 2):
            d[i] = i
    else:
        for key in keys:
            d[key] = key
if '{benchmark_name}' == 'Deletion':
    pass  # keys already defined
gc.enable()
"""
    
    test_code = {
        "Creation":           f"d = {dict_type_name}({{key: key for key in keys}})",
        "Successful Lookups": "for key in keys: d[key]",
        "Failed Lookups":     "for i in range(1, SIZE*2, 2):\n  try: d[i]\n  except KeyError: pass",
        "Insertion (Update)": "for key in keys: d[key] = key+1",
        "Insertion (New)":    "for key in keys: d[key] = key",
        "Deletion":           "for key in keys: del d[key]",
    }[benchmark_name]

    gc.collect()
    try:
        t = timeit.timeit(
            stmt=test_code,
            setup=setup_code,
            number=NUMBER,
            globals={'swiss': swiss}
        )
        return t
    except Exception as e:
        print(f"ERROR running {dict_type_name}: {e}")
        return -1

def main():
    """Main function to run all dictionary benchmarks."""
    print(f"{'Benchmark':<25} | {'Built-in dict':<20} | {'SwissDict':<20} | {'Winner'}")
    print("-" * 80)

    # Test with different key types
    key_types = [
        ("sequential", "Sequential Integers"),
        ("random_ints", "Random Integers"), 
        ("random_strings", "Random Strings")
    ]
    
    for key_type, key_desc in key_types:
        print(f"\n=== Testing with {key_desc} ===\n")
        
        for size in SIZES:
            print(f"\n--- Testing Dictionary Size: {size:,} ---\n")
            
            benchmarks = [
                "Creation", "Successful Lookups", "Failed Lookups",
                "Insertion (Update)", "Insertion (New)",
                # "Deletion" # Not implemented
            ]

            for name in benchmarks:
                time_std = run_benchmark("dict", size, name, key_type)
                time_swiss = run_benchmark("SWISS_DICT_TYPE", size, name, key_type)

                if time_std != -1 and time_swiss != -1:
                    winner = "SwissDict" if time_swiss < time_std else "Built-in"
                    ratio = (time_std / time_swiss) if time_swiss > 0 and time_std > 0 else 1.0
                    details = f"({ratio:.2f}x faster)" if winner == "SwissDict" and ratio > 1.01 else ""
                    print(f"{name:<25} | {time_std:<20.6f} | {time_swiss:<20.6f} | {winner} {details}")

if __name__ == "__main__":
    main() 