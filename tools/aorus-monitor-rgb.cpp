/*---------------------------------------------------------*\
| aorus-monitor-rgb: standalone CLI for testing the IT8295  |
| controller without OpenRGB.                               |
|                                                           |
|   aorus-monitor-rgb info                                  |
|   aorus-monitor-rgb static RRGGBB [brightness 0-9] [zone] |
|   aorus-monitor-rgb breathing RRGGBB [speed 0-5]          |
|   aorus-monitor-rgb flash|dflash RRGGBB [speed]          |
|   aorus-monitor-rgb cycle [speed]                         |
|   aorus-monitor-rgb off                                   |
|                                                           |
|   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#include "AorusMonitorController.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

static int usage()
{
    fprintf(stderr,
        "usage: aorus-monitor-rgb info\n"
        "       aorus-monitor-rgb static RRGGBB [brightness 0-9] [zone 0|1]\n"
        "       aorus-monitor-rgb breathing|flash|dflash RRGGBB [speed 0-5]\n"
        "       aorus-monitor-rgb cycle [speed 0-5]\n"
        "       aorus-monitor-rgb off\n");
    return 2;
}

int main(int argc, char** argv)
{
    if(argc < 2)
    {
        return usage();
    }
    std::string cmd = argv[1];

    hid_init();
    auto paths = AorusMonitorController::Enumerate();
    if(paths.empty())
    {
        fprintf(stderr, "No AORUS monitor RGB controller (048d:8295) found\n");
        return 1;
    }

    AorusMonitorController ctl(paths[0]);
    if(!ctl.IsOpen())
    {
        fprintf(stderr, "Cannot open %s (permissions? install the udev rule)\n", paths[0].c_str());
        return 1;
    }

    const aorus_monitor_info& inf = ctl.GetInfo();

    if(cmd == "info")
    {
        printf("path:          %s\n", ctl.GetPath().c_str());
        printf("product str:   %s\n", inf.product_string.c_str());
        printf("product/dev:   0x%02x / %u\n", inf.product, inf.device_num);
        printf("strip detect:  0x%02x\n", inf.strip_detect);
        printf("firmware:      %s (raw 0x%08x)\n", ctl.GetFirmwareString().c_str(), inf.fw_ver);
        printf("chip id:       0x%08x\n", inf.chip_id);
        printf("protocol:      %s\n", ctl.UsesSingleZoneCommands() ? "single-zone (new fw)" : "2-zone (legacy fw)");
        return 0;
    }

    uint32_t     color  = 0;
    unsigned int speed  = 2;
    unsigned int bright = 9;
    int          zone   = -1;
    aorus_monitor_effect eff;

    auto parse_color = [&](int idx) -> bool {
        if(argc <= idx) return false;
        const char* s = argv[idx];
        if(s[0] == '#') s++;
        char* end;
        color = (uint32_t)strtoul(s, &end, 16);
        return *end == 0 && strlen(s) == 6;
    };

    if(cmd == "static")
    {
        if(!parse_color(2)) return usage();
        if(argc > 3) bright = (unsigned int)atoi(argv[3]);
        if(argc > 4) zone   = atoi(argv[4]);
        eff = AORUS_MONITOR_EFFECT_STATIC;
    }
    else if(cmd == "breathing" || cmd == "flash" || cmd == "dflash")
    {
        if(!parse_color(2)) return usage();
        if(argc > 3) speed = (unsigned int)atoi(argv[3]);
        eff = (cmd == "breathing") ? AORUS_MONITOR_EFFECT_BREATHING
            : (cmd == "flash")     ? AORUS_MONITOR_EFFECT_FLASH
                                   : AORUS_MONITOR_EFFECT_DOUBLE_FLASH;
    }
    else if(cmd == "cycle")
    {
        if(argc > 2) speed = (unsigned int)atoi(argv[2]);
        eff = AORUS_MONITOR_EFFECT_COLOR_CYCLE;
    }
    else if(cmd == "off")
    {
        eff = AORUS_MONITOR_EFFECT_OFF;
    }
    else
    {
        return usage();
    }

    bool ok = ctl.SetZoneEffect(zone, eff, color, speed, bright) && ctl.Apply();
    printf("%s\n", ok ? "ok" : "failed");
    return ok ? 0 : 1;
}
