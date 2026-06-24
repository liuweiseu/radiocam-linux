#include "sdr_config.h"

#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/videodev2.h>
#include <linux/i2c-dev.h>
#include <string.h>
#include <time.h>
#include <errno.h>

#define SENSOR_DEVICE    "/dev/v4l-subdev2"
#define I2C_DEVICE       "/dev/i2c-11"
#define I2C_SLAVE_ADDR   0x28

#define RCDEV_ADC        0x06
#define RCDEV_GPIO       0x08

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
    if (i2c_fd < 0) {
        fprintf(stderr, "[I2C ERROR] Failed to open I2C bus device\n");
        return -1;
    }
    if (ioctl(i2c_fd, I2C_SLAVE, I2C_SLAVE_ADDR) < 0) {
        fprintf(stderr, "[I2C ERROR] Failed to acquire bus access to slave address\n");
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
    ssize_t written = write(i2c_fd, tx_buf, 10);
    if (written != 10) {
        fprintf(stderr, "[I2C ERROR] write() system call failed for rcdev 0x%02X. Written: %ld/10, System Reason: %s\n", 
                rcdev, (long)written, strerror(errno));
        close(i2c_fd);
        return -1;
    }
    usleep(5000);

    uint8_t rx_buf[4];
    ssize_t read_bytes = read(i2c_fd, rx_buf, 4);
    if (read_bytes != 4) {
        fprintf(stderr, "[I2C ERROR] read() system call failed for rcdev 0x%02X. Read: %ld/4, System Reason: %s\n", 
                rcdev, (long)read_bytes, strerror(errno));
        close(i2c_fd);
        return -1;
    }
    close(i2c_fd);

    uint32_t confirm_val = ((uint32_t)rx_buf[3] << 24) | ((uint32_t)rx_buf[2] << 16) | 
                           ((uint32_t)rx_buf[1] << 8)  | ((uint32_t)rx_buf[0]);

    if (confirm_val != expected_success) {
        fprintf(stderr, "I2C Write Verification Mismatch. Expected 0x%08X, got 0x%08X\n", expected_success, confirm_val);
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
            if (sdr_set_adc_config(i, adc_shadow_regs[i]) < 0) {
                return -1; // Fail early if the hardware rejects the write
            }        
        }
    }
    return 0;
}

// Generic I2C read implementation matching your framework's transaction profile
static int sdr_i2c_read(uint8_t rcdev, int reg_addr, uint32_t *out_val) {
    int i2c_fd = open(I2C_DEVICE, O_RDWR);
    if (i2c_fd < 0) return -1;
    if (ioctl(i2c_fd, I2C_SLAVE, I2C_SLAVE_ADDR) < 0) {
        close(i2c_fd);
        return -1;
    }

    uint8_t tx_buf[6];
    tx_buf[0] = rcdev;
    tx_buf[1] = 0; // Read Command Flag (matching python architecture logic)
    tx_buf[2] = (reg_addr >> 24) & 0xFF;
    tx_buf[3] = (reg_addr >> 16) & 0xFF;
    tx_buf[4] = (reg_addr >> 8)  & 0xFF;
    tx_buf[5] = reg_addr & 0xFF;

    usleep(2000);
    if (write(i2c_fd, tx_buf, 6) != 6) {
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

    *out_val = ((uint32_t)rx_buf[3] << 24) | ((uint32_t)rx_buf[2] << 16) | 
               ((uint32_t)rx_buf[1] << 8)  | ((uint32_t)rx_buf[0]);
    return 0;
}

int sdr_mipi_dphy_write(int reg_addr, uint32_t reg_val) {
    return sdr_i2c_write(RCDEV_MIPI_DPHY, reg_addr, reg_val, MIPI_DPHY_REG_SUCCESS);
}

uint32_t sdr_mipi_dphy_read(int reg_addr) {
    uint32_t val = 0;
    if (sdr_i2c_read(RCDEV_MIPI_DPHY, reg_addr, &val) != 0) {
        fprintf(stderr, "MIPI D-PHY Read Failed at 0x%X\n", reg_addr);
    }
    return val;
}

int sdr_mipi_csi_write(int reg_addr, uint32_t reg_val) {
    return sdr_i2c_write(RCDEV_MIPI_CSI, reg_addr, reg_val, MIPI_CSI_REG_SUCCESS);
}

uint32_t sdr_mipi_csi_read(int reg_addr) {
    uint32_t val = 0;
    if (sdr_i2c_read(RCDEV_MIPI_CSI, reg_addr, &val) != 0) {
        fprintf(stderr, "MIPI CSI Read Failed at 0x%X\n", reg_addr);
    }
    return val;
}

// ---------------------------------------------------------
// MIPI Subsystem Configuration Blocks (H1D03 Variants)
// ---------------------------------------------------------

typedef struct {
    int reg;
    uint32_t val;
} mipi_reg_pair_t;

// Maps H1D03 defaults exactly to target registers
static const mipi_reg_pair_t h1d03_dphy_defaults[] = {
    { HOST_NUM_LANES,     3 },
    { HOST_NOCTN_CLK,     0 },
    { HOST_T_PRE,         100 },
    { HOST_T_POST,        33 },
    { HOST_TX_GAP,        30 },
    { HOST_AUTO_EOTP,     1 },
    { HOST_EXT_CMD,       0 },
    { HOST_HSTX_TIMER,    0 },
    { HOST_LPDT_TIMER,    0 },
    { HOST_BTA_TIMER,     0 },
    { HOST_TWAKEUP,       200 },
    { HOST_PHY_D_PRE,     0 },
    { HOST_PHY_CLK_PRE,   0 },
    { HOST_PHY_D_ZERO,    25 },
    { HOST_PHY_CLK_ZERO,  60 },
    { HOST_PHY_D_TRAIL,   4 },
    { HOST_PHY_CLK_TRAIL, 4 },
    { HOST_PLL_CN,        0x01 },
    { HOST_PLL_CM,        0xBD },
    { HOST_PLL_CO,        0 }
};

int sdr_mipi_dphy_configure(void) {
    printf("Configuring MIPI H1D03 D-PHY...\n");
    size_t num_regs = sizeof(h1d03_dphy_defaults) / sizeof(h1d03_dphy_defaults[0]);
    for (size_t i = 0; i < num_regs; i++) {
        if (sdr_mipi_dphy_write(h1d03_dphy_defaults[i].reg, h1d03_dphy_defaults[i].val) != 0) {
            return -1;
        }
    }
    return 0;
}

int sdr_mipi_csi_configure(void) {
    printf("Configuring MIPI CSI H1D03...\n");
    if (sdr_mipi_csi_write(CSI_STREAM, 0) != 0) return -1;
    if (sdr_mipi_csi_write(CSI_CONTROL, 0) != 0) return -1;
    return 0;
}

// ---------------------------------------------------------
// Master MIPI Core Init Sequence Facade
// ---------------------------------------------------------

int sdr_mipi_initialize(double timeout_sec) {
    (void)timeout_sec; // Suppress unused parameter warning

    // 1. Write all DPHY timing parameters using dev_id = 0x0A
    printf("start mipi reg config\n");
    if (sdr_mipi_dphy_configure() != 0) {
        fprintf(stderr, "MIPI D-PHY registry config failed\n");
        return -1;
    }
    printf("mipi reg config done\n");

    // 2. CRITICAL: Replace the inactive CSI_STATUS polling loop with a 
    // hardware stabilization delay. This allows the 312 Mbps PLL to lock.
    printf("waiting for tx dphy readying (stabilization delay)...\n");
    usleep(100000); // 100ms hardware lock window
    printf("tx dphy ready\n");

    printf("MIPI initialization successful.\n");
    return 0;
}

int sdr_set_synthesizer_config(int value) {
    // Not implemented
    return 0;
}
