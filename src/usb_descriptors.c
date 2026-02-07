/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2019 Ha Thach (tinyusb.org)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 */

#include "tusb.h"
#include "usb_descriptors.h"

#include <string.h>

/* A combination of interfaces must have a unique product id, since PC will save device driver after the first plug.
 * Same VID/PID with different interface e.g MSC (first), then CDC (later) will possibly cause system error on PC.
 *
 * Auto ProductID layout's Bitmap:
 *   [MSB]         HID | MSC | CDC          [LSB]
 */
#define PID_MAP(itf, n) ((CFG_TUD_##itf) ? (1 << (n)) : 0)
#define USB_PID 0xcafe
#define USB_VID 0x051d
#define USB_BCD 0x0200

//--------------------------------------------------------------------+
// Device Descriptors
//--------------------------------------------------------------------+
static tusb_desc_device_t const desc_device =
    {
        .bLength = sizeof(tusb_desc_device_t),
        .bDescriptorType = TUSB_DESC_DEVICE,
        .bcdUSB = USB_BCD,
        .bDeviceClass = 0x00,
        .bDeviceSubClass = 0x00,
        .bDeviceProtocol = 0x00,
        .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,

        .idVendor = USB_VID,
        .idProduct = USB_PID,
        .bcdDevice = 0x0100,

        .iManufacturer = 0x01,
        .iProduct = 0x02,
        .iSerialNumber = 0x03,

        .bNumConfigurations = 0x01};

// Invoked when received GET DEVICE DESCRIPTOR
// Application return pointer to descriptor
uint8_t const *tud_descriptor_device_cb(void)
{
    return (uint8_t const *)&desc_device;
}

//--------------------------------------------------------------------+
// HID Report Descriptor
//--------------------------------------------------------------------+

uint8_t const desc_hid_report[] = {
    TUD_HID_REPORT_DESC_UPS()
};


// Invoked when received GET HID REPORT DESCRIPTOR
// Application return pointer to descriptor
// Descriptor contents must exist long enough for transfer to complete
uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance)
{
    (void)instance;
    return desc_hid_report;
}

//--------------------------------------------------------------------+
// Configuration Descriptor
//--------------------------------------------------------------------+

enum
{
    ITF_NUM_HID = 0,
    ITF_NUM_TOTAL = 1
};

#define CONFIG_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_HID_DESC_LEN)

#define EPNUM_HID 0x81

uint8_t const desc_configuration[] =
    {
        // Config number, interface count, string index, total length, attribute, power in mA
        // set attribute to 0 for bus powered device without remote wakeup
        TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, 0, 100),

        // Interface number, string index, protocol, report descriptor len, EP In address, size & polling interval
        TUD_HID_DESCRIPTOR(ITF_NUM_HID, 0, HID_ITF_PROTOCOL_NONE, sizeof(desc_hid_report), EPNUM_HID, CFG_TUD_HID_EP_BUFSIZE, 200)};

// Invoked when received GET CONFIGURATION DESCRIPTOR
// Application return pointer to descriptor
// Descriptor contents must exist long enough for transfer to complete
uint8_t const *tud_descriptor_configuration_cb(uint8_t index)
{
    (void)index; // for multiple configurations

    // This example use the same configuration for both high and full speed mode
    return desc_configuration;
}

//--------------------------------------------------------------------+
// String Descriptors
//--------------------------------------------------------------------+

// String Descriptor Index
enum
{
    STRID_LANGID = 0,
    STRID_MANUFACTURER,
    STRID_PRODUCT,
    STRID_SERIAL,
    STRID_HID_INAME,
    STRID_HID_DEVICE_CHEM,
};

// array of pointer to string descriptors

#ifndef USB_DESC_STR_MAX_CHARS
#define USB_DESC_STR_MAX_CHARS 32U
#endif

static char s_usb_str_manufacturer[USB_DESC_STR_MAX_CHARS] = "American Power Conversion";
static char s_usb_str_product[USB_DESC_STR_MAX_CHARS] = "SPM2K";
static char s_usb_str_serial[USB_DESC_STR_MAX_CHARS] = "1145141919810";
static char s_usb_str_hid_iname[USB_DESC_STR_MAX_CHARS] = "APC UPS";
static char s_usb_str_hid_chem[USB_DESC_STR_MAX_CHARS] = "PbAc";

static char const *string_desc_arr[] = {
    (const char[]){0x09, 0x04}, // 0: supported language is English (0x0409)
    s_usb_str_manufacturer,     // 1: Manufacturer
    s_usb_str_product,          // 2: Product
    s_usb_str_serial,           // 3: Serial
    s_usb_str_hid_iname,        // 4: HID iName
    s_usb_str_hid_chem,         // 5: HID iDeviceChemistery
};

uint8_t usb_desc_string_count(void)
{
    return (uint8_t)(sizeof(string_desc_arr) / sizeof(string_desc_arr[0]));
}

const char *usb_desc_get_string_ascii(uint8_t index)
{
    uint8_t const count = usb_desc_string_count();
    if ((index == 0U) || (index >= count))
    {
        return NULL;
    }
    return string_desc_arr[index];
}

static char *usb_desc_mutable_string_for_index(uint8_t index)
{
    switch (index)
    {
    case USB_STRID_MANUFACTURER:
        return s_usb_str_manufacturer;
    case USB_STRID_PRODUCT:
        return s_usb_str_product;
    case USB_STRID_SERIAL:
        return s_usb_str_serial;
    case USB_STRID_HID_INAME:
        return s_usb_str_hid_iname;
    case USB_STRID_HID_DEVICE_CHEM:
        return s_usb_str_hid_chem;
    default:
        return NULL;
    }
}

bool usb_desc_set_string_ascii(uint8_t index, const char *str)
{
    if ((index == 0U) || (str == NULL))
    {
        return false;
    }

    char *dst = usb_desc_mutable_string_for_index(index);
    if (dst == NULL)
    {
        return false;
    }

    size_t i = 0U;
    while ((i + 1U) < (size_t)USB_DESC_STR_MAX_CHARS)
    {
        char const c = str[i];
        dst[i] = c;
        if (c == '\0')
        {
            return true;
        }
        i++;
    }

    // Ensure null termination if truncated.
    dst[USB_DESC_STR_MAX_CHARS - 1U] = '\0';
    return true;
}

static uint16_t _desc_str[32 + 1];

// Invoked when received GET STRING DESCRIPTOR request
// Application return pointer to descriptor, whose contents must exist long enough for transfer to complete
uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid)
{
    (void)langid;
    size_t chr_count;

    switch (index)
    {
    case STRID_LANGID:
        memcpy(&_desc_str[1], string_desc_arr[0], 2);
        chr_count = 1;
        break;

    default:
        // Note: the 0xEE index string is a Microsoft OS 1.0 Descriptors.
        // https://docs.microsoft.com/en-us/windows-hardware/drivers/usbcon/microsoft-defined-usb-descriptors

        if (!(index < sizeof(string_desc_arr) / sizeof(string_desc_arr[0])))
            return NULL;

        const char *str = string_desc_arr[index];

        // Cap at max char
        chr_count = strlen(str);
        size_t const max_count = sizeof(_desc_str) / sizeof(_desc_str[0]) - 1; // -1 for string type
        if (chr_count > max_count)
            chr_count = max_count;

        // Convert ASCII string into UTF-16
        for (size_t i = 0; i < chr_count; i++)
        {
            _desc_str[1 + i] = str[i];
        }
        break;
    }

    // first byte is length (including header), second byte is string type
    _desc_str[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2 * chr_count + 2));

    return _desc_str;
}
