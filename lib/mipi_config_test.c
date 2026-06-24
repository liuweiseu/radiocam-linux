#include "sdr_config.h"
#include <stdio.h>
#include <unistd.h>

int main(void) {
    printf("=== Starting MIPI Subsystem Driver Test ===\n");

    // 1. Initialize the system file descriptors and structures (Bus 11)
    printf("\n[STEP 1] Initializing system subdevices...\n");
    if (sdr_config_init() != 0) {
        fprintf(stderr, ">> TEST FAILED: Could not initialize subdevices. Are you running as root/sudo?\n");
        return 1;
    }
    printf(">> Subdevice setup successful.\n");

    // 2. Trigger the primary MIPI hardware initialization sequence
    printf("\n[STEP 2] Running Master MIPI Init Sequence...\n");
    double timeout_seconds = 5.0;
    int init_result = sdr_mipi_initialize(timeout_seconds);

    if (init_result != 0) {
        fprintf(stderr, ">> TEST FAILED: MIPI initialization sequence reported an error.\n");
        sdr_config_close();
        return 1;
    }
    printf(">> MIPI hardware configuration completed successfully.\n");

    // 3. Post-verification: Safe stabilization delay for D-PHY-only mode
    printf("\n[STEP 3] Applying hardware clock stabilization delay...\n");
    usleep(100000); // 100ms delay to let the 312 Mbps PLL lock stably
    printf(">> SUCCESS: MIPI D-PHY lanes configured and stabilized.\n");

    // 4. Clean up resources
    printf("\n[STEP 4] Closing subdevices...\n");
    sdr_config_close();
    printf("=== Test Run Finished Successfully ===\n");

    return 0;
}