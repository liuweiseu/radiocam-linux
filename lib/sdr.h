#ifndef SDR_H
#define SDR_H

#include <stdint.h>

int data_transfer_init();
int data_transfer_close();

int read_data(const char *filename);

int sdr_set_control(uint32_t id, int value);

int sdr_set_adc_config(int value);
int sdr_set_mipi_config(int value);
int sdr_set_synthesizer_config(int value);
int sdr_set_test_pattern(int mode);

#endif
