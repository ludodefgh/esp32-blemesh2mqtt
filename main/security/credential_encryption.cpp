#include "credential_encryption.h"

#include "esp_system.h"
#include "esp_mac.h"
#include "esp_random.h"
#include "mbedtls/aes.h"
#include "mbedtls/base64.h"
#include "mbedtls/sha256.h"
#include <cstring>
#include "common/log_common.h"

static const char* TAG = "CRED_ENCRYPT";

CredentialEncryption& CredentialEncryption::instance() {
    static CredentialEncryption instance;
    return instance;
}

CredentialEncryption::~CredentialEncryption() {
    // Clear sensitive data from memory
    memset(encryption_key_, 0, sizeof(encryption_key_));
}

esp_err_t CredentialEncryption::initialize() {
    if (initialized_) {
        return ESP_OK;
    }
    
    LOG_INFO(TAG, "Initializing credential encryption system");
    
    esp_err_t err = setup_encryption_key();
    if (err != ESP_OK) {
        LOG_ERROR(TAG, "Failed to setup encryption key: %s", esp_err_to_name(err));
        return err;
    }
    
    initialized_ = true;
    LOG_INFO(TAG, "Credential encryption system initialized successfully");
    
    // Quick self-test
    std::string test_plaintext = "TEST123";
    std::string test_encrypted, test_decrypted;
    
    esp_err_t test_err = encrypt_string(test_plaintext, test_encrypted);
    if (test_err == ESP_OK) {
        test_err = decrypt_string(test_encrypted, test_decrypted);
        if (test_err == ESP_OK && test_decrypted == test_plaintext) {
            LOG_INFO(TAG, "Encryption self-test PASSED");
        } else {
            LOG_ERROR(TAG, "Encryption self-test FAILED: decrypt error=%s, expected='%s', got='%s'", 
                     esp_err_to_name(test_err), test_plaintext.c_str(), test_decrypted.c_str());
        }
    } else {
        LOG_ERROR(TAG, "Encryption self-test FAILED: encrypt error=%s", esp_err_to_name(test_err));
    }
    
    return ESP_OK;
}

esp_err_t CredentialEncryption::setup_encryption_key() {
    // Generate device-specific key using ONLY MAC address (stable across reboots)
    uint8_t base_mac[6];
    esp_err_t err = esp_read_mac(base_mac, ESP_MAC_WIFI_STA);
    if (err != ESP_OK) {
        LOG_ERROR(TAG, "Failed to read MAC address");
        return err;
    }
    
    // Create a deterministic seed from MAC address only
    uint8_t seed[32];
    // Repeat and derive from MAC address in a predictable way
    for (int i = 0; i < 32; i++) {
        seed[i] = base_mac[i % 6] ^ (0xA5 + i); // XOR with fixed pattern + index
    }
    
    // Generate final key using SHA-256 of the seed
    mbedtls_sha256_context sha256_ctx;
    mbedtls_sha256_init(&sha256_ctx);
    
    err = (mbedtls_sha256_starts(&sha256_ctx, 0) == 0) ? ESP_OK : ESP_FAIL;
    if (err == ESP_OK) {
        err = (mbedtls_sha256_update(&sha256_ctx, seed, sizeof(seed)) == 0) ? ESP_OK : ESP_FAIL;
    }
    if (err == ESP_OK) {
        err = (mbedtls_sha256_finish(&sha256_ctx, encryption_key_) == 0) ? ESP_OK : ESP_FAIL;
    }
    
    mbedtls_sha256_free(&sha256_ctx);
    
    // Clear seed from memory
    memset(seed, 0, sizeof(seed));
    
    if (err != ESP_OK) {
        LOG_ERROR(TAG, "Failed to generate encryption key");
        return ESP_FAIL;
    }
    
    LOG_INFO(TAG, "Generated device-specific encryption key from MAC: %02X:%02X:%02X:%02X:%02X:%02X", 
             base_mac[0], base_mac[1], base_mac[2], base_mac[3], base_mac[4], base_mac[5]);
    return ESP_OK;
}

void CredentialEncryption::generate_iv(uint8_t* iv, size_t length) {
    esp_fill_random(iv, length);
}

esp_err_t CredentialEncryption::encrypt_string(const std::string& plaintext, std::string& ciphertext) {
    if (!initialized_) {
        LOG_ERROR(TAG, "Encryption not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (plaintext.empty()) {
        ciphertext.clear();
        return ESP_OK;
    }
    
    LOG_INFO(TAG, "Encrypting data (length: %zu)", plaintext.length());
    
    // Generate random IV
    uint8_t iv[AES_BLOCK_SIZE];
    generate_iv(iv, AES_BLOCK_SIZE);
    
    // Calculate padding needed for PKCS#7
    size_t plaintext_len = plaintext.length();
    size_t padded_len = ((plaintext_len / AES_BLOCK_SIZE) + 1) * AES_BLOCK_SIZE;
    uint8_t padding = padded_len - plaintext_len;
    
    // Create padded plaintext
    std::vector<uint8_t> padded_data(padded_len);
    memcpy(padded_data.data(), plaintext.c_str(), plaintext_len);
    
    // Apply PKCS#7 padding
    for (size_t i = plaintext_len; i < padded_len; i++) {
        padded_data[i] = padding;
    }
    
    // Encrypt using AES-256-CBC
    mbedtls_aes_context aes_ctx;
    mbedtls_aes_init(&aes_ctx);
    
    int ret = mbedtls_aes_setkey_enc(&aes_ctx, encryption_key_, AES_KEY_SIZE * 8);
    if (ret != 0) {
        mbedtls_aes_free(&aes_ctx);
        LOG_ERROR(TAG, "Failed to set AES key: %d", ret);
        return ESP_FAIL;
    }
    
    std::vector<uint8_t> encrypted_data(padded_len);
    uint8_t iv_copy[AES_BLOCK_SIZE];
    memcpy(iv_copy, iv, AES_BLOCK_SIZE);
    
    ret = mbedtls_aes_crypt_cbc(&aes_ctx, MBEDTLS_AES_ENCRYPT, padded_len,
                                iv_copy, padded_data.data(), encrypted_data.data());
    
    mbedtls_aes_free(&aes_ctx);
    
    if (ret != 0) {
        LOG_ERROR(TAG, "AES encryption failed: %d", ret);
        return ESP_FAIL;
    }
    
    // Combine IV + encrypted data
    std::vector<uint8_t> final_data(AES_BLOCK_SIZE + padded_len);
    memcpy(final_data.data(), iv, AES_BLOCK_SIZE);
    memcpy(final_data.data() + AES_BLOCK_SIZE, encrypted_data.data(), padded_len);
    
    // Base64 encode the result
    size_t encoded_len;
    ret = mbedtls_base64_encode(nullptr, 0, &encoded_len, final_data.data(), final_data.size());
    if (ret != MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL) {
        LOG_ERROR(TAG, "Base64 length calculation failed");
        return ESP_FAIL;
    }
    
    std::vector<uint8_t> encoded_data(encoded_len);
    ret = mbedtls_base64_encode(encoded_data.data(), encoded_len, &encoded_len,
                                final_data.data(), final_data.size());
    if (ret != 0) {
        LOG_ERROR(TAG, "Base64 encoding failed: %d", ret);
        return ESP_FAIL;
    }
    
    ciphertext = std::string(reinterpret_cast<char*>(encoded_data.data()), encoded_len);
    
    LOG_INFO(TAG, "Encryption successful, ciphertext: '%s' (length: %zu)", ciphertext.c_str(), ciphertext.length());
    
    // Clear sensitive data
    memset(padded_data.data(), 0, padded_data.size());
    memset(encrypted_data.data(), 0, encrypted_data.size());
    
    return ESP_OK;
}

esp_err_t CredentialEncryption::decrypt_string(const std::string& ciphertext, std::string& plaintext) {
    if (!initialized_) {
        LOG_ERROR(TAG, "Encryption not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (ciphertext.empty()) {
        plaintext.clear();
        return ESP_OK;
    }
    
    LOG_DEBUG(TAG, "Decrypting data of length: %zu", ciphertext.length());
    LOG_DEBUG(TAG, "Input ciphertext: %s", ciphertext.c_str());
    
    // Base64 decode
    size_t decoded_len;
    int ret = mbedtls_base64_decode(nullptr, 0, &decoded_len,
                                    reinterpret_cast<const uint8_t*>(ciphertext.c_str()),
                                    ciphertext.length());
    if (ret != MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL) {
        LOG_ERROR(TAG, "Base64 decode length calculation failed: %d", ret);
        return ESP_FAIL;
    }
    
    if (decoded_len < AES_BLOCK_SIZE + AES_BLOCK_SIZE) { // IV + at least one block
        LOG_ERROR(TAG, "Decoded data too short: %zu bytes", decoded_len);
        return ESP_FAIL;
    }
    
    std::vector<uint8_t> decoded_data(decoded_len);
    ret = mbedtls_base64_decode(decoded_data.data(), decoded_len, &decoded_len,
                                reinterpret_cast<const uint8_t*>(ciphertext.c_str()),
                                ciphertext.length());
    if (ret != 0) {
        LOG_ERROR(TAG, "Base64 decoding failed: %d", ret);
        return ESP_FAIL;
    }
    
    // Extract IV and encrypted data
    uint8_t iv[AES_BLOCK_SIZE];
    memcpy(iv, decoded_data.data(), AES_BLOCK_SIZE);
    
    size_t encrypted_len = decoded_len - AES_BLOCK_SIZE;
    if (encrypted_len % AES_BLOCK_SIZE != 0) {
        LOG_ERROR(TAG, "Invalid encrypted data length: %zu", encrypted_len);
        return ESP_FAIL;
    }
    
    // Decrypt using AES-256-CBC
    mbedtls_aes_context aes_ctx;
    mbedtls_aes_init(&aes_ctx);
    
    ret = mbedtls_aes_setkey_dec(&aes_ctx, encryption_key_, AES_KEY_SIZE * 8);
    if (ret != 0) {
        mbedtls_aes_free(&aes_ctx);
        LOG_ERROR(TAG, "Failed to set AES key: %d", ret);
        return ESP_FAIL;
    }
    
    std::vector<uint8_t> decrypted_data(encrypted_len);
    ret = mbedtls_aes_crypt_cbc(&aes_ctx, MBEDTLS_AES_DECRYPT, encrypted_len,
                                iv, decoded_data.data() + AES_BLOCK_SIZE, decrypted_data.data());
    
    mbedtls_aes_free(&aes_ctx);
    
    if (ret != 0) {
        LOG_ERROR(TAG, "AES decryption failed: %d", ret);
        return ESP_FAIL;
    }
    
    // Remove PKCS#7 padding
    if (decrypted_data.empty()) {
        LOG_ERROR(TAG, "Decrypted data is empty");
        return ESP_FAIL;
    }
    
    uint8_t padding = decrypted_data.back();
    if (padding == 0 || padding > AES_BLOCK_SIZE || padding > decrypted_data.size()) {
        LOG_ERROR(TAG, "Invalid padding: %d", padding);
        return ESP_FAIL;
    }
    
    // Verify padding
    for (size_t i = decrypted_data.size() - padding; i < decrypted_data.size(); i++) {
        if (decrypted_data[i] != padding) {
            LOG_ERROR(TAG, "Padding verification failed");
            return ESP_FAIL;
        }
    }
    
    size_t plaintext_len = decrypted_data.size() - padding;
    plaintext = std::string(reinterpret_cast<char*>(decrypted_data.data()), plaintext_len);
    
    LOG_DEBUG(TAG, "Decryption successful (length: %zu)", plaintext.length());
    
    // Clear sensitive data
    memset(decrypted_data.data(), 0, decrypted_data.size());
    
    return ESP_OK;
}