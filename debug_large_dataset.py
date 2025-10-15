#!/usr/bin/env python3
"""
Debug script to test large dataset behavior
"""

import swiss

def test_large_dataset():
    print("Testing large dataset behavior...")
    
    # Test with 100000 keys
    d = swiss.SwissDict()
    
    print("Inserting 100000 keys...")
    for i in range(100000):
        d[f'key_{i}'] = f'value_{i}'
    
    print(f"Stored 100000 keys, length: {len(d)}")
    
    print("Testing retrieval...")
    missing = []
    for i in range(100000):
        try:
            val = d[f'key_{i}']
        except KeyError:
            missing.append(f'key_{i}')
    
    if missing:
        print(f"Missing keys: {missing[:10]}...")  # Show first 10 missing
        print(f"Total missing: {len(missing)}")
        
        # Test if missing keys exist in iteration
        print("\\nChecking if missing keys exist in iteration...")
        items = list(d.items())
        keys_in_iteration = [item[0] for item in items]
        
        for missing_key in missing[:5]:  # Check first 5 missing keys
            if missing_key in keys_in_iteration:
                print(f"  {missing_key} exists in iteration but not in lookup!")
            else:
                print(f"  {missing_key} truly missing")
    else:
        print("All keys found!")

if __name__ == "__main__":
    test_large_dataset()
