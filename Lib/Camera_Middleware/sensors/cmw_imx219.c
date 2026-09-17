/**
  ******************************************************************************
  * @file    cmw_imx219.c
  *
  * CMW sensor wrapper for the IMX219 hardware driver (imx219.c).
  *
  * ISP tuning (Inc/imx219_isp_param_conf.h) is adapted from IMX335's, not
  * calibrated against a real IMX219 module -- expect a color cast until it's
  * redone with real STM32 ISP IQTune calibration. See that file's header
  * comment for exactly which fields are placeholders.
  ******************************************************************************
  */

#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <stddef.h>
#include <string.h>
#include "cmw_imx219.h"
#include "cmw_camera.h"
#include "imx219_reg.h"
#include "imx219.h"
#ifndef ISP_MW_TUNING_TOOL_SUPPORT
#include "imx219_isp_param_conf.h"
#endif

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

static int CMW_IMX219_GetResType(uint32_t width, uint32_t height, uint32_t *res)
{
  if (width == IMX219_WIDTH && height == IMX219_HEIGHT)
  {
    *res = IMX219_R1640_1232;
  }
  else
  {
    return CMW_ERROR_WRONG_PARAM;
  }
  return 0;
}

static int32_t CMW_IMX219_getMirrorFlipConfig(uint32_t Config)
{
  int32_t ret;

  switch (Config)
  {
    case CMW_MIRRORFLIP_NONE:
      ret = IMX219_MIRROR_FLIP_NONE;
      break;
    case CMW_MIRRORFLIP_FLIP:
      ret = IMX219_FLIP;
      break;
    case CMW_MIRRORFLIP_MIRROR:
      ret = IMX219_MIRROR;
      break;
    case CMW_MIRRORFLIP_FLIP_MIRROR:
    default:
      ret = IMX219_MIRROR_FLIP;
      break;
  }

  return ret;
}

static int32_t CMW_IMX219_DeInit(void *io_ctx)
{
  return IMX219_DeInit(&((CMW_IMX219_t *)io_ctx)->ctx_driver) ? CMW_ERROR_COMPONENT_FAILURE : CMW_ERROR_NONE;
}

static int32_t CMW_IMX219_ReadID(void *io_ctx, uint32_t *Id)
{
  return IMX219_ReadID(&((CMW_IMX219_t *)io_ctx)->ctx_driver, Id);
}

static int32_t CMW_IMX219_SetGain(void *io_ctx, int32_t gain)
{
  return IMX219_SetGain(&((CMW_IMX219_t *)io_ctx)->ctx_driver, gain);
}

static int32_t CMW_IMX219_SetExposure(void *io_ctx, int32_t exposure)
{
  return IMX219_SetExposure(&((CMW_IMX219_t *)io_ctx)->ctx_driver, exposure);
}

static int32_t CMW_IMX219_SetFramerate(void *io_ctx, int32_t framerate)
{
  const int32_t available_imx219_fps[] = {10, 15, 20, 25, 30};

  for (int i = 0; i < ARRAY_SIZE(available_imx219_fps); i++)
    if (framerate == available_imx219_fps[i])
      return IMX219_SetFramerate(&((CMW_IMX219_t *)io_ctx)->ctx_driver, framerate);

  return CMW_ERROR_WRONG_PARAM;
}

static int32_t CMW_IMX219_SetMirrorFlip(void *io_ctx, uint32_t config)
{
  int32_t mirrorFlip = CMW_IMX219_getMirrorFlipConfig(config);
  return IMX219_MirrorFlipConfig(&((CMW_IMX219_t *)io_ctx)->ctx_driver, mirrorFlip);
}

static int32_t CMW_IMX219_GetSensorInfo(void *io_ctx, ISP_SensorInfoTypeDef *info)
{
  if ((io_ctx == NULL) || (info == NULL))
  {
    return CMW_ERROR_WRONG_PARAM;
  }

  if (sizeof(info->name) >= strlen(IMX219_NAME) + 1)
  {
    strcpy(info->name, IMX219_NAME);
  }
  else
  {
    return CMW_ERROR_COMPONENT_FAILURE;
  }

  info->bayer_pattern = IMX219_BAYER_PATTERN;
  info->color_depth = IMX219_COLOR_DEPTH;
  info->width = IMX219_WIDTH;
  info->height = IMX219_HEIGHT;
  info->gain_min = IMX219_GAIN_MIN;
  info->gain_max = IMX219_GAIN_MAX;
  info->again_max = IMX219_GAIN_MAX;
  info->exposure_min = IMX219_EXPOSURE_MIN;
  info->exposure_max = IMX219_EXPOSURE_MAX;

  return CMW_ERROR_NONE;
}

static int32_t CMW_IMX219_SetTestPattern(void *io_ctx, int32_t mode)
{
  return IMX219_SetTestPattern(&((CMW_IMX219_t *)io_ctx)->ctx_driver, mode);
}

static int32_t CMW_IMX219_Init(void *io_ctx, CMW_Sensor_Init_t *initSensor)
{
  int ret;
  uint32_t resolution;
  CMW_IMX219_config_t *sensor_config;
  sensor_config = (CMW_IMX219_config_t *)(initSensor->sensor_config);
  if (sensor_config == NULL)
  {
    return CMW_ERROR_WRONG_PARAM;
  }

  ret = CMW_IMX219_GetResType(initSensor->width, initSensor->height, &resolution);
  if (ret)
  {
    return CMW_ERROR_WRONG_PARAM;
  }

  ret = CMW_IMX219_SetMirrorFlip(io_ctx, initSensor->mirrorFlip);
  if (ret)
  {
    return CMW_ERROR_WRONG_PARAM;
  }

  ret = IMX219_Init(&((CMW_IMX219_t *)io_ctx)->ctx_driver, resolution, sensor_config->pixel_format);
  if (ret != IMX219_OK)
  {
    return CMW_ERROR_COMPONENT_FAILURE;
  }

  return CMW_ERROR_NONE;
}

void CMW_IMX219_SetDefaultSensorValues(CMW_IMX219_config_t *imx219_config)
{
  assert(imx219_config != NULL);
  imx219_config->pixel_format = CMW_PIXEL_FORMAT_RAW10;
}

static int32_t CMW_IMX219_Start(void *io_ctx)
{
#ifndef ISP_MW_TUNING_TOOL_SUPPORT
  int ret;
  /* Statistic area is provided with null value so that it force the ISP Library to get the statistic
   * area information from the tuning file.
   */
  ret = ISP_Init(&((CMW_IMX219_t *)io_ctx)->hIsp, ((CMW_IMX219_t *)io_ctx)->hdcmipp, 0, &((CMW_IMX219_t *)io_ctx)->appliHelpers, &ISP_IQParamCacheInit_IMX219);
  if (ret != ISP_OK)
  {
    return CMW_ERROR_COMPONENT_FAILURE;
  }

  ret = ISP_Start(&((CMW_IMX219_t *)io_ctx)->hIsp);
  if (ret != ISP_OK)
  {
      return CMW_ERROR_PERIPH_FAILURE;
  }
#endif
  return IMX219_Start(&((CMW_IMX219_t *)io_ctx)->ctx_driver);
}

static int32_t CMW_IMX219_Run(void *io_ctx)
{
#ifndef ISP_MW_TUNING_TOOL_SUPPORT
  int ret;
  ret = ISP_BackgroundProcess(&((CMW_IMX219_t *)io_ctx)->hIsp);
  if (ret != ISP_OK)
  {
      return CMW_ERROR_PERIPH_FAILURE;
  }
#endif
  return CMW_ERROR_NONE;
}

static void CMW_IMX219_PowerOn(CMW_IMX219_t *io_ctx)
{
  /* Per the IMX219 datasheet / Linux driver (IMX219_XCLR_MIN_DELAY_US),
   * the sensor needs at least 6.2ms between releasing XCLR (reset) and the
   * first I2C register access, notably longer than the IMX335's ~1ms.
   *
   * Same board IO mapping as the IMX335 module on this connector:
   *  ShutdownPin PC8 (CN14-17) -- (CN1-17) NRST_CAM  -- RESET (XCLR)
   *  EnablePin   PD2 (CN14-18) -- (CN1-18) EN_MODULE -- DVDD + 24MHz CAM_CLK
   */
  io_ctx->EnablePin(1);   /* Enable DVDD and 24MHz CAM_CLK */
  io_ctx->ShutdownPin(0); /* Set RESET low */
  io_ctx->Delay(1);       /* Hold RESET low for at least 500 ns */
  io_ctx->ShutdownPin(1); /* Release RESET */
  io_ctx->Delay(7);       /* >= 6.2ms before the sensor accepts I2C access */
}

static void CMW_IMX219_VsyncEventCallback(void *io_ctx, uint32_t pipe)
{
#ifndef ISP_MW_TUNING_TOOL_SUPPORT
  /* Update the ISP frame counter and call its statistics handler */
  switch (pipe)
  {
    case DCMIPP_PIPE0 :
      ISP_IncDumpFrameId(&((CMW_IMX219_t *)io_ctx)->hIsp);
      break;
    case DCMIPP_PIPE1 :
      ISP_IncMainFrameId(&((CMW_IMX219_t *)io_ctx)->hIsp);
      ISP_GatherStatistics(&((CMW_IMX219_t *)io_ctx)->hIsp);
      break;
    case DCMIPP_PIPE2 :
      ISP_IncAncillaryFrameId(&((CMW_IMX219_t *)io_ctx)->hIsp);
      break;
  }
#endif
}

static void CMW_IMX219_FrameEventCallback(void *io_ctx, uint32_t pipe)
{
}

int CMW_IMX219_Probe(CMW_IMX219_t *io_ctx, CMW_Sensor_if_t *imx219_if)
{
  int ret;
  uint32_t id;
  io_ctx->ctx_driver.IO.Address = io_ctx->Address;
  io_ctx->ctx_driver.IO.Init = io_ctx->Init;
  io_ctx->ctx_driver.IO.DeInit = io_ctx->DeInit;
  io_ctx->ctx_driver.IO.GetTick = io_ctx->GetTick;
  io_ctx->ctx_driver.IO.ReadReg = io_ctx->ReadReg;
  io_ctx->ctx_driver.IO.WriteReg = io_ctx->WriteReg;

  CMW_IMX219_PowerOn(io_ctx);

  ret = IMX219_RegisterBusIO(&io_ctx->ctx_driver, &io_ctx->ctx_driver.IO);
  if (ret != IMX219_OK)
  {
    return CMW_ERROR_COMPONENT_FAILURE;
  }

  ret = IMX219_ReadID(&io_ctx->ctx_driver, &id);
  if (ret != IMX219_OK)
  {
    return CMW_ERROR_COMPONENT_FAILURE;
  }
  if (id != IMX219_CHIP_ID)
  {
    ret = CMW_ERROR_UNKNOWN_COMPONENT;
  }

  memset(imx219_if, 0, sizeof(*imx219_if));
  imx219_if->Init = CMW_IMX219_Init;
  imx219_if->Start = CMW_IMX219_Start;
  imx219_if->DeInit = CMW_IMX219_DeInit;
  imx219_if->Run = CMW_IMX219_Run;
  imx219_if->VsyncEventCallback = CMW_IMX219_VsyncEventCallback;
  imx219_if->FrameEventCallback = CMW_IMX219_FrameEventCallback;
  imx219_if->ReadID = CMW_IMX219_ReadID;
  imx219_if->SetGain = CMW_IMX219_SetGain;
  imx219_if->SetExposure = CMW_IMX219_SetExposure;
  imx219_if->SetFramerate = CMW_IMX219_SetFramerate;
  imx219_if->SetMirrorFlip = CMW_IMX219_SetMirrorFlip;
  imx219_if->GetSensorInfo = CMW_IMX219_GetSensorInfo;
  imx219_if->SetTestPattern = CMW_IMX219_SetTestPattern;
  return ret;
}
