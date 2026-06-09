#include <stdio.h>
#include <stdlib.h>
#include <unistd.h> // Required for getopt
#include "sdr_transfer.h"

int main(int argc, char **argv) {
    int error_code;
    
    // Default values
    int num_frames = 5;
    char filename[256] = "data_test.dat";
    int opt;

    // Parse command line arguments
    // "n:o:h" means expect -n <value>, -o <value>, and optional -h
    while ((opt = getopt(argc, argv, "n:o:h")) != -1) {
        switch (opt) {
            case 'n':
                num_frames = atoi(optarg);
                if (num_frames <= 0) {
                    fprintf(stderr, "Error: Number of frames must be greater than 0.\n");
                    return -1;
                }
                break;
            case 'o':
                snprintf(filename, sizeof(filename), "%s", optarg);
                break;
            case 'h':
                printf("Usage: %s [-n num_frames] [-o output_filename]\n", argv[0]);
                printf("  -n : Number of frames to capture. Default: 5\n");
                printf("  -o : Output filename. Default: data_test.dat\n");
                return 0;
            default:
                fprintf(stderr, "Usage: %s [-n num_frames] [-o output_filename]\n", argv[0]);
                return -1;
        }
    }

    printf("--- Starting SDR Data Capture ---\n");
    printf("Capturing %d frames to '%s'\n", num_frames, filename);

    error_code = data_transfer_init();
    if (error_code != 0) {
        printf("Error with initialization.\n");
        return -1;
    }

    // Now using the variable from the command line!
    error_code = read_data(filename, num_frames);
    if (error_code != 0) {
        printf("Error with data read.\n");
        data_transfer_close(); // Make sure to close if we fail midway
        return -1;
    }

    error_code = data_transfer_close();
    if (error_code != 0) {
        printf("Error with closing data bus.\n");
        return -1;
    }

    printf("Data capture complete!\n");
    return 0;
}
