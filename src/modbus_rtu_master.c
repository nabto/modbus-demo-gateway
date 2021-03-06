/*
 * Copyright (C) Nabto - All Rights Reserved.
 */
#define NABTO_LOG_MODULE_CURRENT NABTO_LOG_MODULE_MODBUS

#include "modbus_rtu_master.h"
#include "modbus_rtu_crc.h"
#include <modules/util/list_malloc.h>
#include <modules/util/memory_allocation.h>
#include <modbus_application.h>
#include <unabto/unabto_external_environment.h>
#include <unabto/unabto_util.h>

// The maximum time allowed to pass before the response must be initiated
// (time from the last byte sent from the master to the first byte received at the master)
#define DEFAULT_RESPONSE_TIMEOUT                                    500 
#define DEFAULT_MAXIMUM_TRANSMISSION_ATTEMPTS                       3

#define BITS_PER_CHARACTER          (1 + 8 + 1 + 1)
#define FRAME_SILENT_DURATION       MAX(1, (((BITS_PER_CHARACTER * 3500000) / baudrate) / 1000))

typedef enum
{
    BUS_STATE_IDLE,
    BUS_STATE_BUSY
} modbus_state;

typedef struct
{
    int identifier;
    list messageQueue;
    uint8_t uartChannel;
    modbus_state state;
    uint8_t response[MODBUS_MAXIMUM_FRAME_SIZE];
    uint16_t responseSize;
    bool responseOverrun;
    nabto_stamp_t frameCommitTimeout; // the timestamp where the frame is considered completed
    nabto_stamp_t responseTimeout; // a response should be received within this limit
} modbus;

static void retransmit_current_message(modbus* bus);
static uint32_t calculate_transmission_time(uint32_t numberOfBytes);

const char* modbusMessageStateNames[] = 
{
    "Allocated",
    "Queued",
    "Transferring",
    "Completed",
    "Failed",
    "Discarded"
};

static const char* modbusStateNames[] = 
{
    "Idle",
    "Busy"
};

static modbus busses[MODBUS_NUMBER_OF_BUSSES];
static list completedList;
struct uart_if* uart;

void modbus_rtu_master_initialize(struct uart_if* uartPtr)
{
    uint8_t i;
    modbus* bus;
    uart = uartPtr;
    
    // reset all busses
    for(i = 0; i < MODBUS_NUMBER_OF_BUSSES; i++)
    {
        bus = &busses[i];
        bus->identifier = i;
        bus->uartChannel = i;
        list_initialize(&bus->messageQueue);
        bus->state = BUS_STATE_IDLE;
    }

    list_initialize(&completedList);
    
    NABTO_LOG_INFO(("Initialized."));
    NABTO_LOG_TRACE(("Silent:%i",FRAME_SILENT_DURATION));
}

void modbus_rtu_master_tick(void)
{
    modbus* bus;
    
    // tick all busses
    for(bus = busses; bus < (busses + MODBUS_NUMBER_OF_BUSSES); bus++) {
        modbus_state oldState;
        
        oldState = bus->state;
        
        switch(bus->state) {
        case BUS_STATE_IDLE:
        {
            modbus_message* message;
            // is there a message waiting to be sent
            if(list_peek_first(&bus->messageQueue, (void**)&message)) {
                
                // has message been discared by the application while waiting in the queue?
                if(message->state == MODBUS_MESSAGE_STATE_DISCARDED) {
                    list_remove(&bus->messageQueue, message);
                    modbus_rtu_master_release_message(message);
                    
                    NABTO_LOG_TRACE(("Completing discard request (bus=%u, query=%u).", bus->identifier, *message));
                }
                // else, start transferring the message
                else {
                    uint32_t compensatedReponseTime = message->maximumResponsetime +
                        calculate_transmission_time(message->frameSize);
                    
                    NABTO_LOG_TRACE(("Sending query (bus=%u, timeout=%u, compensated timeout=%u).",
                                     bus->identifier, (int)message->maximumResponsetime, (int)compensatedReponseTime));
                    
                    uart->flush_receiver(bus->uartChannel);
                    uart->write_buffer(bus->uartChannel, message->frame, message->frameSize);
                    
                    message->state = MODBUS_MESSAGE_STATE_TRANSFERRING;
                    
                    // time to wait for start of response: transmission time of outgoing frame + maximum response time
                    nabtoSetFutureStamp(&bus->responseTimeout, compensatedReponseTime);
                    bus->responseSize = 0;
                    bus->responseOverrun = false;
                    
                    bus->state = BUS_STATE_BUSY;
                    
                    NABTO_LOG_TRACE(("Query sent (bus=%u, timeout=%u, compensated timeout=%u).",
                                     bus->identifier, (int)message->maximumResponsetime, (int)compensatedReponseTime));
                    NABTO_LOG_BUFFER(NABTO_LOG_SEVERITY_LEVEL_TRACE,
                                     ("Query (length=%u): (shown with CRC)",
                                      (int)message->frameSize), message->frame, message->frameSize);
                }
            }
        }
        break;
        
        case BUS_STATE_BUSY: {
            uint16_t length;
            
            length = uart->can_read(bus->uartChannel);

            // has data been received?
            if(length > 0) {
                NABTO_LOG_TRACE(("Need to read on modbus"));
                
                // ignore all data after an overrun has occurred
                if(bus->responseOverrun == false) {
                    
                    // is there room for the data?
                    if((bus->responseSize + length) <= MODBUS_MAXIMUM_FRAME_SIZE) {
                        // add data to response buffer
                        uart->read_buffer(bus->uartChannel, bus->response + bus->responseSize, length);
                        bus->responseSize += length;
                        NABTO_LOG_TRACE(("Received response (bus=%u, length=%u)!", bus->identifier, (int)length ));
                    }
                    // response buffer overrun
                    else {
                        uart->flush_receiver(bus->uartChannel); // discard data
                        bus->responseOverrun = true; // mark response as over size
                        NABTO_LOG_TRACE(("Receiving oversize response (bus=%u, oversize length=%u)!",
                                         bus->identifier, (int)((bus->responseSize + length) - MODBUS_MAXIMUM_FRAME_SIZE)));
                    }
                } // bus->responseOverrun != false
                else {
                    uart->flush_receiver(bus->uartChannel); // discard data
                    NABTO_LOG_TRACE(("Dumping overrun data (bus=%u, length=%u)!", bus->identifier, (int)length));
                }
                
                nabtoSetFutureStamp(&bus->frameCommitTimeout, FRAME_SILENT_DURATION); // reset packet commit timer
            }
            
            // has the response begun
            else if(bus->responseSize > 0) {
                NABTO_LOG_TRACE(("Need to parse recieved buffer"));
                // has the frame ended
                if(nabtoIsStampPassed(&bus->frameCommitTimeout)) {
                    modbus_message* message;
                    list_peek_first(&bus->messageQueue, (void**)&message);

                    // has the message been discarded
                    if(message->state == MODBUS_MESSAGE_STATE_DISCARDED) {
                        list_remove(&bus->messageQueue, message);
                        
                        modbus_rtu_master_release_message(message); // perform actual release of discarded message
                        
                        bus->state = BUS_STATE_IDLE;
                        
                        NABTO_LOG_TRACE(("Received response for dumped query (bus=%u, query=%u, response length=%u).",
                                         bus->identifier, *message, (int)bus->responseSize));
                    } else {
                        // does reponse pass CRC check?
                        if(modbus_rtu_crc_verify_crc_field(bus->response, bus->responseSize)) {
                            NABTO_LOG_BUFFER(NABTO_LOG_SEVERITY_LEVEL_TRACE,
                                             ("Received response (bus=%u, length=%u): (shown with CRC)",
                                              bus->identifier, (int)bus->responseSize), bus->response, bus->responseSize);

                            // replace the query in the message with the response (removing CRC)
                            memcpy(message->frame, bus->response, bus->responseSize - 2);
                            message->frameSize = bus->responseSize - 2;
                            
                            message->state = MODBUS_MESSAGE_STATE_COMPLETED; // mark message as completed
                            
                            // move message from transfer queue to completed list
                            list_remove(&bus->messageQueue, message);
                            list_append(&completedList, message);
                            
                            bus->state = BUS_STATE_IDLE;
                            
                            NABTO_LOG_TRACE(("Received response for query (bus=%u, query=%u).", bus->identifier, *message));
                        } else {
                            NABTO_LOG_TRACE(("Received bad response for query (bus=%u, query=%u)!", bus->identifier, *message));
                            retransmit_current_message(bus);
                        }
                    }
                } else {
                    NABTO_LOG_TRACE(("Frame has not ended yet (need to wait at least 3.5 chars time)"
                                     " timenow:(%i) frameCommitTimeout(%i)", nabtoGetStamp(), &bus->frameCommitTimeout));
                }
            } else
                // the response has not begun within the time limit
                if(nabtoIsStampPassed(&bus->responseTimeout)) {
                    modbus_message* message;
                    list_peek_first(&bus->messageQueue, (void**)&message);
                    
                    NABTO_LOG_TRACE(("No response received (bus=%u, message=%u)!", bus->identifier, *message));

                    // has the application discarded the message?
                    if(message->state == MODBUS_MESSAGE_STATE_DISCARDED) {
                        list_remove(&bus->messageQueue, message);
                        
                        modbus_rtu_master_release_message(message); // perform actual release of discarded message
                        
                        bus->state = BUS_STATE_IDLE;
                        
                        NABTO_LOG_TRACE(("Completing discard request (bus=%u, query=%u).", bus->identifier, *message));
                    }
                    // no - just continue and retransmit the message
                    else {
                        retransmit_current_message(bus);
                    }
                }
        }
            break;
        }
        
        if(oldState != bus->state) {
            NABTO_LOG_TRACE(("State change in bus %u: %s -> %s", bus->identifier,
                             modbusStateNames[oldState], modbusStateNames[bus->state]));
        }
    }
}

modbus_message* modbus_rtu_master_allocate_message(void)
{
    modbus_message* message = (modbus_message*) checked_malloc(sizeof(modbus_message));
    
    if(message != NULL) {
        message->state = MODBUS_MESSAGE_STATE_ALLOCATED;
        // set default maximum number of transmission attempts
        message->remainingTransmissionAttempts = DEFAULT_MAXIMUM_TRANSMISSION_ATTEMPTS; 
        message->maximumResponsetime = DEFAULT_RESPONSE_TIMEOUT;
        message->deferredRetransmissions = false;
        NABTO_LOG_TRACE(("Allocated message (message=%u).", *message));
    } else {
        NABTO_LOG_TRACE(("Message allocation failed (no free messages)!"));
    }
    
    return message;
}

void modbus_rtu_master_release_message(modbus_message* message)
{
    switch(message->state) {
    case MODBUS_MESSAGE_STATE_ALLOCATED:
        // message is held solely by the application
        break;
        
    case MODBUS_MESSAGE_STATE_QUEUED:
    case MODBUS_MESSAGE_STATE_TRANSFERRING:
        // can't remove from the transfer queue once inserted - signal pending release
        message->state = MODBUS_MESSAGE_STATE_DISCARDED; 
        NABTO_LOG_TRACE(("Query discard requested (query=%u).", *message));
        return;
        
    case MODBUS_MESSAGE_STATE_COMPLETED: // completed and failed messages are both placed in the completed list
    case MODBUS_MESSAGE_STATE_FAILED:
        list_remove(&completedList, message);
        break;
        
    case MODBUS_MESSAGE_STATE_DISCARDED:
        break;
    }

    checked_free(message);

    NABTO_LOG_TRACE(("Released query (query=%u).", *message));
    NABTO_LOG_TRACE(("--------------------------------------"));
}

bool modbus_rtu_master_transfer_message(modbus_message* message) {
    modbus* bus;

    // invalid bus
    if(message->bus >= MODBUS_NUMBER_OF_BUSSES) {
        NABTO_LOG_TRACE(("Attempted to enqueue a message on an invalid bus (query=%u)!", *message));
        return false;
    }
    
    bus = &busses[message->bus];

    // not enough space to add CRC
    if((message->frameSize + 2) > MODBUS_MAXIMUM_FRAME_SIZE) {
        NABTO_LOG_TRACE(("Attempted to enqueue an oversize message (bus=%u, query=%u)!", (int)bus->identifier, *message));
        return false;
    }

    // add CRC
    message->frameSize += 2;
    modbus_rtu_crc_update_crc_field(message->frame, message->frameSize);

    // add to the end of the transfer queue
    if(list_append(&bus->messageQueue, message)) {
        message->state = MODBUS_MESSAGE_STATE_QUEUED;
        NABTO_LOG_TRACE(("Query has been queued (bus=%u, query=%u, length=%u).",
                         bus->identifier, *message, (int)message->frameSize));

        // eager tick when queue was empty
        if(list_count(&bus->messageQueue) == 1) {
            NABTO_LOG_TRACE(("Performing eager tick as message queue was empty (bus=%u).", bus->identifier));
            modbus_rtu_master_tick();
        }
        
        return true;
    } else {
        NABTO_LOG_ERROR(("Unable to enqueued query (bus=%u, query=%u, length=%u)!",
                         bus->identifier, *message, (int)message->frameSize));
        return false;
    }
}

// function creation helper functions


// privates

static void retransmit_current_message(modbus* bus)
{
    modbus_message* message;
    list_peek_first(&bus->messageQueue, (void**)&message);

    // retransmit message or drop it?
    if(--message->remainingTransmissionAttempts > 0) {
        uint32_t compensatedReponseTimeout;
        
        if(message->deferredRetransmissions) {
            // should a deferred retransmission be attempted and does it make any sense to defer
            // (only makes sense if more than one packet is in the queue)
            if(list_count(&bus->messageQueue) > 1) {
                // add to end of queue
                if(list_append(&bus->messageQueue, message)) {
                    list_remove(&bus->messageQueue, message); // remove current/first instance of message in transfer queue
                    message->state = MODBUS_MESSAGE_STATE_QUEUED;
                    bus->state = BUS_STATE_IDLE;
                    NABTO_LOG_TRACE(("Deferring retransmission of query (bus=%u, query=%u).", bus->identifier, *message));
                    return;
                }
            
                bus->state = BUS_STATE_IDLE;
                NABTO_LOG_TRACE(("Unable to defer query - retransmitting immediately (bus=%u, query=%u)!", bus->identifier, *message));
            } else {
                NABTO_LOG_TRACE(("Query allowed deferrence but transfer queue is empty (bus=%u, query=%u).", bus->identifier, *message));
            }
        }
        
        // retransmit immediately (also used as fallback if deferred retransmission fails)
        compensatedReponseTimeout = (uint32_t)message->maximumResponsetime + calculate_transmission_time(message->frameSize);
        
        uart->flush_receiver(bus->uartChannel);
        uart->write_buffer(bus->uartChannel, message->frame, message->frameSize);

        nabtoSetFutureStamp(&bus->responseTimeout, compensatedReponseTimeout); // time to wait for start of response: transmission time of outgoing frame + maximum response time
        bus->responseSize = 0;
        bus->responseOverrun = false;
        
        NABTO_LOG_TRACE(("Retransmitting query (bus=%u, remaining attempts=%u).", bus->identifier, (int)message->remainingTransmissionAttempts));
    } else {
        // mark message as failed
        message->state = MODBUS_MESSAGE_STATE_FAILED;
        
        // move message from transfer queue to completed list
        list_remove(&bus->messageQueue, message);
        list_append(&completedList, message);
        
        bus->state = BUS_STATE_IDLE;

        NABTO_LOG_TRACE(("Dropped query due too many retransmissions (bus=%u, message=%u).", bus->identifier, *message));
    }
}

// Calculate the number of milliseconds it takes to send the specified number
// of bytes at the current baudrate (actual wire time).
static uint32_t calculate_transmission_time(uint32_t numberOfBytes) {
    return ((numberOfBytes * BITS_PER_CHARACTER * 1000000) / baudrate) / 1000;
}
