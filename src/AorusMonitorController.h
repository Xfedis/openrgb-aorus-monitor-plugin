/*---------------------------------------------------------*\
| AorusMonitorController.h                                  |
|                                                           |
|   Driver for the ITE IT8295 RGB controller found behind   |
|   the USB hub of GIGABYTE AORUS monitors (CV27Q, CV27F,   |
|   FI27Q, KD25F, ...). USB ID 048d:8295.                   |
|                                                           |
|   Protocol: HID feature report 0xCC (64 bytes), the same  |
|   IT8297 "Fusion 2" command set used by GIGABYTE boards:  |
|     0xCC 0x60        read controller info                 |
|     0xCC 0x32 bits   enable/disable digital strip control |
|     0xCC 0x20+n ...  write effect for zone n              |
|     0xCC 0x28 bits   apply                                |
|                                                           |
|   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>
#include <hidapi.h>

#define AORUS_MONITOR_VID           0x048D
#define AORUS_MONITOR_PID           0x8295
#define AORUS_MONITOR_REPORT_ID     0xCC
#define AORUS_MONITOR_REPORT_LEN    64

enum aorus_monitor_effect
{
    AORUS_MONITOR_EFFECT_OFF            = 0,
    AORUS_MONITOR_EFFECT_STATIC         = 1,
    AORUS_MONITOR_EFFECT_BREATHING      = 2,
    AORUS_MONITOR_EFFECT_FLASH          = 3,
    AORUS_MONITOR_EFFECT_DOUBLE_FLASH   = 4,
    AORUS_MONITOR_EFFECT_COLOR_CYCLE    = 5,
};

#define AORUS_MONITOR_SPEED_MIN         0
#define AORUS_MONITOR_SPEED_MAX         5
#define AORUS_MONITOR_BRIGHTNESS_MIN    0
#define AORUS_MONITOR_BRIGHTNESS_MAX    9

struct aorus_monitor_info
{
    uint8_t     product;
    uint8_t     device_num;
    uint8_t     strip_detect;
    uint32_t    fw_ver;             /* raw little-endian value from the device */
    std::string product_string;
    uint32_t    chip_id;
};

class AorusMonitorController
{
public:
    /*-----------------------------------------------------*\
    | Enumerate controller paths (one per physical device)  |
    \*-----------------------------------------------------*/
    static std::vector<std::string> Enumerate();

    explicit AorusMonitorController(const std::string& path);
    ~AorusMonitorController();

    bool                IsOpen() const;
    const std::string&  GetPath() const;
    std::string         GetSerial() const;
    std::string         GetFirmwareString() const;
    const aorus_monitor_info& GetInfo() const;

    /*-----------------------------------------------------*\
    | Newer firmware ("FO series" style) addresses all LEDs |
    | as one zone; older firmware (CV27Q etc.) has 2 zones  |
    \*-----------------------------------------------------*/
    bool                UsesSingleZoneCommands() const;
    unsigned int        GetZoneCount() const;

    /*-----------------------------------------------------*\
    | color is 0x00RRGGBB, speed 0-5, brightness 0-9        |
    | zone -1 = all zones                                   |
    \*-----------------------------------------------------*/
    bool                SetZoneEffect(int zone, aorus_monitor_effect effect, uint32_t color, unsigned int speed, unsigned int brightness);
    bool                Apply();

private:
    hid_device*         dev = nullptr;
    std::string         path;
    std::wstring        serial;
    aorus_monitor_info  info{};
    bool                single_zone = false;
    bool                strip_ctrl_sent = false;
    mutable std::mutex  dev_mutex;

    bool                SendReport(const uint8_t* data, size_t len);
    bool                ReadInfo();
    bool                SetDigitalStripControl(uint8_t bits);
};
