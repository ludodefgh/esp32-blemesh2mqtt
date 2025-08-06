#!/usr/bin/env python3
"""
Test script to verify the binary company lookup implementation
"""

import struct
import sys
from pathlib import Path

def read_binary_file(file_path):
    """Read and parse binary company file"""
    companies = {}
    
    with open(file_path, 'rb') as f:
        # Read header
        count_bytes = f.read(2)
        if len(count_bytes) != 2:
            return None
        
        count = struct.unpack('<H', count_bytes)[0]
        print(f"Reading {count} companies from binary file...")
        
        # Read entries
        for i in range(count):
            # Read company_id (2 bytes)
            company_id_bytes = f.read(2)
            if len(company_id_bytes) != 2:
                break
            company_id = struct.unpack('<H', company_id_bytes)[0]
            
            # Read name_len (1 byte)
            name_len_bytes = f.read(1)
            if len(name_len_bytes) != 1:
                break
            name_len = struct.unpack('<B', name_len_bytes)[0]
            
            # Read name
            name_bytes = f.read(name_len)
            if len(name_bytes) != name_len:
                break
            
            name = name_bytes.decode('utf-8')
            companies[company_id] = name
    
    print(f"Successfully read {len(companies)} companies")
    return companies

def read_indexed_file(file_path):
    """Read and parse indexed binary company file"""
    with open(file_path, 'rb') as f:
        # Read header
        count = struct.unpack('<H', f.read(2))[0]
        print(f"Reading indexed file with {count} companies...")
        
        # Read index
        index = {}
        for i in range(count):
            company_id, offset = struct.unpack('<HI', f.read(6))
            index[company_id] = offset
        
        # Verify some entries by reading data
        test_ids = [0x0001, 0x0002, 0x0003, 0x02E5]  # Nokia, Intel, IBM, ESP
        for company_id in test_ids:
            if company_id in index:
                f.seek(index[company_id])
                entry_id, name_len = struct.unpack('<HB', f.read(3))
                if entry_id == company_id:
                    name = f.read(name_len).decode('utf-8')
                    print(f"  0x{company_id:04X}: {name}")
    
    return True

def test_common_companies():
    """Test some common company IDs"""
    # Test data from original file
    test_companies = {
        0x0001: "Nokia Mobile Phones",
        0x0002: "Intel Corp.",
        0x0003: "IBM Corp.",
        0x02E5: "Espressif Systems (Shanghai) Co., Ltd.",
        0x0006: "Microsoft",
        0x004C: "Apple, Inc."
    }
    
    binary_file = Path(__file__).parent.parent / "littlefs" / "company_map.bin"
    indexed_file = Path(__file__).parent.parent / "littlefs" / "company_map_idx.bin"
    
    if binary_file.exists():
        print("\n=== Testing Binary File ===")
        companies = read_binary_file(binary_file)
        if companies:
            for company_id, expected_name in test_companies.items():
                actual_name = companies.get(company_id)
                if actual_name:
                    match = actual_name == expected_name
                    status = "✓" if match else "✗"
                    print(f"{status} 0x{company_id:04X}: {actual_name}")
                    if not match:
                        print(f"  Expected: {expected_name}")
                else:
                    print(f"✗ 0x{company_id:04X}: Not found")
    
    if indexed_file.exists():
        print("\n=== Testing Indexed File ===")
        read_indexed_file(indexed_file)
    
    return True

def main():
    print("=== Company Map Binary Test ===")
    test_common_companies()
    
    # File size comparison
    script_dir = Path(__file__).parent
    original_cpp = script_dir.parent / "sig_companies" / "company_map.cpp.backup"
    binary_file = script_dir.parent / "littlefs" / "company_map.bin"
    indexed_file = script_dir.parent / "littlefs" / "company_map_idx.bin"
    
    print(f"\n=== File Size Comparison ===")
    if original_cpp.exists():
        print(f"Original .cpp: {original_cpp.stat().st_size/1024:.1f}KB")
    if binary_file.exists():
        print(f"Binary file: {binary_file.stat().st_size/1024:.1f}KB")
    if indexed_file.exists():
        print(f"Indexed file: {indexed_file.stat().st_size/1024:.1f}KB")
    
    print("\n=== Memory Usage Estimation ===")
    print("Current implementation:")
    print("  - Flash: ~103KB (array + strings)")
    print("  - RAM: 0KB (static data)")
    print("New implementation:")
    print("  - Flash: 0KB (moved to LittleFS)")
    print("  - LittleFS: ~109KB (indexed file)")
    print("  - RAM: ~2KB (32-entry cache)")
    print("  - Net flash savings: ~103KB")
    
    return 0

if __name__ == "__main__":
    sys.exit(main())