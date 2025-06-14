#pragma once

#include <inttypes.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"

// Potentiometer section
#define POTENTIOMETER_ADC_CHANNEL ADC1_CHANNEL_6 // GPIO34
#define ADC_WIDTH ADC_WIDTH_BIT_12               // 12-bit ADC (0-4095)
#define DEFAULT_VREF 1100                        // Use default reference voltage in mV
// Potentiometer section


 void initDebugGPIO();

