#pragma once
#include "driver/uart.h"

#define UART_PORT_NUM UART_NUM_0
#define UART_BAUD_RATE 115200
#define UART_BUFFER_SIZE (1024)

void initUART();