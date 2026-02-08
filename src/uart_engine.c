/**
 * @file uart_engine.c
 * @brief Non-blocking UART request/response engine.
 *
 * Implements a cooperative state machine around the UART2_* adapter functions
 * (DMA TX + buffered RX). Requests are queued and executed sequentially; each
 * request can optionally be retried on failure.
 *
 * A periodic heartbeat can be configured via uart_engine_set_heartbeat() to
 * monitor link/UPS health and trigger a conservative "battery unknown" state
 * after repeated failures.
 *
 * Call uart_engine_tick() frequently from the main loop.
 */

#include "uart_engine.h"

#include "main.h"

#include <string.h>

#ifndef UART_ENGINE_QUEUE_SIZE
#define UART_ENGINE_QUEUE_SIZE 32U
#endif

#ifndef UART_ENGINE_MAX_EXPECTED_LEN
#define UART_ENGINE_MAX_EXPECTED_LEN 256U
#endif

#ifndef UART_ENGINE_TX_TIMEOUT_MS
#define UART_ENGINE_TX_TIMEOUT_MS 250U
#endif

#ifndef UART_ENGINE_RETRY_COOLDOWN_MS
#define UART_ENGINE_RETRY_COOLDOWN_MS 25U
#endif

typedef enum
{
    UART_ENGINE_STATE_IDLE = 0,
    UART_ENGINE_STATE_TX_START,
    UART_ENGINE_STATE_TX_WAIT,
    UART_ENGINE_STATE_RX_WAIT,
    UART_ENGINE_STATE_PROCESS,
} uart_engine_state_t;

typedef struct
{
    uart_engine_request_t req;
    uint8_t retries_left;
    bool in_use;
    bool is_heartbeat;
} uart_engine_job_t;

static uart_engine_job_t s_queue[UART_ENGINE_QUEUE_SIZE];
static uint8_t s_q_head;
static uint8_t s_q_tail;
static uint8_t s_q_count;

static uart_engine_job_t s_active;
static uart_engine_state_t s_state;
static uint32_t s_state_start_ms;
static uint32_t s_retry_not_before_ms;

static uint8_t s_rx_buf[UART_ENGINE_MAX_EXPECTED_LEN];
static uint16_t s_rx_got;
// DMA TX must use storage that outlives job_start_tx(); a stack buffer can be
// overwritten before transfer completes, corrupting multi-byte commands.
static uint8_t s_tx_buf[8U];

static bool s_hb_enabled;
static uart_engine_heartbeat_cfg_t s_hb_cfg;
static uint32_t s_hb_next_due_ms;
static uint8_t s_hb_consecutive_failures;
static bool s_hb_queued_or_active;

static bool s_enabled;

static void set_not_before_ms(uint32_t candidate_ms)
{
    if ((int32_t)(candidate_ms - s_retry_not_before_ms) > 0)
    {
        s_retry_not_before_ms = candidate_ms;
    }
}

static void apply_interjob_cooldown(uint32_t now_ms)
{
#if (UART_ENGINE_INTERJOB_COOLDOWN_MS > 0U)
    set_not_before_ms(now_ms + UART_ENGINE_INTERJOB_COOLDOWN_MS);
#else
    (void)now_ms;
#endif
}

static void uart_engine_debug_print_raw_rx(const char *reason, const uint8_t *rx, uint16_t rx_len)
{
    if (!g_ups_debug_status_print_enabled)
    {
        return;
    }

    printf("UART_ENG raw rx: %s len=%u",
           (reason != NULL) ? reason : "unknown",
           (unsigned int)rx_len);

    if ((rx == NULL) || (rx_len == 0U))
    {
        printf(" (empty)\r\n");
        return;
    }

    printf(" data=");
    for (uint16_t i = 0U; i < rx_len; i++)
    {
        printf("%02X", rx[i]);
        if ((uint16_t)(i + 1U) < rx_len)
        {
            printf(" ");
        }
    }
    printf("\r\n");
}

static void uart_engine_debug_print_retry(const uart_engine_job_t *job, const char *reason)
{
    if (!g_ups_debug_status_print_enabled || (job == NULL))
    {
        return;
    }

    printf("UART_ENG retry: %s cmd=0x%04X hb=%u retries_left=%u q=%u\r\n",
           (reason != NULL) ? reason : "unknown",
           (unsigned int)job->req.cmd,
           job->is_heartbeat ? 1U : 0U,
           (unsigned int)job->retries_left,
           (unsigned int)s_q_count);

    uart_engine_debug_print_raw_rx("retry", s_rx_buf, s_rx_got);
}

static void uart_engine_debug_print_failure(const uart_engine_job_t *job, const char *reason)
{
    if (!g_ups_debug_status_print_enabled || (job == NULL))
    {
        return;
    }

    printf("UART_ENG failure: %s cmd=0x%04X hb=%u retries_left=%u q=%u\r\n",
           (reason != NULL) ? reason : "unknown",
           (unsigned int)job->req.cmd,
           job->is_heartbeat ? 1U : 0U,
           (unsigned int)job->retries_left,
           (unsigned int)s_q_count);
}

static void uart_engine_debug_print_timeout(const uart_engine_job_t *job,
                                            const char *phase,
                                            uint32_t elapsed_ms,
                                            uint32_t timeout_ms)
{
    if (!g_ups_debug_status_print_enabled || (job == NULL))
    {
        return;
    }

    printf("UART_ENG timeout: %s cmd=0x%04X hb=%u elapsed=%lu timeout=%lu retries_left=%u\r\n",
           (phase != NULL) ? phase : "unknown",
           (unsigned int)job->req.cmd,
           job->is_heartbeat ? 1U : 0U,
           (unsigned long)elapsed_ms,
           (unsigned long)timeout_ms,
           (unsigned int)job->retries_left);
}

static void uart_engine_debug_print_enqueue_failure(const char *reason, const uart_engine_request_t *req)
{
    if (!g_ups_debug_status_print_enabled)
    {
        return;
    }

    if (req == NULL)
    {
        printf("UART_ENG enqueue failure: %s req=null q=%u\r\n",
               (reason != NULL) ? reason : "unknown",
               (unsigned int)s_q_count);
        uart_engine_debug_print_raw_rx("enqueue failure", s_rx_buf, s_rx_got);
        return;
    }

    printf("UART_ENG enqueue failure: %s cmd=0x%04X q=%u\r\n",
           (reason != NULL) ? reason : "unknown",
           (unsigned int)req->cmd,
           (unsigned int)s_q_count);
    uart_engine_debug_print_raw_rx("enqueue failure", s_rx_buf, s_rx_got);
}

static uint32_t tick_now_ms(void)
{
    return HAL_GetTick();
}

static bool queue_is_full(void)
{
    return (s_q_count >= UART_ENGINE_QUEUE_SIZE);
}

static bool queue_push(const uart_engine_request_t *req, bool is_heartbeat)
{
    if (queue_is_full())
    {
        return false;
    }

    uart_engine_job_t *slot = &s_queue[s_q_tail];
    slot->req = *req;
    slot->retries_left = req->max_retries;
    slot->in_use = true;
    slot->is_heartbeat = is_heartbeat;

    s_q_tail = (uint8_t)((s_q_tail + 1U) % UART_ENGINE_QUEUE_SIZE);
    s_q_count++;
    return true;
}

static bool queue_pop(uart_engine_job_t *out)
{
    if ((out == NULL) || (s_q_count == 0U))
    {
        return false;
    }

    uart_engine_job_t *slot = &s_queue[s_q_head];
    *out = *slot;
    slot->in_use = false;

    s_q_head = (uint8_t)((s_q_head + 1U) % UART_ENGINE_QUEUE_SIZE);
    s_q_count--;
    return true;
}

static uint16_t build_cmd_bytes(uint8_t *tx, uint16_t tx_cap, uint16_t cmd, uint8_t cmd_bits)
{
    if ((tx == NULL) || (tx_cap == 0U))
    {
        return 0U;
    }

    uint16_t used = 0U;

    if (cmd_bits == 8U)
    {
        if (tx_cap < 1U)
        {
            return 0U;
        }
        tx[used++] = (uint8_t)(cmd & 0xFFU);
    }
    else if (cmd_bits == 16U)
    {
        if (tx_cap < 2U)
        {
            return 0U;
        }
        // MSB then LSB (works well for 2-char ASCII commands like 0x5131 => 'Q''1').
        tx[used++] = (uint8_t)((cmd >> 8) & 0xFFU);
        tx[used++] = (uint8_t)(cmd & 0xFFU);
    }
    else
    {
        return 0U;
    }

    return used;
}

static bool request_is_valid(const uart_engine_request_t *req)
{
    if (req == NULL)
    {
        return false;
    }

    if ((req->cmd_bits != 8U) && (req->cmd_bits != 16U))
    {
        return false;
    }

    if (req->expected_len > UART_ENGINE_MAX_EXPECTED_LEN)
    {
        return false;
    }

    if (req->expected_ending)
    {
        if ((req->expected_ending_len == 0U) || (req->expected_ending_len > UART_ENGINE_MAX_ENDING_LEN))
        {
            return false;
        }
    }

    return true;
}

// Determine how many bytes the engine should read for the active request.
// For fixed-length responses it returns the expected length; when an ending
// sequence is tracked, it defaults to the configured max buffer size if no
// explicit expected length was provided so the terminator detection still has
// room to read.
static uint16_t request_rx_cap(const uart_engine_request_t *req)
{
    if (req == NULL)
    {
        return 0U;
    }

    if (!req->expected_ending)
    {
        return req->expected_len;
    }

    if (req->expected_len == 0U)
    {
        return UART_ENGINE_MAX_EXPECTED_LEN;
    }

    return req->expected_len;
}

static bool rx_has_expected_ending(const uart_engine_request_t *req, const uint8_t *rx, uint16_t rx_len)
{
    if ((req == NULL) || !req->expected_ending)
    {
        return false;
    }

    uint8_t const ending_len = req->expected_ending_len;
    if ((ending_len == 0U) || (ending_len > UART_ENGINE_MAX_ENDING_LEN) || (rx_len < ending_len))
    {
        return false;
    }

    if (rx == NULL)
    {
        return false;
    }

    return (memcmp(&rx[rx_len - ending_len], req->expected_ending_bytes, ending_len) == 0);
}

static void active_clear(void)
{
    (void)memset(&s_active, 0, sizeof(s_active));
    s_rx_got = 0U;
}

static void on_job_success(const uart_engine_job_t *job)
{
    if ((job != NULL) && job->is_heartbeat)
    {
        s_hb_consecutive_failures = 0U;
    }
}

static void on_job_final_failure(const uart_engine_job_t *job)
{
    if ((job != NULL) && job->is_heartbeat)
    {
        if (s_hb_consecutive_failures < 255U)
        {
            s_hb_consecutive_failures++;
        }

        uint8_t threshold = s_hb_cfg.failure_threshold;
        if (threshold == 0U)
        {
            threshold = 5U;
        }

        if (s_hb_consecutive_failures >= threshold)
        {
            g_battery.remaining_capacity = 0U;
            g_battery.remaining_time_limit_s = 0U;
        }
    }
}

/**
 * @brief Initialize the UART engine runtime state.
 *
 * Resets the internal queue/state machine and enables the engine.
 */
void uart_engine_init(void)
{
    s_q_head = 0U;
    s_q_tail = 0U;
    s_q_count = 0U;
    s_state = UART_ENGINE_STATE_IDLE;
    s_state_start_ms = 0U;
    s_retry_not_before_ms = 0U;

    s_hb_enabled = false;
    (void)memset(&s_hb_cfg, 0, sizeof(s_hb_cfg));
    s_hb_next_due_ms = 0U;
    s_hb_consecutive_failures = 0U;
    s_hb_queued_or_active = false;

    s_enabled = true;

    active_clear();
}

static void uart_engine_reset_internal(void)
{
    s_q_head = 0U;
    s_q_tail = 0U;
    s_q_count = 0U;
    s_state = UART_ENGINE_STATE_IDLE;
    s_state_start_ms = 0U;
    s_retry_not_before_ms = 0U;

    s_hb_enabled = false;
    (void)memset(&s_hb_cfg, 0, sizeof(s_hb_cfg));
    s_hb_next_due_ms = 0U;
    s_hb_consecutive_failures = 0U;
    s_hb_queued_or_active = false;

    active_clear();

    // Ensure we don't leave the UART locked if the engine was disabled mid-job.
    UART2_Unlock();
}

/**
 * @brief Enable or disable the UART engine.
 *
 * When disabling, queued/active jobs are dropped, heartbeat scheduling is
 * stopped, and the UART lock is released.
 *
 * @param enable true to enable, false to disable.
 */
void uart_engine_set_enabled(bool enable)
{
    if (enable == s_enabled)
    {
        return;
    }

    s_enabled = enable;
    if (!s_enabled)
    {
        uart_engine_reset_internal();
    }
}

/**
 * @brief Get whether the engine is enabled.
 * @return true if enabled; false if disabled.
 */
bool uart_engine_is_enabled(void)
{
    return s_enabled;
}

bool uart_engine_is_busy(void)
{
    return (s_state != UART_ENGINE_STATE_IDLE) || (s_q_count != 0U);
}

/**
 * @brief Enqueue a UART request for execution by uart_engine_tick().
 * @param req Request descriptor (command, expected length, timeout, callback).
 * @return Result code indicating success or why the enqueue failed.
 */
uart_engine_result_t uart_engine_enqueue(const uart_engine_request_t *req)
{
    if (!s_enabled)
    {
        uart_engine_debug_print_enqueue_failure("engine disabled", req);
        return UART_ENGINE_ERR_DISABLED;
    }
    if (!request_is_valid(req))
    {
        uart_engine_debug_print_enqueue_failure("bad request", req);
        return UART_ENGINE_ERR_BAD_PARAM;
    }

    if (!queue_push(req, false))
    {
        uart_engine_debug_print_enqueue_failure("queue full", req);
        return UART_ENGINE_ERR_QUEUE_FULL;
    }
    return UART_ENGINE_OK;
}

/**
 * @brief Configure or disable the periodic heartbeat request.
 * @param cfg Heartbeat configuration. Pass NULL to disable.
 */
void uart_engine_set_heartbeat(const uart_engine_heartbeat_cfg_t *cfg)
{
    if (!s_enabled)
    {
        return;
    }
    if (cfg == NULL)
    {
        s_hb_enabled = false;
        s_hb_queued_or_active = false;
        s_hb_consecutive_failures = 0U;
        return;
    }

    s_hb_cfg = *cfg;
    if (!request_is_valid(&s_hb_cfg.req))
    {
        s_hb_enabled = false;
        return;
    }

    if (s_hb_cfg.failure_threshold == 0U)
    {
        s_hb_cfg.failure_threshold = 5U;
    }

    s_hb_enabled = true;
    s_hb_next_due_ms = tick_now_ms();
    s_hb_consecutive_failures = 0U;
    s_hb_queued_or_active = false;
}

/**
 * @brief Helper process function that checks for an exact byte-for-byte match.
 *
 * out_value must point to a uart_engine_expect_bytes_t.
 */
bool uart_engine_process_expect_exact(uint16_t cmd, const uint8_t *rx, uint16_t rx_len, void *out_value)
{
    (void)cmd;
    const uart_engine_expect_bytes_t *exp = (const uart_engine_expect_bytes_t *)out_value;
    if ((exp == NULL) || (exp->expected == NULL))
    {
        return false;
    }
    if (rx_len != exp->expected_len)
    {
        return false;
    }
    if ((rx == NULL) && (rx_len != 0U))
    {
        return false;
    }

    return (memcmp(rx, exp->expected, rx_len) == 0);
}

static void maybe_enqueue_heartbeat(uint32_t now_ms)
{
    if (!s_hb_enabled)
    {
        return;
    }

    if (s_hb_queued_or_active)
    {
        return;
    }

    if ((int32_t)(now_ms - s_hb_next_due_ms) < 0)
    {
        return;
    }

    if (queue_is_full())
    {
        if (g_ups_debug_status_print_enabled)
        {
            printf("UART_ENG failure: heartbeat enqueue queue full q=%u\r\n",
                   (unsigned int)s_q_count);
        }
        return;
    }

    if (queue_push(&s_hb_cfg.req, true))
    {
        s_hb_queued_or_active = true;
        uint32_t interval = s_hb_cfg.interval_ms;
        if (interval == 0U)
        {
            interval = 1000U;
        }
        s_hb_next_due_ms = now_ms + interval;
    }
}

static void job_start_tx(uint32_t now_ms)
{
    // Build command bytes into persistent buffer for asynchronous DMA send.
    uint16_t tx_len = build_cmd_bytes(s_tx_buf,
                                      (uint16_t)sizeof(s_tx_buf),
                                      s_active.req.cmd,
                                      s_active.req.cmd_bits);
    if (tx_len == 0U)
    {
        s_state = UART_ENGINE_STATE_IDLE;
        apply_interjob_cooldown(now_ms);
        UART2_Unlock();
        uart_engine_debug_print_failure(&s_active, "build tx command bytes failed");
        on_job_final_failure(&s_active);
        if (s_active.is_heartbeat)
        {
            s_hb_queued_or_active = false;
        }
        active_clear();
        return;
    }

    UART2_DiscardBuffered();
    UART2_TxDoneClear();

    UPS_DebugPrintTxCommand(s_tx_buf, tx_len);

    HAL_StatusTypeDef st = UART2_SendBytesDMA(s_tx_buf, tx_len);
    if (st == HAL_OK)
    {
        s_state = UART_ENGINE_STATE_TX_WAIT;
        s_state_start_ms = now_ms;
        return;
    }

    // Busy/error: treat as a failure and retry.
    UART2_Unlock();

    if (s_active.retries_left > 0U)
    {
        s_active.retries_left--;
        if (queue_push(&s_active.req, s_active.is_heartbeat))
        {
            uart_engine_debug_print_retry(&s_active, "tx dma start failed");
            s_retry_not_before_ms = now_ms + UART_ENGINE_RETRY_COOLDOWN_MS;
        }
        else
        {
            uart_engine_debug_print_failure(&s_active, "tx dma start failed and retry enqueue failed");
            on_job_final_failure(&s_active);
            if (s_active.is_heartbeat)
            {
                s_hb_queued_or_active = false;
            }
        }
    }
    else
    {
        uart_engine_debug_print_failure(&s_active, "tx dma start failed no retries left");
        on_job_final_failure(&s_active);
        if (s_active.is_heartbeat)
        {
            s_hb_queued_or_active = false;
        }
    }

    s_state = UART_ENGINE_STATE_IDLE;
    apply_interjob_cooldown(now_ms);
    active_clear();
}

static void job_fail_and_maybe_retry(uint32_t now_ms, const char *reason)
{
    UART2_Unlock();

    if (s_active.retries_left > 0U)
    {
        s_active.retries_left--;
        if (queue_push(&s_active.req, s_active.is_heartbeat))
        {
            uart_engine_debug_print_retry(&s_active, reason);
            s_retry_not_before_ms = now_ms + UART_ENGINE_RETRY_COOLDOWN_MS;
        }
        else
        {
            uart_engine_debug_print_failure(&s_active, "retry enqueue failed");
            on_job_final_failure(&s_active);
            if (s_active.is_heartbeat)
            {
                s_hb_queued_or_active = false;
            }
        }
    }
    else
    {
        uart_engine_debug_print_failure(&s_active, reason);
        on_job_final_failure(&s_active);
        if (s_active.is_heartbeat)
        {
            s_hb_queued_or_active = false;
        }
    }

    s_state = UART_ENGINE_STATE_IDLE;
    apply_interjob_cooldown(now_ms);
    active_clear();
}

/**
 * @brief Advance the UART engine state machine.
 *
 * Call frequently (e.g. each main loop iteration). This function is
 * non-blocking and will return quickly.
 */
void uart_engine_tick(void)
{
    if (!s_enabled)
    {
        return;
    }
    uint32_t const now_ms = tick_now_ms();

    maybe_enqueue_heartbeat(now_ms);

    if ((int32_t)(now_ms - s_retry_not_before_ms) < 0)
    {
        return;
    }

    switch (s_state)
    {
    case UART_ENGINE_STATE_IDLE:
    {
        if (s_q_count == 0U)
        {
            return;
        }

        if (!UART2_TryLock())
        {
            return;
        }

        uart_engine_job_t job;
        if (!queue_pop(&job))
        {
            UART2_Unlock();
            return;
        }

        s_active = job;
        s_state = UART_ENGINE_STATE_TX_START;
        s_state_start_ms = now_ms;
        if (s_active.is_heartbeat)
        {
            // consumed from queue into active
            s_hb_queued_or_active = true;
        }
        break;
    }

    case UART_ENGINE_STATE_TX_START:
        job_start_tx(now_ms);
        break;

    case UART_ENGINE_STATE_TX_WAIT:
        if (UART2_TxDone())
        {
            s_state = UART_ENGINE_STATE_RX_WAIT;
            s_state_start_ms = now_ms;
            s_rx_got = 0U;
        }
        else if ((now_ms - s_state_start_ms) >= UART_ENGINE_TX_TIMEOUT_MS)
        {
            uart_engine_debug_print_timeout(&s_active,
                                            "tx wait",
                                            (uint32_t)(now_ms - s_state_start_ms),
                                            UART_ENGINE_TX_TIMEOUT_MS);
            job_fail_and_maybe_retry(now_ms, "tx timeout");
        }
        break;

    case UART_ENGINE_STATE_RX_WAIT:
    {
        uint16_t const rx_cap = request_rx_cap(&s_active.req);

        if (rx_cap == 0U)
        {
            s_state = UART_ENGINE_STATE_PROCESS;
            break;
        }

        if (s_rx_got < rx_cap)
        {
            uint16_t want = (uint16_t)(rx_cap - s_rx_got);
            s_rx_got += UART2_Read(&s_rx_buf[s_rx_got], want);
        }

        if (s_active.req.expected_ending)
        {
            if (rx_has_expected_ending(&s_active.req, s_rx_buf, s_rx_got))
            {
                s_state = UART_ENGINE_STATE_PROCESS;
                break;
            }

            if (s_rx_got >= rx_cap)
            {
                uart_engine_debug_print_failure(&s_active, "rx reached cap before ending");
                job_fail_and_maybe_retry(now_ms, "rx ending not found");
                break;
            }
        }
        else if (s_rx_got >= rx_cap)
        {
            s_state = UART_ENGINE_STATE_PROCESS;
            break;
        }

        if ((now_ms - s_state_start_ms) >= s_active.req.timeout_ms)
        {
            uart_engine_debug_print_timeout(&s_active,
                                            "rx wait",
                                            (uint32_t)(now_ms - s_state_start_ms),
                                            s_active.req.timeout_ms);
            job_fail_and_maybe_retry(now_ms, "rx timeout");
        }
        break;
    }

    case UART_ENGINE_STATE_PROCESS:
    {
        bool ok = true;
        if (s_active.req.process_fn != NULL)
        {
            ok = s_active.req.process_fn(s_active.req.cmd, s_rx_buf, s_rx_got, s_active.req.out_value);
        }

        UART2_Unlock();

        if (ok)
        {
            on_job_success(&s_active);
            if (s_active.is_heartbeat)
            {
                s_hb_queued_or_active = false;
            }
            s_state = UART_ENGINE_STATE_IDLE;
            apply_interjob_cooldown(now_ms);
            active_clear();
            return;
        }

        // Parse failed.
        uart_engine_debug_print_raw_rx("process callback returned false", s_rx_buf, s_rx_got);
        if (s_active.retries_left > 0U)
        {
            s_active.retries_left--;
            if (queue_push(&s_active.req, s_active.is_heartbeat))
            {
                uart_engine_debug_print_retry(&s_active, "process callback returned false");
                s_retry_not_before_ms = now_ms + UART_ENGINE_RETRY_COOLDOWN_MS;
            }
            else
            {
                uart_engine_debug_print_failure(&s_active, "parse failed and retry enqueue failed");
                on_job_final_failure(&s_active);
                if (s_active.is_heartbeat)
                {
                    s_hb_queued_or_active = false;
                }
            }
        }
        else
        {
            uart_engine_debug_print_failure(&s_active, "process callback returned false");
            on_job_final_failure(&s_active);
            if (s_active.is_heartbeat)
            {
                s_hb_queued_or_active = false;
            }
        }

        s_state = UART_ENGINE_STATE_IDLE;
        apply_interjob_cooldown(now_ms);
        active_clear();
        break;
    }

    default:
        s_state = UART_ENGINE_STATE_IDLE;
        active_clear();
        break;
    }
}
