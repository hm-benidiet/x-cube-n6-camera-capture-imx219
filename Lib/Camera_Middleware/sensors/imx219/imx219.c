/**
  ******************************************************************************
  * @file    imx219.c
  * @brief   This file provides the IMX219 camera driver.
  *
  * Register sequences are ported from the mainline Linux kernel driver
  * drivers/media/i2c/imx219.c (Copyright (C) 2019, Raspberry Pi (Trading)
  * Ltd), which computes framing from a crop rectangle rather than shipping
  * one fixed table per resolution. This driver only exposes the single
  * 1640x1232 2x2-analog-binned mode (full field of view), which is derived
  * from that source the same way the kernel driver derives it at runtime:
  *
  *  - X/Y_ADD_STA/END_A: crop window in native-pixel coordinates. Here the
  *    crop covers the whole active pixel array (8,8)-(3287,2471), so
  *    STA=0 and END=ACTIVE_AREA_{WIDTH,HEIGHT}-1.
  *  - X/Y_OUTPUT_SIZE: 1640x1232 (crop size / 2 for 2x2 binning).
  *  - BINNING_MODE_H/V: analog x2 (0x03).
  *  - LINE_LENGTH_A (HTS): IMX219_BINNED_LLP_MIN (3560), the kernel driver's
  *    minimum for binned RAW10 modes.
  *  - FRM_LENGTH_A (VTS): width/height/HTS fixed, so VTS is simply
  *    round(pixel_clock / (HTS * fps)); see IMX219_SetFramerate.
  *  - CSI_DATA_FORMAT_A / OPPXCK_DIV: derived from bits-per-pixel (10).
  *
  * The 2-lane PLL table (imx219_2lane_regs) and common power-up sequence
  * (imx219_common_regs) are copied verbatim (byte-for-byte) from the kernel
  * source, only reformatted from 16-bit CCI writes into explicit
  * MSB-then-LSB single-byte table entries to match this codebase's style.
  *
  * ISP/image-pipeline integration (debayering, AWB, AE) is intentionally
  * NOT implemented here -- see cmw_imx219.c.
  ******************************************************************************
  */

#include "imx219.h"
#include <math.h>
#include <string.h>

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

struct regval {
  uint16_t addr;
  uint8_t val;
};

/* Verbatim from imx219_common_regs (Linux driver), minus MODE_SELECT which
 * IMX219_Start()/IMX219_DeInit() handle separately. */
static const struct regval common_regs[] = {
  /* To access addresses 3000-5fff, send the following commands */
  {0x30eb, 0x05},
  {0x30eb, 0x0c},
  {0x300a, 0xff},
  {0x300b, 0xff},
  {0x30eb, 0x05},
  {0x30eb, 0x09},
  /* Undocumented registers */
  {0x455e, 0x00},
  {0x471e, 0x4b},
  {0x4767, 0x0f},
  {0x4750, 0x14},
  {0x4540, 0x00},
  {0x47b4, 0x14},
  {0x4713, 0x30},
  {0x478b, 0x10},
  {0x478f, 0x10},
  {0x4793, 0x10},
  {0x4797, 0x0e},
  {0x479b, 0x0e},
  /* Frame Bank Register Group "A" */
  {IMX219_REG_X_ODD_INC, 0x01},
  {IMX219_REG_Y_ODD_INC, 0x01},
  /* Output setup registers */
  {IMX219_REG_DPHY_CTRL, IMX219_DPHY_CTRL_AUTO},
  {IMX219_REG_EXCK_FREQ_MSB, 0x18}, /* 24MHz * 256 = 0x1800 */
  {IMX219_REG_EXCK_FREQ_LSB, 0x00},
};

/* Verbatim from imx219_2lane_regs (Linux driver): fixed 456MHz link
 * frequency (912Mbps/lane), independent of the output resolution/binning. */
static const struct regval lane2_pll_regs[] = {
  {IMX219_REG_VTPXCK_DIV, 5},
  {IMX219_REG_VTSYCK_DIV, 1},
  {IMX219_REG_PREPLLCK_VT_DIV, 3},  /* 0x03 = auto set */
  {IMX219_REG_PREPLLCK_OP_DIV, 3},  /* 0x03 = auto set */
  {IMX219_REG_PLL_VT_MPY_MSB, 0x00},
  {IMX219_REG_PLL_VT_MPY_LSB, 57},
  {IMX219_REG_OPSYCK_DIV, 1},
  {IMX219_REG_PLL_OP_MPY_MSB, 0x00},
  {IMX219_REG_PLL_OP_MPY_LSB, 114},
  {IMX219_REG_CSI_LANE_MODE, IMX219_CSI_2_LANE_MODE},
};

/* 1640x1232, 2x2 analog binned, full field of view, RAW10.
 * See the derivation in the file header comment. */
static const struct regval res_1640_1232_regs[] = {
  {IMX219_REG_X_ADD_STA_MSB, 0x00},
  {IMX219_REG_X_ADD_STA_LSB, 0x00},
  {IMX219_REG_X_ADD_END_MSB, 0x0c},
  {IMX219_REG_X_ADD_END_LSB, 0xcf}, /* 3279 */
  {IMX219_REG_Y_ADD_STA_MSB, 0x00},
  {IMX219_REG_Y_ADD_STA_LSB, 0x00},
  {IMX219_REG_Y_ADD_END_MSB, 0x09},
  {IMX219_REG_Y_ADD_END_LSB, 0x9f}, /* 2463 */
  {IMX219_REG_BINNING_MODE_H, IMX219_BINNING_X2_ANALOG},
  {IMX219_REG_BINNING_MODE_V, IMX219_BINNING_X2_ANALOG},
  {IMX219_REG_X_OUTPUT_SIZE_MSB, 0x06},
  {IMX219_REG_X_OUTPUT_SIZE_LSB, 0x68}, /* 1640 */
  {IMX219_REG_Y_OUTPUT_SIZE_MSB, 0x04},
  {IMX219_REG_Y_OUTPUT_SIZE_LSB, 0xd0}, /* 1232 */
  {IMX219_REG_TP_WINDOW_WIDTH_MSB, 0x06},
  {IMX219_REG_TP_WINDOW_WIDTH_LSB, 0x68},
  {IMX219_REG_TP_WINDOW_HEIGHT_MSB, 0x04},
  {IMX219_REG_TP_WINDOW_HEIGHT_LSB, 0xd0},
  {IMX219_REG_CSI_DATA_FORMAT_MSB, 0x0a}, /* bpp = 10 */
  {IMX219_REG_CSI_DATA_FORMAT_LSB, 0x0a},
  {IMX219_REG_OPPXCK_DIV, 0x0a},
  {IMX219_REG_LINE_LENGTH_MSB, 0x0d},
  {IMX219_REG_LINE_LENGTH_LSB, 0xe8}, /* HTS = 3560 */
  {IMX219_REG_FRM_LENGTH_MSB, 0x1a},
  {IMX219_REG_FRM_LENGTH_LSB, 0xaf}, /* VTS = 6831 (~15fps default) */
};

/** @defgroup IMX219_Private_Functions_Prototypes Private Functions Prototypes
  * @{
  */
static int32_t IMX219_WriteTable(IMX219_Object_t *pObj, const struct regval *regs, uint32_t size);
static int32_t IMX219_ReadRegWrap(void *handle, uint16_t Reg, uint8_t* Data, uint16_t Length);
static int32_t IMX219_WriteRegWrap(void *handle, uint16_t Reg, uint8_t* Data, uint16_t Length);
static int32_t IMX219_Delay(IMX219_Object_t *pObj, uint32_t Delay);

static int32_t IMX219_WriteTable(IMX219_Object_t *pObj, const struct regval *regs, uint32_t size)
{
  uint32_t index;
  int32_t ret = IMX219_OK;

  for (index = 0; index < size; index++)
  {
    if (ret != IMX219_ERROR)
    {
      if (imx219_write_reg(&pObj->Ctx, regs[index].addr, (uint8_t *)&(regs[index].val), 1) != IMX219_OK)
      {
        ret = IMX219_ERROR;
      }
    }
  }
  return ret;
}

static int32_t IMX219_Delay(IMX219_Object_t *pObj, uint32_t Delay)
{
  uint32_t tickstart;
  tickstart = pObj->IO.GetTick();
  while ((pObj->IO.GetTick() - tickstart) < Delay)
  {
  }
  return IMX219_OK;
}

static int32_t IMX219_ReadRegWrap(void *handle, uint16_t Reg, uint8_t* pData, uint16_t Length)
{
  IMX219_Object_t *pObj = (IMX219_Object_t *)handle;

  return pObj->IO.ReadReg(pObj->IO.Address, Reg, pData, Length);
}

static int32_t IMX219_WriteRegWrap(void *handle, uint16_t Reg, uint8_t* pData, uint16_t Length)
{
  IMX219_Object_t *pObj = (IMX219_Object_t *)handle;

  return pObj->IO.WriteReg(pObj->IO.Address, Reg, pData, Length);
}

/**
  * @brief  Write a big-endian 16-bit register as two single-byte writes.
  */
static int32_t IMX219_Write16(IMX219_Object_t *pObj, uint16_t reg_msb, uint16_t value)
{
  uint8_t bytes[2] = { (uint8_t)(value >> 8), (uint8_t)(value & 0xFF) };

  if (imx219_write_reg(&pObj->Ctx, reg_msb, &bytes[0], 1) != IMX219_OK)
  {
    return IMX219_ERROR;
  }
  return imx219_write_reg(&pObj->Ctx, reg_msb + 1, &bytes[1], 1);
}

/**
  * @brief  Read a big-endian 16-bit register written as two consecutive
  *         single-byte registers.
  */
static int32_t IMX219_Read16(IMX219_Object_t *pObj, uint16_t reg_msb, uint16_t *value)
{
  uint8_t bytes[2];

  if (imx219_read_reg(&pObj->Ctx, reg_msb, &bytes[0], 1) != IMX219_OK)
  {
    return IMX219_ERROR;
  }
  if (imx219_read_reg(&pObj->Ctx, reg_msb + 1, &bytes[1], 1) != IMX219_OK)
  {
    return IMX219_ERROR;
  }
  *value = ((uint16_t)bytes[0] << 8) | bytes[1];
  return IMX219_OK;
}

int32_t IMX219_RegisterBusIO(IMX219_Object_t *pObj, IMX219_IO_t *pIO)
{
  int32_t ret;

  if (pObj == NULL)
  {
    ret = IMX219_ERROR;
  }
  else
  {
    pObj->IO.Init      = pIO->Init;
    pObj->IO.DeInit    = pIO->DeInit;
    pObj->IO.Address   = pIO->Address;
    pObj->IO.WriteReg  = pIO->WriteReg;
    pObj->IO.ReadReg   = pIO->ReadReg;
    pObj->IO.GetTick   = pIO->GetTick;

    pObj->Ctx.ReadReg  = IMX219_ReadRegWrap;
    pObj->Ctx.WriteReg = IMX219_WriteRegWrap;
    pObj->Ctx.handle   = pObj;

    if (pObj->IO.Init != NULL)
    {
      ret = pObj->IO.Init();
    }
    else
    {
      ret = IMX219_ERROR;
    }
  }

  return ret;
}

int32_t IMX219_Init(IMX219_Object_t *pObj, uint32_t Resolution, uint32_t PixelFormat)
{
  int32_t ret = IMX219_OK;
  uint8_t standby = IMX219_MODE_STANDBY;

  if (pObj->IsInitialized == 0U)
  {
    /* Ensure the sensor is in software standby before configuring it */
    if (imx219_write_reg(&pObj->Ctx, IMX219_REG_MODE_SELECT, &standby, 1) != IMX219_OK)
    {
      return IMX219_ERROR;
    }

    if (IMX219_WriteTable(pObj, common_regs, ARRAY_SIZE(common_regs)) != IMX219_OK)
    {
      return IMX219_ERROR;
    }

    if (IMX219_WriteTable(pObj, lane2_pll_regs, ARRAY_SIZE(lane2_pll_regs)) != IMX219_OK)
    {
      return IMX219_ERROR;
    }

    switch (Resolution)
    {
      case IMX219_R1640_1232:
        if (IMX219_WriteTable(pObj, res_1640_1232_regs, ARRAY_SIZE(res_1640_1232_regs)) != IMX219_OK)
        {
          ret = IMX219_ERROR;
        }
        break;
      /* Add new resolution here */
      default:
        ret = IMX219_ERROR;
    }

    if (!ret)
    {
      pObj->IsInitialized = 1U;
    }
  }

  return ret;
}

int32_t IMX219_Start(IMX219_Object_t *pObj)
{
  uint8_t tmp;
  int32_t ret;

  tmp = IMX219_MODE_STREAMING;
  ret = imx219_write_reg(&pObj->Ctx, IMX219_REG_MODE_SELECT, &tmp, 1);
  if (ret != IMX219_OK)
  {
    return IMX219_ERROR;
  }
  IMX219_Delay(pObj, 20);
  return ret;
}

int32_t IMX219_DeInit(IMX219_Object_t *pObj)
{
  if (pObj->IsInitialized == 1U)
  {
    uint8_t standby = IMX219_MODE_STANDBY;
    (void)imx219_write_reg(&pObj->Ctx, IMX219_REG_MODE_SELECT, &standby, 1);
    pObj->IsInitialized = 0U;
  }

  return IMX219_OK;
}

int32_t IMX219_ReadID(IMX219_Object_t *pObj, uint32_t *Id)
{
  uint16_t chip_id;

  /* Initialize I2C */
  pObj->IO.Init();

  if (IMX219_Read16(pObj, IMX219_REG_CHIP_ID_MSB, &chip_id) != IMX219_OK)
  {
    return IMX219_ERROR;
  }

  *Id = chip_id;
  return IMX219_OK;
}

/**
  * @brief  Set the analog gain.
  * @param  pObj  pointer to component object
  * @param  gain  Gain in mdB
  * @retval Component status
  *
  * Converts from mdB to the sensor's analog gain code using the standard
  * Sony CIS formula: gain_linear = 256 / (256 - code), i.e.
  * code = 256 - 256 / gain_linear. This formula is not stated in the Linux
  * driver itself (it just forwards the raw V4L2 control value) but is the
  * documented analog gain law for this sensor family; verify against a
  * known target if precise gain values matter.
  */
int32_t IMX219_SetGain(IMX219_Object_t *pObj, int32_t gain)
{
  uint8_t code;
  float gain_db;
  float gain_linear;
  int32_t code_i;

  if ((gain > IMX219_GAIN_MAX) || (gain < IMX219_GAIN_MIN))
  {
    return IMX219_ERROR;
  }

  gain_db = (float)gain / 1000.0f;
  gain_linear = powf(10.0f, gain_db / 20.0f);
  code_i = (int32_t)(256.0f - 256.0f / gain_linear + 0.5f);
  if (code_i < IMX219_ANA_GAIN_MIN_CODE)
  {
    code_i = IMX219_ANA_GAIN_MIN_CODE;
  }
  if (code_i > IMX219_ANA_GAIN_MAX_CODE)
  {
    code_i = IMX219_ANA_GAIN_MAX_CODE;
  }
  code = (uint8_t)code_i;

  /* No GROUPED_PARAMETER_HOLD here: on real hardware, wrapping this write
   * with it froze the sensor's frame output after one frame. The reference
   * Linux driver (drivers/media/i2c/imx219.c) writes this register directly
   * too, without any grouped-hold mechanism. */
  return imx219_write_reg(&pObj->Ctx, IMX219_REG_ANALOG_GAIN, &code, 1);
}

/**
  * @brief  Set the exposure.
  * @param  pObj      pointer to component object
  * @param  exposure  Exposure in microseconds
  * @retval Component status
  */
int32_t IMX219_SetExposure(IMX219_Object_t *pObj, int32_t exposure)
{
  uint16_t vts;
  uint32_t exposure_lines;

  if (IMX219_Read16(pObj, IMX219_REG_FRM_LENGTH_MSB, &vts) != IMX219_OK)
  {
    return IMX219_ERROR;
  }

  exposure_lines = (uint32_t)((float)exposure / IMX219_1H_PERIOD_USEC + 0.5f);
  if (exposure_lines < IMX219_EXPOSURE_MIN_LINES)
  {
    return IMX219_ERROR;
  }
  if (exposure_lines > (uint32_t)(vts - IMX219_EXPOSURE_OFFSET))
  {
    exposure_lines = (uint32_t)(vts - IMX219_EXPOSURE_OFFSET);
  }

  /* No GROUPED_PARAMETER_HOLD here: on real hardware, wrapping this write
   * with it froze the sensor's frame output after one frame. The reference
   * Linux driver (drivers/media/i2c/imx219.c) writes this register directly
   * too, without any grouped-hold mechanism. */
  return IMX219_Write16(pObj, IMX219_REG_EXPOSURE_MSB, (uint16_t)exposure_lines);
}

/**
  * @brief  Set the framerate by adjusting the frame length (VTS); the line
  *         length (HTS) and pixel clock are fixed by the mode configured in
  *         IMX219_Init(), so this only needs to compute
  *         VTS = round(pixel_clock / (HTS * fps)).
  * @param  pObj       pointer to component object
  * @param  framerate  10, 15, 20, 25 or 30 fps
  * @retval Component status
  */
int32_t IMX219_SetFramerate(IMX219_Object_t *pObj, int32_t framerate)
{
  uint32_t vts;

  if (framerate <= 0)
  {
    return IMX219_ERROR;
  }

  vts = (uint32_t)((float)IMX219_VT_PIX_CLK_HZ / ((float)IMX219_LINE_LENGTH * (float)framerate) + 0.5f);

  if (vts < (IMX219_HEIGHT + IMX219_VBLANK_MIN))
  {
    vts = IMX219_HEIGHT + IMX219_VBLANK_MIN;
  }
  if (vts > IMX219_FRM_LENGTH_MAX)
  {
    return IMX219_ERROR;
  }

  return IMX219_Write16(pObj, IMX219_REG_FRM_LENGTH_MSB, (uint16_t)vts);
}

/**
  * @brief  Control IMX219 camera mirror/vflip using the sensor's single
  *         orientation register (bit0 = hflip/mirror, bit1 = vflip).
  * @param  pObj    pointer to component object
  * @param  Config  To configure mirror, flip, both or none
  * @retval Component status
  */
int32_t IMX219_MirrorFlipConfig(IMX219_Object_t *pObj, uint32_t Config)
{
  uint8_t reg_val;

  switch (Config)
  {
    case IMX219_FLIP:         /* vertical flip only */
      reg_val = 0x02;
      break;
    case IMX219_MIRROR:       /* horizontal mirror only */
      reg_val = 0x01;
      break;
    case IMX219_MIRROR_FLIP:
      reg_val = 0x03;
      break;
    case IMX219_MIRROR_FLIP_NONE:
    default:
      reg_val = 0x00;
      break;
  }

  return imx219_write_reg(&pObj->Ctx, IMX219_REG_ORIENTATION, &reg_val, 1);
}

/**
  * @brief  Set the Test Pattern Generator.
  * @param  pObj  pointer to component object
  * @param  mode  Pattern mode:
  *              -1 : Disable
  *               0 : Color bars
  *               1 : Solid color
  *               2 : Grey color bars
  *               3 : PN9
  *               4 : 16 split color bars
  *               5 : 16 split inverted color bars
  *               6 : Column counter
  *               7 : Inverted column counter
  *               8 : PN31
  * @retval Component status
  */
int32_t IMX219_SetTestPattern(IMX219_Object_t *pObj, int32_t mode)
{
  uint16_t reg_val = (mode >= 0) ? (uint16_t)(mode + 1) : 0;

  return IMX219_Write16(pObj, IMX219_REG_TEST_PATTERN_MSB, reg_val);
}

#if IMX219_RAW_DUMP_TEST
/**
  * @brief  Diagnostic only: shrink the crop/output window to a small,
  *         centered 128x128 region (still 2x2 analog binned) so a raw
  *         frame is small enough to dump over SWD without needing a
  *         multi-megabyte buffer. See imx219.h for context.
  *
  * Crop is centered in the active area: width=height=256 (2x binned down
  * to 128x128), left=(3280-256)/2=1512, top=(2464-256)/2=1104.
  */
int32_t IMX219_DebugSmallCrop(IMX219_Object_t *pObj)
{
  static const struct regval debug_small_crop_regs[] = {
    {IMX219_REG_X_ADD_STA_MSB, 0x05},
    {IMX219_REG_X_ADD_STA_LSB, 0xe8}, /* 1512 */
    {IMX219_REG_X_ADD_END_MSB, 0x06},
    {IMX219_REG_X_ADD_END_LSB, 0xe7}, /* 1767 */
    {IMX219_REG_Y_ADD_STA_MSB, 0x04},
    {IMX219_REG_Y_ADD_STA_LSB, 0x50}, /* 1104 */
    {IMX219_REG_Y_ADD_END_MSB, 0x05},
    {IMX219_REG_Y_ADD_END_LSB, 0x4f}, /* 1359 */
    {IMX219_REG_BINNING_MODE_H, IMX219_BINNING_X2_ANALOG},
    {IMX219_REG_BINNING_MODE_V, IMX219_BINNING_X2_ANALOG},
    {IMX219_REG_X_OUTPUT_SIZE_MSB, 0x00},
    {IMX219_REG_X_OUTPUT_SIZE_LSB, 0x80}, /* 128 */
    {IMX219_REG_Y_OUTPUT_SIZE_MSB, 0x00},
    {IMX219_REG_Y_OUTPUT_SIZE_LSB, 0x80}, /* 128 */
  };

  return IMX219_WriteTable(pObj, debug_small_crop_regs, ARRAY_SIZE(debug_small_crop_regs));
}

/**
  * @brief  Diagnostic only: read back the registers IMX219_SetGain()/
  *         IMX219_SetExposure() actually wrote, to check them against what
  *         was requested.
  */
int32_t IMX219_DebugReadExposureGain(IMX219_Object_t *pObj, uint16_t *exposure_lines, uint8_t *gain_code,
                                      uint16_t *frm_length)
{
  if (imx219_read_reg(&pObj->Ctx, IMX219_REG_ANALOG_GAIN, gain_code, 1) != IMX219_OK)
  {
    return IMX219_ERROR;
  }
  if (IMX219_Read16(pObj, IMX219_REG_EXPOSURE_MSB, exposure_lines) != IMX219_OK)
  {
    return IMX219_ERROR;
  }
  return IMX219_Read16(pObj, IMX219_REG_FRM_LENGTH_MSB, frm_length);
}
#endif /* IMX219_RAW_DUMP_TEST */
