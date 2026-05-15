#include "sdr_config.h"

#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/videodev2.h>
#include <linux/i2c-dev.h>
#include <string.h>

#define SENSOR_DEVICE    "/dev/v4l-subdev2"
#define I2C_DEVICE       "/dev/i2c-1"
#define I2C_SLAVE_ADDR   0x28

#define RCDEV_ADC        0x06
#define RCDEV_GPIO       0x08
#define ADC_REG_SUCCESS  0x55aa5506
#define GPIO_REG_SUCCESS 0x55aa5508
#define NREGS            0x58

static int sensor_fd = -1;

// Shadow registers to keep track of ADC state since it is write-only
static uint16_t adc_shadow_regs[NREGS];

// Initializes shadow registers with defaults from sdradc.py REGVALS
static void sdr_init_shadow_regs(void) {
    for (int i = 0; i < NREGS; i++) {
        adc_shadow_regs[i] = 0xFFFF; // Default 'do not write' flag
    }
    adc_shadow_regs[0x00] = 0x0000;
    adc_shadow_regs[0x0f] = 0x0000;
    adc_shadow_regs[0x11] = 0x0000;
    adc_shadow_regs[0x12] = 0x0000;
    adc_shadow_regs[0x24] = 0x0000;
    adc_shadow_regs[0x25] = 0x0000;
    adc_shadow_regs[0x26] = 0x0000;
    adc_shadow_regs[0x27] = 0x0000;
    adc_shadow_regs[0x2a] = 0x0000;
    adc_shadow_regs[0x2b] = 0x0000;
    adc_shadow_regs[0x30] = 0x0001;
    adc_shadow_regs[0x31] = 0x0004;
    adc_shadow_regs[0x33] = 0x0001;
    adc_shadow_regs[0x34] = 0x0000;
    adc_shadow_regs[0x35] = 0x0000;
    adc_shadow_regs[0x36] = 0x0000;
    adc_shadow_regs[0x37] = 0x0000;
    adc_shadow_regs[0x3a] = 0x0402;
    adc_shadow_regs[0x3b] = 0x8008;
    adc_shadow_regs[0x42] = 0x0040;
    adc_shadow_regs[0x45] = 0x0000;
    adc_shadow_regs[0x46] = 0x0000;
    adc_shadow_regs[0x50] = 0x0010;
    adc_shadow_regs[0x52] = 0x0000;
    adc_shadow_regs[0x53] = 0x0000;
    adc_shadow_regs[0x55] = 0x0020;
    adc_shadow_regs[0x56] = 0x0000;
}

int sdr_config_init(void) {
    sensor_fd = open(SENSOR_DEVICE, O_RDWR);
    if (sensor_fd < 0) {
        perror("Failed to open sensor subdevice");
        return -1;
    }
    sdr_init_shadow_regs();
    return 0;
}

void sdr_config_close(void) {
    if (sensor_fd >= 0) {
        close(sensor_fd);
        sensor_fd = -1;
    }
}

static int sdr_i2c_write(uint8_t rcdev, int reg_addr, int reg_val, uint32_t expected_success) {
    int i2c_fd = open(I2C_DEVICE, O_RDWR);
    if (i2c_fd < 0) return -1;
    if (ioctl(i2c_fd, I2C_SLAVE, I2C_SLAVE_ADDR) < 0) {
        close(i2c_fd);
        return -1;
    }

    uint8_t tx_buf[10];
    tx_buf[0] = rcdev; 
    tx_buf[1] = 1; 
    tx_buf[2] = (reg_addr >> 24) & 0xFF;
    tx_buf[3] = (reg_addr >> 16) & 0xFF;
    tx_buf[4] = (reg_addr >> 8)  & 0xFF;
    tx_buf[5] = reg_addr & 0xFF;
    tx_buf[6] = (reg_val >> 24) & 0xFF;
    tx_buf[7] = (reg_val >> 16) & 0xFF;
    tx_buf[8] = (reg_val >> 8)  & 0xFF;
    tx_buf[9] = reg_val & 0xFF;

    usleep(2000);
    if (write(i2c_fd, tx_buf, 10) != 10) {
        close(i2c_fd);
        return -1;
    }
    usleep(2000);

    uint8_t rx_buf[4];
    if (read(i2c_fd, rx_buf, 4) != 4) {
        close(i2c_fd);
        return -1;
    }
    close(i2c_fd);

    uint32_t confirm_val = ((uint32_t)rx_buf[3] << 24) | ((uint32_t)rx_buf[2] << 16) | 
                           ((uint32_t)rx_buf[1] << 8)  | ((uint32_t)rx_buf[0]);

    if (confirm_val != expected_success) {
        fprintf(stderr, "I2C Write Failed. Expected 0x%X, got 0x%X\n", expected_success, confirm_val);
        return -1;
    }
    return 0;
}

int sdr_set_adc_config(int reg_addr, int reg_val) {
    return sdr_i2c_write(RCDEV_ADC, reg_addr, reg_val, ADC_REG_SUCCESS);
}

int sdr_gpio_write_reg(int reg_addr, int reg_val) {
    return sdr_i2c_write(RCDEV_GPIO, reg_addr, reg_val, GPIO_REG_SUCCESS);
}

// ---------------------------------------------------------
// Bitfield and Mode Implementations
// ---------------------------------------------------------

int sdr_set_adc_field(int reg_addr, int bit_loc, int bit_width, int val, int write_now) {
    if (reg_addr < 0 || reg_addr >= NREGS) return -1;
    
    // Create mask for the specific bits and clear them
    uint16_t mask = ((1 << bit_width) - 1) << bit_loc;
    uint16_t current = adc_shadow_regs[reg_addr];
    
    current &= ~mask;
    current |= ((val << bit_loc) & mask);
    
    // Update shadow register
    adc_shadow_regs[reg_addr] = current;
    
    // Write out to hardware if requested
    if (write_now) {
        return sdr_set_adc_config(reg_addr, current);
    }
    return 0;
}

int sdr_adc_soft_reset(int on) {
    if (on) {
        sdr_set_adc_field(ADC_FLD_RST, 1, 1);
        usleep(100000);
        sdr_set_adc_field(ADC_FLD_PD, 1, 1);
        usleep(100000);
        // RST=1, PD=0, RST_IOB=1
        sdr_gpio_write_reg(0x00, (1 << 0) | (0 << 1) | (1 << 2));
        usleep(100000);
    } else {
        usleep(100000);
        // RST=1, PD=0, RST_IOB=0
        sdr_gpio_write_reg(0x00, (1 << 0) | (0 << 1) | (0 << 2));
        usleep(100000);
        sdr_set_adc_field(ADC_FLD_PD, 0, 1);
    }
    return 0;
}

// Exactly mirrors your Python Notebook Configuration
int sdr_configure_adc_mode(const char* chan_mode, int fs) {
    for (int i = 0; i < 2; i++) {
        sdr_adc_soft_reset(1); // 'on'
        
        if (strcmp(chan_mode, "quad") == 0) {
            if (fs == 125) {
                sdr_set_adc_field(ADC_FLD_CLK_DIVIDE, 1, 1);
                sdr_set_adc_field(ADC_FLD_LVDS_DELAY, 2, 1);
                sdr_set_adc_field(ADC_FLD_PHASE_DDR,  1, 1);
                sdr_set_adc_field(ADC_FLD_CHANNEL_NUM,4, 0); // write = False
                sdr_set_adc_field(ADC_FLD_STARTUP_CTRL, 0, 1);
            } else if (fs == 250) {
                sdr_set_adc_field(ADC_FLD_CLK_DIVIDE, 0, 1);
                sdr_set_adc_field(ADC_FLD_LVDS_DELAY, 2, 1);
                sdr_set_adc_field(ADC_FLD_PHASE_DDR,  3, 1);
                sdr_set_adc_field(ADC_FLD_CHANNEL_NUM,4, 0); // write = False
                sdr_set_adc_field(ADC_FLD_STARTUP_CTRL, 4, 1); // 0b100 = 4
            }
        } else if (strcmp(chan_mode, "dual") == 0) {
            if (fs == 250) {
                sdr_set_adc_field(ADC_FLD_CLK_DIVIDE, 0, 1);
                sdr_set_adc_field(ADC_FLD_LVDS_DELAY, 0, 1);
                sdr_set_adc_field(ADC_FLD_PHASE_DDR,  2, 1);
                sdr_set_adc_field(ADC_FLD_CHANNEL_NUM,2, 0); // write = False
                sdr_set_adc_field(ADC_FLD_STARTUP_CTRL, 0, 1);
            }
        }
        
        sdr_adc_soft_reset(0); // 'off'
    }
    printf("Configuration done.\n");
    return 0;
}

int sdr_configure_test_mode(const char* test_mode, int custom_pat) {
    if (strcmp(test_mode, "sync") == 0) {
        sdr_set_adc_field(ADC_FLD_PAT_SYNC, 2, 1);
        sdr_set_adc_field(ADC_FLD_EN_RAMP, 0, 1);
    } else if (strcmp(test_mode, "ramp") == 0) {
        sdr_set_adc_field(ADC_FLD_PAT_SYNC, 0, 1);
        sdr_set_adc_field(ADC_FLD_EN_RAMP, 4, 1); // 0b100 = 4
    } else if (strcmp(test_mode, "normal") == 0) {
        sdr_set_adc_field(ADC_FLD_PAT_SYNC, 0, 1);
        sdr_set_adc_field(ADC_FLD_EN_RAMP, 0, 1);
    } else if (strcmp(test_mode, "custom") == 0) {
        sdr_set_adc_field(ADC_FLD_SINGLE_CUST, 1, 1);
        sdr_set_adc_field(ADC_FLD_BITS_CUST1, custom_pat, 1);
    }
    printf("Test Configuration done.\n");
    return 0;
}

int sdr_adc_init_sequence(void) {
    // 1. Reset the ADC through the RST pin
    sdr_gpio_write_reg(0x00, (0 << 0) | (0 << 1) | (0 << 2));
    usleep(100000); 
    sdr_gpio_write_reg(0x00, (1 << 0) | (0 << 1) | (0 << 2));
    usleep(100000);

    // 2. Power cycle the ADC and reset FPGA IOB
    sdr_gpio_write_reg(0x00, (1 << 0) | (1 << 1) | (1 << 2));
    usleep(100000);
    sdr_gpio_write_reg(0x00, (1 << 0) | (0 << 1) | (0 << 2));
    usleep(100000);

    // 3. Push default shadow registers to hardware (only those != 0xFFFF)
    for(int i = 0; i < NREGS; i++){
        if(adc_shadow_regs[i] != 0xFFFF) {
            sdr_set_adc_config(i, adc_shadow_regs[i]);
        }
    }
    return 0;
}

int sdr_set_mipi_config(int value) {
    // Not implemented
    return 0;
}

int sdr_set_synthesizer_config(int value) {
    // Not implemented
    return 0;
}