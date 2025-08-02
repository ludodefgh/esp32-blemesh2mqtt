#include "dns_server.h"
#include "esp_log.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <sys/socket.h>

static const char* TAG = "dns_server";
static TaskHandle_t dns_task_handle = NULL;
static int dns_socket = -1;
static bool dns_server_running = false;
static dns_server_config_t s_dns_config;
static bool s_config_set = false;

typedef struct {
    uint16_t id;
    uint16_t flags;
    uint16_t qdcount;
    uint16_t ancount;
    uint16_t nscount;
    uint16_t arcount;
} __attribute__((packed)) dns_header_t;

typedef struct {
    uint16_t qtype;
    uint16_t qclass;
} __attribute__((packed)) dns_question_t;

typedef struct {
    uint16_t name;
    uint16_t type;
    uint16_t class_;
    uint32_t ttl;
    uint16_t rdlength;
    uint32_t rdata;
} __attribute__((packed)) dns_answer_t;

static bool match_domain(const char* domain, const char* pattern) {
    // Wildcard match
    if (strcmp(pattern, "*") == 0) {
        return true;
    }
    
    // Exact match (case insensitive)
    return strcasecmp(domain, pattern) == 0;
}

static esp_ip4_addr_t get_response_ip(const char* domain) {
    esp_ip4_addr_t default_ip;
    esp_netif_str_to_ip4("192.168.4.1", &default_ip);
    
    if (!s_config_set) {
        return default_ip;  // Fallback to default captive portal IP
    }
    
    for (int i = 0; i < s_dns_config.num_of_entries; i++) {
        if (match_domain(domain, s_dns_config.item[i].name)) {
            ESP_LOGD(TAG, "Matched domain '%s' to pattern '%s', responding with IP", 
                     domain, s_dns_config.item[i].name);
            return s_dns_config.item[i].ip;
        }
    }
    
    ESP_LOGD(TAG, "No match for domain '%s', using default IP", domain);
    return default_ip;
}

static void dns_server_task(void *pvParameters)
{
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_addr_len = sizeof(client_addr);
    uint8_t rx_buffer[512];
    uint8_t tx_buffer[DNS_RESPONSE_BUFFER_SIZE];
    
    dns_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (dns_socket < 0) {
        ESP_LOGE(TAG, "Failed to create DNS socket");
        dns_server_running = false;
        vTaskDelete(NULL);
        return;
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(DNS_PORT);

    if (bind(dns_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        ESP_LOGE(TAG, "Failed to bind DNS socket");
        close(dns_socket);
        dns_socket = -1;
        dns_server_running = false;
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "DNS server started on port %d", DNS_PORT);

    while (dns_server_running) {
        int len = recvfrom(dns_socket, rx_buffer, sizeof(rx_buffer), 0,
                          (struct sockaddr*)&client_addr, &client_addr_len);
        
        if (len < 0) {
            if (dns_server_running) {
                ESP_LOGE(TAG, "DNS recvfrom failed");
            }
            continue;
        }

        if (len < sizeof(dns_header_t)) {
            ESP_LOGW(TAG, "Received DNS packet too short");
            continue;
        }

        dns_header_t* header = (dns_header_t*)rx_buffer;
        
        uint16_t question_count = ntohs(header->qdcount);
        if (question_count == 0) {
            // Send a proper DNS response even for queries with no questions
            ESP_LOGD(TAG, "DNS query with no questions, sending minimal response");
            
            memset(tx_buffer, 0, sizeof(tx_buffer));
            dns_header_t* response_header = (dns_header_t*)tx_buffer;
            response_header->id = header->id;
            response_header->flags = htons(0x8180);  // Standard response, no error
            response_header->qdcount = 0;
            response_header->ancount = 0;
            response_header->nscount = 0;
            response_header->arcount = 0;
            
            sendto(dns_socket, tx_buffer, sizeof(dns_header_t), 0,
                   (struct sockaddr*)&client_addr, client_addr_len);
            continue;
        }
        
        if (question_count > 1) {
            ESP_LOGD(TAG, "DNS query with %d questions, handling first one", question_count);
        }
        
        // Log the first domain name being queried for debugging
        uint8_t* domain_ptr = rx_buffer + sizeof(dns_header_t);
        char domain_name[128] = {0};
        int domain_pos = 0;
        
        while (*domain_ptr != 0 && domain_pos < sizeof(domain_name) - 1 && 
               (domain_ptr - rx_buffer) < len) {
            if ((*domain_ptr & 0xC0) == 0xC0) {
                // Compressed name - just log what we have
                break;
            } else {
                uint8_t label_len = *domain_ptr++;
                if (label_len > 63) break;  // Invalid label length
                
                if (domain_pos > 0) domain_name[domain_pos++] = '.';
                
                for (int i = 0; i < label_len && domain_pos < sizeof(domain_name) - 1; i++) {
                    domain_name[domain_pos++] = *domain_ptr++;
                }
            }
        }
        
        ESP_LOGD(TAG, "DNS query for: %s", domain_name);

        memset(tx_buffer, 0, sizeof(tx_buffer));
        dns_header_t* response_header = (dns_header_t*)tx_buffer;
        
        response_header->id = header->id;
        response_header->flags = htons(0x8180);  // Standard response, no error
        response_header->qdcount = header->qdcount;  // Copy original question count
        response_header->ancount = htons(1);  // We provide one answer
        response_header->nscount = 0;
        response_header->arcount = 0;

        int response_len = sizeof(dns_header_t);
        
        // Copy all questions from the original query
        uint8_t* question_start = rx_buffer + sizeof(dns_header_t);
        uint8_t* ptr = question_start;
        
        // Parse through all questions to find total length
        for (int q = 0; q < question_count && (ptr - rx_buffer) < len; q++) {
            // Skip domain name
            while (*ptr != 0 && (ptr - rx_buffer) < len) {
                if ((*ptr & 0xC0) == 0xC0) {
                    // Compressed name - skip 2 bytes
                    ptr += 2;
                    break;
                } else {
                    // Regular label - skip length + label
                    ptr += *ptr + 1;
                }
            }
            if (*ptr == 0) ptr++;  // Skip null terminator
            ptr += sizeof(dns_question_t);  // Skip qtype and qclass
        }
        
        int all_questions_len = ptr - question_start;
        
        if (response_len + all_questions_len > sizeof(tx_buffer)) {
            ESP_LOGW(TAG, "DNS response would be too large");
            continue;
        }
        
        // Copy all questions to response
        memcpy(tx_buffer + response_len, question_start, all_questions_len);
        response_len += all_questions_len;

        dns_answer_t* answer = (dns_answer_t*)(tx_buffer + response_len);
        answer->name = htons(0xC00C);
        answer->type = htons(1);
        answer->class_ = htons(1);
        answer->ttl = htonl(0);  // Zero TTL for immediate response
        answer->rdlength = htons(4);
        
        // Get IP address based on domain configuration
        esp_ip4_addr_t response_ip = get_response_ip(domain_name);
        answer->rdata = response_ip.addr;
        
        response_len += sizeof(dns_answer_t);

        sendto(dns_socket, tx_buffer, response_len, 0,
               (struct sockaddr*)&client_addr, client_addr_len);
        
        ESP_LOGD(TAG, "DNS query redirected to captive portal");
    }

    if (dns_socket >= 0) {
        close(dns_socket);
        dns_socket = -1;
    }
    
    ESP_LOGI(TAG, "DNS server stopped");
    vTaskDelete(NULL);
}

esp_err_t dns_server_start(void)
{
    if (dns_server_running) {
        ESP_LOGW(TAG, "DNS server already running");
        return ESP_OK;
    }

    dns_server_running = true;
    
    if (xTaskCreate(dns_server_task, "dns_server", 4096, NULL, 5, &dns_task_handle) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create DNS server task");
        dns_server_running = false;
        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t dns_server_stop(void)
{
    if (!dns_server_running) {
        ESP_LOGW(TAG, "DNS server not running");
        return ESP_OK;
    }

    dns_server_running = false;
    
    if (dns_socket >= 0) {
        close(dns_socket);
        dns_socket = -1;
    }

    if (dns_task_handle) {
        vTaskDelete(dns_task_handle);
        dns_task_handle = NULL;
    }

    ESP_LOGI(TAG, "DNS server stop requested");
    return ESP_OK;
}

esp_err_t dns_server_start_with_config(const dns_server_config_t *config)
{
    if (!config) {
        ESP_LOGE(TAG, "DNS server config cannot be NULL");
        return ESP_ERR_INVALID_ARG;
    }
    
    if (config->num_of_entries > DNS_SERVER_MAX_ITEMS) {
        ESP_LOGE(TAG, "Too many DNS entries: %d (max: %d)", config->num_of_entries, DNS_SERVER_MAX_ITEMS);
        return ESP_ERR_INVALID_ARG;
    }
    
    // Copy configuration
    memcpy(&s_dns_config, config, sizeof(dns_server_config_t));
    s_config_set = true;
    
    ESP_LOGI(TAG, "DNS server configured with %d entries:", config->num_of_entries);
    for (int i = 0; i < config->num_of_entries; i++) {
        ESP_LOGI(TAG, "  [%d] %s -> " IPSTR, i, config->item[i].name, 
                 IP2STR(&config->item[i].ip));
    }
    
    // Start the DNS server
    return dns_server_start();
}