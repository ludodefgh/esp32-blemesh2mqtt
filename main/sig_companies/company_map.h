#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /**
     * @brief Lookup company name by Bluetooth Company Identifier (CID)
     *
     * This function uses an optimized LittleFS-based storage with LRU cache
     * to reduce flash memory usage while maintaining fast lookups.
     *
     * @param company_id Bluetooth Company Identifier (16-bit)
     * @return const char* Company name string, or "Unknown Company" if not found
     *
     * @note The returned string is valid until the next call or cache eviction
     */
    const char *lookup_company_name(uint16_t company_id);

    /**
     * @brief Print cache statistics for debugging
     */
    void company_map_print_stats(void);

    /**
     * @brief Cleanup company map resources
     *
     * Call this during shutdown to free allocated resources.
     * Optional - resources are cleaned up automatically.
     */
    void company_map_cleanup(void);

#ifdef __cplusplus
}
#endif
