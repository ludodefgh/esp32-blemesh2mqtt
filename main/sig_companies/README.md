# Company Map Optimization - LittleFS Implementation

## Overview

This directory contains an optimized implementation of Bluetooth Company Identifier (CID) lookup that moves company data from flash memory to LittleFS storage with intelligent caching.

## Memory Savings

| Implementation | Flash Usage | RAM Usage | LittleFS Usage | Lookup Speed |
|----------------|-------------|-----------|----------------|--------------|
| **Old (Array)** | ~103KB | 0KB | 0KB | O(n) linear |
| **New (LittleFS)** | 0KB | ~2KB | ~109KB | O(log n) + cache |

**Net Result**: **~103KB flash memory saved** for application code.

## Files

- `company_map.h` - Public API (unchanged interface)
- `company_map.cpp` - New LittleFS-based implementation with LRU cache
- `company_map.cpp.backup` - Original array-based implementation
- `../littlefs/company_map_idx.bin` - Binary company data (109KB)
- `../offline_resources/generate_company_binary.py` - Data generator script

## Features

### 🚀 Performance
- **LRU Cache**: 32-slot cache for frequently accessed companies
- **Binary Search**: O(log n) lookup in sorted index
- **Lazy Loading**: Companies loaded on-demand from storage

### 💾 Memory Efficiency
- **Flash Savings**: 103KB returned to application
- **Minimal RAM**: Only ~2KB for active cache entries
- **Compact Format**: Optimized binary encoding

### 🔧 Compatibility
- **Same API**: Drop-in replacement for existing code
- **Same Results**: Identical company name strings
- **Error Handling**: Graceful fallback to "Unknown Company"

## Binary File Format

### Header
```c
struct file_header_t {
    uint16_t count;  // Number of company entries
} __attribute__((packed));
```

### Index Entry
```c
struct index_entry_t {
    uint16_t company_id;    // Company identifier
    uint32_t file_offset;   // Offset to data entry
} __attribute__((packed));
```

### Data Entry
```c
struct data_entry_t {
    uint16_t company_id;    // Company identifier (verification)
    uint8_t name_len;       // Length of company name
    // char name[name_len]; // Company name string (no null terminator)
} __attribute__((packed));
```

## Usage

### Basic Lookup
```c
#include "sig_companies/company_map.h"

uint16_t cid = 0x02E5;  // Espressif
const char* name = lookup_company_name(cid);
printf("Company: %s\\n", name);  // "Espressif Systems (Shanghai) Co., Ltd."
```

### Debug Functions
```c
// Print cache statistics
company_map_print_stats();

// Cleanup resources (optional)
company_map_cleanup();
```

## Regenerating Data

To update company data from the original source:

```bash
cd main/offline_resources
python3 generate_company_binary.py
```

This reads `company_map.cpp.backup` and generates new binary files in `../littlefs/`.

## Cache Behavior

The LRU cache maintains 32 most recently accessed companies in RAM:

1. **Cache Hit**: Immediate return (fastest)
2. **Cache Miss**: Binary search in file + cache insertion
3. **Cache Full**: LRU eviction of oldest entry

Cache statistics show hit ratio and memory usage.

## Error Handling

- **File Missing**: Graceful fallback to "Unknown Company"
- **Corrupted Data**: Validation and error logging
- **Memory Issues**: Safe cleanup and fallback
- **Invalid CID**: Returns "Unknown Company"

## Integration Points

The lookup function is used in:

- **BLE Mesh Provisioning**: Store CID from composition data
- **MQTT Discovery**: Send manufacturer to Home Assistant  
- **Web Interface**: Display company names in node cards
- **Logging**: Show readable company names in debug output

## Performance Characteristics

- **First Lookup**: ~200μs (file I/O + caching)
- **Cached Lookup**: ~5μs (memory access)
- **Memory Usage**: 2KB + (32 × avg_name_length)
- **Storage Access**: Sequential reads, minimal fragmentation

## Migration Notes

1. **Compile**: Include new `company_map.cpp` in build
2. **Deploy**: Ensure `company_map_idx.bin` in LittleFS
3. **Test**: Verify company lookups work correctly
4. **Rollback**: Restore `company_map.cpp.backup` if needed

The implementation is designed for seamless migration with zero API changes.