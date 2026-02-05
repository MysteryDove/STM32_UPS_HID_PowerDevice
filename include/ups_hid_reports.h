#ifndef UPS_HID_REPORTS_H_
#define UPS_HID_REPORTS_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

// Builds a HID INPUT report payload (without the leading Report ID byte).
// Returns number of bytes written to buffer.
uint16_t build_hid_input_report(uint8_t report_id, uint8_t *buffer, uint16_t reqlen);

// Builds a HID FEATURE report payload (without the leading Report ID byte).
// Returns number of bytes written to buffer.
uint16_t build_hid_feature_report(uint8_t report_id, uint8_t *buffer, uint16_t reqlen);

// Packs a date string "MM/DD/YY" into HID Battery ManufacturerDate format.
// Returns 1 on success, 0 on failure.
int pack_hid_date_mmddyy(const char *s, uint16_t *out);

#ifdef __cplusplus
}
#endif

#endif // UPS_HID_REPORTS_H_
