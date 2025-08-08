#pragma once
#include <string>
#include <vector>

#include <esp_console.h>
#include <esp_err.h>

struct console_command_info
{
    std::string name;
    std::string help;
};

esp_err_t register_console_command(const esp_console_cmd_t *cmd);

const std::vector<console_command_info> &get_registered_commands();