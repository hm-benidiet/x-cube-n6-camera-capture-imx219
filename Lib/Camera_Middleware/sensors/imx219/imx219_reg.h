/**
  ******************************************************************************
  * @file    imx219_reg.h
  * @brief   Header of imx219_reg.c
  *
  * Register set ported from the Linux kernel driver
  * drivers/media/i2c/imx219.c (Copyright (C) 2019, Raspberry Pi (Trading) Ltd),
  * adapted to this codebase's 8-bit-per-transfer register I/O style (all
  * multi-byte registers are written/read as explicit MSB-then-LSB byte pairs,
  * matching how imx335_reg.h/imx335.c already do it).
  ******************************************************************************
  */

#ifndef IMX219_REG_H
#define IMX219_REG_H

#include <cmsis_compiler.h>

#ifdef __cplusplus
 extern "C" {
#endif

/* Chip identification: 16-bit register, MSB at the lower address */
#define IMX219_REG_CHIP_ID_MSB    0x0000
#define IMX219_REG_CHIP_ID_LSB    0x0001
#define IMX219_CHIP_ID            0x0219U

#define IMX219_REG_MODE_SELECT    0x0100
#define IMX219_MODE_STANDBY         0x00
#define IMX219_MODE_STREAMING       0x01

#define IMX219_REG_HOLD           0x0104  /* GROUPED_PARAMETER_HOLD */

#define IMX219_REG_CSI_LANE_MODE  0x0114
#define IMX219_CSI_2_LANE_MODE      0x01

#define IMX219_REG_DPHY_CTRL      0x0128
#define IMX219_DPHY_CTRL_AUTO       0x00

#define IMX219_REG_EXCK_FREQ_MSB  0x012a
#define IMX219_REG_EXCK_FREQ_LSB  0x012b

#define IMX219_REG_ANALOG_GAIN    0x0157  /* 8-bit, code = 256 - 256/gain_linear */

#define IMX219_REG_DIGITAL_GAIN_MSB  0x0158
#define IMX219_REG_DIGITAL_GAIN_LSB  0x0159

#define IMX219_REG_EXPOSURE_MSB   0x015a  /* Coarse integration time, in row units */
#define IMX219_REG_EXPOSURE_LSB   0x015b

#define IMX219_REG_FRM_LENGTH_MSB 0x0160  /* VTS */
#define IMX219_REG_FRM_LENGTH_LSB 0x0161
#define IMX219_REG_LINE_LENGTH_MSB 0x0162 /* HTS */
#define IMX219_REG_LINE_LENGTH_LSB 0x0163

#define IMX219_REG_X_ADD_STA_MSB  0x0164
#define IMX219_REG_X_ADD_STA_LSB  0x0165
#define IMX219_REG_X_ADD_END_MSB  0x0166
#define IMX219_REG_X_ADD_END_LSB  0x0167
#define IMX219_REG_Y_ADD_STA_MSB  0x0168
#define IMX219_REG_Y_ADD_STA_LSB  0x0169
#define IMX219_REG_Y_ADD_END_MSB  0x016a
#define IMX219_REG_Y_ADD_END_LSB  0x016b
#define IMX219_REG_X_OUTPUT_SIZE_MSB 0x016c
#define IMX219_REG_X_OUTPUT_SIZE_LSB 0x016d
#define IMX219_REG_Y_OUTPUT_SIZE_MSB 0x016e
#define IMX219_REG_Y_OUTPUT_SIZE_LSB 0x016f
#define IMX219_REG_X_ODD_INC      0x0170
#define IMX219_REG_Y_ODD_INC      0x0171
#define IMX219_REG_ORIENTATION    0x0172  /* bit0 = hflip (mirror), bit1 = vflip */

#define IMX219_REG_BINNING_MODE_H 0x0174
#define IMX219_REG_BINNING_MODE_V 0x0175
#define IMX219_BINNING_NONE         0x00
#define IMX219_BINNING_X2_ANALOG    0x03

#define IMX219_REG_CSI_DATA_FORMAT_MSB 0x018c
#define IMX219_REG_CSI_DATA_FORMAT_LSB 0x018d

#define IMX219_REG_VTPXCK_DIV      0x0301
#define IMX219_REG_VTSYCK_DIV      0x0303
#define IMX219_REG_PREPLLCK_VT_DIV 0x0304
#define IMX219_REG_PREPLLCK_OP_DIV 0x0305
#define IMX219_REG_PLL_VT_MPY_MSB  0x0306
#define IMX219_REG_PLL_VT_MPY_LSB  0x0307
#define IMX219_REG_OPPXCK_DIV      0x0309
#define IMX219_REG_OPSYCK_DIV      0x030b
#define IMX219_REG_PLL_OP_MPY_MSB  0x030c
#define IMX219_REG_PLL_OP_MPY_LSB  0x030d

#define IMX219_REG_TEST_PATTERN_MSB 0x0600
#define IMX219_REG_TEST_PATTERN_LSB 0x0601
#define IMX219_REG_TP_WINDOW_WIDTH_MSB  0x0624
#define IMX219_REG_TP_WINDOW_WIDTH_LSB  0x0625
#define IMX219_REG_TP_WINDOW_HEIGHT_MSB 0x0626
#define IMX219_REG_TP_WINDOW_HEIGHT_LSB 0x0627

/* External clock: this board supplies a fixed 24MHz CAM_CLK, same as the
 * IMX335 module on the same connector. */
#define IMX219_XCLK_FREQ_HZ       24000000UL

/* Sensor native/active pixel array (used to compute the crop window) */
#define IMX219_NATIVE_WIDTH       3296U
#define IMX219_NATIVE_HEIGHT      2480U
#define IMX219_ACTIVE_AREA_LEFT   8U
#define IMX219_ACTIVE_AREA_TOP    8U
#define IMX219_ACTIVE_AREA_WIDTH  3280U
#define IMX219_ACTIVE_AREA_HEIGHT 2464U

/* Supported mode: 2x2 analog-binned, full field of view, RAW10.
 * PLL/lane configuration below always yields a fixed 456MHz link frequency
 * (912Mbps/lane, 2 lanes), regardless of binning -- only VTS/HTS and the
 * crop/output-size registers change per mode. */
#define IMX219_WIDTH              1640
#define IMX219_HEIGHT             1232
#define IMX219_LINE_LENGTH        3560   /* HTS, "binned" minimum per datasheet */
#define IMX219_VT_PIX_CLK_HZ      364800000UL /* 182.4MHz pixel rate x2 (analog binning) */
#define IMX219_FRM_LENGTH_MAX     0xfffeU
#define IMX219_VBLANK_MIN         32U

#define IMX219_1H_PERIOD_USEC     (IMX219_LINE_LENGTH * 1000000.0F / IMX219_VT_PIX_CLK_HZ)

#define IMX219_EXPOSURE_MIN_LINES 4U
#define IMX219_EXPOSURE_OFFSET    4U

#define IMX219_NAME               "IMX219"
#define IMX219_BAYER_PATTERN      0 /* RGGB (no flip) */
#define IMX219_COLOR_DEPTH        10 /* in bits */

#define IMX219_ANA_GAIN_MIN_CODE  0
#define IMX219_ANA_GAIN_MAX_CODE  232
/* Gain interface is in mdB, converted to/from the sensor's analog gain code
 * via the standard Sony CIS formula: gain_linear = 256 / (256 - code).
 * GAIN_MAX below is code=232 expressed in mdB (20*log10(256/(256-232))). */
#define IMX219_GAIN_MIN           0
#define IMX219_GAIN_MAX           20560
#define IMX219_EXPOSURE_MIN       39      /* 4 lines, in us */
#define IMX219_EXPOSURE_DEFAULT   15614   /* 1600 lines, in us */
#define IMX219_EXPOSURE_MAX       66644   /* for sensor @15fps, in us */

/************** Generic Function  *******************/

typedef int32_t (*IMX219_Write_Func)(void *, uint16_t, uint8_t*, uint16_t);
typedef int32_t (*IMX219_Read_Func) (void *, uint16_t, uint8_t*, uint16_t);

typedef struct
{
  IMX219_Write_Func   WriteReg;
  IMX219_Read_Func    ReadReg;
  void                *handle;
} imx219_ctx_t;

int32_t imx219_write_reg(imx219_ctx_t *ctx, uint16_t reg, uint8_t *pdata, uint16_t length);
int32_t imx219_read_reg(imx219_ctx_t *ctx, uint16_t reg, uint8_t *pdata, uint16_t length);

#ifdef __cplusplus
}
#endif

#endif /* IMX219_REG_H */
