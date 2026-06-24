#ifndef SDR_CONFIG_H
#define SDR_CONFIG_H

#include <stdint.h>

// ============================================================================
// 1. DEVICE IDENTIFIERS (Datasheet §2)
// ============================================================================
#define RCDEV_SYS_HW             0x00
#define RCDEV_SYS_SW             0x01
#define RCDEV_MIPI_CSI           0x02  // Controls stream, reset, status (EMIF 0x8817)
#define RCDEV_SYNTHESIZER        0x03
#define RCDEV_LO                 0x04
#define RCDEV_MIXER              0x05
#define RCDEV_ADC                0x06  // Controls HMCAD1511 SPI shadow registers
#define RCDEV_ADC_BUFFER         0x07
#define RCDEV_GPIO               0x08  // Controls hardware board lines (EMIF 0x8808)
#define RCDEV_FPGA_STATUS        0x09
#define RCDEV_MIPI_DPHY          0x0A  // Controls D-PHY lanes and PLL (EMIF 0x8000)

// ============================================================================
// 2. I2C PROTOCOL SUCCESS CODES (Datasheet §1)
// ============================================================================
#define ADC_REG_SUCCESS          0x55AA5506
#define GPIO_REG_SUCCESS         0x55AA5508
#define MIPI_CSI_REG_SUCCESS     0x55AA5502
#define MIPI_DPHY_REG_SUCCESS    0x55AA550A

// ============================================================================
// 3. REGISTERS & BITFIELDS MAP
// ============================================================================

// --- GPIO Control Registers (Datasheet §4.6) ---
#define GPIO_CTRL_REG            0x8808
#define GPIO_BIT_ADC_RST         (1 << 0)
#define GPIO_BIT_ADC_PD          (1 << 1)
#define GPIO_BIT_FPGA_IOB_RST    (1 << 2)

// --- ADC Bitfield Packs (RegAddr, BitLoc, BitWidth) ---
#define ADC_FLD_RST              0x00, 0, 1
#define ADC_FLD_PD               0x0F, 9, 1
#define ADC_FLD_CLK_DIVIDE       0x31, 8, 2
#define ADC_FLD_LVDS_DELAY       0x53, 4, 2
#define ADC_FLD_PHASE_DDR        0x42, 5, 2
#define ADC_FLD_CHANNEL_NUM      0x31, 0, 3
#define ADC_FLD_STARTUP_CTRL     0x56, 0, 3
#define ADC_FLD_PAT_SYNC         0x45, 0, 2
#define ADC_FLD_EN_RAMP          0x25, 4, 3
#define ADC_FLD_SINGLE_CUST      0x25, 4, 3
#define ADC_FLD_BITS_CUST1       0x26, 8, 8

// --- MIPI CSI-2 Stream Engine (Datasheet §4.3, Base 0x8817) ---
#define CSI_STREAM               0x8817
#define CSI_CONTROL              0x8818
#define CSI_STATUS               0x8819

#define CSI_STREAM_ON            (1 << 0)
#define CSI_CTRL_RSTN_ALL        (1 << 1)
#define CSI_CTRL_RESET_DPI_N     (1 << 2)
#define CSI_CTRL_RSTN_MIPI       (1 << 3)
#define CSI_STATUS_TX_DPHY_RDY   (1 << 0)

// --- MIPI D-PHY & Frame Generator (Datasheet §4.8, Base 0x8000) ---
#define HOST_NUM_LANES           0x8000
#define HOST_NOCTN_CLK           0x8004
#define HOST_T_PRE               0x8008
#define HOST_T_POST              0x800C
#define HOST_TX_GAP              0x8010
#define HOST_AUTO_EOTP           0x8014
#define HOST_EXT_CMD             0x8018
#define HOST_HSTX_TIMER          0x801C
#define HOST_LPDT_TIMER          0x8020
#define HOST_BTA_TIMER           0x8024
#define HOST_TWAKEUP             0x8028

#define HOST_PHY_D_PRE           0x8300
#define HOST_PHY_CLK_PRE         0x8304
#define HOST_PHY_D_ZERO          0x8308
#define HOST_PHY_CLK_ZERO        0x830C
#define HOST_PHY_D_TRAIL         0x8310
#define HOST_PHY_CLK_TRAIL       0x8314
#define HOST_PLL_CN              0x8318
#define HOST_PLL_CM              0x831C
#define HOST_PLL_CO              0x8320

// ============================================================================
// 4. FUNCTION PROTOTYPES (API Export)
// ============================================================================

// Global Lifecycle Configurations
int  sdr_config_init(void);
void sdr_config_close(void);

// Hardware Driver Base Layer
int  sdr_gpio_write_reg(int reg_addr, int reg_val);
int  sdr_set_adc_config(int reg_addr, int reg_val);
int  sdr_set_adc_field(int reg_addr, int bit_loc, int bit_width, int val, int write_now);

// ADC Domain Routines
int  sdr_adc_soft_reset(int on);
int  sdr_adc_init_sequence(void);
int  sdr_configure_adc_mode(const char* chan_mode, int fs);
int  sdr_configure_test_mode(const char* test_mode, int custom_pat);

// MIPI Subsystem Routines
int      sdr_mipi_dphy_write(int reg_addr, uint32_t reg_val);
uint32_t sdr_mipi_dphy_read(int reg_addr);
int      sdr_mipi_csi_write(int reg_addr, uint32_t reg_val);
uint32_t sdr_mipi_csi_read(int reg_addr);

int  sdr_mipi_dphy_configure(void);
int  sdr_mipi_csi_configure(void);
int  sdr_mipi_initialize(double timeout_sec);

// Stub placeholders
int  sdr_set_synthesizer_config(int value);

#endif // SDR_CONFIG_H