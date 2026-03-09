# pwm-zigbee-sensor

ESP32-H2 firmware that measures the duty cycle of two PWM signals and exposes them as Zigbee sensor entities in Home Assistant via ZHA.

## What it does

- Captures PWM signals on **GPIO 4** (CH0) and **GPIO 5** (CH1) using the MCPWM hardware capture peripheral
- Measures duty cycle as a percentage (0.0–100.0 %)
- Joins a Zigbee network as an **End Device** and reports duty cycle via the **Relative Humidity cluster** (0x0405), which ZHA auto-discovers as sensor entities
- Only sends a Zigbee update when the duty cycle changes by more than **0.5 %** (dead-band) to avoid flooding the network

## Hardware

| Item | Detail |
|------|--------|
| SoC | ESP32-H2 (RISC-V, native IEEE 802.15.4) |
| Flash | 4 MB |
| PWM input CH0 | GPIO 4 (internal pull-up enabled) |
| PWM input CH1 | GPIO 5 (internal pull-up enabled) |
| Zigbee role | End Device |

> The internal pull-ups mean floating inputs read as logic-high (100 % duty). Drive the inputs from a low-impedance source or disable `pull_up` in `pwm_capture.c` if that causes issues.

## Dependencies

| Component | Version |
|-----------|---------|
| ESP-IDF | 5.5.3 |
| espressif/esp-zigbee-lib | ≥ 1.4.0 (tested 1.6.8) |
| espressif/esp-zboss-lib | ≥ 1.4.0 (tested 1.6.4) |

Components are fetched automatically by the ESP-IDF component manager on first build.

## Building

```bash
# Source ESP-IDF environment (adjust path if needed)
source ~/esp/esp-idf/export.sh

cd esp32-h2

# Delete any stale sdkconfig before first build or after changing sdkconfig.defaults
rm -f sdkconfig

idf.py build
```

## Flashing

```bash
# Full erase (recommended on first flash or after partition table changes)
idf.py erase-flash flash

# Subsequent flashes (preserves NVS / Zigbee network keys)
idf.py flash
```

> After `erase-flash` the device loses its Zigbee network credentials and must re-join.

## Pairing with ZHA (Home Assistant)

1. Flash the firmware (erase-flash recommended for a clean pair)
2. In Home Assistant: **Settings → Devices & Services → Zigbee Home Automation → Add Device** (opens permit join)
3. The device joins within a few seconds and ZHA runs the interview automatically
4. Two sensor entities appear — one per channel — labelled as humidity sensors (see note below)

### Renaming entities

ZHA creates the entities as `sensor.<device>_humidity` and `sensor.<device>_humidity_2` because we use the Relative Humidity cluster (the only ZHA-auto-discovered cluster that maps cleanly to a 0–100 % value). Rename them in HA:

**Settings → Devices & Services → ZHA → Devices → [device] → click entity → pencil icon**

Suggested names: **PWM CH0 Duty** / **PWM CH1 Duty**

## Project structure

```
.
├── CMakeLists.txt              # Top-level project CMake
├── sdkconfig.defaults          # Kconfig defaults (Zigbee ED, custom partitions)
├── partitions.csv              # Custom partition table (includes zb_storage for ZBOSS NVRAM)
└── main/
    ├── CMakeLists.txt
    ├── idf_component.yml       # Component manager dependencies
    ├── main.c                  # app_main: init, sensor_task (poll + report loop)
    ├── pwm_capture.h / .c      # MCPWM hardware capture driver
    ├── zigbee.h / .c           # Zigbee stack init, endpoint registration, attribute updates
```

## Key design notes

### PWM capture

Uses the ESP32-H2 **MCPWM Capture** peripheral (`driver/mcpwm_cap.h`), not GPIO interrupt bit-banging. A single capture timer runs at **1 MHz** (1 µs resolution) shared across both channels. An ISR fires on every rising and falling edge and computes:

```
duty = high_ticks / period_ticks × 100
```

Results are mutex-protected. If no edge is seen for **500 ms** (`PWM_CAPTURE_TIMEOUT_MS`) the channel is marked invalid and reports 0 %.

### Zigbee cluster choice

ZHA does not auto-create sensor entities for the **Analog Input** cluster (0x000C) on unknown devices — it requires a custom quirk. The **Relative Humidity** cluster (0x0405) is natively discovered by ZHA and creates a `sensor` entity automatically. The value mapping is:

```
measured_value (uint16) = duty_percent × 100
```

ZHA divides by 100 to display, so 5000 → 50.00 %.

### Reporting

`main.c` polls both channels every 200 ms and calls `zigbee_set_duty()` only when `|delta| ≥ 0.5 %` or on the first reading. `zigbee_set_duty()` acquires the ZBOSS stack lock before writing the attribute.

### Partition table

ZBOSS requires a `zb_storage` FAT partition for its NVRAM (network keys, channel, PAN ID). Without it the stack logs `Failed to find zb_storage partition` and cannot join. The custom `partitions.csv` adds:

| Name | Type | Size |
|------|------|------|
| nvs | data/nvs | 24 KB |
| phy_init | data/phy | 4 KB |
| factory | app | 1792 KB |
| zb_storage | data/fat | 64 KB |
| zb_fct | data/fat | 4 KB |

## Debugging

### Serial monitor

```bash
# idf.py monitor requires a TTY; use it from a real terminal, not a subprocess
idf.py monitor
```

Key log tags:
- `zigbee` — stack init, join/leave events, attribute writes
- `pwm_cap` — capture timer init, per-channel GPIO assignment
- `main` — periodic duty cycle reports with delta values

### Common issues

| Symptom | Likely cause | Fix |
|---------|-------------|-----|
| `Failed to find zb_storage partition` | Wrong partition table flashed | `idf.py erase-flash flash` |
| `Network steering failed` retrying | ZHA permit join not open | Open permit join in ZHA, device joins automatically |
| ZHA interview never completes | Device too far from coordinator | Move closer; check LQI in ZHA device page (needs > ~20) |
| Entities show 0 % with no PWM source | Inputs floating high, then timeout | Expected — connect a PWM source |
| Binary too large | `CONFIG_LOG_DEFAULT_LEVEL_DEBUG` set | Remove debug log level from sdkconfig.defaults |
