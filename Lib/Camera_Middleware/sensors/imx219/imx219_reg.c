/**
  ******************************************************************************
  * @file    imx219_reg.c
  * @brief   This file provides unitary register functions to control the
  *          IMX219 Camera driver.
  ******************************************************************************
  */

#include "imx219_reg.h"

int32_t imx219_read_reg(imx219_ctx_t *ctx, uint16_t reg, uint8_t *pdata, uint16_t length)
{
  return ctx->ReadReg(ctx->handle, reg, pdata, length);
}

int32_t imx219_write_reg(imx219_ctx_t *ctx, uint16_t reg, uint8_t *data, uint16_t length)
{
  return ctx->WriteReg(ctx->handle, reg, data, length);
}
