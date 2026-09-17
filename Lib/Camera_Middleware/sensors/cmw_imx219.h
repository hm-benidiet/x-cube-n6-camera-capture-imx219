/**
  ******************************************************************************
  * @file    cmw_imx219.h
  ******************************************************************************
  */

#ifndef CMW_IMX219
#define CMW_IMX219

#ifdef __cplusplus
 extern "C" {
#endif

#include <stdint.h>
#include "cmw_sensors_if.h"
#include "cmw_errno.h"
#include "imx219.h"
#include "isp_api.h"
#include "cmw_camera.h"

typedef struct
{
  uint16_t Address;
  uint32_t ClockInHz;
  IMX219_Object_t ctx_driver;
  ISP_HandleTypeDef hIsp;
  ISP_AppliHelpersTypeDef appliHelpers;
  DCMIPP_HandleTypeDef *hdcmipp;
  uint8_t IsInitialized;
  int32_t (*Init)(void);
  int32_t (*DeInit)(void);
  int32_t (*WriteReg)(uint16_t, uint16_t, uint8_t*, uint16_t);
  int32_t (*ReadReg) (uint16_t, uint16_t, uint8_t*, uint16_t);
  int32_t (*GetTick) (void);
  void (*Delay)(uint32_t delay_in_ms);
  void (*ShutdownPin)(int value);
  void (*EnablePin)(int value);
} CMW_IMX219_t;

int CMW_IMX219_Probe(CMW_IMX219_t *io_ctx, CMW_Sensor_if_t *imx219_if);
void CMW_IMX219_SetDefaultSensorValues(CMW_IMX219_config_t *imx219_config);

#ifdef __cplusplus
}
#endif

#endif
