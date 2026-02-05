#ifndef UPS_HID_DEVICE_H_
#define UPS_HID_DEVICE_H_

#ifdef __cplusplus
extern "C" {
#endif

// Runs periodic HID housekeeping (e.g., interrupt IN heartbeat report).
// Call this frequently from the main loop.
void ups_hid_periodic_task(void);

#ifdef __cplusplus
}
#endif

#endif // UPS_HID_DEVICE_H_
