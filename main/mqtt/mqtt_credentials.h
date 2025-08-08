#pragma once

#include <string>

#include "esp_err.h"

enum class mqtt_connection_state_t : uint8_t
{
    UNCONFIGURED = 0,
    CONFIGURED,
    CONNECTING,
    CONNECTED,
    DISCONNECTED,
    ERROR_AUTH,
    ERROR_NETWORK,
    ERROR_TIMEOUT
};

struct mqtt_credentials_t
{
    std::string broker_host;
    uint16_t broker_port;
    std::string username;
    std::string password;
    bool use_ssl;

    mqtt_credentials_t() : broker_port(1883), use_ssl(false) {}

    bool is_valid() const
    {
        return !broker_host.empty() && !username.empty() && !password.empty();
    }

    void clear()
    {
        broker_host.clear();
        username.clear();
        password.clear();
        broker_port = 1883;
        use_ssl = false;
    }
};

class mqtt_credentials_manager
{
public:
    static mqtt_credentials_manager &instance();

    // Credential management
    esp_err_t load_credentials();
    esp_err_t save_credentials(const mqtt_credentials_t &creds);
    esp_err_t clear_credentials();

    // Getters
    const mqtt_credentials_t &get_credentials() const { return credentials_; }
    mqtt_connection_state_t get_connection_state() const { return connection_state_; }
    std::string get_connection_state_string() const;
    std::string get_last_error() const { return last_error_; }

    // State management
    void set_connection_state(mqtt_connection_state_t state);
    void set_last_error(const std::string &error);

    // Validation
    bool validate_credentials(const mqtt_credentials_t &creds, std::string &error_msg) const;
    bool has_valid_credentials() const;

    // Security
    void clear_sensitive_memory();

private:
    mqtt_credentials_manager() = default;
    ~mqtt_credentials_manager() { clear_sensitive_memory(); }

    // Non-copyable
    mqtt_credentials_manager(const mqtt_credentials_manager &) = delete;
    mqtt_credentials_manager &operator=(const mqtt_credentials_manager &) = delete;

    esp_err_t encrypt_and_store(const std::string &key, const std::string &value);
    esp_err_t decrypt_and_load(const std::string &key, std::string &value);

    mqtt_credentials_t credentials_;
    mqtt_connection_state_t connection_state_ = mqtt_connection_state_t::UNCONFIGURED;
    std::string last_error_;

    static constexpr const char *NVS_NAMESPACE = "mqtt_creds";
    static constexpr const char *KEY_BROKER_HOST = "broker_host";
    static constexpr const char *KEY_BROKER_PORT = "broker_port";
    static constexpr const char *KEY_USERNAME = "username";
    static constexpr const char *KEY_PASSWORD = "password";
    static constexpr const char *KEY_USE_SSL = "use_ssl";
};

// Convenience function
inline mqtt_credentials_manager &mqtt_credentials()
{
    return mqtt_credentials_manager::instance();
}