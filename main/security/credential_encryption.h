#pragma once

#include "esp_err.h"
#include <string>
#include <vector>

/**
 * Simple AES-256-CBC encryption for WiFi and MQTT credentials
 * Uses ESP32 hardware AES acceleration with a device-specific key
 */
class CredentialEncryption {
public:
    static CredentialEncryption& instance();
    
    /**
     * Initialize the encryption system
     * Generates or loads the encryption key from eFuse
     */
    esp_err_t initialize();
    
    /**
     * Encrypt a string value
     * @param plaintext The string to encrypt
     * @param ciphertext Output encrypted data (Base64 encoded)
     * @return ESP_OK on success
     */
    esp_err_t encrypt_string(const std::string& plaintext, std::string& ciphertext);
    
    /**
     * Decrypt a string value
     * @param ciphertext The encrypted data (Base64 encoded)
     * @param plaintext Output decrypted string
     * @return ESP_OK on success
     */
    esp_err_t decrypt_string(const std::string& ciphertext, std::string& plaintext);
    
    /**
     * Check if encryption is properly initialized
     */
    bool is_initialized() const { return initialized_; }

private:
    CredentialEncryption() = default;
    ~CredentialEncryption();
    
    // Disable copy
    CredentialEncryption(const CredentialEncryption&) = delete;
    CredentialEncryption& operator=(const CredentialEncryption&) = delete;
    
    /**
     * Generate or retrieve device-specific encryption key
     */
    esp_err_t setup_encryption_key();
    
    /**
     * Generate a random IV for each encryption operation  
     */
    void generate_iv(uint8_t* iv, size_t length);
    
    bool initialized_ = false;
    uint8_t encryption_key_[32]; // AES-256 key
    
    static constexpr size_t AES_BLOCK_SIZE = 16;
    static constexpr size_t AES_KEY_SIZE = 32; // AES-256
};