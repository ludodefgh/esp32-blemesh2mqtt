#pragma once
#include <stdint.h>

typedef struct {
    uint32_t model_id;
    const char * const name;
} model_map_entry_t;

const char *lookup_model_name(uint32_t model_id);
    