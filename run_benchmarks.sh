#!/bin/bash

# ==============================================================================
#  All-in-One Swiss Table Dictionary Benchmark Runner
# ==============================================================================
#
# This single script runs a Python dictionary performance benchmark against two
# different Python executables to compare their performance.
#
# --- PRE-REQUISITES ---
# You must have two Python executables compiled in the root of the cpython
# directory:
#
#    - `./python.old`: Compiled from the original, unmodified `main` branch.
#    - `./python.new`: Compiled from your branch with the Swiss Table changes.
#
# See comments in the script for build instructions if needed.
#

# --- Configuration ---
PYTHON_OLD="/Users/asheshvidyut/.virtualenvs/venv313/bin/python"
PYTHON_NEW="/Users/asheshvidyut/.virtualenvs/venv315/bin/python"

# --- Helper Functions ---
print_header() {
    echo "=================================================="
    echo "$1"
    echo "=================================================="
}

# --- Check for Executables ---
# (Instructions for building are included for clarity)
# To build python.old:
#   git checkout main && ./configure --with-pydebug && make -j && mv python python.old
# To build python.new:
#   git checkout your-branch && ./configure --with-pydebug && make -j && mv python python.new

if [ ! -f "$PYTHON_OLD" ]; then
    echo "Error: Old Python executable not found at '$PYTHON_OLD'"
    exit 1
fi

if [ ! -f "$PYTHON_NEW" ]; then
    echo "Error: New Python executable not found at '$PYTHON_NEW'"
    exit 1
fi

# --- Python Benchmark Code (embedded via heredoc) ---
read -r -d '' PYTHON_CODE <<'EOF'
import timeit
import gc

# Benchmark Configuration
SIZES = [1_000, 10_000, 100_000, 1_000_000]
NUMBER = 100

def run_benchmark(name, setup_code, test_code):
    """A helper to run and print timings for a benchmark."""
    print(f"  Running: {name:<25}", end="", flush=True)
    gc.collect()
    try:
        t = timeit.timeit(stmt=test_code, setup=setup_code, number=NUMBER)
        print(f"{t:.6f} seconds")
    except Exception as e:
        print(f"ERROR: {e}")

def main():
    for size in SIZES:
        print(f"\n--- Testing Dictionary Size: {size:,} ---")
        
        # Creation
        run_benchmark(
            "Dict Creation",
            f"SIZE={size}",
            "d = {i: i for i in range(SIZE)}"
        )
        # Successful Lookups
        run_benchmark(
            "Successful Lookups",
            f"SIZE={size}; d = {{i: i for i in range(SIZE)}}",
            "for i in range(SIZE): d[i]"
        )
        # Failed Lookups
        run_benchmark(
            "Failed Lookups",
            f"SIZE={size}; d = {{i: i for i in range(0, SIZE*2, 2)}}",
            "for i in range(1, SIZE*2, 2): \n  try: d[i] \n  except KeyError: pass"
        )
        # Insertion (Update)
        run_benchmark(
            "Insertion (Update)",
            f"SIZE={size}; d = {{i: i for i in range(SIZE)}}",
            "for i in range(SIZE): d[i] = i+1"
        )
        # Insertion (New)
        run_benchmark(
            "Insertion (New)",
            f"SIZE={size}; d = {{}}",
            "for i in range(SIZE): d[i] = i"
        )
        # Deletion
        run_benchmark(
            "Deletion",
            f"SIZE={size}; keys = list(range(SIZE)); d = {{k:k for k in keys}}",
            "for k in keys: del d[k]"
        )
        # Iteration
        run_benchmark(
            "Iteration (items())",
            f"SIZE={size}; d = {{i: i for i in range(SIZE)}}",
            "for k, v in d.items(): pass"
        )

if __name__ == "__main__":
    main()
EOF

# --- Main Execution ---

# Run benchmark with the OLD Python implementation
print_header "Running benchmarks with OLD Dictionary..."
echo "$PYTHON_CODE" | $PYTHON_OLD -
echo ""

# Run benchmark with the NEW Python implementation
print_header "Running benchmarks with NEW Swiss Table Dictionary..."
echo "$PYTHON_CODE" | $PYTHON_NEW -
echo ""

print_header "Benchmark Complete"
echo "Compare the timing results above. Lower 'seconds' values are better." 