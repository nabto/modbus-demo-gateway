/*
 * Copyright (C) Nabto - All Rights Reserved.
 */
#ifndef _UART_H_
#define _UART_H_

#include <unabto/unabto_env_base.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    UART_PARITY_NONE,
    UART_PARITY_EVEN,
    UART_PARITY_ODD,
    UART_PARITY_MARK,
    UART_PARITY_SPACE
} uart_parity;

typedef enum
{
    UART_STOPBITS_ONE,
    UART_STOPBITS_TWO
} uart_stopbits;

struct uart_if { // possible rename
    void (*shutdown)(uint8_t channel);

    uint16_t (*can_read)(uint8_t channel);
    uint16_t (*can_write)(uint8_t channel);

    uint8_t (*read)(uint8_t channel);
    void (*read_buffer)(uint8_t channel, void* buffer, uint16_t length);
    void (*write)(uint8_t channel, uint8_t value);
    void (*write_buffer)(uint8_t channel, const void* buffer, uint16_t length);

    void (*flush_receiver)(uint8_t channel);
    void (*flush_transmitter)(uint8_t channel);
};

void uart_initialize_as_unix_uart(struct uart_if* uartIf, uint8_t channel, void* name, uint32_t baudrate, uint8_t databits, uart_parity parity, uart_stopbits stopbits);


#ifdef __cplusplus
} //extern "C"
#endif

#endif
