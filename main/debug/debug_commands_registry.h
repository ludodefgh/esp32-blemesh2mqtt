#pragma once
#include <vector>

using DebugInitFunc = void(*)();

class debug_command_registry {
public:
    static void register_func(DebugInitFunc fn);
    static void run_all();

private:
    static std::vector<DebugInitFunc>& get_list();
};

#define REGISTER_DEBUG_COMMAND(fn)  \
    static void __attribute__((constructor)) _reg_ ## fn () { \
        debug_command_registry::register_func(fn);    \
    }