/*
 * Copyright (C) Nabto - All Rights Reserved.
 */
#ifndef _UART_UNIX_H_
#define _UART_UNIX_H_

#include <unabto/unabto_env_base.h>
#include "uart.h"

#ifdef __cplusplus
extern "C" {
#endif

void uart_initialize_as_unix_uart(struct uart_if* uartIf, uint8_t channel, void* name, uint32_t baudrate, uint8_t databits, uart_parity parity, uart_stopbits stopbits);


#ifdef __cplusplus
} //extern "C"
#endif

#endif
