#include "ups_hid_device.h"

#include "ups_hid_reports.h"

#include "stm32f1xx_hal.h"
#include "tusb.h"

#include <stdbool.h>
#include <stdint.h>

static uint32_t hid_last_report_ms;
static uint8_t hid_report_cycle_index;

static void reset_hid_timing_state(void)
{
    hid_last_report_ms = 0U;
    hid_report_cycle_index = 0U;
}

void ups_hid_periodic_task(void)
{
    // Both Linux and Windows rely on GET_REPORT to do the polling job.
    // The interrupt IN report is a heartbeat to let the host know the device is alive.
    uint32_t const now_ms = HAL_GetTick();
    if ((now_ms - hid_last_report_ms) < 5000U)
    {
        return;
    }

    hid_last_report_ms = now_ms;

    if (!tud_hid_ready())
    {
        return;
    }

    uint8_t report[8];
    uint8_t report_id = 1;
    (void)hid_report_cycle_index;

    uint16_t len = build_hid_input_report(report_id, report, sizeof(report));
    if (len > 0U)
    {
        (void)tud_hid_report(report_id, report, len);
    }
}

void tud_hid_set_report_cb(uint8_t instance,
                           uint8_t report_id,
                           hid_report_type_t report_type,
                           uint8_t const *buffer,
                           uint16_t bufsize)
{
    (void)instance;
    (void)report_id;
    (void)report_type;
    (void)buffer;
    (void)bufsize;
}

uint16_t tud_hid_get_report_cb(uint8_t instance,
                               uint8_t report_id,
                               hid_report_type_t report_type,
                               uint8_t *buffer,
                               uint16_t reqlen)
{
    (void)instance;

    if (report_type == HID_REPORT_TYPE_INPUT)
    {
        return build_hid_input_report(report_id, buffer, reqlen);
    }
    if (report_type == HID_REPORT_TYPE_FEATURE)
    {
        return build_hid_feature_report(report_id, buffer, reqlen);
    }

    return 0U;
}

// Mount and unmount callbacks to prevent usb failures due to stale state.
void tud_mount_cb(void)
{
    reset_hid_timing_state();
}

void tud_umount_cb(void)
{
    reset_hid_timing_state();
}

void tud_suspend_cb(bool remote_wakeup_en)
{
    (void)remote_wakeup_en;
}

void tud_resume_cb(void)
{
    reset_hid_timing_state();
}
