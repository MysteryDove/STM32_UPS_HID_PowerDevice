
  

# STM32 UPS HID Power Device (APC RS232 → USB HID)

  

Firmware for an STM32F103 ("Blue Pill" / genericSTM32F103C8) that exposes a **USB HID Power Device (UPS)** interface.

The intent is to bridge a UPS that speaks a simple serial protocol (e.g. APC/"SPM2K"-style) into a standards-based HID Power Device so that:

  

-  **Windows** can bind the device as a system battery via `battc.sys` (HID-to-ACPI battery bridge)

-  **Linux / NUT** can read standard Power Device / Battery System usages (For now it's using vid 051D so it's developed towards **apchid** subdriver from NUT)

  

This project uses **TinyUSB** for the USB device stack and STM32Cube HAL for the MCU peripheral layer.

Please notice the EDA project is also under GPL-3.0 License, so if you want to use or modify the PCB design, please also comply with the license terms.

## What you get

  

- USB HID **Power Device / UPS** report descriptor (Power Summary + Input + Output + Battery)

- HID **GET_REPORT** callbacks for both **Input** and **Feature** reports

- A small **UART2 adapter** (DMA TX + interrupt RX ring buffer)

- A **non-blocking UART request engine** (queue + retries + optional heartbeat monitoring)

- A protocol parser module for **SPM2K/APC-style serial responses** (`src/spm2k.c`) that provides LUT-based request definitions and value parsers

  

## Current status (important)

- Everything should work fine now and code development is kinda done. I draw a pcb board using easyEDA and the project file can be found in the repo now. The board is not validated yet.

- For Windows Support, system level warning is triggered by `g_power_summary.warning_capacity_limit` and force shutdown is triggered by `g_power_summary.remaining_capacity_limit`; no charging icon will show if there is no current under power summary.

- USB startup is now gated in `main.c` by `g_usb_init_enabled` (default `false`). On Blue Pill boards with a fixed D+ pull-up, firmware can hold PA12 low until USB start to avoid early host attach detection.

- UPS state defaults in `main.c` are now zeroed at boot and become valid after successful UART bootstrap.



## Known Issues

- Under Linux NUT, because the apc-hid sub-driver have a flag ST_FLAG_STRING on `battery.runtime.low` `input.transfer.low` `input.transfer.high` the current reading of these values from nut will be very strange single digit number. Further investigation is needed to see if it's possible to make it working.

- No watchdog for stm32 system now, if the stm32 system hangs for some reason, it will not recover itself. Adding a watchdog is possible but need more work to be done.

## Hardware


Target board: STM32F103C8 (48 MHz PLL in current config).

  

Typical connections:

  

- USB FS: PA11 (DM), PA12 (DP)

- UART2 (TTL level): PA2 (TX), PA3 (RX)

- UART1 (debug to get all print message with baudrate 115200): PA9

- If your UPS is true RS-232 voltage levels, use a level shifter/transceiver (e.g. MAX3232).

  

UART2 defaults to **2400 8N1** (see `MX_USART2_UART_Init()` in `src/main.c`).

  

## Build & flash (PlatformIO)

  

This is a PlatformIO project (see `platformio.ini`).

  

- Build: `pio run`

- Upload via ST-Link: `pio run -t upload`

  

Default environment is `env:genericSTM32F103C8` using `framework = stm32cube`.

  

## Quick host-side HID inspection (Windows)

  

`gethidwindows.py` is a helper script to enumerate HID interfaces and read reports using SetupAPI + `hid.dll`.

  

Examples:

  

- Enumerate and auto-open the first openable HID interface:

-  `python .\gethidwindows.py`

- Narrow to a device path containing a VID/PID substring:

-  `python .\gethidwindows.py --path-contains vid_051d&pid_cafe`

- Pick a specific interface by index:

-  `python .\gethidwindows.py --index 0`

  

Note: the USB VID/PID are defined in `src/usb_descriptors.c`. If you change them, update the `--path-contains` filter accordingly.

  

## Source guide: project-specific .c files

  

The following files are **not** STM32CubeMX-generated and contain the core project logic.

  

### `src/uart_adaptor.c`

  

Low-level UART2 glue used by the request engine:

  

- RX: interrupt-driven single-byte receive feeding a ring buffer (`UART2_RxStartIT()`, `HAL_UART_RxCpltCallback()`)

- TX: blocking (`HAL_UART_Transmit`) and DMA (`HAL_UART_Transmit_DMA`) send helpers

- A simple IRQ-safe lock (`UART2_TryLock()` / `UART2_Unlock()`) so the engine can own UART2 during a transaction

  

This module exposes the `UART2_*` functions declared in `include/main.h`.

  

### `src/uart_engine.c`

  

Non-blocking UART polling engine.

  

- Provides a small queue of jobs (`uart_engine_enqueue()`)

- Callback signature is `process_fn(cmd, rx, rx_len, out_value)` (no `user_ctx`)

- Supports two RX completion modes:
  - fixed-length mode (`expected_ending = false`): wait for `expected_len` bytes
  - terminator mode (`expected_ending = true`): wait until `expected_ending_bytes[]` is received; `expected_len` is treated as max capture length

- Command framing/suffix bytes (e.g. CRLF) are caller-side protocol concerns, not engine concerns

- Handles retries and a short cooldown between retries

- Adds a configurable inter-job pacing gap (`UART_ENGINE_INTERJOB_COOLDOWN_MS`, default 15 ms)

- Exposes `uart_engine_is_busy()` so upper-layer scheduling can know when queue/active work has drained

- Includes richer debug diagnostics (TX command bytes, enqueue/retry/failure/timeout logs, and raw RX dump on parse/enqueue failures) when debug printing is enabled in `main.c`

  

It depends on the adapter functions from `src/uart_adaptor.c` and is intended to be called frequently from the main loop via `uart_engine_tick()`.

  

### `src/main.c`

  

Application orchestration and runtime flow (inside USER CODE regions):

  

- Selects sub-adapter LUTs (currently SPM2K)

- Runs bootstrap sequence (heartbeat -> constant LUT -> dynamic LUT -> sanity check)

- Starts USB only after bootstrap success (`g_usb_init_enabled` gating)

- Schedules periodic dynamic refresh cycles

- Provides optional UART debug status prints and LED busy blinking while UART engine is active

- Exposes a global debug gate (`g_ups_debug_status_print_enabled`) and TX logging helper (`UPS_DebugPrintTxCommand()`), used by the UART engine debug output path

  

### `src/ups_hid_reports.c`

  

Builds HID report payloads from the global UPS state in `include/ups_data.h`:

  

-  `build_hid_input_report()` for interrupt-IN/input style reports

-  `build_hid_feature_report()` for feature reports (what Windows/Linux typically query via control transfers)

- Packs the Power Summary PresentStatus boolean fields into a bitfield

- Provides `pack_hid_date_mmddyy()` helper that packs a date into the HID Battery ManufacturerDate format used by common UPS stacks

  

### `src/usb_hid_ups.c`

  

TinyUSB HID callbacks and small periodic behavior:

  

- Implements `tud_hid_get_report_cb()` to serve both Input and Feature reports using `src/ups_hid_reports.c`

- Provides `ups_hid_periodic_task()` which sends a low-rate interrupt-IN "heartbeat" report (hosts generally still poll via GET_REPORT)

- Resets internal timing state on USB mount/unmount/resume

  

### `src/usb_descriptors.c`

  

USB descriptor definitions for TinyUSB:

  

- Device descriptor (VID/PID, strings)

- Configuration descriptor (single HID interface)

- HID report descriptor via `TUD_HID_REPORT_DESC_UPS()` (macro defined in `include/usb_descriptors.h`)

  

This is where you change VID/PID and default USB strings.

String descriptors are now backed by mutable buffers and can be read/updated at runtime via:

- `usb_desc_string_count()`
- `usb_desc_get_string_ascii(index)`
- `usb_desc_set_string_ascii(index, str)`

  

### `src/spm2k.c`

  

SPM2K/APC protocol request definitions and response parsers.

  

This module now provides:

  

- LUTs for "constant" and "dynamic" UPS query sets (`g_spm2k_constant_lut`, `g_spm2k_dynamic_lut`)

- Heartbeat request definition and expected response bytes (`g_spm2k_constant_heartbeat`, `g_spm2k_constant_heartbeat_expect_return`)

- Dynamic LUT includes an explicit periodic `'Y'` liveness query

- parsing helpers that validate ASCII/CSV/hex formats and write converted values into HID state fields

- Accepts `NA` / `N/A` for selected numeric fields (e.g. some voltage/frequency/current replies) and keeps prior values instead of failing the whole update

- command-aware string parsing that can update USB product/serial strings via `usb_desc_set_string_ascii()`


## Notes / references


- TinyUSB is vendored under `lib/tinyusb/`.
