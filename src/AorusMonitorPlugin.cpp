/*---------------------------------------------------------*\
| AorusMonitorPlugin.cpp                                    |
|                                                           |
|   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#include "AorusMonitorPlugin.h"

#include <QLabel>
#include <QVBoxLayout>
#include <QWidget>

#include <cstdarg>
#include <dirent.h>
#include <fstream>
#include <iterator>
#include <thread>

#define PLUGIN_NAME         "AORUS Monitor RGB"
#define PLUGIN_DESCRIPTION  "Rear RGB lighting of GIGABYTE AORUS monitors (CV27Q, CV27F, FI27Q, KD25F, ...)"

/* LogManager levels (LogManager.h): 0 fatal, 1 error, 2 warning, 3 info, 4 verbose, 5 debug */
#define AM_LOG(api, level, ...) do { if(api) (api)->LogEntry(__FILE__, __LINE__, level, __VA_ARGS__); } while(0)

/*---------------------------------------------------------*\
| Mode values (mode.value) map 1:1 to aorus_monitor_effect  |
\*---------------------------------------------------------*/
enum
{
    MODE_IDX_DIRECT = 0,
};

/*---------------------------------------------------------*\
| Find the monitor model name from EDID. The IT8295 info    |
| block does not carry the model, GIGABYTE's own software   |
| uses the display's PnP ID (e.g. GBT2704 / GBT2705).       |
\*---------------------------------------------------------*/
static std::string FindAorusModelName()
{
    const char* drm = "/sys/class/drm";
    DIR* d = opendir(drm);
    if(!d)
    {
        return "";
    }

    std::string result;
    struct dirent* e;
    while((e = readdir(d)) && result.empty())
    {
        std::string edid_path = std::string(drm) + "/" + e->d_name + "/edid";
        std::ifstream f(edid_path, std::ios::binary);
        if(!f)
        {
            continue;
        }
        std::vector<unsigned char> edid((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        if(edid.size() < 128)
        {
            continue;
        }

        /* Manufacturer ID "GBT" = 0x1C 0xF4 */
        char mfg[4] = {
            (char)('@' + ((edid[8] >> 2) & 0x1F)),
            (char)('@' + (((edid[8] & 0x03) << 3) | (edid[9] >> 5))),
            (char)('@' + (edid[9] & 0x1F)),
            0
        };
        if(std::string(mfg) != "GBT")
        {
            continue;
        }

        /* Monitor name descriptor (tag 0xFC) */
        for(int off = 54; off <= 108; off += 18)
        {
            if(edid[off] == 0 && edid[off + 1] == 0 && edid[off + 3] == 0xFC)
            {
                std::string name;
                for(int i = 5; i < 18; i++)
                {
                    char c = (char)edid[off + i];
                    if(c == '\n' || c == 0) break;
                    name += c;
                }
                while(!name.empty() && name.back() == ' ') name.pop_back();
                result = name;
                break;
            }
        }
    }
    closedir(d);
    return result;
}

/*---------------------------------------------------------*\
| Device callbacks                                          |
\*---------------------------------------------------------*/
static void ApplyDevice(AorusMonitorDevice* dev)
{
    RGBControllerInterface* rgb = dev->rgb;
    AorusMonitorController* ctl = dev->controller.get();

    if(!rgb || !ctl)
    {
        return;
    }

    int                 active  = rgb->GetActiveMode();
    if(active < 0)
    {
        return;
    }
    unsigned int        m       = (unsigned int)active;
    std::string         name    = rgb->GetModeName(m);
    unsigned int        flags   = rgb->GetModeFlags(m);
    unsigned int        speed   = (flags & MODE_FLAG_HAS_SPEED)      ? rgb->GetModeSpeed(m)      : 0;
    unsigned int        bright  = (flags & MODE_FLAG_HAS_BRIGHTNESS) ? rgb->GetModeBrightness(m) : AORUS_MONITOR_BRIGHTNESS_MAX;
    unsigned int        cmode   = rgb->GetModeColorMode(m);

    aorus_monitor_effect effect;
    if(name == "Direct" || name == "Static")    effect = AORUS_MONITOR_EFFECT_STATIC;
    else if(name == "Breathing")                effect = AORUS_MONITOR_EFFECT_BREATHING;
    else if(name == "Flashing")                 effect = AORUS_MONITOR_EFFECT_FLASH;
    else if(name == "Double Flash")             effect = AORUS_MONITOR_EFFECT_DOUBLE_FLASH;
    else if(name == "Spectrum Cycle")           effect = AORUS_MONITOR_EFFECT_COLOR_CYCLE;
    else                                        effect = AORUS_MONITOR_EFFECT_OFF;

    unsigned int zones = rgb->GetZoneCount();
    for(unsigned int z = 0; z < zones; z++)
    {
        RGBColor c = 0;
        if(cmode == MODE_COLORS_PER_LED)
        {
            c = rgb->GetColor(rgb->GetZoneStartIndex(z));
        }
        else if(cmode == MODE_COLORS_MODE_SPECIFIC && rgb->GetModeColorsCount(m) > 0)
        {
            c = rgb->GetModeColor(m, 0);
        }

        uint32_t rgb24 = (RGBGetRValue(c) << 16) | (RGBGetGValue(c) << 8) | RGBGetBValue(c);
        ctl->SetZoneEffect(ctl->UsesSingleZoneCommands() ? -1 : (int)z, effect, rgb24, speed, bright);

        if(ctl->UsesSingleZoneCommands())
        {
            break;
        }
    }
    ctl->Apply();
}

static void CB_UpdateLEDs(void* obj)               { ApplyDevice((AorusMonitorDevice*)obj); }
static void CB_UpdateZoneLEDs(void* obj, int)      { ApplyDevice((AorusMonitorDevice*)obj); }
static void CB_UpdateSingleLED(void* obj, int)     { ApplyDevice((AorusMonitorDevice*)obj); }
static void CB_UpdateMode(void* obj)               { ApplyDevice((AorusMonitorDevice*)obj); }
static void CB_SaveMode(void* obj)                 { ApplyDevice((AorusMonitorDevice*)obj); }

static mode MakeMode(const char* name, int value, unsigned int flags, unsigned int color_mode)
{
    mode m;
    m.name       = name;
    m.value      = value;
    m.flags      = flags;
    m.color_mode = color_mode;

    if(flags & MODE_FLAG_HAS_SPEED)
    {
        m.speed_min = AORUS_MONITOR_SPEED_MIN;
        m.speed_max = AORUS_MONITOR_SPEED_MAX;
        m.speed     = 2;
    }
    if(flags & MODE_FLAG_HAS_BRIGHTNESS)
    {
        m.brightness_min = AORUS_MONITOR_BRIGHTNESS_MIN;
        m.brightness_max = AORUS_MONITOR_BRIGHTNESS_MAX;
        m.brightness     = AORUS_MONITOR_BRIGHTNESS_MAX;
    }
    if(flags & MODE_FLAG_HAS_MODE_SPECIFIC_COLOR)
    {
        m.colors_min = 1;
        m.colors_max = 1;
        m.colors.resize(1, ToRGBColor(0, 0, 255));
    }
    return m;
}

/*---------------------------------------------------------*\
| Plugin                                                    |
\*---------------------------------------------------------*/
AorusMonitorPlugin::~AorusMonitorPlugin()
{
}

OpenRGBPluginInfo AorusMonitorPlugin::GetPluginInfo()
{
    OpenRGBPluginInfo info;
    info.Name            = PLUGIN_NAME;
    info.Description     = PLUGIN_DESCRIPTION;
    info.Version         = "1.0.0";
    info.Commit          = "";
    info.URL             = "";
    info.Location        = OPENRGB_PLUGIN_LOCATION_INFORMATION;
    info.Label           = "AORUS Monitor";
    info.ProtocolVersion = 0;
    return info;
}

unsigned int AorusMonitorPlugin::GetPluginAPIVersion()
{
    return OPENRGB_PLUGIN_API_VERSION;
}

void AorusMonitorPlugin::Load(OpenRGBPluginAPIInterface* plugin_api_ptr)
{
    api = plugin_api_ptr;

    if(hid_init() == 0)
    {
        hid_initialized = true;
    }

    DetectDevices();
}

void AorusMonitorPlugin::DetectDevices()
{
    std::lock_guard<std::mutex> lock(devices_mutex);

    std::vector<std::string> paths = AorusMonitorController::Enumerate();
    AM_LOG(api, 3, "[AORUS Monitor] Found %d IT8295 controller(s)", (int)paths.size());

    std::string model = FindAorusModelName();

    for(const std::string& path : paths)
    {
        auto dev        = std::make_unique<AorusMonitorDevice>();
        dev->api        = api;
        dev->controller = std::make_unique<AorusMonitorController>(path);

        if(!dev->controller->IsOpen())
        {
            AM_LOG(api, 2, "[AORUS Monitor] Could not open %s (check udev rule / permissions)", path.c_str());
            continue;
        }

        const aorus_monitor_info& inf = dev->controller->GetInfo();
        AM_LOG(api, 3, "[AORUS Monitor] %s: product '%s', fw %s, %s", path.c_str(),
               inf.product_string.c_str(), dev->controller->GetFirmwareString().c_str(),
               dev->controller->UsesSingleZoneCommands() ? "single-zone protocol" : "2-zone protocol");

        RGBController_Setup setup;
        setup.name          = model.empty() ? "GIGABYTE AORUS Monitor" : ("GIGABYTE " + model);
        setup.vendor        = "GIGABYTE";
        setup.description   = "AORUS monitor rear RGB (ITE IT8295, " + inf.product_string + ")";
        setup.version       = dev->controller->GetFirmwareString();
        setup.serial        = dev->controller->GetSerial();
        setup.location      = "HID: " + path;
        setup.configuration = "";
        setup.type          = DEVICE_TYPE_MONITOR;
        setup.flags         = CONTROLLER_FLAG_VIRTUAL;
        setup.active_mode   = 1;    /* Static */

        unsigned int per_led    = MODE_FLAG_HAS_PER_LED_COLOR;
        unsigned int specific   = MODE_FLAG_HAS_MODE_SPECIFIC_COLOR;

        setup.modes.push_back(MakeMode("Direct",         AORUS_MONITOR_EFFECT_STATIC,       per_led,                                          MODE_COLORS_PER_LED));
        setup.modes.push_back(MakeMode("Static",         AORUS_MONITOR_EFFECT_STATIC,       per_led | MODE_FLAG_HAS_BRIGHTNESS | MODE_FLAG_MANUAL_SAVE, MODE_COLORS_PER_LED));
        setup.modes.push_back(MakeMode("Breathing",      AORUS_MONITOR_EFFECT_BREATHING,    specific | MODE_FLAG_HAS_SPEED,                   MODE_COLORS_MODE_SPECIFIC));
        setup.modes.push_back(MakeMode("Flashing",       AORUS_MONITOR_EFFECT_FLASH,        specific | MODE_FLAG_HAS_SPEED | MODE_FLAG_HAS_BRIGHTNESS, MODE_COLORS_MODE_SPECIFIC));
        setup.modes.push_back(MakeMode("Double Flash",   AORUS_MONITOR_EFFECT_DOUBLE_FLASH, specific | MODE_FLAG_HAS_SPEED | MODE_FLAG_HAS_BRIGHTNESS, MODE_COLORS_MODE_SPECIFIC));
        setup.modes.push_back(MakeMode("Spectrum Cycle", AORUS_MONITOR_EFFECT_COLOR_CYCLE,  MODE_FLAG_HAS_SPEED | MODE_FLAG_HAS_BRIGHTNESS,   MODE_COLORS_NONE));
        setup.modes.push_back(MakeMode("Off",            AORUS_MONITOR_EFFECT_OFF,          0,                                                MODE_COLORS_NONE));

        unsigned int zone_count = dev->controller->GetZoneCount();
        for(unsigned int z = 0; z < zone_count; z++)
        {
            zone zn;
            zn.name       = (zone_count == 1) ? "Rear Lighting" : ("Rear Zone " + std::to_string(z + 1));
            zn.type       = ZONE_TYPE_SINGLE;
            zn.leds_count = 1;
            zn.leds_min   = 1;
            zn.leds_max   = 1;
            setup.zones.push_back(zn);

            led l;
            l.name  = zn.name;
            l.value = z;
            setup.leds.push_back(l);
        }

        setup.object_ptr                                   = dev.get();
        setup.DeviceConfigureZone                          = nullptr;
        setup.DeviceUpdateLEDs                             = CB_UpdateLEDs;
        setup.DeviceUpdateZoneLEDs                         = CB_UpdateZoneLEDs;
        setup.DeviceUpdateSingleLED                        = CB_UpdateSingleLED;
        setup.DeviceUpdateMode                             = CB_UpdateMode;
        setup.DeviceSaveMode                               = CB_SaveMode;
        setup.DeviceUpdateZoneMode                         = nullptr;
        setup.DeviceUpdateDeviceSpecificConfiguration      = nullptr;
        setup.DeviceUpdateDeviceSpecificZoneConfiguration  = nullptr;

        dev->rgb = api->CreateVirtualRGBController(&setup);
        if(!dev->rgb)
        {
            continue;
        }

        /*-------------------------------------------------*\
        | Register from a background thread, as the API     |
        | recommends when called during plugin loading      |
        \*-------------------------------------------------*/
        api->RegisterVirtualRGBControllerInThread(dev->rgb);
        devices.push_back(std::move(dev));
    }
}

void AorusMonitorPlugin::RemoveDevices()
{
    std::lock_guard<std::mutex> lock(devices_mutex);

    for(auto& dev : devices)
    {
        if(dev->rgb && api)
        {
            api->UnregisterVirtualRGBController(dev->rgb);
            api->DeleteVirtualRGBController(dev->rgb);
            dev->rgb = nullptr;
        }
    }
    devices.clear();
}

/*---------------------------------------------------------*\
| Information tab. OpenRGB's GUI does not null-check the    |
| widget for a tab location, so this must return a widget.  |
\*---------------------------------------------------------*/
QWidget* AorusMonitorPlugin::GetWidget()
{
    QWidget*     w      = new QWidget();
    QVBoxLayout* layout = new QVBoxLayout(w);
    QString      text;

    {
        std::lock_guard<std::mutex> lock(devices_mutex);

        if(devices.empty())
        {
            text = "No AORUS monitor RGB controller (USB 048d:8295) was found or it could not be opened.<br>"
                   "Make sure the monitor's USB upstream cable is connected and the udev rule is installed.";
        }
        for(auto& dev : devices)
        {
            AorusMonitorController* c = dev->controller.get();
            text += QString("<b>%1</b><br>Controller: ITE IT8295 (%2)<br>Firmware: %3<br>Protocol: %4<br>Device: %5<br><br>")
                        .arg(dev->rgb ? QString::fromStdString(dev->rgb->GetName()) : "AORUS Monitor")
                        .arg(QString::fromStdString(c->GetInfo().product_string))
                        .arg(QString::fromStdString(c->GetFirmwareString()))
                        .arg(c->UsesSingleZoneCommands() ? "single zone" : "2 zones")
                        .arg(QString::fromStdString(c->GetPath()));
        }
    }

    QLabel* label = new QLabel(text, w);
    label->setTextFormat(Qt::RichText);
    label->setWordWrap(true);
    label->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    layout->addWidget(label);
    layout->addStretch();
    return w;
}
QMenu*   AorusMonitorPlugin::GetTrayMenu()                     { return nullptr; }

void AorusMonitorPlugin::Unload()
{
    RemoveDevices();

    if(hid_initialized)
    {
        /* OpenRGB itself also uses hidapi; leave hid_exit() to the host */
        hid_initialized = false;
    }
}

void            AorusMonitorPlugin::OnProfileAboutToLoad()                   {}
void            AorusMonitorPlugin::OnProfileLoad(nlohmann::json)             {}
nlohmann::json  AorusMonitorPlugin::OnProfileSave()                          { return nlohmann::json(); }
unsigned char*  AorusMonitorPlugin::OnSDKCommand(unsigned int, unsigned char*, unsigned int*) { return nullptr; }
void            AorusMonitorPlugin::ProfileManagerUpdated(unsigned int)       {}
void            AorusMonitorPlugin::ResourceManagerUpdated(unsigned int)      {}
void            AorusMonitorPlugin::SettingsManagerUpdated(unsigned int)      {}
