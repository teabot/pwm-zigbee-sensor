# Agent notes — ESP32-H2 Zigbee development

Lessons learned during build, debug and HA integration. Intended as a quick-reference for future sessions.

---

## Build

- **Toolchain**: ESP-IDF 5.5.3. Source with `source ~/esp/esp-idf/export.sh` before any `idf.py` command.
- **Always delete `sdkconfig`** before the first build or after changing `sdkconfig.defaults` — stale values survive and cause confusing failures.
- **Component manager**: `idf_component.yml` drives automatic fetching of `esp-zigbee-lib` and `esp-zboss-lib`. Components land in `managed_components/` (gitignored). A clean checkout will re-fetch on first build.
- **Binary size**: `CONFIG_LOG_DEFAULT_LEVEL_DEBUG` inflates the binary enough to overflow the default partition layout. Keep it out of `sdkconfig.defaults`; use `ESP_LOGD` sparingly and only enable debug logging at runtime when needed.
- **Partition table**: ZBOSS requires a `zb_storage` FAT partition. Without it the stack logs `Failed to find zb_storage partition` and cannot join any network. See `partitions.csv` — do not shrink or remove that partition.

---

## Flashing

- Use `idf.py erase-flash flash` after any partition table change, or when the device has stale Zigbee credentials that are causing join failures. This wipes NVS and ZBOSS NVRAM — the device must re-join ZHA afterwards.
- Subsequent flashes (`idf.py flash`) preserve NVS / network keys — use this for firmware-only updates.

---

## Zigbee stack

### Device role
- Use **End Device** (`CONFIG_ZB_ZED=y`, `ESP_ZB_DEVICE_TYPE_ED`). Router mode adds complexity to the ZHA interview and is unnecessary for a sensor that will always be mains-powered and within range of the coordinator.
- `ZB_ED_CONFIG()` macro is defined locally in `zigbee.c` — the SDK does not provide a ready-made equivalent for the ED role (unlike the router macros).

### Radio / host config
- `ESP_ZB_DEFAULT_RADIO_CONFIG()` is not defined in ESP-IDF 5.5.x headers. Initialise the platform config struct explicitly:
  ```c
  .radio_config = { .radio_mode = ZB_RADIO_MODE_NATIVE },
  .host_config  = { .host_connection_mode = ZB_HOST_CONNECTION_MODE_NONE },
  ```

### Signal handler
- `esp_zb_app_signal_handler` is **mandatory** — the linker will fail without it. Handle at minimum: `SKIP_STARTUP`, `DEVICE_FIRST_START`, `DEVICE_REBOOT`, `STEERING`, `LEAVE`.
- On steering failure, retry with a 1-second scheduler alarm rather than a tight loop.

### ZCL strings
- Manufacturer and model strings are ZCL pascal-style: first byte is the string length, no null terminator.
  ```c
  static char manufacturer[] = "\x06" "Teabot";   // 6 chars
  static char model_id[]     = "\x09" "PWMSensor"; // 9 chars
  ```
  Getting this wrong produces garbled strings in ZHA (e.g. `"IY"` instead of `"DIY"`).

### Cluster choice for sensor entities
- **Analog Input cluster (0x000C)** does *not* auto-create sensor entities in ZHA for unknown devices — ZHA requires a full custom HA component (not just a quirk) to handle it.
- **Relative Humidity cluster (0x0405)** is natively handled by ZHA and automatically creates `sensor` entities. Use this as the transport for any 0–100 % value:
  ```
  measured_value (uint16) = duty_percent × 100
  ```
  ZHA divides by 100 to display, so 5000 → 50.00 %.
- `esp_zb_humidity_meas_cluster_cfg_t` fields are `measured_value`, `min_value`, `max_value` (not `min_measured_value` / `max_measured_value`).

---

## ZHA quirk

- The quirk in `quirk/pwm_zigbee_sensor.py` provides device identity matching (`Teabot` / `PWMSensor`). Its primary value is showing `Quirk: pwm_zigbee_sensor.PWMZigbeeSensor` in the ZHA device page and ensuring correct cluster handling.
- **Do not subclass `RelativeHumidity` in the quirk replacement.** A custom cluster subclass (`class PwmDutyCycleCluster(CustomCluster, RelativeHumidity)`) prevents ZHA from finding the sensor entity description for that cluster — entities disappear. Use the plain `RelativeHumidity` class.
- Deploy to `/config/custom_zha_quirks/` and set `zha: custom_quirks_path: /config/custom_zha_quirks` in `configuration.yaml`. Restart HA after any quirk change.
- After a firmware re-flash with `erase-flash`, delete the device from ZHA and re-pair — the old entry will have stale endpoints.

---

## Debugging

- `idf.py monitor` requires a real TTY — it will not work from a non-interactive shell.
- Key log tags: `zigbee`, `pwm_cap`, `main`.
- ZHA device page → "Zigbee info" shows LQI. Below ~20 the interview will stall or fail; move the device closer to the coordinator.
- If entities are missing after pairing, check: (a) quirk deployed and HA restarted, (b) no custom cluster subclass in quirk replacement, (c) correct cluster IDs in firmware.

---

## HA automation testing without hardware

See `docs/testing.md` for the full procedure. In brief:

- Create `input_number` helpers (`mirror_ww_duty_cycle`, `mirror_cw_duty_cycle`, range 0–100 %, step 0.5) as stand-ins for the real sensor entities.
- Point the automation triggers and variable templates at the helpers.
- Set values via HA MCP (`input_number.set_value`) and read back light state with `ha_get_state`.
- Switch triggers/variables to `sensor.diy_pwmsensor_ch1/ch2_duty_cycle` when hardware is installed.
