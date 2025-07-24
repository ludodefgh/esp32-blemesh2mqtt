#pragma once
#include <esp_err.h>
#include <esp_console.h>

#include <string>
#include <vector>

struct console_command_info {
    std::string name;
    std::string help;
};

esp_err_t register_console_command(const esp_console_cmd_t *cmd);

const std::vector<console_command_info>& get_registered_commands();