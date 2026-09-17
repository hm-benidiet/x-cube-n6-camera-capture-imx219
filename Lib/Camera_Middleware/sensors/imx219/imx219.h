/**
  ******************************************************************************
  * @file    imx219.h
  * @brief   This file contains all the functions prototypes for the imx219.c
  *          driver.
  ******************************************************************************
  */

#ifndef IMX219_H
#define IMX219_H

#ifdef __cplusplus
 extern "C" {
#endif

#include "imx219_reg.h"
#include <stddef.h>

typedef int32_t (*IMX219_Init_Func)    (void);
typedef int32_t (*IMX219_DeInit_Func)  (void);
typedef int32_t (*IMX219_GetTick_Func) (void);
typedef int32_t (*IMX219_WriteReg_Func)(uint16_t, uint16_t, uint8_t*, uint16_t);
typedef int32_t (*IMX219_ReadReg_Func) (uint16_t, uint16_t, uint8_t*, uint16_t);

typedef struct
{
  IMX219_Init_Func          Init;
  IMX219_DeInit_Func        DeInit;
  uint16_t                  Address;
  IMX219_WriteReg_Func      WriteReg;
  IMX219_ReadReg_Func       ReadReg;
  IMX219_GetTick_Func       GetTick;
} IMX219_IO_t;

typedef struct
{
  IMX219_IO_t         IO;
  imx219_ctx_t        Ctx;
  uint8_t             IsInitialized;
} IMX219_Object_t;

#define IMX219_OK                      (0)
#define IMX219_ERROR                   (-1)

/* Camera resolutions */
#define IMX219_R1640_1232               6U   /* 1640x1232, 2x2 analog binned */

/* Camera Pixel Format */
#define IMX219_RAW_RGGB10               10U

/* Mirror/Flip */
#define IMX219_MIRROR_FLIP_NONE         0x00U
#define IMX219_FLIP                     0x01U   /* Vertical flip */
#define IMX219_MIRROR                   0x02U   /* Horizontal mirror */
#define IMX219_MIRROR_FLIP              0x03U

int32_t IMX219_RegisterBusIO(IMX219_Object_t *pObj, IMX219_IO_t *pIO);
int32_t IMX219_Init(IMX219_Object_t *pObj, uint32_t Resolution, uint32_t PixelFormat);
int32_t IMX219_Start(IMX219_Object_t *pObj);
int32_t IMX219_DeInit(IMX219_Object_t *pObj);
int32_t IMX219_ReadID(IMX219_Object_t *pObj, uint32_t *Id);
int32_t IMX219_SetGain(IMX219_Object_t *pObj, int32_t gain);
int32_t IMX219_SetExposure(IMX219_Object_t *pObj, int32_t exposure);
int32_t IMX219_SetFramerate(IMX219_Object_t *pObj, int32_t framerate);
int32_t IMX219_MirrorFlipConfig(IMX219_Object_t *pObj, uint32_t Config);
int32_t IMX219_SetTestPattern(IMX219_Object_t *pObj, int32_t mode);

/* Temporary hardware bring-up diagnostic: reduce the crop/output window to
 * 128x128 so a raw-Bayer frame fits in a small SWD-dumpable buffer, bypassing
 * the DCMIPP pixel-packer/ISP entirely. Not part of the normal capture path;
 * set to 0 to compile it out, or remove once ISP integration lands.
 * See Doc/CMake-Build.md and cmake/imx219_raw_dump.py. */
#ifndef IMX219_RAW_DUMP_TEST
#define IMX219_RAW_DUMP_TEST 1
#endif
#define IMX219_DEBUG_RAW_DUMP_WIDTH  128
#define IMX219_DEBUG_RAW_DUMP_HEIGHT 128
#if IMX219_RAW_DUMP_TEST
int32_t IMX219_DebugSmallCrop(IMX219_Object_t *pObj);
int32_t IMX219_DebugReadExposureGain(IMX219_Object_t *pObj, uint16_t *exposure_lines, uint8_t *gain_code,
                                      uint16_t *frm_length);
#endif

#ifdef __cplusplus
}
#endif

#endif /* IMX219_H */
