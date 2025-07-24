#include "console_cmd.h"

#include <vector>
#include <string>

static std::vector<console_command_info> registered_commands;

const std::vector<console_command_info> &get_registered_commands()
{
    return registered_commands;
}

esp_err_t register_console_command(const esp_console_cmd_t *cmd)
{
    if (cmd && cmd->command)
    {
        registered_commands.emplace_back(console_command_info{cmd->command, cmd->help ? cmd->help : ""});
    }
    return esp_console_cmd_register(cmd);
}
