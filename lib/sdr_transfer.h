#ifndef SDR_TRANSFER_H
#define SDR_TRANSFER_H

// Added debug parameter
int sdr_transfer_capture_ram(int num_frames, const char *ram_filename, int debug);

int sdr_transfer_save_disk(const char *ram_filename, const char *disk_filename);

#endif // SDR_TRANSFER_H