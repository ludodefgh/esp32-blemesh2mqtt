#include "debugGPIO.h"

#include "esp_log.h"
#include "driver/adc.h"
#include "esp_adc_cal.h"
#include "driver/gpio.h"

#define TAG "DEBUG_GPIO"
#define BUTTON_GPIO GPIO_NUM_18  // Use the appropriate GPIO number for your button
#define BUTTON_GPIO2 GPIO_NUM_19 // Use the appropriate GPIO number for your button
#define DEBOUNCE_TIME_MS 200     // Debounce time in milliseconds

extern void SendBrightness();
extern void SendGenericOnOffToggle();
extern void NextMode();

/// @brief button stuff
static TimerHandle_t debounce_timer;
//static TimerHandle_t debounce_timer2;

static esp_adc_cal_characteristics_t adc_chars;

// Function prototype
void IRAM_ATTR gpio_isr_handler(void *arg);
void debounce_timer_callback(TimerHandle_t xTimer);

extern "C" void initDebugGPIO()
{
    ESP_LOGI(TAG, "initDebugGPIO");
    // Configure button GPIO
    gpio_config_t io_conf = {};

        io_conf.intr_type = GPIO_INTR_NEGEDGE; // Interrupt on falling edge
        io_conf.mode = GPIO_MODE_INPUT;
        io_conf.pin_bit_mask = (1ULL << BUTTON_GPIO) | (1ULL << BUTTON_GPIO2);
        io_conf.pull_up_en = GPIO_PULLUP_ENABLE; // Enable internal pull-up
        io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    gpio_config(&io_conf);
    // Create debounce timer
    debounce_timer = xTimerCreate("debounce_timer", pdMS_TO_TICKS(DEBOUNCE_TIME_MS), pdFALSE, NULL, debounce_timer_callback);

    // Install GPIO ISR service
    gpio_install_isr_service(0);
    gpio_isr_handler_add(BUTTON_GPIO, gpio_isr_handler, NULL);
    gpio_isr_handler_add(BUTTON_GPIO2, gpio_isr_handler, NULL);

    /////////////////////////////////////////////////////////////////////////////
    // Potentiometer inputs
    /////////////////////////////////////////////////////////////////////////////
    // Configure ADC
    adc1_config_width(ADC_WIDTH);
    adc1_config_channel_atten(POTENTIOMETER_ADC_CHANNEL, ADC_ATTEN_DB_12); // 0-3.3V range

    // Characterize ADC for voltage conversion
    esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_12, ADC_WIDTH, DEFAULT_VREF, &adc_chars);
}

void gpio_isr_handler(void *arg)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    // Start debounce timer (from ISR)
    xTimerStartFromISR(debounce_timer, &xHigherPriorityTaskWoken);

    if (xHigherPriorityTaskWoken)
    {
        portYIELD_FROM_ISR();
    }
}

void debounce_timer_callback(TimerHandle_t xTimer)
{
    if (gpio_get_level(BUTTON_GPIO) == 0)
    {
        SendGenericOnOffToggle();
    }
    
    if (gpio_get_level(BUTTON_GPIO2) == 0)
    {
        NextMode();
    }
}