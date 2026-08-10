#include "sdr_transfer.h"
#include <stdio.h>
#include <stdlib.h>

int sdr_transfer_capture_ram(int num_frames, const char *ram_filename, int debug) {
    char v4l2_cmd[512];
    
    if (debug) {
        // Normal command (prints <<<< fps to terminal)
        snprintf(v4l2_cmd, sizeof(v4l2_cmd), 
                 "v4l2-ctl -d /dev/video0 --stream-mmap --stream-count=%d --stream-to=%s", 
                 num_frames, ram_filename);
    } else {
        // Silenced command (redirects output to nowhere)
        snprintf(v4l2_cmd, sizeof(v4l2_cmd), 
                 "v4l2-ctl -d /dev/video0 --stream-mmap --stream-count=%d --stream-to=%s > /dev/null 2>&1", 
                 num_frames, ram_filename);
    }
             
    int ret = system(v4l2_cmd);
    if (ret != 0) {
        fprintf(stderr, "Error: V4L2 Capture failed.\n");
        return -1;
    }
    
    return 0;
}

int sdr_transfer_save_disk(const char *ram_filename, const char *disk_filename) {
    char cp_cmd[512];
    snprintf(cp_cmd, sizeof(cp_cmd), "cp %s %s", ram_filename, disk_filename);
    
    int ret = system(cp_cmd);
    if (ret != 0) {
        fprintf(stderr, "Error: Failed to copy data to disk.\n");
        return -1;
    }
    
    remove(ram_filename); // Clean up RAM
    return 0;
}