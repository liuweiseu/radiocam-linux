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

static int sensor_fd = -1;

int sdr_config_init(void) {
    sensor_fd = open(SENSOR_DEVICE, O_RDWR);
    if (sensor_fd < 0) {
        perror("Failed to open sensor subdevice");
        return -1;
    }
    return 0;
}

void sdr_config_close(void) {
    if (sensor_fd >= 0) {
        close(sensor_fd);
        sensor_fd = -1;
    }
}

// Internal generic I2C write function based on the Python rclib format
static int sdr_i2c_write(uint8_t rcdev, int reg_addr, int reg_val, uint32_t expected_success) {
    int i2c_fd = open(I2C_DEVICE, O_RDWR);
    if (i2c_fd < 0) return -1;

    if (ioctl(i2c_fd, I2C_SLAVE, I2C_SLAVE_ADDR) < 0) {
        close(i2c_fd);
        return -1;
    }

    uint8_t tx_buf[10];
    tx_buf[0] = rcdev; 
    tx_buf[1] = 1; // Write flag
    tx_buf[2] = (reg_addr >> 24) & 0xFF;
    tx_buf[3] = (reg_addr >> 16) & 0xFF;
    tx_buf[4] = (reg_addr >> 8)  & 0xFF;
    tx_buf[5] = reg_addr & 0xFF;
    tx_buf[6] = (reg_val >> 24) & 0xFF;
    tx_buf[7] = (reg_val >> 16) & 0xFF;
    tx_buf[8] = (reg_val >> 8)  & 0xFF;
    tx_buf[9] = reg_val & 0xFF;

    usleep(2000); // Mimics 2ms sleep from python library

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

int sdr_adc_init_sequence(void) {
    // 1. Reset the ADC through the RST pin
    sdr_gpio_write_reg(0x00, (0 << 0) | (0 << 1) | (0 << 2));
    usleep(100000); // 0.1 sec sleep
    sdr_gpio_write_reg(0x00, (1 << 0) | (0 << 1) | (0 << 2));
    usleep(100000);

    // 2. Power cycle the ADC and reset FPGA IOB
    sdr_gpio_write_reg(0x00, (1 << 0) | (1 << 1) | (1 << 2));
    usleep(100000);
    sdr_gpio_write_reg(0x00, (1 << 0) | (0 << 1) | (0 << 2));
    usleep(100000);

    // 3. Write default registers
    sdr_set_adc_config(0x00, 0x0000); // Software reset bits
    sdr_set_adc_config(0x31, 0x0004); // Modes of Operation
    sdr_set_adc_config(0x3a, 0x0402); // Input Select 1/2
    sdr_set_adc_config(0x3b, 0x8008); // Input Select 3/4
    
    // Note: The caller can push additional configurations using sdr_set_adc_config()
    
    return 0;
}

int sdr_set_control(uint32_t id, int value) {
    if (sensor_fd < 0) return -1;
    
    struct v4l2_control ctrl;
    memset(&ctrl, 0, sizeof(ctrl));
    ctrl.id = id;
    ctrl.value = value;

    if (ioctl(sensor_fd, VIDIOC_S_CTRL, &ctrl) < 0) {
        perror("VIDIOC_S_CTRL");
        return -1;
    }
    return 0;
}

int sdr_set_test_pattern(int mode) {
    return sdr_set_control(V4L2_CID_TEST_PATTERN, mode);
}

int sdr_set_mipi_config(int value) {
    // Not implemented
    return 0;
}

int sdr_set_synthesizer_config(int value) {
    // Not implemented
    return 0;
}