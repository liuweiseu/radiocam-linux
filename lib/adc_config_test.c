#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>      // Required for getopt
#include <time.h>        // Required for clock_gettime
#include "sdr_config.h"

int main(int argc, char **argv) {
    // Default values
    char mode[10] = "dual";
    int fs = 125;
    char test_mode[10] = "normal";
    int num_frames = 5; // Default frame count
    int opt;

    // Parse command line arguments: added 'n:' for number of frames
    while ((opt = getopt(argc, argv, "m:f:t:n:h")) != -1) {
        switch (opt) {
            case 'm':
                strncpy(mode, optarg, 9);
                mode[9] = '\0';
                break;
            case 'f':
                fs = atoi(optarg);
                break;
            case 't':
                strncpy(test_mode, optarg, 9);
                test_mode[9] = '\0';
                break;
            case 'n':
                num_frames = atoi(optarg);
                if (num_frames <= 0) {
                    fprintf(stderr, "Error: Frames must be > 0.\n");
                    return EXIT_FAILURE;
                }
                break;
            case 'h':
                printf("Usage: %s [-m mode] [-f fs] [-t test_mode] [-n frames]\n", argv[0]);
                printf("  -m : Set ADC channel mode (quad|dual). Default: dual\n");
                printf("  -f : Set sampling frequency in MHz (125|250). Default: 125\n");
                printf("  -t : Set test pattern (normal|ramp|sync|custom). Default: normal\n");
                printf("  -n : Number of frames to capture. Default: 5\n");
                return EXIT_SUCCESS;
            default:
                fprintf(stderr, "Usage: %s [-m mode] [-f fs] [-t test_mode] [-n frames]\n", argv[0]);
                return EXIT_FAILURE;
        }
    }

    printf("--- Starting SDR Capture & Timing Test ---\n");
    printf("Config -> Mode: %s, FS: %d MHz, Pattern: %s, Frames: %d\n", mode, fs, test_mode, num_frames);

    // 1. Initialize Configuration
    if (sdr_config_init() < 0) {
        fprintf(stderr, "Warning: Failed to init I2C config.\n");
    }

    // 2. Hardware Bootstrap
    printf("1. Bootstrapping ADC Hardware...\n");
    if (sdr_adc_init_sequence() < 0) {
        fprintf(stderr, "Error: ADC Init Sequence failed.\n");
        sdr_config_close();
        return EXIT_FAILURE;
    }

    // 3. Configure ADC and Test Mode
    if (sdr_configure_adc_mode(mode, fs) < 0) {
        fprintf(stderr, "Error: Failed to set ADC mode.\n");
    }
    if (sdr_configure_test_mode(test_mode, 0xAA) < 0) { 
        fprintf(stderr, "Error: Failed to set test mode.\n");
    }

    // 4. Initialize MIPI D-PHY
    printf("2. Waking up MIPI D-PHY...\n");
    if (sdr_mipi_initialize(5.0) != 0) {
        fprintf(stderr, "MIPI Initialization failed.\n");
        sdr_config_close();
        return EXIT_FAILURE;
    }
    usleep(100000); // Let hardware stabilize

    // --- TIMING SETUP ---
    struct timespec start, end;
    double capture_time, write_time;
    
    // We capture to RAM (/dev/shm) to isolate hardware speed from disk IO
    char ram_file[256] = "/dev/shm/data_test.dat";
    char disk_file[256] = "data_test.dat";
    char v4l2_cmd[512];
    char cp_cmd[512];

    snprintf(v4l2_cmd, sizeof(v4l2_cmd), 
             "v4l2-ctl -d /dev/video0 --stream-mmap --stream-count=%d --stream-to=%s", 
             num_frames, ram_file);

    snprintf(cp_cmd, sizeof(cp_cmd), "cp %s %s", ram_file, disk_file);

    printf("3. Capturing to RAM (%s) to measure pure hardware speed...\n", ram_file);

    // --- MEASURE CAPTURE TIME ---
    clock_gettime(CLOCK_MONOTONIC, &start);
    int ret = system(v4l2_cmd);
    clock_gettime(CLOCK_MONOTONIC, &end);
    
    capture_time = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;

    if (ret != 0) {
        fprintf(stderr, "V4L2 Capture failed!\n");
        sdr_config_close();
        return EXIT_FAILURE;
    }

    // --- MEASURE DISK WRITE TIME ---
    printf("4. Copying from RAM to Disk (%s) to measure storage speed...\n", disk_file);
    
    clock_gettime(CLOCK_MONOTONIC, &start);
    system(cp_cmd);
    clock_gettime(CLOCK_MONOTONIC, &end);
    
    write_time = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;

    // Clean up the RAM file so we don't eat up system memory
    remove(ram_file);
    sdr_config_close();

    // --- RESULTS ---
    double total_mb = (2048.0 * 2556.0 * num_frames) / (1024.0 * 1024.0);
    
    printf("\n=== Performance Results ===\n");
    printf("Total Data:      %.2f MB\n", total_mb);
    printf("---------------------------\n");
    printf("Capture Time:    %.4f seconds (%.2f MB/s)\n", capture_time, total_mb / capture_time);
    printf("Disk Write Time: %.4f seconds (%.2f MB/s)\n", write_time, total_mb / write_time);
    printf("---------------------------\n");
    printf("Total Time:      %.4f seconds\n", capture_time + write_time);

    return EXIT_SUCCESS;
}