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
#include "spm2k.h"
/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdbool.h>
#include <string.h>

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

// ---- USB init gate (simple) -----------------------------------------------
// User-controlled variable: set true to initialize USB device stack.
// Default is false: USB is not initialized at all.
volatile bool g_usb_init_enabled = false;
static bool s_usb_started = false;

// Blue Pill hardware note:
// Your schematic shows a fixed 1.5k pull-up (R10) from D+ to 3V3. That means the
// host will see an attach as soon as you plug the cable in, even if firmware
// hasn't initialized USB yet.
//
// Workaround (no hardware change): drive PA12 (D+) low as GPIO until we are
// ready to start USB, then release it.
#ifndef USB_HOLD_DP_LOW_UNTIL_USB_START
#define USB_HOLD_DP_LOW_UNTIL_USB_START 1
#endif

#ifndef UPS_DYNAMIC_UPDATE_PERIOD_S
#define UPS_DYNAMIC_UPDATE_PERIOD_S 5U
#endif

#ifndef UPS_INIT_RETRY_PERIOD_S
#define UPS_INIT_RETRY_PERIOD_S 5U
#endif

#ifndef UPS_DEBUG_STATUS_PRINT_ENABLED
#define UPS_DEBUG_STATUS_PRINT_ENABLED 1
#endif

#ifndef UPS_DEBUG_STATUS_PRINT_PERIOD_MS
#define UPS_DEBUG_STATUS_PRINT_PERIOD_MS 10000U
#endif

#ifndef UPS_LED_BUSY_BLINK_PERIOD_MS
#define UPS_LED_BUSY_BLINK_PERIOD_MS 80U
#endif

#ifndef UPS_BOOTSTRAP_HEARTBEAT_RX_BUF_SIZE
#define UPS_BOOTSTRAP_HEARTBEAT_RX_BUF_SIZE 16U
#endif

#define UPS_DYNAMIC_UPDATE_PERIOD_MS ((uint32_t)(UPS_DYNAMIC_UPDATE_PERIOD_S) * 1000U)
#define UPS_INIT_RETRY_PERIOD_MS ((uint32_t)(UPS_INIT_RETRY_PERIOD_S) * 1000U)

#if (UPS_DEBUG_STATUS_PRINT_ENABLED != 0)
#define UPS_DEBUG_PRINTF(...) printf(__VA_ARGS__)
#else
#define UPS_DEBUG_PRINTF(...)
#endif

static bool s_usb_dp_held_low = false;

static void usb_dp_hold_low(bool hold)
{
#if (USB_HOLD_DP_LOW_UNTIL_USB_START != 0)
    GPIO_InitTypeDef gpio = {0};
    gpio.Pin = GPIO_PIN_12; // PA12 = USB D+

    if (hold)
    {
        gpio.Mode = GPIO_MODE_OUTPUT_OD;
        gpio.Speed = GPIO_SPEED_FREQ_LOW;
        HAL_GPIO_Init(GPIOA, &gpio);
        HAL_GPIO_WritePin(GPIOA, GPIO_PIN_12, GPIO_PIN_RESET);
        s_usb_dp_held_low = true;
    }
    else
    {
        // Release to USB peripheral. Floating input avoids fighting the pull-up.
        gpio.Mode = GPIO_MODE_INPUT;
        gpio.Pull = GPIO_NOPULL;
        HAL_GPIO_Init(GPIOA, &gpio);
        s_usb_dp_held_low = false;
    }
#else
    (void)hold;
#endif
}

static void usb_start_if_enabled(void)
{
    if (s_usb_started || !g_usb_init_enabled)
    {
        return;
    }

#if (USB_HOLD_DP_LOW_UNTIL_USB_START != 0)
    // Keep D+ low during init, then release after stack is ready.
    bool const release_dp_after_init = s_usb_dp_held_low;
#else
    bool const release_dp_after_init = false;
#endif

    MX_USB_PCD_Init();
    tusb_init();

    if (release_dp_after_init)
    {
        usb_dp_hold_low(false);
        // Give the host a moment to detect attach before first control traffic.
        HAL_Delay(5);
    }

    (void)tud_connect();
    s_usb_started = true;
}

typedef enum
{
    UPS_SUB_ADAPTER_SPM2K = 0,
} ups_sub_adapter_t;

#ifndef UPS_ACTIVE_SUB_ADAPTER
#define UPS_ACTIVE_SUB_ADAPTER UPS_SUB_ADAPTER_SPM2K
#endif

typedef enum
{
    UPS_BOOTSTRAP_ENQUEUE_HEARTBEAT = 0,
    UPS_BOOTSTRAP_WAIT_HEARTBEAT_DRAIN,
    UPS_BOOTSTRAP_HEARTBEAT_VERIFY,
    UPS_BOOTSTRAP_WAIT_RETRY,
    UPS_BOOTSTRAP_ENQUEUE_CONSTANT,
    UPS_BOOTSTRAP_ENQUEUE_DYNAMIC,
    UPS_BOOTSTRAP_WAIT_DRAIN,
    UPS_BOOTSTRAP_SANITY_CHECK,
    UPS_BOOTSTRAP_DONE,
} ups_bootstrap_state_t;

static const uart_engine_request_t *g_sub_adapter_constant_lut = NULL;
static size_t g_sub_adapter_constant_lut_count = 0U;
static const uart_engine_request_t *g_sub_adapter_dynamic_lut = NULL;
static size_t g_sub_adapter_dynamic_lut_count = 0U;
static const uart_engine_request_t *g_sub_adapter_constant_heartbeat = NULL;
static const uint8_t *g_sub_adapter_constant_heartbeat_expect_return = NULL;
static size_t g_sub_adapter_constant_heartbeat_expect_return_len = 0U;

static ups_bootstrap_state_t s_ups_bootstrap_state = UPS_BOOTSTRAP_ENQUEUE_HEARTBEAT;
static size_t s_bootstrap_constant_idx = 0U;
static size_t s_bootstrap_dynamic_idx = 0U;
static uint32_t s_init_retry_not_before_ms = 0U;
static uint32_t s_init_bootstrap_start_ms = 0U;
static bool s_init_bootstrap_started = false;
static uint32_t s_last_dynamic_cycle_start_ms = 0U;

static uint8_t s_bootstrap_heartbeat_rx[UPS_BOOTSTRAP_HEARTBEAT_RX_BUF_SIZE];
static uint16_t s_bootstrap_heartbeat_rx_len = 0U;
static bool s_bootstrap_heartbeat_done = false;

static bool s_dynamic_update_cycle_active = false;
static size_t s_dynamic_update_idx = 0U;
static uint32_t s_next_dynamic_update_ms = 0U;

static void ups_sub_adapter_select(void)
{
    switch ((ups_sub_adapter_t)UPS_ACTIVE_SUB_ADAPTER)
    {
    case UPS_SUB_ADAPTER_SPM2K:
        g_sub_adapter_constant_lut = g_spm2k_constant_lut;
        g_sub_adapter_constant_lut_count = g_spm2k_constant_lut_count;
        g_sub_adapter_dynamic_lut = g_spm2k_dynamic_lut;
        g_sub_adapter_dynamic_lut_count = g_spm2k_dynamic_lut_count;
        g_sub_adapter_constant_heartbeat = &g_spm2k_constant_heartbeat;
        g_sub_adapter_constant_heartbeat_expect_return = g_spm2k_constant_heartbeat_expect_return;
        g_sub_adapter_constant_heartbeat_expect_return_len = g_spm2k_constant_heartbeat_expect_return_len;
        break;
    default:
        g_sub_adapter_constant_lut = NULL;
        g_sub_adapter_constant_lut_count = 0U;
        g_sub_adapter_dynamic_lut = NULL;
        g_sub_adapter_dynamic_lut_count = 0U;
        g_sub_adapter_constant_heartbeat = NULL;
        g_sub_adapter_constant_heartbeat_expect_return = NULL;
        g_sub_adapter_constant_heartbeat_expect_return_len = 0U;
        break;
    }
}

static bool ups_bootstrap_heartbeat_capture(uint16_t cmd,
                                            const uint8_t *rx,
                                            uint16_t rx_len,
                                            void *out_value)
{
    (void)cmd;
    (void)out_value;

    s_bootstrap_heartbeat_done = false;
    s_bootstrap_heartbeat_rx_len = 0U;

    if (rx == NULL)
    {
        return false;
    }

    if (rx_len > (uint16_t)sizeof(s_bootstrap_heartbeat_rx))
    {
        return false;
    }

    memcpy(s_bootstrap_heartbeat_rx, rx, rx_len);
    s_bootstrap_heartbeat_rx_len = rx_len;
    s_bootstrap_heartbeat_done = true;
    return true;
}

static bool ups_bootstrap_heartbeat_matches_expected(void)
{
    if (!s_bootstrap_heartbeat_done ||
        (g_sub_adapter_constant_heartbeat_expect_return == NULL) ||
        (g_sub_adapter_constant_heartbeat_expect_return_len == 0U))
    {
        return false;
    }

    if (s_bootstrap_heartbeat_rx_len != g_sub_adapter_constant_heartbeat_expect_return_len)
    {
        return false;
    }

    return (memcmp(s_bootstrap_heartbeat_rx,
                   g_sub_adapter_constant_heartbeat_expect_return,
                   s_bootstrap_heartbeat_rx_len) == 0);
}

static void ups_bootstrap_reset_for_retry(uint32_t now_ms)
{
    s_bootstrap_constant_idx = 0U;
    s_bootstrap_dynamic_idx = 0U;
    s_bootstrap_heartbeat_rx_len = 0U;
    s_bootstrap_heartbeat_done = false;
    s_init_retry_not_before_ms = now_ms + UPS_INIT_RETRY_PERIOD_MS;
    s_ups_bootstrap_state = UPS_BOOTSTRAP_WAIT_RETRY;
}

static void ups_enqueue_full_lut_step(const uart_engine_request_t *lut,
                                      size_t lut_count,
                                      size_t *inout_index)
{
    if ((lut == NULL) || (inout_index == NULL))
    {
        return;
    }

    if (*inout_index >= lut_count)
    {
        return;
    }

    uart_engine_result_t const result = uart_engine_enqueue(&lut[*inout_index]);
    if (result == UART_ENGINE_OK)
    {
        (*inout_index)++;
    }
}

static void ups_bootstrap_task(void)
{
    uint32_t const now_ms = HAL_GetTick();

    if (!s_init_bootstrap_started)
    {
        s_init_bootstrap_started = true;
        s_init_bootstrap_start_ms = now_ms;
    }

    switch (s_ups_bootstrap_state)
    {
    case UPS_BOOTSTRAP_ENQUEUE_HEARTBEAT:
    {
        if (g_sub_adapter_constant_heartbeat == NULL)
        {
            ups_bootstrap_reset_for_retry(now_ms);
            break;
        }

        uart_engine_request_t hb_req = *g_sub_adapter_constant_heartbeat;
        hb_req.out_value = NULL;
        hb_req.process_fn = ups_bootstrap_heartbeat_capture;

        uart_engine_result_t const result = uart_engine_enqueue(&hb_req);
        if (result == UART_ENGINE_OK)
        {
            s_bootstrap_heartbeat_done = false;
            s_ups_bootstrap_state = UPS_BOOTSTRAP_WAIT_HEARTBEAT_DRAIN;
        }
        break;
    }

    case UPS_BOOTSTRAP_WAIT_HEARTBEAT_DRAIN:
        if (!uart_engine_is_busy())
        {
            s_ups_bootstrap_state = UPS_BOOTSTRAP_HEARTBEAT_VERIFY;
        }
        break;

    case UPS_BOOTSTRAP_HEARTBEAT_VERIFY:
        if (ups_bootstrap_heartbeat_matches_expected())
        {
            s_ups_bootstrap_state = UPS_BOOTSTRAP_ENQUEUE_CONSTANT;
        }
        else
        {
            UPS_DEBUG_PRINTF("INIT heartbeat failed, retry in %lu ms\r\n",
                             (unsigned long)UPS_INIT_RETRY_PERIOD_MS);
            ups_bootstrap_reset_for_retry(now_ms);
        }
        break;

    case UPS_BOOTSTRAP_WAIT_RETRY:
        if ((int32_t)(now_ms - s_init_retry_not_before_ms) >= 0)
        {
            s_ups_bootstrap_state = UPS_BOOTSTRAP_ENQUEUE_HEARTBEAT;
        }
        break;

    case UPS_BOOTSTRAP_ENQUEUE_CONSTANT:
        ups_enqueue_full_lut_step(g_sub_adapter_constant_lut,
                                  g_sub_adapter_constant_lut_count,
                                  &s_bootstrap_constant_idx);
        if (s_bootstrap_constant_idx >= g_sub_adapter_constant_lut_count)
        {
            s_ups_bootstrap_state = UPS_BOOTSTRAP_ENQUEUE_DYNAMIC;
        }
        break;

    case UPS_BOOTSTRAP_ENQUEUE_DYNAMIC:
        ups_enqueue_full_lut_step(g_sub_adapter_dynamic_lut,
                                  g_sub_adapter_dynamic_lut_count,
                                  &s_bootstrap_dynamic_idx);
        if (s_bootstrap_dynamic_idx >= g_sub_adapter_dynamic_lut_count)
        {
            s_ups_bootstrap_state = UPS_BOOTSTRAP_WAIT_DRAIN;
        }
        break;

    case UPS_BOOTSTRAP_WAIT_DRAIN:
        if (!uart_engine_is_busy())
        {
            s_ups_bootstrap_state = UPS_BOOTSTRAP_SANITY_CHECK;
        }
        break;

    case UPS_BOOTSTRAP_SANITY_CHECK:
        if (g_battery.remaining_capacity > 0U)
        {
            g_usb_init_enabled = true;
            s_next_dynamic_update_ms = HAL_GetTick() + UPS_DYNAMIC_UPDATE_PERIOD_MS;
            s_ups_bootstrap_state = UPS_BOOTSTRAP_DONE;
            UPS_DEBUG_PRINTF("INIT full bootstrap done in %lu ms\r\n",
                             (unsigned long)(now_ms - s_init_bootstrap_start_ms));
        }
        else
        {
            UPS_DEBUG_PRINTF("INIT sanity failed (remaining_capacity=0), retry in %lu ms\r\n",
                             (unsigned long)UPS_INIT_RETRY_PERIOD_MS);
            ups_bootstrap_reset_for_retry(now_ms);
        }
        break;

    case UPS_BOOTSTRAP_DONE:
    default:
        break;
    }
}

static void ups_dynamic_update_task(void)
{
    if (s_ups_bootstrap_state != UPS_BOOTSTRAP_DONE)
    {
        return;
    }

    uint32_t const now_ms = HAL_GetTick();
    if (!s_dynamic_update_cycle_active)
    {
        if ((int32_t)(now_ms - s_next_dynamic_update_ms) < 0)
        {
            return;
        }

        s_dynamic_update_cycle_active = true;
        s_dynamic_update_idx = 0U;
        s_last_dynamic_cycle_start_ms = now_ms;
    }

    if (s_dynamic_update_idx < g_sub_adapter_dynamic_lut_count)
    {
        ups_enqueue_full_lut_step(g_sub_adapter_dynamic_lut,
                                  g_sub_adapter_dynamic_lut_count,
                                  &s_dynamic_update_idx);
        return;
    }

    if (uart_engine_is_busy())
    {
        return;
    }

    s_dynamic_update_cycle_active = false;
    s_next_dynamic_update_ms = now_ms + UPS_DYNAMIC_UPDATE_PERIOD_MS;
    UPS_DEBUG_PRINTF("DYN refresh done in %lu ms\r\n",
                     (unsigned long)(now_ms - s_last_dynamic_cycle_start_ms));
}

static void ups_debug_status_print_task(void)
{
#if (UPS_DEBUG_STATUS_PRINT_ENABLED != 0)
    static uint32_t next_print_ms = 0U;
    uint32_t const now_ms = HAL_GetTick();
    if ((int32_t)(now_ms - next_print_ms) < 0)
    {
        return;
    }

    next_print_ms = now_ms + UPS_DEBUG_STATUS_PRINT_PERIOD_MS;

    printf("PS: ac=%u chg=%u dis=%u full=%u repl=%u low=%u bpres=%u ovl=%u shut=%u\r\n",
           (unsigned)g_power_summary_present_status.ac_present,
           (unsigned)g_power_summary_present_status.charging,
           (unsigned)g_power_summary_present_status.discharging,
           (unsigned)g_power_summary_present_status.fully_charged,
           (unsigned)g_power_summary_present_status.need_replacement,
           (unsigned)g_power_summary_present_status.below_remaining_capacity_limit,
           (unsigned)g_power_summary_present_status.battery_present,
           (unsigned)g_power_summary_present_status.overload,
           (unsigned)g_power_summary_present_status.shutdown_imminent);

    printf("BAT: cap=%u rt=%u rtl=%u vb=%u ib=%d cfgv=%u temp=%u mfg=%u\r\n",
           (unsigned)g_battery.remaining_capacity,
           (unsigned)g_battery.run_time_to_empty_s,
           (unsigned)g_battery.remaining_time_limit_s,
           (unsigned)g_battery.battery_voltage,
           (int)g_battery.battery_current,
           (unsigned)g_battery.config_voltage,
           (unsigned)g_battery.temperature,
           (unsigned)g_battery.manufacturer_date);

    printf("IN: v=%u f=%u cfgv=%u low=%u high=%u\r\n",
           (unsigned)g_input.voltage,
           (unsigned)g_input.frequency,
           (unsigned)g_input.config_voltage,
           (unsigned)g_input.low_voltage_transfer,
           (unsigned)g_input.high_voltage_transfer);

    printf("OUT: load=%u cfgp=%u cfgv=%u v=%u i=%d f=%u\r\n",
           (unsigned)g_output.percent_load,
           (unsigned)g_output.config_active_power,
           (unsigned)g_output.config_voltage,
           (unsigned)g_output.voltage,
           (int)g_output.current,
           (unsigned)g_output.frequency);
#endif
}

static void ups_led_task(void)
{
    static bool led_state_low = true;
    static uint32_t next_toggle_ms = 0U;

    if (!uart_engine_is_enabled())
    {
        led_state_low = true;
        HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_RESET);
        next_toggle_ms = 0U;
        return;
    }

    if (!uart_engine_is_busy())
    {
        led_state_low = true;
        HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_RESET);
        next_toggle_ms = 0U;
        return;
    }

    uint32_t const now_ms = HAL_GetTick();
    if (next_toggle_ms == 0U)
    {
        next_toggle_ms = now_ms;
    }

    if ((int32_t)(now_ms - next_toggle_ms) < 0)
    {
        return;
    }

    led_state_low = !led_state_low;
    HAL_GPIO_WritePin(GPIOC,
                      GPIO_PIN_13,
                      led_state_low ? GPIO_PIN_RESET : GPIO_PIN_SET);
    next_toggle_ms = now_ms + UPS_LED_BUSY_BLINK_PERIOD_MS;
}

// UART engine enable switch.
// Set UART_ENGINE_DEFAULT_ENABLED to 0 to compile with UART polling disabled.
#ifndef UART_ENGINE_DEFAULT_ENABLED

#define UART_ENGINE_DEFAULT_ENABLED 1
#endif

static bool s_uart_engine_enabled = (UART_ENGINE_DEFAULT_ENABLED != 0);

ups_present_status_t g_power_summary_present_status = {
    .ac_present = false,
    .charging = false,
    .discharging = false,
    .fully_charged = false,
    .need_replacement = false,
    .below_remaining_capacity_limit = false,
    .battery_present = false,
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
    .battery_voltage = 0,
    .battery_current = 0,
    .config_voltage = 0,
    .run_time_to_empty_s = 0,
    .remaining_time_limit_s = 0,
    .temperature = 0,
    .manufacturer_date = 0,
    .remaining_capacity = 0,
};

ups_input_t g_input = {
    .voltage = 0,
    .frequency = 0,
    .config_voltage = 0,
    .low_voltage_transfer = 0,
    .high_voltage_transfer = 0,
};
ups_output_t g_output = {
    .percent_load = 0,
    .config_active_power = 0,
    .config_voltage = 0,
    .voltage = 0,
    .current = 0,
    .frequency = 0,
};

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

#if (USB_HOLD_DP_LOW_UNTIL_USB_START != 0)
    // Prevent early attach until we deliberately start USB.
    if (!g_usb_init_enabled)
    {
        usb_dp_hold_low(true);
    }
#endif

    MX_USART1_UART_Init();
    MX_USART2_UART_Init();
    UART2_RxStartIT();
    uart_engine_init();
    uart_engine_set_enabled(s_uart_engine_enabled);
    ups_sub_adapter_select();

    /* Infinite loop */
    /* USER CODE BEGIN WHILE */
    while (1)
    {
        /* USER CODE END WHILE */

        usb_start_if_enabled();
        if (s_usb_started) {
            tud_task(); // TinyUSB device task
            ups_hid_periodic_task();

        }
        ups_bootstrap_task();
        ups_dynamic_update_task();
        ups_debug_status_print_task();
        ups_led_task();
        uart_engine_tick();
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
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_RESET);

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
