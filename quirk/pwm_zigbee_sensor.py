"""ZHA quirk for Teabot PWM Zigbee Sensor (ESP32-H2)."""

from zigpy.profiles import zha
from zigpy.quirks import CustomDevice
from zigpy.zcl.clusters.general import Basic, Identify
from zigpy.zcl.clusters.measurement import RelativeHumidity

from zhaquirks.const import (
    DEVICE_TYPE,
    ENDPOINTS,
    INPUT_CLUSTERS,
    MANUFACTURER,
    MODEL,
    OUTPUT_CLUSTERS,
    PROFILE_ID,
)

_DEVICE_TYPE = 0x0307  # HA profile: Humidity Sensor


class PWMZigbeeSensor(CustomDevice):
    """Two-channel PWM duty cycle sensor (Teabot / ESP32-H2).

    Duty cycle (0.0–100.0 %) is transported via the Relative Humidity cluster
    (measured_value = duty_percent * 100). ZHA divides by 100 to display.
    Rename the entities in HA to reflect their actual meaning.
    """

    signature = {
        MANUFACTURER: "Teabot",
        MODEL: "PWMSensor",
        ENDPOINTS: {
            1: {
                PROFILE_ID: zha.PROFILE_ID,
                DEVICE_TYPE: _DEVICE_TYPE,
                INPUT_CLUSTERS: [
                    Basic.cluster_id,
                    Identify.cluster_id,
                    RelativeHumidity.cluster_id,  # 0x0405
                ],
                OUTPUT_CLUSTERS: [],
            },
            2: {
                PROFILE_ID: zha.PROFILE_ID,
                DEVICE_TYPE: _DEVICE_TYPE,
                INPUT_CLUSTERS: [
                    Basic.cluster_id,
                    Identify.cluster_id,
                    RelativeHumidity.cluster_id,
                ],
                OUTPUT_CLUSTERS: [],
            },
        },
    }

    replacement = {
        ENDPOINTS: {
            1: {
                PROFILE_ID: zha.PROFILE_ID,
                DEVICE_TYPE: _DEVICE_TYPE,
                INPUT_CLUSTERS: [Basic, Identify, RelativeHumidity],
                OUTPUT_CLUSTERS: [],
            },
            2: {
                PROFILE_ID: zha.PROFILE_ID,
                DEVICE_TYPE: _DEVICE_TYPE,
                INPUT_CLUSTERS: [Basic, Identify, RelativeHumidity],
                OUTPUT_CLUSTERS: [],
            },
        },
    }
