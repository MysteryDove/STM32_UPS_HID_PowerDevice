
  

# STM32 UPS HID Power Device (APC RS232 → USB HID)

  

Firmware for an STM32F103 ("Blue Pill" / genericSTM32F103C8) that exposes a **USB HID Power Device (UPS)** interface.

The intent is to bridge a UPS that speaks a simple serial protocol (e.g. APC/"SPM2K"-style) into a standards-based HID Power Device so that:

  

-  **Windows** can bind the device as a system battery via `battc.sys` (HID-to-ACPI battery bridge)

-  **Linux / NUT** can read standard Power Device / Battery System usages (For now it's using vid 051D so it's developed towards **apchid** subdriver from NUT)

  

This project uses **TinyUSB** for the USB device stack and STM32Cube HAL for the MCU peripheral layer.

  

## What you get

  

- USB HID **Power Device / UPS** report descriptor (Power Summary + Input + Output + Battery)

- HID **GET_REPORT** callbacks for both **Input** and **Feature** reports

- ~~A small **UART2 adapter** (DMA TX + interrupt RX ring buffer)~~ **WIP!!!**

- ~~A **non-blocking UART request engine** (queue + retries + optional heartbeat monitoring)~~ **WIP!!!**

  

## Current status (important)

  

Right now the firmware populates the exported UPS values from **static demo values** defined in `src/main.c`, plus an optional demo decay function. You can use it as a base and get whatever battery information and report to the pc. The values may have incorrect unit exponent so parsed value under linux might be ridiculous.

To be noticed that for now it's developing towards NUT support for subdriver apchid, so some of the value might missing under linux simply because the subdriver from NUT is not using it. (Maybe developing a custom driver for nut can solve the problem? But I do not have time for now.)

For Windows Support, system level warning is triggered by by `g_power_summary.warning_capacity_limit` and force shutdown triggered by `g_power_summary.remaining_capacity_limit`, no charging icon will show if there's no current under power summary.

The UART engine is present and working as infrastructure, but the **UPS serial protocol parsing / polling is not yet implemented** (see `src/spm2k.c`).

  

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

- Each job sends an 8-bit or 16-bit command (optional CRLF suffix), waits for an expected RX length, then calls a `process_fn` callback

- Handles retries and a short cooldown between retries

- Optional "heartbeat" scheduling (`uart_engine_set_heartbeat()`); after N consecutive failures it forces some battery fields to 0 as a safety/fail indication

  

It depends on the adapter functions from `src/uart_adaptor.c` and is intended to be called frequently from the main loop via `uart_engine_tick()`.

  

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

  

This is where you change product strings (e.g. "SPM2K") and the VID/PID.

  

### `src/spm2k.c`

  

Placeholder for the UPS serial protocol implementation.

  

At the moment this file is empty; the intent is for it to:

  

- define the UPS query commands and parsing logic

- schedule UART jobs via `uart_engine_enqueue()`

- update the global UPS state (`g_battery`, `g_input`, `g_output`, `g_power_summary_*`) that feeds HID reporting

  

## CubeMX / tool-generated files (not documented here)

  

The following `.c` files are generated by STM32CubeMX:

  

-  `src/main.c`

-  `src/stm32f1xx_it.c`

-  `src/stm32f1xx_hal_msp.c`

-  `src/system_stm32f1xx.c`

-  `src/syscalls.c`

-  `src/sysmem.c`

  

## Notes / references

  

-  `src/DescriptorNUTAPC.txt` contains a working-note summary of what NUT’s APC HID mapping expects, and which HID paths/usages are commonly used.

- TinyUSB is vendored under `lib/tinyusb/`.