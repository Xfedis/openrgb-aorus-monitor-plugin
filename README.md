# OpenRGB AORUS Monitor Plugin

An [OpenRGB](https://openrgb.org) plugin that controls the rear RGB lighting of
GIGABYTE AORUS monitors. OpenRGB itself does not support these monitors.

Tested on an **AORUS CV27Q** (controller firmware 39.10.0.0) with OpenRGB 1.1 on Linux.
Other AORUS monitors that use the same ITE IT8295 RGB controller (USB ID `048d:8295`)
should work too, but are untested.

> **Note:** this plugin was written with the help of an AI coding assistant.
> It is an independent project, not part of or endorsed by OpenRGB or GIGABYTE.

## Features

- Adds the monitor to OpenRGB as a device with two zones ("Rear Zone 1" and "Rear Zone 2")
- Modes: Direct, Static (per-zone colour + brightness), Breathing, Flashing,
  Double Flash, Spectrum Cycle and Off
- Information tab showing the controller's product string and firmware
- Standalone command-line tool `aorus-monitor-rgb` for use without OpenRGB

## Requirements

- OpenRGB 1.1 (plugin API version 5)
- Qt 6, hidapi (`hidapi-hidraw`), CMake 3.16+ and a C++17 compiler
- A checkout of the OpenRGB source tree matching your installed version (used for headers only)

## Build and install

```sh
git clone --branch release_1.1 https://gitlab.com/CalcProgrammer1/OpenRGB.git ~/src/OpenRGB
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DOPENRGB_SOURCE_DIR=$HOME/src/OpenRGB
cmake --build build -j
cmake --install build
```

This installs the plugin to `~/.config/OpenRGB/plugins/` and the CLI to `~/.local/bin/`.

Give your user access to the device with the included udev rule:

```sh
sudo cp udev/60-aorus-monitor-rgb.rules /etc/udev/rules.d/
sudo udevadm control --reload-rules
sudo udevadm trigger --subsystem-match=hidraw
```

## Usage notes

- OpenRGB loads plugins only in the **GUI**, not when it runs as a pure command-line tool.
- Plugin devices are hidden from SDK clients and the `openrgb --client` CLI unless
  **Settings → Server → Serve All Controllers** is enabled
  (`"Server": {"all_controllers": true}` in `OpenRGB.json`). With it enabled:

  ```sh
  openrgb --client 127.0.0.1 -d "CV27Q" -m static -c 0000FF
  ```

### Standalone CLI

```sh
aorus-monitor-rgb info
aorus-monitor-rgb static 0000FF [brightness 0-9] [zone 0|1]
aorus-monitor-rgb breathing|flash|dflash FF0000 [speed 0-5]
aorus-monitor-rgb cycle [speed 0-5]
aorus-monitor-rgb off
```

## Protocol

The monitor's built-in USB hub exposes an ITE IT8295 HID device (`048d:8295`).
It uses vendor usage page `0xFF89`, with 64-byte feature report `0xCC`. The commands are
almost the same as the IT8297 "RGB Fusion 2" controller on GIGABYTE motherboards:

| Command | Meaning |
|---|---|
| `CC 60` | Request controller info (read back with a get-feature report) |
| `CC 32 00` | Disable the digital strip engine |
| `CC 20+n <32-byte effect block>` | Set the effect for zone *n* (older firmware, 2 zones) |
| `CC 21 <32-byte effect block>` | Set the effect for all LEDs (newer firmware) |
| `CC 28 03` | Apply |

The 32-byte effect block is: zone mask (u32), zone mask 2 (u32), reserved (u8),
mode (u8: 1 static, 2 pulse, 3 flash, 4 cycle), max/min brightness (u8, u8),
colour 0 and colour 1 (u32 `0x00RRGGBB`), four u16 timing values and four control bytes.
All values are little-endian. See `src/AorusMonitorController.cpp` for the exact values per effect.

## License

GPL-2.0-or-later, the same as OpenRGB. See [LICENSE](LICENSE).
