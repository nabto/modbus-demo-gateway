#ifndef _UNABTO_CONFIG_H_
#define _UNABTO_CONFIG_H_

#define NABTO_ENABLE_STATUS_CALLBACKS 0



// Default for gateway application
#define DEFAULT_BAUDRATE                                            19200
#define DEFAULT_STOPBITS                                            UART_STOPBITS_ONE
#define DEFAULT_PARITY                                              UART_PARITY_EVEN
#define DEFAULT_MODBUS_NUMBER_OF_ADDRESSES                          240
#define MODBUS_NUMBER_OF_BUSSES                                     1


#define UART_NUMBER_OF_CHANNELS                                     MODBUS_NUMBER_OF_BUSSES

#define MAXIMUM_NUMBER_OF_SIMULTANEOUS_MODBUS_QUERIES               10

#define NABTO_ENABLE_REMOTE_CONNECTION                              1
#define NABTO_ENABLE_LOCAL_CONNECTION                               1
#define NABTO_ENABLE_LOCAL_ACCESS_LEGACY_PROTOCOL                   0

#define NABTO_REQUEST_MAX_SIZE                                      1400
#define NABTO_RESPONSE_MAX_SIZE                                     1400
#define NABTO_CONNECTIONS_SIZE                                      20
#define NABTO_SET_TIME_FROM_ALIVE                                   0

#define NABTO_ENABLE_STREAM                                         0

#define NABTO_ENABLE_CLIENT_ID                                      1
#define NABTO_ENABLE_CONNECTION_ESTABLISHMENT_ACL_CHECK             1

#define NABTO_ENABLE_LOGGING						                1
#define NABTO_LOG_ALL                                               1
//#define NABTO_LOG_MODULE_FILTER                                     NABTO_LOG_MODULE_ALL & ~(NABTO_LOG_MODULE_PERIPHERAL | NABTO_LOG_MODULE_ENCRYPTION | NABTO_LOG_MODULE_NETWORK)
#define NABTO_LOG_MODULE_FILTER                                     NABTO_LOG_MODULE_MODBUS
#define NABTO_LOG_SEVERITY_FILTER                                   NABTO_LOG_SEVERITY_WARN

#define NABTO_APPLICATION_EVENT_MODEL_ASYNC                         1
#define NABTO_APPREQ_QUEUE_SIZE                                     10

#endif
