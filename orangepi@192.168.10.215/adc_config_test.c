#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

// Include your newly created configuration header
#include "sdr_config.h"

int main(int argc, char **argv) {
    printf("Starting SDR ADC Configuration Test...\n");

    // 1. Initialize the configuration interfaces (opens /dev/v4l-subdev2)
    if (sdr_config_init() < 0) {
        fprintf(stderr, "Warning: Failed to initialize subdevice. V4L2 controls may fail, but I2C might still work.\n");
    } else {
        printf("Configuration interface initialized successfully.\n");
    }

    // 2. Run the ADC hardware bootstrap sequence 
    // (This does the GPIO RST toggling and writes the default 0x57 registers)
    printf("Running ADC Init Sequence (Reset & Power Cycle)...\n");
    if (sdr_adc_init_sequence() < 0) {
        fprintf(stderr, "Error: ADC Init Sequence failed. Check I2C connections and permissions.\n");
        sdr_config_close();
        return EXIT_FAILURE;
    }
    printf("ADC Init Sequence completed successfully.\n");

    // 3. Example of a user writing a custom configuration to the ADC
    // Let's say a user wants to override register 0x31 (Modes of Operation)
    int reg_addr = 0x31;
    int reg_val = 0x0004; // Quad channel mode, clk div 1
    
    printf("Writing 0x%04X to ADC Register 0x%02X...\n", reg_val, reg_addr);
    if (sdr_set_adc_config(reg_addr, reg_val) < 0) {
        fprintf(stderr, "Error: Failed to write to ADC register.\n");
    } else {
        printf("Successfully wrote custom configuration to ADC.\n");
    }

    // 4. Example of setting a test pattern using the V4L2 subdevice
    int test_pattern_mode = 1; // Change this to whatever mode you want to test
    printf("Setting Test Pattern to mode %d...\n", test_pattern_mode);
    if (sdr_set_test_pattern(test_pattern_mode) < 0) {
        fprintf(stderr, "Warning: Failed to set test pattern.\n");
    } else {
        printf("Successfully set test pattern.\n");
    }

    // 5. Clean up and close file descriptors
    printf("Cleaning up and closing configuration interface...\n");
    sdr_config_close();

    printf("ADC Configuration Test Completed successfully.\n");
    return EXIT_SUCCESS;
}