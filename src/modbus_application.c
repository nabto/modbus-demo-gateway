#define NABTO_LOG_MODULE_CURRENT NABTO_LOG_MODULE_APPLICATION

#include "modbus_application.h"
#include <modbus_rtu_master.h>
#include <modules/util/list_malloc.h>
#include <modules/util/memory_allocation.h>
#include <modules/acl/acl.h>
#include <modules/configuration_store/configuration_store.h>
#include <modules/settings/settings.h>

#include <unabto/unabto_connection.h>
#include <unabto/unabto_app.h>
#include <unabto/unabto_util.h>
#include <unabto/util/unabto_buffer.h>

#include <stdio.h>
#include <modules/fingerprint_acl/fp_acl_ae.h>
#include <modules/fingerprint_acl/fp_acl_memory.h>
#include <modules/fingerprint_acl/fp_acl_file.h>

// address 1 , regs array (next lines)
// Name:Temp type:number register:0001
// Name:Mode type:number register:0002

// For debugging purposes only
#define MODBUS_CONFIGURATION "{'a':1," \
                             " 'r':[{'n':'Temperature','t':'n','r':'0003', 'f':'r/10'}," \
                             "      {'n':'Resitance','t':'n','r':'0005'}]" \
                             "}"


#define SCANNER_RESPONSE_TIMEOUT                                    1000

static char modbus_configuration_filename[1400];
static char modbus_configuration[1400];

enum
{
    // Modbus/device related queries
    QUERY_MODBUS_FUNCTION = 20000,
    QUERY_MODBUS_CONFIGURATION = 20001,

};

enum
{
    QUERY_STATUS_OK,
    QUERY_STATUS_ERROR,
    QUERY_STATUS_OUT_OF_RESOURCES,
    QUERY_STATUS_USER_NOT_FOUND,
    QUERY_STATUS_SETTING_NOT_FOUND
};

enum
{
    MODBUS_FUNCTION_RESULT_SUCCESS,
    MODBUS_FUNCTION_RESULT_OUT_OF_RESOURCES,
    MODBUS_FUNCTION_RESULT_NO_REPLY,
    MODBUS_FUNCTION_RESULT_INVALID_BUS,
    MODBUS_FUNCTION_RESULT_OVERSIZE_FRAME
};

typedef struct
{
    bool present;
    uint16_t number;
    uint16_t version;
    uint16_t type;
} device;

typedef struct
{
    bool active;
    uint8_t address;
    modbus_message* message;
} modbus_scanner;

typedef struct
{
    application_request* request;
    list messages;
} modbus_query;



// div private functions forward declerations
static modbus_query* find_query(application_request* request);
static void release_messages(list* messages);
static void release_query(modbus_query* query);
static application_event_result abort_modbus_function(modbus_query* query, buffer_write_t* writeBuffer,
                                                      uint8_t bus, uint8_t address, uint8_t result);
static application_event_result abort_modbus_function_batch(modbus_query* query,
                                                            buffer_write_t* writeBuffer, uint8_t result);
static bool read_string_null_terminated_modbus(unabto_query_request* read_buffer, char* out, size_t outlen);


void application_initialize(void);

#define MODBUS_NUMBER_OF_ADDRESSES                                  248

// the list of Modbus queries currently in progress
static list modbusQueries; 

// Configuration status stuff
uint32_t unabto_shouldrun;
uint32_t baudrate;
uart_parity parity;
uart_stopbits stopbits;
struct uart_if uart;
uint32_t modbus_number_of_addresses = 240;



// From AMP-stub
#define DEVICE_NAME_DEFAULT "Nabto modbus gateway demo"
#define MAX_DEVICE_NAME_LENGTH 50
static char device_name_[MAX_DEVICE_NAME_LENGTH];
static const char* device_product_ = "Modbus gateway demo";
static const char* device_icon_ = "chip-small.png";
static const char* device_interface_id_ = "DC14A962-39C7-4067-8EC6-6A491E45E283";
static uint16_t device_interface_version_major_ = 1;
static uint16_t device_interface_version_minor_ = 0;

static struct fp_acl_db db_;
struct fp_mem_persistence fp_file_;

#define REQUIRES_GUEST FP_ACL_PERMISSION_NONE
#define REQUIRES_OWNER FP_ACL_PERMISSION_ADMIN


static char uart_name_[1024];


void debug_dump_acl();

void modbus_application_set_device_name(const char* name) {
    strncpy(device_name_, name, MAX_DEVICE_NAME_LENGTH);
}

void modbus_application_set_device_product(const char* product) {
    device_product_ = product;
}

void modbus_application_set_device_icon_(const char* icon) {
    device_icon_ = icon;
}



/**
 * Read settings from file
 */
void modbus_read_application_settings(void)
{
    int baudrateSetting;
    char paritySetting[100];
    char stopbitsSetting[100];

    baudrate = DEFAULT_BAUDRATE;
    if(settings_read_int("baudrate", &baudrateSetting))
    {
        baudrate = (int)baudrateSetting;
    }

    parity = DEFAULT_PARITY;
    if(settings_read_string("parity", paritySetting))
    {
        if(strcmp(paritySetting, "none") == 0)
        {
            parity = UART_PARITY_NONE;
        }
        else if(strcmp(paritySetting, "even") == 0)
        {
            parity = UART_PARITY_EVEN;
        }
        else if(strcmp(paritySetting, "odd") == 0)
        {
            parity = UART_PARITY_ODD;
        }
        else
        {
            NABTO_LOG_ERROR(("Bad parity setting (value='%s')!", paritySetting));
        }
    }
    if(!settings_read_string("uart", uart_name_)) {
      NABTO_LOG_ERROR(("No uart specified in settings. Please specify uart device to listen to"));
      exit(-1);
    }

    stopbits = DEFAULT_STOPBITS;
    if(settings_read_string("stopbits", stopbitsSetting))
    {
        if(strcmp(stopbitsSetting, "1") == 0)
        {
            stopbits = UART_STOPBITS_ONE;
        }
        else if(strcmp(stopbitsSetting, "2") == 0)
        {
            stopbits = UART_STOPBITS_TWO;
        }
        else
        {
            NABTO_LOG_ERROR(("Bad stopbits setting (value='%s')!", stopbitsSetting));
        }
    }

    NABTO_LOG_TRACE(("UART configuration: baudrate=%u, parity=%u, stopbits=%u", baudrate, parity, stopbits));



    modbus_number_of_addresses = DEFAULT_MODBUS_NUMBER_OF_ADDRESSES;
    if(!settings_read_int("modbus_number_of_addresses", (int *)&modbus_number_of_addresses)) {
      NABTO_LOG_ERROR(("Bad setting for 'modbus_number_of_addresses'"));
    }

    if(settings_read_string("modbus_configuration", modbus_configuration_filename)) {
      NABTO_LOG_ERROR(("Could not read 'modbus_configuration' from settingsfile"));
    }


    NABTO_LOG_TRACE(("Modbus configuration: modbus_number_of_addresses=%u",modbus_number_of_addresses));

}

void modbus_read_modbus_configuration() {
   FILE *fp;
   long length;
  
   // make sure the string is terminated 
   modbus_configuration[0] = 0;

   
   NABTO_LOG_TRACE(("Will open modbusconfig file"));
   fp = fopen(modbus_configuration_filename, "r");  /* open file for input */
  
   if (fp) {   
     fseek (fp, 0, SEEK_END);
     length = ftell(fp);
     fseek (fp, 0, SEEK_SET);
     if(length>sizeof(modbus_configuration)-1) {
       NABTO_LOG_ERROR(("Modbus configuration file is to large"));
     }
     fread(modbus_configuration, 1, length, fp); /* read the configuration from the file */
     modbus_configuration[length] = '\0';
   }
   else {
      NABTO_LOG_ERROR(("Bad modbus_configuration file."));
      modbus_configuration[0] = 0;
      return;
  }
  fclose(fp);  /* close the input file */

   // For debugging: strncpy(modbus_configuration, MODBUS_CONFIGURATION, sizeof(modbus_configuration));
   // Replace ' with "
   modbus_configuration[sizeof(modbus_configuration)-1] = 0;
   for(int i=0;i<sizeof(modbus_configuration);i++) {
     if(modbus_configuration[i] == '\'')
       modbus_configuration[i] = '\"';
   }
   NABTO_LOG_TRACE(("Modbus JSON configuration: %s", modbus_configuration));

	
}


void modbus_application_initialize_with_uart(void) {


    struct fp_acl_settings default_settings;
    NABTO_LOG_WARN(("WARNING: Remote access to the device is turned on by default. Please read TEN36 \"Security in Nabto Solutions\" to understand the security implications."));
    default_settings.systemPermissions =
        FP_ACL_SYSTEM_PERMISSION_PAIRING |
        FP_ACL_SYSTEM_PERMISSION_LOCAL_ACCESS |
        FP_ACL_SYSTEM_PERMISSION_REMOTE_ACCESS;
    default_settings.defaultUserPermissions =
        FP_ACL_PERMISSION_LOCAL_ACCESS;
    default_settings.firstUserPermissions =
        FP_ACL_PERMISSION_ADMIN |
        FP_ACL_PERMISSION_LOCAL_ACCESS |
        FP_ACL_PERMISSION_REMOTE_ACCESS;
    
    if (fp_acl_file_init("persistence.bin", "tmp.bin", &fp_file_) != FP_ACL_DB_OK) {
        NABTO_LOG_ERROR(("cannot load acl file"));
        exit(1);
    }
    fp_mem_init(&db_, &default_settings, &fp_file_);
    fp_acl_ae_init(&db_);
    snprintf(device_name_, sizeof(device_name_), DEVICE_NAME_DEFAULT);
    debug_dump_acl();

    

    modbus_read_application_settings();
    modbus_read_modbus_configuration();

    uart_initialize_as_unix_uart(&uart, 0, uart_name_, baudrate, 8, parity, stopbits);

    list_initialize(&modbusQueries);
    modbus_rtu_master_initialize(&uart);

    

    
}




static application_event_result begin_modbus_function_query(application_request* request, buffer_read_t* readBuffer, buffer_write_t* writeBuffer)
{
    uint8_t bus;
    uint8_t address;
    buffer_t data;
    modbus_query* query;
    modbus_message* message;

    // allow up to n simultaneous queries
    if(list_count(&modbusQueries) >= MAXIMUM_NUMBER_OF_SIMULTANEOUS_MODBUS_QUERIES)
    {
        NABTO_LOG_TRACE(("Throttling queries."));
        return AER_REQ_OUT_OF_RESOURCES;
    }

    // read input parameters
    if(!buffer_read_uint8(readBuffer, &bus) || !buffer_read_uint8(readBuffer, &address) || !buffer_read_raw_nc(readBuffer, &data))
    {
        return AER_REQ_TOO_SMALL;
    }

    if(bus >= MODBUS_NUMBER_OF_BUSSES)
    {
        return abort_modbus_function(NULL, writeBuffer, bus, address, MODBUS_FUNCTION_RESULT_INVALID_BUS);
    }
    if(data.size > MODBUS_MAXIMUM_FRAME_SIZE)
    {
        return abort_modbus_function(NULL, writeBuffer, bus, address, MODBUS_FUNCTION_RESULT_OVERSIZE_FRAME);
    }

    // create query
    query = (modbus_query*)checked_malloc(sizeof(modbus_query)); // allocate query resource
    if(query == NULL)
    {
        return abort_modbus_function(query, writeBuffer, bus, address, MODBUS_FUNCTION_RESULT_OUT_OF_RESOURCES);
    }

    // initialize query
    query->request = request;
    list_initialize(&query->messages);

    // create message
    message = modbus_rtu_master_allocate_message();
    if(message == NULL)
    {
        NABTO_LOG_ERROR(("Unable to allocate Modbus message!"));
        return abort_modbus_function(query, writeBuffer, bus, address, MODBUS_FUNCTION_RESULT_OUT_OF_RESOURCES);
    }

    // initialize message
    message->bus = bus;
    message->address = address;
    memcpy(message->frame, data.data, data.size); // copy frame to message...
    message->frameSize = data.size; // ...

    // enqueue message for transfer
    if(modbus_rtu_master_transfer_message(message) == false)
    {
        NABTO_LOG_ERROR(("Unable to transfer Modbus message!"));
        return abort_modbus_function(query, writeBuffer, bus, address, MODBUS_FUNCTION_RESULT_OUT_OF_RESOURCES);
    }

    // add message to query
    if(list_append(&query->messages, message) == false)
    {
        NABTO_LOG_ERROR(("Unable to enqueue message in query!"));
        return abort_modbus_function(query, writeBuffer, bus, address, MODBUS_FUNCTION_RESULT_OUT_OF_RESOURCES);
    }

    // add query to list of pending queries
    if(list_append(&modbusQueries, query) == false)
    {
        NABTO_LOG_ERROR(("Unable to enqueue query in query queue!"));
        return abort_modbus_function(query, writeBuffer, bus, address, MODBUS_FUNCTION_RESULT_OUT_OF_RESOURCES);
    }

    return AER_REQ_ACCEPTED;
}
static application_event_result end_modbus_function_query(application_request* request, buffer_read_t* readBuffer, buffer_write_t * writeBuffer, modbus_query* query)
{
    modbus_message* message;
    if(list_peek_first(&query->messages, (void**)&message) == false)
    {
        NABTO_LOG_ERROR(("Unable to get message from query!"));
        release_query(query);
        return AER_REQ_SYSTEM_ERROR;
    }

    if(!buffer_write_uint8(writeBuffer, message->bus) || !buffer_write_uint8(writeBuffer, message->address))
    {
        NABTO_LOG_ERROR(("Unable to write response to client!"));
        release_query(query);
        return AER_REQ_RSP_TOO_LARGE;
    }

    if(message->state == MODBUS_MESSAGE_STATE_COMPLETED) // Modbus slave replied to message
    {
        if(!buffer_write_uint8(writeBuffer, MODBUS_FUNCTION_RESULT_SUCCESS) || !buffer_write_raw_from_array(writeBuffer, message->frame, message->frameSize))
        {
            NABTO_LOG_ERROR(("Unable to write response to client!"));
            release_query(query);
            return AER_REQ_RSP_TOO_LARGE;
        }
    }
    else // Modbus slave did not reply
    {
        if(!buffer_write_uint8(writeBuffer, MODBUS_FUNCTION_RESULT_NO_REPLY) || !buffer_write_raw_from_array(writeBuffer, NULL, 0))
        {
            NABTO_LOG_ERROR(("Unable to write response to client!"));
            release_query(query);
            return AER_REQ_RSP_TOO_LARGE;
        }
    }

    if(!buffer_write_uint8(writeBuffer, QUERY_STATUS_OK))
    {
        NABTO_LOG_ERROR(("Unable to write response to client!"));
        release_query(query);
        return AER_REQ_RSP_TOO_LARGE;
    }

    NABTO_LOG_TRACE(("Application poll ended successfully."));
    release_query(query);
    return AER_REQ_RESPONSE_READY;
}






// poll active asynchronous queries to see if one has completed
bool application_poll_query(application_request** request)
{
    list_element* q;

    list_foreach(&modbusQueries, q)
      {
        modbus_query* query = (modbus_query*)q->data;
	
        switch(query->request->queryId)
	  {
            // Note: all Modbus function queries are async
	  case QUERY_MODBUS_FUNCTION:
            {
	      list_element* m;
	      bool completed = true;
	      
	      // if all messages are completed or failed the query has completed
	      list_foreach(&query->messages, m)
                {
		  modbus_message* message = (modbus_message*)m->data;
		  if(message->state != MODBUS_MESSAGE_STATE_COMPLETED && message->state != MODBUS_MESSAGE_STATE_FAILED)
                    {
		      completed = false;
		      break;
                    }
                }
	      
	      // this request is ready to be returned to the client
	      if(completed)
                {
		  *request = query->request;
		  return true;
                }
            }
            break;
	    
	  default:
            NABTO_LOG_FATAL(("Invalid query was queued!"));
            break;
	  }
      }
    
    return false;
}

// end an asynchronous query that signalled its completion
application_event_result application_poll(application_request* request, buffer_write_t * writeBuffer)
{
    modbus_query* query = find_query(request); // find the query that contains the request
    if(query == NULL)
    {
        NABTO_LOG_FATAL(("An unknown query was polled!"));
        return AER_REQ_SYSTEM_ERROR;
    }

    // remove query from query queue
    if(list_remove(&modbusQueries, query) == false)
    {
        NABTO_LOG_FATAL(("Unable to remove query from query queue!"));
        return AER_REQ_SYSTEM_ERROR;
    }

    switch(query->request->queryId)
    {
    case QUERY_MODBUS_FUNCTION:
        return end_modbus_function_query(request, NULL, writeBuffer, query);


    default:
        NABTO_LOG_FATAL(("Invalid query was queued earlier!"));
        return AER_REQ_SYSTEM_ERROR;
    }
}

// the framework calls this function when a connection is lost when a request is in progress
void application_poll_drop(application_request* request)
{
  modbus_query* query = find_query(request);
  
  if(query == NULL)
    {
      NABTO_LOG_TRACE(("Tried to drop request with no associated query!"));
      //NABTO_LOG_FATAL(("Tried to drop request with no associated query!"));
      return;
    }
  
  switch(query->request->queryId)
    {
    case QUERY_MODBUS_FUNCTION:
      // remove the query from the queue
      if(list_remove(&modbusQueries, query) == false)
        {
	  NABTO_LOG_FATAL(("Query was removed from the query queue unexpectedly!"));
	  return;
        }
      
      release_query(query);
      
      NABTO_LOG_TRACE(("Application event dropped."));
      break;
      
    default:
      NABTO_LOG_FATAL(("Invalid query was queued!"));
      return;
    }
}

// find the query containing the specified request
static modbus_query* find_query(application_request* request)
{
    list_element* e;

    list_foreach(&modbusQueries, e)
    {
        modbus_query* query = (modbus_query*)e->data;
        if(query->request == request)
        {
            return query;
        }
    }

    return NULL;
}


static void release_messages(list* messages)
{
    modbus_message* message;

    while(list_remove_first(messages, (void**)&message))
    {
        modbus_rtu_master_release_message(message);
    }
}

static void release_query(modbus_query* query)
{
    if(query != NULL)
    {
        release_messages(&query->messages); // release all messages
        checked_free(query); // release the query it self
    }
}

static application_event_result abort_modbus_function(modbus_query* query, buffer_write_t* writeBuffer, uint8_t bus, uint8_t address, uint8_t result)
{
    release_query(query);

    // send 'result' with zero-length 'data'
    if(!buffer_write_uint8(writeBuffer, bus) || !buffer_write_uint8(writeBuffer, address) || !buffer_write_uint8(writeBuffer, result) || !buffer_write_raw_from_array(writeBuffer, NULL, 0) || !buffer_write_uint8(writeBuffer, QUERY_STATUS_OK))
    {
        return AER_REQ_RSP_TOO_LARGE;
    }

    return AER_REQ_RESPONSE_READY;
}







// copy a string to out ensure it's null terminated.
static bool read_string_null_terminated_modbus(unabto_query_request* read_buffer, char* out, size_t outlen)
{
  uint8_t* list;
  uint16_t length;
  if (!unabto_query_read_uint8_list(read_buffer, &list, &length)) {
    return false;
  }
  
  memset(out, 0, outlen);

  memcpy(out, list, MIN(length, outlen-1));
  return true;
}



int copy_buffer(unabto_query_request* read_buffer, uint8_t* dest, uint16_t bufSize, uint16_t* len) {
    uint8_t* buffer;
    if (!(unabto_query_read_uint8_list(read_buffer, &buffer, len))) {
        return AER_REQ_TOO_SMALL;
    }
    if (*len > bufSize) {
        return AER_REQ_TOO_LARGE;
    }
    memcpy(dest, buffer, *len);
    return AER_REQ_RESPONSE_READY;
}

int copy_string(unabto_query_request* read_buffer, char* dest, uint16_t destSize) {
    uint16_t len;
    int res = copy_buffer(read_buffer, (uint8_t*)dest, destSize-1, &len);
    if (res != AER_REQ_RESPONSE_READY) {
        return res;
    }
    dest[len] = 0;
    return AER_REQ_RESPONSE_READY;
}

int write_string(unabto_query_response* write_buffer, const char* string) {
    return unabto_query_write_uint8_list(write_buffer, (uint8_t *)string, strlen(string));
}

void debug_dump_acl() {
    void* it = db_.first();
    if (!it) {
        NABTO_LOG_INFO(("ACL is empty (no paired users)"));
    } else {
        NABTO_LOG_INFO(("ACL entries:"));
        while (it != NULL) {
            struct fp_acl_user user;
            fp_acl_db_status res = db_.load(it, &user);
            if (res != FP_ACL_DB_OK) {
                NABTO_LOG_WARN(("ACL error %d\n", res));
                return;
            }
            NABTO_LOG_INFO((" - %s [%02x:%02x:%02x:%02x:...]: %04x",
                            user.name,
                            user.fp[0], user.fp[1], user.fp[2], user.fp[3],
                            user.permissions));
            it = db_.next(it);
        }
    }
}




bool allow_client_access(nabto_connect* connection) {
    bool local = connection->isLocal;
    bool allow = fp_acl_is_connection_allowed(connection) || local;
    NABTO_LOG_INFO(("Allowing %s connect request: %s", (local ? "local" : "remote"), (allow ? "yes" : "no")));
    debug_dump_acl();
    return allow;
}

application_event_result application_event(application_request* request,
                                           unabto_query_request* query_request,
                                           unabto_query_response* query_response) {

    NABTO_LOG_INFO(("Nabto application_event: %u", request->queryId));
    debug_dump_acl();

    // handle requests as defined in interface definition shared with
    // client - for the default demo, see
    // https://github.com/nabto/ionic-starter-nabto/blob/master/www/nabto/unabto_queries.xml

    application_event_result res;

    if (request->queryId >= 11000 && request->queryId < 12000) {
        // default PPKA access control (see unabto/src/modules/fingerprint_acl/fp_acl_ae.c)
        application_event_result res = fp_acl_ae_dispatch(11000, request, query_request, query_response);
        NABTO_LOG_INFO(("ACL request [%d] handled with status %d", request->queryId, res));
        debug_dump_acl();
        return res;
    }
    
    switch (request->queryId) {
    case 0:
        // get_interface_info.json
        if (!write_string(query_response, device_interface_id_)) return AER_REQ_RSP_TOO_LARGE;
        if (!unabto_query_write_uint16(query_response, device_interface_version_major_)) return AER_REQ_RSP_TOO_LARGE;
        if (!unabto_query_write_uint16(query_response, device_interface_version_minor_)) return AER_REQ_RSP_TOO_LARGE;
        return AER_REQ_RESPONSE_READY;

    case 10000:
        // get_public_device_info.json
        if (!write_string(query_response, device_name_)) return AER_REQ_RSP_TOO_LARGE;
        if (!write_string(query_response, device_product_)) return AER_REQ_RSP_TOO_LARGE;
        if (!write_string(query_response, device_icon_)) return AER_REQ_RSP_TOO_LARGE;
        if (!unabto_query_write_uint8(query_response, fp_acl_is_pair_allowed(request))) return AER_REQ_RSP_TOO_LARGE;
        if (!unabto_query_write_uint8(query_response, fp_acl_is_user_paired(request))) return AER_REQ_RSP_TOO_LARGE; 
        if (!unabto_query_write_uint8(query_response, fp_acl_is_user_owner(request))) return AER_REQ_RSP_TOO_LARGE;
        return AER_REQ_RESPONSE_READY;

    case 10010:
        // set_device_info.json
        if (!fp_acl_is_request_allowed(request, REQUIRES_OWNER)) return AER_REQ_NO_ACCESS;
        int res = copy_string(query_request, device_name_, sizeof(device_name_));
        if (res != AER_REQ_RESPONSE_READY) return res;
        if (!write_string(query_response, device_name_)) return AER_REQ_RSP_TOO_LARGE;
        return AER_REQ_RESPONSE_READY;


        


    case QUERY_MODBUS_FUNCTION:

        //if (!fp_acl_is_request_allowed(request, REQUIRES_GUEST)) return AER_REQ_NO_ACCESS;
        return begin_modbus_function_query(request, query_request, query_response);

    case QUERY_MODBUS_CONFIGURATION:
        if (!write_string(query_response, modbus_configuration)) return AER_REQ_RSP_TOO_LARGE;
        //if (!write_string(query_response, "HEJHEJHEJ")) return AER_REQ_RSP_TOO_LARGE;
        return AER_REQ_RESPONSE_READY;

    default:
        NABTO_LOG_WARN(("Unhandled query id: %u", request->queryId));
        return AER_REQ_INV_QUERY_ID;
    }
}
