#ifndef UART_ENGINE_H_
#define UART_ENGINE_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Non-blocking UART request engine.
//
// - Enqueue requests (cmd 8/16-bit, expected response length) paired with a
//   process callback.
// - Call uart_engine_tick() frequently from the main loop.
// - Engine uses UART2_* adapter functions (DMA TX, ring-buffer RX).

typedef enum
{
    UART_ENGINE_OK = 0,
    UART_ENGINE_ERR_QUEUE_FULL,
    UART_ENGINE_ERR_BAD_PARAM,
    UART_ENGINE_ERR_DISABLED,
} uart_engine_result_t;

typedef bool (*uart_engine_process_fn)(const uint8_t *rx, uint16_t rx_len, void *out_value, void *user_ctx);

typedef struct
{
    void *out_value;
    uint16_t cmd;
    uint8_t cmd_bits;      // 8 or 16. For 16-bit, bytes are sent MSB then LSB.
    uint8_t crlf_count;    // 0: no suffix; 1: append 0x0D 0x0A; 2: append twice; etc.
    uint16_t expected_len; // bytes to read before calling process_fn
    uint32_t timeout_ms;   // overall RX timeout
    uint8_t max_retries;   // max retries after a failure (engine will attempt 1 + max_retries total)

    uart_engine_process_fn process_fn;
    void *user_ctx;
} uart_engine_request_t;

void uart_engine_init(void);

// Enable/disable the engine at runtime.
//
// When disabled:
// - uart_engine_tick() becomes a no-op
// - queued/active jobs are dropped
// - heartbeat scheduling is stopped
// - UART lock is released (so other code won't deadlock)
void uart_engine_set_enabled(bool enable);
bool uart_engine_is_enabled(void);

// Call frequently (e.g., each main loop iteration).
void uart_engine_tick(void);

// Enqueue a request that will call process_fn(rx, expected_len, out_value, user_ctx).
// If process_fn returns true, the value is considered successfully updated.
// Note: process_fn should only write to out_value on success.
//
// cmd_bits must be 8 or 16.
// Most commands should set crlf_count=1 per your protocol; commands without CRLF
// can set crlf_count=0 (they must have a fixed expected_len).

uart_engine_result_t uart_engine_enqueue(const uart_engine_request_t *req);

// Convenience for common usage.
static inline uart_engine_result_t uart_engine_enqueue_value(void *out_value,
                                                            uint16_t cmd,
                                                            uint8_t cmd_bits,
                                                            uint8_t crlf_count,
                                                            uint16_t expected_len,
                                                            uint32_t timeout_ms,
                                                            uint8_t max_retries,
                                                            uart_engine_process_fn process_fn,
                                                            void *user_ctx)
{
    uart_engine_request_t req = {
        .out_value = out_value,
        .cmd = cmd,
        .cmd_bits = cmd_bits,
        .crlf_count = crlf_count,
        .expected_len = expected_len,
        .timeout_ms = timeout_ms,
        .max_retries = max_retries,
        .process_fn = process_fn,
        .user_ctx = user_ctx,
    };
    return uart_engine_enqueue(&req);
}

// Heartbeat monitor.
//
// The heartbeat is scheduled periodically by the engine.
// If the heartbeat request fails (after its internal retries) consecutively
// failure_threshold times (default 5), battery fields are forced to 0.

typedef struct
{
    uart_engine_request_t req;
    uint32_t interval_ms;
    uint8_t failure_threshold;
} uart_engine_heartbeat_cfg_t;

// Enable heartbeat scheduling. Pass NULL to disable.
void uart_engine_set_heartbeat(const uart_engine_heartbeat_cfg_t *cfg);

// Helper process function: exact match against expected bytes.
// user_ctx should point to a uart_engine_expect_bytes_t.

typedef struct
{
    const uint8_t *expected;
    uint16_t expected_len;
} uart_engine_expect_bytes_t;

bool uart_engine_process_expect_exact(const uint8_t *rx, uint16_t rx_len, void *out_value, void *user_ctx);

#ifdef __cplusplus
}
#endif

#endif // UART_ENGINE_H_
