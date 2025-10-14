#!/bin/bash
# Build script for SwissDict module

set -e  # Exit on any error

echo "Building SwissDict module..."
echo "============================"

# Check if we're in the CPython directory
if [ ! -f "configure" ] || [ ! -f "Makefile.pre.in" ]; then
    echo "Error: This script must be run from the CPython source directory"
    exit 1
fi

# Create Modules/Setup.local if it doesn't exist
if [ ! -f "Modules/Setup.local" ]; then
    echo "Creating Modules/Setup.local..."
    echo "# SwissDict module configuration" > Modules/Setup.local
    echo "swiss Modules/swissmodule.c" >> Modules/Setup.local
else
    # Check if swiss is already in Setup.local
    if ! grep -q "swiss Modules/swissmodule.c" Modules/Setup.local; then
        echo "Adding swiss module to Modules/Setup.local..."
        echo "swiss Modules/swissmodule.c" >> Modules/Setup.local
    else
        echo "SwissDict already configured in Modules/Setup.local"
    fi
fi

# Clean previous builds
echo "Cleaning previous builds..."
make clean 2>/dev/null || true

# Configure
echo "Configuring..."
./configure --enable-optimizations

# Build
echo "Building..."
make -j$(nproc)

# Check if the module was built
echo "Checking if SwissDict module was built..."
if find . -name "swiss*.so" | grep -q .; then
    echo "✓ SwissDict module built successfully"
    find . -name "swiss*.so" -exec ls -la {} \;
else
    echo "✗ SwissDict module not found after build"
    echo "Checking build logs for errors..."
    echo "You may need to check the build output for compilation errors."
fi

# Install (optional)
echo "Installing..."
sudo make install

echo "Checking installation..."
if find /usr/local/lib/python3.*/ -name "swiss*.so" 2>/dev/null | grep -q .; then
    echo "✓ SwissDict module installed successfully"
    find /usr/local/lib/python3.*/ -name "swiss*.so" 2>/dev/null
else
    echo "✗ SwissDict module not found in installation directory"
fi

echo ""
echo "Build complete!"
echo ""
echo "To test the module:"
echo "  python3 -c \"import swiss; print('SwissDict available!')\""
echo ""
echo "To run benchmarks:"
echo "  python3 simple_benchmark.py"
echo "  python3 test_swiss.py"
