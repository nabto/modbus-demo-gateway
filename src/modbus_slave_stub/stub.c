/*
 * slave.c - a MODBUS slave
 */

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

#include <modbus.h>

#define MODBUS_REGS 10

int main(int argc, char* argv[])
{
    int i;

    modbus_t *ctx;
    modbus_mapping_t *mb_mapping;

    if(argc!=2) {
        fprintf(stderr, "No serial device specifed\n");
        fprintf(stderr, "Usage: %s [serial device filename]\n", argv[0]);
        return -1;
    }
    
    ctx = modbus_new_rtu(argv[1], 115200, 'N', 8, 1);
    if (ctx == NULL) {
        fprintf(stderr, "Unable to create the libmodbus context\n");
        return -1;
    }

    modbus_set_debug(ctx, TRUE); 

    // Adress at 1 
    if ( modbus_set_slave ( ctx, 1 ) == -1 ) {
        fprintf(stderr, "Failed to set as slave: %s\n", modbus_strerror(errno));
        return -1;
    }

    if (modbus_connect(ctx) == -1) {
        fprintf(stderr, "Connection failed: %s\n", modbus_strerror(errno));
        fprintf(stderr, "Does the device file exists?\n");
        
        modbus_free(ctx);
        return -1;
    }
    
            
    mb_mapping = modbus_mapping_new(0, 0, MODBUS_REGS, MODBUS_REGS);
    if (mb_mapping == NULL) {
        fprintf(stderr, "Failed to allocate the mapping: %s\n",
                modbus_strerror(errno));
        modbus_free(ctx);
        return -1;

    }

    // Put something into the modbus register tables
    mb_mapping->tab_registers[0] = 0x9999;
    mb_mapping->tab_registers[1] = 0x0001;
    mb_mapping->tab_registers[2] = 0x0002;
    mb_mapping->tab_registers[3] = 0x4321;
    mb_mapping->tab_registers[4] = 0x4444;
    mb_mapping->tab_registers[5] = 0x5678;
    mb_mapping->tab_registers[6] = 0xFFFF;
    mb_mapping->tab_registers[7] = 0xABCD;
    mb_mapping->tab_registers[8] = 0xA0B0;
    mb_mapping->tab_registers[9] = 0xFEDC;
    
    mb_mapping->tab_input_registers[0] = 0x1876;
    mb_mapping->tab_input_registers[1] = 0x1001;
    mb_mapping->tab_input_registers[2] = 0x1002;
    mb_mapping->tab_input_registers[3] = 0x1321;
    mb_mapping->tab_input_registers[4] = 0x1444;
    mb_mapping->tab_input_registers[5] = 0x1678;
    mb_mapping->tab_input_registers[6] = 0x1FFF;
    mb_mapping->tab_input_registers[7] = 0x1BCD;
    mb_mapping->tab_input_registers[8] = 0x10B0;
    mb_mapping->tab_input_registers[9] = 0x1EDC;
    
    for (;;) {
        uint8_t query[MODBUS_TCP_MAX_ADU_LENGTH];
        int rc;
        
        rc = modbus_receive(ctx, query);

        printf("SLAVE before : input_regs[] =\t");
        for(i = 0; i != MODBUS_REGS; i++) { // looks like 1..n index
            printf("%04x ", mb_mapping->tab_input_registers[i]);
        }
        printf("\n");
        printf("SLAVE before : holding_regs[] =\t");
        for(i = 0; i != MODBUS_REGS; i++) { // looks like 1..n index
            printf("%04x ", mb_mapping->tab_registers[i]);
        }
        printf("\n");
        
        if (rc > 0) {
            /* rc is the query size */
            modbus_reply(ctx, query, rc, mb_mapping);
            printf("SLAVE after : input_regs[] =\t");
            for(i = 0; i != MODBUS_REGS; i++) { // looks like 1..n index
                printf("%04x ", mb_mapping->tab_input_registers[i]);
            }
            printf("\n");
            printf("SLAVE after : holding_regs[] =\t");
            for(i = 0; i != MODBUS_REGS; i++) { // looks like 1..n index
                printf("%04x ", mb_mapping->tab_registers[i]);
            }
            printf("\n");
        } else if (rc == -1) {
            /* Connection closed by the client or error */
            printf("Connection closed\n");
            break;

        }
    }
    
    printf("Quit the loop: %s\n", modbus_strerror(errno));
    

    modbus_mapping_free(mb_mapping);
    modbus_close(ctx);
    modbus_free(ctx);
    
    return 0;
}
