/* USER CODE BEGIN Header */
/**
 ******************************************************************************
 * @file           : main.c
 * @brief          : Main program body
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
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "tusb.h"
#include "usb_descriptors.h"
#include "ups_hid_reports.h"
#include "ups_hid_device.h"
#include "uart_engine.h"
/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <string.h>
#include <stdbool.h>

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/

/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
UART_HandleTypeDef huart1;
UART_HandleTypeDef huart2;

PCD_HandleTypeDef hpcd_USB_FS;

/* USER CODE BEGIN PV */
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_USART1_UART_Init(void);
static void MX_USART2_UART_Init(void);
static void MX_USB_PCD_Init(void);
/* USER CODE BEGIN PFP */
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

// Simple UART engine demo: send 'Y' and print the response.
//
// Notes:
// - uart_engine requires a fixed expected_len. For 'Y', the UPS replies with
//   2 data bytes + CRLF (0x0D 0x0A) => 4 bytes total.
// - Printing uses printf(), which is retargeted to SWO/ITM by _write below.

#ifndef UART_TEST_Y_ENABLED
#define UART_TEST_Y_ENABLED 1
#endif

#ifndef UART_TEST_Y_PERIOD_MS
#define UART_TEST_Y_PERIOD_MS 5000U
#endif

#ifndef UART_TEST_Y_EXPECTED_LEN
#define UART_TEST_Y_EXPECTED_LEN 4U
#endif

#ifndef UART_TEST_Y_CMD_CRLF_COUNT
#define UART_TEST_Y_CMD_CRLF_COUNT 0U
#endif

#ifndef UART_TEST_Y_RX_TIMEOUT_MS
#define UART_TEST_Y_RX_TIMEOUT_MS 250U
#endif

typedef struct
{
    bool in_flight;
    bool done;
    uint32_t started_ms;
    uint16_t len;
    uint8_t buf[64];
} uart_test_y_ctx_t;

static uart_test_y_ctx_t s_uart_test_y;

static bool uart_test_y_process(const uint8_t *rx, uint16_t rx_len, void *out_value, void *user_ctx)
{
    (void)out_value;

    uart_test_y_ctx_t *ctx = (uart_test_y_ctx_t *)user_ctx;
    if (ctx == NULL)
    {
        return false;
    }

    ctx->len = 0U;

    if ((rx != NULL) && (rx_len != 0U))
    {
        uint16_t copy_len = rx_len;
        if (copy_len > (uint16_t)sizeof(ctx->buf))
        {
            copy_len = (uint16_t)sizeof(ctx->buf);
        }
        (void)memcpy(ctx->buf, rx, copy_len);
        ctx->len = copy_len;
    }

    // Drain any additional already-buffered bytes (non-blocking).
    uint16_t avail = UART2_Available();
    if (avail != 0U)
    {
        uint16_t cap = (uint16_t)(sizeof(ctx->buf) - ctx->len);
        if (cap != 0U)
        {
            uint16_t want = avail;
            if (want > cap)
            {
                want = cap;
            }
            ctx->len += UART2_Read(&ctx->buf[ctx->len], want);
        }
    }

    ctx->done = true;
    ctx->in_flight = false;
    return true;
}

static void uart_engine_send_Y_and_print_task(void)
{
#if (UART_TEST_Y_ENABLED != 0)
    if (!uart_engine_is_enabled())
    {
        return;
    }

    uint32_t const now_ms = HAL_GetTick();
    static uint32_t next_send_ms = 0U;

    // Detect a lost request (engine has no user-visible failure callback).
    if (s_uart_test_y.in_flight)
    {
        // TX wait has its own timeout (UART_ENGINE_TX_TIMEOUT_MS). Add margin.
        uint32_t const overall_timeout_ms = (uint32_t)UART_TEST_Y_RX_TIMEOUT_MS + 600U;
        if ((now_ms - s_uart_test_y.started_ms) >= overall_timeout_ms)
        {
            s_uart_test_y.in_flight = false;
            printf("UART 'Y' request timed out\r\n");
        }
    }

    if (s_uart_test_y.done)
    {
        printf("UART 'Y' reply (%u bytes): ", (unsigned)s_uart_test_y.len);
        for (uint16_t i = 0U; i < s_uart_test_y.len; i++)
        {
            printf("%02X ", (unsigned)s_uart_test_y.buf[i]);
        }
        printf("\r\n");

        s_uart_test_y.done = false;
        s_uart_test_y.len = 0U;
    }

    if (!s_uart_test_y.in_flight && ((int32_t)(now_ms - next_send_ms) >= 0))
    {
        uart_engine_request_t req = {
            .out_value = NULL,
            .cmd = (uint16_t)'Y',
            .cmd_bits = 8U,
            .crlf_count = (uint8_t)UART_TEST_Y_CMD_CRLF_COUNT,
            .expected_len = (uint16_t)UART_TEST_Y_EXPECTED_LEN,
            .timeout_ms = (uint32_t)UART_TEST_Y_RX_TIMEOUT_MS,
            .max_retries = 0U,
            .process_fn = uart_test_y_process,
            .user_ctx = &s_uart_test_y,
        };

        uart_engine_result_t const r = uart_engine_enqueue(&req);
        if (r == UART_ENGINE_OK)
        {
            s_uart_test_y.in_flight = true;
            s_uart_test_y.started_ms = now_ms;
        }
        else
        {
            printf("UART 'Y' enqueue failed: %u\r\n", (unsigned)r);
        }

        next_send_ms = now_ms + (uint32_t)UART_TEST_Y_PERIOD_MS;
    }
#endif
}

// UART engine enable switch.
// Set UART_ENGINE_DEFAULT_ENABLED to 0 to compile with UART polling disabled.
#ifndef UART_ENGINE_DEFAULT_ENABLED
#define UART_ENGINE_DEFAULT_ENABLED 1
#endif

static bool s_uart_engine_enabled = (UART_ENGINE_DEFAULT_ENABLED != 0);

ups_present_status_t g_power_summary_present_status = {
    .ac_present = true,
    .charging = true,
    .discharging = false,
    .fully_charged = false,
    .need_replacement = false,
    .below_remaining_capacity_limit = false,
    .battery_present = true,
    .overload = false,
    .shutdown_imminent = false,
};

ups_summary_t g_power_summary ={
    .rechargeable = true,
    .capacity_mode = 2U,
    .design_capacity = 100U,
    .full_charge_capacity = 100U,
    .warning_capacity_limit = 20U,
    .remaining_capacity_limit = 10U,
    .i_device_chemistry = 0x05U,
    .capacity_granularity_1 = 1U,
    .capacity_granularity_2 = 1U,
    // Descriptor uses 2-bit fields, so values are 0..3.
    .i_manufacturer_2bit = 1U,
    .i_product_2bit = 2U,
    .i_serial_number_2bit = 3U,
    .i_name_2bit = 2U,
};

ups_battery_t g_battery = {
    .battery_voltage = 24000,
    .battery_current = -500,
    .config_voltage = 24000,
    .run_time_to_empty_s = 3600,
    .remaining_time_limit_s = 300,
    .temperature = 250,
    .manufacturer_date = 0,
    .remaining_capacity = 20,
};

ups_input_t g_input = {
    .voltage = 22950,
    .frequency = 5000,
    .config_voltage = 23000,
    .low_voltage_transfer = 18000,
    .high_voltage_transfer = 26000,
};
ups_output_t g_output = {
    .percent_load = 50,
    .config_active_power = 1200,
    .config_voltage = 23000,
    .voltage = 22950,
    .current = 520,
    .frequency = 5000,
};

#ifndef TEST_DECAY_REMAINING_CAPACITY_ENABLED
#define TEST_DECAY_REMAINING_CAPACITY_ENABLED 0
#endif

#if (TEST_DECAY_REMAINING_CAPACITY_ENABLED != 0)
static void test_decay_remaining_capacity(void)
{
    static uint32_t last_ms = 0U;
    uint32_t const now_ms = HAL_GetTick();

    if ((now_ms - last_ms) < 30000U)
    {
        return;
    }

    last_ms = now_ms;
    if (g_battery.remaining_capacity > 20U)
    {
        g_battery.remaining_capacity--;
        if (g_battery.run_time_to_empty_s >= 30U)
        {
            g_battery.run_time_to_empty_s -= 30U;
        }
        if (g_battery.battery_voltage >= 15U)
        {
            g_battery.battery_voltage -= 15U;
        }
    }
    g_battery.remaining_time_limit_s = g_battery.run_time_to_empty_s;
}
#endif

static void test_increase_remaining_capacity(void)
{
    static uint32_t last_ms = 0U;
    uint32_t const now_ms = HAL_GetTick();

    if ((now_ms - last_ms) < 30000U)
    {
        return;
    }

    last_ms = now_ms;
    if (g_battery.remaining_capacity < 100U)
    {
        g_battery.remaining_capacity++;

    }
    g_battery.remaining_time_limit_s = g_battery.run_time_to_empty_s;
}

int _write(int file, char *ptr, int len)
{
    (void)file;

    if ((ptr == NULL) || (len <= 0))
    {
        return 0;
    }

    // Route stdout to USART1 so a separate USB-TTL adapter can be used as a monitor.
    // Wiring (STM32F103): PA9=USART1_TX -> USB-TTL RX, GND shared.
    (void)HAL_UART_Transmit(&huart1, (uint8_t *)ptr, (uint16_t)len, 200U);
    return len;
}
/* USER CODE END 0 */

/**
 * @brief  The application entry point.
 * @retval int
 */
int main(void)
{

    /* USER CODE BEGIN 1 */

    /* USER CODE END 1 */

    /* MCU Configuration--------------------------------------------------------*/

    /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
    HAL_Init();
    /* USER CODE BEGIN Init */

    /* USER CODE END Init */

    /* Configure the system clock */
    SystemClock_Config();

    /* USER CODE BEGIN SysInit */

    /* USER CODE END SysInit */

    /* Initialize all configured peripherals */
    MX_GPIO_Init();
    MX_USART1_UART_Init();
    MX_USART2_UART_Init();
    MX_USB_PCD_Init();

    tusb_init();
    UART2_RxStartIT();
    uart_engine_init();
    uart_engine_set_enabled(s_uart_engine_enabled);

    /* USER CODE BEGIN 2 */
    if (g_battery.manufacturer_date == 0U)
    {
        uint16_t packed_date = 0U;
        if (pack_hid_date_mmddyy("02/02/26", &packed_date))
        {
            g_battery.manufacturer_date = packed_date;
        }
    }

    /* USER CODE END 2 */

    /* Infinite loop */
    /* USER CODE BEGIN WHILE */
    while (1)
    {
        /* USER CODE END WHILE */
        tud_task(); // TinyUSB device task
        /* USER CODE BEGIN 3 */

        // // Quick test: decay remaining capacity every 30 seconds.
        // test_decay_remaining_capacity();
        // test_increase_remaining_capacity(); 

        uart_engine_send_Y_and_print_task();
        uart_engine_tick();
        ups_hid_periodic_task();
    }
    /* USER CODE END 3 */
}



/**
 * @brief System Clock Configuration
 * @retval None
 */
void SystemClock_Config(void)
{
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};
    RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};

    /** Initializes the RCC Oscillators according to the specified parameters
     * in the RCC_OscInitTypeDef structure.
     */
    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    RCC_OscInitStruct.HSEState = RCC_HSE_ON;
    RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
    RCC_OscInitStruct.HSIState = RCC_HSI_ON;
    RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL6;
    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
    {
        Error_Handler();
    }

    /** Initializes the CPU, AHB and APB buses clocks
     */
    RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV2;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_1) != HAL_OK)
    {
        Error_Handler();
    }
    PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_USB;
    PeriphClkInit.UsbClockSelection = RCC_USBCLKSOURCE_PLL;
    if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK)
    {
        Error_Handler();
    }

    /** Enables the Clock Security System
     */
    HAL_RCC_EnableCSS();
}

/**
 * @brief USART2 Initialization Function
 * @param None
 * @retval None
 */
static void MX_USART2_UART_Init(void)
{

    /* USER CODE BEGIN USART2_Init 0 */

    /* USER CODE END USART2_Init 0 */

    /* USER CODE BEGIN USART2_Init 1 */

    /* USER CODE END USART2_Init 1 */
    huart2.Instance = USART2;
    huart2.Init.BaudRate = 2400;
    huart2.Init.WordLength = UART_WORDLENGTH_8B;
    huart2.Init.StopBits = UART_STOPBITS_1;
    huart2.Init.Parity = UART_PARITY_NONE;
    huart2.Init.Mode = UART_MODE_TX_RX;
    huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    huart2.Init.OverSampling = UART_OVERSAMPLING_16;
    if (HAL_UART_Init(&huart2) != HAL_OK)
    {
        Error_Handler();
    }
    /* USER CODE BEGIN USART2_Init 2 */

    /* USER CODE END USART2_Init 2 */
}

/**
 * @brief USART1 Initialization Function (monitor/printf output)
 * @param None
 * @retval None
 */
static void MX_USART1_UART_Init(void)
{
    huart1.Instance = USART1;
    huart1.Init.BaudRate = 115200;
    huart1.Init.WordLength = UART_WORDLENGTH_8B;
    huart1.Init.StopBits = UART_STOPBITS_1;
    huart1.Init.Parity = UART_PARITY_NONE;
    huart1.Init.Mode = UART_MODE_TX_RX;
    huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    huart1.Init.OverSampling = UART_OVERSAMPLING_16;
    if (HAL_UART_Init(&huart1) != HAL_OK)
    {
        Error_Handler();
    }
}

/**
 * @brief USB Initialization Function
 * @param None
 * @retval None
 */
static void MX_USB_PCD_Init(void)
{

    /* USER CODE BEGIN USB_Init 0 */

    /* USER CODE END USB_Init 0 */

    /* USER CODE BEGIN USB_Init 1 */

    /* USER CODE END USB_Init 1 */
    hpcd_USB_FS.Instance = USB;
    hpcd_USB_FS.Init.dev_endpoints = 8;
    hpcd_USB_FS.Init.speed = PCD_SPEED_FULL;
    // Keep USB fully active: VM attach/detach and host handoff often trigger
    // suspend/reset sequences; HAL low-power/LPM paths can leave the device
    // stuck until a full power-cycle.
    //
    // Previous behavior (kept for reference):
    // hpcd_USB_FS.Init.low_power_enable = ENABLE;
    // hpcd_USB_FS.Init.lpm_enable = ENABLE;
    hpcd_USB_FS.Init.low_power_enable = DISABLE;
    hpcd_USB_FS.Init.lpm_enable = DISABLE;
    hpcd_USB_FS.Init.battery_charging_enable = DISABLE;
    if (HAL_PCD_Init(&hpcd_USB_FS) != HAL_OK)
    {
        Error_Handler();
    }
    /* USER CODE BEGIN USB_Init 2 */

    /* USER CODE END USB_Init 2 */
}

/**
 * @brief GPIO Initialization Function
 * @param None
 * @retval None
 */
static void MX_GPIO_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    /* USER CODE BEGIN MX_GPIO_Init_1 */

    /* USER CODE END MX_GPIO_Init_1 */

    /* GPIO Ports Clock Enable */
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();

    /*Configure GPIO pins : PC13 PC14 PC15 */
    GPIO_InitStruct.Pin = GPIO_PIN_13 | GPIO_PIN_14 | GPIO_PIN_15;
    GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
    HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);
    GPIO_InitStruct.Pin = GPIO_PIN_13;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    GPIO_InitStruct.Pull = GPIO_PULLDOWN;
    HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

    /*Configure GPIO pins : PA0 PA1 PA4 PA5
                             PA6 PA7 PA8 PA9
                             PA10 PA15 */
    GPIO_InitStruct.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_6 | GPIO_PIN_7 | GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_10 | GPIO_PIN_15;
    GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    /*Configure GPIO pins : PB0 PB1 PB2 PB10
                             PB11 PB12 PB13 PB14
                             PB15 PB3 PB4 PB5
                             PB6 PB7 PB8 PB9 */
    GPIO_InitStruct.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_10 | GPIO_PIN_11 | GPIO_PIN_12 | GPIO_PIN_13 | GPIO_PIN_14 | GPIO_PIN_15 | GPIO_PIN_3 | GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_6 | GPIO_PIN_7 | GPIO_PIN_8 | GPIO_PIN_9;
    GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    /* USER CODE BEGIN MX_GPIO_Init_2 */

    /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
 * @brief  This function is executed in case of error occurrence.
 * @retval None
 */
void Error_Handler(void)
{
    /* USER CODE BEGIN Error_Handler_Debug */
    /* User can add his own implementation to report the HAL error return state */
    __disable_irq();
    while (1)
    {
    }
    /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
 * @brief  Reports the name of the source file and the source line number
 *         where the assert_param error has occurred.
 * @param  file: pointer to the source file name
 * @param  line: assert_param error line source number
 * @retval None
 */
void assert_failed(uint8_t *file, uint32_t line)
{
    /* USER CODE BEGIN 6 */
    /* User can add his own implementation to report the file name and line number,
       ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
    /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
