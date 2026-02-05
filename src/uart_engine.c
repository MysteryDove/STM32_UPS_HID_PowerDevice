#include "uart_engine.h"

#include "main.h"

#include <string.h>

#ifndef UART_ENGINE_QUEUE_SIZE
#define UART_ENGINE_QUEUE_SIZE 8U
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

static bool s_hb_enabled;
static uart_engine_heartbeat_cfg_t s_hb_cfg;
static uint32_t s_hb_next_due_ms;
static uint8_t s_hb_consecutive_failures;
static bool s_hb_queued_or_active;

static bool s_enabled;

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

static uint16_t build_cmd_bytes(uint8_t *tx, uint16_t tx_cap, uint16_t cmd, uint8_t cmd_bits, uint8_t crlf_count)
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

    while (crlf_count > 0U)
    {
        if ((used + 2U) > tx_cap)
        {
            return 0U;
        }
        tx[used++] = 0x0DU;
        tx[used++] = 0x0AU;
        crlf_count--;
    }

    return used;
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

bool uart_engine_is_enabled(void)
{
    return s_enabled;
}

uart_engine_result_t uart_engine_enqueue(const uart_engine_request_t *req)
{
    if (!s_enabled)
    {
        return UART_ENGINE_ERR_DISABLED;
    }
    if (req == NULL)
    {
        return UART_ENGINE_ERR_BAD_PARAM;
    }
    if ((req->cmd_bits != 8U) && (req->cmd_bits != 16U))
    {
        return UART_ENGINE_ERR_BAD_PARAM;
    }
    if (req->expected_len > UART_ENGINE_MAX_EXPECTED_LEN)
    {
        return UART_ENGINE_ERR_BAD_PARAM;
    }

    if (!queue_push(req, false))
    {
        return UART_ENGINE_ERR_QUEUE_FULL;
    }
    return UART_ENGINE_OK;
}

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
    if ((s_hb_cfg.req.cmd_bits != 8U) && (s_hb_cfg.req.cmd_bits != 16U))
    {
        s_hb_enabled = false;
        return;
    }

    if (s_hb_cfg.req.expected_len > UART_ENGINE_MAX_EXPECTED_LEN)
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

bool uart_engine_process_expect_exact(const uint8_t *rx, uint16_t rx_len, void *out_value, void *user_ctx)
{
    (void)out_value;

    const uart_engine_expect_bytes_t *exp = (const uart_engine_expect_bytes_t *)user_ctx;
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
    uint8_t tx_buf[8];
    uint16_t tx_len = build_cmd_bytes(tx_buf, (uint16_t)sizeof(tx_buf), s_active.req.cmd, s_active.req.cmd_bits,
                                      s_active.req.crlf_count);
    if (tx_len == 0U)
    {
        s_state = UART_ENGINE_STATE_IDLE;
        UART2_Unlock();
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

    HAL_StatusTypeDef st = UART2_SendBytesDMA(tx_buf, tx_len);
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
            s_retry_not_before_ms = now_ms + UART_ENGINE_RETRY_COOLDOWN_MS;
        }
        else
        {
            on_job_final_failure(&s_active);
            if (s_active.is_heartbeat)
            {
                s_hb_queued_or_active = false;
            }
        }
    }
    else
    {
        on_job_final_failure(&s_active);
        if (s_active.is_heartbeat)
        {
            s_hb_queued_or_active = false;
        }
    }

    s_state = UART_ENGINE_STATE_IDLE;
    active_clear();
}

static void job_fail_and_maybe_retry(uint32_t now_ms)
{
    UART2_Unlock();

    if (s_active.retries_left > 0U)
    {
        s_active.retries_left--;
        if (queue_push(&s_active.req, s_active.is_heartbeat))
        {
            s_retry_not_before_ms = now_ms + UART_ENGINE_RETRY_COOLDOWN_MS;
        }
        else
        {
            on_job_final_failure(&s_active);
            if (s_active.is_heartbeat)
            {
                s_hb_queued_or_active = false;
            }
        }
    }
    else
    {
        on_job_final_failure(&s_active);
        if (s_active.is_heartbeat)
        {
            s_hb_queued_or_active = false;
        }
    }

    s_state = UART_ENGINE_STATE_IDLE;
    active_clear();
}

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
            job_fail_and_maybe_retry(now_ms);
        }
        break;

    case UART_ENGINE_STATE_RX_WAIT:
    {
        if (s_active.req.expected_len == 0U)
        {
            s_state = UART_ENGINE_STATE_PROCESS;
            break;
        }

        if (s_rx_got < s_active.req.expected_len)
        {
            uint16_t want = (uint16_t)(s_active.req.expected_len - s_rx_got);
            s_rx_got += UART2_Read(&s_rx_buf[s_rx_got], want);
        }

        if (s_rx_got >= s_active.req.expected_len)
        {
            s_state = UART_ENGINE_STATE_PROCESS;
            break;
        }

        if ((now_ms - s_state_start_ms) >= s_active.req.timeout_ms)
        {
            job_fail_and_maybe_retry(now_ms);
        }
        break;
    }

    case UART_ENGINE_STATE_PROCESS:
    {
        bool ok = true;
        if (s_active.req.process_fn != NULL)
        {
            ok = s_active.req.process_fn(s_rx_buf, s_active.req.expected_len, s_active.req.out_value, s_active.req.user_ctx);
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
            active_clear();
            return;
        }

        // Parse failed.
        if (s_active.retries_left > 0U)
        {
            s_active.retries_left--;
            if (queue_push(&s_active.req, s_active.is_heartbeat))
            {
                s_retry_not_before_ms = now_ms + UART_ENGINE_RETRY_COOLDOWN_MS;
            }
            else
            {
                on_job_final_failure(&s_active);
                if (s_active.is_heartbeat)
                {
                    s_hb_queued_or_active = false;
                }
            }
        }
        else
        {
            on_job_final_failure(&s_active);
            if (s_active.is_heartbeat)
            {
                s_hb_queued_or_active = false;
            }
        }

        s_state = UART_ENGINE_STATE_IDLE;
        active_clear();
        break;
    }

    default:
        s_state = UART_ENGINE_STATE_IDLE;
        active_clear();
        break;
    }
}
