#include "debug_commands_registry.h"


std::vector<DebugInitFunc>& debug_command_registry::get_list() {
    static std::vector<DebugInitFunc> list;
    return list;
}

void debug_command_registry::register_func(DebugInitFunc fn) {
    get_list().push_back(fn);
}

void debug_command_registry::run_all() {
    for (auto& fn : get_list()) {
        fn();
    }
    get_list().clear(); // Clear the list after running all functions
}
