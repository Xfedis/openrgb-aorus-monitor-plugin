/*---------------------------------------------------------*\
| AorusMonitorController.cpp                                |
|                                                           |
|   See AorusMonitorController.h for protocol summary.      |
|   Timings and field values mirror GIGABYTE Control        |
|   Center's RgbMonitor module so effects look identical.   |
|                                                           |
|   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#include "AorusMonitorController.h"

#include <cstdio>
#include <cstring>
#include <set>

/*---------------------------------------------------------*\
| Effect parameter block, written at offset 2 of the report |
| (layout of GIGABYTE's DataFormat_8297, 32 bytes, LE)      |
\*---------------------------------------------------------*/
#pragma pack(push, 1)
struct aorus_effect_block
{
    uint32_t    zone_sel0;
    uint32_t    zone_sel1;
    uint8_t     reserved0;
    uint8_t     mode_sel;
    uint8_t     max_brightness;
    uint8_t     min_brightness;
    uint32_t    color0;             /* 0x00RRGGBB */
    uint32_t    color1;
    uint16_t    time_base0;
    uint16_t    time_base1;
    uint16_t    time_base2;
    uint16_t    time_base3;
    uint8_t     ctrl_val0;
    uint8_t     ctrl_val1;
    uint8_t     ctrl_oem0;
    uint8_t     ctrl_oem1;
};
#pragma pack(pop)
static_assert(sizeof(aorus_effect_block) == 32, "effect block must be 32 bytes");

static const uint8_t  led_brightness[10]   = { 15, 20, 30, 40, 50, 60, 70, 80, 90, 100 };
static const uint16_t pulse_timing[6][2]   = { {1600,1600}, {1400,1400}, {1200,1200}, {1000,1000}, {900,900}, {800,800} };
static const uint16_t flash_timing[6][3]   = { {100,100,2400}, {100,100,2200}, {100,100,2000}, {100,100,1800}, {100,100,1600}, {100,100,1400} };
static const uint16_t dflash_timing[6][3]  = { {100,100,2600}, {100,100,2400}, {100,100,2200}, {100,100,2000}, {100,100,1800}, {100,100,1600} };
static const uint16_t cycle_timing[6][2]   = { {2400,2200}, {1100,900}, {1000,800}, {900,700}, {800,600}, {700,500} };

static uint16_t le16(uint16_t v) { return v; }  /* x86/ARM Linux are little-endian */

std::vector<std::string> AorusMonitorController::Enumerate()
{
    std::vector<std::string> paths;
    std::set<std::string>    seen;

    hid_device_info* devs = hid_enumerate(AORUS_MONITOR_VID, AORUS_MONITOR_PID);

    for(hid_device_info* cur = devs; cur; cur = cur->next)
    {
        /*-------------------------------------------------*\
        | hidraw lists one entry per top-level collection;  |
        | the 0xCC collection lives on usage page 0xFF89    |
        \*-------------------------------------------------*/
        if(cur->usage_page != 0 && cur->usage_page != 0xFF89)
        {
            continue;
        }

        std::string p = cur->path ? cur->path : "";
        if(!p.empty() && seen.insert(p).second)
        {
            paths.push_back(p);
        }
    }

    hid_free_enumeration(devs);
    return paths;
}

AorusMonitorController::AorusMonitorController(const std::string& dev_path) : path(dev_path)
{
    dev = hid_open_path(path.c_str());

    if(!dev)
    {
        return;
    }

    wchar_t buf[128] = {0};
    if(hid_get_serial_number_string(dev, buf, 127) == 0)
    {
        serial = buf;
    }

    if(!ReadInfo())
    {
        hid_close(dev);
        dev = nullptr;
        return;
    }

    /*-----------------------------------------------------*\
    | Same rule GIGABYTE uses (RGB_Monitor.GetFwVersion)    |
    \*-----------------------------------------------------*/
    uint8_t fw[4];
    memcpy(fw, &info.fw_ver, 4);
    single_zone = (fw[2] == 4) || (fw[0] == 1);
}

AorusMonitorController::~AorusMonitorController()
{
    if(dev)
    {
        hid_close(dev);
    }
}

bool AorusMonitorController::IsOpen() const                  { return dev != nullptr; }
const std::string& AorusMonitorController::GetPath() const    { return path; }
const aorus_monitor_info& AorusMonitorController::GetInfo() const { return info; }
bool AorusMonitorController::UsesSingleZoneCommands() const   { return single_zone; }
unsigned int AorusMonitorController::GetZoneCount() const     { return single_zone ? 1 : 2; }

std::string AorusMonitorController::GetSerial() const
{
    return std::string(serial.begin(), serial.end());
}

std::string AorusMonitorController::GetFirmwareString() const
{
    uint8_t fw[4];
    memcpy(fw, &info.fw_ver, 4);
    char s[32];
    snprintf(s, sizeof(s), "%u.%u.%u.%u", fw[0], fw[1], fw[2], fw[3]);
    return s;
}

bool AorusMonitorController::SendReport(const uint8_t* data, size_t len)
{
    uint8_t buf[AORUS_MONITOR_REPORT_LEN] = {0};
    memcpy(buf, data, len < sizeof(buf) ? len : sizeof(buf));
    buf[0] = AORUS_MONITOR_REPORT_ID;

    std::lock_guard<std::mutex> lock(dev_mutex);
    if(!dev)
    {
        return false;
    }
    return hid_send_feature_report(dev, buf, sizeof(buf)) == (int)sizeof(buf);
}

bool AorusMonitorController::ReadInfo()
{
    uint8_t cmd[2] = { AORUS_MONITOR_REPORT_ID, 0x60 };
    if(!SendReport(cmd, sizeof(cmd)))
    {
        return false;
    }

    uint8_t buf[AORUS_MONITOR_REPORT_LEN] = {0};
    buf[0] = AORUS_MONITOR_REPORT_ID;

    int res;
    {
        std::lock_guard<std::mutex> lock(dev_mutex);
        res = hid_get_feature_report(dev, buf, sizeof(buf));
    }
    if(res < 12)
    {
        return false;
    }

    /*-----------------------------------------------------*\
    | IT8297_Info layout                                    |
    |   0 report id, 1 product, 2 device num, 3 strip detect|
    |   4..7 fw ver, 8..9 strip0 ctrl len, 10..11 reserved  |
    |   12..39 product string, 40..55 calibration, 56 chip  |
    \*-----------------------------------------------------*/
    info.product      = buf[1];
    info.device_num   = buf[2];
    info.strip_detect = buf[3];
    memcpy(&info.fw_ver, &buf[4], 4);

    char name[29] = {0};
    memcpy(name, &buf[12], 28);
    info.product_string = name;

    if(res >= 60)
    {
        memcpy(&info.chip_id, &buf[56], 4);
    }
    return true;
}

bool AorusMonitorController::SetDigitalStripControl(uint8_t bits)
{
    uint8_t cmd[3] = { AORUS_MONITOR_REPORT_ID, 0x32, bits };
    return SendReport(cmd, sizeof(cmd));
}

bool AorusMonitorController::SetZoneEffect(int zone, aorus_monitor_effect effect, uint32_t color, unsigned int speed, unsigned int brightness)
{
    if(!dev)
    {
        return false;
    }

    if(speed > AORUS_MONITOR_SPEED_MAX)           speed      = AORUS_MONITOR_SPEED_MAX;
    if(brightness > AORUS_MONITOR_BRIGHTNESS_MAX) brightness = AORUS_MONITOR_BRIGHTNESS_MAX;

    /*-----------------------------------------------------*\
    | Make sure the firmware's addressable-strip animation  |
    | engine is off, otherwise it overrides zone effects    |
    \*-----------------------------------------------------*/
    if(!strip_ctrl_sent)
    {
        SetDigitalStripControl(0x00);
        strip_ctrl_sent = true;
    }

    aorus_effect_block blk;
    memset(&blk, 0, sizeof(blk));

    switch(effect)
    {
        case AORUS_MONITOR_EFFECT_OFF:
            blk.mode_sel       = 1;
            blk.max_brightness = 100;
            color              = 0;
            break;

        case AORUS_MONITOR_EFFECT_STATIC:
            blk.mode_sel       = 1;
            blk.max_brightness = led_brightness[brightness];
            break;

        case AORUS_MONITOR_EFFECT_BREATHING:
        {
            uint16_t p0  = pulse_timing[speed][0];
            uint16_t p1  = pulse_timing[speed][1];
            uint16_t n2  = p0 - p0 / 2;
            uint16_t n3  = p1 - p0 / 2;
            uint16_t n4  = n2 - 220;
            blk.mode_sel       = 2;
            blk.time_base0     = le16(n2);
            blk.time_base1     = le16(n3 - n4);
            blk.time_base2     = le16(n2 + n4 / 2);
            blk.max_brightness = 100;
            blk.min_brightness = 0;
            blk.ctrl_val1      = 1;
            break;
        }

        case AORUS_MONITOR_EFFECT_FLASH:
            blk.mode_sel       = 3;
            blk.time_base0     = le16(flash_timing[speed][0]);
            blk.time_base1     = le16(flash_timing[speed][1]);
            blk.time_base2     = le16(flash_timing[speed][2]);
            blk.max_brightness = led_brightness[brightness];
            blk.ctrl_val1      = 1;
            blk.ctrl_oem0      = 1;
            break;

        case AORUS_MONITOR_EFFECT_DOUBLE_FLASH:
            blk.mode_sel       = 3;
            blk.time_base0     = le16(dflash_timing[speed][0]);
            blk.time_base1     = le16(dflash_timing[speed][1]);
            blk.time_base2     = le16(dflash_timing[speed][2]);
            blk.max_brightness = led_brightness[brightness];
            blk.ctrl_val1      = 1;
            blk.ctrl_oem0      = 2;
            break;

        case AORUS_MONITOR_EFFECT_COLOR_CYCLE:
            blk.mode_sel       = 4;
            blk.time_base0     = le16(cycle_timing[speed][0]);
            blk.time_base1     = le16(cycle_timing[speed][1]);
            blk.max_brightness = led_brightness[brightness];
            blk.ctrl_val0      = 7;
            color              = 0;
            break;
    }

    blk.color0 = color & 0x00FFFFFF;

    uint8_t buf[AORUS_MONITOR_REPORT_LEN] = {0};
    buf[0] = AORUS_MONITOR_REPORT_ID;

    if(single_zone)
    {
        /*-------------------------------------------------*\
        | Newer firmware: one register (0x21) for all LEDs  |
        \*-------------------------------------------------*/
        blk.zone_sel0 = 0xFFFFFFFE;
        blk.zone_sel1 = 0x000001FF;
        buf[1] = 0x21;
        memcpy(&buf[2], &blk, sizeof(blk));
        return SendReport(buf, sizeof(buf));
    }

    bool ok = true;
    for(int z = 0; z < 2; z++)
    {
        if(zone != -1 && zone != z)
        {
            continue;
        }
        aorus_effect_block zb = blk;
        zb.zone_sel0 = (uint32_t)1 << z;
        buf[1] = (uint8_t)(0x20 + z);
        memcpy(&buf[2], &zb, sizeof(zb));
        ok &= SendReport(buf, sizeof(buf));
    }
    return ok;
}

bool AorusMonitorController::Apply()
{
    uint8_t cmd[3] = { AORUS_MONITOR_REPORT_ID, 0x28, 0x03 };
    return SendReport(cmd, sizeof(cmd));
}
