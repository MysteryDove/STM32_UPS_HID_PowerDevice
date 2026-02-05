#ifndef USB_DESCRIPTORS_H_
#define USB_DESCRIPTORS_H_

#ifdef __cplusplus
 extern "C" {
#endif

#define TUD_HID_REPORT_DESC_UPS(...) \
  HID_USAGE_PAGE(HID_USAGE_PAGE_POWER), \
  HID_USAGE(HID_USAGE_POWER_UPS), \
  HID_COLLECTION(HID_COLLECTION_APPLICATION), \
  /* Power Summary */ \
  /*
      Battery Page
    Feature(in order)
    UPS.PowerSummary.WarningCapacityLimit
    UPS.PowerSummary.RemainingCapacityLimit            
    UPS.PowerSummary.RemainingCapacity
    UPS.PowerSummary.RunTimeToEmpty
    UPS.PowerSummary.RemainingTimeLimit      
    UPS.PowerSummary.iDeviceChemistry    
    UPS.PowerSummary.CapacityMode
    UPS.PowerSummary.DesignCapacity
    UPS.PowerSummary.FullChargeCapacity
    UPS.PowerSummary.Rechargable
    UPS.PowerSummary.CapacityGranularity1
    UPS.PowerSummary.CapacityGranularity2
    UPS.PowerSummary.iManufacturer
    UPS.PowerSummary.iProduct
    UPS.PowerSummary.iSerialNumber
    UPS.PowerSummary.iName
    UPS.PowerSummary.PresentStatus.ACPresent
    UPS.PowerSummary.PresentStatus.Charging
    UPS.PowerSummary.PresentStatus.Discharging
    UPS.PowerSummary.PresentStatus.FullyCharged
    UPS.PowerSummary.PresentStatus.NeedReplacement
    UPS.PowerSummary.PresentStatus.BelowRemainingCapacityLimit
    UPS.PowerSummary.PresentStatus.BatteryPresent
    UPS.PowerSummary.PresentStatus.Overload
    UPS.PowerSummary.PresentStatus.ShutdownImminent

    INPUT(in order)
    UPS.PowerSummary.RemainingCapacity
    UPS.PowerSummary.RunTimeToEmpty
    UPS.PowerSummary.Voltage
    UPS.PowerSummary.PresentStatus.ACPresent
    UPS.PowerSummary.PresentStatus.Charging
    UPS.PowerSummary.PresentStatus.Discharging
    UPS.PowerSummary.PresentStatus.FullyCharged
    UPS.PowerSummary.PresentStatus.NeedReplacement
    UPS.PowerSummary.PresentStatus.BelowRemainingCapacityLimit
    UPS.PowerSummary.PresentStatus.BatteryPresent
    UPS.PowerSummary.PresentStatus.Overload
    UPS.PowerSummary.PresentStatus.ShutdownImminent

    - UPS.PowerSummary.AudibleAlarmControl               (Not implemented)      
    - UPS.PowerSummary.DelayBeforeStartup                (Not implemented)      
    - UPS.PowerSummary.DelayBeforeShutdown               (Not implemented)      
    - UPS.PowerSummary.DelayBeforeReboot                 (Not implemented)      
    - UPS.PowerSummary.APCBattReplaceDate                (Not implemented)      
    - UPS.PowerSummary.APCPanelTest                      (Not implemented)     

    Extra required info to make it compatible with windows's battc.sys which convert hid device into ACPI battery device
    According to pdc 5.6 Equivalence between ACPI Battery Information and Power Summary Usages
    - UPS.PowerSummary.PresentStatus.RemainingTimeLimitExpired (not implemented)

  */ \
    HID_REPORT_ID(1) \
    HID_USAGE(HID_USAGE_POWER_POWER_SUMMARY), \
    HID_COLLECTION(HID_COLLECTION_LOGICAL), \
      HID_USAGE_PAGE(HID_USAGE_PAGE_BATTERY), \
      HID_USAGE(HID_USAGE_BATTERY_REMAINING_CAPACITY), \
      HID_LOGICAL_MIN(0), \
      HID_LOGICAL_MAX(100), \
      HID_REPORT_SIZE(8), \
      HID_REPORT_COUNT(1), \
      HID_UNIT_EXPONENT(0), \
      HID_UNIT(0x00), \
      HID_INPUT(HID_DATA | HID_VARIABLE | HID_ABSOLUTE | HID_VOLATILE), \
      HID_USAGE(HID_USAGE_BATTERY_RUN_TIME_TO_EMPTY), \
      HID_UNIT_N(0x0110, 2), /* SI Lin: Time */ \
      HID_REPORT_SIZE(16), \
      HID_REPORT_COUNT(1), \
      HID_LOGICAL_MIN(0), \
      HID_LOGICAL_MAX_N(65534,3), \
      HID_INPUT(HID_DATA | HID_VARIABLE | HID_ABSOLUTE | HID_VOLATILE), \
      HID_USAGE_PAGE(HID_USAGE_PAGE_POWER), \
      HID_USAGE(HID_USAGE_POWER_VOLTAGE), \
      HID_UNIT_N(0x00F0D121, 3), /* SI Lin: Volts */ \
      HID_UNIT_EXPONENT(5), \
      HID_LOGICAL_MIN(0), \
      HID_LOGICAL_MAX_N(65534,3), \
      HID_REPORT_SIZE(16), \
      HID_REPORT_COUNT(1), \
      HID_INPUT(HID_DATA | HID_VARIABLE | HID_ABSOLUTE | HID_VOLATILE), \
      HID_USAGE_PAGE(HID_USAGE_PAGE_BATTERY), \
      HID_USAGE(HID_USAGE_BATTERY_WARNING_CAPACITY_LIMIT), \
      HID_USAGE(HID_USAGE_BATTERY_REMAINING_CAPACITY_LIMIT), \
      HID_LOGICAL_MIN(0), \
      HID_LOGICAL_MAX(100), \
      HID_REPORT_SIZE(8), \
      HID_REPORT_COUNT(2), \
      HID_UNIT_EXPONENT(0), \
      HID_UNIT(0x00), \
      HID_FEATURE(HID_DATA | HID_VARIABLE | HID_ABSOLUTE), \
      HID_USAGE(HID_USAGE_BATTERY_REMAINING_CAPACITY), \
      HID_LOGICAL_MIN(0), \
      HID_LOGICAL_MAX(100), \
      HID_REPORT_SIZE(8), \
      HID_REPORT_COUNT(1), \
      HID_UNIT_EXPONENT(0), \
      HID_UNIT(0x00), \
      HID_FEATURE(HID_DATA | HID_VARIABLE | HID_ABSOLUTE | HID_VOLATILE), \
      HID_USAGE(HID_USAGE_BATTERY_RUN_TIME_TO_EMPTY), \
      HID_UNIT_N(0x0110, 2), /* SI Lin: Time */ \
      HID_REPORT_SIZE(16), \
      HID_REPORT_COUNT(1), \
      HID_LOGICAL_MIN(0), \
      HID_LOGICAL_MAX_N(65534,3), \
      HID_FEATURE(HID_DATA | HID_VARIABLE | HID_ABSOLUTE | HID_VOLATILE), \
      HID_USAGE(HID_USAGE_BATTERY_REMAINING_TIME_LIMIT), \
      HID_UNIT_N(0x1001, 2), /* SI Lin: Time */ \
      HID_REPORT_SIZE(16), \
      HID_REPORT_COUNT(1), \
      HID_LOGICAL_MIN(0), \
      HID_LOGICAL_MAX_N(65534,3), \
      HID_FEATURE(HID_DATA | HID_VARIABLE | HID_ABSOLUTE), \
      HID_USAGE(HID_USAGE_BATTERY_I_DEVICE_CHEMISTRY), \
      HID_UNIT(0), \
      HID_REPORT_SIZE(8), \
      HID_REPORT_COUNT(1), \
      HID_UNIT_EXPONENT(0), \
      HID_LOGICAL_MIN(0), \
      HID_LOGICAL_MAX_N(255,2), \
      HID_FEATURE(HID_DATA | HID_VARIABLE | HID_ABSOLUTE), \
      HID_USAGE(HID_USAGE_BATTERY_CAPACITY_MODE), \
      HID_LOGICAL_MIN(0), \
      HID_LOGICAL_MAX(3), \
      HID_REPORT_SIZE(8), \
      HID_REPORT_COUNT(1), \
      HID_FEATURE(HID_DATA | HID_VARIABLE | HID_ABSOLUTE), \
      HID_USAGE(HID_USAGE_BATTERY_FULL_CHARGE_CAPACITY), \
      HID_USAGE(HID_USAGE_BATTERY_DESIGN_CAPACITY), \
      HID_LOGICAL_MIN(0), \
      HID_LOGICAL_MAX(100), \
      HID_REPORT_SIZE(8), \
      HID_REPORT_COUNT(2), \
      HID_FEATURE(HID_DATA | HID_VARIABLE | HID_ABSOLUTE), \
      HID_USAGE(HID_USAGE_BATTERY_RECHARGEABLE), \
      HID_LOGICAL_MIN(0), \
      HID_LOGICAL_MAX(1), \
      HID_REPORT_SIZE(1), \
      HID_REPORT_COUNT(1), \
      HID_FEATURE(HID_DATA | HID_VARIABLE | HID_ABSOLUTE), \
      /* Padding */ \
      HID_REPORT_SIZE(7), \
      HID_REPORT_COUNT(1), \
      HID_FEATURE(HID_CONSTANT | HID_VARIABLE | HID_ABSOLUTE), \
      HID_USAGE(HID_USAGE_BATTERY_CAPACITY_GRANULARITY_1), \
      HID_USAGE(HID_USAGE_BATTERY_CAPACITY_GRANULARITY_2), \
      HID_LOGICAL_MIN(0), \
      HID_LOGICAL_MAX(100), \
      HID_REPORT_SIZE(8), \
      HID_REPORT_COUNT(2), \
      HID_FEATURE(HID_DATA | HID_VARIABLE | HID_ABSOLUTE), \
      HID_USAGE_PAGE(HID_USAGE_PAGE_POWER), \
      HID_USAGE(HID_USAGE_POWER_I_MANUFACTURER), \
      HID_USAGE(HID_USAGE_POWER_I_PRODUCT), \
      HID_USAGE(HID_USAGE_POWER_I_SERIAL_NUMBER), \
      HID_USAGE(HID_USAGE_POWER_I_NAME), \
      HID_REPORT_SIZE(2), \
      HID_REPORT_COUNT(4), \
      HID_FEATURE(HID_DATA | HID_VARIABLE | HID_ABSOLUTE), \
      HID_USAGE_PAGE(HID_USAGE_PAGE_POWER), \
      HID_USAGE(HID_USAGE_POWER_PRESENT_STATUS), \
      HID_COLLECTION(HID_COLLECTION_LOGICAL), \
        HID_USAGE_PAGE(HID_USAGE_PAGE_BATTERY), \
        HID_USAGE(HID_USAGE_BATTERY_AC_PRESENT), \
        HID_USAGE(HID_USAGE_BATTERY_CHARGING), \
        HID_USAGE(HID_USAGE_BATTERY_DISCHARGING), \
        HID_USAGE(HID_USAGE_BATTERY_FULLY_CHARGED), \
        HID_USAGE(HID_USAGE_BATTERY_NEED_REPLACEMENT), \
        HID_USAGE(HID_USAGE_BATTERY_BELOW_REMAINING_CAPACITY_LIMIT), \
        HID_USAGE(HID_USAGE_BATTERY_BATTERY_PRESENT), \
        HID_USAGE_PAGE(HID_USAGE_PAGE_POWER), \
        HID_USAGE(HID_USAGE_POWER_OVERLOAD), \
        HID_USAGE(HID_USAGE_POWER_SHUTDOWN_IMMINENT), \
        HID_REPORT_SIZE(1), \
        HID_REPORT_COUNT(9), \
        HID_LOGICAL_MIN(0), \
        HID_LOGICAL_MAX(1), \
        HID_INPUT(HID_DATA | HID_VARIABLE | HID_ABSOLUTE | HID_VOLATILE), \
        /* Padding */ \
        HID_REPORT_SIZE(1), \
        HID_REPORT_COUNT(7), \
        HID_INPUT(HID_CONSTANT | HID_VARIABLE | HID_ABSOLUTE), \
        HID_USAGE_PAGE(HID_USAGE_PAGE_BATTERY), \
        HID_USAGE(HID_USAGE_BATTERY_AC_PRESENT), \
        HID_USAGE(HID_USAGE_BATTERY_CHARGING), \
        HID_USAGE(HID_USAGE_BATTERY_DISCHARGING), \
        HID_USAGE(HID_USAGE_BATTERY_FULLY_CHARGED), \
        HID_USAGE(HID_USAGE_BATTERY_NEED_REPLACEMENT), \
        HID_USAGE(HID_USAGE_BATTERY_BELOW_REMAINING_CAPACITY_LIMIT), \
        HID_USAGE(HID_USAGE_BATTERY_BATTERY_PRESENT), \
        HID_USAGE_PAGE(HID_USAGE_PAGE_POWER), \
        HID_USAGE(HID_USAGE_POWER_OVERLOAD), \
        HID_USAGE(HID_USAGE_POWER_SHUTDOWN_IMMINENT), \
        HID_REPORT_SIZE(1), \
        HID_REPORT_COUNT(9), \
        HID_LOGICAL_MIN(0), \
        HID_LOGICAL_MAX(1), \
        HID_FEATURE(HID_DATA | HID_VARIABLE | HID_ABSOLUTE | HID_VOLATILE), \
        /* Padding */ \
        HID_REPORT_SIZE(1), \
        HID_REPORT_COUNT(7), \
        HID_FEATURE(HID_CONSTANT | HID_VARIABLE | HID_ABSOLUTE), \
      HID_COLLECTION_END, \
    HID_COLLECTION_END, \
  /* Input Section
      UPS.Input.APCLineFailCause  (Not implemented)
      UPS.Input.Voltage                 16b
      UPS.Input.Frequency               16b
      UPS.Input.ConfigVoltage           16b
      UPS.Input.LowVoltageTransfer      16b
      UPS.Input.HighVoltageTransfer     16b
      UPS.Input.APCSensitivity    (Not implemented)
      Total : 5 fields, 80 bits = 10 bytes
      Unfortunately, things like input frequency is not present in NUT's apc-hid.c maybe i can write a stm32 usb hid driver for nut later?
  */ \
  HID_REPORT_ID(2) \
  HID_USAGE_PAGE(HID_USAGE_PAGE_POWER), \
  HID_USAGE(HID_USAGE_POWER_INPUT), \
  HID_COLLECTION(HID_COLLECTION_LOGICAL), \
    HID_USAGE(HID_USAGE_POWER_VOLTAGE), \
    HID_UNIT_N(0x00F0D121, 3), /* SI Lin: Volts */ \
    HID_UNIT_EXPONENT(5), \
    HID_LOGICAL_MIN(0), \
    HID_LOGICAL_MAX_N(65534,3), \
    HID_REPORT_SIZE(16), \
    HID_REPORT_COUNT(1), \
    HID_FEATURE(HID_DATA | HID_VARIABLE | HID_ABSOLUTE | HID_VOLATILE), \
    HID_USAGE(HID_USAGE_POWER_FREQUENCY), \
    HID_UNIT_N(0xF001, 2), /* SI Lin: Hertz */ \
    HID_UNIT_EXPONENT(-2), \
    HID_LOGICAL_MIN(0), \
    HID_LOGICAL_MAX_N(65534,3), \
    HID_REPORT_SIZE(16), \
    HID_REPORT_COUNT(1), \
    HID_FEATURE(HID_DATA | HID_VARIABLE | HID_ABSOLUTE | HID_VOLATILE), \
    HID_USAGE(HID_USAGE_POWER_CONFIG_VOLTAGE), \
    HID_UNIT_N(0x00F0D121, 3), /* SI Lin: Volts */ \
    HID_UNIT_EXPONENT(5), \
    HID_LOGICAL_MIN(0), \
    HID_LOGICAL_MAX_N(65534,3), \
    HID_REPORT_SIZE(16), \
    HID_REPORT_COUNT(1), \
    HID_FEATURE(HID_DATA | HID_VARIABLE | HID_ABSOLUTE), \
    HID_USAGE(HID_USAGE_POWER_LOW_VOLTAGE_TRANSFER), \
    HID_USAGE(HID_USAGE_POWER_HIGH_VOLTAGE_TRANSFER), \
    HID_LOGICAL_MIN(0), \
    HID_LOGICAL_MAX_N(400,2), \
    HID_UNIT_EXPONENT(5), \
    HID_REPORT_SIZE(16), \
    HID_REPORT_COUNT(2), \
    HID_FEATURE(HID_DATA | HID_VARIABLE | HID_ABSOLUTE), \
  HID_COLLECTION_END,\
    /* Output Section
      UPS.Output.PercentLoad          8b
      UPS.Output.ConfigActivePower    16b
      UPS.Output.ConfigVoltage        16b
      UPS.Output.Voltage              16b
      UPS.Output.Current              16b
      UPS.Output.Frequency            16b
      Total: 6 fields, 80 bits = 10 bytes
  */ \
  HID_REPORT_ID(3) \
  HID_USAGE(HID_USAGE_POWER_OUTPUT), \
  HID_COLLECTION(HID_COLLECTION_LOGICAL), \
    HID_USAGE(HID_USAGE_POWER_PERCENT_LOAD), \
    HID_UNIT(0x00), \
    HID_UNIT_EXPONENT(0), \
    HID_LOGICAL_MIN(0), \
    HID_LOGICAL_MAX(100), \
    HID_REPORT_SIZE(8), \
    HID_REPORT_COUNT(1), \
    HID_FEATURE(HID_DATA | HID_VARIABLE | HID_ABSOLUTE | HID_VOLATILE), \
    HID_USAGE(HID_USAGE_POWER_CONFIG_ACTIVE_POWER), \
    HID_UNIT_N(0x21D1, 2), /* SI Lin: Watts */ \
    HID_UNIT_EXPONENT(7), \
    HID_LOGICAL_MIN(0), \
    HID_LOGICAL_MAX_N(65534,3), \
    HID_REPORT_SIZE(16), \
    HID_REPORT_COUNT(1), \
    HID_FEATURE(HID_DATA | HID_VARIABLE | HID_ABSOLUTE), \
    HID_USAGE(HID_USAGE_POWER_CONFIG_VOLTAGE), \
    HID_LOGICAL_MIN(0), \
    HID_LOGICAL_MAX_N(400,2), \
    HID_UNIT_EXPONENT(5), \
    HID_UNIT_N(0x00F0D121, 3), /* SI Lin: Volts */ \
    HID_REPORT_SIZE(16), \
    HID_REPORT_COUNT(1), \
    HID_FEATURE(HID_DATA | HID_VARIABLE | HID_ABSOLUTE), \
    HID_USAGE(HID_USAGE_POWER_VOLTAGE), \
    HID_UNIT_N(0x00F0D121, 3), /* SI Lin: Volts */ \
    HID_UNIT_EXPONENT(5), \
    HID_LOGICAL_MIN(0), \
    HID_LOGICAL_MAX_N(65534,3), \
    HID_REPORT_SIZE(16), \
    HID_REPORT_COUNT(1), \
    HID_FEATURE(HID_DATA | HID_VARIABLE | HID_ABSOLUTE | HID_VOLATILE), \
    HID_USAGE(HID_USAGE_POWER_CURRENT), \
    HID_UNIT_N(0x00100001, 3), /* SI Lin: Amps */ \
    HID_UNIT_EXPONENT(-2), \
    HID_LOGICAL_MIN_N(-32768,2), \
    HID_LOGICAL_MAX_N(32767,2), \
    HID_REPORT_SIZE(16), \
    HID_REPORT_COUNT(1), \
    HID_FEATURE(HID_DATA | HID_VARIABLE | HID_ABSOLUTE | HID_VOLATILE), \
    HID_USAGE(HID_USAGE_POWER_FREQUENCY), \
    HID_UNIT_N(0xF001, 2), /* SI Lin: Hertz */ \
    HID_UNIT_EXPONENT(-2), \
    HID_LOGICAL_MIN(0), \
    HID_LOGICAL_MAX_N(65534,3), \
    HID_REPORT_SIZE(16), \
    HID_REPORT_COUNT(1), \
    HID_FEATURE(HID_DATA | HID_VARIABLE | HID_ABSOLUTE | HID_VOLATILE), \
  HID_COLLECTION_END, \
  /* Battery 
    UPS.Battery.RunTimeToEmpty        16b
    UPS.Battery.RemainingTimeLimit    16b
    UPS.Battery.ManufacturerDate      16b
    UPS.Battery.Voltage               16b
    UPS.Battery.Current               16b
    UPS.Battery.ConfigVoltage         16b
    UPS.Battery.Temperature           16b
    Total: 7 fields, 112 bits = 14 bytes
  */ \
  HID_REPORT_ID(4) \
  HID_USAGE(HID_USAGE_POWER_BATTERY), \
  HID_COLLECTION(HID_COLLECTION_LOGICAL), \
    HID_USAGE_PAGE(HID_USAGE_PAGE_BATTERY), \
    HID_USAGE(HID_USAGE_BATTERY_RUN_TIME_TO_EMPTY), \
    HID_UNIT_N(0x0110, 2), /* SI Lin: Time */ \
    HID_UNIT_EXPONENT(0), \
    HID_LOGICAL_MIN(0), \
    HID_LOGICAL_MAX_N(65534,3), \
    HID_REPORT_SIZE(16), \
    HID_REPORT_COUNT(1), \
    HID_FEATURE(HID_DATA | HID_VARIABLE | HID_ABSOLUTE | HID_VOLATILE), \
    HID_USAGE(HID_USAGE_BATTERY_REMAINING_TIME_LIMIT), \
    HID_UNIT_N(0x1001, 2), /* SI Lin: Time */ \
    HID_UNIT_EXPONENT(0), \
    HID_LOGICAL_MIN(0), \
    HID_LOGICAL_MAX_N(65534,3), \
    HID_REPORT_SIZE(16), \
    HID_REPORT_COUNT(1), \
    HID_FEATURE(HID_DATA | HID_VARIABLE | HID_ABSOLUTE), \
    HID_USAGE(HID_USAGE_BATTERY_MANUFACTURER_DATE), \
    HID_UNIT(0), \
    HID_UNIT_EXPONENT(0), \
    HID_LOGICAL_MIN(0), \
    HID_LOGICAL_MAX_N(65534,3), \
    HID_REPORT_SIZE(16), \
    HID_REPORT_COUNT(1), \
    HID_FEATURE(HID_DATA | HID_VARIABLE | HID_ABSOLUTE), \
    HID_USAGE_PAGE(HID_USAGE_PAGE_POWER), \
    HID_USAGE(HID_USAGE_POWER_VOLTAGE), \
    HID_UNIT_N(0x00F0D121, 3), /* SI Lin: Volts */ \
    HID_UNIT_EXPONENT(5), \
    HID_LOGICAL_MIN(0), \
    HID_LOGICAL_MAX_N(65534,3), \
    HID_REPORT_SIZE(16), \
    HID_REPORT_COUNT(1), \
    HID_FEATURE(HID_DATA | HID_VARIABLE | HID_ABSOLUTE | HID_VOLATILE), \
    HID_USAGE(HID_USAGE_POWER_CURRENT), \
    HID_UNIT_N(0x00100001, 3), /* SI Lin: Amps */ \
    HID_UNIT_EXPONENT(-2), \
    HID_LOGICAL_MIN_N(-32768,2), \
    HID_LOGICAL_MAX_N(32767,2), \
    HID_REPORT_SIZE(16), \
    HID_REPORT_COUNT(1), \
    HID_FEATURE(HID_DATA | HID_VARIABLE | HID_ABSOLUTE | HID_VOLATILE), \
    HID_USAGE(HID_USAGE_POWER_CONFIG_VOLTAGE), \
    HID_UNIT_N(0x00F0D121, 3), /* SI Lin: Volts */ \
    HID_UNIT_EXPONENT(5), \
    HID_LOGICAL_MIN(0), \
    HID_LOGICAL_MAX_N(65534,3), \
    HID_REPORT_SIZE(16), \
    HID_REPORT_COUNT(1), \
    HID_FEATURE(HID_DATA | HID_VARIABLE | HID_ABSOLUTE), \
    HID_USAGE(HID_USAGE_POWER_TEMPERATURE), \
    HID_UNIT_N(0x00010001, 3), /* SI Lin: Temperature */ \
    HID_UNIT_EXPONENT(-1), \
    HID_LOGICAL_MIN(0), \
    HID_LOGICAL_MAX_N(4000,2), \
    HID_REPORT_SIZE(16), \
    HID_REPORT_COUNT(1), \
    HID_FEATURE(HID_DATA | HID_VARIABLE | HID_ABSOLUTE | HID_VOLATILE), \
  HID_COLLECTION_END, \
HID_COLLECTION_END \


#ifdef __cplusplus
}
#endif

#endif