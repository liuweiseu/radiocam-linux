#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h> // Required for getopt

#include "sdr_config.h"

int main(int argc, char **argv) {
    // Default values if the user doesn't provide any arguments
    char mode[10] = "dual";
    int fs = 125;
    char test_mode[10] = "normal"; // New default test mode
    int opt;

    // Parse command line arguments using getopt
    // "m:f:t:h" means it expects -m, -f, -t (all with values), and an optional -h
    while ((opt = getopt(argc, argv, "m:f:t:h")) != -1) {
        switch (opt) {
            case 'm':
                strncpy(mode, optarg, 9);
                mode[9] = '\0'; // Ensure null-termination
                break;
            case 'f':
                fs = atoi(optarg);
                break;
            case 't':
                strncpy(test_mode, optarg, 9);
                test_mode[9] = '\0'; // Ensure null-termination
                break;
            case 'h':
                printf("Usage: %s [-m mode] [-f fs] [-t test_mode]\n", argv[0]);
                printf("  -m : Set ADC channel mode (quad|dual). Default: dual\n");
                printf("  -f : Set sampling frequency in MHz (125|250). Default: 125\n");
                printf("  -t : Set test pattern mode (normal|ramp|sync|custom). Default: normal\n");
                return EXIT_SUCCESS;
            default:
                fprintf(stderr, "Usage: %s [-m mode] [-f fs] [-t test_mode]\n", argv[0]);
                return EXIT_FAILURE;
        }
    }

    printf("--- Starting SDR ADC Configuration Test ---\n");
    printf("Target Configuration -> Mode: %s, FS: %d MHz, Test Mode: %s\n", mode, fs, test_mode);

    // 1. Initialize the configuration interfaces
    if (sdr_config_init() < 0) {
        fprintf(stderr, "Warning: Failed to initialize V4L2 subdevice. I2C may still work.\n");
    }

    // 2. Run the hardware bootstrap
    printf("1. Bootstrapping ADC Hardware (Power Cycle & Defaults)...\n");
    if (sdr_adc_init_sequence() < 0) {
        fprintf(stderr, "Error: ADC Init Sequence failed. Check I2C connections.\n");
        sdr_config_close();
        return EXIT_FAILURE;
    }

    // 3. Configure ADC Mode using the command line variables!
    printf("2. Configuring ADC Mode...\n");
    if (sdr_configure_adc_mode(mode, fs) < 0) {
        fprintf(stderr, "Error: Failed to set ADC mode.\n");
    }

    // 4. Configure Test Pattern using the command line variable!
    printf("3. Setting ADC Test Mode to '%s'...\n", test_mode);
    // Note: If test_mode is "custom", it will use 0xAA as the pattern.
    // The second argument is ignored for normal, ramp, and sync.
    if (sdr_configure_test_mode(test_mode, 0xAA) < 0) { 
        fprintf(stderr, "Error: Failed to set test mode.\n");
    }

    // 5. Clean up 
    printf("4. Cleaning up and closing interface...\n");
    sdr_config_close();

    printf("--- ADC Configuration Test Completed successfully ---\n");
    return EXIT_SUCCESS;
}
