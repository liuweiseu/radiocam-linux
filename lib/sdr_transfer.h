#ifndef SDR_TRANSFER_H
#define SDR_TRANSFER_H

/**
 * Initializes the V4L2 video device.
 * Maps memory buffers and starts the DMA stream.
 * @return 0 on success, -1 on failure.
 */
int data_transfer_init(void);

/**
 * Stops the stream, unmaps buffers, and closes the video device.
 * @return 0 on success.
 */
int data_transfer_close(void);

/**
 * De-queues a buffer, writes its contents to a file, and re-queues.
 * @param filename Path to save the raw data.
 * @return 0 on success, -1 on failure.
 */
int read_data(const char *filename, const int num_frames);

#endif // SDR_TRANSFER_H