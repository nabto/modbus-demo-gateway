/**
 *  Implementation of main for uNabto SDK
 */
#if defined(WIN32) || defined(WINCE)
#include <windows.h>
//#include <winsock2.h>
#endif

#if !defined(WIN32) && !defined(__MACH__)
#include <sched.h>
#endif

#include <sys/types.h>
#include <modules/cli/gopt/gopt.h> // http://www.purposeful.co.uk/software/gopt/
#include <modules/util/read_hex.h>
#include <modules/log/dynamic/unabto_dynamic_log.h>
#include "unabto/unabto_env_base.h"
#include "unabto/unabto_common_main.h"
#include "unabto/unabto_logging.h"
#include "modbus_application.h"
#include "modbus_rtu_master.h"

#if defined(WIN32)
#define strdup(s) _strdup(s)
#endif

struct configuration {
    const char *device_id;
    const char *pre_shared_key;
    const char *local_port_str;
    const char *device_name;
    const char *product_name;
    const char *icon_url;
    const char *log;
};
    
void nabto_yield(int msec);
static void help(const char* errmsg, const char *progname);
bool parse_argv(int argc, char* argv[], struct configuration* config);

static void help(const char* errmsg, const char *progname)
{
    if (errmsg) {
        printf("ERROR: %s\n", errmsg);
    }
    printf("\nAppMyProduct demo stub application to try the platform and example apps.\n");
    printf("Obtain a device id and crypto key from www.appmyproduct.com\n\n");

    printf("Usage: %s -d <device id> -k <crypto key> \n\t"
           "[-N <demo display name>] \n\t"
           "[-P <product name>] \n\t"
           "[-I <icon url>] \n\t"
           "[-p <local/discovery port>] \n\t"
           "[-l <log pattern (e.g. *.trace)>]\n\n", progname);
}


int main(int argc, char* argv[])
{
    struct configuration config;
    memset(&config, 0, sizeof(struct configuration));

    if (!parse_argv(argc, argv, &config)) {
        help(0, argv[0]);
        return 1;
    }

    nabto_main_setup* nms = unabto_init_context();
    nms->id = strdup(config.device_id);

    nms->secureAttach = true;
    nms->secureData = true;
    nms->cryptoSuite = CRYPT_W_AES_CBC_HMAC_SHA256;

    if (!unabto_read_psk_from_hex(config.pre_shared_key, nms->presharedKey, 16)) {
        help("Invalid cryptographic key specified", argv[0]);
        return false;
    }
  
    if (config.local_port_str) {
        nms->localPort = atoi(config.local_port_str);
    }

    if (config.device_name) {
        modbus_application_set_device_name(config.device_name);
    }

    if (config.product_name) {
        modbus_application_set_device_product(config.product_name);
    }

    if (config.icon_url) {
        modbus_application_set_device_icon_(config.icon_url);
    }

    if (config.log) {
        if (!unabto_log_system_enable_stdout_pattern(config.log)) {            
            NABTO_LOG_FATAL(("Logstring %s is not a valid logsetting", config.log));
        }
    } else {
        unabto_log_system_enable_stdout_pattern("*.info");
    }

    if (!unabto_init()) {
        NABTO_LOG_FATAL(("Failed at nabto_main_init"));
    }
    modbus_application_initialize_with_uart();

    NABTO_LOG_INFO(("Modbus demo stub [%s] running!", nms->id));

    while (true) {
        unabto_tick();
        nabto_yield(10);
	modbus_rtu_master_tick();

    }

    unabto_close();
    return 0;
}

bool parse_argv(int argc, char* argv[], struct configuration* config) {
    const char x0s[] = "h?";     const char* x0l[] = { "help", 0 };
    const char x1s[] = "d";      const char* x1l[] = { "deviceid", 0 };
    const char x2s[] = "k";      const char* x2l[] = { "presharedkey", 0 };
    const char x3s[] = "p";      const char* x3l[] = { "localport", 0 };
    const char x4s[] = "N";      const char* x4l[] = { "devicename", 0 };
    const char x5s[] = "P";      const char* x5l[] = { "productname", 0 };
    const char x6s[] = "I";      const char* x6l[] = { "iconurl", 0 };
    const char x7s[] = "l";      const char* x7l[] = { "log", 0 };

    const struct { int k; int f; const char *s; const char*const* l; } opts[] = {
        { 'h', 0,           x0s, x0l },
        { 'd', GOPT_ARG,    x1s, x1l },
        { 'k', GOPT_ARG,    x2s, x2l },
        { 'p', GOPT_ARG,    x3s, x3l },
        { 'N', GOPT_ARG,    x4s, x4l },
        { 'P', GOPT_ARG,    x5s, x5l },
        { 'I', GOPT_ARG,    x6s, x6l },
        { 'l', GOPT_ARG,    x7s, x7l },
        { 0, 0, 0, 0 }
    };
    void *options = gopt_sort( & argc, (const char**)argv, opts);

    if( gopt( options, 'h')) {
        help(0, argv[0]);
        exit(0);
    }

    if (!gopt_arg(options, 'd', &config->device_id)) {
        return false;
    }

    if (!gopt_arg(options, 'k', &config->pre_shared_key)) {
        return false;
    }

    gopt_arg(options, 'p', &config->local_port_str);
    gopt_arg(options, 'N', &config->device_name);
    gopt_arg(options, 'P', &config->product_name);
    gopt_arg(options, 'I', &config->icon_url);
    gopt_arg(options, 'l', &config->log);

    return true;
}

void nabto_yield(int msec)
{
#ifdef WIN32
    Sleep(msec);
#elif defined(__MACH__)
    if (msec) usleep(1000*msec);
#else
    if (msec) usleep(1000*msec); else sched_yield();
#endif
}

#ifdef WIN32
void setTimeFromGSP(uint32_t stamp){
}
#endif
    
