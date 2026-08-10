#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

#include "sdr_config.h"
#include "sdr_transfer.h"

int main(int argc, char **argv) {
    char mode[10] = "dual";
    int fs = 125;
    char test_mode[10] = "normal";
    int num_frames = 5;
    int debug = 0; // Default to quiet mode
    int opt;

    // Added 'd' to the getopt string
    while ((opt = getopt(argc, argv, "m:f:t:n:dh")) != -1) {
        switch (opt) {
            case 'm': strncpy(mode, optarg, 9); mode[9] = '\0'; break;
            case 'f': fs = atoi(optarg); break;
            case 't': strncpy(test_mode, optarg, 9); test_mode[9] = '\0'; break;
            case 'n': num_frames = atoi(optarg); break;
            case 'd': debug = 1; break; // Enable verbose output
            case 'h':
                printf("Usage: %s [-m mode] [-f fs] [-t test_mode] [-n frames] [-d]\n", argv[0]);
                printf("  -m : ADC channel mode (quad|dual). Default: dual\n");
                printf("  -f : Sampling frequency in MHz (125|250). Default: 125\n");
                printf("  -t : Test pattern (normal|ramp|sync|custom). Default: normal\n");
                printf("  -n : Number of frames to capture. Default: 5\n");
                printf("  -d : Enable debug/verbose output\n");
                return EXIT_SUCCESS;
            default:
                return EXIT_FAILURE;
        }
    }

    if (num_frames <= 0) {
        fprintf(stderr, "Error: Frames must be > 0.\n");
        return EXIT_FAILURE;
    }

    if (debug) {
        printf("--- Starting SDR Capture & Timing Test ---\n");
        printf("Config -> Mode: %s, FS: %d MHz, Pattern: %s, Frames: %d\n", mode, fs, test_mode, num_frames);
        printf("1. Hardware Initialization...\n");
    }

    // 1. Hardware Initialization
    sdr_config_init();
    sdr_adc_init_sequence();
    sdr_configure_adc_mode(mode, fs);
    sdr_configure_test_mode(test_mode, 0xAA);
    
    if (sdr_mipi_initialize(5.0) != 0) {
        fprintf(stderr, "MIPI Initialization failed.\n");
        sdr_config_close();
        return EXIT_FAILURE;
    }
    usleep(100000); // Stabilize hardware

    struct timespec start, end;
    double capture_time, write_time;
    
    const char *ram_file = "/dev/shm/data_test.dat";
    const char *disk_file = "data_test.dat";

    // 2. Measure Pure Hardware Capture Time
    if (debug) printf("2. Capturing %d frames to RAM...\n", num_frames);
    clock_gettime(CLOCK_MONOTONIC, &start);
    
    // Pass the debug flag into our library
    if (sdr_transfer_capture_ram(num_frames, ram_file, debug) != 0) {
        sdr_config_close();
        return EXIT_FAILURE;
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    capture_time = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;

    // 3. Measure Disk Write Time
    if (debug) printf("3. Saving frames to physical disk...\n");
    clock_gettime(CLOCK_MONOTONIC, &start);
    
    if (sdr_transfer_save_disk(ram_file, disk_file) != 0) {
        sdr_config_close();
        return EXIT_FAILURE;
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    write_time = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;

    // 4. Cleanup Hardware
    if (debug) printf("4. Cleaning up hardware...\n");
    sdr_config_close();

    // --- PRINT RESULTS ---
    double total_mb = (2048.0 * 2556.0 * num_frames) / (1024.0 * 1024.0);
    
    // If debug is on, print the big formatted block. If not, print a clean 1-liner.
    if (debug) {
        printf("\n=== Performance Results ===\n");
        printf("Total Data:      %.2f MB\n", total_mb);
        printf("---------------------------\n");
        printf("Capture Time:    %.4f seconds (%.2f MB/s)\n", capture_time, total_mb / capture_time);
        printf("Disk Write Time: %.4f seconds (%.2f MB/s)\n", write_time, total_mb / write_time);
        printf("---------------------------\n");
        printf("Total Time:      %.4f seconds\n", capture_time + write_time);
    } else {
        printf("Capture: %.2f MB/s | Write: %.2f MB/s | Total Time: %.4fs\n", 
               total_mb / capture_time, total_mb / write_time, capture_time + write_time);
    }

    return EXIT_SUCCESS;
}