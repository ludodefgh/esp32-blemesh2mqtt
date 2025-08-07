#include "mqtt_credentials.h"

#include "nvs_flash.h"
#include "nvs.h"
#include "security/credential_encryption.h"
#include <cstring>
#include <cctype>
#include "common/log_common.h"

static const char* TAG = "MQTT_CREDS";

MqttCredentialManager& MqttCredentialManager::instance() {
    static MqttCredentialManager instance;
    return instance;
}

std::string MqttCredentialManager::get_connection_state_string() const {
    switch (connection_state_) {
        case mqtt_connection_state_t::UNCONFIGURED: return "unconfigured";
        case mqtt_connection_state_t::CONFIGURED: return "configured";
        case mqtt_connection_state_t::CONNECTING: return "connecting";
        case mqtt_connection_state_t::CONNECTED: return "connected";
        case mqtt_connection_state_t::DISCONNECTED: return "disconnected";
        case mqtt_connection_state_t::ERROR_AUTH: return "auth_error";
        case mqtt_connection_state_t::ERROR_NETWORK: return "network_error";
        case mqtt_connection_state_t::ERROR_TIMEOUT: return "timeout_error";
        default: return "unknown";
    }
}

void MqttCredentialManager::set_connection_state(mqtt_connection_state_t state) {
    if (connection_state_ != state) {
        std::string old_state = get_connection_state_string();
        connection_state_ = state;
        std::string new_state = get_connection_state_string();
        LOG_INFO(TAG, "Connection state changed: %s -> %s", 
                 old_state.c_str(), new_state.c_str());
    }
}

void MqttCredentialManager::set_last_error(const std::string& error) {
    last_error_ = error;
    LOG_WARN(TAG, "MQTT Error: %s", error.c_str());
}

// Helper function to validate IP address
static bool is_valid_ip(const std::string& ip) {
    if (ip.empty() || ip.length() > 15) return false;
    
    int dots = 0;
    int num = 0;
    bool has_digit = false;
    
    for (char c : ip) {
        if (c == '.') {
            if (!has_digit || num > 255 || dots >= 3) return false;
            dots++;
            num = 0;
            has_digit = false;
        } else if (std::isdigit(c)) {
            num = num * 10 + (c - '0');
            has_digit = true;
            if (num > 255) return false;
        } else {
            return false;
        }
    }
    
    return dots == 3 && has_digit && num <= 255;
}

// Helper function to validate hostname
static bool is_valid_hostname(const std::string& hostname) {
    if (hostname.empty() || hostname.length() > 253) return false;
    if (hostname[0] == '.' || hostname.back() == '.') return false;
    
    for (size_t i = 0; i < hostname.length(); i++) {
        char c = hostname[i];
        if (!std::isalnum(c) && c != '.' && c != '-') return false;
        if (c == '-' && (i == 0 || hostname[i-1] == '.' || (i+1 < hostname.length() && hostname[i+1] == '.'))) return false;
    }
    
    return true;
}

bool MqttCredentialManager::validate_credentials(const mqtt_credentials_t& creds, std::string& error_msg) const {
    // Validate broker host
    if (creds.broker_host.empty()) {
        error_msg = "Broker host cannot be empty";
        return false;
    }
    
    // Basic hostname/IP validation using simple string parsing
    if (!is_valid_ip(creds.broker_host) && !is_valid_hostname(creds.broker_host)) {
        error_msg = "Invalid broker host format";
        return false;
    }
    
    // Validate port
    if (creds.broker_port == 0 || creds.broker_port > 65535) {
        error_msg = "Invalid broker port (1-65535)";
        return false;
    }
    
    // Validate username
    if (creds.username.empty()) {
        error_msg = "Username cannot be empty";
        return false;
    }
    
    if (creds.username.length() > 256) {
        error_msg = "Username too long (max 256 characters)";
        return false;
    }
    
    // Validate password
    if (creds.password.empty()) {
        error_msg = "Password cannot be empty";
        return false;
    }
    
    if (creds.password.length() > 512) {
        error_msg = "Password too long (max 512 characters)";
        return false;
    }
    
    return true;
}

bool MqttCredentialManager::has_valid_credentials() const {
    std::string error_msg;
    return validate_credentials(credentials_, error_msg);
}

esp_err_t MqttCredentialManager::encrypt_and_store(const std::string& key, const std::string& value) {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        LOG_ERROR(TAG, "Failed to open NVS handle: %s", esp_err_to_name(err));
        return err;
    }
    
    // Encrypt the value
    std::string encrypted_value;
    if (!CredentialEncryption::instance().is_initialized()) {
        LOG_ERROR(TAG, "Encryption not initialized");
        nvs_close(nvs_handle);
        return ESP_ERR_INVALID_STATE;
    }
    
    esp_err_t encrypt_err = CredentialEncryption::instance().encrypt_string(value, encrypted_value);
    if (encrypt_err != ESP_OK) {
        LOG_ERROR(TAG, "Failed to encrypt %s: %s", key.c_str(), esp_err_to_name(encrypt_err));
        nvs_close(nvs_handle);
        return encrypt_err;
    }
    
    LOG_INFO(TAG, "MQTT %s encrypted successfully", key.c_str());
    
    err = nvs_set_str(nvs_handle, key.c_str(), encrypted_value.c_str());
    if (err != ESP_OK) {
        LOG_ERROR(TAG, "Failed to set NVS key %s: %s", key.c_str(), esp_err_to_name(err));
        nvs_close(nvs_handle);
        return err;
    }
    
    err = nvs_commit(nvs_handle);
    if (err != ESP_OK) {
        LOG_ERROR(TAG, "Failed to commit NVS: %s", esp_err_to_name(err));
    }
    
    nvs_close(nvs_handle);
    return err;
}

esp_err_t MqttCredentialManager::decrypt_and_load(const std::string& key, std::string& value) {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        if (err != ESP_ERR_NVS_NOT_FOUND) {
            LOG_ERROR(TAG, "Failed to open NVS handle: %s", esp_err_to_name(err));
        }
        return err;
    }
    
    size_t required_size = 0;
    err = nvs_get_str(nvs_handle, key.c_str(), nullptr, &required_size);
    if (err != ESP_OK) {
        nvs_close(nvs_handle);
        return err;
    }
    
    char* encrypted_buffer = new char[required_size];
    err = nvs_get_str(nvs_handle, key.c_str(), encrypted_buffer, &required_size);
    if (err != ESP_OK) {
        delete[] encrypted_buffer;
        nvs_close(nvs_handle);
        return err;
    }
    
    // Decrypt the value
    std::string decrypted_value;
    if (!CredentialEncryption::instance().is_initialized()) {
        LOG_ERROR(TAG, "Encryption not initialized");
        delete[] encrypted_buffer;
        nvs_close(nvs_handle);
        return ESP_ERR_INVALID_STATE;
    }
    
    esp_err_t decrypt_err = CredentialEncryption::instance().decrypt_string(encrypted_buffer, decrypted_value);
    if (decrypt_err != ESP_OK) {
        LOG_ERROR(TAG, "Failed to decrypt %s: %s", key.c_str(), esp_err_to_name(decrypt_err));
        delete[] encrypted_buffer;
        nvs_close(nvs_handle);
        return decrypt_err;
    }
    
    value = decrypted_value;
    
    // Clear sensitive data from memory
    memset(encrypted_buffer, 0, required_size);
    delete[] encrypted_buffer;
    
    nvs_close(nvs_handle);
    return ESP_OK;
}

esp_err_t MqttCredentialManager::load_credentials() {
    LOG_INFO(TAG, "Loading MQTT credentials from NVS");
    
    credentials_.clear();
    
    // Load broker host
    esp_err_t err = decrypt_and_load(KEY_BROKER_HOST, credentials_.broker_host);
    if (err != ESP_OK) {
        if (err == ESP_FAIL) {
            LOG_WARN(TAG, "MQTT credentials appear to be in old plain text format, clearing them");
            LOG_WARN(TAG, "Please reconfigure MQTT settings via the web interface");
            clear_credentials(); // Clear old plain text credentials
        }
        LOG_WARN(TAG, "No valid broker host found in NVS");
        set_connection_state(mqtt_connection_state_t::UNCONFIGURED);
        return err;
    }
    
    // Load broker port
    nvs_handle_t nvs_handle;
    err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err == ESP_OK) {
        uint16_t port;
        err = nvs_get_u16(nvs_handle, KEY_BROKER_PORT, &port);
        if (err == ESP_OK) {
            credentials_.broker_port = port;
        } else {
            credentials_.broker_port = 1883; // Default
        }
        
        uint8_t use_ssl;
        err = nvs_get_u8(nvs_handle, KEY_USE_SSL, &use_ssl);
        if (err == ESP_OK) {
            credentials_.use_ssl = (use_ssl != 0);
        } else {
            credentials_.use_ssl = false; // Default
        }
        
        nvs_close(nvs_handle);
    }
    
    // Load username
    err = decrypt_and_load(KEY_USERNAME, credentials_.username);
    if (err != ESP_OK) {
        LOG_WARN(TAG, "No username found in NVS");
        credentials_.clear();
        set_connection_state(mqtt_connection_state_t::UNCONFIGURED);
        return err;
    }
    
    // Load password
    err = decrypt_and_load(KEY_PASSWORD, credentials_.password);
    if (err != ESP_OK) {
        LOG_WARN(TAG, "No password found in NVS");
        credentials_.clear();
        set_connection_state(mqtt_connection_state_t::UNCONFIGURED);
        return err;
    }
    
    // Validate loaded credentials
    std::string error_msg;
    if (!validate_credentials(credentials_, error_msg)) {
        LOG_ERROR(TAG, "Loaded credentials are invalid: %s", error_msg.c_str());
        credentials_.clear();
        set_connection_state(mqtt_connection_state_t::UNCONFIGURED);
        return ESP_ERR_INVALID_ARG;
    }
    
    LOG_INFO(TAG, "Successfully loaded MQTT credentials for broker: %s:%d", 
             credentials_.broker_host.c_str(), credentials_.broker_port);
    set_connection_state(mqtt_connection_state_t::CONFIGURED);
    
    return ESP_OK;
}

esp_err_t MqttCredentialManager::save_credentials(const mqtt_credentials_t& creds) {
    LOG_INFO(TAG, "Saving MQTT credentials to NVS");
    
    // Validate before saving
    std::string error_msg;
    if (!validate_credentials(creds, error_msg)) {
        LOG_ERROR(TAG, "Invalid credentials: %s", error_msg.c_str());
        return ESP_ERR_INVALID_ARG;
    }
    
    // Save broker host
    esp_err_t err = encrypt_and_store(KEY_BROKER_HOST, creds.broker_host);
    if (err != ESP_OK) {
        LOG_ERROR(TAG, "Failed to save broker host");
        return err;
    }
    
    // Save broker port and SSL flag
    nvs_handle_t nvs_handle;
    err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        LOG_ERROR(TAG, "Failed to open NVS handle: %s", esp_err_to_name(err));
        return err;
    }
    
    err = nvs_set_u16(nvs_handle, KEY_BROKER_PORT, creds.broker_port);
    if (err != ESP_OK) {
        LOG_ERROR(TAG, "Failed to save broker port");
        nvs_close(nvs_handle);
        return err;
    }
    
    err = nvs_set_u8(nvs_handle, KEY_USE_SSL, creds.use_ssl ? 1 : 0);
    if (err != ESP_OK) {
        LOG_ERROR(TAG, "Failed to save SSL flag");
        nvs_close(nvs_handle);
        return err;
    }
    
    err = nvs_commit(nvs_handle);
    nvs_close(nvs_handle);
    if (err != ESP_OK) {
        LOG_ERROR(TAG, "Failed to commit NVS");
        return err;
    }
    
    // Save username
    err = encrypt_and_store(KEY_USERNAME, creds.username);
    if (err != ESP_OK) {
        LOG_ERROR(TAG, "Failed to save username");
        return err;
    }
    
    // Save password
    err = encrypt_and_store(KEY_PASSWORD, creds.password);
    if (err != ESP_OK) {
        LOG_ERROR(TAG, "Failed to save password");
        return err;
    }
    
    // Update current credentials
    credentials_ = creds;
    set_connection_state(mqtt_connection_state_t::CONFIGURED);
    
    LOG_INFO(TAG, "Successfully saved MQTT credentials for broker: %s:%d", 
             credentials_.broker_host.c_str(), credentials_.broker_port);
    
    return ESP_OK;
}

esp_err_t MqttCredentialManager::clear_credentials() {
    LOG_INFO(TAG, "Clearing MQTT credentials from NVS");
    
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        LOG_ERROR(TAG, "Failed to open NVS handle: %s", esp_err_to_name(err));
        return err;
    }
    
    // Erase all keys
    nvs_erase_key(nvs_handle, KEY_BROKER_HOST);
    nvs_erase_key(nvs_handle, KEY_BROKER_PORT);
    nvs_erase_key(nvs_handle, KEY_USERNAME);
    nvs_erase_key(nvs_handle, KEY_PASSWORD);
    nvs_erase_key(nvs_handle, KEY_USE_SSL);
    
    err = nvs_commit(nvs_handle);
    nvs_close(nvs_handle);
    
    // Clear in-memory credentials
    clear_sensitive_memory();
    credentials_.clear();
    set_connection_state(mqtt_connection_state_t::UNCONFIGURED);
    
    LOG_INFO(TAG, "Successfully cleared MQTT credentials");
    return err;
}

void MqttCredentialManager::clear_sensitive_memory() {
    // Overwrite sensitive data in memory
    if (!credentials_.password.empty()) {
        memset(const_cast<char*>(credentials_.password.data()), 0, credentials_.password.size());
    }
    if (!credentials_.username.empty()) {
        memset(const_cast<char*>(credentials_.username.data()), 0, credentials_.username.size());
    }
}