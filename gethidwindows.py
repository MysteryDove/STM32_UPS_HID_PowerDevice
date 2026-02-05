"""Windows HID reader by Device Interface GUID.

What it does
- Enumerates device interfaces for a hardcoded interface GUID via SetupAPI.
- Opens the selected HID interface path with CreateFile.
- Reads interrupt-transferred input reports using ReadFile (overlapped I/O).
- Queries HID report sizes and report-item mapping using hid.dll (HidP_*).

Notes
- The *HID class* interface GUID is usually {4D1E55B2-F16F-11CF-88CB-001111000030}.
  Your device may expose additional (vendor) interface GUID(s). Use the one you want.
"""

from __future__ import annotations

import argparse
import ctypes
import sys
import time
from ctypes import wintypes


def _usage_page_name(usage_page: int) -> str | None:
	# Lightweight: only the pages we care about for UPS.
	return {
		0x0084: "Power Device",
		0x0085: "Battery System",
	}.get(usage_page)


def _usage_name(usage_page: int, usage: int) -> str | None:
	# Lightweight: focus on the UPS pages. Unknown usages return None.
	if usage_page == 0x0084:
		return {
			0x0004: "Power Summary",
			0x0030: "Voltage",
			0x0031: "Current",
			0x0032: "Frequency",
			0x0035: "Percent Load",
			0x0036: "Temperature",
			0x0040: "Config Voltage",
			0x0044: "Config Active Power",
			0x0053: "Low Voltage Transfer",
			0x0054: "High Voltage Transfer",
			# PresentStatus bit usages (common UPS mapping)
			0x0069: "AC Present",
			0x0065: "Charging",
			0x00D1: "Discharging",
			0x0042: "Fully Charged",
			0x004B: "Need Replacement",
			0x0046: "Below Remaining Capacity Limit",
			0x0045: "Battery Present",
			0x0044: "Overload",
			0x00D0: "Shutdown Imminent",
			# Your descriptor uses 2-bit packed string indices
			0x0001: "iManufacturer (2-bit)",
			0x00FF: "iProduct (2-bit)",
			0x00FE: "iSerialNumber (2-bit)",
			0x00FD: "iName (2-bit)",
		}.get(usage)

	if usage_page == 0x0085:
		return {
			0x0066: "Remaining Capacity",
			0x0067: "Full Charge Capacity",
			0x0068: "Run Time To Empty",
			0x0029: "Remaining Capacity Limit",
			0x008C: "Warning Capacity Limit",
			0x002A: "Remaining Time Limit",
			0x0089: "Device Chemistry",
			0x002C: "Capacity Mode",
			0x0083: "Design Capacity",
			0x008D: "Capacity Granularity 2",
			0x008E: "Capacity Granularity 1",
			0x008B: "Rechargeable",
			0x0085: "Battery Voltage",
		}.get(usage)

	return None


def _fmt_usage(usage_page: int, usage: int) -> str:
	upn = _usage_page_name(usage_page)
	un = _usage_name(usage_page, usage)
	up = f"0x{usage_page:04X}" + (f" ({upn})" if upn else "")
	u = f"0x{usage:04X}" + (f" ({un})" if un else "")
	return f"UsagePage={up} Usage={u}"


# Some Python builds don't expose wintypes.ULONG_PTR.
ULONG_PTR = ctypes.c_size_t

# ----------------------------
# User hardcoded settings
# ----------------------------

# Replace this with your target device interface GUID.
# Default is the standard HID class interface GUID.
TARGET_INTERFACE_GUID = "{4D1E55B2-F16F-11CF-88CB-001111000030}"

# Optional: narrow selection by substring match in the device path.
# Example: "vid_051d&pid_0002" (APC) or a serial fragment.
TARGET_DEVICE_PATH_CONTAINS: str | None = None


# ----------------------------
# Win32 + SetupAPI + HID types
# ----------------------------

setupapi = ctypes.WinDLL("setupapi", use_last_error=True)
hid = ctypes.WinDLL("hid", use_last_error=True)
kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)


class GUID(ctypes.Structure):
	_fields_ = [
		("Data1", wintypes.DWORD),
		("Data2", wintypes.WORD),
		("Data3", wintypes.WORD),
		("Data4", wintypes.BYTE * 8),
	]


def parse_guid(guid_str: str) -> GUID:
	s = guid_str.strip().lstrip("{").rstrip("}")
	parts = s.split("-")
	if len(parts) != 5:
		raise ValueError(f"Invalid GUID: {guid_str!r}")
	data1 = int(parts[0], 16)
	data2 = int(parts[1], 16)
	data3 = int(parts[2], 16)
	d4a = bytes.fromhex(parts[3])
	d4b = bytes.fromhex(parts[4])
	if len(d4a) != 2 or len(d4b) != 6:
		raise ValueError(f"Invalid GUID: {guid_str!r}")
	data4 = (wintypes.BYTE * 8)(*(d4a + d4b))
	return GUID(data1, data2, data3, data4)


INVALID_HANDLE_VALUE = wintypes.HANDLE(-1).value

DIGCF_PRESENT = 0x00000002
DIGCF_DEVICEINTERFACE = 0x00000010

SPDRP_HARDWAREID = 0x00000001


class SP_DEVINFO_DATA(ctypes.Structure):
	_fields_ = [
		("cbSize", wintypes.DWORD),
		("ClassGuid", GUID),
		("DevInst", wintypes.DWORD),
		("Reserved", ULONG_PTR),
	]


class SP_DEVICE_INTERFACE_DATA(ctypes.Structure):
	_fields_ = [
		("cbSize", wintypes.DWORD),
		("InterfaceClassGuid", GUID),
		("Flags", wintypes.DWORD),
		("Reserved", ULONG_PTR),
	]


def _raise_last_error(msg: str) -> None:
	err = ctypes.get_last_error()
	raise OSError(err, f"{msg}: {ctypes.WinError(err).strerror}")


setupapi.SetupDiGetClassDevsW.argtypes = [
	ctypes.POINTER(GUID),
	wintypes.LPCWSTR,
	wintypes.HWND,
	wintypes.DWORD,
]
setupapi.SetupDiGetClassDevsW.restype = wintypes.HANDLE

setupapi.SetupDiEnumDeviceInterfaces.argtypes = [
	wintypes.HANDLE,
	ctypes.POINTER(SP_DEVINFO_DATA),
	ctypes.POINTER(GUID),
	wintypes.DWORD,
	ctypes.POINTER(SP_DEVICE_INTERFACE_DATA),
]
setupapi.SetupDiEnumDeviceInterfaces.restype = wintypes.BOOL

setupapi.SetupDiGetDeviceInterfaceDetailW.argtypes = [
	wintypes.HANDLE,
	ctypes.POINTER(SP_DEVICE_INTERFACE_DATA),
	wintypes.LPVOID,
	wintypes.DWORD,
	ctypes.POINTER(wintypes.DWORD),
	ctypes.POINTER(SP_DEVINFO_DATA),
]
setupapi.SetupDiGetDeviceInterfaceDetailW.restype = wintypes.BOOL

setupapi.SetupDiDestroyDeviceInfoList.argtypes = [wintypes.HANDLE]
setupapi.SetupDiDestroyDeviceInfoList.restype = wintypes.BOOL


# HID parser structures (from hidpi.h)
HIDP_STATUS_SUCCESS = 0x00110000


class HIDP_CAPS(ctypes.Structure):
	_fields_ = [
		("Usage", wintypes.USHORT),
		("UsagePage", wintypes.USHORT),
		("InputReportByteLength", wintypes.USHORT),
		("OutputReportByteLength", wintypes.USHORT),
		("FeatureReportByteLength", wintypes.USHORT),
		("Reserved", wintypes.USHORT * 17),
		("NumberLinkCollectionNodes", wintypes.USHORT),
		("NumberInputButtonCaps", wintypes.USHORT),
		("NumberInputValueCaps", wintypes.USHORT),
		("NumberInputDataIndices", wintypes.USHORT),
		("NumberOutputButtonCaps", wintypes.USHORT),
		("NumberOutputValueCaps", wintypes.USHORT),
		("NumberOutputDataIndices", wintypes.USHORT),
		("NumberFeatureButtonCaps", wintypes.USHORT),
		("NumberFeatureValueCaps", wintypes.USHORT),
		("NumberFeatureDataIndices", wintypes.USHORT),
	]


class HIDP_RANGE(ctypes.Structure):
	_fields_ = [
		("UsageMin", wintypes.USHORT),
		("UsageMax", wintypes.USHORT),
		("StringMin", wintypes.USHORT),
		("StringMax", wintypes.USHORT),
		("DesignatorMin", wintypes.USHORT),
		("DesignatorMax", wintypes.USHORT),
		("DataIndexMin", wintypes.USHORT),
		("DataIndexMax", wintypes.USHORT),
	]


class HIDP_NOTRANGE(ctypes.Structure):
	_fields_ = [
		("Usage", wintypes.USHORT),
		("Reserved1", wintypes.USHORT),
		("StringIndex", wintypes.USHORT),
		("Reserved2", wintypes.USHORT),
		("DesignatorIndex", wintypes.USHORT),
		("Reserved3", wintypes.USHORT),
		("DataIndex", wintypes.USHORT),
		("Reserved4", wintypes.USHORT),
	]


class _HIDP_UNION_RANGE(ctypes.Union):
	_fields_ = [("Range", HIDP_RANGE), ("NotRange", HIDP_NOTRANGE)]


class HIDP_VALUE_CAPS(ctypes.Structure):
	_anonymous_ = ("u",)
	_fields_ = [
		("UsagePage", wintypes.USHORT),
		("ReportID", wintypes.BYTE),
		("IsAlias", wintypes.BOOLEAN),
		("BitField", wintypes.USHORT),
		("LinkCollection", wintypes.USHORT),
		("LinkUsage", wintypes.USHORT),
		("LinkUsagePage", wintypes.USHORT),
		("IsRange", wintypes.BOOLEAN),
		("IsStringRange", wintypes.BOOLEAN),
		("IsDesignatorRange", wintypes.BOOLEAN),
		("IsAbsolute", wintypes.BOOLEAN),
		("HasNull", wintypes.BOOLEAN),
		("Reserved", wintypes.BYTE),
		("BitSize", wintypes.USHORT),
		("ReportCount", wintypes.USHORT),
		("Reserved2", wintypes.USHORT * 5),
		("UnitsExp", wintypes.ULONG),
		("Units", wintypes.ULONG),
		("LogicalMin", wintypes.LONG),
		("LogicalMax", wintypes.LONG),
		("PhysicalMin", wintypes.LONG),
		("PhysicalMax", wintypes.LONG),
		("u", _HIDP_UNION_RANGE),
	]


class HIDP_BUTTON_CAPS(ctypes.Structure):
	_anonymous_ = ("u",)
	_fields_ = [
		("UsagePage", wintypes.USHORT),
		("ReportID", wintypes.BYTE),
		("IsAlias", wintypes.BOOLEAN),
		("BitField", wintypes.USHORT),
		("LinkCollection", wintypes.USHORT),
		("LinkUsage", wintypes.USHORT),
		("LinkUsagePage", wintypes.USHORT),
		("IsRange", wintypes.BOOLEAN),
		("IsStringRange", wintypes.BOOLEAN),
		("IsDesignatorRange", wintypes.BOOLEAN),
		("IsAbsolute", wintypes.BOOLEAN),
		("Reserved", wintypes.DWORD * 10),
		("u", _HIDP_UNION_RANGE),
	]


PHIDP_PREPARSED_DATA = wintypes.LPVOID

hid.HidD_GetPreparsedData.argtypes = [wintypes.HANDLE, ctypes.POINTER(PHIDP_PREPARSED_DATA)]
hid.HidD_GetPreparsedData.restype = wintypes.BOOL
hid.HidD_FreePreparsedData.argtypes = [PHIDP_PREPARSED_DATA]
hid.HidD_FreePreparsedData.restype = wintypes.BOOL

hid.HidD_GetInputReport.argtypes = [wintypes.HANDLE, wintypes.LPVOID, wintypes.ULONG]
hid.HidD_GetInputReport.restype = wintypes.BOOL

hid.HidD_GetFeature.argtypes = [wintypes.HANDLE, wintypes.LPVOID, wintypes.ULONG]
hid.HidD_GetFeature.restype = wintypes.BOOL

hid.HidP_GetCaps.argtypes = [PHIDP_PREPARSED_DATA, ctypes.POINTER(HIDP_CAPS)]
hid.HidP_GetCaps.restype = wintypes.ULONG  # NTSTATUS

HidP_Input = 0
HidP_Output = 1
HidP_Feature = 2

hid.HidP_GetValueCaps.argtypes = [
	wintypes.ULONG,
	ctypes.POINTER(HIDP_VALUE_CAPS),
	ctypes.POINTER(wintypes.USHORT),
	PHIDP_PREPARSED_DATA,
]
hid.HidP_GetValueCaps.restype = wintypes.ULONG

hid.HidP_GetButtonCaps.argtypes = [
	wintypes.ULONG,
	ctypes.POINTER(HIDP_BUTTON_CAPS),
	ctypes.POINTER(wintypes.USHORT),
	PHIDP_PREPARSED_DATA,
]
hid.HidP_GetButtonCaps.restype = wintypes.ULONG

hid.HidP_GetUsageValue.argtypes = [
	wintypes.ULONG,
	wintypes.USHORT,
	wintypes.USHORT,
	wintypes.USHORT,
	ctypes.POINTER(wintypes.ULONG),
	PHIDP_PREPARSED_DATA,
	wintypes.LPVOID,
	wintypes.ULONG,
]
hid.HidP_GetUsageValue.restype = wintypes.ULONG

hid.HidP_MaxUsageListLength.argtypes = [wintypes.ULONG, wintypes.USHORT, PHIDP_PREPARSED_DATA]
hid.HidP_MaxUsageListLength.restype = wintypes.ULONG

hid.HidP_GetUsages.argtypes = [
	wintypes.ULONG,
	wintypes.USHORT,
	wintypes.USHORT,
	ctypes.POINTER(wintypes.USHORT),
	ctypes.POINTER(wintypes.ULONG),
	PHIDP_PREPARSED_DATA,
	wintypes.LPVOID,
	wintypes.ULONG,
]
hid.HidP_GetUsages.restype = wintypes.ULONG


kernel32.CreateFileW.argtypes = [
	wintypes.LPCWSTR,
	wintypes.DWORD,
	wintypes.DWORD,
	wintypes.LPVOID,
	wintypes.DWORD,
	wintypes.DWORD,
	wintypes.HANDLE,
]
kernel32.CreateFileW.restype = wintypes.HANDLE

kernel32.ReadFile.argtypes = [
	wintypes.HANDLE,
	wintypes.LPVOID,
	wintypes.DWORD,
	ctypes.POINTER(wintypes.DWORD),
	wintypes.LPVOID,
]
kernel32.ReadFile.restype = wintypes.BOOL

kernel32.CloseHandle.argtypes = [wintypes.HANDLE]
kernel32.CloseHandle.restype = wintypes.BOOL

kernel32.CreateEventW.argtypes = [wintypes.LPVOID, wintypes.BOOL, wintypes.BOOL, wintypes.LPCWSTR]
kernel32.CreateEventW.restype = wintypes.HANDLE

kernel32.ResetEvent.argtypes = [wintypes.HANDLE]
kernel32.ResetEvent.restype = wintypes.BOOL

kernel32.WaitForSingleObject.argtypes = [wintypes.HANDLE, wintypes.DWORD]
kernel32.WaitForSingleObject.restype = wintypes.DWORD

kernel32.GetOverlappedResult.argtypes = [
	wintypes.HANDLE,
	wintypes.LPVOID,
	ctypes.POINTER(wintypes.DWORD),
	wintypes.BOOL,
]
kernel32.GetOverlappedResult.restype = wintypes.BOOL


class OVERLAPPED(ctypes.Structure):
	_fields_ = [
		("Internal", ULONG_PTR),
		("InternalHigh", ULONG_PTR),
		("Offset", wintypes.DWORD),
		("OffsetHigh", wintypes.DWORD),
		("hEvent", wintypes.HANDLE),
	]


GENERIC_READ = 0x80000000
GENERIC_WRITE = 0x40000000
FILE_SHARE_READ = 0x00000001
FILE_SHARE_WRITE = 0x00000002
OPEN_EXISTING = 3
FILE_FLAG_OVERLAPPED = 0x40000000
WAIT_OBJECT_0 = 0x00000000
WAIT_TIMEOUT = 0x00000102
ERROR_IO_PENDING = 997


def enum_device_interface_paths(interface_guid: GUID) -> list[str]:
	hinfo = setupapi.SetupDiGetClassDevsW(
		ctypes.byref(interface_guid),
		None,
		None,
		DIGCF_PRESENT | DIGCF_DEVICEINTERFACE,
	)
	if hinfo == INVALID_HANDLE_VALUE:
		_raise_last_error("SetupDiGetClassDevsW failed")

	paths: list[str] = []
	try:
		index = 0
		while True:
			iface_data = SP_DEVICE_INTERFACE_DATA()
			iface_data.cbSize = ctypes.sizeof(SP_DEVICE_INTERFACE_DATA)

			ok = setupapi.SetupDiEnumDeviceInterfaces(
				hinfo, None, ctypes.byref(interface_guid), index, ctypes.byref(iface_data)
			)
			if not ok:
				err = ctypes.get_last_error()
				# ERROR_NO_MORE_ITEMS = 259
				if err == 259:
					break
				_raise_last_error("SetupDiEnumDeviceInterfaces failed")

			required = wintypes.DWORD(0)
			devinfo = SP_DEVINFO_DATA()
			devinfo.cbSize = ctypes.sizeof(SP_DEVINFO_DATA)

			setupapi.SetupDiGetDeviceInterfaceDetailW(
				hinfo,
				ctypes.byref(iface_data),
				None,
				0,
				ctypes.byref(required),
				ctypes.byref(devinfo),
			)

			buf = ctypes.create_string_buffer(required.value)
			cbsize = 8 if ctypes.sizeof(ctypes.c_void_p) == 8 else 6
			ctypes.cast(buf, ctypes.POINTER(wintypes.DWORD)).contents.value = cbsize

			ok = setupapi.SetupDiGetDeviceInterfaceDetailW(
				hinfo,
				ctypes.byref(iface_data),
				ctypes.byref(buf),
				required,
				ctypes.byref(required),
				ctypes.byref(devinfo),
			)
			if not ok:
				_raise_last_error("SetupDiGetDeviceInterfaceDetailW failed")

			base = ctypes.addressof(buf)
			# The offset to DevicePath is annoyingly inconsistent across examples/toolchains.
			# Be robust: try common offsets and choose a sane-looking path.
			candidates = [
				ctypes.wstring_at(base + 4),
				ctypes.wstring_at(base + 8),
			]
			device_path = next(
				(c for c in candidates if c.startswith("\\\\?\\") or c.startswith("\\\\.\\")),
				candidates[0],
			)
			paths.append(device_path)
			index += 1
	finally:
		setupapi.SetupDiDestroyDeviceInfoList(hinfo)

	return paths


def open_hid_path(path: str) -> wintypes.HANDLE:
	desired = GENERIC_READ | GENERIC_WRITE
	# Many HID collections work fine without OVERLAPPED; some return ERROR_INVALID_FUNCTION
	# when attempting overlapped reads. We default to non-overlapped.
	flags = 0
	handle = kernel32.CreateFileW(
		path,
		desired,
		FILE_SHARE_READ | FILE_SHARE_WRITE,
		None,
		OPEN_EXISTING,
		flags,
		None,
	)
	if handle == INVALID_HANDLE_VALUE:
		# Retry read-only
		handle = kernel32.CreateFileW(
			path,
			GENERIC_READ,
			FILE_SHARE_READ | FILE_SHARE_WRITE,
			None,
			OPEN_EXISTING,
			flags,
			None,
		)
	if handle == INVALID_HANDLE_VALUE:
		_raise_last_error(f"CreateFileW failed for {path}")
	return handle


def get_hid_caps(handle: wintypes.HANDLE) -> tuple[HIDP_CAPS, PHIDP_PREPARSED_DATA]:
	preparsed = PHIDP_PREPARSED_DATA()
	if not hid.HidD_GetPreparsedData(handle, ctypes.byref(preparsed)):
		_raise_last_error("HidD_GetPreparsedData failed")

	caps = HIDP_CAPS()
	status = hid.HidP_GetCaps(preparsed, ctypes.byref(caps))
	if status != HIDP_STATUS_SUCCESS:
		hid.HidD_FreePreparsedData(preparsed)
		raise RuntimeError(f"HidP_GetCaps failed NTSTATUS=0x{status:08X}")
	return caps, preparsed


def _dump_value_caps(
	preparsed: PHIDP_PREPARSED_DATA,
	report_type: int,
	count: int,
	report_ids: set[int] | None = None,
) -> None:
	if count <= 0:
		return
	length = wintypes.USHORT(count)
	arr = (HIDP_VALUE_CAPS * count)()
	status = hid.HidP_GetValueCaps(report_type, arr, ctypes.byref(length), preparsed)
	if status != HIDP_STATUS_SUCCESS:
		print(f"  HidP_GetValueCaps({report_type}) failed NTSTATUS=0x{status:08X}")
		return

	print(f"  ValueCaps[{length.value}]:")
	for i in range(length.value):
		vc = arr[i]
		if report_ids is not None:
			report_ids.add(int(vc.ReportID))
		if vc.IsRange:
			usage = f"0x{vc.Range.UsageMin:04X}-0x{vc.Range.UsageMax:04X}"
			data_index = f"{vc.Range.DataIndexMin}-{vc.Range.DataIndexMax}"
		else:
			usage = f"0x{vc.NotRange.Usage:04X}"
			data_index = f"{vc.NotRange.DataIndex}"
		if vc.IsRange:
			usage_desc = usage
		else:
			usage_desc = _fmt_usage(int(vc.UsagePage), int(vc.NotRange.Usage)).replace("UsagePage=", "").replace(" Usage=", " ")
		print(
			"    "
			f"UsagePage=0x{vc.UsagePage:04X} Usage={usage_desc} ReportID={vc.ReportID} "
			f"BitSize={vc.BitSize} ReportCount={vc.ReportCount} DataIndex={data_index} "
			f"Logical=[{vc.LogicalMin},{vc.LogicalMax}] Physical=[{vc.PhysicalMin},{vc.PhysicalMax}] "
			f"Abs={int(vc.IsAbsolute)} Null={int(vc.HasNull)}"
		)


def _get_value_caps(preparsed: PHIDP_PREPARSED_DATA, report_type: int, count: int) -> list[HIDP_VALUE_CAPS]:
	if count <= 0:
		return []
	length = wintypes.USHORT(count)
	arr = (HIDP_VALUE_CAPS * count)()
	status = hid.HidP_GetValueCaps(report_type, arr, ctypes.byref(length), preparsed)
	if status != HIDP_STATUS_SUCCESS:
		return []
	return [arr[i] for i in range(length.value)]


def _dump_button_caps(
	preparsed: PHIDP_PREPARSED_DATA,
	report_type: int,
	count: int,
	report_ids: set[int] | None = None,
) -> None:
	if count <= 0:
		return
	length = wintypes.USHORT(count)
	arr = (HIDP_BUTTON_CAPS * count)()
	status = hid.HidP_GetButtonCaps(report_type, arr, ctypes.byref(length), preparsed)
	if status != HIDP_STATUS_SUCCESS:
		print(f"  HidP_GetButtonCaps({report_type}) failed NTSTATUS=0x{status:08X}")
		return

	print(f"  ButtonCaps[{length.value}]:")
	for i in range(length.value):
		bc = arr[i]
		if report_ids is not None:
			report_ids.add(int(bc.ReportID))
		if bc.IsRange:
			usage = f"0x{bc.Range.UsageMin:04X}-0x{bc.Range.UsageMax:04X}"
			data_index = f"{bc.Range.DataIndexMin}-{bc.Range.DataIndexMax}"
		else:
			usage = f"0x{bc.NotRange.Usage:04X}"
			data_index = f"{bc.NotRange.DataIndex}"
		if bc.IsRange:
			usage_desc = usage
		else:
			usage_desc = _fmt_usage(int(bc.UsagePage), int(bc.NotRange.Usage)).replace("UsagePage=", "").replace(" Usage=", " ")
		print(
			"    "
			f"UsagePage=0x{bc.UsagePage:04X} Usage={usage_desc} ReportID={bc.ReportID} DataIndex={data_index}"
		)


def _get_button_caps(preparsed: PHIDP_PREPARSED_DATA, report_type: int, count: int) -> list[HIDP_BUTTON_CAPS]:
	if count <= 0:
		return []
	length = wintypes.USHORT(count)
	arr = (HIDP_BUTTON_CAPS * count)()
	status = hid.HidP_GetButtonCaps(report_type, arr, ctypes.byref(length), preparsed)
	if status != HIDP_STATUS_SUCCESS:
		return []
	return [arr[i] for i in range(length.value)]



def dump_report_mapping(preparsed: PHIDP_PREPARSED_DATA, caps: HIDP_CAPS) -> tuple[set[int], set[int]]:
	input_report_ids: set[int] = set()
	feature_report_ids: set[int] = set()
	print("HID Report Mapping (from hid.dll parser)")
	print(
		f"  TopLevel UsagePage=0x{caps.UsagePage:04X} Usage=0x{caps.Usage:04X} "
		f"InputLen={caps.InputReportByteLength} OutputLen={caps.OutputReportByteLength} FeatureLen={caps.FeatureReportByteLength}"
	)

	print(" Input:")
	_dump_button_caps(preparsed, HidP_Input, caps.NumberInputButtonCaps, input_report_ids)
	_dump_value_caps(preparsed, HidP_Input, caps.NumberInputValueCaps, input_report_ids)

	print(" Output:")
	_dump_button_caps(preparsed, HidP_Output, caps.NumberOutputButtonCaps)
	_dump_value_caps(preparsed, HidP_Output, caps.NumberOutputValueCaps)

	print(" Feature:")
	_dump_button_caps(preparsed, HidP_Feature, caps.NumberFeatureButtonCaps, feature_report_ids)
	_dump_value_caps(preparsed, HidP_Feature, caps.NumberFeatureValueCaps, feature_report_ids)

	input_report_ids.discard(0)
	feature_report_ids.discard(0)
	return input_report_ids, feature_report_ids


def _iter_usages_from_value_cap(vc: HIDP_VALUE_CAPS) -> list[int]:
	if vc.IsRange:
		umin = int(vc.Range.UsageMin)
		umax = int(vc.Range.UsageMax)
		# Avoid pathological ranges
		if umax < umin or (umax - umin) > 256:
			return [umin]
		return list(range(umin, umax + 1))
	return [int(vc.NotRange.Usage)]


def decode_report(
	preparsed: PHIDP_PREPARSED_DATA,
	report_type: int,
	report: bytes,
	value_caps: list[HIDP_VALUE_CAPS],
	button_caps: list[HIDP_BUTTON_CAPS],
	label: str,
) -> None:
	if not report:
		return
	report_id = report[0]
	report_buf = (ctypes.c_ubyte * len(report)).from_buffer_copy(report)

	print(f"Decoded {label} ReportID={report_id}:")

	# Values
	for vc in value_caps:
		if int(vc.ReportID) != report_id:
			continue
		usage_page = int(vc.UsagePage)
		for usage in _iter_usages_from_value_cap(vc):
			out = wintypes.ULONG(0)
			status = hid.HidP_GetUsageValue(
				report_type,
				usage_page,
				0,
				usage,
				ctypes.byref(out),
				preparsed,
				ctypes.byref(report_buf),
				len(report),
			)
			if status == HIDP_STATUS_SUCCESS:
				print(f"  Value {_fmt_usage(usage_page, usage)} -> {int(out.value)}")

	# Buttons (pressed usages per usage page)
	usage_pages = sorted({int(bc.UsagePage) for bc in button_caps if int(bc.ReportID) == report_id})
	for up in usage_pages:
		max_len = hid.HidP_MaxUsageListLength(report_type, up, preparsed)
		if max_len == 0:
			continue
		usage_list = (wintypes.USHORT * int(max_len))()
		usage_len = wintypes.ULONG(max_len)
		status = hid.HidP_GetUsages(
			report_type,
			up,
			0,
			usage_list,
			ctypes.byref(usage_len),
			preparsed,
			ctypes.byref(report_buf),
			len(report),
		)
		if status == HIDP_STATUS_SUCCESS and usage_len.value:
			pressed = ", ".join(
				(
					f"0x{int(usage_list[i]):04X} ({_usage_name(up, int(usage_list[i]))})"
					if _usage_name(up, int(usage_list[i]))
					else f"0x{int(usage_list[i]):04X}"
				)
				for i in range(int(usage_len.value))
			)
			upn = _usage_page_name(up)
			up_desc = f"0x{up:04X}" + (f" ({upn})" if upn else "")
			print(f"  Buttons UsagePage={up_desc} pressed: {pressed}")


def decode_input_report(
	preparsed: PHIDP_PREPARSED_DATA,
	report: bytes,
	value_caps: list[HIDP_VALUE_CAPS],
	button_caps: list[HIDP_BUTTON_CAPS],
) -> None:
	decode_report(preparsed, HidP_Input, report, value_caps, button_caps, label="Input")


def decode_feature_report(
	preparsed: PHIDP_PREPARSED_DATA,
	report: bytes,
	value_caps: list[HIDP_VALUE_CAPS],
	button_caps: list[HIDP_BUTTON_CAPS],
) -> None:
	decode_report(preparsed, HidP_Feature, report, value_caps, button_caps, label="Feature")


def read_interrupt_reports_sync(
	handle: wintypes.HANDLE,
	report_len: int,
	count: int,
	on_report: "callable[[bytes], None] | None" = None,
) -> None:
	buf = (ctypes.c_ubyte * report_len)()
	bytes_read = wintypes.DWORD(0)
	for n in range(count):
		ok = kernel32.ReadFile(handle, ctypes.byref(buf), report_len, ctypes.byref(bytes_read), None)
		if not ok:
			err = ctypes.get_last_error()
			raise OSError(err, f"ReadFile (sync) failed: {ctypes.WinError(err).strerror}")
		data = bytes(buf[: bytes_read.value])
		report_id = data[0] if data else 0
		print(f"[{n+1}/{count}] {bytes_read.value} bytes | ReportID={report_id} | {data.hex(' ')}")
		if on_report is not None and data:
			on_report(data)


def read_interrupt_reports(
	handle: wintypes.HANDLE,
	report_len: int,
	count: int,
	timeout_ms: int,
	on_report: "callable[[bytes], None] | None" = None,
) -> None:
	if report_len <= 0:
		raise ValueError("Invalid input report length")

	event = kernel32.CreateEventW(None, True, False, None)
	if not event:
		_raise_last_error("CreateEventW failed")

	try:
		buf = (ctypes.c_ubyte * report_len)()

		for n in range(count):
			kernel32.ResetEvent(event)
			overlapped = OVERLAPPED()
			overlapped.hEvent = event

			bytes_read = wintypes.DWORD(0)
			ok = kernel32.ReadFile(handle, ctypes.byref(buf), report_len, ctypes.byref(bytes_read), ctypes.byref(overlapped))
			if not ok:
				err = ctypes.get_last_error()
				if err != ERROR_IO_PENDING:
					raise OSError(err, f"ReadFile failed: {ctypes.WinError(err).strerror}")

				wait = kernel32.WaitForSingleObject(event, timeout_ms)
				if wait == WAIT_TIMEOUT:
					print(f"[{n+1}/{count}] timeout after {timeout_ms}ms")
					continue
				if wait != WAIT_OBJECT_0:
					_raise_last_error("WaitForSingleObject failed")

				ok = kernel32.GetOverlappedResult(handle, ctypes.byref(overlapped), ctypes.byref(bytes_read), False)
				if not ok:
					_raise_last_error("GetOverlappedResult failed")

			data = bytes(buf[: bytes_read.value])
			if not data:
				print(f"[{n+1}/{count}] empty read")
				continue
			report_id = data[0]
			print(f"[{n+1}/{count}] {bytes_read.value} bytes | ReportID={report_id} | {data.hex(' ')}")
			if on_report is not None and data:
				on_report(data)
	finally:
		kernel32.CloseHandle(event)


def get_input_report_control(handle: wintypes.HANDLE, report_len: int, report_id: int) -> bytes:
	buf = (ctypes.c_ubyte * report_len)()
	buf[0] = report_id & 0xFF
	ok = hid.HidD_GetInputReport(handle, ctypes.byref(buf), report_len)
	if not ok:
		err = ctypes.get_last_error()
		raise OSError(err, f"HidD_GetInputReport failed: {ctypes.WinError(err).strerror}")
	return bytes(buf)


def get_feature_report_control(handle: wintypes.HANDLE, report_len: int, report_id: int) -> bytes:
	buf = (ctypes.c_ubyte * report_len)()
	buf[0] = report_id & 0xFF
	ok = hid.HidD_GetFeature(handle, ctypes.byref(buf), report_len)
	if not ok:
		err = ctypes.get_last_error()
		raise OSError(err, f"HidD_GetFeature failed: {ctypes.WinError(err).strerror}")
	return bytes(buf)


def main(argv: list[str]) -> int:
	ap = argparse.ArgumentParser(description="Read HID reports for a device interface GUID (Windows).")
	ap.add_argument("--guid", default=TARGET_INTERFACE_GUID, help="Device interface GUID to enumerate")
	ap.add_argument(
		"--path-contains",
		default=TARGET_DEVICE_PATH_CONTAINS,
		help="Optional substring that must appear in the device path (e.g. vid_XXXX&pid_YYYY)",
	)
	ap.add_argument(
		"--index",
		type=int,
		default=-1,
		help="If multiple devices match, pick by index. Default -1 means auto-pick first openable device.",
	)
	ap.add_argument("--read-count", type=int, default=25, help="How many input reports to try to read")
	ap.add_argument("--timeout-ms", type=int, default=2000, help="Per-report read timeout")
	ap.add_argument(
		"--no-input",
		action="store_true",
		help="Skip input reads (ReadFile / HidD_GetInputReport).",
	)
	ap.add_argument(
		"--no-feature",
		action="store_false",
		dest="get_feature",
		help="Disable HidD_GetFeature fetching.",
	)
	# Back-compat (now default behavior): keep the flag but it is effectively a no-op.
	ap.add_argument(
		"--get-feature",
		action="store_true",
		dest="get_feature",
		help="(Deprecated) Feature fetching is now default; use --no-feature to disable.",
	)
	ap.set_defaults(get_feature=True)
	args = ap.parse_args(argv)

	guid = parse_guid(args.guid)
	paths = enum_device_interface_paths(guid)
	if args.path_contains:
		paths = [p for p in paths if args.path_contains.lower() in p.lower()]

	if not paths:
		print("No matching device interfaces found.")
		print("Tip: Try the HID class GUID, and/or remove --path-contains filtering.")
		return 2

	print(f"Found {len(paths)} matching interface(s):")
	for i, p in enumerate(paths):
		print(f"  [{i}] {p}")

	selected_index: int | None = None
	handle: wintypes.HANDLE | None = None
	if args.index != -1:
		if args.index < 0 or args.index >= len(paths):
			print(f"Invalid --index {args.index}; must be -1 or 0..{len(paths)-1}")
			return 2
		selected_index = args.index
		path = paths[selected_index]
		print(f"\nOpening [{selected_index}]...")
		handle = open_hid_path(path)
	else:
		print("\nAuto-selecting first openable interface...")
		for i, p in enumerate(paths):
			try:
				h = open_hid_path(p)
			except OSError as e:
				# Common for protected keyboard/mouse collections.
				print(f"  skip [{i}] open failed: {e.strerror} (errno={e.errno})")
				continue
			selected_index = i
			path = p
			handle = h
			print(f"  selected [{selected_index}] {path}")
			break

		if handle is None or selected_index is None:
			print("No interface could be opened (all failed).")
			print("Tip: use --path-contains vid_xxxx&pid_yyyy to target your device.")
			return 5

	try:
		caps, preparsed = get_hid_caps(handle)
		try:
			input_report_ids, feature_report_ids = dump_report_mapping(preparsed, caps)
			input_value_caps = _get_value_caps(preparsed, HidP_Input, caps.NumberInputValueCaps)
			input_button_caps = _get_button_caps(preparsed, HidP_Input, caps.NumberInputButtonCaps)
			feature_value_caps = _get_value_caps(preparsed, HidP_Feature, caps.NumberFeatureValueCaps)
			feature_button_caps = _get_button_caps(preparsed, HidP_Feature, caps.NumberFeatureButtonCaps)

			def _decode_input(data: bytes) -> None:
				decode_input_report(preparsed, data, input_value_caps, input_button_caps)

			if not args.no_input:
				print("\nReading interrupt input reports (ReadFile)...")
				try:
					# Try sync first (most compatible). This still reads interrupt-transferred input reports.
					read_interrupt_reports_sync(
						handle,
						caps.InputReportByteLength,
						args.read_count,
						on_report=_decode_input,
					)
				except OSError as e:
					# ERROR_INVALID_FUNCTION=1 is common when the collection doesn't support ReadFile.
					if getattr(e, "errno", None) != 1:
						raise
					print("ReadFile not supported by this collection (errno=1). Trying overlapped ReadFile...")
					try:
						read_interrupt_reports(
							handle,
							caps.InputReportByteLength,
							args.read_count,
							args.timeout_ms,
							on_report=_decode_input,
						)
					except OSError as e2:
						if getattr(e2, "errno", None) != 1:
							raise
						if not input_report_ids:
							raise
						print("Overlapped ReadFile also not supported. Trying HidD_GetInputReport (control transfer)...")
						for n in range(args.read_count):
							for rid in sorted(input_report_ids):
								try:
									data = get_input_report_control(handle, caps.InputReportByteLength, rid)
								except OSError as e3:
									print(f"  ReportID={rid}: failed ({e3.strerror})")
									continue
								print(f"  [{n+1}/{args.read_count}] ReportID={rid}: {data.hex(' ')}")
								_decode_input(data)

			if args.get_feature:
				if caps.FeatureReportByteLength <= 0:
					print("\nNo feature report length reported by device.")
				elif not feature_report_ids:
					print("\nNo feature ReportID(s) found in parsed mapping.")
				else:
					print("\nFetching feature reports (HidD_GetFeature)...")
					for rid in sorted(feature_report_ids):
						try:
							data = get_feature_report_control(handle, caps.FeatureReportByteLength, rid)
						except OSError as e:
							print(f"  Feature ReportID={rid}: failed ({e.strerror})")
							continue
						print(f"  Feature ReportID={rid}: {data.hex(' ')}")
						decode_feature_report(preparsed, data, feature_value_caps, feature_button_caps)
		finally:
			hid.HidD_FreePreparsedData(preparsed)
	finally:
		kernel32.CloseHandle(handle)

	return 0


if __name__ == "__main__":
	raise SystemExit(main(sys.argv[1:]))
