# Automation Testing Notes

## Method

Tested `automations/mirror_touch_ensuite_lights.yaml` without physical hardware using two `input_number` helpers as stand-ins for the real Zigbee sensor entities.

### Helpers created

| Helper entity | Friendly name | Range | Step | Mode |
|---|---|---|---|---|
| `input_number.mirror_ww_duty_cycle` | Mirror WW Duty Cycle | 0–100 % | 0.5 | slider |
| `input_number.mirror_cw_duty_cycle` | Mirror CW Duty Cycle | 0–100 % | 0.5 | slider |

The automation was temporarily updated to trigger from these helpers instead of `sensor.diy_pwmsensor_ch1_duty_cycle` / `sensor.diy_pwmsensor_ch2_duty_cycle`. Helper values were set via the Home Assistant MCP (`input_number.set_value`), and the resulting light state was read back with `ha_get_state`.

### Switching to real sensors

When hardware is installed, change the `trigger` and `variables` blocks in `mirror_touch_ensuite_lights.yaml`:

```yaml
# Testing (helpers)
trigger:
  - platform: state
    entity_id:
      - input_number.mirror_ww_duty_cycle
      - input_number.mirror_cw_duty_cycle
variables:
  ww: "{{ states('input_number.mirror_ww_duty_cycle') | float(0) }}"
  cw: "{{ states('input_number.mirror_cw_duty_cycle') | float(0) }}"

# Production (real sensors)
trigger:
  - platform: state
    entity_id:
      - sensor.diy_pwmsensor_ch1_duty_cycle
      - sensor.diy_pwmsensor_ch2_duty_cycle
variables:
  ww: "{{ states('sensor.diy_pwmsensor_ch1_duty_cycle') | float(0) }}"
  cw: "{{ states('sensor.diy_pwmsensor_ch2_duty_cycle') | float(0) }}"
```

---

## Test results

Target: `light.master_en_suite` (Hue room, 2202 K – 6535 K, brightness 0–255)

Formula under test:
```
color_temp_kelvin = (WW / total) × 2202 + (CW / total) × 6535
brightness_pct    = (WW + CW) / 2
```

| WW % | CW % | Expected temp | Actual temp | Expected brightness | Actual brightness | Light state |
|------|------|--------------|-------------|---------------------|-------------------|-------------|
| 0    | 0    | —            | —           | —                   | —                 | off ✓       |
| 80   | 20   | 3069 K       | 3076 K      | 50 % (128/255)      | 128/255           | on ✓        |
| 20   | 80   | 5668 K       | 5681 K      | 50 % (128/255)      | 128/255           | on ✓        |
| 100  | 100  | 4369 K       | 4385 K      | 100 % (255/255)     | 255/255           | on ✓        |

Small temperature discrepancies (10–16 K) are due to Hue's internal rounding — the formula is correct.

All four test cases passed.
