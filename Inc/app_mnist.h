/**
 ******************************************************************************
 * @file    app_mnist.h
 *
 ******************************************************************************
 */
#ifndef APP_MNIST
#define APP_MNIST

#include <stdint.h>

/* user button (PC13) that triggers a classification */
#ifdef STM32N6570_DK_REV
#define MNIST_BUTTON BUTTON_USER1
#else
#define MNIST_BUTTON BUTTON_USER
#endif

void MNIST_Init(void);
void MNIST_ProcessFrame(uint8_t *frame, int width, int height);

#endif
