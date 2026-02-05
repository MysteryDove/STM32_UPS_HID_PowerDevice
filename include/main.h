/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32f1xx_hal.h"
#include <stdio.h>
#include <stdbool.h>
/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

#include "ups_data.h"

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/

/* USER CODE BEGIN Private defines */

#define UART2_RX_BUFFER_SIZE 256U

void UART2_RxStartIT(void);
HAL_StatusTypeDef UART2_SendBytes(const uint8_t *data, uint16_t len, uint32_t timeout_ms);
HAL_StatusTypeDef UART2_SendBytesDMA(const uint8_t *data, uint16_t len);
bool UART2_TxDone(void);
void UART2_TxDoneClear(void);
uint16_t UART2_Available(void);
int UART2_ReadByte(uint8_t *out);
uint16_t UART2_Read(uint8_t *dst, uint16_t len);
void UART2_DiscardBuffered(void);
bool UART2_ReadExactTimeout(uint8_t *dst, uint16_t len, uint32_t timeout_ms);

bool UART2_TryLock(void);
void UART2_Unlock(void);

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
