#pragma once
#include "argtable3/argtable3.h"

typedef struct {
    struct arg_int *node_index;
    struct arg_end *end;
} debug_cmd_node_index_args_t;

static debug_cmd_node_index_args_t node_index_args;