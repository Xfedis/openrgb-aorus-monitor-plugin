/*---------------------------------------------------------*\
| AorusMonitorPlugin.h                                      |
|                                                           |
|   OpenRGB plugin exposing GIGABYTE AORUS monitor rear     |
|   RGB lighting (IT8295 controller, 048d:8295) as an       |
|   OpenRGB device.                                         |
|                                                           |
|   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#pragma once

#include <QObject>
#include <QtPlugin>
#include <memory>
#include <mutex>
#include <vector>

#include "OpenRGBPluginInterface.h"
#include "AorusMonitorController.h"

struct AorusMonitorDevice
{
    std::unique_ptr<AorusMonitorController> controller;
    RGBControllerInterface*                 rgb             = nullptr;
    OpenRGBPluginAPIInterface*              api             = nullptr;
};

class AorusMonitorPlugin : public QObject, public OpenRGBPluginInterface
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID OpenRGBPluginInterface_IID FILE "AorusMonitorPlugin.json")
    Q_INTERFACES(OpenRGBPluginInterface)

public:
    ~AorusMonitorPlugin() override;

    OpenRGBPluginInfo   GetPluginInfo() override;
    unsigned int        GetPluginAPIVersion() override;

    void                Load(OpenRGBPluginAPIInterface* plugin_api_ptr) override;
    QWidget*            GetWidget() override;
    QMenu*              GetTrayMenu() override;
    void                Unload() override;
    void                OnProfileAboutToLoad() override;
    void                OnProfileLoad(nlohmann::json profile_data) override;
    nlohmann::json      OnProfileSave() override;
    unsigned char*      OnSDKCommand(unsigned int pkt_id, unsigned char* pkt_data, unsigned int* pkt_size) override;

    void                ProfileManagerUpdated(unsigned int update_reason) override;
    void                ResourceManagerUpdated(unsigned int update_reason) override;
    void                SettingsManagerUpdated(unsigned int update_reason) override;

private:
    OpenRGBPluginAPIInterface*                          api = nullptr;
    std::vector<std::unique_ptr<AorusMonitorDevice>>    devices;
    std::mutex                                          devices_mutex;
    bool                                                hid_initialized = false;

    void                DetectDevices();
    void                RemoveDevices();
};
