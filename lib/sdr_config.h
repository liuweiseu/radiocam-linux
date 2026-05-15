#ifndef SDR_CONFIG_H
#define SDR_CONFIG_H

#include <stdint.h>

// --- ADC Bitfield Definitions (RegAddr, BitLoc, BitWidth) ---
#define ADC_FLD_RST            0x00, 0, 1
#define ADC_FLD_PD             0x0f, 9, 1
#define ADC_FLD_CLK_DIVIDE     0x31, 8, 2
#define ADC_FLD_LVDS_DELAY     0x53, 4, 2
#define ADC_FLD_PHASE_DDR      0x42, 5, 2
#define ADC_FLD_CHANNEL_NUM    0x31, 0, 3
#define ADC_FLD_STARTUP_CTRL   0x56, 0, 3
#define ADC_FLD_PAT_SYNC       0x45, 0, 2
#define ADC_FLD_EN_RAMP        0x25, 4, 3
#define ADC_FLD_SINGLE_CUST    0x25, 4, 3
#define ADC_FLD_BITS_CUST1     0x26, 8, 8

/**
 * Core Hardware Configuration via I2C.
 * Use these to set ADC registers and toggle GPIO pins on the board.
 */
int sdr_gpio_write_reg(int reg_addr, int reg_val);
int sdr_set_adc_config(int reg_addr, int reg_val);

/**
 * Sets a specific bitfield within an ADC register.
 * Mimics Python: adc.set_reg('name', val, write)
 * @param write_now If 1, writes to hardware immediately. If 0, only updates shadow register.
 */
int sdr_set_adc_field(int reg_addr, int bit_loc, int bit_width, int val, int write_now);

/**
 * ADC Soft Reset / Power cycle routine.
 * @param on 1 for 'on', 0 for 'off'
 */
int sdr_adc_soft_reset(int on);

/**
 * Bootstraps the ADC hardware by performing the soft reset 
 * and power cycling routines.
 */
int sdr_adc_init_sequence(void);

// Notebook-equivalent helper functions
int sdr_configure_adc_mode(const char* chan_mode, int fs);
int sdr_configure_test_mode(const char* test_mode, int custom_pat);

/**
 * Placeholders for future implementations
 */
int sdr_set_mipi_config(int value);
int sdr_set_synthesizer_config(int value);

#endif // SDR_CONFIG_H