#ifndef SDR_CONFIG_H
#define SDR_CONFIG_H

#include <stdint.h>

/**
 * Initializes the configuration interfaces (V4L2 subdev).
 * Call this before writing any configs.
 */
int sdr_config_init(void);
void sdr_config_close(void);

/**
 * Core Hardware Configuration via I2C.
 * Use these to set ADC registers and toggle GPIO pins on the board.
 */
int sdr_gpio_write_reg(int reg_addr, int reg_val);
int sdr_set_adc_config(int reg_addr, int reg_val);

/**
 * Bootstraps the ADC hardware by performing the soft reset 
 * and power cycling routines.
 */
int sdr_adc_init_sequence(void);

/**
 * FPGA/Camera Subdevice controls via V4L2 IOCTLs.
 */
int sdr_set_control(uint32_t id, int value);
int sdr_set_test_pattern(int mode);

/**
 * Placeholders for future implementations
 */
int sdr_set_mipi_config(int value);
int sdr_set_synthesizer_config(int value);

#endif // SDR_CONFIG_H