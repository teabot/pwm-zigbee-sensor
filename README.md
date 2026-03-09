# pwm-zigbee-sensor

## Project overview

This project is part of a smart bathroom mirror lighting system.

The mirror has an existing touch interface that drives WW (warm white) and CW (cool white) LED strips directly, outputting two PWM signals — one per channel — to control brightness and colour temperature. Rather than replace the touch interface, this project intercepts those PWM signals and translates them into Zigbee sensor values, integrating the mirror controls into a broader ZHA-based lighting system in Home Assistant.

The mirror's LED strips are driven by a separate Zigbee LED controller instead of the original driver. This means:

- The mirror lights can be activated by **room switches** via HA automations, independently of the touch interface
- The **mirror touch interface** continues to work as before, but now controls lights across the room (or triggers automations) rather than driving the LED strips directly
- The whole system is visible and automatable in **Home Assistant** via ZHA

This firmware runs on an **ESP32-H2** dev board installed inside the mirror housing. It reads the two PWM outputs from the touch controller, measures their duty cycles, and reports them to ZHA as sensor entities. HA automations then use those values to control the Zigbee LED controller driving the mirror strips, as well as any other lights in the room.

---

## What it does

- Captures PWM signals on **GPIO 4** (CH1 = WW) and **GPIO 5** (CH2 = CW) using the MCPWM hardware capture peripheral
- Measures duty cycle as a percentage (0.0–100.0 %)
- Joins a Zigbee network as an **End Device** and reports duty cycle via the **Relative Humidity cluster** (0x0405), which ZHA auto-discovers as sensor entities
- Only sends a Zigbee update when the duty cycle changes by more than **0.5 %** (dead-band) to avoid flooding the network

## Hardware

### ESP32-H2 board

| Item | Detail |
|------|--------|
| Board | Waveshare ESP32-H2-Zero Mini |
| SoC | ESP32-H2 (RISC-V, native IEEE 802.15.4) |
| Flash | 4 MB |
| PWM input CH1 (WW) | GPIO 4 |
| PWM input CH2 (CW) | GPIO 5 |
| Zigbee role | End Device |

> GPIO 4 and 5 have internal pull-ups enabled. Floating inputs read as logic-high (100 % duty). Drive from a low-impedance source or disable `pull_up` in `pwm_capture.c`.

### Signal conditioning

The touch controller outputs 12–24 V PWM (≈1 kHz). Each channel is stepped down to 3.3 V logic via a resistor voltage divider before connecting to the ESP32-H2 GPIO:

```
PWM (12–24V) ──[ R1: 6.8kΩ ]──┬── GPIO
                               [ R2: 1kΩ ]
                               [C: 100nF]  (noise suppression, across R2)
                                │
                               GND
```

| Component | Value | Purpose |
|-----------|-------|---------|
| R1 | 6.8 kΩ | Series dropper |
| R2 | 1 kΩ | Lower leg of divider |
| C | 100 nF | Noise filter across R2 |

Output voltage at GPIO: 1.0 V (12 V input) – 3.2 V (24 V input). Safe for 3.3 V GPIO.

### Power supply

A 12–24 V → 5 V buck converter module feeds the board's 5 V pin. The ESP32-H2-Zero draws ≈100–200 mA — well within the 1 A available from the LED driver supply.

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

1. Flash the firmware (`erase-flash` recommended for a clean pair)
2. Copy `quirk/pwm_zigbee_sensor.py` to `/config/custom_zha_quirks/` on your HA instance
3. Add to `configuration.yaml` if not already present:
   ```yaml
   zha:
     custom_quirks_path: /config/custom_zha_quirks
   ```
4. Restart Home Assistant
5. **Settings → Devices & Services → Zigbee Home Automation → Add Device** (opens permit join)
6. The device joins within a few seconds and ZHA runs the interview automatically

### Entities

ZHA creates two sensor entities (rename via the entity edit UI if needed):

| Entity ID | Channel | Signal | GPIO |
|-----------|---------|--------|------|
| `sensor.diy_pwmsensor_ch1_duty_cycle` | CH1 | WW (warm white) | GPIO 4 |
| `sensor.diy_pwmsensor_ch2_duty_cycle` | CH2 | CW (cool white) | GPIO 5 |

The friendly name defaults to "Humidity" — a ZHA platform limitation. See [Zigbee cluster choice](#zigbee-cluster-choice).

### Testing without hardware

Use **Developer Tools → States** in HA to set entity values manually and test automations:

| CH1 (WW) | CH2 (CW) | Expected result |
|----------|----------|-----------------|
| `0.0` | `0.0` | Lights off |
| `80.0` | `20.0` | Warm white, ~50 % brightness |
| `20.0` | `80.0` | Cool white, ~50 % brightness |
| `50.0` | `50.0` | Neutral white, 50 % brightness |

## Home Assistant automations

Automation YAML files are stored in `automations/` and should be imported into HA or referenced from `configuration.yaml`.

### Mirror touch → En-suite lights (`automations/mirror_touch_ensuite_lights.yaml`)

**Entity:** `automation.mirror_touch_en_suite_lights`

Controls `light.master_en_suite` (Hue room group: Toilet, Shower head, Shower entrance, Basin) based on the mirror touch controller output.

| Condition | Action |
|-----------|--------|
| Both CH1 and CH2 = 0 % | Turn lights off |
| Either channel > 0 % | Turn lights on with colour temp and brightness derived from WW/CW mix |

**Colour temperature mapping:**

The Hue group supports 2202 K – 6535 K. Colour temp is a weighted average of the two channels:

```
color_temp_kelvin = (WW / total) × 2202 + (CW / total) × 6535
brightness_pct    = (WW + CW) / 2
```

Examples:

| WW | CW | Colour temp | Brightness |
|----|----|-------------|------------|
| 100 | 0 | 2202 K (warmest) | 50 % |
| 0 | 100 | 6535 K (coolest) | 50 % |
| 50 | 50 | 4369 K (neutral) | 50 % |
| 100 | 100 | 4369 K (neutral) | 100 % |

## Project structure

```
.
├── CMakeLists.txt              # Top-level project CMake
├── sdkconfig.defaults          # Kconfig defaults (Zigbee ED, custom partitions)
├── partitions.csv              # Custom partition table (includes zb_storage for ZBOSS NVRAM)
├── quirk/
│   └── pwm_zigbee_sensor.py   # ZHA custom quirk (deploy to /config/custom_zha_quirks/)
├── automations/
│   └── mirror_touch_ensuite_lights.yaml
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

ZHA does not auto-create sensor entities for the **Analog Input** cluster (0x000C) on unknown devices — it requires a full custom HA component, not just a quirk. The **Relative Humidity** cluster (0x0405) is natively discovered by ZHA and creates a `sensor` entity automatically. The value mapping is:

```
measured_value (uint16) = duty_percent × 100
```

ZHA divides by 100 to display, so 5000 → 50.00 %.

### ZHA quirk

`quirk/pwm_zigbee_sensor.py` matches the device by manufacturer (`Teabot`) and model (`PWMSensor`), ensuring ZHA correctly identifies the device and applies the right cluster handlers. The quirk uses the standard `RelativeHumidity` cluster — the device identity match is the primary value it provides.

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
# idf.py monitor requires a TTY; run from a real terminal
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
| Entities show 0 % with no PWM source | Inputs floating, then timeout | Expected — connect a PWM source |
| Binary too large | `CONFIG_LOG_DEFAULT_LEVEL_DEBUG` set | Remove debug log level from sdkconfig.defaults |
| Quirk not matching | Quirk not deployed or HA not restarted | Re-check deployment path and restart HA |
