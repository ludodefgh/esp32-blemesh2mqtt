#!/usr/bin/env python3
"""
Script to convert company_map.cpp to optimized binary format for LittleFS storage.

Binary format:
- Header: uint16_t count (number of entries)
- Entries: [uint16_t company_id][uint8_t name_len][name_string (no null terminator)]

This format saves ~13KB compared to C array in flash.
"""

import re
import struct
import sys
from pathlib import Path

def parse_company_map(cpp_file_path):
    """Parse company_map.cpp and extract company entries."""
    companies = []
    
    with open(cpp_file_path, 'r', encoding='utf-8') as f:
        content = f.read()
    
    # Find all company entries using regex
    pattern = r'\{\s*0x([0-9A-Fa-f]+)\s*,\s*"([^"]+)"\s*\}'
    matches = re.findall(pattern, content)
    
    for company_id_hex, company_name in matches:
        if company_name:  # Skip empty names
            company_id = int(company_id_hex, 16)
            companies.append((company_id, company_name))
    
    print(f"Parsed {len(companies)} company entries")
    return companies

def generate_binary_file(companies, output_path):
    """Generate optimized binary file."""
    with open(output_path, 'wb') as f:
        # Write header: number of entries
        f.write(struct.pack('<H', len(companies)))
        
        # Write entries
        for company_id, company_name in companies:
            name_bytes = company_name.encode('utf-8')
            if len(name_bytes) > 255:
                print(f"Warning: Company name too long (truncated): {company_name}")
                name_bytes = name_bytes[:255]
            
            # Write: company_id (2 bytes) + name_len (1 byte) + name
            f.write(struct.pack('<H', company_id))
            f.write(struct.pack('<B', len(name_bytes)))
            f.write(name_bytes)
    
    file_size = Path(output_path).stat().st_size
    print(f"Generated binary file: {output_path} ({file_size/1024:.1f}KB)")
    return file_size

def generate_index_file(companies, output_path):
    """Generate index file for faster lookups (optional optimization)."""
    # Sort companies by ID for binary search
    sorted_companies = sorted(companies, key=lambda x: x[0])
    
    with open(output_path, 'wb') as f:
        # Write header
        f.write(struct.pack('<H', len(sorted_companies)))
        
        # Write index entries: [company_id][file_offset]
        offset = 2 + len(sorted_companies) * 6  # Header + index table
        
        for company_id, company_name in sorted_companies:
            name_bytes = company_name.encode('utf-8')
            name_len = min(len(name_bytes), 255)
            
            f.write(struct.pack('<HI', company_id, offset))
            offset += 3 + name_len  # company_id(2) + name_len(1) + name
        
        # Write data entries
        for company_id, company_name in sorted_companies:
            name_bytes = company_name.encode('utf-8')
            if len(name_bytes) > 255:
                name_bytes = name_bytes[:255]
            
            f.write(struct.pack('<H', company_id))
            f.write(struct.pack('<B', len(name_bytes)))
            f.write(name_bytes)
    
    file_size = Path(output_path).stat().st_size
    print(f"Generated indexed file: {output_path} ({file_size/1024:.1f}KB)")

def main():
    script_dir = Path(__file__).parent
    cpp_file = script_dir.parent / "sig_companies" / "company_map.cpp"
    littlefs_dir = script_dir.parent / "littlefs"
    
    if not cpp_file.exists():
        print(f"Error: {cpp_file} not found")
        return 1
    
    if not littlefs_dir.exists():
        littlefs_dir.mkdir(exist_ok=True)
    
    # Parse existing company_map.cpp
    companies = parse_company_map(cpp_file)
    
    # Generate binary files
    binary_file = littlefs_dir / "company_map.bin"
    index_file = littlefs_dir / "company_map_idx.bin"
    
    binary_size = generate_binary_file(companies, binary_file)
    generate_index_file(companies, index_file)
    
    # Generate statistics
    original_size = cpp_file.stat().st_size
    print(f"\n=== Size Comparison ===")
    print(f"Original .cpp file: {original_size/1024:.1f}KB")
    print(f"Binary file: {binary_size/1024:.1f}KB")
    print(f"Estimated flash savings: ~{(103*1024 - binary_size)/1024:.1f}KB")
    
    return 0

if __name__ == "__main__":
    sys.exit(main())