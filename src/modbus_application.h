#ifndef _MODBUS_APPLICATION_H_
#define _MODBUS_APPLICATION_H_

#include <unabto_platform_types.h>
#include "uart.h"


void modbus_read_application_settings(void);
void modbus_application_initialize_with_uart(void);
void modbus_application_tick(void);

void modbus_application_set_device_name(const char* name);
void modbus_application_set_device_product(const char* product);
void modbus_application_set_device_icon_(const char* icon);


extern uint32_t unabto_shouldrun;

extern uint32_t baudrate;
extern uart_parity parity;
extern uart_stopbits stopbits;
extern uint32_t modbus_number_of_addresses;
extern uint32_t scanner_transmission_attempts;
extern uint32_t first_address_to_scan;
extern bool modbus_tcp_slave_enabled;

#endif
