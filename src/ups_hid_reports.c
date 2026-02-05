#include "ups_hid_reports.h"

#include "tusb.h"

#include "ups_data.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

// HID payload report layouts (do not include Report ID byte; TinyUSB prepends it).
typedef struct TU_ATTR_PACKED
{
    uint8_t remaining_capacity;
    uint16_t run_time_to_empty_s;
    uint16_t ps_voltage;
    uint16_t present_status_bits;
} ups_report_power_summary_input_t;

typedef struct TU_ATTR_PACKED
{
    uint8_t warning_capacity_limit;
    uint8_t remaining_capacity_limit;
    uint8_t remaining_capacity;
    uint16_t run_time_to_empty_s;
    uint16_t remaining_time_limit_s;
    uint8_t i_device_chemistry;
    uint8_t capacity_mode;
    uint8_t full_charge_capacity;
    uint8_t design_capacity;
    uint8_t rechargeable_and_padding;
    uint8_t capacity_granularity_1;
    uint8_t capacity_granularity_2;
    uint8_t i_strings_packed;
    uint16_t present_status_bits;
} ups_report_power_summary_feature_t;

typedef struct TU_ATTR_PACKED
{
    uint16_t input_voltage;
    uint16_t input_frequency;
    uint16_t config_voltage;
    uint16_t low_voltage_transfer;
    uint16_t high_voltage_transfer;
} ups_report_input_feature_t;

typedef struct TU_ATTR_PACKED
{
    uint8_t percent_load;
    uint16_t config_active_power;
    uint16_t config_voltage;
    uint16_t output_voltage;
    int16_t output_current;
    uint16_t output_frequency;
} ups_report_output_feature_t;

typedef struct TU_ATTR_PACKED
{
    uint16_t run_time_to_empty_s;
    uint16_t remaining_time_limit_s;
    uint16_t manufacturer_date;
    uint16_t battery_voltage;
    int16_t battery_current;
    uint16_t config_voltage;
    uint16_t temperature;
} ups_report_battery_feature_t;

static inline uint8_t pack_2bit4(uint8_t a, uint8_t b, uint8_t c, uint8_t d)
{
    return (uint8_t)(((a & 0x03U) << 0) |
                     ((b & 0x03U) << 2) |
                     ((c & 0x03U) << 4) |
                     ((d & 0x03U) << 6));
}

static uint16_t pack_present_status(const ups_present_status_t *status)
{
    if (status == NULL)
    {
        return 0U;
    }
    uint16_t bits = 0U;
    if (status->ac_present)
        bits |= (1U << 0);
    if (status->charging)
        bits |= (1U << 1);
    if (status->discharging)
        bits |= (1U << 2);
    if (status->fully_charged)
        bits |= (1U << 3);
    if (status->need_replacement)
        bits |= (1U << 4);
    if (status->below_remaining_capacity_limit)
        bits |= (1U << 5);
    if (status->battery_present)
        bits |= (1U << 6);
    if (status->overload)
        bits |= (1U << 7);
    if (status->shutdown_imminent)
        bits |= (1U << 8);
    return bits;
}

uint16_t build_hid_input_report(uint8_t report_id, uint8_t *buffer, uint16_t reqlen)
{
    if ((buffer == NULL) || (reqlen == 0U))
    {
        return 0U;
    }

    switch (report_id)
    {
    case REPORT_ID_POWER_SUMMARY:
    {
        uint16_t const needed = (uint16_t)sizeof(ups_report_power_summary_input_t);
        if (reqlen < needed)
        {
            return 0U;
        }
        ups_report_power_summary_input_t report = {
            .remaining_capacity = g_battery.remaining_capacity,
            .run_time_to_empty_s = g_battery.run_time_to_empty_s,
            .ps_voltage = g_battery.battery_voltage,
            .present_status_bits = pack_present_status(&g_power_summary_present_status),
        };
        memcpy(buffer, &report, sizeof(report));
        return (uint16_t)sizeof(report);
    }
    default:
        break;
    }

    return 0U;
}

uint16_t build_hid_feature_report(uint8_t report_id, uint8_t *buffer, uint16_t reqlen)
{
    if ((buffer == NULL) || (reqlen == 0U))
    {
        return 0U;
    }

    switch (report_id)
    {
    case REPORT_ID_POWER_SUMMARY:
    {
        uint16_t const needed = (uint16_t)sizeof(ups_report_power_summary_feature_t);
        if (reqlen < needed)
        {
            return 0U;
        }
        ups_report_power_summary_feature_t report = {
            .warning_capacity_limit = g_power_summary.warning_capacity_limit,
            .remaining_capacity_limit = g_power_summary.remaining_capacity_limit,
            .remaining_capacity = g_battery.remaining_capacity,
            .run_time_to_empty_s = g_battery.run_time_to_empty_s,
            .remaining_time_limit_s = g_battery.remaining_time_limit_s,
            .i_device_chemistry = g_power_summary.i_device_chemistry,
            .capacity_mode = g_power_summary.capacity_mode,
            .full_charge_capacity = g_power_summary.full_charge_capacity,
            .design_capacity = g_power_summary.design_capacity,
            .rechargeable_and_padding = (uint8_t)(g_power_summary.rechargeable ? 0x01U : 0x00U),
            .capacity_granularity_1 = g_power_summary.capacity_granularity_1,
            .capacity_granularity_2 = g_power_summary.capacity_granularity_2,
            .i_strings_packed = pack_2bit4(g_power_summary.i_manufacturer_2bit,
                                           g_power_summary.i_product_2bit,
                                           g_power_summary.i_serial_number_2bit,
                                           g_power_summary.i_name_2bit),
            .present_status_bits = pack_present_status(&g_power_summary_present_status),
        };
        memcpy(buffer, &report, sizeof(report));
        return (uint16_t)sizeof(report);
    }
    case REPORT_ID_INPUT:
    {
        uint16_t const needed = (uint16_t)sizeof(ups_report_input_feature_t);
        if (reqlen < needed)
        {
            return 0U;
        }
        ups_report_input_feature_t report = {
            .input_voltage = g_input.voltage,
            .input_frequency = g_input.frequency,
            .config_voltage = g_input.config_voltage,
            .low_voltage_transfer = g_input.low_voltage_transfer,
            .high_voltage_transfer = g_input.high_voltage_transfer,
        };
        memcpy(buffer, &report, sizeof(report));
        return (uint16_t)sizeof(report);
    }
    case REPORT_ID_OUTPUT:
    {
        uint16_t const needed = (uint16_t)sizeof(ups_report_output_feature_t);
        if (reqlen < needed)
        {
            return 0U;
        }
        ups_report_output_feature_t report = {
            .percent_load = g_output.percent_load,
            .config_active_power = g_output.config_active_power,
            .config_voltage = g_output.config_voltage,
            .output_voltage = g_output.voltage,
            .output_current = g_output.current,
            .output_frequency = g_output.frequency,
        };
        memcpy(buffer, &report, sizeof(report));
        return (uint16_t)sizeof(report);
    }
    case REPORT_ID_BATTERY:
    {
        uint16_t const needed = (uint16_t)sizeof(ups_report_battery_feature_t);
        if (reqlen < needed)
        {
            return 0U;
        }
        ups_report_battery_feature_t report = {
            .run_time_to_empty_s = g_battery.run_time_to_empty_s,
            .remaining_time_limit_s = g_battery.remaining_time_limit_s,
            .manufacturer_date = g_battery.manufacturer_date,
            .battery_voltage = g_battery.battery_voltage,
            .battery_current = g_battery.battery_current,
            .config_voltage = g_battery.config_voltage,
            .temperature = g_battery.temperature,
        };
        memcpy(buffer, &report, sizeof(report));
        return (uint16_t)sizeof(report);
    }
    default:
        break;
    }

    return 0U;
}

/*
  NUT’s date_conversion_reverse() for USB/HID packs a date into a 16‑bit value like this:
  bits 15..9: (year - 1980) (7 bits, 0..127)
  bits 8..5: month (4 bits, 1..12)
  bits 4..0: day (5 bits, 1..31)

  E.g. For 2023/02/20, the HID numeric value is:
  year - 1980 = 43
  date = (43 << 9) + (2 << 5) + 20
  (43 << 9) = 22016 (0x5600)
  (2 << 5) = 64 (0x0040)
  + 20 (0x0014)
  Total: 22016 + 64 + 20 = 22100 decimal = 0x5654
*/
int pack_hid_date_mmddyy(const char *s, uint16_t *out)
{
    unsigned int mm, dd, yy;

    if (!s || !out)
        return 0;

    /* Accept exactly "MM/DD/YY" with 2-digit fields.
       (If you want to accept "8/6/23", adjust the sscanf format.) */
    if (sscanf(s, "%2u/%2u/%2u", &mm, &dd, &yy) != 3)
        return 0;

    if (mm < 1 || mm > 12 || dd < 1 || dd > 31)
        return 0;

    /* Interpret YY as 2000..2099 by default (common expectation for 2-digit years).
       If you want a sliding window, change this mapping. */
    unsigned int year = 2000u + yy;

    if (year < 1980u || year > 2107u)
        return 0;

    unsigned int yoff = year - 1980u; /* 0..127 */

    uint16_t packed = (uint16_t)((yoff << 9) | (mm << 5) | dd);
    *out = packed;
    return 1;
}
