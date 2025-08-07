#include "company_map.h"
#include <cstring>
#include <memory>
#include "esp_littlefs.h"
#include "common/log_common.h"

#define TAG "COMPANY_MAP_FS"
#define COMPANY_MAP_FILE "/littlefs/company_map.bin"
#define CACHE_SIZE 32  // LRU cache for most accessed companies

// LRU Cache entry
struct cache_entry_t {
    uint16_t company_id;
    char* company_name;
    uint32_t last_access;
    bool valid;
};

// File format structures
struct file_header_t {
    uint16_t count;
} __attribute__((packed));


struct data_entry_t {
    uint16_t company_id;
    uint8_t name_len;
    // name data follows
} __attribute__((packed));

// Static variables
static cache_entry_t cache[CACHE_SIZE];
static uint32_t access_counter = 0;
static bool cache_initialized = false;
static FILE* company_file = nullptr;
static uint16_t total_companies = 0;

// Initialize cache
static void init_cache() {
    if (cache_initialized) return;
    
    memset(cache, 0, sizeof(cache));
    cache_initialized = true;
    LOG_INFO(TAG, "Company cache initialized with %d slots", CACHE_SIZE);
}

// Open company file and read header
static bool open_company_file() {
    if (company_file) return true;
    
    company_file = fopen(COMPANY_MAP_FILE, "rb");
    if (!company_file) {
        LOG_ERROR(TAG, "Failed to open %s", COMPANY_MAP_FILE);
        return false;
    }
    
    // Read header
    file_header_t header;
    if (fread(&header, sizeof(header), 1, company_file) != 1) {
        LOG_ERROR(TAG, "Failed to read file header");
        fclose(company_file);
        company_file = nullptr;
        return false;
    }
    
    total_companies = header.count;
    
    LOG_INFO(TAG, "Opened company file with %d entries", total_companies);
    return true;
}

// Find company in cache
static const char* find_in_cache(uint16_t company_id) {
    init_cache();
    
    for (int i = 0; i < CACHE_SIZE; i++) {
        if (cache[i].valid && cache[i].company_id == company_id) {
            cache[i].last_access = ++access_counter;
            return cache[i].company_name;
        }
    }
    return nullptr;
}

// Add company to cache (with LRU eviction)
static void add_to_cache(uint16_t company_id, const char* company_name) {
    init_cache();
    
    // Find LRU slot
    int lru_slot = 0;
    uint32_t oldest_access = cache[0].last_access;
    
    for (int i = 0; i < CACHE_SIZE; i++) {
        if (!cache[i].valid) {
            lru_slot = i;
            break;
        }
        if (cache[i].last_access < oldest_access) {
            oldest_access = cache[i].last_access;
            lru_slot = i;
        }
    }
    
    // Free old entry
    if (cache[lru_slot].valid && cache[lru_slot].company_name) {
        free(cache[lru_slot].company_name);
    }
    
    // Add new entry
    cache[lru_slot].company_id = company_id;
    cache[lru_slot].company_name = strdup(company_name);
    cache[lru_slot].last_access = ++access_counter;
    cache[lru_slot].valid = true;
}

// Linear search through file entries
static const char* linear_search_file(uint16_t company_id) {
    if (!open_company_file()) return nullptr;
    
    // Seek to start of entries (after header)
    if (fseek(company_file, sizeof(file_header_t), SEEK_SET) != 0) {
        LOG_ERROR(TAG, "Failed to seek to start of entries");
        return nullptr;
    }
    
    // Use heap-allocated buffer to avoid static buffer issues
    char* name_buffer = (char*)malloc(512);
    if (!name_buffer) {
        LOG_ERROR(TAG, "Failed to allocate name buffer");
        return nullptr;
    }
    
    // Read through entries sequentially
    for (int i = 0; i < total_companies; i++) {
        data_entry_t entry;
        size_t bytes_read = fread(&entry, sizeof(entry), 1, company_file);
        if (bytes_read != 1) {
            LOG_ERROR(TAG, "Failed to read entry header %d (got %zu bytes)", i, bytes_read);
            free(name_buffer);
            return nullptr;
        }
        
        // Validate entry data
        if (entry.name_len == 0 || entry.name_len > 255) {
            LOG_ERROR(TAG, "Invalid name_len %d for entry %d (CID: 0x%04X) at file pos %ld", 
                     entry.name_len, i, entry.company_id, ftell(company_file));
            free(name_buffer);
            return nullptr;
        }
        
        // Read the name
        size_t bytes_to_read = (entry.name_len < 511) ? entry.name_len : 511;
        size_t name_bytes_read = fread(name_buffer, 1, bytes_to_read, company_file);
        if (name_bytes_read != bytes_to_read) {
            LOG_ERROR(TAG, "Failed to read name for entry %d: expected %zu, got %zu", 
                     i, bytes_to_read, name_bytes_read);
            free(name_buffer);
            return nullptr;
        }
        
        // Skip remaining bytes if name was truncated
        if (bytes_to_read < entry.name_len) {
            LOG_DEBUG(TAG, "Truncating long company name from %d to %zu bytes", entry.name_len, bytes_to_read);
            if (fseek(company_file, entry.name_len - bytes_to_read, SEEK_CUR) != 0) {
                LOG_ERROR(TAG, "Failed to skip remaining %d bytes for entry %d", 
                         entry.name_len - (int)bytes_to_read, i);
                free(name_buffer);
                return nullptr;
            }
        }
        
        name_buffer[bytes_to_read] = '\0';
        
        // Check if this is the company we're looking for
        if (entry.company_id == company_id) {
            // Copy to static buffer for return (temporary solution)
            static char result_buffer[512];
            strncpy(result_buffer, name_buffer, 511);
            result_buffer[511] = '\0';
            free(name_buffer);
            return result_buffer;
        }
    }
    
    free(name_buffer);
    return nullptr;  // Not found
}


// Main lookup function (API compatible with old version)
const char *lookup_company_name(uint16_t company_id) {
    if (company_id == 0) {
        return "Unknown Company";
    }
    
    // Check cache first
    const char* cached = find_in_cache(company_id);
    if (cached) {
        return cached;
    }
    
    // Linear search in file
    const char* company_name = linear_search_file(company_id);
    if (!company_name) {
        return "Unknown Company";
    }
    
    // Add to cache
    add_to_cache(company_id, company_name);
    
    return find_in_cache(company_id);  // Return cached version
}

// Cleanup function (optional)
void company_map_cleanup() {
    // Free cache
    if (cache_initialized) {
        for (int i = 0; i < CACHE_SIZE; i++) {
            if (cache[i].valid && cache[i].company_name) {
                free(cache[i].company_name);
                cache[i].valid = false;
                cache[i].company_name = nullptr;
            }
        }
        cache_initialized = false;
    }
    
    // Close file
    if (company_file) {
        fclose(company_file);
        company_file = nullptr;
    }
    
    LOG_INFO(TAG, "Company map cleanup completed");
}

// Debug function to print cache statistics
void company_map_print_stats() {
    if (!cache_initialized) {
        LOG_DEBUG(TAG, "Cache not initialized");
        return;
    }
    
    int used_slots = 0;
    for (int i = 0; i < CACHE_SIZE; i++) {
        if (cache[i].valid) {
            used_slots++;
        }
    }
    
    LOG_INFO(TAG, "Cache stats: %d/%d slots used, %lu total accesses", 
             used_slots, CACHE_SIZE, access_counter);
}